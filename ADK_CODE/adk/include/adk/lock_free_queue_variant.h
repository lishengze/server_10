/**
 * @file       adk/lock_free_queue_variant.h
 * @brief      lock free queue variant
 * @note       SPSC\MPSC\SPMC\MPMC
 * @author     zhangwei, zhangwei@archforce.com.cn
 * @date       2017/06/30
 */

/**
 * @note       使用MPSCQueue/SPMCQueue/MPMCQueue的版本
 *             需要注意：多线出队或者入队的时候，由于多线程操作队列动作不做同步，
 *             即多线程出队时出队顺序不做保证，多线程入队时，入队顺序也不做保证，
 *             一般使用场景不会由于上述限制有任何影响，
 *             但是使用场景中有出队的元素需要再入队的情况，可能由于某个出队线程未完成导致队列出现虚空
 *             |n|y|n|n|n|y|n|n|
 */

#ifndef ADK_IMPL_LOCK_FREE_QUEUE_VARIANT_H_
#define ADK_IMPL_LOCK_FREE_QUEUE_VARIANT_H_

#include "util.h"
#include "libadk.h"
#include "shm_ptr.h"
#include "constant.h"
#include "error_code.h"
#include "arch/generic.h"

#include <assert.h>
#include <string>
#include <boost/function.hpp>

#ifndef QUEUE_CURSOR_INIT_VALUE
#define QUEUE_CURSOR_INIT_VALUE             1
#endif

#ifndef QUEUE_CURSOR_INVALID_VALUE
#define QUEUE_CURSOR_INVALID_VALUE          0
#endif

#define PTR_ENTRY_EMPTY                     0
#define VARIANT_QUEUE_CURSOR_INIT_VALUE     -1

#define CHECK_PTR_QUEUE_FULL(entry_ptr)\
    do\
    {\
        if (ADK_UNLIKELY(PTR_ENTRY_EMPTY != ACCESS_ONCE((entry_ptr)->size)))\
            return kQueueFull;\
    }while(false)

#define CHECK_PTR_QUEUE_EMPTY(entry_ptr)\
    do\
    {\
        if (ADK_UNLIKELY(PTR_ENTRY_EMPTY == ACCESS_ONCE((entry_ptr)->size)))\
            return kQueueEmpty;\
    }while(false)

#define POSITIVE_NUM_INT64(int64_num)   ((~(int64_num) + 1) & ADK_DEL_ULL_CONST(0x7FFFFFFFFFFFFFFF))
#define NEGATIVE_NUM_INT64(int64_num)   ( - (int64_num))
//#define NEGATIVE_NUM_INT64(int64_num)   ((~(int64_num) + 1) | ADK_DEL_ULL_CONST(0x800000000000000))

#define DEFINE_BACKOFF_COUNTER()\
    uint32_t backoff_counter = 128;

    //uint32_t backoff_counter = 512;

#define PAUSE_BACKOFF()\
    do\
    {\
        for (uint32_t i = 0; i < backoff_counter; ++i)\
            ADK_PAUSE();\
    }while(false)

namespace adk_impl
{

namespace variant
{

struct VariantEntry
{
    int64_t  pos;
    char     buffer[];

    static VariantEntry* GetEntry(void* buf)
    {
        return ADK_CONTAINER_OF(buf, VariantEntry, buffer);
    }

    static uint32_t CalcEntrySize(uint32_t payload_size)
    {
        uint32_t entry_size = ADK_ROUND_TO_POWER_OF_2(payload_size + sizeof(struct VariantEntry));
    #if 1
        return entry_size;
    #else
        return entry_size > ADK_CACHE_LINE_SIZE ? entry_size : ADK_CACHE_LINE_SIZE;
    #endif
    }
};

struct PtrEntry
{
    uint64_t    size;
    char        buffer[];

    static uint32_t PtrEntrySize()
    {
        return ADK_ROUND_UP(sizeof(void*) + sizeof(struct PtrEntry), ADK_CACHE_LINE_SIZE);
        //return ADK_ROUND_TO_POWER_OF_2(sizeof(void*) + sizeof(struct PtrEntry));
    }
};

struct VariantQueueHeader
{
    char        queue_name[ADK_MAX_NAME_LEN];
    uint32_t    entry_size;
    uint32_t    queue_mask;
    uint32_t    queue_size;
    int64_t     reference_count;
    // offset from the header
    uint64_t    queue_entry_offset;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t    tail;

    ADK_EMPTY_CACHE_LINE;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t    head;

    void* entries()
    {
        return (void*)(ptr_add(this, queue_entry_offset)); 
    }
};

struct ThreadLocalQueueHeader
{
    char        queue_name[ADK_MAX_NAME_LEN];
    uint32_t    entry_size;
    uint32_t    queue_mask;
    uint32_t    queue_size;
    int64_t     reference_count;
    // offset from the header
    uint64_t    queue_entry_offset;
    uint64_t    tail;
    uint64_t    head;

    void* entries()
    {
        return (void*)(ptr_add(this, queue_entry_offset));
    }
};

template<typename QHeaderType>
struct is_thread_local : std::false_type
{
};

template<>
struct is_thread_local<ThreadLocalQueueHeader> : std::true_type
{
};

class ADK_API PtrQueueBase
{
public:
    PtrQueueBase();
    ~PtrQueueBase();

    /**
     * @brief      将VariantQueueHeader所指向的queue实体挂载到现有的对象上
     *
     * @param[in]  header 无锁队列内存头结构
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t Init(struct VariantQueueHeader* header);
 
    /**
     * @brief      创建无锁队列并将已有的队列内存挂载到该对象上
     *
     * @param[in]      header 无锁队列内存头结构
     *
     * @return     成功时返回 非空对象指针
     */
    static PtrQueueBase *Create(struct VariantQueueHeader* header);

    /**
     * @brief      创建完整的对象结构
     *
     * @param      name 队列名
     * @param      queue_size 制定队列的大小
     *
     * @return     成功时返回 非空对象指针
     */
    static PtrQueueBase *Create(const std::string &name, uint32_t queue_size);

    /**
     * @brief      复制一个队列与源队列共实体
     *
     * @param[in]  queue 源队列
     *
     * @return     成功时返回 非空对象指针
     */
    static PtrQueueBase *Duplicate(PtrQueueBase& queue);

    /**
     * @brief      复制一个队列与源队列共实体
     *
     * @return     成功时返回 非空对象指针
     */
    PtrQueueBase *Duplicate();

    /**
     * @brief      删除队列对象
     *
     * @param[in]  ptr_queuebase 队列对象
     *
     * @attention  函数之删除对象本身的内存，不处理队列内存
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    static int32_t Delete(PtrQueueBase* ptr_queuebase);

    /**
     * @brief      释放所有出队和入队被阻塞住的线程
     *
     * @attention  该函数的调用后该队列将不能再正常使用
     */
    void set_release_alert()
    {
        release_alert_ = true;
    }

    /**
     * @brief     获取每个内存entry能放指针的个数
     *
     * @attention 该数值也是每次能够入队和出队的指针的最大值
     */
    static uint32_t GetEntrySizeLimit()
    {
        return s_entry_size_limit_;
    }

    /**
     * @brief      初始化VariantQueueHeader指向的内存结构
     *
     * @param[in]  header 无锁队列内存头结构
     *
     * @return     成功时返回 队列内存end后第一个无效内存
     */
    static struct PtrEntry* InitEntries(struct VariantQueueHeader* header);

#ifndef __ADK_DEBUG__
protected:
#endif

    template<typename T>
    inline int32_t SP_Push(T* payload)
    {
        uint64_t monotonic_tail_index = queue_header_->tail;
        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_tail_index) << entry_bits_);
        CHECK_PTR_QUEUE_FULL(entry_ptr);
        char* const buffer = entry_ptr->buffer;
        *(T**)(buffer) = payload;
        ADK_BARRIER();
        entry_ptr->size = 1;
        ++(queue_header_->tail);
        return kSuccess;
    }

    template<typename T>
    inline int32_t SP_Push(T* payload[], uint32_t num, uint32_t& push_num)
    {
        assert(0 != num);

        uint64_t monotonic_tail_index = queue_header_->tail;
        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_tail_index) << entry_bits_);
        CHECK_PTR_QUEUE_FULL(entry_ptr);

        push_num = num < s_entry_size_limit_ ? num : s_entry_size_limit_;

        /*for (uint32_t index=0; index<push_num; ++index)
        {
            *(T**)ptr_add(entry_ptr->buffer, index<<ptr_bits_) = payload[index];
        }*/

        memcpy(entry_ptr->buffer, payload, push_num << ptr_bits_);
        
        ADK_BARRIER();
        entry_ptr->size = push_num;
        ++(queue_header_->tail);
        return kSuccess;
    }

    //payload的合法内存大小必须大于等于entry_size_limit_
    template<typename T>
    inline int32_t SC_Pop(T* payload[], uint32_t& pop_num)
    {
        uint64_t monotonic_head_index = queue_header_->head;
        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);
        
        if (ADK_UNLIKELY(PTR_ENTRY_EMPTY == (pop_num = entry_ptr->size)))
        {
            return kQueueEmpty;
        }

        /*for (uint32_t index=0; index<pop_num; ++index)
        {
            payload[index] = *(T**)ptr_add(entry_ptr->buffer, index<<ptr_bits_);
        }*/

        memcpy(payload, entry_ptr->buffer, pop_num << ptr_bits_);

        ADK_BARRIER();
        entry_ptr->size = PTR_ENTRY_EMPTY;
        ++(queue_header_->head);
        return kSuccess;
    }

    template<typename T>
    inline int32_t SC_Pop(T** payload)
    {
        uint64_t monotonic_head_index = queue_header_->head;
        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);
        
        if (ADK_UNLIKELY(PTR_ENTRY_EMPTY == entry_ptr->size))
        {
            return kQueueEmpty;
        }

        char* const buffer = entry_ptr->buffer;
        *payload = *(T**)(buffer);

        ADK_BARRIER();
        entry_ptr->size = PTR_ENTRY_EMPTY;
        ++(queue_header_->head);
        return kSuccess;
    }

    template<typename T>
    inline int32_t MPSC_Push(T* payload)
    {
        uint64_t monotonic_tail_index = __sync_fetch_and_add(&(queue_header_->tail), 1);
        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_tail_index) << entry_bits_);

        DEFINE_BACKOFF_COUNTER();
        while (ADK_UNLIKELY(monotonic_tail_index >= tail_threshold_))
        {
            tail_threshold_ = queue_header_->head + queue_size_;
            if (monotonic_tail_index < tail_threshold_)
            {
                break;
            }

            if (ACCESS_ONCE(release_alert_))
            {
                return kWouldblock;
            }
            PAUSE_BACKOFF();
        }

        char* const buffer = entry_ptr->buffer;
        *(T**)(buffer) = payload;

        ADK_BARRIER();
        entry_ptr->size = 1;
        return kSuccess;
    }

    template<typename T>
    inline int32_t MPSC_Push(T* payload[], uint32_t num, uint32_t& push_num)
    {
        assert(0 != num);

        uint64_t monotonic_tail_index = __sync_fetch_and_add(&(queue_header_->tail), 1);
        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_tail_index) << entry_bits_);

        DEFINE_BACKOFF_COUNTER();
        while (ADK_UNLIKELY(monotonic_tail_index >= tail_threshold_))
        {
            tail_threshold_ = queue_header_->head + queue_size_;
            if (monotonic_tail_index < tail_threshold_)
            {
                break;
            }

            if (ACCESS_ONCE(release_alert_))
            {
                return kWouldblock;
            }
            PAUSE_BACKOFF();
        }

        push_num = num < s_entry_size_limit_ ? num : s_entry_size_limit_;
        memcpy(entry_ptr->buffer, payload, push_num<<ptr_bits_);
        
        ADK_BARRIER();
        entry_ptr->size = push_num;
        return kSuccess;
    }

    template<typename T>
    inline int32_t MPSC_TryPush(T* payload)
    {
        uint64_t temp_tail;
        
        do 
        {
            temp_tail = queue_header_->tail;
            if (ADK_UNLIKELY(temp_tail >= tail_threshold_))
            {
                tail_threshold_ = queue_header_->head + queue_size_;
                if (ADK_UNLIKELY(temp_tail >= tail_threshold_))
                {
                    return kQueueFull;
                }
            }
        } while (!__sync_bool_compare_and_swap(&(queue_header_->tail), temp_tail, temp_tail + 1));

        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(temp_tail) << entry_bits_);
        char* const buffer = entry_ptr->buffer;
        *(T**)(buffer) = payload;

        ADK_BARRIER();
        entry_ptr->size = 1;
        return kSuccess;
    }

    template<typename T>
    inline int32_t MPSC_TryPush(T* payload[], uint32_t num, uint32_t& push_num)
    {
        assert(0 != num);

        uint64_t temp_tail;

        do 
        {
            temp_tail = queue_header_->tail;
            if (ADK_UNLIKELY(temp_tail >= tail_threshold_))
            {
                tail_threshold_ = queue_header_->head + queue_size_;
                if (ADK_UNLIKELY(temp_tail >= tail_threshold_))
                {
                    return kQueueFull;
                }
            }
        } while (!__sync_bool_compare_and_swap(&(queue_header_->tail), temp_tail, temp_tail + 1));

        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(temp_tail) << entry_bits_);
        push_num = num < s_entry_size_limit_ ? num : s_entry_size_limit_;
        memcpy(entry_ptr->buffer, payload, push_num<<ptr_bits_);

        ADK_BARRIER();
        entry_ptr->size = push_num;
        return kSuccess;
    }

    template<typename T>
    inline int32_t SPMC_Pop(T* payload[], uint32_t& pop_num)
    {
        uint64_t monotonic_head_index = __sync_fetch_and_add(&(queue_header_->head), 1);
        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);

        DEFINE_BACKOFF_COUNTER();
        while (ADK_UNLIKELY(monotonic_head_index >= head_threshold_))
        {
            head_threshold_ = queue_header_->tail;
            if (monotonic_head_index < head_threshold_)
            {
                break;
            }

            if (ACCESS_ONCE(release_alert_))
            {
                return kWouldblock;
            }
            PAUSE_BACKOFF();
        }

        pop_num = entry_ptr->size;
        memcpy(payload, entry_ptr->buffer, pop_num<<ptr_bits_);

        ADK_BARRIER();
        entry_ptr->size = PTR_ENTRY_EMPTY;
        return kSuccess;
    }

    template<typename T>
    inline int32_t SPMC_Pop(T** payload)
    {
        uint64_t monotonic_head_index = __sync_fetch_and_add(&(queue_header_->head), 1);
        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);

        DEFINE_BACKOFF_COUNTER();
        while (ADK_UNLIKELY(monotonic_head_index >= head_threshold_))
        {
            head_threshold_ = queue_header_->tail;
            if (monotonic_head_index < head_threshold_)
            {
                break;
            }

            if (ACCESS_ONCE(release_alert_))
            {
                return kWouldblock;
            }
            PAUSE_BACKOFF();
        }

        char* const buffer = entry_ptr->buffer;
        *payload = *(T**)buffer;

        ADK_BARRIER();
        entry_ptr->size = PTR_ENTRY_EMPTY;
        return kSuccess;
    }

    template<typename T>
    inline int32_t SPMC_TryPop(T** payload)
    {
        uint64_t temp_head;

        do
        {
            temp_head = queue_header_->head;
            if (ADK_UNLIKELY(temp_head >= head_threshold_))
            {
                head_threshold_ = queue_header_->tail;
                if (ADK_UNLIKELY(temp_head >= head_threshold_))
                {
                    return kQueueEmpty;
                }
            }
        }while (!__sync_bool_compare_and_swap(&(queue_header_->head), temp_head, temp_head + 1));

        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(temp_head) << entry_bits_);
        
        char* const buffer = entry_ptr->buffer;
        *payload = *(T**)buffer;

        ADK_BARRIER();
        entry_ptr->size = PTR_ENTRY_EMPTY;
        return kSuccess;
    }

    template<typename T>
    inline int32_t SPMC_TryPop(T* payload[], uint32_t& pop_num)
    {
        uint64_t temp_head;

        do
        {
            temp_head = queue_header_->head;
            if (ADK_UNLIKELY(temp_head >= head_threshold_))
            {
                head_threshold_ = queue_header_->tail;
                if (ADK_UNLIKELY(temp_head >= head_threshold_))
                {
                    return kQueueEmpty;
                }
            }
        }while (!__sync_bool_compare_and_swap(&(queue_header_->head), temp_head, temp_head + 1));

        PtrEntry *entry_ptr = ptr_add(entries_, GetInQueueIndex(temp_head) << entry_bits_);
        pop_num = entry_ptr->size;
        memcpy(payload, entry_ptr->buffer, pop_num<<ptr_bits_);

        ADK_BARRIER();
        entry_ptr->size = PTR_ENTRY_EMPTY;
        return kSuccess;
    }

    inline uint64_t GetInQueueIndex(uint64_t index)
    {
        return index & queue_mask_;
    }

#ifndef __ADK_DEBUG__
private:
#endif

    VariantQueueHeader* queue_header_;
    struct PtrEntry*    entries_;
    uint64_t            queue_mask_;
    uint64_t            queue_size_;
    //设定entry_size为2的N次方
    uint32_t            entry_bits_;
    //设定ptrsize为2的N次方
    uint32_t            ptr_bits_;
    //一次最多入队的数量
    static uint32_t     s_entry_size_limit_;
    bool                release_alert_;
    ADK_EMPTY_CACHE_LINE;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t  head_threshold_;
    ADK_EMPTY_CACHE_LINE;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t  tail_threshold_;
    ADK_EMPTY_CACHE_LINE;
};

class SPSCPtrQueue : public PtrQueueBase
{
public:
    SPSCPtrQueue() = default;
    ~SPSCPtrQueue() = default;

    static SPSCPtrQueue *Create(const std::string &name, uint32_t queue_size)
    {
        return (SPSCPtrQueue*)PtrQueueBase::Create(name, queue_size);
    }

    /**
     * @brief      向无锁队列中入队一个元素
     *
     * @param[in]  *payload  入队内存地址
     *
     * @return     成功时返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Push(T* payload)
    {   
        return SP_Push(payload);
    }

    /**
     * @brief      向无锁队列中入队一组数据
     *
     * @param[in]  * payload[]  入队的内存地址
     * @param[in]  num          请求入队元素数量
     * @param[out] & push_num   实际入队数量
     *
     * @attention  每次入队个数最大值限制为GetEntrySizeLimit;部分入队成功也算入队成功
     *
     * @return     成功返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Push(T* payload[], uint32_t num, uint32_t& push_num)
    {
        return SP_Push(payload, num, push_num);
    }

    /**
     * @brief      从无锁队列中取出部分
     *
     * @param[out] * payload[]  出队目标内存
     * @param[out] & pop_num    实际出队的数量
     *
     * @attention  * payload[]的目标内存由调用方维护，数组长度不能小于GetEntrySizeLimit;
     *
     * @return     成功返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Pop(T* payload[], uint32_t& pop_num)
    {
        return SC_Pop(payload, pop_num);
    }

    /**
     * @brief      从无锁队列中取出一个指针
     *
     * @param[out] ** payload出队目标指针
     *
     * @attention  取出一个solt内的一个指针后，该slot的内存将失效
     *
     * @return     成功返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Pop(T** payload)
    {
        return SC_Pop(payload);
    }
};

class MPSCPtrQueue : public PtrQueueBase
{
public:
    MPSCPtrQueue() = default;
    ~MPSCPtrQueue() = default;

    static MPSCPtrQueue *Create(const std::string &name, uint32_t queue_size)
    {
        return (MPSCPtrQueue*)PtrQueueBase::Create(name, queue_size);
    }

    /**
     * @brief      向无锁队列中入队一个元素
     *
     * @param[in]  *payload  入队内存地址
     *
     * @attention  MP入队时，如果队列是满的，该函数将被阻塞直到入队成功为止或者调用set_release_alert函数
     *
     * @return     成功时返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Push(T* payload)
    {    
        return MPSC_Push(payload);
    }

    /**
     * @brief      尝试向无锁队列入队数据
     *
     * @param[in]  *payload  入队内存地址
     *
     * @attention  MP入队非阻塞版本
     *
     * @return     成功时返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t TryPush(T* payload)
    {
        return MPSC_TryPush(payload);
    }

    /**
     * @brief      向无锁队列中入队一组数据
     *
     * @param[in]  * payload[]  入队的内存地址
     * @param[in]  num          请求入队元素数量
     * @param[out] & push_num   实际入队数量
     *
     * @attention  每次入队个数最大值限制为GetEntrySizeLimit;部分入队成功也算入队成功
     *             如果队列是满的，该函数将被阻塞直到入队成功为止或者调用set_release_alert函数
     *
     * @return     成功返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Push(T* payload[], uint32_t num, uint32_t& push_num)
    {
        return MPSC_Push(payload, num, push_num);
    }

    /**
     * @brief      尝试向无锁队列入队一组数据
     *
     * @param[in]  * payload[]  入队的内存地址
     * @param[in]  num           请求入队元素数量
     * @param[out] & push_num    实际入队数量
     *
     * @attention  MP入队非阻塞版本
     *
     * @return     成功时返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t TryPush(T* payload[], uint32_t num, uint32_t& push_num)
    {
        return MPSC_TryPush(payload, num, push_num);
    }

    /**
     * @brief      从无锁队列中取出部分
     *
     * @param[out] * payload[]  出队目标内存
     * @param[out] & pop_num    实际出队的数量
     *
     * @attention  * payload[]的目标内存由调用方维护，数组长度不能小于GetEntrySizeLimit;
     *
     * @return     成功返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Pop(T* payload[], uint32_t& pop_num)
    {
        return SC_Pop(payload, pop_num);
    }

    /**
     * @brief      从无锁队列中取出一个指针
     *
     * @param[out] ** payload出队目标指针
     *
     * @attention  取出一个solt内的一个指针后，该slot的内存将失效
     *
     * @return     成功返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Pop(T** payload)
    {
        return SC_Pop(payload);
    }
};

class SPMCPtrQueue : public PtrQueueBase
{
public:
    SPMCPtrQueue() = default;
    ~SPMCPtrQueue() = default;
    static SPMCPtrQueue *Create(const std::string &name, uint32_t queue_size)
    {
        return (SPMCPtrQueue*)PtrQueueBase::Create(name, queue_size);
    }

    /**
     * @brief      向无锁队列中入队一个元素
     *
     * @param[in]  *payload  入队内存地址
     *
     * @return     成功时返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Push(T* payload)
    {   
        return SP_Push(payload);
    }

    /**
     * @brief      向无锁队列中入队一组元素
     *
     * @param[in]  * payload[]  入队的内存地址
     * @param[in]  num          请求入队元素数量
     * @param[out] & push_num    实际入队数量
     *
     * @attention  每次入队个数最大值限制为GetEntrySizeLimit;部分入队成功也算入队成功
     *
     * @return     成功返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Push(T* payload[], uint32_t num, uint32_t& push_num)
    {
        return SP_Push(payload, num, push_num);
    }

    /**
     * @brief      从无锁队列中取出部分
     *
     * @param[out] * payload[]  出队目标内存
     * @param[out] & pop_num    实际出队的数量
     *
     * @attention  * payload[]的目标内存由调用方维护，数组长度不能小于GetEntrySizeLimit;
     *             如果队列为空函数将会被阻塞直到队列有元素为止或者调用set_release_alert函数
     *
     * @return     成功返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Pop(T* payload[], uint32_t& pop_num)
    {
        return SPMC_Pop(payload, pop_num);
    }

    /**
     * @brief      从无锁队列中取出一个指针
     *
     * @param[out] ** payload出队目标指针
     *
     * @attention  取出一个solt内的一个指针后，该slot的内存将失效
     *             如果队列为空函数将会被阻塞直到队列有元素为止或者调用set_release_alert函数
     *
     * @return     成功返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t Pop(T** payload)
    {
        return SPMC_Pop(payload);
    }

    /**
     * @brief      尝试从无锁队列中取出部分
     *
     * @param[out] * payload[]  出队目标内存
     * @param[out] & pop_num    实际出队的数量
     *
     * @attention  int32_t Pop(T* payload[], uint32_t& pop_num)的非阻塞版本
     *
     * @return     成功返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t TryPop(T* payload[], uint32_t& pop_num)
    {
        return SPMC_TryPop(payload, pop_num);
    }

    /**
     * @brief      尝试从无锁队列中取出一个指针
     *
     * @param[out] ** payload出队目标指针
     *
     * @attention  Pop(T** payload)函数的非阻塞版本
     *
     * @return     成功返回ErrorCode::kSuccess，失败返回相应的ErrorCode
     */
    template<typename T>
    inline int32_t TryPop(T** payload)
    {
        return SPMC_TryPop(payload);
    }
};

template<typename QElementType>
struct QueueIterator
{
    typedef QElementType element_type;
    
    QueueIterator& operator++()
    {
        ++index;
        return *this;
    }

    QueueIterator operator++(int)
    {
        QueueIterator local_value = *this;
        ++index;
        return local_value;
    }

    QueueIterator& operator--()
    {
        --index;
        return *this;
    }

    QueueIterator operator--(int)
    {
        QueueIterator local_value = *this;
        --index;
        return local_value;
    }

    operator element_type*()
    {
        struct VariantEntry* entry_ptr = ptr_add(entries, (index & queue_mask) << entry_bits);
        return (element_type*)(entry_ptr->buffer);
    }

    element_type* operator->()
    {
        return (element_type*)(*this);
    }

    element_type& operator*()
    {
        struct VariantEntry* entry_ptr = ptr_add(entries, (index & queue_mask) << entry_bits);
        char* const buffer = entry_ptr->buffer;
        return *(element_type*)(buffer);
    }

    bool operator == (const QueueIterator& iter_input) const
    {
        return (index == iter_input.index);
    }

    bool operator != (const QueueIterator& iter_input) const
    {
        return (index != iter_input.index);
    }

    bool operator < (const QueueIterator& iter_input) const
    {
        return (index < iter_input.index);
    }

    bool operator > (const QueueIterator& iter_input) const
    {
        return (index > iter_input.index);
    }

    int64_t operator - (const QueueIterator& iter_input) const
    {
        return index - iter_input.index;
    }

    QueueIterator& operator += (int32_t adden)
    {
        index += adden;
        return *this;
    }

    uint64_t             index;
    struct VariantEntry* entries;
    uint64_t             queue_mask;
    uint32_t             entry_bits;
};

template<typename QElementType, typename QHeaderType, ADK_TTPARAM(Derived)>
class QueueBase
{
public:
    typedef QueueIterator<QElementType>                   iterator;
    typedef QueueBase<QElementType, QHeaderType, Derived> class_type;
    typedef QElementType                                  element_type;
    typedef Derived<QElementType>                         derived_type;
    typedef QHeaderType                                   queue_header_type;

    ~QueueBase()
    {
        if (NULL != queue_header_)
        {
            if (ADK_UNLIKELY(1 == __sync_fetch_and_sub(&(queue_header_->reference_count), 1)))
            {
                if (std::is_class<element_type>::value)
                {
                    const uint32_t entry_num = queue_header_->queue_size;
                    const uint32_t entry_size = queue_header_->entry_size;
                    struct VariantEntry* current_entry = (struct VariantEntry*)queue_header_->entries();
                    for (uint32_t i = 0; i < entry_num; ++i)
                    {
                        char* const buffer = current_entry->buffer;
                        ((element_type*)(buffer))->~element_type();
                        current_entry = ptr_add(current_entry, entry_size);
                    }
                }
                aligned_free(queue_header_);
            }
        }
    }
    
    /**
     * @brief      将VariantQueueHeader所指向的queue实体挂载到现有的对象上
     *
     * @param[in]  header 无锁队列内存头结构
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    void Init(queue_header_type* header)
    {
        assert(NULL != header);
        queue_header_ = header;

        //add reference count when queue_header_ being copyed;
        __sync_fetch_and_add(&(queue_header_->reference_count), 1);

        entries_ = (struct VariantEntry*)(header->entries());
        queue_size_ = header->queue_size;
        sqn_offset_ = queue_size_ << 1;
        queue_mask_ = header->queue_mask;
        entry_bits_ = GetBits(header->entry_size);
    }

    /**
     * @brief      创建无锁队列并将已有的队列内存挂载到该对象上
     *
     * @param[in]  header 无锁队列内存头结构
     *
     * @return     成功时返回 非空对象指针
     */
    static derived_type *Create(queue_header_type* header)
    {
        derived_type* queue = (derived_type*)aligned_malloc(ADK_CACHE_LINE_SIZE, sizeof(derived_type));
        if (NULL != queue)
        {
            new (queue) derived_type();
            queue->Init(header);
        }
        return queue;
    }

    /**
     * @brief      创建完整的对象结构
     *
     * @param[in]  name 队列名
     * @param[in]  queue_size 制定队列的大小
     *
     * @return     成功时返回 非空对象指针
     */
    static derived_type *Create(const std::string &name, uint32_t queue_size)
    {
        return Create(name, queue_size, sizeof(element_type));
    }

    static derived_type *Create(const std::string &name, uint32_t queue_size, uint32_t element_size)
    {
        derived_type* queue = (derived_type*)aligned_malloc(ADK_CACHE_LINE_SIZE, sizeof(derived_type));
        if (nullptr == queue)
        {
            return nullptr;
        }

        new (queue) derived_type();

        const uint32_t entry_size = VariantEntry::CalcEntrySize(element_size);
        const uint32_t queue_size_create = ADK_ROUND_TO_POWER_OF_2(std::max<uint32_t>(queue_size, 2));
        const uint32_t queue_header_size = uint32_t(ADK_ROUND_UP(sizeof(queue_header_type), ADK_PAGE_SIZE));
        const uint64_t total_size = queue_header_size + queue_size_create * entry_size;
        queue_header_type* queue_header = (queue_header_type*)aligned_malloc(ADK_PAGE_SIZE, (size_t)total_size);
        if (nullptr == queue_header)
        {
            queue->~derived_type();
            aligned_free(queue);
            return nullptr;
        }

        //fill queue header
        NameCopy(queue_header->queue_name, name);
        queue_header->entry_size = entry_size;
        queue_header->queue_mask = queue_size_create - 1;
        queue_header->queue_size = queue_size_create;
        queue_header->queue_entry_offset = queue_header_size;
        queue_header->tail = queue_size_create << 1;
        queue_header->head = queue_size_create << 1;
        queue_header->reference_count = 0;
        InitEntries(queue_header);
        
        queue->Init(queue_header);
        return queue;
    }

    /**
     * @brief      复制一个队列与源队列共实体
     *
     * @param[in]  queue 待拷贝源对象
     *
     * @return     成功时返回 非空对象指针
     */
    static derived_type *Duplicate(const derived_type& queue)
    {
        return Create(queue.queue_header_);
    }

    /**
     * @brief      复制一个队列与源队列共实体
     *
     * @return     成功时返回 非空对象指针
     */
     derived_type *Duplicate()
    {
        return Duplicate(*static_cast<derived_type*>(this));
    }

    /**
     * @brief      删除队列对象
     *
     * @param[in]  queue 队列对象
     *
     * @attention  函数只删除对象本身内部内存，实际队列内存不影响
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    static int32_t Delete(derived_type* queue)
    {
        assert(NULL != queue);
        queue->~derived_type();
        aligned_free(queue);
        return kSuccess;
    }

    /**
     * @brief      获取队列实际slot个数
     *
     * @return     返回队列slot个数
     */
    inline uint32_t queue_size() const
    {
        return queue_size_;
    }

    /**
     * @brief      队列的容量，即队列长度的上限
     *
     * @return     队列容量
     */
    inline uint64_t capacity() const
    {
        return queue_size_;
    }

    /**
     * @brief      释放所有出队和入队被阻塞住的线程
     *
     * @attention  该函数的调用后该队列将不能再正常使用
     */
    void set_release_alert()
    {
        release_alert_ = true;
    }

    bool release_alert() const
    {
        return ACCESS_ONCE(release_alert_);
    }

    /**
     * @brief      当前的队列长度，即队列中携带队列元素的Entry数量
     *
     * @return     队列长度
     */
    template<bool kDoFence = false>
    inline uint64_t length() const
    {
        const uint64_t head = queue_header_->head;
        if (!is_thread_local<queue_header_type>::value)
        {
            if (kDoFence)
            {
                ADK_MB();
            }
            else
            {
                ADK_BARRIER();
            }
        }
        const uint64_t tail = queue_header_->tail;
        const uint64_t queue_length = tail - head;
        return queue_length;
        //return (queue_length < queue_size_) ? queue_length : queue_size_;
    }

    /**
     * @brief      向队列中插入一个元素
     *
     * @param[in]  payload将要插入的元素
     *
     * @note       MPQueue如果队列是满的，该函数将被阻塞直到入队成功为止或者调用set_release_alert函数
     *
     * @return     成功时返回kSuccess, 失败返回相应的ErrorCode
     */
    inline int32_t Push(const element_type& payload)
    {
        struct VariantEntry* entry;
        ADK_CHECK_RET_SUCCESS(AllocEntry(&entry));
        char* const buffer = entry->buffer;
        *(element_type*)(buffer) = payload;
        PostEntry(entry);
        return ErrorCode::kSuccess;
    }

    inline int32_t Push(element_type&& payload)
    {
        struct VariantEntry* entry;
        ADK_CHECK_RET_SUCCESS(AllocEntry(&entry));
        char* const buffer = entry->buffer;
        *(element_type*)(buffer) = std::forward<element_type>(payload);
        PostEntry(entry);
        return ErrorCode::kSuccess;
    }

    /**
     * @brief      尝试向队列中插入一个元素
     *
     * @param[in]  payload将要插入的元素
     *
     * @note       int32_t Push(const element_type& payload)的非阻塞版本
     *
     * @return     成功时返回kSuccess, 失败返回相应的ErrorCode
     */
    inline int32_t TryPush(const element_type& payload)
    {
        struct VariantEntry* entry;
        ADK_CHECK_RET_SUCCESS(TryAllocEntry(&entry));
        char* const buffer = entry->buffer;
        *(element_type*)(buffer) = payload;
        PostEntry(entry);
        return ErrorCode::kSuccess;
    }

    inline int32_t TryPush(element_type&& payload)
    {
        struct VariantEntry* entry;
        ADK_CHECK_RET_SUCCESS(TryAllocEntry(&entry));
        char* const buffer = entry->buffer;
        *(element_type*)(buffer) = std::forward<element_type>(payload);
        PostEntry(entry);
        return ErrorCode::kSuccess;
    }

    /**
     * @brief      向队列中插入多个元素
     *
     * @param[in]  payload[]要插入的元素
     *
     * @param[in]  num入队元素的个数
     *
     * @note       MPQueue 如果队列是满的，该函数将被阻塞直到入队成功为止或者调用set_release_alert函数
     *
     * @return     成功时返回kSuccess, 失败返回相应的ErrorCode
     */
    inline int32_t Push(const element_type payload[], uint32_t num)
    {
        struct VariantEntry* entry_array[num];
        ADK_CHECK_RET_SUCCESS(AllocEntry(entry_array, num));

        for (uint32_t index=0; index<num; ++index)
        {
            char* const buffer = entry_array[index]->buffer;
            *(element_type*)(buffer) = payload[index];
            PostEntry(entry_array[index]);
        }
        return kSuccess;
    }

    /**
     * @brief      尝试向队列中插入多个元素
     *
     * @param[in]  payload[]要插入的元素
     *
     * @param[in]  num入队元素的个数
     *
     * @note       int32_t Push(const element_type payload[], uint32_t num)队列的非阻塞版本
     *
     * @return     成功时返回kSuccess, 失败返回相应的ErrorCode
     */
    inline int32_t TryPush(const element_type payload[], uint32_t num)
    {
        struct VariantEntry* entry_array[num];
        ADK_CHECK_RET_SUCCESS(TryAllocEntry(entry_array, num));

        for (uint32_t index=0; index<num; ++index)
        {
            char* const buffer = entry_array[index]->buffer;
            *(element_type*)(buffer) = payload[index];
            PostEntry(entry_array[index]);
        }
        return kSuccess;
    }

    /**
     * @brief      从队列中取出一个元素
     *
     * @param[out] & payload取出的元素
     *
     * @note       如果MCQueue出队时，如果队列是空函数将被阻塞，直到出队成功为止或者调用set_release_alert函数
     *
     * @return     成功时返回kSuccess, 失败返回相应的ErrorCode
     */
    inline int32_t Pop(element_type& payload)
    {
        struct VariantEntry* entry;
        ADK_CHECK_RET_SUCCESS(WaitEntry(&entry));
        char* const buffer = entry->buffer;
        payload = std::move(*(element_type*)(buffer));
        FreeEntry(entry);
        return ErrorCode::kSuccess;
    }

    /**
     * @brief       尝试从队列中取出一个元素
     *
     * @param[out]  &payload取出的元素
     *
     * @note        int32_t Pop(element_type& payload)的非阻塞版本
     *
     * @return      成功时返回kSuccess, 失败返回相应的ErrorCode
     */
    inline int32_t TryPop(element_type& payload)
    {
        struct VariantEntry* entry;
        ADK_CHECK_RET_SUCCESS(TryWaitEntry(&entry));
        char* const buffer = entry->buffer;
        payload = std::move(*(element_type*)(buffer));
        FreeEntry(entry);
        return ErrorCode::kSuccess;
    }

    inline element_type* UnsafeAbsAt(uint64_t index)
    {
        struct VariantEntry* entry_ptr = ptr_add(entries_, GetInQueueIndex(index) << entry_bits_);
        char* const buffer = entry_ptr->buffer;
        return (element_type*)buffer;
    }

    inline element_type* UnsafeAt(uint64_t index)
    {
        return UnsafeAbsAt(queue_header_->head + index);
    }

    inline element_type* UnsafeSqnAt(uint64_t sqn_index)
    {
        return UnsafeAbsAt(sqn_index + sqn_offset_);
    }

    inline element_type* ElementAbsAt(uint64_t index)
    {
        struct VariantEntry* entry_ptr = ptr_add(entries_, GetInQueueIndex(index) << entry_bits_);
        if (ADK_UNLIKELY(ACCESS_ONCE(entry_ptr->pos) < 0))
        {
            return nullptr;
        }

        // LOAD other member variable of entry_ptr may be reordered before check entry_ptr->pos in arm
        ADK_BARRIER();
        char* const buffer = entry_ptr->buffer;
        return ((element_type*)buffer);
    }

    inline element_type* ElementAt(uint64_t index)
    {
        return ElementAbsAt(queue_header_->head + index);
    }

    inline element_type* ElementSqnAt(uint64_t sqn_index)
    {
        return ElementAbsAt(sqn_index + sqn_offset_);
    }

    /**
     * @brief       从队列连续丢弃元素
     *
     * @param[in]   size 丢弃元素的个数
     *
     * @return      成功时返回kSuccess, 失败返回相应的ErrorCode
     */
    inline int32_t Drop(uint32_t size)
    {
        struct VariantEntry* entry;
        for (uint32_t index=0; index < size; ++index)
        {
            ADK_CHECK_RET_SUCCESS(WaitEntry(&entry));
            FreeEntry(entry);
        }
        return kSuccess;
    }

    /**
     * @brief       从队列连续丢弃元素
     *
     * @param[in]   &iter 丢弃到此迭代器
     *
     * @return      成功时返回kSuccess, 失败返回相应的ErrorCode
     */
    inline int32_t Drop(const iterator& iter)
    {
        uint64_t monotonic_index = iter.index + 1;
        if ((monotonic_index <= queue_header_->head) || (monotonic_index > queue_header_->tail))
        {
            return kOutOfRange;
        }

        for (uint64_t index=queue_header_->head; index < monotonic_index; ++index)
        {
            struct VariantEntry* const entry_ptr = ptr_add(entries_, GetInQueueIndex(index) << entry_bits_);
            entry_ptr->pos = NEGATIVE_NUM_INT64(entry_ptr->pos);
        }
        queue_header_->head = monotonic_index;
        return kSuccess;
    }

    /**
     * @brief       从队列连续丢弃元素
     *
     * @param[in]   size丢弃元素的数量
     *
     * @note        不做任何安全检查且接口非线程安全
     */
    inline void UnsafeDrop(uint32_t size)
    {
        assert(size > 0);
        const uint64_t loop_end = queue_header_->head + size;

        do
        {
            struct VariantEntry* const entry_ptr = ptr_add(entries_, GetInQueueIndex(queue_header_->head) << entry_bits_);
            entry_ptr->pos = NEGATIVE_NUM_INT64(entry_ptr->pos);

            ADK_BARRIER();
            ++queue_header_->head;
        } while (queue_header_->head < loop_end);
    }

    inline void UnsafeDrop()
    {
        struct VariantEntry* const entry_ptr = ptr_add(entries_, GetInQueueIndex(queue_header_->head) << entry_bits_);
        entry_ptr->pos = NEGATIVE_NUM_INT64(entry_ptr->pos);

        ADK_BARRIER();
        ++queue_header_->head;
    }

    uint64_t last_pop_sqn() const
    {
        return Head() - sqn_offset_;
    }

    uint64_t last_push_sqn() const
    {
        return Tail() - sqn_offset_;
    }

    uint64_t Head() const
    {
        return queue_header_->head;
    }

    uint64_t Tail() const
    {
        return queue_header_->tail;
    }

    iterator Begin() const
    {
        return { queue_header_->head, entries_, queue_mask_, entry_bits_ };
    }

    iterator End() const
    {
        return { queue_header_->tail, entries_, queue_mask_, entry_bits_ };
    }

    void UnsafeRecoveryBack(uint64_t record_queue)
    {
        assert(record_queue < queue_header_->head);

        const uint64_t new_tail = record_queue + queue_header_->queue_size;
        for (uint64_t index = queue_header_->tail; index <= new_tail; ++index)
        {
            struct VariantEntry* const entry_ptr
                = (struct VariantEntry *)ptr_add(entries_, GetInQueueIndex(index) << entry_bits_);
            assert(entry_ptr->pos < 0);

            if (!is_thread_local<queue_header_type>::value)
            {
                ADK_BARRIER();
            }

            entry_ptr->pos = queue_size_ - entry_ptr->pos;
            ++queue_header_->tail;
        }
    }

protected:
    //QueueBase not supposed to have object
    QueueBase()
        :   queue_header_(NULL),
            entries_(NULL),
            queue_mask_(0),
            queue_size_(0),
            sqn_offset_(0),
            entry_bits_(0),
            release_alert_(false)
    {
    }

    inline int32_t SP_AllocEntry(struct VariantEntry** entry_pptr)
    {
        struct VariantEntry* entry_ptr = ptr_add(entries_, GetInQueueIndex(queue_header_->tail) << entry_bits_);
        if (ADK_UNLIKELY(ACCESS_ONCE(entry_ptr->pos) > 0))
        {
            return kQueueFull;
        }

        *entry_pptr = entry_ptr;
        return kSuccess;
    }

    inline int32_t SP_AllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        uint64_t monotonic_index = queue_header_->tail;
        uint64_t end_index = monotonic_index + num;

        struct VariantEntry*  entry_ptr;
        struct VariantEntry** entry_pptr = (struct VariantEntry**)entry_array;
        for (uint64_t index = monotonic_index; index < end_index; ++index)
        {
            entry_ptr = (struct VariantEntry *)ptr_add(entries_, GetInQueueIndex(index) << entry_bits_);
            if (ADK_UNLIKELY(ACCESS_ONCE(entry_ptr->pos) > 0))
            {
                return kQueueFull;
            }

            ADK_BARRIER();
            *(entry_pptr++) = entry_ptr;
        }

        return kSuccess;
    }

    inline int32_t SC_WaitEntry(struct VariantEntry** entry_pptr)
    {
        struct VariantEntry* entry_ptr = ptr_add(entries_, GetInQueueIndex(queue_header_->head) << entry_bits_);
        if (ADK_UNLIKELY(ACCESS_ONCE(entry_ptr->pos) < 0))
        {
            return kQueueEmpty;
        }

        *entry_pptr = entry_ptr;
        return kSuccess;
    }

    inline int32_t MP_AllocEntry(struct VariantEntry** entry_pptr)
    {
        uint64_t monotonic_index = __sync_fetch_and_add(&(queue_header_->tail), 1);
        struct VariantEntry* entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_index) << entry_bits_);

        DEFINE_BACKOFF_COUNTER();
        while (ADK_UNLIKELY(monotonic_index + ACCESS_ONCE(entry_ptr->pos) != queue_size_))
        {
            if (ACCESS_ONCE(release_alert_))
            {
                return kWouldblock;
            }
            PAUSE_BACKOFF();
        }

        *entry_pptr = entry_ptr;
        return kSuccess;
    }

    inline int32_t MP_TryAllocEntry(struct VariantEntry** entry_pptr)
    {
#if 0
        uint64_t monotonic_index = __sync_fetch_and_add(&(queue_header_->tail), 1);
        struct VariantEntry* entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_index) << entry_bits_);

        DEFINE_BACKOFF_COUNTER();
        while (ADK_UNLIKELY(monotonic_index + ACCESS_ONCE(entry_ptr->pos) != queue_size_))
        {
            const auto expected_tail = monotonic_index + 1;
            if (ACCESS_ONCE(queue_header_->tail) == expected_tail)
            {
                if (__sync_bool_compare_and_swap(&(queue_header_->tail), expected_tail, monotonic_index))
                {
                    return kQueueFull;
                }
            }

            if (ACCESS_ONCE(release_alert_))
            {
                return kWouldblock;
            }
            PAUSE_BACKOFF();
        }
#else
        uint64_t monotonic_index;
        struct VariantEntry* entry_ptr;

        do 
        {
        reload_tail:
            monotonic_index = ACCESS_ONCE(queue_header_->tail);
            entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_index) << entry_bits_);
            if (ADK_UNLIKELY(monotonic_index + entry_ptr->pos != queue_size_))
            {
                if (monotonic_index == ACCESS_ONCE(queue_header_->tail))
                {
                    return kQueueFull;
                }
                goto reload_tail;
            }
        } while (!__sync_bool_compare_and_swap(&(queue_header_->tail), monotonic_index, monotonic_index + 1));
#endif
        *entry_pptr = entry_ptr;
        return kSuccess;
    }

    inline int32_t MP_AllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        uint64_t monotonic_index = __sync_fetch_and_add(&(queue_header_->tail), num);
        uint64_t alloc_end_index = monotonic_index + num;
        
        struct VariantEntry* entry_ptr;
        struct VariantEntry** entry_pptr = (struct VariantEntry**)entry_array;
        for (uint64_t index = monotonic_index; index < alloc_end_index; ++index)
        {
            DEFINE_BACKOFF_COUNTER();
            entry_ptr = ptr_add(entries_, GetInQueueIndex(index) << entry_bits_);
            while (ADK_UNLIKELY(index + ACCESS_ONCE(entry_ptr->pos) != queue_size_))
            {
                if (ACCESS_ONCE(release_alert_))
                {
                    return kWouldblock;
                }
                
                PAUSE_BACKOFF();
            }

            ADK_BARRIER();
            *(entry_pptr++) = entry_ptr;
        }
        return kSuccess;
    }

    inline int32_t MP_TryAllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        uint64_t monotonic_index;
        uint64_t alloc_end_index;
        struct VariantEntry* entry_ptr;
        struct VariantEntry** entry_pptr;
        do
        {
            monotonic_index = ACCESS_ONCE(queue_header_->tail);
            alloc_end_index = monotonic_index + num;
            entry_pptr = (struct VariantEntry**)entry_array;
            for (uint64_t index = monotonic_index; index < alloc_end_index; ++index)
            {
                entry_ptr = ptr_add(entries_, GetInQueueIndex(index) << entry_bits_);
                if (ADK_UNLIKELY(index + entry_ptr->pos != queue_size_))
                {
                    if (monotonic_index == ACCESS_ONCE(queue_header_->tail))
                    {
                        return kQueueFull;
                    }
 
                    // retry from the begin
                    break;
                }
                *(entry_pptr++) = entry_ptr;
            }
        } while (!__sync_bool_compare_and_swap(&(queue_header_->tail), monotonic_index, alloc_end_index));
        return kSuccess;
    }

    inline int32_t MC_WaitEntry(struct VariantEntry** entry_pptr)
    {
        uint64_t monotonic_index = __sync_fetch_and_add(&(queue_header_->head), 1);
        struct VariantEntry* entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_index) << entry_bits_);

        DEFINE_BACKOFF_COUNTER();
        while (ADK_UNLIKELY((int64_t)monotonic_index != ACCESS_ONCE(entry_ptr->pos)))
        {
            if (ACCESS_ONCE(release_alert_))
            {
                return kWouldblock;
            }
            PAUSE_BACKOFF();
        }

        *entry_pptr = entry_ptr;
        return kSuccess;
    }

    inline int32_t MC_TryWaitEntry(struct VariantEntry** entry_pptr)
    {
#if 0
        uint64_t monotonic_index = __sync_fetch_and_add(&(queue_header_->head), 1);
        struct VariantEntry* entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_index) << entry_bits_);

        DEFINE_BACKOFF_COUNTER();
        while (ADK_UNLIKELY((int64_t)monotonic_index != ACCESS_ONCE(entry_ptr->pos)))
        {
            const auto expected_head = monotonic_index + 1;
            if (ACCESS_ONCE(queue_header_->head) == expected_head)
            {
                if (__sync_bool_compare_and_swap(&(queue_header_->head), expected_head, monotonic_index))
                {
                    return kQueueEmpty;
                }
            }

            if (ACCESS_ONCE(release_alert_))
            {
                return kWouldblock;
            }
            PAUSE_BACKOFF();
        }
#else
        struct VariantEntry* entry_ptr;
        uint64_t monotonic_index;

        do
        {
        reload_head:
            monotonic_index = ACCESS_ONCE(queue_header_->head);
            entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_index) << entry_bits_);
            if (ADK_UNLIKELY((int64_t)monotonic_index != entry_ptr->pos))
            {
                if (monotonic_index == ACCESS_ONCE(queue_header_->head))
                {
                    return kQueueEmpty;
                }
                goto reload_head;
            }
        }while (!__sync_bool_compare_and_swap(&(queue_header_->head), monotonic_index, monotonic_index + 1));

#endif
        *entry_pptr = entry_ptr;
        return kSuccess;
    }

    inline void SP_PostEntry(struct VariantEntry* entry_ptr)
    {
        if (!is_thread_local<queue_header_type>::value)
        {
            ADK_BARRIER();
        }

        entry_ptr->pos = queue_size_ - entry_ptr->pos;
        ++queue_header_->tail;
    }

    inline void MP_PostEntry(struct VariantEntry* entry_ptr)
    {
        ADK_BARRIER();
        entry_ptr->pos = queue_size_ - entry_ptr->pos;
    }

    inline void SC_FreeEntry(struct VariantEntry* entry_ptr)
    {
        if (!is_thread_local<queue_header_type>::value)
        {
            ADK_BARRIER();
        }

        entry_ptr->pos = NEGATIVE_NUM_INT64(entry_ptr->pos);
        ++queue_header_->head;
    }

    inline void MC_FreeEntry(struct VariantEntry* entry_ptr)
    {
        ADK_BARRIER();
        entry_ptr->pos = NEGATIVE_NUM_INT64(entry_ptr->pos);
    }

    inline void Clear(boost::function<void(element_type*)>& destructor)
    {
        while (queue_header_->head < queue_header_->tail)
        {
            struct VariantEntry* entry_ptr = ptr_add(entries_, GetInQueueIndex(queue_header_->head) << entry_bits_);
            char* const buffer = entry_ptr->buffer;
            destructor((element_type*)buffer);
            entry_ptr->pos = NEGATIVE_NUM_INT64(entry_ptr->pos);

            ADK_BARRIER();
            ++queue_header_->head;
        }
    }

    inline void Clear()
    {
        while (queue_header_->head < queue_header_->tail)
        {
            struct VariantEntry* entry_ptr = ptr_add(entries_, GetInQueueIndex(queue_header_->head) << entry_bits_);
            entry_ptr->pos = NEGATIVE_NUM_INT64(entry_ptr->pos);

            ADK_BARRIER();
            ++queue_header_->head;
        }
    }

private:
    /**
     * @brief      初始化VariantQueueHeader指向的内存结构
     *
     * @param      header 无锁队列内存头结构
     */
    static void InitEntries(queue_header_type* header)
    {
        assert(NULL != header);
        const uint32_t entry_num = header->queue_size;
        const uint32_t entry_size = header->entry_size;
        struct VariantEntry* current_entry = (struct VariantEntry*)header->entries();
        for (uint32_t i = 0; i < entry_num; ++i)
        {
            current_entry->pos = NEGATIVE_NUM_INT64(int64_t(entry_num + i));
            if (std::is_class<element_type>::value)
            {
                new (current_entry->buffer) element_type();
            }
            current_entry = ptr_add(current_entry, entry_size);
        }
    }

    inline uint64_t GetInQueueIndex(uint64_t index)
    {
        return index & queue_mask_;
    }

    inline int32_t AllocEntry(struct VariantEntry** entry_pptr)
    {
        return static_cast<derived_type*>(this)->AllocEntry(entry_pptr);
    }

    inline int32_t TryAllocEntry(struct VariantEntry** entry_pptr)
    {
        return static_cast<derived_type*>(this)->TryAllocEntry(entry_pptr);
    }

    inline int32_t AllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return static_cast<derived_type*>(this)->AllocEntry(entry_array, num);
    }
    
    inline int32_t TryAllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return static_cast<derived_type*>(this)->TryAllocEntry(entry_array, num);
    }

    inline void PostEntry(struct VariantEntry* entry)
    {
        static_cast<derived_type*>(this)->PostEntry(entry);
    }

    inline int32_t WaitEntry(struct VariantEntry** entry_pptr)
    {
        return static_cast<derived_type*>(this)->WaitEntry(entry_pptr);
    }

    inline int32_t TryWaitEntry(struct VariantEntry** entry_pptr)
    {
        return static_cast<derived_type*>(this)->TryWaitEntry(entry_pptr);
    }

    inline void FreeEntry(struct VariantEntry* entry)
    {
        static_cast<derived_type*>(this)->FreeEntry(entry);
    }

    queue_header_type*   queue_header_;
    struct VariantEntry* entries_;
    uint64_t             queue_mask_;
    uint64_t             queue_size_;
    uint64_t             sqn_offset_;
    //设定entry_size为2的N次方
    uint32_t             entry_bits_;
    bool                 release_alert_;
};

template<typename QElementType>
class SPSCQueue : public QueueBase<QElementType, VariantQueueHeader, SPSCQueue>
{
public:
    typedef QElementType element_type;
    typedef QueueBase<QElementType, VariantQueueHeader, SPSCQueue> base_class;

    static constexpr bool kSingleProducer = true;
    static constexpr bool kSingleConsumer = true;

    inline int32_t AllocEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::SP_AllocEntry(entry_pptr);
    }

    inline int32_t TryAllocEntry(struct VariantEntry** entry_pptr)
    {
        return AllocEntry(entry_pptr);
    }

    inline int32_t AllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return base_class::SP_AllocEntry(entry_array, num);
    }

    inline int32_t TryAllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return AllocEntry(entry_array, num);
    }

    inline void PostEntry(struct VariantEntry* entry)
    {
        base_class::SP_PostEntry(entry);
    }

    inline int32_t WaitEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::SC_WaitEntry(entry_pptr);
    }

    inline int32_t TryWaitEntry(struct VariantEntry** entry_pptr)
    {
        return WaitEntry(entry_pptr);
    }

    inline void FreeEntry(struct VariantEntry* entry)
    {
        base_class::SC_FreeEntry(entry);
    }

    inline void Clear(boost::function<void(element_type*)>& destructor)
    {
        base_class::Clear(destructor);
    }

    inline void Clear()
    {
        base_class::Clear();
    }
};

template<typename QElementType>
class ThreadLocalQueue : public QueueBase<QElementType, ThreadLocalQueueHeader, ThreadLocalQueue>
{
public:
    typedef QElementType element_type;
    typedef QueueBase<QElementType, ThreadLocalQueueHeader, ThreadLocalQueue> base_class;

    static constexpr bool kSingleProducer = true;
    static constexpr bool kSingleConsumer = true;

    inline int32_t AllocEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::SP_AllocEntry(entry_pptr);
    }

    inline int32_t TryAllocEntry(struct VariantEntry** entry_pptr)
    {
        return AllocEntry(entry_pptr);
    }

    inline int32_t AllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return base_class::SP_AllocEntry(entry_array, num);
    }

    inline int32_t TryAllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return AllocEntry(entry_array, num);
    }

    inline void PostEntry(struct VariantEntry* entry)
    {
        base_class::SP_PostEntry(entry);
    }

    inline int32_t WaitEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::SC_WaitEntry(entry_pptr);
    }

    inline int32_t TryWaitEntry(struct VariantEntry** entry_pptr)
    {
        return WaitEntry(entry_pptr);
    }

    inline void FreeEntry(struct VariantEntry* entry)
    {
        base_class::SC_FreeEntry(entry);
    }

    inline void Clear(boost::function<void(element_type*)>& destructor)
    {
        base_class::Clear(destructor);
    }

    inline void Clear()
    {
        base_class::Clear();
    }
};

template<typename QElementType>
class MPSCQueue : public QueueBase<QElementType, VariantQueueHeader, MPSCQueue>
{
public:
    typedef QElementType element_type;
    typedef QueueBase<QElementType, VariantQueueHeader, MPSCQueue> base_class;

    static constexpr bool kSingleProducer = false;
    static constexpr bool kSingleConsumer = true;

    inline int32_t AllocEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::MP_AllocEntry(entry_pptr);
    }

    inline int32_t TryAllocEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::MP_TryAllocEntry(entry_pptr);
    }

    inline int32_t AllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return base_class::MP_AllocEntry(entry_array, num);
    }

    inline int32_t TryAllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return base_class::MP_TryAllocEntry(entry_array, num);
    }

    inline void PostEntry(struct VariantEntry* entry)
    {
        base_class::MP_PostEntry(entry);
    }

    inline int32_t WaitEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::SC_WaitEntry(entry_pptr);
    }

    inline int32_t TryWaitEntry(struct VariantEntry** entry_pptr)
    {
        return WaitEntry(entry_pptr);
    }

    inline void FreeEntry(struct VariantEntry* entry)
    {
        base_class::SC_FreeEntry(entry);
    }
};

template<typename QElementType>
class SPMCQueue : public QueueBase<QElementType, VariantQueueHeader, SPMCQueue>
{
public:
    typedef QElementType element_type;
    typedef QueueBase<QElementType, VariantQueueHeader, SPMCQueue> base_class;

    static constexpr bool kSingleProducer = true;
    static constexpr bool kSingleConsumer = false;

    inline int32_t AllocEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::SP_AllocEntry(entry_pptr);
    }

    inline int32_t TryAllocEntry(struct VariantEntry** entry_pptr)
    {
        return AllocEntry(entry_pptr);
    }

    inline int32_t AllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return base_class::SP_AllocEntry(entry_array, num);
    }

    inline int32_t TryAllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return AllocEntry(entry_array, num);
    }

    inline void PostEntry(struct VariantEntry* entry)
    {
        base_class::SP_PostEntry(entry);
    }

    inline int32_t WaitEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::MC_WaitEntry(entry_pptr);
    }

    inline int32_t TryWaitEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::MC_TryWaitEntry(entry_pptr);
    }

    inline void FreeEntry(struct VariantEntry* entry)
    {
        base_class::MC_FreeEntry(entry);
    }
};

template<typename QElementType>
class MPMCQueue : public QueueBase<QElementType, VariantQueueHeader, MPMCQueue>
{
public:
    typedef QElementType element_type;
    typedef QueueBase<QElementType, VariantQueueHeader, MPMCQueue> base_class;

    static constexpr bool kSingleProducer = false;
    static constexpr bool kSingleConsumer = false;

    inline int32_t AllocEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::MP_AllocEntry(entry_pptr);
    }

    inline int32_t TryAllocEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::MP_TryAllocEntry(entry_pptr);
    }

    inline int32_t AllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return base_class::MP_AllocEntry(entry_array, num);
    }

    inline int32_t TryAllocEntry(struct VariantEntry* entry_array[], uint32_t num)
    {
        return base_class::MP_TryAllocEntry(entry_array, num);
    }

    inline void PostEntry(struct VariantEntry* entry)
    {
        base_class::MP_PostEntry(entry);
    }

    inline int32_t WaitEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::MC_WaitEntry(entry_pptr);
    }

    inline int32_t TryWaitEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::MC_TryWaitEntry(entry_pptr);
    }

    inline void FreeEntry(struct VariantEntry* entry)
    {
        base_class::MC_FreeEntry(entry);
    }
};

}
}


#endif