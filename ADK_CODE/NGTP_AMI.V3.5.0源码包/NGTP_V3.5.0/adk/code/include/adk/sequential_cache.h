/**
 * @brief      limited sequential data cache, the cache never shrink.
 * @author     zhaonan@archforce.com.cn
 * @date       2017/09/28
 */
#ifndef ADK_IMPL_SEQUENTIAL_CACHE_H_
#define ADK_IMPL_SEQUENTIAL_CACHE_H_

#include "util.h"
#include "event.h"
#include "arch/generic.h"
#include "lock_free_msg_queue.h"

#include <assert.h>

#include <list>
#include <deque>

#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>

namespace adk_impl
{

#ifndef ADK_MAX_SLAVES
#define ADK_MAX_SLAVES   1024
#endif

struct SlaveCursor
{
    uint64_t head __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t tail_threshold;
    void*    context;
};

typedef int32_t SlaveHandler;
/**
 * @brief      slave join in event. when SlaveNext() returns NULL, 
 *             this event is delivered to master 
 */
struct SlaveJoinEvent
{
    SlaveHandler    slave;          /** save handler */
    uint64_t        join_index;     /** the join index that master resume processing */
    void*           context;        /** the context registered by slave */
};

const SlaveHandler kInvalidSlaveHandler = -1;

struct SCacheStats
{
    uint32_t cache_usage;
    uint32_t nr_slave_joins;
    uint32_t nr_slaves;
};

/**
 * @brief      Class for sequential cache.
 *
 * @tparam     DataType            the cached element type
 * @tparam     BackoffPolicy       the polling backoff policy, default is using pause instructions
 * @tparam     interrupt_interval  the event check intervals. increase this value could reduce the
 *                                 event processing latency and also lower the loop efficiency
 */
template<typename DataType, typename BackoffPolicy = policy::Pause, uint32_t interrupt_interval = 8>
class SequentialCache
{
public:
    /**
     * @brief      the constructor
     *
     * @param[in]  nr_max_cache_data  The maximum number of cache data
     * @param[in]  join_threshold     The slave shall join with master when the distance is less
     *                                than the threshold
     */
    SequentialCache(uint64_t nr_max_cache_data = 1024*1024*16, // default size : 128MB
                    uint64_t join_threshold = 256)
        :   tail_(1),
            head_(1),
            cache_data_(NULL),
            nr_max_cache_data_(nr_max_cache_data),
            join_threshold_(join_threshold),
            nr_slaves_(0)
    {
        interrupt_counter_ = 1;
        is_release_alert_ = false;
        tail_threshold_ = 1;
        event_queue_ = NULL;
        BackoffPolicy::Init(backoff_);
    }

    ~SequentialCache()   
    {}

    /**
     * @brief      allocate internal resources
     *
     * @return     On success, kSuccess is returned
     */
    int32_t Init()
    {
        if (nr_max_cache_data_ == 0)
        {
            return ErrorCode::kInvalidParameters;
        }

        cache_data_ = new (std::nothrow) DataType* [nr_max_cache_data_];
        if (cache_data_ == NULL)
        {
            return ErrorCode::kFailure;
        }

        event_queue_ = SPSCQueue<SlaveJoinEvent>::Create("event_queue", 1024);
        if (event_queue_ == NULL)
        {
            return ErrorCode::kFailure;
        }

        memset(&cache_stats_, 0x00, sizeof(cache_stats_));
        return ErrorCode::kSuccess;
    }

    // ===================== producer api ============================   
    /**
     * @brief      Insert data buffer into the cache
     *
     * @param[in]      user_data  The user data pointer
     * @param[out]     index      The the sequential data index inside the cache
     * @note           not thread safe!
     * @return         on success, kSuccess is returned
     */
    int32_t Insert(DataType*& user_data, uint64_t& index)
    {
        if (ADK_UNLIKELY(tail_ >= nr_max_cache_data_))
        {
            return ErrorCode::kFailure;
        }

        index = tail_;
        cache_data_[tail_] = user_data;

        ADK_BARRIER();
        ++tail_;
        return ErrorCode::kSuccess;
    }

    /**
     * @brief      facillity the application which is not care about the output index
     *
     * @param      user_data  The user data
     *
     * @return     on success, kSuccess is returned
     */
    int32_t Insert(DataType*& user_data)
    {
        uint64_t dummy;
        return Insert(user_data, dummy);
    }

    // DataType* AllocBuffer()
    // {
    //     return cache_data_[tail_];
    // }

    // int32_t PostBuffer(DataType* data_buf);

    // ===================== consumer api ============================
    
    /**
     * @brief      start a new slave
     *
     * @param[in]  begin_index  the slave starts processing from begin_index
     * @param      context      the context is delivered to master when the slave is joined.
     *
     * @return     on success, slave handler is returned. on error, kInvalidSlaveHandler is returned
     */
    SlaveHandler NewSlave(uint64_t begin_index, void* context)
    {
        if (begin_index == 0)
        {
            return kInvalidSlaveHandler;
        }

        SlaveHandler ret_hdl;
        boost::mutex::scoped_lock lock_guard(sc_lock_);
        if (!free_hdl_list_.empty())
        {
            ret_hdl = free_hdl_list_.front();
            free_hdl_list_.pop_front();
        }
        else
        {
            ret_hdl = nr_slaves_;
            if (ret_hdl >= ADK_MAX_SLAVES)
            {
                return kInvalidSlaveHandler;
            }
            ++nr_slaves_;
        }

        slave_cursors_[ret_hdl].context = context;
        slave_cursors_[ret_hdl].head = begin_index;
        slave_cursors_[ret_hdl].tail_threshold = tail_;

        ++cache_stats_.nr_slaves;
        return ret_hdl;
    }

    /**
     * @brief      delete the slave handler
     *
     * @param[in]  hdl   The handler
     */
    void DeleteSlave(SlaveHandler hdl)
    {
        boost::mutex::scoped_lock lock_guard(sc_lock_);
        free_hdl_list_.push_back(hdl);
        --cache_stats_.nr_slaves;
    }

    // #############  lamda style
    /**
     * @brief      try to get next cache data, this method is for master
     *
     * @param[out] index          The data index
     * @param[in]  event_handler  The handler to process slave join in event
     *
     * @tparam     OnJoinEvent    the handler type, the function signature is "void (SlaveJoinEvent&)"
     *
     * @return     On success, cached data is returned. On error, NULL is returned
     */
    template<typename OnJoinEvent>
    DataType* MasterTryNext(uint64_t& index, const OnJoinEvent& event_handler)
    {
        if (ADK_UNLIKELY((((++interrupt_counter_) & (interrupt_interval - 1)) == 0)
                         && event_queue_->Pop(join_event_) == ErrorCode::kSuccess))
        {
            event_handler(join_event_);
            ++cache_stats_.nr_slave_joins;
        }

        index = head_;
        if (HaveData(index, &tail_threshold_))
        {
            ++head_;
            return cache_data_[index];
        }
        return NULL;
    }

    /**
     * @brief      get next cache data until sucess, this method is for master
     */
    template<typename OnJoinEvent>
    DataType* MasterNext(uint64_t& index, const OnJoinEvent& event_handler)
    {
        DataType* ret;
        do {
            ret = MasterTryNext(index, event_handler);
            if (ret != NULL)
            {
                backoff_.Reset();
                return ret;
            }
            backoff_.Run();
        } while (!ACCESS_ONCE(is_release_alert_));

        backoff_.Reset();
        return NULL;
    }

    /**
     * @brief      get the lastest data index that was consumed by master
     *
     * @return     on success, the data index is returned, if no data was consumed, 0 is returned.
     */
    inline uint64_t MasterLastIndex() { return head_ - 1; }

    // #############  boost function style
    // void RegisterEventHandler(const boost::function<void (SlaveJoinEvent& event)>& event_handler);
    // DataType* MasterNext(uint64_t& index);
    // #############

    /**
     * @brief      get cached data with index, this method is for master
     *
     * @param[in]  index  The data index
     *
     * @return     On sucess, cached data is returned. On error, NULL is returned.
     */
    DataType* MasterAt(uint64_t index)
    {
        if (ADK_LIKELY(HaveData(index, &tail_threshold_)))
        {
            return cache_data_[index];            
        }
        
        return NULL;
    }

    /**
     * @brief      get cached data, this method is for slave
     *
     * @param[in]  slave  The slave handler
     * @param[out] index  The data index
     *
     * @return     On success, cached data is returned. On error, NULL is returned.
     */
    DataType* SlaveNext(SlaveHandler slave, uint64_t& index)
    {
        auto& slave_cur = slave_cursors_[slave];
        index = slave_cur.head;
        if (ADK_LIKELY(!IsCatchUpWithMaster(slave_cur.head, &slave_cur.tail_threshold)))
        {
            ++slave_cur.head;
            return cache_data_[index];
        }

        SignalJoin(slave);
        return NULL;
    }

    /**
     * @brief      get cached data with index, this method is for slave
     *
     * @param[in]  slave  The slave handler
     * @param[in]  index  The date index 
     *
     * @return     On success, cached data is returned. On error, NULL is returned.
     */
    DataType* SlaveAt(SlaveHandler slave, uint64_t index)
    {
        if (ADK_LIKELY(HaveData(index, &(slave_cursors_[slave].tail_threshold))))
        {
            return cache_data_[index];
        }
        return NULL;
    }

    /**
     * @brief      release the consumer thread which is blocked on method MasterNext()
     */
    void ReleaseRxThread()
    {
        is_release_alert_ = true;
    }

    void GetStats(SCacheStats& stats)
    {
        stats = cache_stats_;
        stats.cache_usage = tail_ * 100Ul / nr_max_cache_data_;
    }

private:
    uint64_t                        tail_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));

    uint64_t                        head_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t                        tail_threshold_;
    uint32_t                        interrupt_counter_;
    bool                            is_release_alert_;
    SPSCQueue<SlaveJoinEvent>*      event_queue_;
    Backoff                         backoff_;
    SlaveJoinEvent                  join_event_;
    
    SlaveCursor                     slave_cursors_[ADK_MAX_SLAVES];
    DataType**                      cache_data_;
    const uint64_t                  nr_max_cache_data_;
    const uint64_t                  join_threshold_;
    
    boost::mutex                    sc_lock_;
    std::list<int32_t>              free_hdl_list_;
    int32_t                         nr_slaves_;

    SCacheStats                     cache_stats_;

    inline bool HaveData(uint64_t head, uint64_t* tail_threshold)
    {
        if (head < *tail_threshold)
        {
            return true;
        }

        *tail_threshold = ACCESS_ONCE(tail_);

        if (head < *tail_threshold)
        {
            return true;
        }

        return false;
    }

    inline bool IsCatchUpWithMaster(uint64_t head, uint64_t* tail_threshold)
    {
        if (head + join_threshold_ < *tail_threshold)
        {
            return false;
        }

        *tail_threshold = ACCESS_ONCE(head_);

        if (head + join_threshold_ < *tail_threshold)
        {
            return false;
        }

        return true;
    }

    void SignalJoin(SlaveHandler slave)
    {
        boost::mutex::scoped_lock lock_guard(sc_lock_);
        auto& slave_cur = slave_cursors_[slave];
        SlaveJoinEvent event = { slave, slave_cur.head, slave_cur.context };
        int32_t ec = event_queue_->Push(event);
        assert(ec == ErrorCode::kSuccess);
        ADK_NOTUSE(ec);
    }
};

} // adk

#endif // ADK_SEQUENTIAL_CACHE_H_
