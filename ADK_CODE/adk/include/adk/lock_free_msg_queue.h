/**
 * @file
 * @brief      SPSC,MPSC,SPMC,CCQ无锁队列实现
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017/01/20
 */
#ifndef ADK_IMPL_LOCK_FREE_MSG_QUEUE_H_
#define ADK_IMPL_LOCK_FREE_MSG_QUEUE_H_

#include "util.h"
#include "shm_ptr.h"
#include "constant.h"
#include "error_code.h"
#include "arch/generic.h"

#include <stdio.h>
#include <assert.h>
#include <malloc.h>
#include <vector>
#include <type_traits>

#include <map>
#include <string>

#include <boost/serialization/static_warning.hpp>

#define ADK_LFMQ_AVG_BUF_SIZE 8192
#define ADK_LFMQ_AVG_BUF_MASK (ADK_LFMQ_AVG_BUF_SIZE - 1)

#define ADK_LFMQ_AVG_LAT_BUF_SIZE (8192 * 64)
#define ADK_LFMQ_AVG_LAT_BUF_MASK (ADK_LFMQ_AVG_LAT_BUF_SIZE - 1)

///< 创建队列时额外的标志
enum QueueFlags
{
    // note: for compatible reason we do not change the name
    ADK_LFMQ_FLAG_CREATE_AVG_BUF = 0x0001,      /**< 指示MQ分配用于统计队列长度的缓存       */
    ADK_LFMQ_FLAG_CREATE_AVG_LAT_BUF = 0x0002,  /**< 指示MQ分配用于统计排队延迟的缓存       */
};

// 以下接口内部使用，请勿修改
#define ADK_MQ_TSC_PRECISION    (5)                         // 32 ticks
#define ADK_MQ_LAT_SHIFT        (32)
#define ADK_MQ_LAT_BITS         (31)
#define ADK_MQ_OFV_MASK         ((1ULL<<ADK_MQ_LAT_BITS)-1)
#define ADK_MQ_TSC_MASK         (ADK_MQ_OFV_MASK << ADK_MQ_LAT_SHIFT)
#define ADK_MQ_POS_MASK         (~(ADK_MQ_TSC_MASK))

namespace adk_impl
{

static thread_local uint64_t g_pipeline_total_order_seq_num = 0;

using std::string;

struct Entry
{
    uint64_t    pos;
    char        buffer[];

    /**
     * @brief      计算该entry的排队延迟
     *
     * @return     返回该entry的排队延迟，单位为时钟滴答数(tick)
     */
    uint64_t CalcLatency()
    {
        uint64_t current = GetTSCLowerBits<ADK_MQ_TSC_PRECISION>();
        uint64_t prev = ((pos & ADK_MQ_TSC_MASK) >> ADK_MQ_LAT_SHIFT);
        return ((current - prev) & (ADK_MQ_OFV_MASK >> ADK_MQ_TSC_PRECISION));
    }

    static uint32_t CalcEntrySize(uint32_t payload_size)
    {
        uint32_t entry_size = ADK_ROUND_TO_POWER_OF_2(payload_size + sizeof(struct Entry));
        return entry_size;
        // return entry_size > ADK_CACHE_LINE_SIZE ? entry_size : ADK_CACHE_LINE_SIZE;
        // return ADK_ROUND_TO_POWER_OF_2(payload_size + sizeof(struct Entry));             // FIXME : should we do this?
        // return ADK_ROUND_UP(payload_size + sizeof(struct Entry), ADK_CACHE_LINE_SIZE);   // FIXME : should we do this?
    }
};

struct QueueMemoryHeader
{
    char        queue_name[ADK_MAX_NAME_LEN];
    uint32_t    entry_size;
    uint32_t    queue_mask;
    uint32_t    queue_size;
    uint64_t    queue_entry_offset;             // offset from this header
    
    alignas(ADK_CACHE_LINE_SIZE) uint64_t    reserve;
    uint64_t    nr_forward_fail;
    uint64_t    saved_nr_forward_fail;
    uint64_t    tail; 
    ADK_EMPTY_CACHE_LINE;
    
    alignas(ADK_CACHE_LINE_SIZE) uint64_t    head;
    uint32_t    max_queue_length;
    uint32_t    avg_buf_index;
    uint32_t    avg_buf_index_save;
    uint64_t    release;
    ADK_EMPTY_CACHE_LINE;

    struct Entry* entries() 
    { 
        return (struct Entry*)(ptr_add(this, queue_entry_offset)); 
    }
};

extern thread_local uint64_t g_enqueue_sqn;
extern thread_local Entry*   g_head_entry;

struct QueueStats
{
    uint64_t nr_forward_fail;
    uint32_t max_queue_length;
};

struct QueueStatsExp
{
    uint64_t nr_forward_fail;
    uint32_t max_queue_length;
    int32_t avg_queue_length;
};

/**
 * @brief      用于获取队列排队延迟相关的统计指标
 */
struct QueueLatStats
{
    uint64_t avg;       /**< 均值 */
    uint64_t max;       /**< 最大值 */
    uint64_t min;       /**< 最小值 */
    uint64_t errors;    /**< 统计错误 */
};

/**
 * @brief      多生产者单消费者无锁队列，该无锁队列支持多个生产者同时将消息入队
 *             但仅支持一个消费者同时从队列出队。
 * @attention  无锁队列的Push,Pop,AllocEntry,WaitEntry均为非阻塞接口
 */
class MPSCQueue
{
public:
    constexpr static bool peek = true;

    MPSCQueue();

    ~MPSCQueue();

    /**
     * @brief      使用QueueMemoryHeader对象初始化无锁队列
     *
     * @param      header  无锁队列内存头结构
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t Init(struct QueueMemoryHeader* header);

    /**
     * @brief      向无锁队列中入队一个元素
     *
     * @param      payload  入队元素的引用
     *
     * @tparam     T        入队元素类型
     *
     * @attention  Push接口的模板参数应与Pop接口保持一致
     *
     * @return     成功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kQueueFull
     */
    template<typename T>
    inline int32_t Push(const T& payload)
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif

        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = AllocEntry(&entry);
        if (ret != kSuccess)
            return ret;

        // g_enqueue_sqn = entry->pos;

        char* buf = entry->buffer;
        *(T*)(buf) = payload;
        return PostEntry(entry);
    }

    /**
     * @brief      入队消息，同时记录入队时间
     *
     * @param[in]  payload  入队的消息对象
     *
     * @tparam     T        消息类型
     *
     * @return     成功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kQueueFull
     */
    template<typename T>
    inline int32_t PushTsc(const T& payload)
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif

        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = AllocEntryTsc(&entry);
        if (ret != kSuccess)
            return ret;

        char* buf = entry->buffer;
        *(T*)(buf) = payload;
        return PostEntryTsc(entry);
    }

    template<typename T>
    inline int32_t PushExp(const T& payload)
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif

        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        uint64_t index = AllocEntryExp(&entry);
        if (ADK_UNLIKELY(index == 0))
            return kQueueFull;

        // g_enqueue_sqn = entry->pos;

        char* buf = entry->buffer;
        *(T*)(buf) = payload;
        return PostEntryExp(index);
    }

    template<typename T, typename CB>
    inline int32_t Push(const T& payload, const CB& cb)
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif

        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = AllocEntry(&entry);
        if (ADK_UNLIKELY(ret != kSuccess))
            return ret;

        // g_enqueue_sqn = entry->pos;

        char* buf = entry->buffer;
        *(T*)(buf) = payload;
        ret = PostEntry(entry);
        cb();
        return ret;
    }

    /**
     * @brief      向无锁队列中入队一个元素
     * 使用入参直接在列表的entry上构造出入队元素，避免一次拷贝
     *
     * @tparam     T        入队元素类型
     *
     * @tparam     Args     用于构造入队元素的构造函数参数类型列表
     *
     * @param args 用于构造入队元素的构造函数参数列表
     *
     * @return     成功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kQueueFull
     */
    template<typename T, typename... Args>
    int32_t EmplacePush(Args&&... args)
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif


        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = AllocEntry(&entry);
        if (ret != kSuccess)
            return ret;

        // g_enqueue_sqn = entry->pos;

        new (entry->buffer) T(std::forward<Args>(args)...);
        return PostEntry(entry);
    }
    
    /**
     * @brief      从无锁队列中出队一个元素
     *
     * @param      payload  出队元素引用
     *
     * @tparam     T        出对元素类型
     *
     * @attention  Pop接口的模板参数应与Push接口保持一致
     *
     * @return     成功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kQueueEmpty
     */
    template<typename T>
    inline int32_t Pop(T& payload)
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif


        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = WaitEntry(&entry);
        if (ret != kSuccess)
            return ret;

        char* buf = entry->buffer;
        payload = *(T*)(buf);

        assert((mem_header_->release & ADK_MQ_POS_MASK) == (entry->pos & ADK_MQ_POS_MASK));
        ADK_BARRIER();
        ++(mem_header_->release);
        return ErrorCode::kSuccess;
    }

    /**
     * @brief      出队消息，和PushTsc配合
     *
     * @param      payload                入队消息对象
     *
     * @tparam     is_update_queue_stats  是否需要更新计算队列均值长度统计
     * @tparam     T                      消息类型
     *
     * @return     陈功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kQueueEmpty
     */
    template<bool is_update_queue_stats, typename T>
    inline int32_t PopTsc(T& payload)
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif

        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = WaitEntryTsc<is_update_queue_stats>(&entry);
        if (ret != kSuccess)
            return ret;

        char* buf = entry->buffer;
        payload = *(T*)(buf);

        assert((mem_header_->release & ADK_MQ_POS_MASK) == (entry->pos & ADK_MQ_POS_MASK));
        ADK_BARRIER();
        ++(mem_header_->release);
        return ErrorCode::kSuccess;
    }

    template<typename T>
    inline int32_t PopExp(T& payload)
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif

        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = WaitEntryExp(&entry);
        if (ADK_UNLIKELY(ret != kSuccess))
            return ret;

        char* buf = entry->buffer;
        payload = *(T*)(buf);

        ADK_BARRIER();
        ++(mem_header_->release);
        return ErrorCode::kSuccess;
    }

    template<typename T, typename CB>
    inline int32_t Pop(T& payload, const CB& cb) // [](char*, struct timeval*, int){}
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif

        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = WaitEntry(&entry);
        if (ret != kSuccess)
            return ret;

        char* buf = entry->buffer;

        #if defined(__ADK_MQ_PROFILE_LAT__) || defined(__AMI_LAT_PROFILE__)
        cb(*(char**)buf, &tv_, path_);
        #endif

        payload = *(T*)(buf);

        assert((mem_header_->release & ADK_MQ_POS_MASK) == (entry->pos & ADK_MQ_POS_MASK));
        ADK_BARRIER();
        ++(mem_header_->release);
        return ErrorCode::kSuccess;
    }

    template<typename T>
    inline T* Head()
    {
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        uint64_t monotonic_head_index = mem_header_->head;
        if (monotonic_head_index >= head_threshold_)
        {
            head_threshold_ = ACCESS_ONCE(mem_header_->tail);
            if (monotonic_head_index >= head_threshold_)
            {
                return NULL;
            }

            UpdateQueueLenth(monotonic_head_index);
        }

        g_head_entry = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);
        char* buf = g_head_entry->buffer;
        return (T*)(buf);
    }

    inline int32_t Pop()
    {
        assert(g_head_entry != NULL);
        assert(g_head_entry->pos == mem_header_->head);
        ++(mem_header_->head);
        return FreeEntry(g_head_entry);
    }

    template<typename T>
    inline int32_t Pop()
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif


        Entry* entry;
        int32_t ret = WaitEntry(&entry);
        if (ret != kSuccess)
            return ret;

        return FreeEntry(entry);
    }

    /**
     * @brief      从申请一个空闲Entry，该Entry指向队列中一块空闲的缓存，可用于填充待入队元素。
     *
     * @param[out]      entry_pptr  成功时该指针指向空闲的Entry
     *
     * @return     成功时返回ErrorCode::kSuccess，队列满时返回ErrorCode::kQueueFull
     */
    ADK_ALWAYS_INLINE int32_t AllocEntry(struct Entry** entry_pptr)
    {
        uint64_t monotonic_reserve_index;
        if (ADK_UNLIKELY(!GetFreeEntry(monotonic_reserve_index)))
        {
            ++(mem_header_->nr_forward_fail);
            return kQueueFull;
        }

        *entry_pptr = ptr_add(entries_, GetInQueueIndex(monotonic_reserve_index) << entry_bits_);
        (*entry_pptr)->pos = monotonic_reserve_index;
        return kSuccess;
    }

    /**
     * @brief      申请入队消息的Slot
     *
     * @param[out] entry_pptr  输出参数，用于保存申请到的slot
     *
     * @return     成功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kQueueFull
     */
    inline int32_t AllocEntryTsc(struct Entry** entry_pptr)
    {
        uint64_t monotonic_reserve_index;
        if (ADK_UNLIKELY(!GetFreeEntry(monotonic_reserve_index)))
        {
            ++(mem_header_->nr_forward_fail);
            return kQueueFull;
        }

        *entry_pptr = ptr_add(entries_, GetInQueueIndex(monotonic_reserve_index) << entry_bits_);
        (*entry_pptr)->pos = monotonic_reserve_index;
        return kSuccess;
    }

    inline uint64_t AllocEntryExp(struct Entry** entry_pptr)
    {
        uint64_t monotonic_reserve_index;
        if (ADK_UNLIKELY(!GetFreeEntryExp(monotonic_reserve_index)))
        {
            ++(mem_header_->nr_forward_fail);
            return 0;
        }

        *entry_pptr = ptr_add(entries_, GetInQueueIndex(monotonic_reserve_index) << entry_bits_);
        return monotonic_reserve_index;
    }

    /**
     * @brief      待入队元素填充到AllocEntry接口申请的Entry之后，将该Entry入队
     *
     * @param      entry  AllocEntry接口申请的Entry
     *
     * @return     成功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kWouldblock
     */
    inline int32_t PostEntry(struct Entry* entry)
    {
        uint64_t pos = entry->pos;
        auto* mem_header = mem_header_;
        while (ADK_UNLIKELY(mem_header->tail != pos))
        {
            if (release_alert_)
                return kWouldblock;

            ADK_PAUSE();
        }

        ADK_BARRIER();
        assert(mem_header->tail == pos);
        mem_header->tail = pos + 1;
        return kSuccess;
    }

    /**
     * @brief      消息写入entry之后，通知消费一端有数据可读
     *
     * @param      entry  消息所对应的entry
     *
     * @return     成功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kWouldblock
     */
    inline int32_t PostEntryTsc(struct Entry* entry)
    {
        uint64_t pos = entry->pos;
        auto* mem_header = mem_header_;
        while (ADK_UNLIKELY(mem_header->tail != pos))
        {
            if (release_alert_)
                return kWouldblock;

            ADK_PAUSE();
        }

        entry->pos = (entry->pos & ADK_MQ_POS_MASK) | GetTSCLowerBits<ADK_MQ_TSC_PRECISION, ADK_MQ_LAT_SHIFT>();

        ADK_BARRIER();
        assert(mem_header->tail == pos);
        mem_header->tail = pos + 1;
        return kSuccess;
    }

    inline int32_t PostEntryExp(uint64_t index)
    {
        auto* mem_header = mem_header_;
        while (ADK_UNLIKELY(mem_header->tail != index))
        {
            if (release_alert_)
                return kWouldblock;

            ADK_PAUSE();
        }

        ADK_BARRIER();
        assert(mem_header->tail == index);
        mem_header->tail = index + 1;
        return kSuccess;
    }

    /**
     * @brief      等待队列中有携带队列元素（入队元素）的Entry
     *
     * @param      entry_pptr  指向有携带队列元素的Entry
     *
     * @return     成功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kQueueEmpty
     */
    template<bool is_peek = false>
    inline int32_t WaitEntry(struct Entry** entry_pptr)
    {
        #if defined(__ADK_MQ_PROFILE_LAT__) || defined(__AMI_LAT_PROFILE__)
        path_ = 0;
        #endif

        uint64_t monotonic_head_index = mem_header_->head;
        if (ADK_UNLIKELY(monotonic_head_index >= head_threshold_))
        {
            head_threshold_ = ACCESS_ONCE(mem_header_->tail);   // FIXME : prevent optimization scenario Pop(); xxx; Pop()
            if (monotonic_head_index >= head_threshold_)
            {
                return kQueueEmpty;
            }

            UpdateQueueLenth(monotonic_head_index);
            #if defined(__ADK_MQ_PROFILE_LAT__) || defined(__AMI_LAT_PROFILE__)
            path_ = 1;
            #endif
        }

        #if defined(__ADK_MQ_PROFILE_LAT__) || defined(__AMI_LAT_PROFILE__)
        gettimeofday(&tv_, nullptr);
        #endif

        *entry_pptr = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);

        ADK_BARRIER();
        assert(mem_header_->head == monotonic_head_index);

        if (!is_peek)
        {
            mem_header_->head = monotonic_head_index + 1;
        }

        return kSuccess;   
    }

    /**
     * @brief      出队端(消费端)等待新的Entry，每个Entry包含一个消息
     *
     * @param      entry_pptr             Entry指针
     *
     * @tparam     is_update_queue_stats  是否统计队列平均长度
     *
     * @return     成功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kQueueEmpty
     */
    template<bool is_update_queue_stats>
    inline int32_t WaitEntryTsc(struct Entry** entry_pptr)
    {
        uint64_t monotonic_head_index = mem_header_->head;
        if (ADK_UNLIKELY(monotonic_head_index >= head_threshold_))
        {
            head_threshold_ = ACCESS_ONCE(mem_header_->tail);   // FIXME : prevent optimization scenario Pop(); xxx; Pop()
            if (monotonic_head_index >= head_threshold_)
            {
                return kQueueEmpty;
            }

            UpdateQueueLenthTsc<is_update_queue_stats>(monotonic_head_index);
        }

        *entry_pptr = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);

        ADK_BARRIER();
        assert(mem_header_->head == monotonic_head_index);
        mem_header_->head = monotonic_head_index + 1;
        return kSuccess;   
    }

    inline int32_t WaitEntryExp(struct Entry** entry_pptr)
    {
        uint64_t monotonic_head_index = mem_header_->head;
        if (ADK_UNLIKELY(monotonic_head_index >= head_threshold_))
        {
            head_threshold_ = ACCESS_ONCE(mem_header_->tail);   // FIXME : prevent optimization scenario Pop(); xxx; Pop()
            if (monotonic_head_index >= head_threshold_)
            {
                return kQueueEmpty;
            }

            UpdateQueueLenth(monotonic_head_index);
        }

        *entry_pptr = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);
        
        ADK_BARRIER();
        mem_header_->head = monotonic_head_index + 1;
        return kSuccess;   
    }

    /**
     * @brief      该接口与WaitEntry配合使用，待队列元素不再使用时，释放携带该队列元素的Entry
     *
     * @param      entry  携带队列元素的Entry
     *
     * @return     成功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kWouldblock
     *
     * @note 释放的顺序须和入队(Push)的顺序一致
     */
    template<bool is_peek = false>
    inline int32_t FreeEntry(struct Entry* entry)
    {
        // FIXME: Do we need check this condition in single consumer scenario?
        //        ami FreeEntry() by Merger or Deliverer!
        //        recorder FreeEntry() by IOThreads!
        //        we need the following check only if we do out of order FreeEntry()
        uint64_t pos = entry->pos;
        auto* mem_header = mem_header_;

        if (is_peek)
        {
            mem_header->head = pos + 1;
        }

        while (mem_header->release != pos)
        {
            if (release_alert_)
                return kWouldblock;

            ADK_PAUSE();
        }

        ADK_BARRIER();
        assert(mem_header->release == pos);
        mem_header->release = pos + 1;
        return kSuccess;
    }

    /**
     * @brief      消息被消费完毕，释放消息对应的entry
     *
     * @param      entry  entry指针
     *
     * @return     成功返回ErrorCode::kSuccess
     */
    inline int32_t FreeEntryTsc(struct Entry* entry)
    {
        ADK_BARRIER();
        assert((mem_header_->release & ADK_MQ_POS_MASK) == (entry->pos & ADK_MQ_POS_MASK));
        ++(mem_header_->release);
        return kSuccess;
    }

    template<bool is_peek = false>
    inline int32_t FreeEntryOrdered(struct Entry* entry)
    {
        uint64_t pos = entry->pos;
        auto* mem_header = mem_header_;

        if (is_peek)
        {
            mem_header->head = pos + 1;
        }

        ADK_BARRIER();
        assert(mem_header->release == pos);
        mem_header->release = pos + 1;
        return kSuccess;
    }

    /**
     * @brief      获取队列Entry大小
     *
     * @return     队列Entry大小
     */
    inline uint32_t entry_size() { return entry_size_; }

    /**
     * @brief      获取队列Entry可承载的队列元素大小的上限
     *
     * @return     有效负载大小
     */
    inline uint32_t effective_payload_size() { return entry_size_ - sizeof(struct Entry); }

    /**
     * @brief      释放在队列上阻塞的线程
     *
     * @param[in]  release  releae值为true时，释放正在队列上阻塞的出入对线程
     */
    void set_release_alert(bool release)
    {
        release_alert_ = release;
    }

    /**
     * @brief      获取无锁队列在QueueManager处的索引计数
     *
     * @return     索引计数
     */
    uint32_t index()
    {
        return mq_index_;   
    }

    /**
     * @brief      获取队列名称
     * 
     * @attention  注意队列名称不能超过255个字符
     *
     * @return     队列名称
     */
    string name()
    {
        string name_str;
        NameCopy(mem_header_->queue_name, &name_str);
        return name_str;
    }

    /**
     * @brief      当前的队列长度，即队列中携带队列元素的Entry数量
     *
     * @return     队列长度
     */
    // inline uint64_t length()
    // {
    //     const uint64_t release = mem_header_->release;
    //     ADK_BARRIER();
    //     const uint64_t reserve = mem_header_->reserve;
    //     // int64_t q_len = reserve - release;
    //     // if (q_len < 0)
    //     //     q_len = 0;
    //     // else if (q_len > queue_size_)
    //     //     q_len = queue_size_;
    //     // return q_len;
    //     return (reserve - release);
    // }
    inline uint64_t length()
    {
        const uint64_t release = mem_header_->release;
        ADK_BARRIER();
        const uint64_t tail = mem_header_->tail;
        const uint64_t queue_length = tail - release;
        
        return (queue_length <= queue_size_) ? queue_length : queue_size_;
    }

    inline uint64_t length2()
    {
        const uint64_t head = mem_header_->head;
        const uint64_t tail = mem_header_->tail;
        return (tail - head);
    }

    inline uint32_t usage()
    {
        return uint32_t(length() * 100 / queue_size_);
    }

    inline uint32_t queue_size()
    {
        return (uint32_t)queue_size_;
    }

    /**
     * @brief      队列的容量，即队列长度的上限
     *
     * @return     队列容量
     */
    inline uint64_t capacity()
    {
        return queue_size_;
    }

    /**
     * @brief      若队列位于共享内存中，在应用故障恢复后，队列的消费者端可使用该接口恢复队列故障前的状态，
     *             即获得队列中未消费的消息，可能有重复消息，应用需要自行滤重
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t ConsumerEndConsistent(bool fix_elem_leak = true);

    /**
     * @brief      若队列位于共享内存中，在应用故障恢复后，队列的生产者端可使用该接口恢复队列故障前的状态，
     *             故障前未完全入队的消息，应用需要重新入队
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t ProducerEndConsistent();

    /**
     * @brief      若队列位于共享内存中，在应用故障恢复后，队列的消费者端和生产者端恢复至队列故障前的状态，
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t Consistent(bool fix_elem_leak = true);

    /**
     * @brief      初始化队列中的Entry
     *
     * @param      entry         队列中第一个Entry的地址
     * @param[in]  entry_num     队列中Entry的数量，即队列容量
     * @param[in]  payload_size  队列元素的大小
     *
     * @return     返回队列中最后一个Entry之后的内存地址
     */
    static struct Entry* InitEntries(struct Entry* entry, uint32_t entry_num, uint32_t payload_size);

    /**
     * @brief      创建一个多生产者单消费者队列
     *
     * @param[in]  name                队列名称
     * @param[in]  entry_payload_size  队列元素的大小
     * @param[in]  queue_size          队列容量
     *
     * @return     成功时返回队列对象的引用，失败时返回NULL
     */
    static MPSCQueue* Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size);
    static void Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size, MPSCQueue* queue);

    /**
     * @brief      创建一个多生产者单消费者队列
     *
     * @param[in]  name                队列名称
     * @param[in]  entry_payload_size  队列元素的大小
     * @param[in]  queue_size          队列容量
     * @param[in]  flags               创建标志，用于指定特殊队列属性
     *                                 QueueFlags::ADK_LFMQ_FLAG_CREATE_AVG_BUF - 创建用于统计队列长度的缓冲区
     *                                 QueueFlags::ADK_LFMQ_FLAG_CREATE_AVG_LAT_BUF - 创建用于统计队列延迟的缓冲区
     *
     * @return     成功时返回队列对象的引用，失败时返回NULL
     */
    static MPSCQueue* Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size, QueueFlags flags);

    /**
     * @brief      复制一个多生产者单消费者队列，该接口方便应用将同一个队列复制多份，交由不同的生产者使用
     *
     * @param      queue  待复制的队列
     *
     * @return     成功时返回队列对象的引用，失败时返回NULL
     */
    static MPSCQueue* Duplicate(MPSCQueue& queue);
    static void Duplicate(MPSCQueue& queue, MPSCQueue* dup_queue);

    /**
     * @brief      获取最近入队元素的序列编号，如多单一生产者入队时该序列连续递增，如果多个生产者入队，每
     *             个生产者得到的该序列都递增
     *
     * @return     最近一次入队元素的序列编号
     */
    // static uint64_t LastEnqueueSqn() { return g_enqueue_sqn; }

    /**
     * @brief      创建一个多生产者单消费者队列
     *
     * @param[in]  name        队列名称
     * @param[in]  queue_size  队列容量
     *
     * @tparam     T           队列元素类型
     *
     * @return     成功时返回队列对象的引用，失败时返回NULL
     */
    template<typename T>
    static MPSCQueue* Create(const string& name, uint32_t queue_size) 
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif


        return Create(name, sizeof(T), queue_size); 
    }

    /**
     * @brief      创建MPSCQueue队列
     *
     * @param[in]  name        队列名称，Debug使用
     * @param[in]  queue_size  队列长度
     * @param[in]  flags       队列标志，用于指示是否需要对队列平均长度和平均排队延迟进行统计
     *
     * @tparam     T           队列元素类型
     *
     * @return     成功时返回队列对象的引用，失败时返回nullptr
     */
    template<typename T>
    static MPSCQueue* Create(const string& name, uint32_t queue_size, QueueFlags flags) 
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif


        return Create(name, sizeof(T), queue_size, flags); 
    }

    void Dump(boost::property_tree::ptree& ptree)
    {
        auto temp_nr_forward_fail = mem_header_->nr_forward_fail;
        auto fwd_fail = temp_nr_forward_fail - mem_header_->saved_nr_forward_fail;
        auto max_qlen = mem_header_->max_queue_length;
        if (fwd_fail > 0)
        {
            max_qlen = decltype(max_qlen) (queue_size_);
        }
        auto avg_qlen = -1;
        if (avg_buf_ != nullptr)
        {
            uint32_t begin = mem_header_->avg_buf_index_save;
            uint32_t end = mem_header_->avg_buf_index;
            if ((end & (decltype(end))ADK_LFMQ_AVG_BUF_MASK) != 0 || begin > end)
            {
                //begin = end - ADK_LFMQ_AVG_BUF_SIZE + 100;
                begin = 0;
                end = ADK_LFMQ_AVG_BUF_SIZE;
            }
            uint64_t sum = 0;
            if (end > begin)
            {
                for (uint32_t i = begin; i != end; ++i)
                {
                    sum += avg_buf_[i];
                }

                avg_qlen = decltype(avg_qlen)(sum / (end - begin));
            }
            else
            {
                avg_qlen = 0;
            }
            mem_header_->avg_buf_index_save = begin;
            ptree.put("avg_qlen", avg_qlen);
        }

        ptree.put("fwd_fail", fwd_fail);
        ptree.put("max_qlen", max_qlen);
        ptree.put("current_qlen", length());

        mem_header_->saved_nr_forward_fail = temp_nr_forward_fail;
        mem_header_->max_queue_length = 0;
    }

    inline void GetStats(QueueStats& stats)
    {
        auto temp_nr_forward_fail = mem_header_->nr_forward_fail;
        stats.nr_forward_fail = temp_nr_forward_fail - mem_header_->saved_nr_forward_fail;
        stats.max_queue_length = mem_header_->max_queue_length;

        mem_header_->saved_nr_forward_fail = temp_nr_forward_fail;
        mem_header_->max_queue_length = 0;
        
        if (stats.nr_forward_fail > 0)
        {
            stats.max_queue_length = decltype(stats.max_queue_length) (queue_size_);
        }
    }

    inline void GetStats(QueueStatsExp& stats)
    {
        auto temp_nr_forward_fail = mem_header_->nr_forward_fail;
        stats.nr_forward_fail = temp_nr_forward_fail - mem_header_->saved_nr_forward_fail;
        stats.max_queue_length = mem_header_->max_queue_length;

        mem_header_->saved_nr_forward_fail = temp_nr_forward_fail;
        mem_header_->max_queue_length = 0;
        
        if (stats.nr_forward_fail > 0)
        {
            stats.max_queue_length = decltype(stats.max_queue_length)(queue_size_);
        }

        if (avg_buf_ != nullptr)
        {
            uint32_t begin = mem_header_->avg_buf_index_save;
            uint32_t end = mem_header_->avg_buf_index;
            if ((end & (decltype(end))ADK_LFMQ_AVG_BUF_MASK) != 0 || begin > end)
            {
                //begin = end - ADK_LFMQ_AVG_BUF_SIZE + 100;
                begin = 0;
                end = ADK_LFMQ_AVG_BUF_SIZE;
            }
            uint64_t sum = 0;
            if (end > begin)
            {
                for (uint32_t i = begin; i != end; ++i)
                {
                    sum += avg_buf_[i];
                }

                stats.avg_queue_length = decltype(stats.avg_queue_length)(sum / (end - begin));
            }
            else
            {
                stats.avg_queue_length = 0;
            }
            mem_header_->avg_buf_index_save = begin;
        }
        else
        {
            stats.avg_queue_length = -1;
        }
    }

    /**
     * @brief      保存该entry的排队延迟
     *
     * @param      entry  队列上的entry
     */
    void SaveLatency(struct Entry* entry)
    {
        assert(avg_lat_buf_ != nullptr);
        avg_lat_buf_[avg_lat_buf_index_ & ADK_LFMQ_AVG_LAT_BUF_MASK] =
                    std::remove_pointer<decltype(avg_lat_buf_)>::type(entry->CalcLatency());
        avg_lat_buf_index_ = avg_lat_buf_index_ + 1;
    }

    /**
     * @brief      计算队列延迟
     *
     * @param      stats  输出延迟到该记录
     */
    void CalcLatency(QueueLatStats& stats)
    {
        stats.avg = 0ul;
        stats.min = (1ul << 31) - 1;
        stats.max = 0ul;
        stats.errors = 0ul;

        if (avg_lat_buf_ != nullptr)
        {
            uint64_t begin = avg_lat_buf_index_save_;
            uint64_t end = avg_lat_buf_index_;
            uint64_t nr_data = end - begin;
            if (nr_data >= ADK_LFMQ_AVG_LAT_BUF_SIZE)
            {
                begin = end - ADK_LFMQ_AVG_LAT_BUF_SIZE + 100;
            }

            uint64_t sum = 0;
            if (end > begin)
            {
                for (auto i = begin; i != end; ++i)
                {
                    uint64_t actual_tick = ((uint64_t)avg_lat_buf_[i & ADK_LFMQ_AVG_LAT_BUF_MASK])
                                           << ADK_MQ_TSC_PRECISION;
                    if (actual_tick == 0 || actual_tick >= 0x80000000ul)
                    {
                        ++stats.errors;
                        continue;
                    }

                    sum += actual_tick;
                    stats.min = std::min(stats.min, actual_tick);
                    stats.max = std::max(stats.max, actual_tick);
                }

                if (end - begin > stats.errors)
                {
                    stats.avg = sum / (end - begin - stats.errors);    
                }
            }

            avg_lat_buf_index_save_ = end;
        }
    }

#ifndef __ADK_DEBUG__
protected:
#endif
    void set_mq_index(uint32_t mq_index)
    {
        mq_index_ = mq_index;
    }
#ifndef __ADK_DEBUG__
private:
#endif
    QueueMemoryHeader*  mem_header_;            // # 1
    struct Entry*       entries_;               // # 2
    uint32_t            entry_size_;
    uint32_t            entry_bits_;            // # 3
    uint64_t            queue_mask_;            // # 4
    uint64_t            queue_size_;            // # 5
    bool                release_alert_;
    uint32_t            mq_index_;              // # 6
    uint32_t*           avg_buf_;               // # 7
    uint32_t*           avg_lat_buf_;           // # 8
    uint64_t            avg_lat_buf_index_;
    uint64_t            avg_lat_buf_index_save_;
    ADK_EMPTY_CACHE_LINE;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t reserve_threshold_;
    uint64_t monotonic_reserve_index_;
    ADK_EMPTY_CACHE_LINE;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t head_threshold_;
    uint64_t monotonic_head_index_;
    ADK_EMPTY_CACHE_LINE;

    #if defined(__ADK_MQ_PROFILE_LAT__) || defined(__AMI_LAT_PROFILE__)
    struct timeval tv_;
    uint32_t path_;
    #endif

    inline uint64_t GetInQueueIndex(uint64_t index)
    {
        return index & queue_mask_;
    }

    inline bool GetFreeEntry(uint64_t& temp_reserve)
    {
        do 
        {
            temp_reserve = mem_header_->reserve;
            if (ADK_UNLIKELY(temp_reserve >= reserve_threshold_))
            {
                reserve_threshold_ = ACCESS_ONCE(mem_header_->release) + queue_size_;
                if (ADK_UNLIKELY(temp_reserve >= reserve_threshold_))
                {
                    return false;
                }
            }
        } while (ADK_UNLIKELY(!__sync_bool_compare_and_swap(&(mem_header_->reserve), temp_reserve, temp_reserve + 1)));
        return true;
    }

    inline bool GetFreeEntryExp(uint64_t& temp_reserve)
    {
        temp_reserve = __sync_fetch_and_add(&(mem_header_->reserve), 1);
        do 
        {
            if (ADK_UNLIKELY(temp_reserve >= reserve_threshold_))
            {
                reserve_threshold_ = ACCESS_ONCE(mem_header_->release) + queue_size_;
                if (ADK_UNLIKELY(temp_reserve >= reserve_threshold_))
                {
                    if (RollBack(temp_reserve))
                        return false;

                    continue;
                }
            }
            
            return true;
        } while (true);
    }

    inline void UpdateQueueLenth(uint64_t& monotonic_head_index)
    {
        const auto current_queue_length = head_threshold_ - monotonic_head_index;
        
        if (avg_buf_ != nullptr)
        {
            avg_buf_[mem_header_->avg_buf_index & (decltype(mem_header_->avg_buf_index))ADK_LFMQ_AVG_BUF_MASK] =
                    std::remove_pointer<decltype(avg_buf_)>::type(current_queue_length);

            mem_header_->avg_buf_index = (mem_header_->avg_buf_index + 1);
        }

        auto & max_queue_length = mem_header_->max_queue_length;
        max_queue_length =std::remove_reference<decltype(max_queue_length)>::type( ((max_queue_length < current_queue_length) ? 
                                    current_queue_length : max_queue_length));
    }

    /*
     * 内部使用统计队列指标
     */
    template<bool is_update_queue_stats = true>
    inline void UpdateQueueLenthTsc(uint64_t& monotonic_head_index)
    {
        const uint32_t current_queue_length = head_threshold_ - monotonic_head_index;
        
        if (is_update_queue_stats && avg_buf_ != nullptr)
        {
            avg_buf_[mem_header_->avg_buf_index & (decltype(mem_header_->avg_buf_index))ADK_LFMQ_AVG_BUF_MASK] = current_queue_length;
            mem_header_->avg_buf_index = (mem_header_->avg_buf_index + 1);
        }

        uint32_t& max_queue_length = mem_header_->max_queue_length;
        max_queue_length = (max_queue_length < current_queue_length) ? 
                                    current_queue_length : max_queue_length;
    }

    bool RollBack(uint64_t temp_reserve)
    {
        if (__sync_bool_compare_and_swap(
            &(mem_header_->reserve), temp_reserve + 1, temp_reserve))
        {
            return true;
        }
        return false;
    }

    #ifdef __ADK_DEBUG__
    void ReinitQueueStatus(uint64_t cursor);
    #endif

    // uint32_t GetPostEntry();
    friend class MQManager;
    friend int32_t DoInit3(void*);
};

/////////////////////////////////////////////---SPMCQueue---/////////////////////////////////////////////////////

class SPMCQueue
{
public:
    // SPMCQueue();
    ~SPMCQueue();

    int32_t Init(struct QueueMemoryHeader* header);

    template<typename T>
    inline int32_t Push(const T& payload)
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif


        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = AllocEntry(&entry);
        if (ret != kSuccess)
            return ret;

        char* buf = entry->buffer;
        *(T*)(buf) = payload;
        return PostEntry(entry);
    }

    template<typename T>
    inline int32_t Pop(T& payload)
    {
#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<T>::value
            && std::is_trivially_copyable<T>::value);
#endif


        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = WaitEntry(&entry);
        if (ret != kSuccess)
            return ret;

        char* buf = entry->buffer;
        payload = *(T*)(buf);
        return FreeEntry(entry);
    }

    inline int32_t AllocEntry(struct Entry** entry_pptr)
    {
        uint64_t monotonic_reserve_index = mem_header_->reserve;
        if (monotonic_reserve_index >= reserve_threshold_)
        {
            reserve_threshold_ = ACCESS_ONCE(mem_header_->release) + queue_size_;
            if (monotonic_reserve_index >= reserve_threshold_)
            {
                return kQueueFull;
            }
        }

        *entry_pptr = ptr_add(entries_, GetInQueueIndex(monotonic_reserve_index) << entry_bits_);
        (*entry_pptr)->pos = monotonic_reserve_index;
        ADK_BARRIER();
        ++mem_header_->reserve;
        return kSuccess;
    }

    inline int32_t PostEntry(struct Entry* entry)
    {
        assert(entry->pos == mem_header_->tail);

        ADK_BARRIER();

        ++(mem_header_->tail);
        return kSuccess;
    }

    inline int32_t WaitEntry(struct Entry** entry_pptr)
    {
        uint64_t monotonic_head_index;
        if (!GetPostEntry(&monotonic_head_index))
            return kQueueEmpty;

        *entry_pptr = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);
        return kSuccess;
    }

    inline int32_t FreeEntry(struct Entry* entry)
    {
        while (mem_header_->release != entry->pos)
        {
            if (release_alert_)
                return kWouldblock;

            ADK_PAUSE();                // FIXME : add backoff strategy
        }
        ADK_BARRIER();
        ++(mem_header_->release);
        return kSuccess;
    }

    inline uint32_t entry_size() { return entry_size_; }

    inline uint32_t effective_payload_size() { return entry_size_ - sizeof(struct Entry); }

    void set_release_alert(bool release)
    {
        release_alert_ = release;
    }

    uint32_t index()
    {
        return mq_index_;   
    }

    string name()
    {
        string name_str;
        NameCopy(mem_header_->queue_name, &name_str);
        return name_str;
    }

    inline uint64_t length()
    {
        const uint64_t release = mem_header_->release;
        ADK_BARRIER();
        const uint64_t tail = mem_header_->tail;
        const uint64_t queue_length = tail - release;
        
        return (queue_length <= queue_size_) ? queue_length : queue_size_;
    }

    inline uint32_t usage()
    {
        return uint32_t(length() * 100 / queue_size_);
    }

    inline uint32_t queue_size()
    {
        return uint32_t(queue_size_);
    }

    inline uint64_t capacity()
    {
        return queue_size_;
    }

    /**
     * @brief      若队列位于共享内存中，在应用故障恢复后，队列的消费者端可使用该接口恢复队列故障前的状态，
     *             即获得队列中未消费的消息，可能有重复消息，应用需要自行滤重
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t ConsumerEndConsistent(bool fix_elem_leak = true);

    /**
     * @brief      若队列位于共享内存中，在应用故障恢复后，队列的生产者端可使用该接口恢复队列故障前的状态，
     *             故障前未完全入队的消息，应用需要重新入队
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t ProducerEndConsistent();

    /**
     * @brief      若队列位于共享内存中，在应用故障恢复后，队列的消费者端和生产者端恢复至队列故障前的状态，
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t Consistent(bool fix_elem_leak = true);

    static SPMCQueue* Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size);
    static SPMCQueue* Duplicate(SPMCQueue& queue);
#ifndef __ADK_DEBUG__
protected:
#endif
    void set_mq_index(uint32_t mq_index)
    {
        mq_index_ = mq_index;
    }
#ifndef __ADK_DEBUG__
private:
#endif
    QueueMemoryHeader*  mem_header_;            // # 1
    struct Entry*       entries_;               // # 2
    uint32_t            entry_size_;
    uint32_t            entry_bits_;            // # 3
    uint64_t            queue_mask_;            // # 4
    uint64_t            queue_size_;            // # 5
    bool                release_alert_;
    uint32_t            mq_index_;              // # 6
    uint32_t*           avg_buf_;               // # 7
    uint32_t*           avg_lat_buf_;           // # 8
    uint64_t            avg_lat_buf_index_;
    uint64_t            avg_lat_buf_index_save_;
    ADK_EMPTY_CACHE_LINE;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t reserve_threshold_;
    uint64_t monotonic_reserve_index_;
    ADK_EMPTY_CACHE_LINE;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t head_threshold_; // FIXME : this alignment is not necessary

    uint64_t monotonic_head_index_;
    ADK_EMPTY_CACHE_LINE;

    inline uint64_t GetInQueueIndex(uint64_t index)
    {
        return index & queue_mask_;
    }

    inline bool GetPostEntry(uint64_t* entry_index)
    {
        uint64_t temp_head;
        do 
        {
            temp_head = mem_header_->head;
            if (temp_head >= head_threshold_)
            {
                head_threshold_ = mem_header_->tail;
                if (temp_head >= head_threshold_)
                {
                    return false;
                }
            }
        } while (!__sync_bool_compare_and_swap(&(mem_header_->head), temp_head, temp_head + 1));

        // gcc bug on arm "__sync_bool_compare_and_swap with no full barrier"
    #if (defined __GNUC__) && (__GNUC__ < 5) && (defined __aarch64__)
        ADK_BARRIER();
    #endif

        *entry_index = temp_head;
        return true;
    }
    friend class MQManager;
    friend class MemoryPool;
};

/////////////////////////////////////////////---SPSCQueue---/////////////////////////////////////////////////////

extern int32_t DoInit3(void*);

template<typename QElementType>
class SPSCQueue
{
public:
    ~SPSCQueue() {}

    int32_t Init(struct QueueMemoryHeader* header)
    {
        MPSCQueue* mpsc_queue = reinterpret_cast<MPSCQueue*>(this);
        return mpsc_queue->Init(header);
    }

    int32_t Init3()
    {
        return DoInit3(this);
    }

    inline int32_t ReorderPush(const QElementType& payload, uint64_t seq)
    {
        Entry* entry_ptr;
        ADK_CHECK_RET_SUCCESS(ReorderAllocEntry(&entry_ptr, seq));
        char* const buffer = entry_ptr->buffer;
        *(QElementType*)(buffer) = payload;
        return RecorderPostEntry(entry_ptr, seq);
    }

    inline int32_t ReorderPop(QElementType& payload)
    {
        Entry* entry_ptr;
        ADK_CHECK_RET_SUCCESS(RecorderWaitEntry(&entry_ptr));
        char* const buffer = entry_ptr->buffer;
        payload = *(QElementType*)(buffer);
        return RecorderFreeEntry(entry_ptr);
    }
   
    inline int32_t Push(const QElementType& payload)
    {
        // FIXME: check type size with entry_size
        assert(sizeof(QElementType) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = AllocEntry(&entry);
        if (ret != kSuccess)
            return ret;

        char * buf = entry->buffer;
        *(QElementType*)(buf) = payload;
        return PostEntry(entry);
    }

    inline int32_t Push2(const QElementType& payload)
    {
        assert(sizeof(QElementType) <= entry_size_ - sizeof(uint64_t)
               && sizeof(QElementType) == 8);

        Entry* entry;
        int32_t ret = AllocEntry2(&entry);
        if (ADK_UNLIKELY(ret != kSuccess))
            return ret;

        char * buf = entry->buffer;
        *(QElementType*)(buf) = payload;
        return PostEntry2(entry);
    }

    struct ElementContainer
    {
        uint64_t elem;
    };

    inline int32_t Push3(const QElementType& payload)
    {
        ADK_BARRIER();
        assert(sizeof(QElementType) == 8);
        ElementContainer* ec = (ElementContainer*)ptr_add(
                        entries_, GetInQueueIndex3(monotonic_reserve_index_) << 7);
        if (ADK_UNLIKELY(ACCESS_ONCE(ec->elem) != 0u))
            return ErrorCode::kQueueFull;

        ec->elem = payload;
        ADK_BARRIER();
        ++monotonic_reserve_index_;
        return ErrorCode::kSuccess;
    }

    inline int32_t Pop(QElementType& payload)
    {
        // FIXME: check type size with entry_size
        assert(sizeof(QElementType) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = WaitEntry(&entry);
        if (ret != kSuccess)
            return ret;

        char* buf = entry->buffer;
        payload = *(QElementType*)(buf);
        return FreeEntry(entry);
    }

    template<bool need_stats = true>
    inline int32_t Pop2(QElementType& payload)
    {
        assert(sizeof(QElementType) <= entry_size_ - sizeof(uint64_t)
               && sizeof(QElementType) == 8);

        Entry* entry;
        int32_t ret = WaitEntry2<need_stats>(&entry);
        if (ADK_UNLIKELY(ret != kSuccess))
            return ret;

        char* buf = entry->buffer;
        payload = *(QElementType*)(buf);
        return FreeEntry2(entry);
    }

    inline int32_t Pop3(QElementType& payload)
    {
        assert(sizeof(QElementType) == 8);
        ElementContainer* ec = (ElementContainer*)ptr_add(
                            entries_, GetInQueueIndex3(monotonic_head_index_) << 7);
        if (ADK_UNLIKELY(ACCESS_ONCE(ec->elem) == 0))
            return ErrorCode::kQueueEmpty;

        payload = ec->elem;
        ADK_BARRIER();
        ++monotonic_head_index_;

        ADK_BARRIER();
        ec->elem = 0;
        return ErrorCode::kSuccess;
    }

    inline QElementType* Head()
    {
        assert(sizeof(QElementType) <= entry_size_ - sizeof(uint64_t));

        uint64_t monotonic_head_index;
        if (!HaveData(monotonic_head_index))
        {
            return NULL;
        }

        g_head_entry = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);
        char* buf = g_head_entry->buffer;
        return (QElementType*)(buf);
    }

    inline int32_t Pop()
    {
        UpdateQueueLenth(mem_header_->head);
        assert(g_head_entry != NULL);
        assert(g_head_entry->pos == mem_header_->head);
        ADK_BARRIER();
        ++(mem_header_->head);
        return FreeEntry(g_head_entry);
    }

    template<typename Element>
    inline int32_t Pop()
    {
        Entry* entry;
        int32_t ret = WaitEntry(&entry);
        if (ret != kSuccess)
            return ret;

        return FreeEntry(entry);
    }

    inline int32_t AllocEntry(struct Entry** entry_pptr)
    {
        uint64_t monotonic_reserve_index = mem_header_->reserve;
        if (monotonic_reserve_index >= reserve_threshold_)
        {
            reserve_threshold_ = ACCESS_ONCE(mem_header_->release) + queue_size_;
            if (monotonic_reserve_index >= reserve_threshold_)
            {
                ADK_BARRIER();
                ++(mem_header_->nr_forward_fail);
                return kQueueFull;
            }
        }

        *entry_pptr = ptr_add(entries_, GetInQueueIndex(monotonic_reserve_index) << entry_bits_);
        (*entry_pptr)->pos = monotonic_reserve_index;
        ADK_BARRIER();
        ++mem_header_->reserve;
        return kSuccess;
    }

    inline int32_t AllocEntry2(struct Entry** entry_pptr)
    {
        if (ADK_UNLIKELY(monotonic_reserve_index_ >= reserve_threshold_))
        {
            reserve_threshold_ = ACCESS_ONCE(mem_header_->release) + 8192;
            if (monotonic_reserve_index_ >= reserve_threshold_)
            {
                ++(mem_header_->nr_forward_fail);
                return kQueueFull;
            }
        }

        *entry_pptr = ptr_add(entries_, GetInQueueIndex2(monotonic_reserve_index_) << 4);
        //Following assert() has side effect, but actually, there is no need to modify pos value.
        assert(((*entry_pptr)->pos = monotonic_reserve_index_));
        ADK_BARRIER();
        ++monotonic_reserve_index_;
        return kSuccess;
    }

    template<bool batch_mode = false>
    inline void UpdateTail(Entry* entry)
    {
        if (batch_mode)
        {
            ADK_BARRIER();
            mem_header_->tail = entry->pos + 1;
            return ;
        }

        entry->pos += queue_size_ + 1;
        ADK_BARRIER();
        mem_header_->tail = entry->pos;
    }
    
    inline int32_t PostEntry(struct Entry* entry)
    {
        assert(entry->pos == mem_header_->tail);

        ADK_BARRIER();

        ++(mem_header_->tail);
        return kSuccess;
    }

    inline int32_t PostEntry2(struct Entry* entry)
    {
        auto* temp = mem_header_;
        assert(entry->pos == mem_header_->tail);

        ADK_BARRIER();

        ++(temp->tail);
        return kSuccess;
    }

    template<bool recycle_mode = false>
    inline int32_t WaitEntry(struct Entry** entry_pptr)
    {
        uint64_t monotonic_head_index;
        if (!HaveData(monotonic_head_index))
        {
            return kQueueEmpty;
        }

        *entry_pptr = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);
        if (recycle_mode)
        {
            (*entry_pptr)->pos = monotonic_head_index;
        }

        ADK_BARRIER();
        ++(mem_header_->head);
        return kSuccess;   
    }

    template<bool need_stats = true>
    inline int32_t WaitEntry2(struct Entry** entry_pptr)
    {
        if (!HaveData2<need_stats>())
        {
            return kQueueEmpty;
        }

        *entry_pptr = ptr_add(entries_, GetInQueueIndex2(monotonic_head_index_) << 4);
        ADK_BARRIER();
        ++monotonic_head_index_;
        return kSuccess;   
    }

    inline int32_t FreeEntry(struct Entry* entry)
    {
        // FIXME: Do we need check this condition in single consumer scenario?
        //        ami FreeEntry() by Merger or Deliverer!
        //        recorder FreeEntry() by IOThreads!
        //        we need the following check only if we do out of order FreeEntry()
        assert(mem_header_->release == entry->pos);
        ADK_BARRIER();
        ++(mem_header_->release);
        return kSuccess;
    }

    inline int32_t FreeEntry2(struct Entry* entry)
    {
        auto* temp = mem_header_;
        assert(mem_header_->release == entry->pos);
        ADK_BARRIER();
        ++(temp->release);
        return kSuccess;
    }

    inline int32_t ReorderAllocEntry(Entry** entry_ptr, uint64_t seq)
    {
        if (seq >= reserve_threshold_)
        {
            reserve_threshold_ = ACCESS_ONCE(mem_header_->release) + queue_size_;
            if (seq >= reserve_threshold_)
            {
                ++(mem_header_->nr_forward_fail);
                return kQueueFull;
            }
        }

        *entry_ptr = ptr_add(entries_, GetInQueueIndex(seq) << entry_bits_);
        return kSuccess;
    }

    inline int32_t RecorderPostEntry(Entry* entry_ptr, uint64_t seq)
    {
        ADK_BARRIER();
        entry_ptr->pos = -(seq);
        return kSuccess;
    }

    inline int32_t RecorderWaitEntry(Entry** entry_ptr)
    {
        uint64_t monotonic_head_index = mem_header_->head;
        *entry_ptr = ptr_add(entries_, GetInQueueIndex(monotonic_head_index) << entry_bits_);
        // UpdateQueueLenth(monotonic_head_index);

        if (ADK_UNLIKELY(!(ACCESS_ONCE((*entry_ptr)->pos) & (1ul << 63))))
        {
            return kQueueEmpty;
        }

        ADK_BARRIER();
        ++(mem_header_->head);
        return kSuccess;
    }

    inline int32_t RecorderFreeEntry(Entry* entry_ptr)
    {
        auto* mem_header = mem_header_;
        ADK_BARRIER();
        entry_ptr->pos = -(entry_ptr->pos);
        ADK_BARRIER();
        ++(mem_header->release);
        return kSuccess;
    }

    inline uint32_t entry_size() { return entry_size_; }

    inline uint32_t effective_payload_size() { return entry_size_ - sizeof(struct Entry); }

    void set_release_alert(bool release)
    {
        release_alert_ = release;
    }

    uint32_t index()
    {
        return mq_index_;   
    }

    string name()
    {
        string name_str;
        NameCopy(mem_header_->queue_name, &name_str);
        return name_str;
    }

    inline uint64_t length()
    {
        const uint64_t release = mem_header_->release;
        ADK_BARRIER();
        const uint64_t tail = mem_header_->tail;
        const uint64_t queue_length = tail - release;
        
        return (queue_length <= queue_size_) ? queue_length : queue_size_;
    }

    inline uint32_t usage()
    {
        return length() * 100 / queue_size_;    // FIXME: optimize div operation with bits shift
    }

    inline uint32_t queue_size()
    {
        return queue_size_;
    }

    inline void GetStats(QueueStats& stats)
    {
        auto temp_nr_forward_fail = mem_header_->nr_forward_fail;
        stats.nr_forward_fail = temp_nr_forward_fail - mem_header_->saved_nr_forward_fail;
        stats.max_queue_length = mem_header_->max_queue_length;

        mem_header_->saved_nr_forward_fail = temp_nr_forward_fail;
        mem_header_->max_queue_length = 0;
        
        if (stats.nr_forward_fail > 0)
        {
            stats.max_queue_length = queue_size_;
        }
    }

    static SPSCQueue* Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size)
    {

#if __GNUC__ >= 5
        BOOST_STATIC_WARNING(
            std::is_trivially_destructible<QElementType>::value
            && std::is_trivially_copyable<QElementType>::value);
#endif

        
        MPSCQueue* mpsc_queue = MPSCQueue::Create(name, entry_payload_size, queue_size);
        return reinterpret_cast<SPSCQueue*>(mpsc_queue);
    }

    static SPSCQueue* Duplicate(SPSCQueue& spsc_queue)
    {
        MPSCQueue* mpsc_queue = reinterpret_cast<MPSCQueue*>(&spsc_queue);
        MPSCQueue* dup_mpsc_queue = MPSCQueue::Duplicate(*mpsc_queue);
        return reinterpret_cast<SPSCQueue*>(dup_mpsc_queue);
    }

    static SPSCQueue* Create(const string& name, uint32_t queue_size = 1024) 
    { 
        return Create(name, sizeof(QElementType), queue_size); 
    }

protected:
    void set_mq_index(uint32_t mq_index)
    {
        mq_index_ = mq_index;
    }

private:
    QueueMemoryHeader*  mem_header_;            // # 1
    struct Entry*       entries_;               // # 2
    uint32_t            entry_size_;
    uint32_t            entry_bits_;            // # 3
    uint64_t            queue_mask_;            // # 4
    uint64_t            queue_size_;            // # 5
    bool                release_alert_;
    uint32_t            mq_index_;              // # 6
    uint32_t*           avg_buf_;               // # 7
    uint32_t*           avg_lat_buf_;           // # 8
    uint64_t            avg_lat_buf_index_;
    uint64_t            avg_lat_buf_index_save_;
    ADK_EMPTY_CACHE_LINE;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t reserve_threshold_;

    uint64_t monotonic_reserve_index_;
    ADK_EMPTY_CACHE_LINE;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t head_threshold_;// FIXME : this alignment is not necessary

    uint64_t monotonic_head_index_;
    ADK_EMPTY_CACHE_LINE;

    inline bool HaveData(uint64_t& monotonic_head_index)
    {
        monotonic_head_index = mem_header_->head;
        if (monotonic_head_index >= head_threshold_)
        {
            head_threshold_ = ACCESS_ONCE(mem_header_->tail);   // FIXME : prevent optimization scenario Pop(); xxx; Pop()
            if (monotonic_head_index >= head_threshold_)
            {
                return false;
            }
            
            UpdateQueueLenth(monotonic_head_index);
        }
        return true;
    }

    template<bool need_stats = true>
    inline bool HaveData2()
    {
        if (ADK_UNLIKELY(monotonic_head_index_ >= head_threshold_))
        {
            head_threshold_ = ACCESS_ONCE(mem_header_->tail);   // FIXME : prevent optimization scenario Pop(); xxx; Pop()
            if (monotonic_head_index_ >= head_threshold_)
            {
                return false;
            }
                
            if (need_stats)
                UpdateQueueLenth(monotonic_head_index_);
        }
        return true;
    }

    inline void UpdateQueueLenth(uint64_t& monotonic_head_index)
    {
        const uint32_t current_queue_length = head_threshold_ - monotonic_head_index;
        uint32_t& max_queue_length = mem_header_->max_queue_length;
        max_queue_length = (max_queue_length < current_queue_length) ? current_queue_length : max_queue_length;
    }

    inline uint64_t GetInQueueIndex(uint64_t index)
    {
        return index & queue_mask_;
    }

    inline uint64_t GetInQueueIndex2(uint64_t index)
    {
        assert(queue_mask_ == 8191);
        return index & 8191;
    }

    inline uint64_t GetInQueueIndex3(uint64_t index)
    {
        assert(queue_mask_ == 8191);
        return index & 1023;
    }

    friend class MQManager;
};

template<int32_t nr_queues, typename QElementType>
class ConcurrentQueue
{
public:
    struct alignas(ADK_CACHE_LINE_SIZE) QueueContainer
    {
        SPSCQueue<QElementType>* queue;
    };

    ConcurrentQueue()
    {
        spsc_queues_ = NULL;
        producer_cursor_ = 0;
        consumer_cursor_ = 0;
        release_alert_ = false;
    }

    ~ConcurrentQueue()
    {}

    int32_t Init(SPSCQueue<QElementType>* queue)
    {
        assert(nr_queues == 1);

        spsc_queues_ = reinterpret_cast<QueueContainer*>(memalign(ADK_CACHE_LINE_SIZE, sizeof(QueueContainer)));
        spsc_queues_[0].queue = queue;
        return ErrorCode::kSuccess;
    }

    int32_t Init(const string& name, uint32_t queue_size)
    {
        queue_name_ = name;
        char name_buf[1024];
        spsc_queues_ = reinterpret_cast<QueueContainer*>(memalign(ADK_CACHE_LINE_SIZE, sizeof(QueueContainer) * nr_queues));
        for (int32_t i = 0; i < nr_queues; ++i)
        {
            snprintf(name_buf, 1023, "%s_%d", name.c_str(), i + 1);
            name_buf[1023] = 0;
            spsc_queues_[i].queue = SPSCQueue<QElementType>::Create(&name_buf[0], queue_size);
        }
        return ErrorCode::kSuccess;
    }

    ConcurrentQueue<1, QElementType>* GetProducerByIndex(int index)
    {
        if (index >= nr_queues)
            return NULL;

        void* mem_ccq = memalign(ADK_CACHE_LINE_SIZE, sizeof(ConcurrentQueue<1, QElementType>));
        if (nullptr == mem_ccq)
        {
            return nullptr;
        }

        new (mem_ccq) ConcurrentQueue<1, QElementType>();
        ConcurrentQueue<1, QElementType>* ccq = (ConcurrentQueue<1, QElementType>*)mem_ccq;
        ccq->Init(spsc_queues_[index].queue);

        return ccq;
    }

    ConcurrentQueue<1, QElementType>* GetConsumerByIndex(int index)
    {
        return GetProducerByIndex(index);
    }

    inline int32_t TryPush(QElementType& payload)
    {
        if (nr_queues == 1)
            return spsc_queues_[0].queue->Push(payload);

        ADK_BARRIER();
        ++producer_cursor_;
        if (producer_cursor_ == nr_queues)
            producer_cursor_ = 0;

        return spsc_queues_[producer_cursor_].queue->Push(payload);
    }

    inline int32_t Push(QElementType& payload)
    {
        int32_t ec;
        if (nr_queues == 1)
        {
            while (ADK_UNLIKELY((ec = spsc_queues_[0].queue->Push(payload)) == ErrorCode::kQueueFull))
            {
                if (release_alert_)
                    break;

                ADK_PAUSE();
            }

            return ec;
        }

        if (producer_cursor_ == nr_queues)
            producer_cursor_ = 0;

        while (ADK_UNLIKELY((ec = spsc_queues_[producer_cursor_].queue->Push(payload)) == ErrorCode::kQueueFull))
        {
            if (release_alert_)
                break;

            ADK_PAUSE();
        }

        ADK_BARRIER();
        ++producer_cursor_;
        return ec;
    }

    inline int32_t ReorderPush(QElementType& payload, uint64_t seq)
    {
        assert(nr_queues == 1);

        int32_t ec;
        while (ADK_UNLIKELY((ec = spsc_queues_[0].queue->ReorderPush(payload, seq)) == ErrorCode::kQueueFull))
        {
            if (release_alert_)
                break;

            ADK_PAUSE();
        }

        return ec;
    }

    inline int32_t TryPush(QElementType& payload, int index)
    {       
        assert(index < nr_queues);

        return spsc_queues_[index].queue->Push(payload);
    }

    inline int32_t BroadCast(QElementType& payload)
    {
        for (int32_t i = 0; i < nr_queues; ++i)
        {
            while (ADK_UNLIKELY(spsc_queues_[i].queue->Push(payload) != ErrorCode::kSuccess))
            {
                ADK_PAUSE();        // FIXME: add backoff code
                if (release_alert_)
                    return ErrorCode::kQueueFull;
            }
        }

        return ErrorCode::kSuccess;
    }

    // note: if nr_queues larger than 1, dequeue may out of order
    inline int32_t TryPop(QElementType& payload)
    {
        if (nr_queues == 1)
            return spsc_queues_[0].queue->Pop(payload);

        ADK_BARRIER();
        ++consumer_cursor_;
        if (consumer_cursor_ == nr_queues)
            consumer_cursor_ = 0;

        return spsc_queues_[consumer_cursor_].queue->Pop(payload);
    }

    inline int32_t TryPop(QElementType& payload, int index)
    {
        assert(index < nr_queues);

        return spsc_queues_[index].queue->Pop(payload);
    }

    inline int32_t ReorderPop(QElementType& payload)
    {
        assert(nr_queues == 1);

        return spsc_queues_[0].queue->ReorderPop(payload);
    }

    void reset_consumer_cursor() { consumer_cursor_ = 0; }

    void reset_producer_cursor() { producer_cursor_ = 0; }

    void GetStats(std::vector<QueueStats>& stats)
    {
        stats.resize(nr_queues);

        for (uint32_t index = 0; index < nr_queues; ++index)
        {
            spsc_queues_[index].queue->GetStats(stats[index]);
        }
    }

private:
    QueueContainer*             spsc_queues_;
    bool                        release_alert_;
    string                      queue_name_;
    alignas(ADK_CACHE_LINE_SIZE) uint64_t producer_cursor_;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t consumer_cursor_;

    template<typename ElementType, int fanout>
    friend class Connector;
};

template<typename QElementType>
struct Task
{
    QElementType    element;
    alignas(8) uint32_t seq;
    short           dim;
    short           idx;
    uint64_t        total_order_seq_num;
};

template<typename QElementType, int fanout = 1>
class Connector
{
public:
    Connector()
        :   ccq_(NULL),
            seq_(0),
            lb_cursor_(0)
    {}

    Connector(ConcurrentQueue<fanout, Task<QElementType> >* ccq)
        :   ccq_(ccq),
            seq_(0)
    {}

    ~Connector() {}

    void Init(const string& name, uint32_t depth)
    {
        void* mem_ccq = memalign(ADK_CACHE_LINE_SIZE, sizeof(ConcurrentQueue<fanout, Task<QElementType>>));
        if (nullptr == mem_ccq)
        {
            return;
        }

        new (mem_ccq) ConcurrentQueue<fanout, Task<QElementType>>();
        ccq_ = (ConcurrentQueue<fanout, Task<QElementType>>*)mem_ccq;
        ccq_->Init(name, depth);
    }

    inline int32_t Forward(QElementType& element)
    {
        Task<QElementType> task = {element, 0, 1, 0, g_pipeline_total_order_seq_num};
        int32_t ec;
        while (ADK_UNLIKELY((ec = ccq_->TryPush(task)) != ErrorCode::kSuccess))
        {
            ADK_PAUSE();
        }
        return ec;
    }

    inline int32_t Forward(QElementType& element, short dim, short idx)     // FIXME: using thread local variable?
    {
        assert(fanout == 1);
        Task<QElementType> task = {element, 0, dim, idx, g_pipeline_total_order_seq_num};
        int32_t ec;
        while (ADK_UNLIKELY((ec = ccq_->TryPush(task)) != ErrorCode::kSuccess))
        {
            ADK_PAUSE();
        }
        return ec;
    }

    inline int32_t Forward(QElementType& element, int partition)
    {
        ADK_BARRIER();
        ++seq_;
        Task<QElementType> task;
        task.element = element;
        *(uint64_t*)(&task.seq) = seq_;
        task.total_order_seq_num = g_pipeline_total_order_seq_num;

        int32_t ec;
        while (ADK_UNLIKELY((ec = ccq_->TryPush(task, partition)) != ErrorCode::kSuccess))      // FIXME: dead loop
        {
            ADK_PAUSE();
        }
        return ec;
    }

    inline int32_t ReorderForward(QElementType& element, uint64_t seq)
    {
        assert(fanout == 1);
        Task<QElementType> task = {element, 0, 1, 0, g_pipeline_total_order_seq_num};
        return ccq_->ReorderPush(task, seq);
    }

    inline int32_t SequencialForward(QElementType& element)
    {
        Task<QElementType> task = {element, 0, 1, 0, g_pipeline_total_order_seq_num};
        return ccq_->Push(task);
    }

    inline int32_t LoadBalanceForward(QElementType& element)
    {
        int32_t ec;
        Task<QElementType> task = {element, 0, 1, 0, g_pipeline_total_order_seq_num};
        while ((ec = ccq_->TryPush(task)) != ErrorCode::kSuccess)
        {
            ADK_PAUSE();
        }
        return ec;
    }

    inline int32_t Fanout(QElementType& element, short dim)
    {
        Task<QElementType> task = {element, 0, dim, 0, g_pipeline_total_order_seq_num};
        ccq_->producer_cursor_ = 0;
        while (task.idx != task.dim)
        {
            ADK_BARRIER();
            ++(ccq_->producer_cursor_);
            if (ccq_->producer_cursor_ == fanout)
            {
                ccq_->producer_cursor_ = 0;
            }

            while (ccq_->spsc_queues_[ccq_->producer_cursor_].queue->Push(task) != ErrorCode::kSuccess)      // FIXME: dead loop!
                ADK_PAUSE();

            ++task.idx;
        }
        return ErrorCode::kSuccess;
    }

    inline int32_t Assemble(QElementType& element)
    {
        Task<QElementType> task;
        ccq_->consumer_cursor_ = 0;
        if (ccq_->spsc_queues_[ccq_->consumer_cursor_].queue->Pop(task) != ErrorCode::kSuccess)
        {
            return ErrorCode::kQueueEmpty;
        }

        while (task.idx + 1 != task.dim)
        {
            ADK_BARRIER();
            ++(ccq_->consumer_cursor_);
            if (ccq_->consumer_cursor_ == fanout)
            {
                ccq_->consumer_cursor_ = 0;
            }

            while (ccq_->spsc_queues_[ccq_->consumer_cursor_].queue->Pop(task) != ErrorCode::kSuccess)      // FIXME: dead loop!
                ADK_PAUSE();
        }

        element = task.element;
        g_pipeline_total_order_seq_num = task.total_order_seq_num;
        return ErrorCode::kSuccess;
    }

    inline int32_t SequencialReceive(QElementType& element)
    {
        Task<QElementType> task;
        int32_t ec = ccq_->TryPop(task, lb_cursor_);
        if (ec == ErrorCode::kSuccess)
        {
            ADK_BARRIER();
            ++lb_cursor_;
            if (lb_cursor_ == fanout)
            {
                lb_cursor_ = 0;
            }
            element = task.element;
            g_pipeline_total_order_seq_num = task.total_order_seq_num;
        }
        return ec;
    }

    inline int32_t Receive(QElementType& element)
    {
        int32_t ec;
        if (fanout != 1)
        {
            Task<QElementType> task;
            if ((ec = ccq_->TryPop(task)) != ErrorCode::kSuccess)
            {
                return ec;
            }

            element = task.element;
            g_pipeline_total_order_seq_num = task.total_order_seq_num;
            return ErrorCode::kSuccess;
        }

        if ((ec = ccq_->TryPop(task_)) != ErrorCode::kSuccess)
        {
            return ec;
        }

        element = task_.element;
        g_pipeline_total_order_seq_num = task_.total_order_seq_num;
        return ErrorCode::kSuccess;
    }

    inline int32_t ReceiveTask(Task<QElementType>& task)
    {
        return ccq_->TryPop(task);
    }

    inline int32_t ReorderReceive(QElementType& element)
    {
        assert(fanout == 1);

        int32_t ec = ccq_->ReorderPop(task_);
        if (ec == ErrorCode::kSuccess)
        {
            element = task_.element;
            g_pipeline_total_order_seq_num = task_.total_order_seq_num;
        }
        return ec;
    }

    inline short dim()
    {
        if (fanout == 1)
            return task_.dim;
        else 
            return 0;
    }

    inline short idx()
    {
        if (fanout == 1)
            return task_.idx;
        else
            return 0;
    }

    inline uint64_t total_order_seq_num()
    {
        return task_.total_order_seq_num;
    }

    Connector<QElementType, 1>* GetConnectorByIndex(int index)
    {
        ConcurrentQueue<1, Task<QElementType> >* ccq;

        ccq = ccq_->GetProducerByIndex(index);

        void* connecotr_temp = memalign(ADK_CACHE_LINE_SIZE, sizeof(Connector<QElementType, 1>));
        if (nullptr == connecotr_temp)
        {
            return nullptr;
        }

        new (connecotr_temp) Connector<QElementType, 1>(ccq);
        return (Connector<QElementType, 1>*)connecotr_temp;
    }

    void GetStats(std::vector<QueueStats>& stats)
    {
        ccq_->GetStats(stats);
    }

private:
    ConcurrentQueue<fanout, Task<QElementType> >*   ccq_;
    uint32_t                                        seq_;
    uint32_t                                        lb_cursor_;
    Task<QElementType>                              task_;
};

struct MQTable
{
    uint32_t mq_num;
    uint32_t entry_payload_size;
    uint32_t queue_header_size;
    uint32_t total_entry_num;
    uint32_t total_entry_num_used;
};

class SCSequentialQueue
{
public:
    // SCSequentialQueue();
    ~SCSequentialQueue();

    template<typename T>
    inline int32_t Push(const T& payload, uint64_t enqueue_seq)
    {
        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = AllocEntry(&entry, enqueue_seq);
        if (ret != kSuccess)
            return ret;

        char* buf = entry->buffer;
        *(T*)(buf) = payload;
        return PostEntry(entry, enqueue_seq);
    }

    template<typename T>
    inline int32_t Pop(T& payload)
    {
        // FIXME: check type size with entry_size
        assert(sizeof(T) <= entry_size_ - sizeof(uint64_t));

        Entry* entry;
        int32_t ret = WaitEntry(&entry);
        if (ret != kSuccess)
            return ret;

        char* buf = entry->buffer;
        payload = *(T*)(buf);
        return FreeEntry(entry);
    }

    inline int32_t AllocEntry(struct Entry** entry_pptr, uint64_t enqueue_seq)
    {
        if (enqueue_seq >= reserve_threshold_)
        {
            reserve_threshold_ = ACCESS_ONCE(mem_header_->release) + queue_size_;
            if (enqueue_seq >= reserve_threshold_)
            {
                return kQueueFull;
            }
        }

        *entry_pptr = ptr_add(entries_, GetInQueueIndex(enqueue_seq) << entry_bits_);
        return kSuccess;
    }

    inline int32_t PostEntry(struct Entry* entry, uint64_t enqueue_seq)
    {
        ADK_BARRIER();

        entry->pos = enqueue_seq;
        return kSuccess;
    }

    inline int32_t WaitEntry(struct Entry** entry_pptr)
    {
        const uint64_t next_entry_index = mem_header_->head;
        *entry_pptr = ptr_add(entries_, GetInQueueIndex(next_entry_index) << entry_bits_);
        if (ACCESS_ONCE((*entry_pptr)->pos) != next_entry_index)
        {
            return kQueueEmpty;            
        }

        ADK_BARRIER();
        ++(mem_header_->head);
        return kSuccess;
    }

    inline int32_t FreeEntry(struct Entry* entry)
    {
        assert(mem_header_->release == entry->pos);
        ADK_BARRIER();
        ++(mem_header_->release);
        return kSuccess;
    }

    inline uint32_t entry_size() { return entry_size_; }

    inline uint32_t effective_payload_size() { return entry_size_ - sizeof(struct Entry); }

    void set_release_alert(bool release)
    {
        release_alert_ = release;
    }

    uint32_t index()
    {
        return mq_index_;   
    }

    string name()
    {
        string name_str;
        NameCopy(mem_header_->queue_name, &name_str);
        return name_str;
    }

    inline uint64_t length()
    {
        const uint64_t release = mem_header_->release;
        ADK_BARRIER();
        const uint64_t tail = mem_header_->tail;
        const uint64_t queue_length = tail - release;
        
        return (queue_length <= queue_size_) ? queue_length : queue_size_;
    }

    inline uint32_t usage()
    {
        return uint32_t(length() * 100 / queue_size_);
    }

    inline uint32_t queue_size()
    {
        return uint32_t(queue_size_);
    }

    inline uint64_t capacity()
    {
        return queue_size_;
    }

    static SCSequentialQueue* Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size)
    {
        MPSCQueue* mpsc_queue = MPSCQueue::Create(name, entry_payload_size, queue_size);
        return reinterpret_cast<SCSequentialQueue*>(mpsc_queue);
    }

    static SCSequentialQueue* Duplicate(SCSequentialQueue& scs_queue)
    {
        MPSCQueue* mpsc_queue = reinterpret_cast<MPSCQueue*>(&scs_queue);
        MPSCQueue* dup_mpsc_queue = MPSCQueue::Duplicate(*mpsc_queue);
        return reinterpret_cast<SCSequentialQueue*>(dup_mpsc_queue);
    }

    int32_t Seek(uint64_t enqueue_seq);

#ifndef __ADK_DEBUG__
protected:
#endif
    void set_mq_index(uint32_t mq_index)
    {
        mq_index_ = mq_index;
    }
#ifndef __ADK_DEBUG__
private:
#endif
    QueueMemoryHeader*  mem_header_;            // # 1
    struct Entry*       entries_;               // # 2
    uint32_t            entry_size_;
    uint32_t            entry_bits_;            // # 3
    uint64_t            queue_mask_;            // # 4
    uint64_t            queue_size_;            // # 5
    bool                release_alert_;
    uint32_t            mq_index_;              // # 6
    uint32_t*           avg_buf_;               // # 7
    uint32_t*           avg_lat_buf_;           // # 8
    uint64_t            avg_lat_buf_index_;
    uint64_t            avg_lat_buf_index_save_;
    ADK_EMPTY_CACHE_LINE;

    alignas(ADK_CACHE_LINE_SIZE) uint64_t reserve_threshold_;

    uint64_t monotonic_reserve_index_;
    ADK_EMPTY_CACHE_LINE;

    alignas(ADK_CACHE_LINE_SIZE)  uint64_t head_threshold_;                             // FIXME : this alignment is not necessary

    uint64_t monotonic_head_index_;
    ADK_EMPTY_CACHE_LINE;

    inline uint64_t GetInQueueIndex(uint64_t index)
    {
        return index & queue_mask_;
    }

    friend class MQManager;
};


/*
 *  |QueueMemoryHeader|...|QueueMemoryHeader|<---page align--->|ring_buffer|...|ring_buffer|
 */
class MQManager
{
public:
    ~MQManager()
    {}

    enum Const
    {
        kMaxSharedMsgQueues = (4096 + 64),
    };

    /**
     * @brief      Create a share memory queue manager
     *
     * @param[in]  name                The manager name
     * @param[in]  entry_payload_size  The entry payload size, aka queue element size
     * @param[in]  queue_size          The avg queue size, aka avg ring buffer slots
     * @param[in]  extra_size          The extra queue entries (ring buffer slots)
     * 
     * @note       total_entries = queue_size * kMaxSharedMsgQueues + extra_size
     *             CreateSharedMPSCQueue() should fail, if there are no more entries.
     *
     * @return     On success, a MQManager object reference returned. On error, NULL is returned.
     */
    static MQManager* Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size, uint32_t extra_size = 1024 * 128);

    /**
     * @brief      Create a share memory queue manager
     *
     * @param[in]  name                The manager name
     * @param[in]  entry_payload_size  The entry payload size, aka queue element size
     * @param[in]  total_size          The total size to allocate(byte)
     *
     * @return     On success, a MQManager object reference returned. On error, NULL is returned.
     */
    static MQManager* CreateExt(const string& name, uint32_t entry_payload_size, SizeType total_size);
    
    /**
     * @brief      Attach to a share memory queue manager
     *
     * @param[in]  name  The manager name
     *
     * @return     On success, a MQManager object reference returned. On error, NULL is returned.
     */
    static MQManager* Attach(const string& name);

    /**
     * @brief      Destroy a share memory queue manager
     *
     * @param[in]  name  The manager name
     *
     * @return     On success, ErrorCode::kSuccess is returned. On error, ErrorCode::kFailure is returned.
     */
    static int32_t Destroy(const string& name);

    static int32_t Detach(const string& name);

    // FIXME: the follow methods are not thread safe.
    /**
     * @brief      Creates a shared mpsc queue at server side.
     *
     * @param[in]  queue_name  The queue name
     * @param[in]  block_num   The ring buffer block number
     *
     * @return     On success, a MPSCQueue object reference returned. On error, NULL is returned.
     */
    #ifdef __ADK_MQM_FAILURE_TEST__
    MPSCQueue* CreateSharedMPSCQueue(const string& queue_name, uint32_t block_num, uint32_t fp = 0);
    #else
    MPSCQueue* CreateSharedMPSCQueue(const string& queue_name, uint32_t block_num);
    #endif

    /**
     * @brief      Attach to a shared mpsc queue at client side.
     *
     * @param[in]  queue_index  The queue index. client got the index from other communication protocols
     *
     * @return     On success, a MPSCQueue object reference returned. On error, NULL is returned.
     */
    MPSCQueue* AttachSharedMPSCQueue(uint32_t queue_index);

    /**
     * @brief      Attach to a shared mpsc queue at client side.
     *
     * @param[in]  queue_name  The queue name. client got the queue name from other communication protocols
     *
     * @return     On success, a MPSCQueue object reference returned. On error, NULL is returned.
     */
    MPSCQueue* AttachSharedMPSCQueue(const std::string& queue_name);
#ifndef __ADK_DEBUG__
private:
#endif
    MQTable*                mq_table_;
    char                    mqm_name_[ADK_MAX_NAME_LEN];
    QueueMemoryHeader*      queue_headers_;
    uint32_t                queue_header_size_;
    uint32_t                entry_payload_size_;
    uint32_t                entry_size_;
    std::map<std::string, uint32_t> name_to_index_map_;
    uint32_t                last_iterate_;

    void IterateMQTable();
private:
    MQManager();

    MQManager(const MQManager&) = delete;

    const MQManager &operator = (const MQManager&) = delete;
};

} // adk       

#endif // ADK_LOCK_FREE_MSG_QUEUE_H_
