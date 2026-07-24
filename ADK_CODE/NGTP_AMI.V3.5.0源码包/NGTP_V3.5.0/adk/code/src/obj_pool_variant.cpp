#include <adk/obj_pool_variant.h>
#include <adk/lock_free_msg_queue.h>

#include <boost/thread.hpp>
#include <deque>


namespace adk_impl
{

namespace variant
{

int32_t DoGetTimeId()
{
    static uint32_t id = 0;
    static boost::mutex id_lock;

    boost::mutex::scoped_lock lock_guard(id_lock);
    auto ret = id;
    ++id;
    return ret;
}

struct ObjPoolBaseImpl
{
    SPMCQueue* obj_queue;
    MPSCQueue* free_obj_queue;
    std::deque<void*>* pending_obj_deq;
    void* obj_addr;
    char* buf;
    uint32_t i;
    uint32_t size;
    int32_t id;
};

struct ObjChunk
{
    uint32_t obj_size;
    int16_t  flags;
    int16_t  obj_id;
};

#define ADK_OBJ_POOL_EXTRA_OBJ  0x0001
#define ADK_OBJ_POOL_DELETED    0x0002

static boost::thread* g_obj_gen = nullptr;
static volatile bool g_obj_gen_is_running = false;
MPSCQueue* g_cmd_queue = nullptr;
ObjPoolBaseImpl* g_obj_pool_array[4096];
static boost::mutex g_delete_lock;

int32_t ObjPoolBase::Init(ObjConstructorType f, std::size_t size, int32_t id)
{
    if (id >= 4096 || id < 0)
    {
        return ErrorCode::kFailure;
    }

    static boost::mutex init_lock;
    boost::mutex::scoped_lock lock_guard(init_lock);

    if (g_obj_pool_array[id] != nullptr)
    {
        obj_pool_private_ = g_obj_pool_array[id];
        return ErrorCode::kSuccess;
    }

    size_ = size;
    f_ = f;

    auto* impl = (ObjPoolBaseImpl*)memalign(ADK_CACHE_LINE_SIZE, sizeof(ObjPoolBaseImpl));
    obj_pool_private_ = impl;

    impl->obj_queue = SPMCQueue::Create(std::string("obj_queue_") + std::to_string(id),
                                        sizeof(void*),
                                        8192);

    impl->free_obj_queue = MPSCQueue::Create(std::string("free_obj_queue_") + std::to_string(id),
                                             sizeof(void*),
                                             8192);
    impl->pending_obj_deq = new std::deque<void*>();
    impl->i = 0;
    impl->buf = nullptr;
    impl->obj_addr = nullptr;
    impl->size = size;
    impl->id = id;

    char* buf = (char*)memalign(ADK_CACHE_LINE_SIZE, (size + sizeof(ObjChunk)) * 8192);
    auto* chunk_begin = buf;
    for (uint32_t i = 0; i != 8192; ++i)
    {
        auto* chunk = (ObjChunk*)chunk_begin;
        chunk->obj_size = size;
        chunk->flags = 0;
        chunk->obj_id = id;

        char* obj_addr = (char*)(chunk + 1);
        f(obj_addr);
        auto ret = impl->obj_queue->Push(obj_addr);
        assert(ret == ErrorCode::kSuccess);
        (void)ret;

        chunk_begin += size + sizeof(ObjChunk);
    }

    if (g_obj_gen == nullptr)
    {
        g_cmd_queue = MPSCQueue::Create(std::string("obj_pool_cmd"), sizeof(void*), 4096);
        g_obj_gen_is_running = true;
        g_obj_gen = new boost::thread(&ObjPoolBase::GenObjMain);
    }

    g_obj_pool_array[id] = impl;
    return g_cmd_queue->Push(this);
}

void DrainFreeQueue(ObjPoolBaseImpl* pool_impl)
{
    if (pool_impl->free_obj_queue->length() > 4096)
    {
        uint32_t counter = 0;
        do
        {
            char* obj_addr;
            if (pool_impl->free_obj_queue->Pop(obj_addr) != ErrorCode::kSuccess)
            {
                break;
            }

            ObjChunk* obj_chunk = (ObjChunk*)(obj_addr - sizeof(ObjChunk));
            if (obj_chunk->flags & ADK_OBJ_POOL_EXTRA_OBJ)
            {
                free(obj_chunk);
            }
            else
            {
                pool_impl->pending_obj_deq->push_back(obj_addr);
            }
        } while ((++counter) < 2048);
    }

    // release as soon as possible
    bool head = false, tail = false;
    while(!pool_impl->pending_obj_deq->empty())
    {
        // head
        char* obj_addr = (char*)pool_impl->pending_obj_deq->front();
        ObjChunk* obj_chunk = (ObjChunk*)(obj_addr - sizeof(ObjChunk));
        if (obj_chunk->flags & ADK_OBJ_POOL_EXTRA_OBJ)
        {
            free(obj_chunk);
            head = true;
            pool_impl->pending_obj_deq->pop_front();
        }
        else
        {
            head = false;
        }

        // tail
        obj_addr = (char*)pool_impl->pending_obj_deq->back();
        obj_chunk = (ObjChunk*)(obj_addr - sizeof(ObjChunk));
        if (obj_chunk->flags & ADK_OBJ_POOL_EXTRA_OBJ)
        {
            free(obj_chunk);
            tail = true;
            pool_impl->pending_obj_deq->pop_back();
        }
        else
        {
            tail = false;
        }

        if(!head && !tail)
        {
            break;
        }
    }
}

bool GetPendingObj(ObjPoolBaseImpl* pool_impl, void*& obj_addr)
{
    if (pool_impl->pending_obj_deq->empty())
        return false;

    obj_addr = pool_impl->pending_obj_deq->front();
    pool_impl->pending_obj_deq->pop_front();
    return true;
}

void ObjPoolBase::GenObjMain()
{
    std::vector<ObjPoolBase*> pools;
    ObjPoolBase* new_pool;
    pools.reserve(1024);

    while (g_obj_gen_is_running)
    {
        while (g_cmd_queue->Pop(new_pool) == ErrorCode::kSuccess)
        {
            pools.push_back(new_pool);
        }

        for (auto* pool : pools)
        {
            ObjPoolBaseImpl* pool_impl = (ObjPoolBaseImpl*)(pool->obj_pool_private_);
            if (pool_impl->obj_queue->length() < 4096)
            {
                if (pool_impl->obj_addr != nullptr)
                {
                    pool->f_((char*)pool_impl->obj_addr);
                    if (pool_impl->obj_queue->Push(pool_impl->obj_addr) == ErrorCode::kSuccess)
                    {
                        pool_impl->obj_addr = nullptr;    
                    }
                    else
                    {
                        continue;
                    }
                }

                uint32_t counter = 0;
                bool fillup = false;
                while ((++counter) <= 1024
                       && (pool_impl->free_obj_queue->Pop(pool_impl->obj_addr) == ErrorCode::kSuccess
                           || GetPendingObj(pool_impl, pool_impl->obj_addr)))
                {
                    pool->f_((char*)pool_impl->obj_addr);
                    if (pool_impl->obj_queue->Push(pool_impl->obj_addr) == ErrorCode::kSuccess)
                    {
                        pool_impl->obj_addr = nullptr;
                    }
                    else
                    {
                        fillup = true;
                        break;
                    }
                }

                if (fillup)
                {
                    DrainFreeQueue(pool_impl);
                    continue;
                }

                if (counter > 1024
                    || pool_impl->obj_queue->length() > 4096)
                    continue;

                do
                {
                    auto* chunk_begin = (char*)malloc(pool_impl->size + sizeof(ObjChunk));
                    auto* chunk = (ObjChunk*)chunk_begin;
                    chunk->obj_size = pool_impl->size;
                    chunk->flags = ADK_OBJ_POOL_EXTRA_OBJ;
                    chunk->obj_id = pool_impl->id;

                    char* obj_addr = (char*)(chunk + 1);
                    pool->f_(obj_addr);

                    if (pool_impl->obj_queue->Push(obj_addr) != ErrorCode::kSuccess)
                    {
                        free(chunk_begin);
                        DrainFreeQueue(pool_impl);
                        break;
                    }
                } while ((++counter) <= 1024);
            }
        }

        usleep(200);
    }
}

void* ObjPoolBase::New(int32_t id)
{
    assert(id < 4096);
    void *obj = nullptr;

    auto* pool_impl = g_obj_pool_array[id];
    if (pool_impl == nullptr)
    {
        return nullptr;
    }

    do
    {
        if (pool_impl->obj_queue->Pop(obj) == ErrorCode::kSuccess)
        {
            return obj;
        }

        ADK_PAUSE();
    } while (g_obj_gen_is_running);

    return nullptr;
}

void ObjPoolBase::Delete(void* obj, int32_t id)
{
    assert(id < 4096);

    auto* pool_impl = g_obj_pool_array[id];
    if (pool_impl == nullptr)
    {
        return;
    }

    if (pool_impl->free_obj_queue->Push(obj) != ErrorCode::kSuccess)
    {
        boost::mutex::scoped_lock lock_guard(g_delete_lock);
        pool_impl->pending_obj_deq->push_back(obj);
    }
}

int32_t Delete(void* obj)
{
    ObjChunk* obj_chunk = (ObjChunk*)((char*)obj - sizeof(ObjChunk));
    if (obj_chunk->obj_id < 0 || obj_chunk->obj_id > 4096)
    {
        return ErrorCode::kFailure;
    }

    auto* pool_impl = g_obj_pool_array[obj_chunk->obj_id];
    if (pool_impl == nullptr)
    {
        return ErrorCode::kFailure;
    }

    if (pool_impl->size != obj_chunk->obj_size)
    {
        return ErrorCode::kFailure;
    }

    if (pool_impl->free_obj_queue->Push(obj) != ErrorCode::kSuccess)
    {
        boost::mutex::scoped_lock lock_guard(g_delete_lock);
        pool_impl->pending_obj_deq->push_back(obj);
    }

    return ErrorCode::kSuccess;
}

} // variant

} // adk
