#include <sys/syscall.h>
#include <unistd.h>

#include <iostream>

#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <adk_pack/record_msg_binary_v2.h>
#include <adk_pack/entry_wrapper.h>

struct MyMsg
{
    uint32_t msg_len;
    char msg_data[]; 
};

uint64_t g_serialize_count = 0;
uint64_t g_pre_thread_send_total = 60000;
uint64_t g_pre_thread_send_rate =  10000;
uint32_t g_queue_size = 8;
uint32_t g_queue_mem = 1024ul * 1024ul;
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

void GenerateMsg(adk::RecordMsgBinaryV2* recorder, uint64_t queue_index = 0)
{
    uint64_t delay_us = 1000ul * 1000ul / g_pre_thread_send_rate;
    for (uint64_t count = 1; count <= g_pre_thread_send_total; ++count)
    {
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
        usleep(delay_us);
    }
}

void PrintQueueStatus(adk::RecordMsgBinaryV2* recorder)
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
    boost::program_options::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "show this information")
        ("queue-num", boost::program_options::value<int>()->default_value(g_queue_size), "queue num")
        ("queue-mem", boost::program_options::value<int>()->default_value(g_queue_mem), "queue memory")
        ;
    
    boost::program_options::variables_map vm;

    try {
        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
        boost::program_options::notify(vm);    
    } catch(...)
    {
        std::cout << desc << "\n";
        return 1;
    }

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    if (vm.count("queue-num"))
    {
        g_queue_size = vm["queue-num"].as<int>();
    }

    if (vm.count("queue-mem"))
    {
        g_queue_mem = vm["queue-mem"].as<int>();
    }

    adk::RecordMsgBinaryV2 recorder(g_queue_size, g_queue_mem);
    std::string local_file_name = "./record_msg_binary_v2_test.txt";

    std::string error_msg;
    if (!recorder.Init(local_file_name, &error_msg))
    {
        std::cout << "recorder init failed, " << error_msg << std::endl;
        return 1;
    }

    recorder.SetSerializeFunc(MySerializeFunc);
    if (recorder.Start(&error_msg) != adk::ErrorCode::kSuccess)
    {
        std::cout << "recorder start failed, " << error_msg << std::endl;
        return 1;
    }

    std::vector<boost::thread> msg_thread_vec;
    for (uint32_t queue_index = 0; queue_index < g_queue_size; ++queue_index)
    {
        msg_thread_vec.push_back(
                adk::boost_thread("gm", 
                                 ("generate message" + std::to_string(queue_index)).c_str(),
                                 boost::bind(GenerateMsg, &recorder, queue_index)));
    }

    boost::thread print_thread 
                = adk::boost_thread("pt", 
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