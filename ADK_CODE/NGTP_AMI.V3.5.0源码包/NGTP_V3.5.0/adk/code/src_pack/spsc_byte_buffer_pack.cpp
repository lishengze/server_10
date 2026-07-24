#include <adk/spsc_byte_buffer.h>
#include <adk_pack/arch/generic.h>
#include <adk_pack/spsc_byte_buffer.h>
#include <stdlib.h>
#include <malloc.h>

namespace adk
{

namespace bytebuffer
{

using ProducerImpl = adk_impl::bytebuffer::Producer;
using ConsumerImpl = adk_impl::bytebuffer::Consumer;
using ByteBufferImpl = adk_impl::bytebuffer::ByteBuffer;


int32_t Producer::remaining()
{
    return reinterpret_cast<ProducerImpl *>(this)->remaining();
}

char *Producer::current()
{
    return reinterpret_cast<ProducerImpl *>(this)->current();
}

char *Producer::Current()
{
    return reinterpret_cast<ConsumerImpl *>(this)->current();
}

int32_t Producer::position()
{
    return reinterpret_cast<ProducerImpl *>(this)->position();
}

void Producer::inc_position(int32_t delta)
{
    reinterpret_cast<ProducerImpl *>(this)->inc_position(delta);
}

void Producer::inc_valid_data(int32_t delta_visible_bytes)
{
    reinterpret_cast<ProducerImpl *>(this)->inc_valid_data(delta_visible_bytes);
}

void Producer::set_valid_data(int32_t visible_bytes)
{
    reinterpret_cast<ProducerImpl *>(this)->set_valid_data(visible_bytes);
}

int32_t Producer::valid_data()
{
    return reinterpret_cast<ProducerImpl *>(this)->valid_data();
}

int32_t Producer::RenewBuffer(bool is_non_block)
{
    return reinterpret_cast<ProducerImpl *>(this)->RenewBuffer(is_non_block);
}

void Producer::AppendBytes(const void *src, int32_t len)
{
    reinterpret_cast<ProducerImpl *>(this)->AppendBytes(src, len);
}

int32_t Producer::AppendBytesSize()
{
    return reinterpret_cast<ProducerImpl *>(this)->AppendBytesSize();
}

char *Producer::ValidDataEnd()
{
    return reinterpret_cast<ProducerImpl *>(this)->ValidDataEnd();
}

int32_t Producer::IncValidData(int32_t len, bool is_non_block)
{
    return reinterpret_cast<ProducerImpl *>(this)->IncValidData(len, is_non_block);
}

void Producer::Clear()
{
    reinterpret_cast<ProducerImpl *>(this)->Clear();
}


int32_t Consumer::position()
{
    return reinterpret_cast<ConsumerImpl *>(this)->position();
}

void Consumer::consume_bytes(int32_t bytes)
{
    reinterpret_cast<ConsumerImpl *>(this)->consume_bytes(bytes);
}

void Consumer::ConsumeBytes(int32_t bytes)
{
    reinterpret_cast<ConsumerImpl *>(this)->consume_bytes(bytes);
}

int32_t Consumer::remaining()
{
    return reinterpret_cast<ConsumerImpl *>(this)->remaining();
}

int32_t Consumer::Remaining()
{
    return reinterpret_cast<ConsumerImpl *>(this)->remaining();
}

char *Consumer::current()
{
    return reinterpret_cast<ConsumerImpl *>(this)->current();
}

char *Consumer::Current()
{
    return reinterpret_cast<ConsumerImpl *>(this)->current();
}


ByteBuffer::ByteBuffer()
{
    buff_impl_ = new ByteBufferImpl;
}

int32_t ByteBuffer::Init(int32_t buffer_size, int32_t extra_size)
{
    return reinterpret_cast<ByteBufferImpl *>(buff_impl_)->Init(buffer_size, extra_size);
}

/**
     * @brief      用于生产者线程生产消息
     *
     * @return     返回生产者端
     */
Producer *ByteBuffer::GetBufferProducer()
{
    ProducerImpl *producer_impl = reinterpret_cast<ByteBufferImpl *>(buff_impl_)->GetBufferProducer();
    return reinterpret_cast<Producer *>(producer_impl);
}

/**
     * @brief      用于消费者者线程消费消息
     *
     * @return     返回消费者端
     */
Consumer *ByteBuffer::GetBufferConsumer()
{
    ConsumerImpl* consumer_impl = reinterpret_cast<ByteBufferImpl *>(buff_impl_)->GetBufferConsumer();
    return reinterpret_cast<Consumer *>(consumer_impl);
}

} // namespace bytebuffer

} // namespace adk