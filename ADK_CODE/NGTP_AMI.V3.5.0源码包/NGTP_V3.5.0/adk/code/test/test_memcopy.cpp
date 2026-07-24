#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>

void Func(char* src, char* dst, int len)
{
    memcpy(src, dst, len);
}

void Test(int argc)
{
    char* src = new char [2000000000];
    char* dst = new char [2000000000];
    
    memset(src, 0x00, 2000000000);
    memset(dst, 0x00, 2000000000);
    srandom(time(nullptr));

    uint64_t* offset_1_arr = new uint64_t [2000000];
    uint64_t* offset_2_arr = new uint64_t [2000000];

    for (uint64_t i = 0; i != 2000000; ++i)
    {
        uint64_t offset_1 = random() % 2000000000;
        uint64_t offset_2 = random() % 2000000000;

        if (offset_1 >= 2000000000 - 4096)
            offset_1 = 2000000000 - 4096;

        offset_1_arr[i] = offset_1;

        if (offset_2 >= 2000000000 - 4096)
            offset_2 = 2000000000 - 4096;

        offset_2_arr[i] = offset_2;
    }

    struct timespec ts_begin, ts_end;
    clock_gettime(CLOCK_REALTIME, &ts_begin);
    for (uint64_t i = 0; i != 2000000; ++i)
    {
        memcpy(src + offset_1_arr[i], dst + offset_2_arr[i], argc);
    }
    clock_gettime(CLOCK_REALTIME, &ts_end);

    auto total = (ts_end.tv_sec - ts_begin.tv_sec) * 1000000000 + ts_end.tv_nsec - ts_begin.tv_nsec;
    std::cout << "total time use : " 
              << (ts_end.tv_sec - ts_begin.tv_sec) * 1000000000 + ts_end.tv_nsec - ts_begin.tv_nsec
              << std::endl;

    std::cout << "avg : " << total / 2000000 << std::endl;
}

int main(int argc, char* argv [])
{
    Test(atoi(argv[1]));
    return 0;
}


