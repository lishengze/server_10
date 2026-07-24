/**
 * @file
 * @brief      make sure the process is launched as a singleton inside the host.
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017-03-01
 */
#ifndef ADK_IMPL_SINGLETON_PROCESS_H_
#define ADK_IMPL_SINGLETON_PROCESS_H_

#include <string>
#include <fstream>
#include <iostream>

#include <adk/libadk.h>

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <boost/interprocess/sync/file_lock.hpp>

namespace adk_impl
{

/**
 * @brief     确保进程运行在同一主机上以单例模式运行
 */
class SingletonProcess
{
public:
    /**
     * @brief      构造函数
     *
     * @param[in]  锁文件的路径
     */
    SingletonProcess(const std::string& file_path)
        :   file_path_(file_path + ".pid"),
            is_locked_(false),
            file_lock_(NULL)
    {}

    /**
     * @brief      析构函数，该对象的生命周期应与创建该对象的进程相同
     *             对象析构时会释放Lock()接口锁上的文件锁
     */
    ADK_API ~SingletonProcess();

    /**
     * @brief      尝试对锁文件加锁，只有加锁成功的进程才能继续运行
     *
     * @return     成功时返回ErrorCode::kSuccess;
     */
    ADK_API int32_t Lock();
    
private:
    std::string                     file_path_;
    bool                            is_locked_;
    boost::interprocess::file_lock* file_lock_;
    std::ofstream                   pid_file_stream_;
};
} // adk

#endif // ADK_SINGLETON_PROCESS_H_
