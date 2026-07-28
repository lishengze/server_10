#pragma once

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/unordered_map.hpp>     //boost::unordered_map

#include <functional>                  //std::equal_to
#include <boost/functional/hash.hpp>   //boost::hash

#include <iostream>
#include <mutex>
#include <thread>
#include <atomic>

namespace sharding
{
    class ShardingAgent;
    class ShardingProxy;
    class ShmSeqLock;
    struct ShardingCtx;
}

namespace aaf
{

class ShardingSeqLock
{
private:
    ShardingSeqLock() = default;

public:
    /**
     * @brief 加锁接口
     * 
     * @note 广播消息目前不支持加锁
     */
    void Lock();

    /**
     * @brief 释放锁
     * 
     * @note 可以单独调用 UnLock，表示该 total_seq 的消息无须加锁
     *        无须加锁的消息序号可以不用等待前序消息解锁完成。
     */
    void UnLock();

private:
    sharding::ShmSeqLock* shm_seq_lock_ = nullptr;

    sharding::ShardingCtx* sharding_ctx_ = nullptr;

    friend class ShmDataManager;
    friend class sharding::ShardingAgent;
    friend class sharding::ShardingProxy;
};

class ShmDataManager
{
private:

    ShmDataManager() = default;
public:

    static ShmDataManager* GetInstance();

    ShardingSeqLock* CreateSeqLock(const std::string& name, uint32_t cache_size = 65536);

    void* Allocate(uint32_t size);
    
    void Deallocate(void* ptr);

private:

    template <class T>
    struct ConstructHelperImpl
    {
        template <class... Args>
        static T* Construct(const char* name, Args&&... args)
        {
            // static_assert(sizeof...(Args) > 9999, "Invalid Type");
            using ObjectType = T;
            auto* ret = s_shm_segment_->construct<ObjectType>(name)(s_shm_segment_->get_allocator<ObjectType>());

            return ret;
        }
    };


    template <class K, class T, class H, class P, class A>
    struct ConstructHelperImpl<boost::unordered_map<K, T, H, P, A>>
    {
        using ObjectType = boost::unordered_map<K, T, H, P, A>;

        template <class... Args>
        static ObjectType* Construct(const char* name, Args&&... args)
        {
            using HashType    = H;
            using CompareType = P;

            auto* ret = s_shm_segment_->construct<ObjectType>(name)(
                1,
                HashType {},
                CompareType {},
                s_shm_segment_->get_allocator<ObjectType>());

            return ret;
        }
    };

    template <class K, class T, class P, class A>
    struct ConstructHelperImpl<std::map<K, T, P, A>>
    {
        using ObjectType = std::map<K, T, P, A>;

        template <class... Args>
        static ObjectType* Construct(const char* name, Args&&... args)
        {
            using CompareType = P;

            auto* ret = s_shm_segment_->construct<ObjectType>(name)(
                CompareType {},
                s_shm_segment_->get_allocator<ObjectType>());

            return ret;
        }
    };

public:
    template <class T, class... Args>
    auto Construct(const char* name, Args&&... args) -> T*
    {
        assert(s_shm_segment_);
        return ConstructHelperImpl<T>::Construct(name, std::forward<Args>(args)...);
    }

private:
    sharding::ShardingAgent* sharding_agent_ = nullptr;

    static boost::interprocess::managed_shared_memory* s_shm_segment_;

    friend class sharding::ShardingAgent;
    friend class sharding::ShardingProxy;
};

}   // end of namespace aaf

