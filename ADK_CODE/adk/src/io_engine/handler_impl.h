#ifndef ADK_IMPL_IO_ENGINE_HANDLER_IMPL_H_
#define ADK_IMPL_IO_ENGINE_HANDLER_IMPL_H_

#include <adk/error_code.h>
#include <adk/arch/generic.h>

#include <adk/io_engine/handler.h>
#include <adk/io_engine/endpoint.h>

namespace adk_impl
{

namespace io_engine
{

class EventHandlerBlank final : public EventHandler
{
public:
    void OnEvent(Endpoint* endpoint, Event* event) override
    {
        // do nothing
    }
};

class AcceptHandlerBlank final : public AcceptHandler
{
public:
    void OnAccept(Endpoint* endpoint, Property& ep_props) override
    {
        endpoint->Close(1);
    }
};

class DecodeTemplateBlank final : public DecodeTemplate
{
public:
    int32_t MessageLength(const void* msg_data, uint32_t len) override
    {
        return 0;
    }
};

class MessageHandlerBlank final : public MessageHandler
{
public:
    int32_t OnMessage(Message* message) override
    {
        // do nothing
        return ErrorCode::kSuccess;
    }
};

class HeartbeatHandlerBlank final : public HeartbeatHandler
{
public:
    void SendHBMsg(Endpoint* const endpoint) override
    {
        // do nothing
    }

    uint32_t GetPeriodMilli() override
    {
        return kuint32Max;
    }
};

}

}

#endif