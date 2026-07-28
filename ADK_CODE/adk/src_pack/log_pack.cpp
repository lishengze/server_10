#include <adk/log.h>
#include <adk_pack/log.h>

namespace adk
{

namespace log
{

using IntervalLoggerImpl = adk_impl::log::IntervalLogger;

IntervalLogger::IntervalLogger(size_t seconds)
{
    interval_log_impl_ = new IntervalLoggerImpl(seconds);
}

IntervalLogger::~IntervalLogger()
{
    delete reinterpret_cast<IntervalLoggerImpl*>(interval_log_impl_);
    interval_log_impl_ = nullptr;
}

bool IntervalLogger::ToLog()
{
    return reinterpret_cast<IntervalLoggerImpl*>(interval_log_impl_)->ToLog();
}

}

}