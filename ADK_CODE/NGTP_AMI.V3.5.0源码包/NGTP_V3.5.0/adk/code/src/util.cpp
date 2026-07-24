#ifdef __GNUC__
#include <pwd.h>
#include <sched.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#endif

#include <string.h>
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <set>

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include <adk/util.h>
#include <adk/error_code.h>
#include <adk/timeout_counter.h>
#include <adk/json/json.hpp>

namespace adk_impl
{

boost::mutex g_condition_mutex;

#ifdef __GNUC__
int32_t ParseCpuSet(const std::string& cpu_list, cpu_set_t& var_cpuset)
{
    CPU_ZERO(&var_cpuset);
    try {
        if (!cpu_list.empty())
        {   
            std::vector<std::string> splits;
            std::vector<std::string> splits_v2;
            splits_v2.reserve(2);
            boost::split(splits, cpu_list, boost::is_any_of(","), boost::token_compress_on);
            for (auto& core_range : splits)
            {
                splits_v2.clear();
                boost::algorithm::trim(core_range);
                boost::split(splits_v2, core_range, boost::is_any_of("- "), boost::token_compress_on);

                if (splits_v2.size() > 2 || splits_v2.size() ==0)
                    return ErrorCode::kInvalidParameters;

                int core_num = boost::lexical_cast<int>(splits_v2[0]);
                CPU_SET(core_num, &var_cpuset);
                if (splits_v2.size() == 2)
                {
                    int core_num_end = boost::lexical_cast<int>(splits_v2[1]);
                    for (++core_num; core_num <= core_num_end; ++core_num)
                    {
                        CPU_SET(core_num, &var_cpuset);
                    }
                }
            }
            return ErrorCode::kSuccess;
        }
    }
    catch (...)
    {}

    return ErrorCode::kInvalidParameters;
}

int32_t SetCpuAffinity(const std::string& cpu_list)
{
    cpu_set_t tmp_cpu_set;
    int32_t ec = ParseCpuSet(cpu_list, tmp_cpu_set);
    if (ec != ErrorCode::kSuccess)
    {
        return ec;
    }   

    if (sched_setaffinity(0, sizeof(cpu_set_t), &tmp_cpu_set) != 0)
    {
        return ErrorCode::kFailure;
    }

    return ErrorCode::kSuccess;
}

int32_t GetCpuAffinity(std::string& cpu_list)
{
    cpu_set_t tmp_cpu_set;
    int ret = sched_getaffinity(syscall(SYS_gettid), sizeof(cpu_set_t), &tmp_cpu_set);
    if (ret != 0)
    {
        return ErrorCode::kFailure;
    }

    cpu_list.clear();
    auto nr_cpu = std::thread::hardware_concurrency();
    for (uint32_t i = 0; i < nr_cpu; ++i)
    {
        if (CPU_ISSET(i, &tmp_cpu_set))
        {
            cpu_list.append(boost::lexical_cast<std::string>(i) + ",");
        }
    }
    auto last_comma = cpu_list.rfind(",");
    cpu_list.erase(last_comma, 1);
    return ErrorCode::kSuccess;
}



/*
    bit 0  Dump anonymous private mappings.
    bit 1  Dump anonymous shared mappings.
    bit 2  Dump file-backed private mappings.
    bit 3  Dump file-backed shared mappings.
    bit 4 (since Linux 2.6.24)
        Dump ELF headers.
    bit 5 (since Linux 2.6.28)
        Dump private huge pages.
    bit 6 (since Linux 2.6.28)
        Dump shared huge pages.
 */
int32_t EnableShareMemoryDump(const char* env_name)
{
    bool force    = false;
    char* env_str = nullptr;
    if (env_name == nullptr)
    {
        force = true;
    }
    else
    {
        env_str = std::getenv(env_name);
    }

    if (force
        || (env_str != NULL
            && (*env_str == 'Y' || *env_str == 'y')))
    {
        int proc_fd = open("/proc/self/coredump_filter", O_RDWR, 0660);
        if (proc_fd < 0)
        {
            return ErrorCode::kFailure;
        }
        else
        {
            write(proc_fd, "0x7f", strlen("0x7f"));
            close(proc_fd);
        }

        return ErrorCode::kSuccess;
    }
    return ErrorCode::kFailure;
}

struct GetLoginUserHelper
{
    GetLoginUserHelper()
    {
        struct passwd* pw = getpwuid(geteuid());
        if (!pw)
        {
            return;
        }

        login_user_name = pw->pw_name;
        login_user_home = pw->pw_dir;
        login_user_home += "/";
    }
    std::string login_user_name;
    std::string login_user_home;
};

const std::string& GetLoginUserName()
{
    static GetLoginUserHelper* helper = new GetLoginUserHelper();
    return helper->login_user_name;
}

const std::string& GetLoginUserHome()
{
    static GetLoginUserHelper* helper = new GetLoginUserHelper();
    return helper->login_user_home;
}

typedef std::set<std::string> CpuNodeRage;
typedef std::map<std::string, CpuNodeRage> CpuNodeMap;

struct CpuInfo
{
    uint64_t state;
    std::string processor;
    std::string cpu_node;
};

#define NR_CPU_INFO 2

static CpuNodeMap* InitCpuNodeMap()
{
    CpuNodeMap* cn_map = new CpuNodeMap();
    FILE* cpuinfo_file = fopen("/proc/cpuinfo", "r");
    if (cpuinfo_file == NULL)
        return cn_map;

    char* line = NULL;
    size_t len = 0;
    ssize_t read;

    std::vector<std::string> splits;
    CpuInfo cpu_info;
    cpu_info.state = 0;
    while ((read = getline(&line, &len, cpuinfo_file)) != -1)
    {
        splits.clear();
        try
        {
            boost::split(splits, line, boost::is_any_of(":"), boost::token_compress_on);
            if (splits.size() != 2)
                continue;

            boost::algorithm::trim(splits[0]);
            boost::algorithm::trim(splits[1]);
            if (splits[0].empty()
                || splits[1].empty())
                continue;

            if (splits[0] == "processor")
            {
                cpu_info.processor = splits[1];
                ++cpu_info.state;
            }
            else if (splits[0] == "physical id")
            {
                cpu_info.cpu_node = splits[1];
                ++cpu_info.state;
            }

            if (cpu_info.state == NR_CPU_INFO)
            {
                (*cn_map)[cpu_info.cpu_node].insert(cpu_info.processor);
                cpu_info.state = 0;
            }
        }
        catch (...)
        {
            cpu_info.state = 0;
        }
    }

    if (line != NULL)
        free(line);

    fclose(cpuinfo_file);
    return cn_map;
}

static CpuNodeMap& GetCpuNodeMap()
{
    static CpuNodeMap* cpu_node_map = InitCpuNodeMap();
    return *cpu_node_map;
}

int32_t GetCpuNodeInfo(std::string& cpu_node)
{
    auto& cpu_node_info = GetCpuNodeMap();
    for (auto& node : cpu_node_info)
    {
        cpu_node += node.first + " : ";
        uint32_t i    = 0;
        uint32_t endi = node.second.size();
        for (const auto& processor : node.second)
        {
            cpu_node += processor;
            ++i;
            if (i != endi)
            {
                cpu_node += ",";
            }
        }
        cpu_node += "\n";
    }
    if (cpu_node.empty())
        return ErrorCode::kFailure;
    return ErrorCode::kSuccess;
}

int32_t SetCpuNode(const std::string& cpu_node)
{
    auto& cpu_node_map = GetCpuNodeMap();
    auto it            = cpu_node_map.find(cpu_node);
    if (it != cpu_node_map.end())
    {
        std::string cpu_list;
        uint32_t i    = 0;
        uint32_t endi = it->second.size();
        for (const auto& processor : it->second)
        {
            cpu_list += processor;
            ++i;
            if (i != endi)
            {
                cpu_list += ",";
            }
        }
        return SetCpuAffinity(cpu_list);
    }
    return ErrorCode::kFailure;
}

class SetCpuNodeOnInit
{
public:
    SetCpuNodeOnInit()
    {
        char* env_str = std::getenv("ADK_CPU_NODE");
        if (env_str != nullptr)
        {
            SetCpuNode(env_str);
        }
    }
};
static SetCpuNodeOnInit g_do_set_cpu_node;

void WaitPidUntil(pid_t pid, uint64_t timeout)
{
    assert(pid > 0);
    TimeoutCounter toc(1, timeout);

    do
    {
        if (waitpid(pid, nullptr, WNOHANG) > 0)
        {
            return;
        }
        toc.Run();
    } while (!toc.IsTimeout());
}

bool LatencyStatistics::Calculate()
{
    if (nr_saved_[current_] / 2 == 0
        || is_need_reset_ == true)
    {
        return false;
    }

    current_ = !current_;
    ADK_BARRIER();
    is_need_reset_ = true;

    // std::cout << "delay_us_ = " << delay_us_ << std::endl;
    usleep(delay_us_);

    register uint64_t current = !current_;
    auto* save                = save_[current];
    uint64_t& nr_saved        = nr_saved_[current];
    nr_saved                  = (nr_saved / 2);
    if (nr_saved == 0)
    {
        return false;
    }

    // std::cout << "Calculate nr_saved = " << nr_saved << std::endl;

    uint64_t sum_ns = 0;
    for (uint64_t i = 1; i <= nr_saved; ++i)
    {
        // to debug:
        // slow down rate and un comment following code
        // std::cout << "save[i*2 - 1] = " << save[i*2 - 1] << " save[i*2] = " << save[i*2] << std::endl;
        save[i] = save[i * 2 - 1] * 1000000000 + save[i * 2];
        sum_ns += save[i];
    }
    avg_[current] = sum_ns / nr_saved;
    qsort(&save[1], nr_saved, sizeof(int64_t), LatencyStatistics::CompareLatency);

    return true;
}

uint64_t LatencyStatistics::GetMax()
{
    register uint64_t current = !current_;
    uint64_t nr_saved         = nr_saved_[current];
    if (nr_saved == 0)
    {
        return 0;
    }

    // std::cout << "max nr_saved = " << nr_saved << std::endl;
    auto* save = save_[current];
    return save[nr_saved];
}

uint64_t LatencyStatistics::GetMin()
{
    register uint64_t current = !current_;
    uint64_t nr_saved         = nr_saved_[current];
    if (nr_saved == 0)
    {
        return 0;
    }

    auto* save = save_[current];
    return save[1];
}

uint64_t LatencyStatistics::GetPercentNumber(float percent)
{
    register uint64_t current = !current_;
    auto* save                = save_[current];
    uint64_t nr_saved         = nr_saved_[current];
    if (nr_saved == 0)
    {
        return 0;
    }

    auto index = (uint64_t)((nr_saved)*percent);
    index      = std::min(nr_saved, index);
    index      = std::max(1ul, index);
    return save[index];
}

uint64_t LatencyStatistics::GetAvg()
{
    register uint64_t current = !current_;
    uint64_t nr_saved         = nr_saved_[current];
    if (nr_saved == 0)
    {
        return 0;
    }

    return avg_[current];
}

uint64_t LatencyStatistics::GetNumberRecords()
{
    register uint64_t current = !current_;
    return nr_saved_[current];
}

LatencyStatistics::LatencyStatistics(uint64_t capacity, uint32_t delay_us)
    : delay_us_(delay_us)
{
    save_[0] = new int64_t[capacity * 2];
    save_[1] = new int64_t[capacity * 2];
    memset(save_[0], 0x00, sizeof(int64_t) * capacity);
    memset(save_[1], 0x00, sizeof(int64_t) * capacity);
}

std::string LatencyStatistics::GetDisplayString()
{
    oss_.clear();
    oss_.str("");
    oss_ << "rate: " << GetNumberRecords()
         << " | latency (ns) | avg: " << GetAvg()
         << " | min: " << GetMin()
         << " | 50%: " << GetPercentNumber(0.5)
         << " | 90%: " << GetPercentNumber(0.9)
         << " | 95%: " << GetPercentNumber(0.95)
         << " | max:" << GetMax();
    return oss_.str();
}

void LatencyStatistics::PeriodDisplayStatistic(uint32_t interval_micro)
{
    while (is_running_)
    {
        usleep(interval_micro - delay_us_);
        if (Calculate())
        {
            std::cout << GetDisplayString() << std::endl;
        }
    }
}


#endif 

#define ADK_MEMORY_DUMP_MAX_LEN (1024 * 1024)
static char g_output_buf[ADK_MEMORY_DUMP_MAX_LEN * 3];

static inline char* AllocOutputBuffer()
{
    char* output_buf = &g_output_buf[0];
    output_buf += sprintf(output_buf, "0x0000:  ");
    return output_buf;
}


// Note: thread unsafe!
const char* MemoryHexDump(const void* addr, uint32_t len, uint32_t step, uint32_t width)
{
    if (len > ADK_MEMORY_DUMP_MAX_LEN
        || width > 256
        || step > 8
        || step == 0)
        return "memory dump failed";

    static char* output_buf = AllocOutputBuffer();

    unsigned char translate_buf[1024];
    unsigned char* translate_buf_addr = &translate_buf[0];
    uint32_t step_index               = 0;
    while ((1u << step_index) < step)
        ++step_index;
    width = ((width + step - 1) / step) * step;

    const unsigned char* begin_addr        = (const unsigned char*)addr;
    const unsigned char* display_addr      = begin_addr;
    const unsigned char* next_display_addr = begin_addr + step;
    const unsigned char* end_addr          = begin_addr + len;
    uint32_t width_counter                 = 0;
    char* output_buf_addr                  = output_buf;

    // process each steps
    for (; next_display_addr < end_addr;)
    {
        // print one step
        output_buf_addr += (g_ptable[step_index].print_func)(output_buf_addr,
                                                             display_addr);

        // save human readable characters
        uint32_t sub_counter = 0;
        while (sub_counter < step)
        {
            auto c = display_addr[sub_counter];
            if ((c < 0x20) || (c > 0x7e))
            {
                *translate_buf_addr = '.';
            }
            else
            {
                *translate_buf_addr = c;
            }
            ++sub_counter;
            ++translate_buf_addr;
        }
        *translate_buf_addr = 0x00;
        // printf("%s\n", translate_buf);

        if ((width_counter += step) == width)
        {
            // printf("%s\n", translate_buf);

            // print one line
            width_counter = 0;
            output_buf_addr += sprintf(output_buf_addr,
                                       "  %s  \n0x%04x:  ",
                                       translate_buf,
                                       (uint32_t)(next_display_addr - begin_addr));

            translate_buf_addr = &translate_buf[0];
        }

        display_addr = next_display_addr;
        next_display_addr += step;
    }

    // process the left memory area.
    bool first_round = true;
    while (display_addr < end_addr)
    {
        if (first_round)
        {
            first_round      = false;
            *output_buf_addr = ' ';
            output_buf_addr += 1;
        }

        output_buf_addr += sprintf(output_buf_addr, "%02x", *display_addr);

        if ((*display_addr < 0x20) || (*display_addr > 0x7e))
        {
            *translate_buf_addr = '.';
        }
        else
        {
            *translate_buf_addr = *display_addr;
        }
        ++display_addr;
        ++translate_buf_addr;
        ++width_counter;
    }
    *translate_buf_addr = 0x00;

    auto padding = len % step;
    if (padding > 0)
    {
        padding              = step - padding;
        auto padding_counter = 0u;
        do
        {
            output_buf_addr += sprintf(output_buf_addr, "  ");
            ++padding_counter;
            ++width_counter;
        } while (padding_counter < padding);
    }

    while (width_counter != width)
    {
        // output_buf_addr += sprintf(output_buf_addr, g_ptable[step_index].place_holder);
        memcpy(output_buf_addr,
               g_ptable[step_index].place_holder,
               g_ptable[step_index].place_holder_size);
        output_buf_addr += g_ptable[step_index].place_holder_size;
        width_counter += step;
    }

    // print the final readable characters
    output_buf_addr += sprintf(output_buf_addr, "  %s\n", translate_buf);
    *output_buf_addr = 0x00;
    return &g_output_buf[0];
}

std::string PtreeToString(const boost::property_tree::ptree& ptree, bool is_pretty)
{
    std::ostringstream oss;
    boost::property_tree::json_parser::write_json(oss, ptree, is_pretty);
    return oss.str();
}

bool CompareTwoJson(const nlohmann::json& json_value_1, const nlohmann::json& json_value_2)
{
    try 
    {
        if (json_value_1 == json_value_2)
        {
            return true;
        }

        if (std::string(json_value_1.type_name()) == "array" && std::string(json_value_2.type_name()) == "array")
        {
            auto array_1 = json_value_1;
            auto array_2 = json_value_2;

            std::sort(array_1.begin(), array_1.end());
            std::sort(array_2.begin(), array_2.end());

            if (array_1 == array_2)
            {
                return true;
            }
            else
            {
                // 如果排序后不相等，需要判断下有没有元素类型是array或object，如果有，则需要递归处理
                auto array_1_iter = array_1.begin();
                auto array_2_iter = array_2.begin();
                for (; array_1_iter != array_1.end() && array_2_iter != array_2.end(); ++array_1_iter,++array_2_iter)
                {
                    if (array_1_iter.value() != array_2_iter.value())
                    {   
                        std::string type_name_1(array_1_iter.value().type_name());  
                        std::string type_name_2(array_2_iter.value().type_name());  
                        
                        // 如果类型不一样，直接返回false
                        if (type_name_1 != type_name_2)
                        {
                            std::cout << "two array element type name are different" << std::endl;
                            std::cout << " type_name_1: " << type_name_1 << " array_1_iter value: " << array_1_iter.value() << std::endl;
                            std::cout << " type_name_1: " << type_name_2 << " array_2_iter value: " << array_2_iter.value() << std::endl;
                            
                            return false;
                        }
                        
                        // 如果是object或array，递归处理
                        if (type_name_1 == "object" || type_name_1 == "array")
                        {   
                            if (!CompareTwoJson(array_1_iter.value(), array_2_iter.value()))
                            {
                                std::cout << "two array element value(object or array) are different" << std::endl;
                                std::cout << " type_name_1: " << type_name_1 << " array_1_iter value: " << array_1_iter.value() << std::endl;
                                std::cout << " type_name_1: " << type_name_2 << " array_2_iter value: " << array_2_iter.value() << std::endl;
                                
                                return false;
                            }   
                        }     
                        else // array内的元素不相等，且该元素类型不是array或object，直接返回false
                        {
                            std::cout << "two array element value are different" << std::endl;
                            std::cout << " type_name_1: " << type_name_1 << " array_1_iter value: " << array_1_iter.value() << std::endl;
                            std::cout << " type_name_1: " << type_name_2 << " array_2_iter value: " << array_2_iter.value() << std::endl;    
                            
                            return false;
                        }
                    }
                }
                
                // 如果有一个array没有遍历完，说明该array比另一个array内容多，直接返回false
                if (array_1_iter != array_1.end() || array_2_iter != array_2.end())
                {
                    if (array_1_iter != array_1.end())
                    {
                        std::cout << "two array element value are different, array_1 has more elements: " << std::endl;   
                        std::cout << array_1_iter.value() << std::endl;
                    }
                    else 
                    {
                        std::cout << "two array element value are different, array_2 has more elements" << std::endl;
                        std::cout << array_2_iter.value() << std::endl;
                    }   
                    
                    return false;
                }
                
                // 如果递归地比较完array的元素，都相等，则返回true
                return true;
            }
        }

        if (std::string(json_value_1.type_name()) != "object" || std::string(json_value_2.type_name()) != "object")
        {
            std::cout << "two json value are different" << std::endl;
            std::cout << "type_name_1: " << json_value_1.type_name() << " json_value_1: " << json_value_1 << std::endl;
            std::cout << "type_name_2: " << json_value_2.type_name() << " json_value_2: " << json_value_2 << std::endl;
            
            return false;
        }

        // 比较两个不相等的object，需要分解逐项比较
        auto iter_1 = json_value_1.begin();
        auto iter_2 = json_value_2.begin();

        for (; iter_1 != json_value_1.end() && iter_2 != json_value_2.end(); ++iter_1,++iter_2)
        {
            // 比较key
            if (iter_1.key() != iter_2.key())
            {
                std::cout << "two json key are different" << std::endl;
                std::cout << "json_key_1: " << iter_1.key() << " json_value_1: " << iter_1.value() << std::endl;
                std::cout << "json_key_2: " << iter_2.key() << " json_value_2: " << iter_2.value() << std::endl;
                    
                return false;
            }

            // 比较值
            if (!CompareTwoJson(iter_1.value(), iter_2.value()))
            {
                std::cout << "two json element value are different" << std::endl;
                std::cout << "json_key_1: " << iter_1.key() << " json_value_1: " << iter_1.value() << std::endl;
                std::cout << "json_key_2: " << iter_2.key() << " json_value_2: " << iter_2.value() << std::endl;
                
                return false;
            }
        }
        
        // 如果有一个object没有遍历完，说明该object比另一个object内容多，直接返回false
        if (iter_1 != json_value_1.end() || iter_2 != json_value_2.end())
        {
            if (iter_1 != json_value_1.end())
            {
                std::cout << "two json element value are different, json_value_1 has more elements: " << std::endl;   
                std::cout << iter_1.key() << " " << iter_1.value() << std::endl;
            }
            else 
            {
                std::cout << "two json element value are different, json_value_2 has more elements: " << std::endl;   
                std::cout << iter_2.key() << " " << iter_2.value() << std::endl;
            }
            
            return false;
        }
    }
    catch(...)
    {
        std::cerr << "catch exception in function CompareTwoJson(), what <" << boost::current_exception_diagnostic_information() << ">" << std::endl;
        return false;
    }
    
    return true;    
}

bool CompareTwoJson(const std::string& json_string_value_1, const std::string& json_string_value_2)
{
    nlohmann::json json_value_1;
    nlohmann::json json_value_2;
    
    try 
    {
        json_value_1 = std::move(nlohmann::json::parse(json_string_value_1));
        json_value_2 = std::move(nlohmann::json::parse(json_string_value_2));
    }
    catch(...)
    {
        std::cerr << "catch exception in function CompareTwoJson(), what <" << boost::current_exception_diagnostic_information() << ">" << std::endl;
        return false;
    }
    
    return CompareTwoJson(json_value_1, json_value_2);
}

bool CompareTwoJson(const std::string& json_string_value_1, const nlohmann::json& json_value_2)
{
    nlohmann::json json_value_1;
    
    try 
    {
        json_value_1 = std::move(nlohmann::json::parse(json_string_value_1));
    }
    catch(...)
    {
        std::cerr << "catch exception in function CompareTwoJson(), what <" << boost::current_exception_diagnostic_information() << ">" << std::endl;
        return false;
    }
    
    return CompareTwoJson(json_value_1, json_value_2);
}

} // adk
