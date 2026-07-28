#include <adk/object_pool.h>
#include <adk_pack/object_pool.h>

namespace adk
{

using IPoolImpl = adk_impl::IPool;

IPool::IPool()
{
    new ((void*)(this)) IPoolImpl();
}

IPool::~IPool()
{
    reinterpret_cast<IPoolImpl*>(this)->~IPool();
}

int32_t IPool::Delete(void* mem_buf)
{
    return reinterpret_cast<IPoolImpl*>(this)->Delete((adk_impl::MemoryBuffer*)mem_buf);
}

using IObjectImpl = adk_impl::IObject;

int32_t IObject::Delete()
{
    return reinterpret_cast<IObjectImpl*>(this)->Delete();
}

namespace impl
{

using ObjectPoolImpl = adk_impl::ObjectPool<IObjectImpl>;

ObjectPool* ObjectPool::Create(const string& pool_name, 
                               uint32_t size, 
                               uint32_t element_size, 
                               void* constructor)
{
    ObjectPoolImpl* object_pool_impl = ObjectPoolImpl::Create(pool_name, 
                                                              size, 
                                                              element_size - sizeof(IObjectImpl), 
                                                              reinterpret_cast<adk_impl::construct_type>(constructor));
    if (nullptr != object_pool_impl)
    {
        ObjectPool* object_pool = new ObjectPool;
        object_pool->object_pool_impl_ = reinterpret_cast<void*>(object_pool_impl);
        return object_pool;
    }

    return nullptr;
}

void* ObjectPool::NewBuffer()
{
    IObjectImpl* object_buffer = reinterpret_cast<ObjectPoolImpl*>(object_pool_impl_)->NewObject();
    return (void*)object_buffer;
}

void* ObjectPool::NewBufferEx(bool is_force_new, uint32_t element_size)
{
    IObjectImpl* object_buffer = reinterpret_cast<ObjectPoolImpl*>(object_pool_impl_)->NewObjectEx(is_force_new,
        element_size - sizeof(IObjectImpl));
    return (void*)object_buffer;
}

}

}