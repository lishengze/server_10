#include <adk/memory_pool_variant.h>

#include <thread>
#include <boost/program_options.hpp>

volatile bool g_start_run = false;
constexpr uint32_t kInnerLoopSize = 16;
uint32_t kNewSizeArray[kInnerLoopSize] = 
{ 
    16, 32, 64, 128,
    256, 512, 1024, 2048,
    16, 32, 64, 128,
    256, 512, 1024, 2048 
};

void Worker0(uint32_t loop_counter, uint64_t& time_diff)
{
    while (!g_start_run);
    char* cache_buffer[kInnerLoopSize];

    struct timespec start_time;
    clock_gettime(CLOCK_REALTIME, &start_time);
    for (uint32_t index = 0; index < loop_counter; ++index)
    {
        for (uint32_t inner_index = 0; inner_index < kInnerLoopSize; ++inner_index)
        {
            cache_buffer[inner_index] = new char[kNewSizeArray[inner_index]];
        }

        for (uint32_t inner_index = 0; inner_index < kInnerLoopSize; ++inner_index)
        {
            delete[] cache_buffer[inner_index];
        }
    }

    struct timespec end_time;
    clock_gettime(CLOCK_REALTIME, &end_time);

    time_diff = end_time.tv_sec * 1000000000 + end_time.tv_nsec
              - start_time.tv_sec * 1000000000 - start_time.tv_nsec;
}

void Worker1(adk::variant::MemoryAllocator* const memory_allocator, uint32_t loop_counter, uint64_t& time_diff)
{
    while (!g_start_run);
    void* cache_buffer[kInnerLoopSize];

    struct timespec start_time;
    clock_gettime(CLOCK_REALTIME, &start_time);
    for (uint32_t index = 0; index < loop_counter; ++index)
    {
        for (uint32_t inner_index = 0; inner_index < kInnerLoopSize; ++inner_index)
        {
            cache_buffer[inner_index] = memory_allocator->NewMemory<true>(kNewSizeArray[inner_index]);
        }

        for (uint32_t inner_index = 0; inner_index < kInnerLoopSize; ++inner_index)
        {
            memory_allocator->DeleteMemory(cache_buffer[inner_index]);
        }
    }

    struct timespec end_time;
    clock_gettime(CLOCK_REALTIME, &end_time);

    time_diff = end_time.tv_sec * 1000000000 + end_time.tv_nsec
        - start_time.tv_sec * 1000000000 - start_time.tv_nsec;
}

void Worker2(uint32_t loop_counter, uint64_t& time_diff)
{
    while (!g_start_run);
    void* cache_buffer[kInnerLoopSize];

    adk::variant::MemoryAllocator memory_allocator;
    memory_allocator.Init();

    struct timespec start_time;
    clock_gettime(CLOCK_REALTIME, &start_time);
    for (uint32_t index = 0; index < loop_counter; ++index)
    {
        for (uint32_t inner_index = 0; inner_index < kInnerLoopSize; ++inner_index)
        {
            cache_buffer[inner_index] = memory_allocator.NewMemory<false>(kNewSizeArray[inner_index]);
        }

        for (uint32_t inner_index = 0; inner_index < kInnerLoopSize; ++inner_index)
        {
            memory_allocator.DeleteMemory(cache_buffer[inner_index]);
        }
    }

    struct timespec end_time;
    clock_gettime(CLOCK_REALTIME, &end_time);

    time_diff = end_time.tv_sec * 1000000000 + end_time.tv_nsec
        - start_time.tv_sec * 1000000000 - start_time.tv_nsec;
}

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("test-mode", 
         boost::program_options::value<int32_t>()->default_value(0), 
         "set test mode 0:c++ new/delete, 1: MemoryAllocator multi thread, 2: MemoryAllocator")
        ("loop-counter", boost::program_options::value<int32_t>()->default_value(1000000), "set test loop counter")
        ("thread-num", boost::program_options::value<int32_t>()->default_value(4), "set test thread number")
        ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    const auto thread_num = vm["thread-num"].as<int32_t>();
    const auto loop_counter = vm["loop-counter"].as<int32_t>();
    std::vector<uint64_t> time_diff_vec(thread_num);
    std::vector<std::thread> worker_threads;

    adk::variant::MemoryAllocator memory_allocator;
    switch (vm["test-mode"].as<int32_t>())
    {
    case 0:
        for (int32_t index = 0; index < thread_num; ++index)
        {
            worker_threads.push_back(std::thread(Worker0, loop_counter, std::ref(time_diff_vec[index])));
        }
        break;
    case 1:
        memory_allocator.Init();
        for (int32_t index = 0; index < thread_num; ++index)
        {
            worker_threads.push_back(std::thread(Worker1, &memory_allocator, loop_counter, std::ref(time_diff_vec[index])));
        }
        break;
    case 2:
        for (int32_t index = 0; index < thread_num; ++index)
        {
            worker_threads.push_back(std::thread(Worker2, loop_counter, std::ref(time_diff_vec[index])));
        }
        break;
    default:
        ;
    }

    g_start_run = true;
    for (auto& thread : worker_threads)
    {
        thread.join();
    }

    for (const auto time_diff : time_diff_vec)
    {
        std::cout << "time const: " << time_diff << std::endl;
    }

    return 0;
}
