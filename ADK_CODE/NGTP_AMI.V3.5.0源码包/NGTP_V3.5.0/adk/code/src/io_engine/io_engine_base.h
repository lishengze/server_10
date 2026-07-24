#ifndef ADK_IMPL_IO_ENGINE_BASE_H_
#define ADK_IMPL_IO_ENGINE_BASE_H_

#include "message_impl.h"
#include "message_pool.h"

#include <adk/io_engine/property.h>

namespace adk_impl
{

namespace io_engine
{

class EventHandler;
class AcceptHandler;
class PreSendHandler;
class PreRecvHandler;
class ConnectHandler;
class DecodeTemplate;
class MessageHandler;
class HeartbeatHandler;

class IoEngineBase
{
public:
    IoEngineBase();
    ~IoEngineBase();

    int32_t Init(const Property& engine_props);

    void Exit();

    const std::string& engine_name() const
    {
        return engine_name_;
    }

    inline MessageImpl* NewTxMessage(uint32_t len)
    {
        return tx_memory_pool_.NewMessage(len);
    }

    PreSendHandler* pre_send_handler() const
    {
        return pre_send_handler_;
    }

    PreRecvHandler* pre_recv_handler() const
    {
        return pre_recv_handler_;
    }

    EventHandler* blank_event_handler() const
    {
        return blank_event_handler_;
    }

    AcceptHandler* blank_accept_handler() const
    {
        return blank_accept_handler_;
    }

    DecodeTemplate* blank_decode_template() const
    {
        return blank_decode_template_;
    }

    MessageHandler* blank_message_handler() const
    {
        return blank_message_handler_;
    }

    HeartbeatHandler* blank_heartbeat_handler() const
    {
        return blank_heartbeat_handler_;
    }

    inline static void DeleteTxMessage(MessageImpl* message_impl)
    {
        TxMessagePool::DeleteMessage(message_impl);
    }

    inline static void DeleteRxMessage(MessageImpl* message_impl)
    {
        RxMessagePool::DeleteMessage(message_impl);
    }

    bool sub_reference()
    {
        assert(reference_cnt_ > 0);
        return (0 == --reference_cnt_);
    }

    void add_reference()
    {
        ++reference_cnt_;
    }

protected:
    std::string       engine_name_;
    uint32_t          reference_cnt_;

    EventHandler*     default_event_handler_;
    AcceptHandler*    default_accept_handler_;
    ConnectHandler*   default_connect_handler_;

private:
    TxMessagePool     tx_memory_pool_;
    PreSendHandler*   pre_send_handler_;
    PreRecvHandler*   pre_recv_handler_;

    EventHandler*     blank_event_handler_;
    AcceptHandler*    blank_accept_handler_;
    DecodeTemplate*   blank_decode_template_;
    MessageHandler*   blank_message_handler_;
    HeartbeatHandler* blank_heartbeat_handler_;
};

}

}

#endif