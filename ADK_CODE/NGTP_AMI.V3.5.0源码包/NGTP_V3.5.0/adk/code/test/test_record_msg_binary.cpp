
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>

#include <adk/record_msg_binary.h>

#include <boost/filesystem.hpp>
#include <boost/exception/all.hpp>
#include <boost/filesystem/path.hpp>

using namespace adk;

static bool print_flag = true;

// 获取内存占用情况
uint32_t GetProcVmRSS()
{
    std::fstream proc_info("/proc/self/status", std::ios::in);
    std::string line;
    uint32_t vm_size_kb = 0;
    while (std::getline(proc_info, line))
    {
        std::stringstream ss(line);
        std::string key;
        ss >> key;
        if (key == "VmRSS:")
        {
            ss >> vm_size_kb;
            break;
        }
    }
    proc_info.close();
    return vm_size_kb;
}

void PrintProcInfo()
{
    while (print_flag)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        auto vm_size_kb = GetProcVmRSS();
        std::cout << "内存占用字节（MB）：" << vm_size_kb/1024 << std::endl;
    }
    std::cout<<"print process info is stop,and main is exit."<<std::endl;
    return ;
}

std::string serialize_func(RecordMsgBinary::BinaryMsgObject* object)
{
	std::string result =  object->binary_msg ; // 解析bianry_msg成消息
    return result ;
}

void record_msg_with_create_thread()
{
    RecordMsgBinary recordMsg ;
    recordMsg.SetSerializeFunc(serialize_func) ;
    static int32_t flag = 0 ;
    if (!recordMsg.Init("./test_log/record_" + std::to_string(++flag) + ".txt", false))
    {
        std::cout<<"Init failed"<<std::endl;
        return  ;
    }
    

    recordMsg.Start(true) ;
    int i   = 0;
    std::string str((size_t)(1024), (char)('a'));
    while(i < 1000000)
    {
        recordMsg.PutMsg(str.data(),str.length()) ;
        // usleep(0);
        i++ ;
    }

    std::this_thread::sleep_for(std::chrono::seconds(10)) ;
    recordMsg.Stop() ;

    std::cout << "nr_sys_mem:" << recordMsg.GetSysMem() << std::endl;

    std::cout<<"record msg is stop,and main is exit."<<std::endl;
    return ;
}

// void record_msg_with_io_service()
// {
//     boost::asio::io_service ios ;
//     boost::asio::io_service::work work_(ios) ;
//     RecordMsgBinary recordMsg(&ios) ;
//     recordMsg.SetSerializeFunc([=](RecordMsgBinary::BinaryMsgObject* object){
//         return object->binary_msg ;
//     }) ;

//     if(!recordMsg.Init("test.txt",true))
//     {
//         std::cout<<"Init failed"<<std::endl;
//         return  ;
//     }

//     recordMsg.Start(false) ;
//     //recordMsg.Start(true) ; 自身创建线程处理，调用RunIos将不起作用
//     std::thread put_msg_thrd = std::thread(
//         [&](){
//             int i   = 0;
//             while(i < 10000)
//             {
//                 std::string str=std::to_string(i) ;
//                 recordMsg.PutMsg(str.data(),str.length()) ;
//                 //std::this_thread::sleep_for(std::chrono::milliseconds(10)) ;
//                 i++ ;
//             }
//             std::this_thread::sleep_for(std::chrono::seconds(5)) ;
//         }) ;
    
//     std::thread run_with_ios = std::thread([&](){
//         ios.run() ;
//     }) ;
//     recordMsg.RunIos() ;

//     std::this_thread::sleep_for(std::chrono::seconds(200)) ;
//     recordMsg.Stop() ;
//     ios.stop() ;
//     if(put_msg_thrd.joinable())
//     {
//         put_msg_thrd.join() ;
//     }
//     if(run_with_ios.joinable())
//     {
//         run_with_ios.join() ;
//     }

//     std::cout<<"record msg is stop,and main is exit."<<std::endl;
//     return ;
// }

void test_with_abnormal_branch()
{
    RecordMsgBinary recordMsg ;
    recordMsg.SetSerializeFunc([=](RecordMsgBinary::BinaryMsgObject* object){
        return object->binary_msg ;
    }) ;

    if(!recordMsg.Init("./test_log/test.txt",true))
    {
        std::cout<<"Init failed"<<std::endl;
        return  ;
    }

    recordMsg.Start(true) ; //不启动start
    int i   = 0;
    while(i < 10000000000L)
    {
        std::string str=std::to_string(i) ;
        recordMsg.PutMsg(str.data(),str.length()) ;
        std::this_thread::sleep_for(std::chrono::milliseconds(1)) ;
        i++ ;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1000000)) ;
    recordMsg.Stop() ;
    std::cout<<"record msg is stop,and main is exit."<<std::endl;
}

int main(int argc,char **argv)
{
    if(argc <2)
    {
        std::cout<<"只有一个参数, 值为1或其他整数, eg: ./test_record_msg_binary 1"<<std::endl;
        return 0 ;
    }
    int32_t flag = 0;
    try
    {
        flag = std::stoi(argv[1]) ;
    }
    catch(const std::exception& e)
    {
        std::cout << "参数1必须为整数!!!" << '\n';
        return -1;
    }
    
    std::string temp_path = "./test_log";
    try{
        boost::filesystem::path file_path(temp_path) ;
        if(!boost::filesystem::exists(file_path))
        {
            if(!boost::filesystem::create_directories(file_path))
            {
                std::cout << "在当前路径创建文件目录 <./test_log> 失败" << std::endl;
                return -1 ;
            }
        }
    }
    catch(...)
    {
        std::cout << "在当前路径创建文件目录 <./test_log> 异常" << std::endl;
        return -1 ;
    }

    std::thread print_test = std::thread(PrintProcInfo);
    if(flag ==1)
    //     record_msg_with_io_service() ;
    // else if(flag ==2)
    {
        std::vector<std::thread> s_thread_pool;
        for (uint32_t i = 0; i < 10; i++)
        {
            s_thread_pool.push_back(std::move(std::thread(std::bind(record_msg_with_create_thread))));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for(auto& test : s_thread_pool)
        {
            if (test.joinable())
                test.join();
        }
    }   
    else
        test_with_abnormal_branch();

    print_flag = false;
    if (print_test.joinable())
    {
        print_test.join();
    }
    sleep(2);
    try{
        boost::filesystem::path file_path(temp_path) ;
        if(boost::filesystem::exists(file_path))
        {
            if(!boost::filesystem::remove_all(file_path))
            {
                std::cout << "删除当前路径文件目录 <./test_log> 失败" << std::endl;
                return -1 ;
            }
        }
    }
    catch(...)
    {
        std::cout << "删除当前路径文件目录 <./test_log> 异常" << std::endl;
        return -1 ;
    }

    return 0 ;
}
