#include <iostream>

#include <boost/thread.hpp>

#include <adk/writer_wait_free_ring_buffer.h>
using namespace adk;

volatile bool g_start_running = false;

uint64_t g_feed_counter = 1;
uint64_t g_loss_counter = 0;

void Reader(WWFRingBuffer<uint64_t>* ring_buffer)
{
    WWFRingBuffer<uint64_t>* reader_buffer = ring_buffer->Duplicate();
    uint64_t read_counter;
    uint64_t last_counter = 0;

    while (!g_start_running);
    while (true)
    {
        if (ADK_UNLIKELY(kSuccess != reader_buffer->Read(read_counter)))
        {
            for (int index = 0; index < 128; ++index)
            {
                ADK_PAUSE();
            }
            continue;
        }

        if (ADK_UNLIKELY(read_counter <= last_counter))
        {
            std::cout << "BUG ON! Line:" << __LINE__
                      << ", read_counter = " << read_counter
                      << ", last_counter = " << last_counter << std::endl;
            abort();
        }

        g_loss_counter += (read_counter - last_counter - 1);
        last_counter = read_counter;
    }
}

void Writer(WWFRingBuffer<uint64_t>* ring_buffer)
{
    WWFRingBuffer<uint64_t>* writer_buffer = ring_buffer->Duplicate();

    while (!g_start_running);
    while (true)
    {
        writer_buffer->Write(g_feed_counter++);
    }
}

int main()
{
    WWFRingBuffer<uint64_t>* ring_buffer = WWFRingBuffer<uint64_t>::Create(8192);
    boost::thread writer_thread = boost::thread(boost::bind(Writer, ring_buffer));
    boost::thread reader_thread = boost::thread(boost::bind(Reader, ring_buffer));
    g_start_running = true;

    uint64_t last_feed_counter = 0;
    uint64_t last_loss_counter = 0;
    while (true)
    {
        sleep(1);

        uint64_t temp_feed_counter = g_feed_counter;
        uint64_t temp_loss_counter = g_loss_counter;
        std::cout << "feed counter diff = " << (temp_feed_counter - last_feed_counter) << std::endl;
        std::cout << "loss data counter = " << (temp_loss_counter - last_loss_counter) << std::endl;

        last_feed_counter = temp_feed_counter;
        last_loss_counter = temp_loss_counter;
    }

    writer_thread.join();

    return 0;
}