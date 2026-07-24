#ifndef ADK_IMPL_IO_ENGINE_EVENT_IMPL_H_
#define ADK_IMPL_IO_ENGINE_EVENT_IMPL_H_

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <boost/format.hpp>
#include <adk/io_engine/event.h>

namespace adk_impl
{

namespace io_engine
{

class EventInfo : public Event
{
public:
    EventLevel level()
    {
        return EventLevel::kInfo;
    }
};

class EventWarn : public Event
{
public:
    EventLevel level()
    {
        return EventLevel::kWarn;
    }
};

class EventError : public Event
{
public:
    EventLevel level()
    {
        return EventLevel::kError;
    }
};

class EventFatal : public Event
{
public:
    EventLevel level()
    {
        return EventLevel::kFatal;
    }
};

class EventEpClosed final : public EventInfo
{
public:
    EventType type() override
    {
        return EventType::kEndpointClosed;
    }

    std::string what() override
    {
        return "endpoint is closed";
    }
};

class EventHBTO final : public EventWarn
{
public:
    EventType type() override
    {
        return EventType::kHeartbeatTimeout;
    }

    std::string what() override
    {
        return "heartbeat timeout";
    }
};

class EventNoEpResource final : public EventWarn
{
public:
    EventType type() override
    {
        return EventType::kNoResources;
    }

    std::string what() override
    {
        return "endpoints in process reach the upper limit";
    }
};

class EventConnectFail final : public EventError
{
public:
    EventType type() override
    {
        return EventType::kConnectFailed;
    }

    std::string what() override
    {
        return "connect to remote address failed";
    }
};

class EventSocketError final : public EventError
{
public:
    EventSocketError(const std::string& prefix, int32_t sys_errno)
    {
        if (!prefix.empty())
        {
            str_error_ = prefix + ", ";
        }

        str_error_ += (0 != sys_errno) ? strerror(sys_errno) 
                                       : "socket error";
    }

    EventSocketError(const std::string& prefix, const std::string& str_error)
    {
        if (!prefix.empty())
        {
            str_error_ = prefix + ", ";
        }

        str_error_ += str_error;
    }

    EventType type() override
    {
        return EventType::kSocketError;
    }

    std::string what() override
    {
        return str_error_;
    }

private:
    std::string str_error_;
};

class EventEndOfStream final : public EventError
{
public:
    EventType type() override
    {
        return EventType::kSocketError;
    }

    std::string what() override
    {
        return std::string("end of stream");
    }
};

class EventAddrInuse final : public EventError
{
public:
    EventType type() override
    {
        return EventType::kSocketError;
    }

    std::string what() override
    {
        return (boost::format("port <%1%> is inuse") % port_).str();
    }

    void set_port(uint64_t port)
    {
        port_ = port;
    }
private:
    uint64_t port_;
};

}

}

#endif