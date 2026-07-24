#include <string>
#include <thread>
#include <iostream>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <adk/shm_ptr.h>
#include <adk/arch/generic.h>

uint64_t g_shm_value = 0;
uint64_t g_shm_counter = 0;

void Observe()
{
    uint64_t shm_value = 0;
    uint64_t shm_counter = 0;
    while (true)
    {
        sleep(1);
        const auto temp_shm_value = ACCESS_ONCE(g_shm_value);
        const auto temp_shm_counter = ACCESS_ONCE(g_shm_counter);

        std::cout << "shm value diff = " << temp_shm_value - shm_value
            << ", shm counter diff = " << temp_shm_counter - shm_counter
            << std::endl;

        shm_value = temp_shm_value;
        shm_counter = temp_shm_counter;
    }
}

int main(int argc, char* argv[])
{
    std::string shm_name = "swap123";
    if (argc > 1)
    {
        shm_name = argv[1];
    }

    const std::string shm_full_name = "/dev/shm/" + shm_name;

    struct stat shm_stat;
    if (stat(shm_full_name.c_str(), &shm_stat) < 0)
    {
        std::cout << shm_full_name << " file is not exist!" << std::endl;
        return 0;
    }

    const auto shm_fd = shm_open(shm_name.c_str(), O_RDWR, 0600);
    if (shm_fd < 0)
    {
        std::cout << "shm_open failed " << strerror(errno) << std::endl;
        return 0;
    }

    void* addr = mmap(NULL, shm_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (MAP_FAILED == addr)
    {
        std::cout << "mmap failed " << strerror(errno) << std::endl;
        return 0;
    }

    const uint32_t total_size = 1024 * 1024 * 1024;
    const uint32_t size_mask = total_size - 1;

    std::thread observe_thd = std::thread(Observe);

    uint64_t position = 0;
    while (true)
    {
        char* data = (char*)addr + (position & size_mask);
        const auto shm_value = *((uint64_t*)data);
        if (shm_value > g_shm_value)
        {
            g_shm_value = shm_value;
            ++g_shm_counter;
            continue;
        }

        position += 8;
    }

    observe_thd.join();
    return 0;
}