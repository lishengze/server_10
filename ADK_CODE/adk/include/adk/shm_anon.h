#include <mutex>
#include <string>
#include <unordered_map>

namespace adk_impl
{

class AnonShmFactory
{
public:
    static void* CreateShm(const std::string& name, size_t length);

    static void* GetShm(const std::string& name);

    static std::unordered_map<std::string, void*>& GetShmMap();

    static std::mutex& GetMutex();
};

}
