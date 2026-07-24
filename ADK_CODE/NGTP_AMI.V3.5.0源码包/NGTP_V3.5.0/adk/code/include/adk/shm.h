#ifndef ADK_IMPL_SHM_H_
#define ADK_IMPL_SHM_H_

#include "error_code.h"
#include "arch/generic.h"

#include <map>
#include <string>
#include <vector>

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace adk_impl
{

#define SHM_VERSION_STR_LEN         256

using std::string;
using std::map;
using std::vector;

struct ShareMemoryHeader
{
    char        version[SHM_VERSION_STR_LEN];
    SizeType    len;

    bool ShmCheck(SizeType shm_len);
};

class ShmFactory
{
public:
    static void* Create(const string& shm_name, SizeType size);
    static void* Attach(const string& shm_name);
    static int32_t Detach(const string& shm_name);
    static int32_t Destroy(const string& shm_name);

    /**
     * @brief get application's share memory size 
     * 
     * @param shm_name share memory file full name USER_name
     * @return uint64_t app size
     *          -1 : no such file
     */
    static uint64_t Size(const string &shm_name);

private:
    typedef vector<void*> AddrVecType;
    static map<string, AddrVecType>     share_memory_map_;
    static const uint64_t               addtion_shm_size_;
    static pthread_mutex_t              lock_;

    ShmFactory()
    {}

    ~ShmFactory()
    {}
};

} // adk


#endif // ADK_SHM_H_        
