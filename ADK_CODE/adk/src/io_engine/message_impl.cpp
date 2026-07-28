#include "message_impl.h"
#include "message_pool.h"

namespace adk_impl
{

namespace io_engine
{

void MessageImpl::GrowSize(uint32_t grow_size)
{
    const auto msg_size = data_len();
    const auto allocate_size = msg_size + grow_size;
    void* const resize_buffer = RxMemoryPool::NewMemory(allocate_size);
    assert(resize_buffer);

    if (msg_size > 0)
    {
        memcpy(resize_buffer, const_data(), msg_size);
        consume_len_ = 0;
        data_len_ = msg_size;
    }

    capacity_ = allocate_size;
    if (is_resized_message())
    {
        RxMemoryPool::DeleteMemory(app_data_);
    }

    app_data_ = (char*)resize_buffer;
}

void MessageImpl::Resize(uint32_t new_size)
{
    assert(new_size > capacity_);

    void* const resize_buffer = RxMemoryPool::NewMemory(new_size);
    assert(resize_buffer);

    capacity_ = new_size;
    if (is_resized_message())
    {
        RxMemoryPool::DeleteMemory(app_data_);
    }

    app_data_ = (char*)resize_buffer;
}

}

}