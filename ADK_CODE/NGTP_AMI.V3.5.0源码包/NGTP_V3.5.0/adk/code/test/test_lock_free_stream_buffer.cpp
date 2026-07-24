#include <thread>
#include <iostream>
#include <adk/lock_free_stream_buffer.h>

struct MessageHeader
{
    uint32_t length;
    char     buffer[];
};

constexpr uint32_t kTestMessageSize = 127;
constexpr uint32_t kPayloadSize = kTestMessageSize - sizeof(struct MessageHeader);

inline void OnIdle(uint32_t loop_counter)
{
    for (uint32_t index = 0; index < loop_counter; ++index)
    {
        ADK_PAUSE();
    }
}

int main(int argc, char* argv[])
{
    auto* const stream_buffer = adk::UnboundedStreamBuffer::Create();
    assert(stream_buffer);

    std::thread consumer = std::thread([=]() {
        uint64_t consume_counter = 0;
        char message_consume[kPayloadSize];
        char* const message_buffer = message_consume;
        do
        {
            auto buffer1 = stream_buffer->AllocateBuffer();
            if (buffer1.second >= kTestMessageSize)
            {
                ((MessageHeader*)buffer1.first)->length = kTestMessageSize;
                *((uint64_t*)message_buffer) = ++consume_counter;
                memcpy(((MessageHeader*)buffer1.first)->buffer, message_buffer, kPayloadSize);
                stream_buffer->PostBuffer(kTestMessageSize);
            }

            auto buffer2 = stream_buffer->AllocateBuffer(kTestMessageSize);
            if (nullptr != buffer2)
            {
                ((MessageHeader*)buffer2)->length = kTestMessageSize;
                *((uint64_t*)message_buffer) = ++consume_counter;
                memcpy(((MessageHeader*)buffer2)->buffer, message_buffer, kPayloadSize);
                stream_buffer->PostBuffer(kTestMessageSize);
            }
            else
            {
                std::cout << "BUG ON <" << __LINE__ << ">" << std::endl;
            }

            OnIdle(1);
        } while (true);
    });

    uint64_t produce_counter = 1;
    char message_produce[kPayloadSize];
    std::thread producer = std::thread([&]() {
        char* const message_buffer = message_produce;
        do 
        {
            auto buffer = stream_buffer->WaitBuffer();
            if (buffer.second >= kTestMessageSize)
            {
                if (kTestMessageSize != ((MessageHeader*)buffer.first)->length)
                {
                    std::cout << "BUG ON, message size is error, buffer size = " 
                              << ((MessageHeader*)buffer.first)->length
                              << ", expect message = " << kTestMessageSize << std::endl;
                }

                memcpy(message_buffer, ((MessageHeader*)buffer.first)->buffer, kPayloadSize);
                const uint64_t value = *(uint64_t*)message_buffer;
                if (ADK_UNLIKELY(value != produce_counter))
                {
                    std::cout << "BUG ON, value = " << value 
                              << ", expect value = " << produce_counter << std::endl;
                }

                stream_buffer->FreeBuffer(kTestMessageSize);
                produce_counter = value + 1;
            }
        } while (true);
    });

    uint64_t last_value = 0;
    do 
    {
        sleep(1);
        const auto temp_value = ACCESS_ONCE(produce_counter);
        std::cout << "counter diff = " << temp_value - last_value << std::endl;
        last_value = temp_value;
    } while (true);

    consumer.join();
    producer.join();
    return 0;
}