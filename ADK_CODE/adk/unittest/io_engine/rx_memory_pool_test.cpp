#define BOOST_TEST_MODULE rx_memory_pool

#include <vector>
#include <message_pool.h>

#include <boost/format.hpp>
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(test_RxMemoryPool)
{
    constexpr uint32_t kBlockSize = 1024;
    constexpr uint32_t kBlockNum = 1024;

    auto* const memory_pool = adk::io_engine::RxMemoryPool::Create(kBlockSize, kBlockNum);
    BOOST_CHECK(memory_pool);

    BOOST_TEST_MESSAGE((boost::format("memory pool = %1%") % memory_pool).str());
    BOOST_TEST_MESSAGE((boost::format("memory header = %1%") % memory_pool->memory_header_).str());

    BOOST_CHECK_GE(memory_pool->block_size(), kBlockSize);
    BOOST_TEST_MESSAGE((boost::format("block size = %1%") % memory_pool->block_size()).str());

    uint32_t memory_diff = 0;
    void* test_buffer_array1[kBlockNum];
    for (uint32_t index = 0; index < kBlockNum; ++index)
    {
        auto* const buffer = memory_pool->NewMemory();
        BOOST_CHECK(buffer);

        memset(buffer, 0, memory_pool->block_size());

        auto* const memory_block = adk::variant::MemoryBlock::block(buffer);
        BOOST_CHECK_EQUAL(memory_block->block_ctx(), 
                          reinterpret_cast<int64_t>(memory_pool->memory_cache_queue_));

        test_buffer_array1[index] = buffer;

        if (0 == index)
        {
            BOOST_CHECK_EQUAL(memory_pool->memory_header_, memory_block);
        }
        else if (1 == index)
        {
            memory_diff = (char*)buffer - (char*)(test_buffer_array1[0]);
            BOOST_TEST_MESSAGE((boost::format("memory diff = %1%") % memory_diff).str());
            BOOST_CHECK_GE(memory_diff, memory_pool->block_size());
        }
        else
        {
            BOOST_CHECK_EQUAL(memory_diff, (char*)buffer - (char*)(test_buffer_array1[index - 1]));
        }
    }

    BOOST_TEST_MESSAGE((boost::format("last_buffer1 = %1%") % test_buffer_array1[kBlockNum - 1]).str());

    void* test_buffer1 = memory_pool->NewMemory();
    BOOST_CHECK(test_buffer1);

    memset(test_buffer1, 0, memory_pool->block_size());

    BOOST_TEST_MESSAGE((boost::format("test_buffer1 = %1%") % test_buffer1).str());

    BOOST_CHECK_NE(memory_diff, (char*)test_buffer1 - (char*)test_buffer_array1[kBlockNum - 1]);

    void* test_buffer2 = adk::io_engine::RxMemoryPool::NewMemory(kBlockSize);
    BOOST_CHECK(test_buffer2);

    memset(test_buffer2, 0, kBlockSize);

    BOOST_TEST_MESSAGE((boost::format("test_buffer2 = %1%") % test_buffer2).str());

    void* test_buffer3 = memory_pool->NewMemory();
    BOOST_CHECK(test_buffer3);

    memset(test_buffer3, 0, memory_pool->block_size());

    BOOST_TEST_MESSAGE((boost::format("test_buffer3 = %1%") % test_buffer3).str());

    const auto memory_diff_temp = (char*)test_buffer3 - (char*)test_buffer1;
    BOOST_TEST_MESSAGE((boost::format("test_buffer3 - test_buffer1 = %1%") % memory_diff_temp).str());

    void* test_buffer_array2[kBlockNum];
    memcpy(test_buffer_array2, test_buffer_array1, sizeof(void*) * kBlockNum);

    adk::io_engine::RxMemoryPool::DeleteMemory(test_buffer3);
    for (uint32_t index = 0; index < kBlockNum / 2; ++index)
    {
        adk::io_engine::RxMemoryPool::DeleteMemory(test_buffer_array1[index]);
    }

    adk::io_engine::RxMemoryPool::DeleteMemory(test_buffer2);
    for (uint32_t index = kBlockNum / 2; index < kBlockNum; ++index)
    {
        adk::io_engine::RxMemoryPool::DeleteMemory(test_buffer_array1[index]);
    }

    adk::io_engine::RxMemoryPool::DeleteMemory(test_buffer1);

    for (uint32_t index = 0; index < kBlockNum; ++index)
    {
        auto* const buffer = memory_pool->NewMemory();
        BOOST_CHECK(buffer);

        memset(buffer, 0, memory_pool->block_size());
        test_buffer_array1[index] = buffer;

        if (1 == index)
        {
            memory_diff = (char*)buffer - (char*)(test_buffer_array1[0]);
            BOOST_CHECK_GE(memory_diff, memory_pool->block_size());
        }
        else if (index > 1)
        {
            BOOST_CHECK_EQUAL(memory_diff, (char*)buffer - (char*)(test_buffer_array1[index - 1]));
        }

        BOOST_CHECK_EQUAL(buffer, test_buffer_array2[index]);
    }

    test_buffer1 = memory_pool->NewMemory();
    BOOST_CHECK(test_buffer1);

    memset(test_buffer1, 0, memory_pool->block_size());

    BOOST_CHECK_NE(memory_diff, (char*)test_buffer1 - (char*)test_buffer_array1[kBlockNum - 1]);

    test_buffer2 = adk::io_engine::RxMemoryPool::NewMemory(kBlockSize);
    BOOST_CHECK(test_buffer2);

    memset(test_buffer2, 0, kBlockSize);

    test_buffer3 = memory_pool->NewMemory();
    BOOST_CHECK(test_buffer3);

    memset(test_buffer3, 0, memory_pool->block_size());

    adk::io_engine::RxMemoryPool::DeleteMemory(test_buffer3);
    for (uint32_t index = 0; index < kBlockNum / 2; ++index)
    {
        adk::io_engine::RxMemoryPool::DeleteMemory(test_buffer_array1[index]);
    }

    adk::io_engine::RxMemoryPool::DeleteMemory(test_buffer2);
    for (uint32_t index = kBlockNum / 2; index < kBlockNum; ++index)
    {
        adk::io_engine::RxMemoryPool::DeleteMemory(test_buffer_array1[index]);
    }

    adk::io_engine::RxMemoryPool::DeleteMemory(test_buffer1);

    adk::io_engine::RxMemoryPool::Destroy(memory_pool);
}
