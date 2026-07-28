#include <sys/syscall.h>
#include <unistd.h>

#include <boost/property_tree/json_parser.hpp>

#include <adk/record_msg_binary_v2.h>
#include <adk/entry_wrapper.h>
#include <adk/util.h>

struct MyMsg
{
    uint32_t msg_len;
    char msg_data[]; 
};

uint64_t g_serialize_count = 0;
uint64_t g_pre_thread_send_total = 60000;
uint64_t g_pre_thread_send_rate =  10000;
constexpr uint32_t g_queue_size = 8;
bool g_is_running = true;

std::string MySerializeFunc(const void* data, uint32_t data_size)
{
    MyMsg* my_msg = (MyMsg*)data;
    if (my_msg->msg_len != data_size - sizeof(MyMsg::msg_len))
    {
        std::cout << "data is error" << std::endl;
        abort();
    }

    if (g_serialize_count % (g_pre_thread_send_rate * g_queue_size) == 0)
    {
        std::cout << "delay" << std::endl;
        sleep(2);
    }
    ++g_serialize_count;
    return std::string(my_msg->msg_data, my_msg->msg_len);
}

void GenerateMsg(adk_impl::RecordMsgBinaryV2* recorder, uint64_t queue_index = 0)
{
    adk_impl::SimpleRateController<> rate_ctl(g_pre_thread_send_rate);
    for (uint64_t count = 1; count <= g_pre_thread_send_total; ++count)
    {
        rate_ctl.Wait();
        std::string msg_str = "data from tid " + std::to_string(syscall(SYS_gettid)) + ", index " + 
                            std::to_string(queue_index) + ", count " + std::to_string(count);

        uint32_t alloc_count = 0;
        void* msg = nullptr;
        do 
        {
            msg = recorder->AllocBuffer(msg_str.length() + sizeof(MyMsg::msg_len), queue_index);

            ++alloc_count;
            if (alloc_count >= 1000)
            {
                std::cout << "alloc failed" << std::endl;
                abort();
            }

            if (alloc_count > 1)
            {
                sleep(0);
            }
        } while (msg == nullptr);

        MyMsg* my_msg = (MyMsg*)msg;
        my_msg->msg_len = msg_str.length();
        memcpy(my_msg->msg_data, msg_str.c_str(), msg_str.length());

        recorder->PostBuffer(msg, queue_index);
    }
}

void PrintQueueStatus(adk_impl::RecordMsgBinaryV2* recorder)
{
    boost::property_tree::ptree indicator_pt;
    std::ostringstream oss;
    while (g_is_running)
    {
        indicator_pt.clear();
        oss.str("");
        recorder->CollectIndicator(indicator_pt);
        boost::property_tree::write_json(oss, indicator_pt);
        std::cout << oss.str() << std::endl;

        sleep(1);
    }
}

int main(int argc,char **argv)
{
    adk_impl::RecordMsgBinaryV2 recorder(g_queue_size, 1024ul * 1024ul);
    std::string local_file_name = "./record_msg_binary_v2_test.txt";

    std::string error_msg;
    if (!recorder.Init(local_file_name, &error_msg))
    {
        std::cout << "recorder init failed, " << error_msg << std::endl;
        return 1;
    }

    recorder.SetSerializeFunc(MySerializeFunc);
    if (recorder.Start(&error_msg) != adk_impl::ErrorCode::kSuccess)
    {
        std::cout << "recorder start failed, " << error_msg << std::endl;
        return 1;
    }

    std::vector<boost::thread> msg_thread_vec;
    for (uint32_t queue_index = 0; queue_index < g_queue_size; ++queue_index)
    {
        msg_thread_vec.push_back(
                adk_impl::boost_thread("gm", 
                                       ("generate message" + std::to_string(queue_index)).c_str(),
                                       boost::bind(GenerateMsg, &recorder, queue_index)));
    }

    boost::thread print_thread 
                = adk_impl::boost_thread("pt", 
                                         "print thread", 
                                         boost::bind(PrintQueueStatus, &recorder));

    do
    {
        sleep(1);
        std::cout << "serialize msg " << g_serialize_count << std::endl;
    } while (g_serialize_count != g_pre_thread_send_total * g_queue_size);

    for (auto& msg_thread : msg_thread_vec)
    {
        if (msg_thread.joinable())
        {
            msg_thread.join();
        }
    }

    sleep(2);  // 等待数据写文件完成
    g_is_running = false;
    if (print_thread.joinable())
    {
        print_thread.join();
    }
    recorder.Stop();

    return 0;
}