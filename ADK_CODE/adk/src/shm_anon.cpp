#include <sys/mman.h>

#include <adk/shm_anon.h>

namespace adk_impl
{

void* AnonShmFactory::CreateShm(const std::string& name, size_t length)
{
    std::lock_guard<std::mutex> lock(GetMutex());
    std::unordered_map<std::string, void*>& shm_map = GetShmMap();
    if (shm_map.find(name) != shm_map.end())
    {
        // the same name, mmap only call once, note the length
        return shm_map[name];
    }

    void* ptr = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)
    {
        return nullptr;
    }

    shm_map[name] = ptr;
    return ptr;
}

void* AnonShmFactory::GetShm(const std::string& name)
{
    std::unordered_map<std::string, void*>& shm_map = GetShmMap();
    if (shm_map.find(name) == shm_map.end())
    {
        return nullptr;
    }

    return shm_map[name];
}

std::unordered_map<std::string, void*>& AnonShmFactory::GetShmMap()
{
    static std::unordered_map<std::string, void*> shm_map;
    return shm_map;
}

std::mutex& AnonShmFactory::GetMutex()
{
    static std::mutex mtx;
    return mtx;
}

}