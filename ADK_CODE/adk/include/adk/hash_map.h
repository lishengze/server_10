/**
 * @file       adk/hash_map.h
 * @brief      hash map
 * @author     zhangwei, zhangwei@archforce.com.cn
 * @date       2017/08/7
 */

#ifndef ADK_IMPL_HASH_MAP_H_
#define ADK_IMPL_HASH_MAP_H_

#include "shm_ptr.h"
#include "constant.h"
#include "error_code.h"
#include "arch/generic.h"
#include "arch/synchronize.h"
#include "lock_free_unbounded_queue_variant.h"

#include <string>
#include <utility>
#include <iostream>
#include <malloc.h>
#include <pthread.h>

#include <boost/array.hpp>
#include <boost/thread.hpp>
#include <boost/function.hpp>
#include <boost/unordered/unordered_map.hpp>

using std::string;
using std::vector;

namespace adk_impl
{

namespace hash
{

template<typename _Kty>
inline std::size_t hash_value(const _Kty& key)
{
    return boost::hash<_Kty>()(key);
}

template<typename _Kty, typename... _Args>
inline std::size_t hash_value(const _Kty& key, const _Args& ...keys)
{
    std::size_t seed = hash::hash_value(key);
    return (0x9e3779b9 + (seed << 6) + (seed >> 2)) + hash::hash_value(keys...);
}

}

template<typename... _Keys>
struct Hash
{
    std::size_t operator()(const _Keys& ...keys)
    {
        return hash::hash_value(keys...);
    }
};

template<typename _Kty1, typename _Kty2, typename _Vty, typename hasher=Hash<_Kty1, _Kty2>>
class HashMap;

struct CasLock
{
    CasLock() : lock(false) {};
    ~CasLock()
    {
        lock = false;
        is_lock = false;
    }
    volatile bool lock;
    static thread_local bool is_lock;
};

class CasLockImpl
{
public:
    CasLockImpl(CasLock &lock, bool init_lock=true) : lock_impl(&lock) 
    {
        if (init_lock)
        {
            Lock();
        }
    }

    ~CasLockImpl() 
    {
        Unlock();
    }

    inline void Lock()
    {
        while (!__sync_bool_compare_and_swap(&(lock_impl->lock), false, true))
        {
            ADK_PAUSE();
        }
    }

    bool Islock()
    {
        return lock_impl->lock;
    }

    inline void Unlock()
    {
        lock_impl->lock = false;
    }
private:
    CasLock *lock_impl;
};

template<typename _Kty1, typename _Kty2, typename _Vty, typename hasher>
struct Bucket
{
//type:
    typedef Bucket<_Kty1,_Kty2,_Vty,hasher> this_type;
    typedef typename HashMap<_Kty1,_Kty2,_Vty,hasher>::call_back call_back;
//member:
    _Kty1    key1;
    _Kty2    key2;
    _Vty     value;
    uint64_t hash_code;
    this_type *child;
    volatile bool inusing;
    CasLock  bucket_lock;
//function:
    void Init()
    {
        if (std::is_class<_Kty1>::value)
        {
            new ((void*)&key1) _Kty1();
        }

        if (std::is_class<_Kty2>::value)
        {
            new ((void*)&key2) _Kty2();
        }

        if (std::is_class<_Vty>::value)
        {
            new ((void*)&value) _Vty();
        }
        child = NULL;
        inusing = false;
        new ((void*)&bucket_lock) CasLock();
    }

    void SetNode(const _Kty1& key_1, const _Kty2& key_2,const _Vty& value_input, uint64_t hash_input)
    {
        hash_code = hash_input;
        CasLockImpl lock(bucket_lock);
        key1 = key_1;
        key2 = key_2;
        value = value_input;
    }

    int32_t CompareAndCover(const _Kty1& key_1, const _Kty2& key_2, const _Vty& value_input, uint64_t hash_input, call_back* cover)
    {
        if (hash_input != hash_code)
        {
            return kFailure;
        }

        //the same element
        CasLockImpl lock(bucket_lock);
        if ((key1 != key_1) || (key2 != key_2))
        {
            return kFailure;
        }
        
        //if the cover(call_back function is null) default to do cover
        if ((NULL == cover) || (*cover)(value))
        {
            value = value_input;
        }
        return kSuccess;
    }
};

template<typename _Kty1, typename _Kty2, typename _Vty, typename hasher>
struct AltBucket
{
//type:
    typedef typename HashMap<_Kty1,_Kty2,_Vty,hasher>::call_back call_back;
    typedef boost::unordered_map<std::pair<_Kty1,_Kty2>,_Vty>    alt_hash_map;
    
//member:  
    alt_hash_map* alt_cache_map;
    CasLock bucket_lock;
//function:
    void Init()
    {
        alt_cache_map = NULL;
        new ((void*)&bucket_lock) CasLock();
    }

    void Release()
    {
        if (NULL != alt_cache_map)
        {
            delete alt_cache_map;
            alt_cache_map = NULL;
        }
        bucket_lock.~CasLock();
    }

    int32_t SetNode(const _Kty1& key_1, const _Kty2& key_2, const _Vty& value_input, call_back* cover)
    {
        std::pair<_Kty1,_Kty2> key = std::make_pair(key_1, key_2);

        CasLockImpl lock(bucket_lock);
        if (NULL == alt_cache_map)
        {
            alt_cache_map = new alt_hash_map();
            if (NULL == alt_cache_map)
            {
                return kNoMemory;
            } 
        }
        else
        {
            auto iter = alt_cache_map->find(key);
            if (alt_cache_map->end() != iter)
            {
                if ((NULL == cover) || (*cover)(iter->second))
                {
                    iter->second = value_input;
                }
                return kSuccess;
            }
        }
        (*alt_cache_map)[key] = value_input;
        return kSuccess;
    }

    int32_t Find(const _Kty1& key_1, const _Kty2& key_2, _Vty** value)
    {
        if (NULL == alt_cache_map)
        {
            return kFailure;
        }

        CasLockImpl lock(bucket_lock);
        auto iter = alt_cache_map->find(std::make_pair(key_1,key_2));
        if (alt_cache_map->end() == iter)
        {
            return kFailure;
        }
        *value = &(iter->second);
        return kSuccess;
    }
};

struct HashMapHeader
{
    uint32_t bucket_init_num;
    uint32_t bucket_init_num_mask;
    uint32_t head_offset;
    uint32_t head_alt_offset;
    uint16_t bucket_ext_num;
    uint16_t bucket_ext_num_mask;
    uint16_t bucket_size;
    uint16_t bucket_alt_size;
    uint16_t deep_limit;
    void* entries()
    {
        return (void*)(ptr_add(this, head_offset)); 
    }

    void* altentries()
    {
        return (void*)(ptr_add(this, head_alt_offset)); 
    }
};

#define HASH_MAP_DEBUG 1

template<typename _Kty1, typename _Kty2, typename _Vty, typename hasher>
class HashMap
{
public:
#if HASH_MAP_DEBUG
    uint64_t collision_time_;
    uint32_t *usage_counter_;
#endif

    HashMap() : 
#if HASH_MAP_DEBUG
          collision_time_(0),
#endif
          bucket_init_num_(0),
          bucket_init_num_mask_(0),
          bucket_ext_num_(0),
          bucket_ext_num_mask_(0),
          bucket_size_(0),
          bucket_alt_size_(0),
          deep_limit_(0),
          bucket_init_num_bits_(0),
          bucket_ext_num_bits_(0),
          bucket_size_bits_(0),
          bucket_alt_size_bits_(0),
          bucket_(NULL),
          bucket_alt_(NULL),
          mem_pool_(NULL){}
    ~HashMap() = default;

//type:
    typedef HashMap<_Kty1,_Kty2,_Vty,hasher>    this_type;
    typedef Bucket<_Kty1,_Kty2,_Vty,hasher>     bucket_type;
    typedef AltBucket<_Kty1,_Kty2,_Vty,hasher>  altbucket_type;
    typedef boost::function<bool (const _Vty&)> call_back;

    /**
     * @brief      创建HashMap对象
     *
     * @param[in]  bucket_init_num 初始第一维bucket个数
     * @param[in]  扩展维数的限制
     * @param[in]  扩展维bucket的大小
     *
     * @return     成功时返回指向新生成对象的指针/失败返回NULL
     */
    static HashMap* Create(uint32_t bucket_init_num, uint16_t deep_limit = 4, uint16_t bucket_ext_num = 8)
    {
        this_type* hash_map = (this_type*)malloc(sizeof(this_type));
        if (NULL == hash_map)
        {
            return NULL;
        }

        new (hash_map) HashMap();

        uint32_t bucket_size = ADK_ROUND_TO_POWER_OF_2(sizeof(bucket_type));
        uint32_t bucket_alt_size = ADK_ROUND_TO_POWER_OF_2(sizeof(altbucket_type));
        uint32_t init_num = ADK_ROUND_TO_POWER_OF_2(bucket_init_num);
        uint32_t ext_num = ADK_ROUND_TO_POWER_OF_2(bucket_ext_num);
        uint32_t header_size = ADK_ROUND_UP(sizeof(HashMapHeader), ADK_PAGE_SIZE);
        uint32_t bucket_mem_size = init_num * bucket_size;
        uint32_t bucket_mem_alt_size = init_num * bucket_alt_size;
        uint32_t total_size = header_size + bucket_mem_size + bucket_mem_alt_size;
        HashMapHeader* hash_map_header = (HashMapHeader*)memalign(ADK_PAGE_SIZE, total_size);
        if (NULL == hash_map_header)
        {
            hash_map->~HashMap();
            free(hash_map);
            return NULL;
        }

        memset(hash_map_header, 0, total_size);
        hash_map_header->bucket_init_num = init_num;
        hash_map_header->bucket_init_num_mask = init_num - 1;
        hash_map_header->bucket_ext_num = ext_num;
        hash_map_header->bucket_ext_num_mask = ext_num - 1;
        hash_map_header->bucket_size = bucket_size;
        hash_map_header->bucket_alt_size = bucket_alt_size;  
        hash_map_header->head_offset = header_size;
        hash_map_header->head_alt_offset = header_size + bucket_mem_size;
        hash_map_header->deep_limit = deep_limit;

        bucket_type* bucket = (bucket_type*)hash_map_header->entries();
        altbucket_type* bucket_alt = (altbucket_type*)hash_map_header->altentries();
        for (uint32_t bucket_index=0; bucket_index<init_num; ++bucket_index)
        {
            bucket->Init();
            bucket_alt->Init();
            bucket = ptr_add(bucket, bucket_size);
            bucket_alt = ptr_add(bucket_alt, bucket_alt_size);
        }
        hash_map->Init(hash_map_header);
        return hash_map;
    }

    /**
     * @brief      初始化HashMap
     *
     * @param[in]  header为实体内存部分
     *
     * @return     成功返回kSuccess/失败返回相应的ErrorCode
     */
    int32_t Init(struct HashMapHeader* header)
    {
        assert(NULL != header);
        bucket_init_num_ = header->bucket_init_num;
        bucket_init_num_mask_ = header->bucket_init_num_mask;
        bucket_ext_num_ = header->bucket_ext_num;
        bucket_ext_num_mask_ = header->bucket_ext_num_mask;
        bucket_size_ = header->bucket_size;
        bucket_alt_size_ = header->bucket_alt_size;
        deep_limit_ = header->deep_limit;
        bucket_init_num_bits_ = GetBits(bucket_init_num_);
        bucket_ext_num_bits_ = GetBits(bucket_ext_num_);
        bucket_size_bits_ = GetBits(bucket_size_);
        bucket_alt_size_bits_ = GetBits(bucket_alt_size_);
        bucket_ = (bucket_type*)header->entries();
        bucket_alt_ = (altbucket_type*)header->altentries();
        ADK_CHECK_RET_SUCCESS(InitMemCache());
#if HASH_MAP_DEBUG
        usage_counter_ = new uint32_t[deep_limit_ + 1];
        for (uint16_t index=0; index<deep_limit_ + 1; ++index)
        {
            usage_counter_[index] = 0;
        }
#endif
        return kSuccess;
    }

    /**
     * @brief      插入元素
     *
     * @param[in]  key  顾名思义
     * @param[in]  value顾名思义
     * @param[in]  cover数据覆盖的回调函数
     *
     * @return     成功返回kSuccess/失败返回相应的ErrorCode
     */
    inline int32_t Insert(const _Kty1& key_1, const _Kty2& key_2, const _Vty& value, call_back* cover=NULL)
    {
        const uint64_t orig_hask = hash_(key_1, key_2);
        const uint32_t level_0_pos = orig_hask & bucket_init_num_mask_;

        uint16_t cur_deep = 0;
        uint64_t temp_hash = orig_hask >> bucket_init_num_bits_;
        bucket_type* bucket = ptr_add(bucket_, level_0_pos<<bucket_size_bits_);
        
        while(true)
        {
            if (bucket->inusing)
            {
                //if the slot is in using, try to compare and cover
                if (kSuccess == bucket->CompareAndCover(key_1, key_2, value, orig_hask, cover))
                {
                    return kSuccess;
                }

                if (ADK_UNLIKELY(++cur_deep > deep_limit_))
                {
                    break;
                }

                //extend to deeper level
                if (NULL == bucket->child)
                {
                    bucket_type* new_bucket = NewBucketExt();
                    if (NULL == new_bucket)
                    {
                        return kNoMemory;
                    }

                    //try to add new bucket. 
                    if (!__sync_bool_compare_and_swap(&(bucket->child), NULL, new_bucket))
                    {
                        //if add new bucket fail, give memory back to pool.
                        mem_pool_->Push(new_bucket);
                    }
#if HASH_MAP_DEBUG
                    else
                    {
                        ++collision_time_;
                    }
#endif
                }

                //iterative to deeper level
                bucket = ptr_add(bucket->child, (temp_hash & bucket_ext_num_mask_) << bucket_size_bits_);
                temp_hash >>= bucket_ext_num_bits_;
            }
            else
            {
                //try to lock the slot
                if(__sync_bool_compare_and_swap(&(bucket->inusing), false, true))
                {
#if HASH_MAP_DEBUG
                    ++(usage_counter_[cur_deep]);
#endif
                    //lock slot success insert the value
                    bucket->SetNode(key_1, key_2, value, orig_hask);
                    return kSuccess;
                }
            }
        }

        //search node failed in the preferred hash_map, to search the alt map
        altbucket_type* bucket_alt = ptr_add(bucket_alt_, level_0_pos<<bucket_alt_size_bits_);
        ADK_CHECK_RET_SUCCESS(bucket_alt->SetNode(key_1, key_2, value, cover));
#if HASH_MAP_DEBUG
        ++(usage_counter_[cur_deep]);
#endif
        return kSuccess;
    }

    /**
     * @brief      查找元素
     *
     * @param[in]  key  顾名思义
     * @param[out] **value指向元素的值的引用
     *
     * @return     成功返回kSuccess/失败返回相应的ErrorCode
     */
    int32_t Find(const _Kty1& key_1, const _Kty2& key_2, _Vty** value)
    {
        const uint64_t orig_hask = hash_(key_1, key_2);
        const uint32_t level_0_pos = orig_hask & bucket_init_num_mask_;
        
        uint16_t cur_deep = 0;
        uint64_t temp_hash = orig_hask >> bucket_init_num_bits_;
        bucket_type* bucket = ptr_add(bucket_, level_0_pos<<bucket_size_bits_);

        while(true)
        {
            if (bucket->inusing && (orig_hask == bucket->hash_code))
            {
                CasLockImpl lock(bucket->bucket_lock);
                if ((key_1 == bucket->key1)&&(key_2 == bucket->key2))
                {
                    *value = &(bucket->value);
                    return kSuccess;
                }
            }

            if (ADK_UNLIKELY(NULL == bucket->child))
            {
                break;
            }

            bucket = ptr_add(bucket->child, (temp_hash & bucket_ext_num_mask_)<<bucket_size_bits_);
            temp_hash >>= bucket_ext_num_bits_;
            ++cur_deep;
        }

        if (ADK_UNLIKELY(cur_deep < deep_limit_))
        {
            return kFailure;
        }

        altbucket_type* bucket_alt = ptr_add(bucket_alt_, level_0_pos << bucket_alt_size_bits_);
        ADK_CHECK_RET_SUCCESS(bucket_alt->Find(key_1, key_2, value));
        return kSuccess;
    }

    uint64_t GetHash(const _Kty1& key_1, const _Kty2& key_2)
    {
        return hash_(key_1, key_2);
    }
    
private:

    inline void InitBucketExt(bucket_type* const bucket_ext)
    {
        for (uint32_t index = 0; index < bucket_ext_num_; ++index)
        {
            auto* const temp_bucket_ext = ptr_add(bucket_ext, index << bucket_size_bits_);
            temp_bucket_ext->Init();
        }
    }

    int32_t InitMemCache()
    {
        mem_pool_ = variant::MPSCUnboundedQueue<bucket_type*>::Create("hash_map_mem_cache", 8192);
        if (NULL == mem_pool_)
        {
            return kNoMemory;
        }

        uint64_t ext_mem_num = bucket_ext_num_ << bucket_size_bits_;
        uint64_t new_mem_size = ADK_ROUND_UP(ext_mem_num * 8192, ADK_PAGE_SIZE);
        bucket_type* bucket_ext = (bucket_type*)malloc(new_mem_size);
        if (ADK_UNLIKELY(nullptr == bucket_ext))
        {
            return kNoMemory;
        }

        memset(bucket_ext, 0, new_mem_size);
        bucket_type* end_bucket = (bucket_type*)((char*)ptr_add(bucket_ext, new_mem_size) - ext_mem_num);     
        while (bucket_ext <= end_bucket)
        {
            InitBucketExt(bucket_ext);
            mem_pool_->Push(bucket_ext);
            bucket_ext = ptr_add(bucket_ext, ext_mem_num);
        }
        return kSuccess;
    }

    bucket_type* NewBucketExt()
    {
        int pop_res;
        bucket_type* new_bucket_ext;

        {
            CasLockImpl lock(queue_consume_lock_);
            pop_res = mem_pool_->Pop(new_bucket_ext);
        }
        
        if (kSuccess != pop_res)
        {
            uint64_t ext_mem_num = bucket_ext_num_ << bucket_size_bits_;
            uint64_t new_mem_size = std::max<uint64_t>(ext_mem_num << 8, ADK_PAGE_SIZE << 4);
            new_bucket_ext = (bucket_type*)malloc(new_mem_size);
            if (ADK_UNLIKELY(nullptr == new_bucket_ext))
            {
                return nullptr;
            }

            memset(new_bucket_ext, 0, new_mem_size);
            InitBucketExt(new_bucket_ext);

            bucket_type* end_bucket = (bucket_type*)((char*)ptr_add(new_bucket_ext, new_mem_size) - ext_mem_num);     
            bucket_type* push_bucket = ptr_add(new_bucket_ext, ext_mem_num);
            while (push_bucket <= end_bucket)
            {
                InitBucketExt(push_bucket);
                mem_pool_->Push(push_bucket);
                push_bucket = ptr_add(push_bucket, ext_mem_num);
            }
        }

        return new_bucket_ext;
    }

    uint32_t bucket_init_num_;
    uint32_t bucket_init_num_mask_;
    uint32_t bucket_ext_num_;
    uint32_t bucket_ext_num_mask_;
    uint16_t bucket_size_;
    uint16_t bucket_alt_size_;
    uint16_t deep_limit_;
    uint16_t bucket_init_num_bits_;
    uint16_t bucket_ext_num_bits_;
    uint16_t bucket_size_bits_;
    uint16_t bucket_alt_size_bits_;

    bucket_type* bucket_;
    altbucket_type* bucket_alt_;

    CasLock queue_consume_lock_;
    variant::MPSCUnboundedQueue<bucket_type*>* mem_pool_;

    hasher hash_;
};
}
#endif