/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_SERIAL_WORKER_CPP_
#define AMI_SERIAL_WORKER_CPP_

///< adk, ami public
#include <adk/arch/generic.h>

///< impl
#include "serial_worker.h"

namespace ami
{

LOG_DEFINE(ami::SerialWorker)

uint32_t SerialWorker::s_delay_us = 1;

SerialWorker::~SerialWorker()
{
    if (!exit_)
    {
        exit_ = true;
        if (thread_.joinable())
            thread_.join();
    }
}

void SerialWorker::Launch()
{
    thread_ = adk::boost_thread("ami-serialworker", "process thread", boost::bind(&SerialWorker::Run, this));
}

void SerialWorker::PostJob(const Job& job)
{
    if (exit_)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(new_jobs_mutex_);
    new_jobs_.push_back(job);
    have_new_job_ = true;
}

void SerialWorker::Run()
{
    LOG_DEBUG("running...");

    try
    {
        while (!exit_)
        {
            DoJobs();
            if (ADK_UNLIKELY(live_jobs_.empty() && !have_new_job_)
                || ADK_UNLIKELY(true == all_idle_))
            {
                usleep(s_delay_us);
            }
        }

        //退出以后再尝试将剩下的执行一下
        LOG_TRACE("finish jobs before exit.");
        DoJobs();
    }
    catch (const std::exception& exp)
    {
        LOG_WARN("exit abnormally: {1}.", exp.what());
        return;
    }
    catch (...)
    {
        LOG_WARN("exit abnormally, catch unknown exception.");
        return;
    }

    LOG_DEBUG("exit normally.");
}

void SerialWorker::Stop()
{
    LOG_DEBUG("stop");
    exit_ = true;
    if (thread_.joinable())
        thread_.join();
}

void SerialWorker::DoJobs()
{
    if (ADK_UNLIKELY(have_new_job_))
    {
        std::lock_guard<std::mutex> lock(new_jobs_mutex_);
        live_jobs_.insert(live_jobs_.end(),
                          new_jobs_.begin(), new_jobs_.end());
        new_jobs_.clear();
        have_new_job_ = false;
    }

    all_idle_ = true;
    for (auto job_it = live_jobs_.begin();
         job_it != live_jobs_.end();)
    {
        JobStatus s = (*job_it)();
        switch (s)
        {
        case JobStatus::JS_YIELD:
            all_idle_ = false;
            ++job_it;
            break;
        case JobStatus::JS_IDLE:
            ++job_it;
            break;
        case JobStatus::JS_STOP:
            job_it = live_jobs_.erase(job_it);
            break;
        }
    }
}

}  // namespace ami

#endif /* AMI_SERIAL_WORKER_CPP_ */
