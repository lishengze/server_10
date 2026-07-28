#include <stdio.h>

#include <adk/shm.h>
#include <adk/shm_ptr.h>
#include <adk/constant.h>
#include <adk/scoped_lock.h>
#include <adk/arch/generic.h>

#include <boost/regex.hpp>

#include <iostream>

namespace adk_impl
{

const char kAdkShmVersionStr[SHM_VERSION_STR_LEN] = "ADK_SHM_LAYOUT_0.1";

const uint64_t ShmFactory::addtion_shm_size_ = ADK_PAGE_SIZE * 2;

map<string, ShmFactory::AddrVecType> ShmFactory::share_memory_map_;

pthread_mutex_t ShmFactory::lock_ = PTHREAD_MUTEX_INITIALIZER;

#define ADK_HUGEPAGE_SIZE (2*1024*1024)

class ShmHelper
{
public:
    static const uint32_t shm_path_prefix_len;
    static const char* shm_path_prefix;

    static const uint32_t hugetlb_path_prefix_len;
    static const char* hugetlb_path_prefix;
};

const char* ShmHelper::shm_path_prefix = "/dev/shm/";
const uint32_t ShmHelper::shm_path_prefix_len = 9;

// mount -t hugetlbfs nodev /mnt/hugepages
// cat /sys/devices/system/node/node0/meminfo
// cat /sys/devices/system/node/node1/meminfo
const char* ShmHelper::hugetlb_path_prefix = "/mnt/hugepages/";
const uint32_t ShmHelper::hugetlb_path_prefix_len = 15;

volatile bool g_use_hugetlb = false;
volatile bool g_use_mlock = false;
std::string g_huge_page_regex;

struct Initializer
{
    Initializer()
    {
        const char* use_hugetlb = std::getenv("ADK_SHM_HUGETLB");
        if (use_hugetlb != nullptr
            && (*use_hugetlb == 'Y' || *use_hugetlb == 'y'))
        {
            g_use_hugetlb = true;
        }

        char* env_str = std::getenv("ADK_MLOCK");
        if (env_str != NULL
            && (*env_str == 'Y' || *env_str == 'y'))
        {
            g_use_mlock = true;
        }

        const char* hugetlb_regex = std::getenv("ADK_SHM_HUGETLB_REGEX");
        if (hugetlb_regex != nullptr)
        {
            g_huge_page_regex = hugetlb_regex;
        }
    }
} g_hugetlb_mlock_init;

bool ShareMemoryHeader::ShmCheck(SizeType shm_len)
{
    if (len != shm_len)
    {
        return false;
    }

    if (memcmp(&(version[0]), &kAdkShmVersionStr[0], SHM_VERSION_STR_LEN) != 0)
        return false;
    
    return true;
}

// FIXME: support 1G hugetlb!
void* AllocHugePage(const std::string& shm_name, uint64_t total_size, int prot)
{
    int hugepage_fd = open((std::string(ShmHelper::hugetlb_path_prefix) + shm_name).c_str(),
                           O_RDWR|O_CREAT|prot, 0660);
    if (hugepage_fd < 0)
    {
        return nullptr;
    }

    if (prot & O_EXCL)
    {
        if (ftruncate(hugepage_fd, total_size) < 0)
        {
            close(hugepage_fd);
            return nullptr;
        }
    }

    void* mema = mmap(nullptr, total_size, PROT_READ|PROT_WRITE, MAP_SHARED, hugepage_fd, 0);

    close(hugepage_fd);
    return mema;
}

void* AllocNormal(const std::string& shm_name, uint64_t total_size, int prot, bool is_pre_alloc = false)
{
    int shm_fd = shm_open(shm_name.c_str(), O_CREAT|O_RDWR|prot, 0600);
    if (shm_fd < 0)
        return nullptr;

    if (prot & O_EXCL)
    {
        if (ftruncate(shm_fd, total_size) < 0)
        {
            close(shm_fd);
            return nullptr;
        }
    }

    if (is_pre_alloc)
    {
        // 向这个文件填充 g_max_file_size 字节的零
        int64_t to_write = (int64_t)total_size;
        constexpr uint64_t BatchSize = 1024 * 16;  // 每次写入16KB的全0内容，避免堆栈上过大的对象导致crash！
        char z[BatchSize]; // 全零数组
        memset(z, 0, BatchSize); // 全部清零

        while (to_write > 0)
        {
            int wr = write(shm_fd, z, std::min(BatchSize, (uint64_t)to_write));
            if (wr < 0)
            {
                break;
            }
            to_write -= wr;
        }
    }

    // 获取当前文件信息
    struct stat st;
    if (fstat(shm_fd, &st) < 0)
    {
        close(shm_fd);
        return nullptr;
    }

    // 如果文件大小大于实际占用的磁盘空间, 或者小于待填充的字节数, 说明有错误退出
    if (st.st_size > 512 * st.st_blocks || (uint64_t)st.st_size < total_size)
    {
        close(shm_fd);
        return nullptr;
    }

    
    void* addr = mmap(nullptr, total_size, PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    return addr;
}

void* ShmFactory::Create(const string& shm_name, uint64_t size)
{
    if (size == 0)
        return nullptr;

    ScopedLock lock_guard(lock_);
    uint64_t total_size = size + addtion_shm_size_;
    void* addr;
    
    bool is_regex_match = false;
    if (!g_huge_page_regex.empty())
    {
        boost::smatch search_result;
        boost::regex  expression(g_huge_page_regex);
        is_regex_match = boost::regex_search(shm_name, search_result, expression);
    }

    if (!g_use_hugetlb && !is_regex_match)
    {
        addr = AllocNormal(shm_name, total_size, O_EXCL, true);
    }
    else
    {
        total_size = (total_size + (ADK_HUGEPAGE_SIZE - 1)) / (ADK_HUGEPAGE_SIZE) * (ADK_HUGEPAGE_SIZE);
        addr = AllocHugePage(shm_name, total_size, O_EXCL);
    }

    if (addr == MAP_FAILED || addr == nullptr)
        return nullptr;

    if (g_use_mlock)
    {
        mlock(addr, total_size);
    }
    
    ShareMemoryHeader* mem_header = (ShareMemoryHeader*)addr;
    memcpy(&(mem_header->version[0]), &kAdkShmVersionStr[0], SHM_VERSION_STR_LEN);
    mem_header->len = total_size;

    AddrVecType& addr_vec = share_memory_map_[shm_name];
    addr_vec.push_back(addr);

    assert(ADK_IS_PAGE_ALIGN(addr));

    addr = ptr_add(addr, sizeof(ShareMemoryHeader));
    return ADK_PAGE_ALIGN(addr);
}

void* ShmFactory::Attach(const string& shm_name)
{
    ScopedLock lock_guard(lock_);
    void* addr;
    char shm_path[ADK_MAX_NAME_LEN];

    bool is_regex_match = false;
    if (!g_huge_page_regex.empty())
    {
        boost::smatch search_result;
        boost::regex  expression(g_huge_page_regex);
        is_regex_match = boost::regex_search(shm_name, search_result, expression);
    }

    if (!g_use_hugetlb && !is_regex_match)
    {
        snprintf(shm_path, std::min(ADK_MAX_NAME_LEN, shm_name.size() + ShmHelper::shm_path_prefix_len + 1),    // +1 for \0
                 "%s%s", ShmHelper::shm_path_prefix, shm_name.c_str());
    }
    else
    {
        snprintf(shm_path, std::min(ADK_MAX_NAME_LEN, shm_name.size() + ShmHelper::hugetlb_path_prefix_len + 1),    // +1 for \0
                 "%s%s", ShmHelper::hugetlb_path_prefix, shm_name.c_str());
    }

    struct stat shm_stat;
    int ret = stat(&shm_path[0], &shm_stat);
    if (ret < 0 || (uint64_t)shm_stat.st_size < addtion_shm_size_)
        return nullptr;

    if (!g_use_hugetlb && !is_regex_match)
    {
        addr = AllocNormal(shm_name, shm_stat.st_size, 0);
    }
    else
    {
        addr = AllocHugePage(shm_name, shm_stat.st_size, 0);
    }

    if (addr == MAP_FAILED || addr == nullptr)
        return nullptr;

    ShareMemoryHeader* mem_header = (ShareMemoryHeader*)addr;
    if (!(mem_header->ShmCheck(shm_stat.st_size)))
        return nullptr;

    if (g_use_mlock)
    {
        mlock(addr, shm_stat.st_size);
    }

    AddrVecType& addr_vec = share_memory_map_[shm_name];
    addr_vec.push_back(addr);

    assert(ADK_IS_PAGE_ALIGN(addr));

    addr = ptr_add(addr, sizeof(ShareMemoryHeader));
    return ADK_PAGE_ALIGN(addr);
}

int32_t ShmFactory::Detach(const string& shm_name)
{
    ScopedLock lock_guard(lock_);
    AddrVecType& addr_vec = share_memory_map_[shm_name];
    for (auto it = addr_vec.begin(); it != addr_vec.end(); ++it)
    {
        ShareMemoryHeader* mem_header = (ShareMemoryHeader*)*it;
        munmap(mem_header, mem_header->len);
    }

    share_memory_map_.erase(shm_name);

    return 0;
}

int32_t ShmFactory::Destroy(const string& shm_name)
{
    ScopedLock lock_guard(lock_);

    AddrVecType& addr_vec = share_memory_map_[shm_name];
    for (auto it = addr_vec.begin(); it != addr_vec.end(); ++it)
    {
        ShareMemoryHeader* mem_header = (ShareMemoryHeader*)*it;
        munmap(mem_header, mem_header->len);
    }

    share_memory_map_.erase(shm_name);

    bool is_regex_match = false;
    if (!g_huge_page_regex.empty())
    {
        boost::smatch search_result;
        boost::regex  expression(g_huge_page_regex);
        is_regex_match = boost::regex_search(shm_name, search_result, expression);
    }

    int ret;
    if (g_use_hugetlb || is_regex_match)
    {
        ret = unlink((std::string(ShmHelper::hugetlb_path_prefix) + shm_name).c_str());
    }
    else
    {
        ret = shm_unlink(shm_name.c_str());
    }
    
    return ret;
}

uint64_t ShmFactory::Size(const string &shm_name)
{
    ScopedLock lock_guard(lock_);
    char shm_path[ADK_MAX_NAME_LEN];

    bool is_regex_match = false;
    if (!g_huge_page_regex.empty())
    {
        boost::smatch search_result;
        boost::regex  expression(g_huge_page_regex);
        is_regex_match = boost::regex_search(shm_name, search_result, expression);
    }

    if (!g_use_hugetlb && !is_regex_match)
    {
        snprintf(shm_path, std::min(ADK_MAX_NAME_LEN, shm_name.size() + ShmHelper::shm_path_prefix_len + 1), // +1 for \0
                 "%s%s", ShmHelper::shm_path_prefix, shm_name.c_str());
    }
    else
    {
        snprintf(shm_path, std::min(ADK_MAX_NAME_LEN, shm_name.size() + ShmHelper::hugetlb_path_prefix_len + 1), // +1 for \0
                 "%s%s", ShmHelper::hugetlb_path_prefix, shm_name.c_str());
    }

    struct stat shm_stat;
    int ret = stat(&shm_path[0], &shm_stat);
    if (ret < 0 || (uint64_t)shm_stat.st_size < addtion_shm_size_)
        return -1;

    return (uint64_t)shm_stat.st_size - addtion_shm_size_;
}

} // adk
