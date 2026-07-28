#include "handler_impl.h"
#include "io_engine_base.h"

#include <adk/io_engine/property.h>
#include <adk/io_engine/config_key.h>

namespace adk_impl
{

namespace io_engine
{

IoEngineBase::IoEngineBase()
{
    reference_cnt_ = 1;

    default_event_handler_ = nullptr;
    default_accept_handler_ = nullptr;
    default_connect_handler_ = nullptr;

    pre_send_handler_ = nullptr;
    pre_recv_handler_ = nullptr;

    blank_event_handler_ = nullptr;
    blank_accept_handler_ = nullptr;
    blank_decode_template_ = nullptr;
    blank_message_handler_ = nullptr;
    blank_heartbeat_handler_ = nullptr;
}

IoEngineBase::~IoEngineBase()
{
    Exit();
}

int32_t IoEngineBase::Init(const Property& engine_props)
{
    Exit();

    engine_name_ = engine_props.GetValue(config::kName, std::string());

    default_event_handler_ = engine_props.GetValue(config::kEventHandler, Pointer())
                                         .as_ptr<EventHandler*>();

    default_accept_handler_ = engine_props.GetValue(config::kAcceptHandler, Pointer())
                                          .as_ptr<AcceptHandler*>();

    default_connect_handler_ = engine_props.GetValue(config::kConnectHandler, Pointer())
                                           .as_ptr<ConnectHandler*>();

    pre_send_handler_ = engine_props.GetValue(config::kPreSendHandler, Pointer())
                                    .as_ptr<PreSendHandler*>();

    pre_recv_handler_ = engine_props.GetValue(config::kPreRecvHandler, Pointer())
                                    .as_ptr<PreRecvHandler*>();

    blank_event_handler_ = new EventHandlerBlank;

    blank_accept_handler_ = new AcceptHandlerBlank;

    blank_decode_template_ = new DecodeTemplateBlank;

    blank_message_handler_ = new MessageHandlerBlank;

    blank_heartbeat_handler_ = new HeartbeatHandlerBlank;

    return ErrorCode::kSuccess;
}

void IoEngineBase::Exit()
{
    if (nullptr != blank_event_handler_)
    {
        delete blank_event_handler_;
        blank_event_handler_ = nullptr;
    }

    if (nullptr != blank_accept_handler_)
    {
        delete blank_accept_handler_;
        blank_accept_handler_ = nullptr;
    }

    if (nullptr != blank_decode_template_)
    {
        delete blank_decode_template_;
        blank_decode_template_ = nullptr;
    }

    if (nullptr != blank_message_handler_)
    {
        delete blank_message_handler_;
        blank_message_handler_ = nullptr;
    }

    if (nullptr != blank_heartbeat_handler_)
    {
        delete blank_heartbeat_handler_;
        blank_heartbeat_handler_ = nullptr;
    }

    default_event_handler_ = nullptr;
    default_accept_handler_ = nullptr;
    default_connect_handler_ = nullptr;

    pre_send_handler_ = nullptr;
    pre_recv_handler_ = nullptr;
}

}

}