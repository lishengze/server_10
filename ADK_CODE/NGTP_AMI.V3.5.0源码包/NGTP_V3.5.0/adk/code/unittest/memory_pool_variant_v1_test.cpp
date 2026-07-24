#define BOOST_TEST_MODULE memory_pool_variant
#include <boost/test/included/unit_test.hpp>

#include <iostream>

#include <adk/memory_pool_variant.h>

using adk::variant::MemoryAllocator;

struct Padding1
{
    uint64_t padding;
};

struct Padding2
{
    char padding[2048];
};

BOOST_AUTO_TEST_CASE(rate_comparison_v1)
{
    constexpr uint32_t kTestLoopCount = 1000000;
    using Padding = Padding2;

    constexpr uint32_t block_size = sizeof(Padding);
    void** buffer = new void*[kTestLoopCount];    

    {
        uint64_t new_begin_tsc = adk::GetTSC();
        for (size_t i = 0; i < kTestLoopCount; i++)
        {
            buffer[i] = malloc(block_size);
        }
        uint64_t new_end_tsc = adk::GetTSC();

        const uint64_t new_cost_diff = new_end_tsc - new_begin_tsc;
        std::cout << "Raw new buffers<" << kTestLoopCount << "> " << new_cost_diff << "\t" << double(new_cost_diff) / kTestLoopCount << std::endl;

        uint64_t delete_begin_tsc = adk::GetTSC();
        for (size_t i = 0; i < kTestLoopCount; i++)
        {
            free(buffer[i]);
        }
        uint64_t delete_end_tsc = adk::GetTSC();

        const uint64_t delete_cost_diff = delete_end_tsc - delete_begin_tsc;
        std::cout << "Raw delete buffers<" << kTestLoopCount << "> " << delete_cost_diff << "\t" << double(delete_cost_diff) / kTestLoopCount << std::endl;
    }

    {
        MemoryAllocator memory_alloctor;        
        memory_alloctor.Init(1024 * 4096);
        uint64_t new_begin_tsc = adk::GetTSC();
        for (size_t i = 0; i < kTestLoopCount; i++)
        {
            buffer[i] = memory_alloctor.NewMemory(block_size);
            //BOOST_CHECK(buffer[i]);
        }
        uint64_t new_end_tsc = adk::GetTSC();

        const uint64_t new_cost_diff = new_end_tsc - new_begin_tsc;
        std::cout << "Memory allocator new buffers<" << kTestLoopCount << "> " << new_cost_diff << "\t" << double(new_cost_diff) / kTestLoopCount << std::endl;

        uint64_t delete_begin_tsc = adk::GetTSC();
        for (size_t i = 0; i < kTestLoopCount; i++)
        {
            memory_alloctor.DeleteMemory(buffer[i]);
        }
        uint64_t delete_end_tsc = adk::GetTSC();

        const uint64_t delete_cost_diff = delete_end_tsc - delete_begin_tsc;
        std::cout << "Memory allocator delete buffers<" << kTestLoopCount << "> " << delete_cost_diff << "\t" << double(delete_cost_diff) / kTestLoopCount << std::endl;
    }     
}