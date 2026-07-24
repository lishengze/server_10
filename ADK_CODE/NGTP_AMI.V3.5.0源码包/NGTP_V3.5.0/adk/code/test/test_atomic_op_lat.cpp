#include <time.h>
#include <stdio.h>
#include <iostream>

#include <adk/lock_free_queue_variant.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>
#include <adk/event.h>

#include <boost/thread/thread.hpp>

// using namespace adk::variant;
using namespace adk;

int main(int argc, char const *argv[])
{
    uint64_t begin = adk::GetTSC();
    for (uint32_t i = 0; i < 10000000; ++i)
        ADK_BARRIER();
    uint64_t end = adk::GetTSC();
    uint64_t loop_time = end - begin;

    uint32_t var = 0;
    begin = adk::GetTSC();
    for (uint32_t i = 0; i < 10000000; ++i)    
        __sync_fetch_and_add(&(var), 1);
    end = adk::GetTSC();
    std::cout << "__sync_fetch_and_add : " << end - begin - loop_time << std::endl;

    var = 0;
    begin = adk::GetTSC();
    for (uint32_t i = 0; i < 10000000; ++i)    
        __sync_bool_compare_and_swap(&(var), i, i + 1);
    end = adk::GetTSC();
    std::cout << "__sync_bool_compare_and_swap : " << end - begin - loop_time << std::endl;
    return 0;
}
