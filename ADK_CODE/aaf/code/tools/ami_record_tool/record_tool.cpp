/**
 * @brief 消息查询工具
 * @author 陈志(chenzhi@af.local)
 */

///< linux
#include <stdlib.h> //getenv, setenv
#include <dlfcn.h> //dlopen, dlsym
#include <time.h>
///< cpp std
#include <chrono>
#include <mutex>
#include <memory>
#include <iomanip>

///< boost
#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/locale/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>


///< adk, ami public
#include <ami.h>


///< recorder impl
#include "../ami_recorder/src/record_reader.h"
#include "../ami_recorder/src/record_agent.h"
#include "../src/ami_status_record.h"
#include "../src/master_slave_message_header.h"

#include <adk/util.h>
#include <adk/term_text_colors.h>
#include <adk/property.h>

using namespace ami;
namespace bpo = boost::program_options;
namespace bf = boost::filesystem;
namespace bs = boost::system;
namespace bl = boost::locale;
namespace bal = boost::algorithm;
namespace cc = ami::config::context;
namespace ccr = ami::config::context::recorder;
namespace rcdv = ami::recorder::cdv;
namespace bpt = boost::property_tree;

namespace{
    extern "C" typedef bool (*DumpMsgFunc)(
        const char* msg_data, unsigned long data_len,
        unsigned long* output_len, char* output);
    
    ///< 常量
    constexpr size_t kDefSegPerRow = 8u;
    constexpr const char* kLdLibraryPathEnv = "LD_LIBRARY_PATH";
    constexpr size_t kDefBinaryPrintStep = 1u;
    constexpr size_t kDefBinaryPrintWidth = 16u;

    ///< 全局量
    size_t g_seg_per_row = kDefSegPerRow;
    uint32_t g_binary_print_step = kDefBinaryPrintStep;
    uint32_t g_binary_print_width = kDefBinaryPrintWidth;
    void* g_dl_handler = nullptr;
    DumpMsgFunc g_DumpMsgAsPlainString = nullptr;
    DumpMsgFunc g_DumpMsgAsJson = nullptr;
    Message::SqnType g_discarded_cnt = 0;
    Message::SqnType g_place_holder_cnt = 0;
    bool g_summary = false;
    bool g_show_acked_info = false;
}

LOG_LOCAL(record_qurier)

/***********************************************************
 * ami::Logger
 */
const char* kLabel[6] =
{"Trace", "Debug", "Info", "Warn", "Error", "Fatal"};

class ConsoleLogger : public Logger
{
public:
    ConsoleLogger()
    {
        epoch_ = std::chrono::system_clock::now();
    }
    
    virtual void Log(LogLevel_def level,
                     LogCode code,
                     const std::string& module_name,
                     const std::string& function_name,
                     int32_t src_line,
                     const std::string& message)
    {
        const std::string& now_str = NowStr();
        
        std::lock_guard<std::mutex> lock(mutex_);
        std::clog << now_str << " "
                  << level << "[" << kLabel[level] << "] " << " "
                  << code << " "
                  << getpid() << " "
                  << pthread_self()<< " "
                  << module_name << " "
                  << function_name << " "
                  << src_line << " "
                  << message << std::endl;

        of << now_str << " "
           << level << "[" << kLabel[level] << "] " << " "
           << code << " "
           << getpid() << " "
           << pthread_self()<< " "
           << module_name << " "
           << function_name << " "
           << src_line << " "
           << message << std::endl;
    }
private:
    typedef std::chrono::system_clock ClockType;
    typedef ClockType::time_point TimePointType;
    typedef std::chrono::microseconds DurType;
    
    std::string NowStr()
    {
        const auto& now = ClockType::now();
        std::time_t now_t = ClockType::to_time_t(now);
        char output[128];
        const size_t str_len =
            std::strftime(output, sizeof(output), "%Y%m%d %H%M%S",
                          std::localtime(&now_t));
        return (bl::format("{1}.{2}")
                % std::string(output, str_len)
                % (std::chrono::duration_cast<DurType>
                   (now - epoch_).count() % 1000000)
                ).str();
    }

    std::mutex  mutex_;
    std::ofstream of;
    TimePointType epoch_;
};

//utc time to local time
static std::string UtcToLocalTime(const uint64_t nanoseconds)
{
    static const std::string format_str("CST YYYY-MM-DD HH:MM:SS");
    static const std::size_t format_str_size = format_str.size();
    
    std::string time_str;
    time_str.resize(format_str_size+1); //+1 for null-character
    time_t time = nanoseconds / 1000000000UL;
    uint64_t nanos = nanoseconds % 1000000000UL;
    struct tm *time_info = localtime(&time);
    strftime(&time_str[0], format_str_size+1, "%Z %F %T", time_info); //CST YYYY-MM-DD HH:MM:SS
    time_str[format_str_size] = '.';
    time_str += std::to_string(nanos);
    return time_str;
}

/***********************************************************
 * 参数解析
 */
/**
 * 'opt1' 参数和 'opt2'参数不可以同时指定
 */ 
inline void conflicting_options(const bpo::variables_map& vm, 
                                const char* opt1, const char* opt2)
{
    if (vm.count(opt1) && !vm[opt1].defaulted() 
        && vm.count(opt2) && !vm[opt2].defaulted())
        throw std::logic_error
            (string("conflict optional '") 
             + opt1 + "' and '" + opt2 + "'");
}

/**
 * 'for_what' 参数依赖于 'required_option' 参数
 */ 
inline void option_dependency(const bpo::variables_map& vm,
                              const char* for_what, const char* required_option)
{
    if (vm.count(for_what) && !vm[for_what].defaulted())
        if (vm.count(required_option) == 0)
            throw std::logic_error
                (string("optional '") + for_what 
                 + "' need the optional '" + required_option + "'");
}

inline bool LoadLibrary(const bf::path& file, const bf::path& ld_lib_path)
{
    if(!ld_lib_path.empty())
    {
        const char* origin_ld_lib_path_env =
            std::getenv(kLdLibraryPathEnv);
        std::string ld_lib_path_env;
        if(nullptr == origin_ld_lib_path_env)
        { ld_lib_path_env = ld_lib_path.string(); }
        else
        {
            ld_lib_path_env =
                (bl::format("{1}:{2}")
                 % ld_lib_path.string()
                 % origin_ld_lib_path_env).str();
        }

        setenv(kLdLibraryPathEnv, ld_lib_path_env.c_str(), 1);
    }
    
    g_dl_handler = ::dlopen(file.c_str(), RTLD_NOW);
    if(nullptr == g_dl_handler)
    { 
        std::clog << ADK_RED_TXT("\nload dynamic parse library error: ") << dlerror() << std::endl;
        return false;
    }

    g_DumpMsgAsPlainString =
        (DumpMsgFunc)::dlsym(g_dl_handler, "DumpMsgAsPlainString");
    if (g_DumpMsgAsPlainString != nullptr)
    {
        std::clog << "successfully load the parse interface DumpMsgAsPlainString" << std::endl;
    }
    g_DumpMsgAsJson =
        (DumpMsgFunc)::dlsym(g_dl_handler, "DumpMsgAsJson");
    if (g_DumpMsgAsJson != nullptr)
    {
        std::clog << "successfully load the parse interface DumpMsgAsJson" << std::endl;
    }
    
    return true;
}


/***********************************************************
 * 输出消息
 */
inline void PrintUsage(const bpo::options_description& desc)
{
    std::clog << "record_querier [OPTIONS]... [ROOT_PATH]" << '\n';
    std::clog << "record_querier [OPTIONS]... <CHANNEL_PATH>" << '\n';
    std::clog << desc;
    std::clog << '\n';
    std::clog << "default ROOT_PATHis is: ./" << rcdv::kDataPath << '\n';
}

inline void PrintExample()
{
    std::clog << "\nfor example: \n"
              << "1. show rx summary information for context \n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx -s\n\n"
              << "2. show endpoint or transport information for context \n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 --list transport\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 --list endpoint\n\n"
              << "3. read tx record_data for context\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t tx --transport-name OrderTeOrs_1_CTE\n\n"
              << "4. read rx recorder_data for context \n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx \n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx -b 0 \n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx -b 1 -e 100\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx -b 0 -n 20\n\n"
              << "5. read rx recorder_data for Endpoint of Context \n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --endpoint-id 1234\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --endpoint-name OrderGwTe\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --endpoint-id 1234 -b 0 \n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --endpoint-id OrderGwTe -b 1 -e 100\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --endpoint-id 1234 -b 0 -n 20\n\n"
              << "6. read rx recorder_data for Transport of Context \n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --transport-id 12345\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --transport-name OrderGwTe_1_AGW\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --transport-id 12345 -b 0 \n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --transport-name OrderGwTe -b 1 -e 100\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --transport-id 12345 -b 0 -n 20\n\n"
              << "7. drop rx recorder_data message when recovery\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx -b 0 -n 20 --msg-property d\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --endpoint-id 1234 -b 0 -n 20 --msg-property d\n"
              << "      ./ami_record_tool --path ~/recorder_data -c CTE_1_11_11 -t rx --transport-id 12345 -b 0 -n 20 --msg-property d\n"
              ;
}

inline void PrintMsgAsBinary
(std::ostream& os, const char* data, size_t data_len)
{
    os << adk::MemoryHexDump(data, data_len, g_binary_print_step, g_binary_print_width);
    return;

    typedef uint32_t unit_type;
    const size_t unit_cnt = data_len / sizeof(unit_type);
    const size_t rest_bytes = data_len % sizeof(unit_type);
    const size_t units_per_row = g_seg_per_row;
    const size_t rows = unit_cnt / units_per_row;
    const size_t rest_units = unit_cnt % units_per_row;
    const char* pos = data;
    size_t have_printed = 0;

    const auto old_flag = os.setf(std::ios::hex, std::ios::basefield);
    const auto old_fill = os.fill('0');
    const auto old_width = os.width(8);
    
    os.width(2*sizeof(unit_type));
    for(size_t row = 0; row < rows; row++)
    {
        for(size_t unit = 0; unit < units_per_row; unit++)
        {
            os << boost::endian::native_to_big(*(unit_type*)(pos)) << ' ';
            pos += sizeof(unit_type);
            have_printed += sizeof(unit_type);
        }
        os << '\n';
    }
    
    for(size_t unit = 0; unit < rest_units; unit++)
    {
        os << boost::endian::native_to_big(*(unit_type*)(pos)) << ' ';
        pos += sizeof(unit_type);
        have_printed += sizeof(unit_type);
    }

    os.width(2);
    for(size_t byte = 0; byte < rest_bytes; byte++)
    {
        os << (unit_type)(*pos);
        pos++;
        have_printed++;
    }

    os.width(old_width);
    os.fill(old_fill);
    os.setf(old_flag);
}

static void ShowStatusRecord(std::ostream& os, const char* data, size_t data_len)
{
    ami::StatusCodec st_codec;
    if (st_codec.ParseFromBuffer(data, data_len)
        != ErrorCode::kSuccess)
    {
        os << "!!parse message failed!!";
        return;
    }

    for (uint32_t i = 0; i < st_codec.record_size(); ++i)
    {
        auto* txtp_status = st_codec.record(i);
        os << "status record <" << i << "> : "
           << "endpoint_id <" << txtp_status->endpoint_id << "> "
           << "transport_id <" << txtp_status->transport_id << "> "
           << "acked_msgs <" << txtp_status->last_acked_msg_sqn << ">\n";
    }
    os << std::endl;
}

static char* output_buffer = nullptr;
uint64_t output_buffer_len = 0;

inline void PrintMsgAsJson(std::ostream& os, const char* data, size_t data_len)
{
    unsigned long output_len = 0;
    bool retry = false;
    size_t temp_data_len = data_len;
    const char* temp_data = data;
    do
    {
        if (g_DumpMsgAsJson &&
                (*g_DumpMsgAsJson)(temp_data, temp_data_len, &output_len, nullptr) )
        {
            if ((int64_t)output_len <= 0)
            {
                    os << "!!parse message failed!! output_len " << output_len << " is invalid, code line " << __LINE__; 
                return;
            }

            if (output_len > output_buffer_len)
            {
                delete output_buffer;
                output_buffer = new char[output_len * 2];
                output_buffer_len = output_len * 2;
            }

                if (!(*g_DumpMsgAsJson)(temp_data, temp_data_len, &output_len, output_buffer))
                {
                    // sub the master slave message header
                    // retry!
                    if (!retry)
                    {
                        retry = true;
                        temp_data_len -= sizeof(MasterSlaveMessageHeader);
                        temp_data += sizeof(MasterSlaveMessageHeader);
                        continue;
                    }
                }

            if ((int64_t)output_len <= 0)
            {
                    os << "!!parse message failed!! output_len " << output_len << " is invalid, code line " << __LINE__; 
                return;
            }

            assert(output_buffer_len > 0);
            output_buffer[std::min(output_len, output_buffer_len - 1)] = 0x00;

            os << output_buffer;
        }
        else
        { 
                if (!retry)
                {
                    retry = true;
                    temp_data_len -= sizeof(MasterSlaveMessageHeader);
                    temp_data += sizeof(MasterSlaveMessageHeader);
                    continue;
                }
                os << "!!parse message failed!! code line " << __LINE__; 
        }
        return;
    } while (true);
}

inline void PrintMsgAsPlainString(std::ostream& os, const char* data, size_t data_len)
{
    unsigned long output_len = 0;
    bool retry = false;
    size_t temp_data_len = data_len;
    const char* temp_data = data;
    do
    {
        if (g_DumpMsgAsPlainString &&
            (*g_DumpMsgAsPlainString)(temp_data, temp_data_len, &output_len, nullptr) )
        {
            if ((int64_t)output_len <= 0)
            {
                    os << "!!parse message failed!! output_len " << output_len << " is invalid, code line " << __LINE__; 
                return;
            }

            if (output_len > output_buffer_len)
            {
                delete output_buffer;
                output_buffer = new char[output_len * 2];
                output_buffer_len = output_len * 2;
            }

                if (!(*g_DumpMsgAsPlainString)(temp_data, temp_data_len, &output_len, output_buffer))
                {
                    // sub the master slave message header
                    // retry!
                    if (!retry)
                    {
                        retry = true;
                        temp_data_len -= sizeof(MasterSlaveMessageHeader);
                        temp_data += sizeof(MasterSlaveMessageHeader);
                        continue;
                    }
                }
            
            if ((int64_t)output_len <= 0)
            {
                    os << "!!parse message failed!! output_len " << output_len << " is invalid, code line " << __LINE__; 
                return;
            }

            assert(output_buffer_len > 0);
            output_buffer[std::min(output_len, output_buffer_len - 1)] = 0x00;
            os << output_buffer;
        }
        else
        { 
                if (!retry)
                {
                    retry = true;
                    temp_data_len -= sizeof(MasterSlaveMessageHeader);
                    temp_data += sizeof(MasterSlaveMessageHeader);
                    continue;
                }
                os << "!!parse message failed!! code line " << __LINE__; 
        }
        return;
    } while (true);    
}

class MsgPrinter
{
public:
    MsgPrinter(const Message::SqnType& begin,
               std::ostream& os,
               const std::string& parse_method)
            : sqn_(begin),
              os_(os),
              parse_method_(parse_method)
    {}
    
protected:
    void PrintMsgBody(const char* data, size_t data_len)
    {
        if (g_show_acked_info)
        {
            ShowStatusRecord(os_, data, data_len);
            return;
        }

        if( ("json" == parse_method_) && g_DumpMsgAsJson )
        { PrintMsgAsJson(os_, data, data_len); }
        else if( ("text" == parse_method_) && g_DumpMsgAsPlainString )
        { PrintMsgAsPlainString(os_, data, data_len); }
        else
        { PrintMsgAsBinary(os_, data, data_len); }
    }
    
    Message::SqnType sqn_;
    std::ostream& os_;
    std::string parse_method_;
};

class TxMsgPrinter : public MsgPrinter
{
public:
    using MsgPrinter::MsgPrinter;
    ErrorCode operator()(const AmiMessage* ami_msg)
    {
        Message* msg = ami_msg->message();
        RecordedMsgPropUnion prop_union = {msg->ex_msg_header};

        if(!g_summary)
        {
            os_ << ADK_GREEN_TXT("<AMI REC ORDER>: ") << sqn_ << '\n'
                << "<AMI META DATA>: "
                << "property = { " << prop_union.msg_prop << " }, "
                << "endpoint_id = "
                << ami_msg->ami_meta_data.endpoint_id << ", "
                << "transport_id = "
                << ami_msg->ami_meta_data.transport_id << ", "
                << "stream_id = " << std::hex << std::showbase << msg->msg_header.stream_id << std::dec << '\n'
                << "                 "
                << "topic_sqn = " << msg->topic_sqn << ", "
                << "stream_sqn = " << msg->stream_sqn << ", "
                << "stream_index: " << ami_msg->ami_meta_data.c_stream_sqn << ", "
                << "topic_index: " << ami_msg->ami_meta_data.c_topic_sqn << ", "
                << "endpoint_index: " << ami_msg->ami_meta_data.c_endpoint_sqn << '\n'
                << "                 "
                << "data_len = " << msg->app_data_len << ", "
                << "option = " << std::hex << std::showbase << (msg->msg_header.option_offset & AMI_OPTION_MASK) << ", "
                << "tag = " << (msg->msg_header.option_offset & AMI_OPTION_TAG_MASK) << ", "
                << "ancestor_id = " << msg->msg_header.ancestor_id << std::dec  << '\n'
                << "                 "
                << "receive_time = " << ami_msg->ami_meta_data.recorder_receive_msg_time_ns << ", "
                << UtcToLocalTime(ami_msg->ami_meta_data.recorder_receive_msg_time_ns) 
                << '\n';
                
            os_ << "<AMI MSG BODY> : "
                << '\n';
        
            PrintMsgBody(msg->app_data_begin, msg->app_data_len);
        
            os_ << '\n' << '\n';
        }
        
        sqn_++;

        if(msg->IsDiscarded())
        { g_discarded_cnt++; }

        if(msg->IsPlaceHolder())
        { g_place_holder_cnt++; }
        
        return kSuccess;
    }
};

class RxMsgPrinter : public MsgPrinter
{
public:
    using MsgPrinter::MsgPrinter;
    
    ErrorCode operator()(const AmiMessage* ami_msg)
    {
        Message* msg = ami_msg->message();
        RecordedMsgPropUnion prop_union = {msg->ex_msg_header};
        
        if (!g_summary)
        {
            os_ << ADK_GREEN_TXT("<AMI REC ORDER>: ") << sqn_ << '\n'
                << "<AMI META DATA>: "
                << "property = { " << prop_union.msg_prop << " }, "
                << "endpoint_id = "
                << ami_msg->ami_meta_data.endpoint_id << ", "
                << "transport_id = "
                << ami_msg->ami_meta_data.transport_id << ", "
                << "stream_id = " << std::hex << std::showbase << msg->msg_header.stream_id << std::dec << '\n'
                << "                 "
                << "topic_sqn = " << msg->topic_sqn << ", "
                << "stream_sqn = " << msg->stream_sqn << ", "
                << "stream_index: " << ami_msg->ami_meta_data.c_stream_sqn << ", "
                << "topic_index: " << ami_msg->ami_meta_data.c_topic_sqn << ", "
                << "endpoint_index: " << ami_msg->ami_meta_data.c_endpoint_sqn << '\n'
                << "                 "
                << "data_len = " << msg->app_data_len << ", "
                << "option = " << std::hex << std::showbase << (msg->msg_header.option_offset & AMI_OPTION_MASK) << ", "
                << "tag = " << (msg->msg_header.option_offset & AMI_OPTION_TAG_MASK) << ", "
                << "ancestor_id = " << msg->msg_header.ancestor_id << std::dec << '\n'
                << "                 "    
                << "receive_time = " << ami_msg->ami_meta_data.recorder_receive_msg_time_ns << ", "
                << UtcToLocalTime(ami_msg->ami_meta_data.recorder_receive_msg_time_ns)
                << '\n';

            os_ << "<AMI MSG BODY> : " << '\n';
        
            PrintMsgBody(msg->app_data_begin, msg->app_data_len);
        
            os_ << '\n' << '\n';
        }
        
        sqn_++;
        
        if(msg->IsDiscarded())
        { g_discarded_cnt++; }

        if(msg->IsPlaceHolder())
        { g_place_holder_cnt++; }

        return kSuccess;
    }
};

template<typename KeyIndexType>
ErrorCode_def KeyIndexFileSize
(const bf::path& file_path, KeyIndexType& key_value)
{
    std::string file_name = file_path.filename().string();
    boost::regex key_index_re( (bl::format("{1}-([0-9]+)_{2}")
                                % KeyIndexType::KeyTypeName()
                                % kIndexFileName).str() );
    boost::cmatch what;
    bs::error_code bs_ec;
    if(boost::regex_match(file_name.c_str(), what, key_index_re))
    {
        LOG_DEBUG("locate '{1}' key index file '{2}'",
                  KeyIndexType::KeyTypeName(), file_path);
        
        HashMapKeyType key_value_hash_code = 0;
        std::string key_value_hash_code_str(what[1].first, what[1].second);
        try{
            key_value_hash_code = boost::lexical_cast<HashMapKeyType>
                (key_value_hash_code_str);
        }
        catch(const boost::bad_lexical_cast&)
        { assert(false); /*should not reach here*/ }
        
        key_value = KeyIndexType(key_value_hash_code);
        
        return kSuccess;
    }
    else
    { return kTryAgain; }
}

int main(int ac, const char** av)
{
    bpo::options_description desc("");
    bpo::options_description params;
    bpo::options_description all;
    bpo::variables_map vm;
    uint32_t level;
    int usage = 0;
    std::string context_name;
    std::string list_type;
    std::string channel_type;
    std::string transport_name;
    std::string endpoint_name;
    std::string path_str;
    bf::path path;
    bf::path id_map_file;
    Message::SqnType begin;
    Message::SqnType count;
    boost::optional<Message::SqnType> end;
    boost::optional<MessageHeader::IDType> stream_id;
    boost::optional<AmiMetaData::IDType> transport_id;
    std::string rx_transport_name;
    boost::optional<AmiMetaData::IDType> endpoint_id;
    std::string rx_endpoint_name;
    boost::optional<bf::path> app_msg_parser;
    std::string ld_library_path_str;
    boost::optional<bf::path> ld_library_path;
    std::string parse_method;
    std::string msg_property;
    boost::optional<RecordedMsgProp> msg_prop;
    
    ConsoleLogger record_querier_logger;
    Logger::set_logger_exit_sleep_time(-1);

    RecordReader rr;
    volatile bool is_recount_begin = false;

    std::vector<adk::Property> id_map_props_vec;

    try
    {
        desc.add_options()
            ("help,h", "show this information")
            
            ("verbose,v",
             bpo::value<uint32_t>(&level)->default_value(3, "warn"),
             "log le 0(trace), 1(debug), 2(info), 3(warn), 4(error), 5(fatal)")

            ("context-name,c",
             bpo::value<std::string>(&context_name),
             "ami context name")
            
            ("list,l",
             bpo::value<std::string>(&list_type),
             "list type: transport, endpoint")

            ("channel-type,t",
             bpo::value<std::string>(&channel_type),
             "channel type:tx(send), rx(receive),\n""ack(ack message)")
            
            ("transport-name",
             bpo::value<std::string>(&transport_name),
             "if tx channel specified, context transport name must be specified\n"
             "for rx channel, only show the message by transport name specified")

            ("endpoint-name",
             bpo::value<std::string>(&endpoint_name),
             "for rx channel, only show the message by endpoint name specified")

            ("begin,b",
             bpo::value<Message::SqnType>(&begin)->default_value
             (RecordAgent::kBegin, "1"),
             "the beginning of message sequence, default:1\n"
             "0 for the last one message")

            ("number,n",
             bpo::value<Message::SqnType>(&count)->default_value
             (RecordAgent::kBegin, "1"),
             "the count of messages from begin, message interval [beign, begin+n); "
             "when the value of begin is 0, this means the last number of messages, message interval [end-n, end)\n"
             "default:1")
            
            ("end,e",
             bpo::value<Message::SqnType>(), 
             "the ending of message sequence\n"
             "default:begin+1, 0 for the last one message")
            
            ("stream-id", bpo::value<MessageHeader::IDType>(),
             "only show the message by stream id specified")
            
            ("transport-id", bpo::value<AmiMetaData::IDType>(),
             "for rx channel, only show the message by transport id specified")

            ("endpoint-id", bpo::value<AmiMetaData::IDType>(),
             "for rx channel, only show the message by endpoint id specified")

            ("msg-property", bpo::value<std::string>(&msg_property),
             "set message property -\n"
             "d : drop message\n"
             "n : clear all property")

            ("binary-print-step",
             bpo::value<uint32_t>(&g_binary_print_step)->default_value(kDefBinaryPrintStep),
             "print binary memory step size, optional value: 1,2,4,8")

            ("binary-print-width",
             bpo::value<uint32_t>(&g_binary_print_width)->default_value(kDefBinaryPrintWidth),
             "print binary memory line width, max value 256")

            ("app-msg-parser",
             bpo::value<std::string>(),
             "the path of dynamic library to assist in parsing message")
            
            ("ld-library-path",
             bpo::value<std::string>(&ld_library_path_str),
             "set environment variable 'LD_LIBRARY_PATH' to load dynamic library")

            ("parse-method",
             bpo::value<std::string>(&parse_method)->default_value("json"),
             "parse message method: json, text")

            ("summary,s", "only show summary message")
            ;

        params.add_options()
            ("path", bpo::value<std::string>(&path_str))
            ;
        
        bpo::positional_options_description p;
        p.add("path", 1u);

        all.add(desc).add(params);
        bpo::store(bpo::command_line_parser(ac, av).
                   options(all).positional(p).run(), vm);
        bpo::notify(vm);

        if (vm.count("help")) 
        {
            PrintUsage(desc);
            PrintExample();
            return 0;
        }

        if (level < LogLevel::kTrace || level > LogLevel::kFatal)
        {
            throw std::logic_error("invalid argument, please check -v [ --verbose ] optional"); 
        }

        if (vm.count("end"))
        { end = vm["end"].as<Message::SqnType>(); }
        
        if (!end)
        {
            if (begin == AmiRecorderBase::kMostRecent)
            { 
                end = begin;
                is_recount_begin = true;  
            }
            else
            { 
                end = begin + count;
            }
        }
        
        if (!vm.count("path"))
        {
            path = bf::path(rcdv::kDataPath);
            usage = 1;
        }
        else
        {
            path = bf::path(path_str);
            if ( !bal::find_first(path.string(), kTxPathID)
                && !bal::find_first(path.string(), kRxPathID)
                && !bal::find_first(path.string(), kAckPathID) )
            { usage = 1; }
            else
            {
                if (bal::find_first(path.string(), kTxPathID))
                { channel_type = "tx"; }
                else if (bal::find_first(path.string(), kRxPathID))
                { channel_type = "rx"; }
                else
                { channel_type = "ack"; }

                usage = 2;
            }
        }

        if (channel_type == "ack")
            g_show_acked_info = true;

        if(1 == usage)
        {
            if(!vm.count("context-name"))
            { 
                throw std::logic_error("please specify ami context name,-c [ --context-name ]"); 
            }

            id_map_file = path / bf::path(context_name + "/maps/id_maps");

            if(!vm.count("channel-type") 
               || (channel_type != "tx" && channel_type != "rx"
               && channel_type != "ack"))
            { 
                throw std::logic_error("invalid argument, please check -t [ --channel_type ] optional"); 
            }

            if((channel_type == "tx") && !vm.count("transport-name") && !vm.count("list"))
            { 
                auto it_path = path / bf::path(TX_PATH(context_name, ""));

                std::vector<std::string> tp_vec;
                bf::directory_iterator end_it;
                for(bf::directory_iterator tp_dir(it_path); tp_dir != end_it; ++tp_dir)
                {
                    if(!bf::is_regular_file(tp_dir->status()))
                    {
                        tp_vec.push_back(tp_dir->path().leaf().string());
                    }
                }

                std::string tp_list_string = adk::GetElementList(tp_vec);
                throw std::logic_error(std::string("if specified tx channel, must specify transport name by [ --transport-name ] optional \n"
                                        "Context <") + context_name + "> transport list:\n" +  tp_list_string + "\n"); 
            }

            if("tx" == channel_type)
            { path /= bf::path(TX_PATH(context_name, transport_name)); }
            else if("rx" == channel_type)
            { path /= bf::path(RX_PATH(context_name)); }
            else
            { path /= bf::path(ST_PATH(context_name)); }
        }
        else
        {
            if('/' == *path.string().crbegin())
            {
                std::string orig_path_str = path.string();
                path = bf::path
                    (std::string
                     (orig_path_str, 0, orig_path_str.size()-1));
            }
            if( (path.parent_path().filename().string() != kTxPathID)
                && (path.filename().string() != kRxPathID)
                && (path.filename().string() != kAckPathID) )
            {
                std::string info = (bl::format("'{1}'isn't valid CHANNEL_PATH")
                                    % path.string()).str();
                throw std::logic_error(info);
            }

            id_map_file = path.parent_path() / bf::path("/maps/id_maps");
        }

        bs::error_code bs_err;        
        if(!bf::exists(path, bs_err))
        {
            std::string info = (bl::format("{1} don't exist") % path).str();
            throw std::logic_error(info);
        }

        if(bf::is_regular_file(id_map_file, bs_err))
        {
            bpt::ptree pt;
            bpt::read_json(id_map_file.string(), pt);

            adk::Property id_map_props;
            adk::Property::SetPtree(&id_map_props, pt);

            id_map_props_vec = id_map_props.GetValue("IdObject", id_map_props_vec);
        }

        if (vm.count("list"))
        {
            if (list_type == "transport")
            {
                std::clog << "*********** transport list ***********\n";
                if (channel_type == "rx")
                {
                    for (const auto& prop : id_map_props_vec)
                    {
                        if (prop.GetValue("Type", "") == "RxTransport")
                        {
                            std::clog << prop.GetValue("Type", "")
                                      << " <Name: " << prop.GetValue("Name", "")
                                      << ", Id: " << prop.GetValue("Id", 0)
                                      << ">\n";
                        }
                    }
                }
                if (channel_type == "tx")
                {
                    for (const auto& prop : id_map_props_vec)
                    {
                        if (prop.GetValue("Type", "") == "TxTransport")
                        {
                            std::clog << prop.GetValue("Type", "")
                                      << " <Name: " << prop.GetValue("Name", "")
                                      << ", Id: " << prop.GetValue("Id", 0)
                                      << ">\n";
                        }
                    }
                }
            }
            
            if (list_type == "endpoint")
            {
                std::clog << "*********** endpoint list ***********\n";
                if (channel_type == "rx")
                {
                    for (const auto& prop : id_map_props_vec)
                    {
                        if (prop.GetValue("Type", "") == "RxEndpoint")
                        {
                            std::clog << prop.GetValue("Type", "")
                                      << " <Name: " << prop.GetValue("Name", "")
                                      << ", Id: " << prop.GetValue("Id", 0)
                                      << ">\n";
                        }
                    }
                }
                if (channel_type == "tx")
                {
                    for (const auto& prop : id_map_props_vec)
                    {
                        if (prop.GetValue("Type", "") == "TxEndpoint")
                        {
                            std::clog << prop.GetValue("Type", "")
                                      << " <Name: " << prop.GetValue("Name", "")
                                      << ", Id: " << prop.GetValue("Id", 0)
                                      << ">\n";
                        }
                    }
                }
            }
            return 0;
        }

        if(channel_type == "rx")
        { 
            conflicting_options(vm, "transport-id", "endpoint-id");
            conflicting_options(vm, "transport-id", "transport-name");
            conflicting_options(vm, "transport-id", "endpoint-name");
            conflicting_options(vm, "transport-name", "endpoint-name");
        }
        
        if(vm.count("stream-id"))
        {
            stream_id =
                vm["stream-id"].as<decltype(stream_id)::value_type>();
        }

        if(vm.count("transport-id"))
        {
            transport_id =
                vm["transport-id"].as<decltype(transport_id)::value_type>();
        }

        if(vm.count("endpoint-id"))
        {
            endpoint_id =
                vm["endpoint-id"].as<decltype(endpoint_id)::value_type>();
        }

        if(vm.count("msg-property"))
        {
            if(std::string::npos !=  msg_property.find('n'))
            { msg_prop = RecordedMsgProp(); }
            else
            {
                if(std::string::npos !=  msg_property.find('d'))
                {
                    if(!msg_prop)
                    { msg_prop = RecordedMsgProp(RecordedMsgProp::kDiscarded); }
                    else
                    { *msg_prop |= RecordedMsgProp::kDiscarded; }
                }
            }
        }

        if(g_seg_per_row == 0 || g_seg_per_row > 1024)
        { g_seg_per_row = kDefSegPerRow; }

        if(vm.count("app-msg-parser"))
        {
            std::string app_msg_parser_path_str =
                vm["app-msg-parser"].as<std::string>();
            
            if(!bf::is_regular_file(bf::path(app_msg_parser_path_str), bs_err)
               && !bf::is_symlink(bf::path(app_msg_parser_path_str), bs_err))
            {
                std::string info = (bl::format("{1} don't exist or isn't a valid file")
                                    % app_msg_parser_path_str).str();
                throw std::logic_error(info);
            }

            bf::path app_msg_parser_path(app_msg_parser_path_str);
            if (app_msg_parser_path.parent_path().empty())
            { app_msg_parser = bf::path(".")/app_msg_parser_path; }
            else
            { app_msg_parser = app_msg_parser_path; }

            if (vm.count("ld-library-path"))
            {
                if (!bf::is_directory(bf::path(ld_library_path_str), bs_err))
                {
                    std::string info = (bl::format("{1} don't exist or isn't a valid directory")
                                        % ld_library_path_str).str();
                    throw std::logic_error(info);
                }

                ld_library_path = bf::path(ld_library_path_str);
            }
        }

        if (vm.count("parse-method"))
        {
            if (("json" != parse_method) && ("text" != parse_method))
            { 
                throw std::logic_error("invalid argument, please check [ --parse-method ]"); 
            }
        }

        if (vm.count("summary"))
        { g_summary = true; }

        //输出参数集
        std::clog << "log level:" << level
                  << "[" << kLabel[level] << "] " << '\n';
        std::clog << "channel type: " << channel_type << '\n';
        if (1 == usage)
        {
            std::clog << "ami context name: " << context_name << '\n';
            if(channel_type == "tx")
            { std::clog << "transport name: " << transport_name << '\n'; }
        }
        
        std::clog << "message file path: " << path << '\n';

        if ( ("tx" == channel_type) || ("rx" == channel_type) )
        {
            std::clog << "the beginning of message sequence: " << begin << '\n';
            std::clog << "the ending of message sequence: " << end << '\n';
            std::clog << "the number of display messages: " << count << '\n';
        }

        if (stream_id)
        { std::clog << "stream id: " << stream_id << '\n'; }

        if (channel_type == "rx")
        {
            if(transport_id)
            {
                std::clog << "transport id: " << transport_id << '\n';
                if (!id_map_props_vec.empty())
                {
                    for (const auto& prop : id_map_props_vec)
                    {
                        if (prop.GetValue("Type", "") == "RxTransport" && 
                            prop.GetValue("Id", 0) == *transport_id)
                        {
                            transport_name = prop.GetValue("Name", "");
                            break;
                        }
                    }
                    std::clog << "transport name: " << transport_name << '\n'; 
                }
            }
            else if (!transport_name.empty())
            {
                if (!id_map_props_vec.empty())
                {
                    for (const auto& prop : id_map_props_vec)
                    {
                        if (prop.GetValue("Type", "") == "RxTransport" && 
                            prop.GetValue("Name", "") == transport_name)
                        {
                            transport_id = prop.GetValue("Id", 0);
                            break;
                        }
                    }
                }

                //cannot find id due to id_maps not exist or transport_name error
                if (transport_id)
                {
                    std::clog << "transport id: " << transport_id << '\n';
                    std::clog << "transport name: " << transport_name << '\n';
                }
                else
                {            
                    transport_id = 0;
                    std::clog << "transport_name "<< transport_name << " may be error or id_maps not exist" << '\n';                  
                }
            }

            if (endpoint_id)
            {
                std::clog << "endpoint id: " << endpoint_id << '\n';
                if (!id_map_props_vec.empty())
                {
                    for (const auto& prop : id_map_props_vec)
                    {
                        if (prop.GetValue("Type", "") == "RxEndpoint" && 
                            prop.GetValue("Id", 0) == *endpoint_id)
                        {
                            endpoint_name = prop.GetValue("Name", "");
                            break;
                        }
                    }
                    std::clog << "endpoint name: " << endpoint_name << '\n';
                }
            }
            else if(!endpoint_name.empty())
            {
                for (const auto& prop : id_map_props_vec)
                {
                    if (prop.GetValue("Type", "") == "RxEndpoint" && 
                        prop.GetValue("Name", "") == endpoint_name)
                    {
                        endpoint_id = prop.GetValue("Id", 0);
                        break;
                    }
                }

                //cannot find id due to id_maps not exist or endpoint_name error
                if (endpoint_id)
                {
                    std::clog << "endpoint id: " << endpoint_id << '\n';
                    std::clog << "endpoint name: " << endpoint_name << '\n';                    
                }
                else
                {
                    endpoint_id = 0;
                    std::clog << "endpoint_name "<< endpoint_name << " may be error or id_maps not exist" << '\n';
                }
            }
        }

        if (msg_prop)
        { std::clog << "the message property: " << *msg_prop << '\n'; }
        
        std::clog << "number of segments printed per line " << g_seg_per_row << '\n';
        
        if (app_msg_parser)
        {
            std::clog << "the path of dynamic library to assist in parsing message: " << *app_msg_parser << '\n';
            
            if(ld_library_path)
            {
                LoadLibrary(*app_msg_parser, *ld_library_path);
                std::clog << kLdLibraryPathEnv << ": "
                          << std::getenv(kLdLibraryPathEnv) << '\n';
            }
            else
            { LoadLibrary(*app_msg_parser, bf::path()); }
        }

        if (vm.count("parse-method"))
        {
            std::clog << "parse-method: ";
            if( (("json" == parse_method) && g_DumpMsgAsJson )
                || (("text" == parse_method) && g_DumpMsgAsPlainString) )
            {}
            else
            { parse_method = "bin"; }

            std::clog << parse_method << '\n'; 
        }
    }
    catch (std::exception& e)
    {
        PrintUsage(desc);

        std::cerr << ADK_RED_TXT("\n error argument: ") << e.what() << '\n';
        return 1;
    }

    record_querier_logger.set_min_log_level(level);

    if (!g_summary)
    {
        if ( ("tx" == channel_type) || ("rx" == channel_type) )
        {
            std::string dump_header_var =
                (bl::format("[{1}, {2})") % begin % end).str();
            std::string dump_header =
                (bl::format("*start outputting message between {1}*")
                 % dump_header_var).str();
            std::clog << '\n';
            for (size_t i = dump_header_var.size()+26; i > 0; i--)
            { std::clog << '*'; }
            std::clog << '\n';
            std::clog << dump_header << '\n';
            for (size_t i = dump_header_var.size()+26; i > 0; i--)
            { std::clog << '*'; }
            std::clog << '\n';
        }
        else
        {
            std::string dump_header = "*start outputting status message*";
            std::clog << '\n';
            for (size_t i = 18; i > 0; i--)
            { std::clog << '*'; }
            std::clog << '\n';
            std::clog << dump_header << '\n';
            for (size_t i = 18; i > 0; i--)
            { std::clog << '*'; }
            std::clog << '\n';
        }
    }
    
    int res = 1;
    Message::SqnType total_count = 0;

    if ("tx" == channel_type)
    {
        TxMsgPrinter tx_printer(begin, std::clog, parse_method);

        if (stream_id)
        {
            // when set number argument and begin argument is kMostRecent, need recount begin 
            total_count = rr.GetTxSTRHistMsgCnt(path, *stream_id);
            if (is_recount_begin)
            {
                if (total_count >= count)
                {
                    begin = total_count - count + 1;
                }
                else
                {
                    begin = RecordAgent::kBegin;
                }
            }
            if (kSuccess == rr.ReadTxSTRHistMessage
                (path, *stream_id, begin, *end,
                 std::ref(tx_printer), msg_prop))
            { res = 0; }
        }
        else
        {
            // when set number argument and begin argument is kMostRecent, need recount begin 
            if (is_recount_begin)
            {
                total_count = rr.GetHistMsgCnt(path);
                if (total_count >= count)
                {
                    begin = total_count - count + 1;
                }
                else
                {
                    begin= RecordAgent::kBegin;
                }
            }
            if ( kSuccess == rr.ReadTxHistMessage
                (path, begin, *end, std::ref(tx_printer), msg_prop) )
            { res = 0; }
        }
    }
    else if ("rx" == channel_type)
    {
        RxMsgPrinter rx_printer(begin, std::clog, parse_method);

        if (stream_id)
        {
            // when set number argument and begin argument is kMostRecent, need recount begin 
            if (is_recount_begin)
            {
                total_count = rr.GetRxSTRHistMsgCnt(path, *stream_id);
                if (total_count >= count)
                {
                    begin = total_count - count + 1;
                }
                else
                {
                    begin= RecordAgent::kBegin;
                };
            }

            if (kSuccess == rr.ReadRxSTRHistMessage
                (path, *stream_id, begin, *end,
                 std::ref(rx_printer), msg_prop))
            { res = 0; }
        }
        else if (endpoint_id)
        {
            // when set number argument and begin argument is kMostRecent, need recount begin 
            if (is_recount_begin)
            {
                total_count = rr.GetRxEDPHistMsgCnt(path, *endpoint_id);
                if (total_count >= count)
                {
                    begin = total_count - count + 1;
                }
                else
                {
                    begin = RecordAgent::kBegin;
                }
            }

            if ( kSuccess == rr.ReadRxEDPHistMessage
                (path, *endpoint_id, begin, *end,
                 std::ref(rx_printer), msg_prop) )
            { res = 0; }
        }
        else if (transport_id)
        {
            // when set number argument and begin argument is kMostRecent, need recount begin 
            if (is_recount_begin)
            {
                total_count = rr.GetRxTNPHistMsgCnt(path, *transport_id);
                if (total_count >= count)
                {
                    begin = total_count - count + 1;
                }
                else
                {
                    begin= RecordAgent::kBegin;
                }
            }
            if ( kSuccess == rr.ReadRxTNPHistMessage
                (path, *transport_id, begin, *end,
                 std::ref(rx_printer), msg_prop) )
            { res = 0; }
        }
        else
        {
            // when set number argument and begin argument is kMostRecent, need recount begin 
            if (is_recount_begin)
            {
                total_count = rr.GetHistMsgCnt(path);
                if (total_count >= count)
                {
                    begin = total_count - count + 1;
                }
                else
                {
                    begin= RecordAgent::kBegin;
                }
            }
            if (kSuccess == rr.ReadRxHistMessage
                (path, begin, *end,
                 std::ref(rx_printer), msg_prop))
            { res = 0; }
        }
    }
    else
    {
        if (kSuccess == rr.ReadStatusMessage
            (path, TxMsgPrinter(begin, std::clog, parse_method)))
        { res = 0; }
    }

    ///summary
    std::clog << '\n';
    std::clog << "******\n";
    std::clog << "*summary*\n";
    std::clog << "******\n";
    if ("tx" == channel_type)
    {
        std::clog << "file property:" << rr.GetFileHeader() << '\n';
        std::clog << "total message:" << rr.GetHistMsgCnt(path) << '\n';
        std::clog << "drop message:" << g_discarded_cnt << '\n';
        std::clog << "placeholder message: " << g_place_holder_cnt << '\n';

        bs::error_code bs_ec;
        for (const auto& fe :
             bf::directory_iterator(path, bs_ec))
        {
            ///< StreamKey
            StreamKey str;
            if (kSuccess ==
                KeyIndexFileSize(fe.path(), str))
            {
                std::clog
                    << "stream_id = " << str.key_value
                    << ", total message:"
                    << rr.GetTxSTRHistMsgCnt
                    (fe.path().parent_path(), str.key_value)
                    << '\n';
                continue;
            }
        }
    }
    else if ("rx" == channel_type)
    {
        std::clog << "file property:" << rr.GetFileHeader() << '\n';
        std::clog << "total message:" << rr.GetHistMsgCnt(path) << '\n';
        std::clog << "drop message:" << g_discarded_cnt << '\n';
        std::clog << "placeholder message: " << g_place_holder_cnt << '\n';
            
        bs::error_code bs_ec;
        for (const auto& fe :
                bf::directory_iterator(path, bs_ec))
        {
            ///< TransportKey
            TransportKey tp;
            if ( kSuccess ==
                KeyIndexFileSize(fe.path(), tp) )
            {
                for (const auto& prop : id_map_props_vec)
                {
                    if (prop.GetValue("Type", "") == "RxTransport" && 
                        prop.GetValue("Id", 0) == tp.key_value)
                    {
                        rx_transport_name = prop.GetValue("Name", "");
                        break;
                    }
                }

                std::clog
                    << "transport_id = " << tp.key_value
                    << ", transport_name = " << rx_transport_name
                    << ", total message:"
                    << rr.GetRxTNPHistMsgCnt
                    (fe.path().parent_path(), tp.key_value)
                    << '\n';
                continue;
            }

            ///< EndpointKey
            EndpointKey ep;
            if ( kSuccess ==
                KeyIndexFileSize(fe.path(), ep) )
            {
                for (const auto& prop : id_map_props_vec)
                {
                    if (prop.GetValue("Type", "") == "RxEndpoint" && 
                        prop.GetValue("Id", 0) == ep.key_value)
                    {
                        rx_endpoint_name = prop.GetValue("Name", "");
                        break;
                    }
                }

                std::clog
                    << "endpoint_id = " << ep.key_value
                    << ", endpoint_name = " << rx_endpoint_name
                    << ", total message:"
                    << rr.GetRxEDPHistMsgCnt
                    (fe.path().parent_path(), ep.key_value)
                    << '\n';
                continue;
            }

            ///< StreamKey
            StreamKey str;
            if (kSuccess ==
                KeyIndexFileSize(fe.path(), str))
            {
                std::clog
                    << "stream_id = " << str.key_value
                    << ", total message:"
                    << rr.GetRxSTRHistMsgCnt
                    (fe.path().parent_path(), str.key_value)
                    << '\n';
                continue;
            }
        }
    }
    else
    { std::clog << "no summary for ack channel\n"; }

    std::clog << '\n';
    return res;
}
