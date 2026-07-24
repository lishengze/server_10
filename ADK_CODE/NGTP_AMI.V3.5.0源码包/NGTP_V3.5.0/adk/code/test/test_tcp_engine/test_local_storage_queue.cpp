
#include <time.h>
#include <stdlib.h>
#include <iostream>
#include <local_storage_queue.h>

constexpr uint32_t MAX_QUEUE   = 2048;
constexpr uint32_t MAX_OPERATE = (MAX_QUEUE * 2);
constexpr uint64_t MAX_LOOPS = 1000000000;
int main(int argc, char* argv[])
{
    time_t pre = time(nullptr);
    std::srand(pre);

    uint64_t cnt = MAX_LOOPS;
    adk_impl::LocalStorageQueue<uint64_t, MAX_QUEUE> queue;

    uint64_t wnext = 1, rnext = 1;
    uint32_t len = 0;
    while(--cnt)
    {
        auto cur = time(nullptr);
        if (cur != pre)
        {
            std::cout << "queue len = " << queue.length() << std::endl;
            assert(queue.length() == len);
            pre = cur;
        }

        if (queue.length() != len)
        {
            std::cout << "test failure len error: " << len << " != " << queue.length() << std::endl;
            return 0; 
        }

        auto w = (std::rand() % MAX_OPERATE);
        auto gap = (MAX_QUEUE >> 3);
        
        w = (w < gap ? 0 : (w-gap));
        while (w--)
        {
            if (queue.TryPush(wnext) == 0)
            {
                ++wnext;
                ++len;
            }
            else
            {
                assert(len == MAX_QUEUE);
                if (queue.length() != len)
                {
                    std::cout << "test failure len error: " << len << " != " << MAX_QUEUE << std::endl;
                    return 0; 
                }
            }
        }

        auto r = (std::rand() % MAX_OPERATE);
        gap = (MAX_QUEUE >> 1);
        r = (r < gap ? 0 : (r-gap));
        while (r--)
        {
            uint64_t t = 0;
            if (queue.TryPop(t) == 0)
            {
                --len;
                //assert(t == rnext);
                if (t != rnext)
                {
                    std::cout << "test failure data error: " << t << " != " << rnext << std::endl;
                    return 0; 
                }
                ++rnext;
            }
            else
            {
                assert(len==0);
                if (queue.length() != len)
                {
                    std::cout << "test failure len error: " << len << " != " << 0 << std::endl;
                    return 0; 
                }
            }
        }
    }
    std::cout << "test success" << std::endl;
    return 0;
}