
#include <adk/mem_pool.h>

using namespace adk;

int main(int argc, char const *argv[])
{
    remove("/dev/shm/test_mpm");
    remove("/dev/shm/test_mem_pool1");
    remove("/dev/shm/test_mem_pool2");
    remove("/dev/shm/test_mem_pool3");
    remove("/dev/shm/test_mem_pool4");

    MPManager mpm;    
    mpm.CreateMPTable("test_mpm");
    MemoryPool* mp = mpm.CreateSharedPool("test_mem_pool1", 512, 1024);
    mp = mpm.CreateSharedPool("test_mem_pool2", 512, 1024);
    mp = mpm.CreateSharedPool("test_mem_pool3", 512, 1024);
    mp = mpm.CreateSharedPool("test_mem_pool4", 512, 1024);

    sleep(10);

    mpm.DestroyAll();
    return 0;
}