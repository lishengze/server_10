#include <set>
#include <bitset>
#include <iostream>

#include <adk/index_allocator.h>

template<uint32_t kMaxIndexSize>
void TestBody()
{
    std::bitset<kMaxIndexSize> simulator;
    adk::IndexAllocator<kMaxIndexSize> index_allocator;

    for (int32_t loop = 0; loop < 3; ++loop)
    {
        for (int32_t index = 0; index < kMaxIndexSize; ++index)
        {
            if (index != index_allocator.Allocate())
            {
                std::cout << "ERROR: " << kMaxIndexSize << " " << __LINE__ << std::endl;
                return;
            }
        }

        for (int32_t index = 0; index < 2 * kMaxIndexSize; ++index)
        {
            if (-1 != index_allocator.Allocate())
            {
                std::cout << "ERROR: " << kMaxIndexSize << " " << __LINE__ << std::endl;
                return;
            }
        }

        for (int32_t index = 0; index < kMaxIndexSize; ++index)
        {
            index_allocator.Free(index);
        }
    }

    for (uint64_t loop = 0; loop < 10000000; ++loop)
    {
        const auto factor = random();
        if (factor & 1)
        {
            const auto index = index_allocator.Allocate();
            if (-1 != index)
            {
                if (simulator.test(index))
                {
                    std::cout << "ERROR: " << kMaxIndexSize << " " << __LINE__ << std::endl;
                    return;
                }

                simulator.set(index);
            }
            else
            {
                if (!simulator.all())
                {
                    std::cout << "ERROR: " << kMaxIndexSize << " " << __LINE__ << std::endl;
                    return;
                }

                const int32_t free_index = factor % kMaxIndexSize;
                assert(simulator.test(free_index));

                simulator.set(free_index, false);
                index_allocator.Free(free_index);
            }
        }

        const int32_t free_index = factor % kMaxIndexSize;
        if (simulator.test(free_index))
        {
            simulator.set(free_index, false);
            index_allocator.Free(free_index);
        }
    }

    std::cout << "Test " << kMaxIndexSize << " complete" << std::endl;
}

int main()
{
    for (uint32_t index = 0; index < 10000; ++index)
    {
        TestBody<1>();
        TestBody<2>();
        TestBody<3>();
        TestBody<4>();
        TestBody<5>();
        TestBody<6>();
        TestBody<7>();
        TestBody<8>();
        TestBody<9>();
        TestBody<10>();
        TestBody<11>();
        TestBody<12>();
        TestBody<14>();
        TestBody<16>();
        TestBody<19>();
        TestBody<26>();
        TestBody<39>();
        TestBody<51>();
        TestBody<63>();
        TestBody<64>();
        TestBody<65>();
        TestBody<64 * 3 - 1>();
        TestBody<64 * 3>();
        TestBody<64 * 3 + 1>();
        TestBody<64 * 11 - 1>();
        TestBody<64 * 11>();
        TestBody<64 * 11 + 1>();
        TestBody<64 * 31 - 1>();
        TestBody<64 * 31>();
        TestBody<64 * 31 + 1>();
        TestBody<64 * 64 - 1>();
        TestBody<64 * 64>();
        TestBody<64 * 64 + 1>();
        TestBody<64 * 64 * 64 - 1>();
        TestBody<64 * 64 * 64>();
        TestBody<64 * 64 * 64 + 1>();
    }

    return 0;
}