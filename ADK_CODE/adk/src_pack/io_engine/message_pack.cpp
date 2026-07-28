#include <adk/io_engine/message.h>
#include <adk_pack/io_engine/message.h>

namespace adk
{

namespace io_engine
{

using MessageImp = adk_impl::io_engine::Message;

char* Message::data() const
{
    return reinterpret_cast<const MessageImp*>(this)->data();
}

const char* Message::const_data() const
{
    return reinterpret_cast<const MessageImp*>(this)->const_data();
}

uint32_t Message::data_len() const
{
    return reinterpret_cast<const MessageImp*>(this)->data_len();
}

uint32_t Message::free_size() const
{
    return reinterpret_cast<const MessageImp*>(this)->free_size();
}

void Message::set_data_len(uint32_t len)
{
    reinterpret_cast<MessageImp*>(this)->set_data_len(len);
}

void Message::set_follow_up(uint32_t consume_len, int32_t data_more)
{
    reinterpret_cast<MessageImp*>(this)->set_follow_up(consume_len, data_more);
}

Endpoint* Message::endpoint()
{
    return (Endpoint*)(reinterpret_cast<MessageImp*>(this)->endpoint());
}

void* Message::endpoint_share_ctx()
{
    return reinterpret_cast<MessageImp*>(this)->endpoint_share_ctx();
}

int32_t Message::forward_acquire()
{
    return reinterpret_cast<MessageImp*>(this)->forward_acquire();
}

int32_t Message::set_fanout(uint32_t fanout_nr)
{
    return reinterpret_cast<MessageImp*>(this)->set_fanout(fanout_nr);
}

int32_t Message::ReplyBlock(Message* msg)
{
    return reinterpret_cast<MessageImp*>(this)->Reply<true>((MessageImp*)msg);
}

int32_t Message::ReplyUnBlock(Message* msg)
{
    return reinterpret_cast<MessageImp*>(this)->Reply<false>((MessageImp*)msg);
}

int32_t Message::ReplyBlock(const void* buffer, uint32_t len)
{
    return reinterpret_cast<MessageImp*>(this)->Reply<true>(buffer, len);
}

int32_t Message::ReplyUnBlock(const void* buffer, uint32_t len)
{
    return reinterpret_cast<MessageImp*>(this)->Reply<false>(buffer, len);
}

}

}