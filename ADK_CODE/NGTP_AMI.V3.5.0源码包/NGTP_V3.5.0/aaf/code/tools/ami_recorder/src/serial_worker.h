/**
 * @brief 非特定任务串行执行者
 * @author Li Yunchong
 */
#ifndef AMI_SERIAL_WORKER_H_
#define AMI_SERIAL_WORKER_H_

///< cpp std
#include <list>
#include <mutex>

///< boost
#include <boost/function.hpp>
#include <boost/thread.hpp>

///< ami impl
#include "../log.h"
#include <adk/entry_wrapper.h>

namespace ami
{

class SerialWorker
{
public:
    enum class JobStatus
    {
        JS_YIELD,  ///< 配额用完，主动放弃cpu资源，防止饥饿发生
        JS_IDLE,  ///< 无事可做，放弃cpu资源
        JS_STOP,  ///< 停止
    };

    typedef boost::function<JobStatus()> Job;

    SerialWorker()                    = default;
    SerialWorker(const SerialWorker&) = delete;
    SerialWorker& operator=(const SerialWorker&) = delete;
    ~SerialWorker();

    void Launch();
    void PostJob(const Job& job);

    /**
     * 停止
     *
     * @note 执行本方法的线程必须和本worker的工作线程为不同的线程。
     * 执行本方法的线程会阻塞，直至本worker的线程函数返回
     */
    void Stop();

    static void set_delay_us(uint32_t val) { s_delay_us = val; }

private:
    typedef std::list<Job> JobList;

    void Run();
    void DoJobs();

    volatile bool exit_ = false;
    boost::thread thread_;

    mutable std::mutex new_jobs_mutex_;
    JobList new_jobs_;
    bool have_new_job_ = false;

    JobList live_jobs_;
    bool all_idle_ = false;

    static uint32_t s_delay_us;

    LOG_DECLARE
};

}  // namespace ami

#endif /* AMI_SERIAL_WORKER_H_ */
