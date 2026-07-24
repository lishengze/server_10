#include <stdint.h>
#include <time.h>

#include <list>
#include <deque>
#include <iostream>

struct Element
{
    Element(uint64_t a_)
        :   a(a_)
    {}

    uint64_t a;
    char padding[128 - sizeof(uint64_t)];
};

std::deque<Element>* MakeList()
{
    std::deque<Element>* ret = new std::deque<Element>();
    std::deque<Element>* ret1 = new std::deque<Element>();
    std::deque<Element>* ret2 = new std::deque<Element>();
    for (uint64_t i = 0; i != 2000*10000; ++i)
    {
        ret->push_back(Element(i));
        ret1->push_back(Element(i));
        ret2->push_back(Element(i));
    }
    delete ret1;
    delete ret2;
    return ret;
}

uint64_t Func()
{
    auto* test_list = MakeList();
    struct timespec ts_begin, ts_end;
    clock_gettime(CLOCK_REALTIME, &ts_begin);
    auto it = test_list->begin();
    auto it_end = test_list->end();
    uint64_t sum = 0;
    for (; it!=it_end; ++it)
    {
        sum += it->a;
    }
    clock_gettime(CLOCK_REALTIME, &ts_end);


    std::cout << "total size " << test_list->size() << std::endl;

    std::cout << "total time use : " 
              << (ts_end.tv_sec - ts_begin.tv_sec) * 1000000000 + ts_end.tv_nsec - ts_begin.tv_nsec
              << std::endl;

    return sum;
}

int main()
{
    std::cout << Func() << std::endl;
    return 0;
}
