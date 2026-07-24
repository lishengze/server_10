#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <iostream>
#include <adk/spsc_byte_buffer.h>
#include <adk/lock_free_stream_buffer.h>

uint32_t kTestSizeArray[6] = 
{
    128, 256, 512, 1024, 4096, 8192
};

struct Protocol
{
    uint32_t size;
    char     buffer[];
};

constexpr uint32_t kProtocolSize = ADK_OFFSET_OF(struct Protocol, buffer);

struct ObInfo
{
    ObInfo(const std::string& name)
    {
        actor_name = name;
        current_bytes = 0;
        last_ob_bytes = 0;
    }

    std::string actor_name;
    uint64_t current_bytes;
    uint64_t last_ob_bytes;
};

std::mutex g_lock;
std::vector<ObInfo*> g_info_vec;

inline void OnIdle()
{
#if 0
    for (int32_t index = 0; index < 256; ++index)
    {
        ADK_PAUSE();
    }
#else
    usleep(0);
#endif
}

void SbbProducer(adk::bytebuffer::Producer* producer)
{
    assert(producer);
    char buffer[8192];

    do
    {
        for (int32_t index = 0; index < 6; ++index)
        {
            const auto size = kTestSizeArray[index];
            auto* const protocol = (Protocol*)(&buffer[0]);

            protocol->size = size;
            producer->AppendBytes(protocol, size);

            while (ADK_UNLIKELY(adk::ErrorCode::kSuccess != producer->IncValidData(size, true)))
            {
                OnIdle();
            }
        }
    } while (true);
}

void SbbConsumer(adk::bytebuffer::Consumer* consumer)
{
    assert(consumer);
    
    ObInfo ob_info("<   spsc byte buffer    >");
    {
        std::lock_guard<std::mutex> _(g_lock);
        g_info_vec.push_back(&ob_info);
    }

    do
    {
        const auto remaining_size = consumer->Remaining();
        if (remaining_size > kProtocolSize)
        {
            auto* const protocol = (struct Protocol*)(consumer->Current());
            if (protocol->size <= remaining_size)
            {
                ob_info.current_bytes += protocol->size;
                consumer->ConsumeBytes(protocol->size);
                continue;
            }
        }

        OnIdle();
    } while (true);

    {
        std::lock_guard<std::mutex> _(g_lock);
        auto iter = std::find(g_info_vec.begin(), g_info_vec.end(), &ob_info);
        assert(g_info_vec.end() != iter);
        g_info_vec.erase(iter);
    }
}

void LfsbProducer(adk::StreamBuffer* stream_buffer)
{
    assert(stream_buffer);
    char buffer[8192];

    do
    {
        for (int32_t index = 0; index < 6; ++index)
        {
            const auto size = kTestSizeArray[index];
            auto* const protocol = (Protocol*)(&buffer[0]);

            protocol->size = size;
            while (ADK_UNLIKELY(adk::ErrorCode::kSuccess != stream_buffer->Push((char*)protocol, size)))
            {
                OnIdle();
            }
        }
    } while (true);
}

void LfsbConsumer(adk::StreamBuffer* stream_buffer)
{
    assert(stream_buffer);

    ObInfo ob_info("<lock free stream buffer>");
    {
        std::lock_guard<std::mutex> _(g_lock);
        g_info_vec.push_back(&ob_info);
    }

    do
    {
        auto wait_entry = stream_buffer->WaitBuffer();
        if (wait_entry.second > kProtocolSize)
        {
            auto* const protocol = (struct Protocol*)(wait_entry.first);
            if (protocol->size <= wait_entry.second)
            {
                ob_info.current_bytes += protocol->size;
                stream_buffer->FreeBuffer(protocol->size);
                continue;
            }
        }

        OnIdle();
    } while (true);

    {
        std::lock_guard<std::mutex> _(g_lock);
        auto iter = std::find(g_info_vec.begin(), g_info_vec.end(), &ob_info);
        assert(g_info_vec.end() != iter);
        g_info_vec.erase(iter);
    }
}

int main()
{
    constexpr uint32_t kBufferSize = 8 * 1024 * 1024;
    constexpr uint32_t kExtraSize = 1 * 1024 * 1024;

    adk::bytebuffer::ByteBuffer byte_buffer;
    if (adk::ErrorCode::kSuccess != byte_buffer.Init(kBufferSize, kExtraSize))
    {
        std::cout << "byte buffer init failed ..." << std::endl;
        return 0;
    }

    auto* const sbb_producer = byte_buffer.GetBufferProducer();
    if (nullptr == sbb_producer)
    {
        std::cout << "spsc ByteBuffer GetBufferProducer failed" << std::endl;
        return 0;
    }

    auto* const sbb_consumer = byte_buffer.GetBufferConsumer();
    if (nullptr == sbb_consumer)
    {
        std::cout << "spsc ByteBuffer GetBufferConsumer failed" << std::endl;
        return 0;
    }

    auto* const lfsb_buffer = adk::StreamBuffer::Create(kBufferSize, kExtraSize);
    if (nullptr == lfsb_buffer)
    {
        std::cout << "create lock free stream buffer failed" << std::endl;
        return 0;
    }

    std::thread sbb_producer_thrd = std::thread(SbbProducer, sbb_producer);
    std::thread sbb_consumer_thrd = std::thread(SbbConsumer, sbb_consumer);
    std::thread lfsb_producer_thrd = std::thread(LfsbProducer, lfsb_buffer);
    std::thread lfsb_consumer_thrd = std::thread(LfsbConsumer, lfsb_buffer);

    do
    {
        sleep(1);

        std::lock_guard<std::mutex> _(g_lock);
        for (auto& node : g_info_vec)
        {
            const auto current_bytes = *(volatile uint64_t*)(&(node->current_bytes));
            std::cout << node->actor_name << ", bytes diff = " 
                      << current_bytes - node->last_ob_bytes << std::endl; 
            node->last_ob_bytes = current_bytes;
        }
    } while (true);

    return 0;
}