#ifndef ADK_IMPL_IO_ENGINE_MESSAGE_IMPL_H_
#define ADK_IMPL_IO_ENGINE_MESSAGE_IMPL_H_

#include <string.h>

#include <adk/arch/generic.h>
#include <adk/io_engine/message.h>
#include <adk/io_engine/tcp_engine.h>
namespace adk_impl
{

namespace io_engine
{

constexpr uint64_t kDirectionBit = ADK_DEL_ULL_CONST(1);
constexpr uint64_t kDirectionMask = ~kDirectionBit;

#define IS_RAW_POINTER(pointer) \
    (0 == (reinterpret_cast<uint64_t>(pointer) & kDirectionBit))

class MessageImpl : public Message
{
public:
    void GrowSize(uint32_t grow_size);

    ///> resize with no copy
    void Resize(uint32_t new_size);

    void Reset(uint32_t capacity)
    {
        data_len_ = 0;
        capacity_ = capacity;
        consume_len_ = 0;

        data_flag_ = 1;
        app_data_ = data_;
    }

    template<bool kDirecCtxSet>
    void Reset(uint32_t capacity, void* endpoint_ctx)
    {
        data_len_ = 0;
        capacity_ = capacity;
        consume_len_ = 0;

        if (kDirecCtxSet)
        {
            endpoint_ctx_ = endpoint_ctx;
        }
        else
        {
            set_endpoint_ctx<false>(endpoint_ctx);
        }
        app_data_ = data_;
    }

    void adden_consume_len(uint32_t len)
    {
        consume_len_ += len;
    }

    void set_consume_len(uint32_t len)
    {
        consume_len_ = len;
    }

    uint32_t consume_len() const
    {
        return consume_len_;
    }

    void set_data_len_impl(uint32_t data_len)
    {
        data_len_ = consume_len_ + data_len;
    }

    void set_app_data(char* app_data)
    {
        app_data_ = app_data;
    }

    inline void* app_data() const
    {
        return app_data_;
    }

    void set_data_more(int32_t data_more)
    {
        data_flag_ = data_more;
    }

    template<bool kValidCheck = false>
    void dec_data_more(int32_t dec_len)
    {
        if (kValidCheck)
        {
            assert((data_flag_ < 0) || (data_flag_ > dec_len));
            if (data_flag_ > dec_len)
            {
                data_flag_ -= dec_len;
            }
        }
        else
        {
            assert(data_flag_ > dec_len);
            data_flag_ -= dec_len;
        }
    }

    int32_t data_more() const
    {
        return data_flag_;
    }

    void revert_tail_len(uint32_t tail_len)
    {
        assert(data_len_ + tail_len <= capacity_);

        consume_len_ = data_len_;
        data_len_ += tail_len;
    }

    void set_reference(int32_t counter)
    {
        data_flag_ = counter;
    }

    int32_t get_reference() const 
    {
        return data_flag_;
    }

    bool is_last_reference()
    {
        if (1 == ACCESS_ONCE(data_flag_))
        {
            return true;
        }

        return (1 == __sync_fetch_and_sub(&data_flag_, 1));
    }

    void set_capacity(uint32_t capacity)
    {
        capacity_ = capacity;
    }

    uint32_t capacity() const
    {
        return capacity_;
    }

    void set_tx_callonce()
    {
        capacity_ = 0;
    }

    bool is_tx_called() const
    {
        return 0 == capacity_;
    }

    bool is_direction_tx() const
    {
        return IS_RAW_POINTER(endpoint_ctx_);
    }

    template<bool kIsTxDirect>
    void set_endpoint_ctx(void* const endpoint_ctx)
    {
        assert(IS_RAW_POINTER(endpoint_ctx));

        if (kIsTxDirect)
        {
            endpoint_ctx_ = endpoint_ctx;
        }
        else
        {
            endpoint_ctx_ = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(endpoint_ctx) | kDirectionBit);
        }
    }

    template<bool kIsTxDirect>
    void* endpoint_ctx() const
    {
        if (kIsTxDirect)
        {
            assert(is_direction_tx());
            return endpoint_ctx_;
        }
        else
        {
            assert(!is_direction_tx());
            return reinterpret_cast<void*>(reinterpret_cast<uint64_t>(endpoint_ctx_) & kDirectionMask);
        }
    }

    void set_rx_message_pool(void* const message_pool)
    {
        endpoint_ctx_ = message_pool;
    }

    void* rx_message_pool() const
    {
        return endpoint_ctx_;
    }

    bool is_resized_message() const
    {
        return app_data_ != data_;
    }

    inline char* AllocBuffer(uint32_t buffer_size)
    {
        if (data_len_ + buffer_size <= capacity_)
        {
            return app_data_ + data_len_;
        }

        const auto msg_size = data_len();
        assert(msg_size <= capacity_);

        if (buffer_size <= capacity_ - msg_size)
        {
            assert(msg_size > 0);
            memmove(app_data_, const_data(), msg_size);

            consume_len_ = 0;
            data_len_ = msg_size;
            return app_data_ + msg_size;
        }

        GrowSize(buffer_size);
        return app_data_ + msg_size;
    }

    inline void PostBuffer(uint32_t post_size)
    {
        assert(data_len_ + post_size <= capacity_);
        data_len_ += post_size;
    }

    inline void AppendBuffer(void* buffer, uint32_t buffer_size)
    {
        auto* const alloc_buffer = AllocBuffer(buffer_size);
        assert(alloc_buffer);

        memcpy(alloc_buffer, buffer, buffer_size);
        PostBuffer(buffer_size);
    }

    inline void FreeBuffer()
    {
        consume_len_ = 0;
        data_len_ = 0;
    }

    static constexpr size_t PrefixSize()
    {
        return ((size_t)&(((MessageImpl*)0)->data_));
    }

        
    int32_t get_fanout() const 
    {
        assert(is_direction_tx());
        return get_reference();
    }

    MessageImpl * TxMessageClone(TcpEngine* engine) const  
    {
        auto len = data_len();
        assert(is_direction_tx());

        MessageImpl* other = reinterpret_cast<MessageImpl*>(engine->NewMessage(len));
        /*
           NewMessage  中已经设置了: 
            data_len_ = 0 , 
            capacity_ = capacity = len;
            consume_len_ = 0;
            app_data_ = data_;
        */
        other->endpoint_ctx_ = endpoint_ctx_;
        other->data_flag_ = 1;
       
        //复制数据
        memcpy(other->data_, const_data(), len);
        other->data_len_ = len;
        return other;
    }

};

}

}

#endif