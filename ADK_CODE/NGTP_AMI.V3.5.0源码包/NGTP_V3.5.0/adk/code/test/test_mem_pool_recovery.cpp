#include <adk/mem_pool.h>
#include <adk/lock_free_msg_queue.h>
#include <string.h>
#include <boost/thread.hpp>

using namespace adk;


static void ReleaseMain(MemoryPool* mp)
{
    sleep(2);
    mp->ReleaseAllocThread();   
}

int main(int argc, char const *argv[])
{
    {
        MPManager mpm;
        if (mpm.AttachMPTable("test_mpm") == ErrorCode::kSuccess)
        {
            mpm.DestroyAll();
        }
    }

    MPManager mpm;
    mpm.CreateMPTable("test_mpm");
    MemoryPool* mp = mpm.CreateSharedPool("test_mp", 512, 1024, 1024);

    MemoryBuffer* buf1 = mp->NewBuffer(0);
    assert(buf1 != nullptr);
    // simulate failure
    MemoryBuffer* buf2 = mp->NewBuffer(1);
    assert(buf2 == nullptr);


    // recovery
    MPManager mpm2;
    mpm2.AttachMPTable("test_mpm");
    MemoryPool* mp2 = mpm2.AttachSharedPool("test_mp");    

    mp2->Consistent();
    std::vector<MemoryBuffer*> buf_vec;
    buf_vec.reserve(1024);
    for (uint32_t i = 1; i < 1024; ++i)
    {
        MemoryBuffer* buf = mp2->NewBuffer(0);
        assert(buf != nullptr);
        buf_vec.push_back(buf);
        strcpy(buf->data, "hello world");
    }

    MemoryBuffer* buf = mp2->NewBuffer(0);
    assert(buf == nullptr);

    for (auto* buf : buf_vec)
    {
        mpm2.DeleteBuffer(buf);
    }

    buf = mp2->NewBuffer(0);
    assert(buf != nullptr);

    MemoryPool* mp3 = mpm2.CreateSharedPool("test_mp", 512, 1024, 1024);
    assert(mp3 == nullptr);

    mp3 = mpm2.AttachSharedPool("test_mp3");  
    assert(mp3 == nullptr);

    mp3 = mpm2.CreateSharedPool("test_mp3", 512, 1024, 1024);
    assert(mp3 != nullptr);

    buf = mp3->NewBuffer(0);
    assert(buf != nullptr);

    srandom(time(NULL));
    auto v = random() % random() + 1;
    char test_buf[128];
    strcpy(test_buf, "xxx ddd");
    memcpy(test_buf + strlen("xxx ddd"), &v, sizeof(v));
    size_t total_size = strlen("xxx ddd") + sizeof(v);
    memcpy(buf->data, test_buf, total_size);
    assert(memcmp(buf->data, test_buf, total_size) == 0);

    // simulate failure;
    mpm2.DeleteBuffer(buf, 1);

    buf = mp3->NewBuffer(1);
    assert(buf == nullptr);

    // recovery
    MPManager mpm3;
    mpm3.AttachMPTable("test_mpm");
    MemoryPool* mp4 = mpm3.AttachSharedPool("test_mp3");    
    mp4->Consistent();

    buf_vec.clear();
    for (uint32_t i = 1; i < 1024; ++i)
    {
        MemoryBuffer* buf = mp4->NewBuffer(0);
        assert(buf != nullptr);
        buf_vec.push_back(buf);
        assert(memcmp(buf->data, test_buf, total_size) != 0);
        strcpy(buf->data, "hello world");
    }

    buf = mp4->NewBuffer(0);
    assert(buf == nullptr);


    auto* mp5 = mpm2.CreateSharedPool("test_mp5", 512, 1024, 1024);
    assert(mp5 != nullptr);

    for (uint32_t i = 1; i <= 1024; ++i)
    {
        MemoryBuffer* buf = mp5->NewBuffer(0);
        assert(buf != nullptr);
        buf_vec.push_back(buf);
        memcpy(buf->data, test_buf, total_size);
    }

    for (auto* buf : buf_vec)
    {
        mpm2.DeleteBuffer(buf);
    }

    MPManager mpm4;
    mpm4.AttachMPTable("test_mpm");
    MemoryPool* mp6 = mpm4.AttachSharedPool("test_mp5");  

    for (uint32_t i = 1; i <= 1024; ++i)
    {
        MemoryBuffer* buf = mp6->NewBuffer(0);
        assert(buf != nullptr);
        assert(memcmp(buf->data, test_buf, total_size) == 0);
    }

    // test emergent buffer
    auto* mp7 = mpm4.CreateSharedPool("test_mp7", 512, 1024, 1024);
    assert(mp7 != nullptr);

    buf = mp7->NewEmergentBuffer(0);
    assert(buf != nullptr);
    memcpy(buf->data, test_buf, total_size);

    buf = mp7->NewEmergentBuffer(1);
    assert(buf == nullptr);

    // recovery
    MPManager mpm5;
    mpm5.AttachMPTable("test_mpm");
    MemoryPool* mp8 = mpm5.AttachSharedPool("test_mp7");  
    mp8->Consistent();

    buf_vec.clear();
    for (uint32_t i = 1; i < 2048; ++i)
    {
        MemoryBuffer* buf = mp8->NewEmergentBuffer(0);
        assert(buf != nullptr);
        assert(memcmp(buf->data, test_buf, total_size) != 0);
        buf_vec.push_back(buf);
        memcpy(buf->data, test_buf, total_size);
    }

    // NewEmergentBuffer shall block the application
    boost::thread release_thread = boost::thread(ReleaseMain, mp8);
    buf = mp8->NewEmergentBuffer(0);
    assert(buf == nullptr);

    for (auto* buf : buf_vec)
    {
        auto ret = mpm5.DeleteBuffer(buf);
        assert(ret == ErrorCode::kSuccess);
    }

    buf = mp8->NewEmergentBuffer(0);
    assert(buf != nullptr);

    MPManager mpm6;
    mpm6.AttachMPTable("test_mpm");
    MemoryPool* mp9 = mpm6.AttachSharedPool("test_mp7");  

    buf = mp9->NewEmergentBuffer(0);
    assert(buf != nullptr);
    assert(memcmp(buf->data, test_buf, total_size) == 0);

    return 0;
}
