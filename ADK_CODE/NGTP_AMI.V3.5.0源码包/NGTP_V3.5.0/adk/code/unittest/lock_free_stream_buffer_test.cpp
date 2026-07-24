#define BOOST_TEST_MODULE lock_free_stream_buffer
#include <boost/test/included/unit_test.hpp>
#include <adk/lock_free_stream_buffer.h>
#include <adk/util.h>

#include <thread>
#include <iostream>
#include <time.h>
#include <stdlib.h>

//模拟TcpDecoder的数据结构
struct MessageHeader
{
    uint32_t length;
    uint16_t source_port;
    uint16_t dest_port;
    uint32_t source_ip;
    uint32_t dest_ip;
    uint64_t seq_num;
    uint8_t  data[];
};

constexpr uint64_t kTestMsgCnt = 10000000UL;
constexpr uint32_t kMillionBytes = 1024 * 1024;
constexpr uint32_t kTestMessageSizeMax = 2048;
constexpr uint32_t kLowercaseLettersNum = 26;

inline void OnIdle(uint32_t loop_counter)
{
    for (uint32_t index = 0; index < loop_counter; ++index)
    {
        ADK_PAUSE();
    }
}

BOOST_AUTO_TEST_CASE(unbounded_stream_buffer)
{
    auto* const stream_buffer = adk::UnboundedStreamBuffer::Create(8 * kMillionBytes,
                                                                   1 * kMillionBytes,
                                                                   8,
                                                                   1024);
    assert(stream_buffer);

    std::thread producer = std::thread([=]() {
        uint64_t producer_counter = 0;
        srand((int)time(0));
        do
        {
            uint32_t message_size = (uint32_t)rand() % kTestMessageSizeMax;
            auto payload_size = message_size + sizeof(struct MessageHeader);
            auto buffer = stream_buffer->AllocateBuffer(payload_size);
            
            if (nullptr != buffer)
            {
                MessageHeader* msg = (MessageHeader*)buffer;
                msg->length = message_size;
                msg->source_port = 1;
                msg->dest_port = 2;
                msg->source_ip = 3;
                msg->dest_ip = 4;
                msg->seq_num = ++producer_counter;
                memset(msg->data, 'a' + (producer_counter % kLowercaseLettersNum), message_size);
                stream_buffer->PostBuffer(payload_size);
            }
            else
            {
                OnIdle(1);
            }
            OnIdle(1);
        } while (producer_counter < kTestMsgCnt);
    });

    uint64_t consumer_counter = 1;
    std::thread consumer = std::thread([&]() {
        do 
        {
            auto buffer = stream_buffer->WaitBuffer();
            volatile auto payload_size = buffer.second;
            if (payload_size > 0)
            {
                volatile MessageHeader* msg = (MessageHeader*)(buffer.first);
                uint32_t message_size = msg->length;

                BOOST_REQUIRE(payload_size >= (message_size + sizeof(struct MessageHeader)));
                BOOST_REQUIRE(msg->source_port == 1);
                BOOST_REQUIRE(msg->dest_port == 2);
                BOOST_REQUIRE(msg->source_ip == 3);
                BOOST_REQUIRE(msg->dest_ip == 4);
                BOOST_REQUIRE(msg->length == message_size);
                BOOST_REQUIRE(msg->seq_num == consumer_counter);

                if (message_size)
                {
                    BOOST_REQUIRE(msg->data[message_size - 1] == ('a' + (consumer_counter % kLowercaseLettersNum)));
                }
                stream_buffer->FreeBuffer(message_size + sizeof(struct MessageHeader));
                consumer_counter++;
            }
            else
            {
                OnIdle(1);
            }
        } while (consumer_counter <= kTestMsgCnt);
    });

    uint64_t last_value = 0;
    do 
    {
        sleep(1);
        const auto temp_value = ACCESS_ONCE(consumer_counter);
        std::cout << "counter diff = " << temp_value - last_value << std::endl;
        last_value = temp_value;
    } while (last_value < kTestMsgCnt);

    consumer.join();
    producer.join();
}
