#include <adk_pack/spsc_byte_buffer.h>
#include <adk_pack/arch/generic.h>
#include <iostream>
#include <thread>
#include <unistd.h>

using namespace adk;
using bytebuffer::ByteBuffer;

const uint64_t kTestLoop = 10000000;

void ProducerThrd(ByteBuffer *byte_buff, uint64_t &counter)
{
    char *data = new char[256];
    memset(data, 1, 256);
    byte_buff->GetBufferProducer()->Clear();
    auto *ptr = byte_buff->GetBufferProducer();

    while (counter < kTestLoop)
    {
        ptr->AppendBytes(data, 256);
        for (int i = 0; i < 16; ++i)
        {
            ADK_PAUSE();
        }
        if (counter % 2)
            ptr->IncValidData(512);
        ++counter;
    }
}

void ConsumerThrd(ByteBuffer *byte_buff, uint64_t &counter)
{
    auto *ptr = byte_buff->GetBufferConsumer();
    while (counter < kTestLoop / 2)
    {
        while (ptr->Remaining() < 512)
        {
            for (int i = 0; i < 64; ++i)
            {
                ADK_PAUSE();
            }
        }
        for (int i = 0; i < 64; ++i)
        {
            ADK_PAUSE();
        }
        ptr->ConsumeBytes(512);
        ++counter;
    }
}

int main(int argc, char const *argv[])
{
    ByteBuffer byte_buffer;
    if (ErrorCode::kSuccess != byte_buffer.Init(1024 * 1024 * 64))
    {
        std::cout << "Init byte buffer failed" << std::endl;
        return 1;
    }

    uint64_t produce_counter = 0, consume_counter = 0;
    char data[1024] = {0};
    // auto *ptr = byte_buffer.GetBufferProducer();
    // ptr->AppendBytes(data, sizeof(data));
    // ptr->Clear();
    std::thread produce(ProducerThrd, &byte_buffer, std::ref(produce_counter));
    std::thread consum(ConsumerThrd, &byte_buffer, std::ref(consume_counter));

    while (consume_counter < kTestLoop / 2)
    {
        sleep(1);
        std::cout << "counter diff: " << produce_counter - consume_counter - consume_counter
                  << "\t " << produce_counter << "  :  " << consume_counter << std::endl;
    }

    produce.join();
    consum.join();
    return 0;
}