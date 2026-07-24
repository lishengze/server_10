
#include <adk/mem_pool.h>

using namespace adk;

int main(int argc, char const *argv[])
{
    remove("/dev/shm/test_mpm");
    remove("/dev/shm/test_mem_pool1");
    // clear test
    MPManager mpm_tmp;
    if (mpm_tmp.AttachMPTable("test_mpm") == ErrorCode::kSuccess)
    {
        mpm_tmp.DestroyAll();
    }

    MPManager mpm;    
    mpm.CreateMPTable("test_mpm");
    MemoryPool* mp = mpm.CreateSharedPool("test_mem_pool1", 512, 1024);
    mpm.DetachAll();
    //****************
    
    MPManager mpm1;
    mpm1.AttachMPTable("test_mpm");
    MemoryPool* mp1 = mpm1.AttachSharedPool("test_mem_pool1");
    
    MPManager mpm2;
    mpm2.AttachMPTable("test_mpm");
    MemoryPool* mp2 = mpm2.AttachSharedPool("test_mem_pool1");

    std::cout << "before detach" << std::endl;
    sleep(20);
    //****************

    mpm1.DetachAll();
    mpm2.DetachAll();    
    std::cout << "after detach" << std::endl;
    sleep(20);
    //****************
    return 0;
}