
#include <adk/mem_pool.h>

using namespace adk;

int main(int argc, char const *argv[])
{
    remove("/dev/shm/test_mpm");
    remove("/dev/shm/test_mem_pool");
    
    MPManager mpm;    
    mpm.CreateMPTable("test_mpm");
    MemoryPool* mp = mpm.CreateSharedPool("test_mem_pool", 512, 1024);

    MPManager mpm_rec;
    mpm_rec.AttachMPTable("test_mpm");
    MemoryPool* mp_rec = mpm_rec.AttachSharedPool("test_mem_pool");

    MemoryBuffer* membuf = mp->NewBuffer();
    *(uint64_t*)(membuf->data) = 1234;
    ShmPointer shm_ptr = membuf->shm_ptr;

    MemoryPool* mp_rec_2 = mpm_rec.IndexToMempool(shm_ptr.mp_index());
    assert(mp_rec == mp_rec_2);

    MemoryBuffer* membuf_rec = mpm_rec.ShmPtrToMemBuf(&shm_ptr);
    assert(membuf_rec->shm_ptr.value == shm_ptr.value);
    assert(membuf_rec != membuf);
    assert(*(uint64_t*)(membuf_rec->data) == 1234);

    return 0;
}