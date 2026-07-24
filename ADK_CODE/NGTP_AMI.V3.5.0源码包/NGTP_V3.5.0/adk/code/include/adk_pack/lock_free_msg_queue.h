/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_LOCK_FREE_MSG_QUEUE_H_
#define ADK_LOCK_FREE_MSG_QUEUE_H_

#include "error_code.h"
#include "arch/generic.h"

#include <assert.h>
#include <string>
#include <vector>

namespace adk
{

extern uint64_t pipeline_total_order_seq_num();
extern void set_pipeline_total_order_seq_num(uint64_t sqn);

using std::string;

struct QueueStats
{
    uint64_t nr_forward_fail;
    uint32_t max_queue_length;
};

class MPSCQueue
{
public:
    static MPSCQueue* Create(const std::string& name, uint32_t entry_payload_size, uint32_t queue_size);

    template<typename T>
    inline int32_t Push(const T& payload)
    {
        return Push((const void*)&payload, (void*)impl::AssignFunction<T>);
    }

    template<typename T>
    inline int32_t Pop(T& payload)
    {
        return Pop((void*)&payload, (void*)impl::AssignFunction<T>);
    }
private:
    int32_t Push(const void* payload, void* assign);
    int32_t Pop(void* payload, void* assign);
};

class SPMCQueue
{
public:
    static SPMCQueue* Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size);

    template<typename T>
    inline int32_t Push(const T& payload)
    {
        return Push((const void*)&payload, (void*)impl::AssignFunction<T>);
    }

    template<typename T>
    inline int32_t Pop(T& payload)
    {
        return Pop((void*)&payload, (void*)impl::AssignFunction<T>);
    }

private:
    int32_t Push(const void* payload, void* assign);
    int32_t Pop(void* payload, void* assign);
};

class SPSCQueueBase
{
protected:
    static void* Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size);
    int32_t Push(const void* payload, void* assign);
    int32_t Pop(void* payload, void* assign);
};

template<typename QElementType>
class SPSCQueue : public SPSCQueueBase
{
public:
    static SPSCQueue* Create(const string& name, uint32_t queue_size = 1024)
    {
        return Create(name, sizeof(QElementType), queue_size);
    }

    static SPSCQueue* Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size)
    {
        return reinterpret_cast<SPSCQueue*>(SPSCQueueBase::Create(name, entry_payload_size, queue_size));
    }

    inline int32_t Push(const QElementType& payload)
    {
        return SPSCQueueBase::Push(&payload, (void*)impl::AssignFunction<QElementType>);
    }

    inline int32_t Pop(QElementType& payload)
    {
        return SPSCQueueBase::Pop(&payload, (void*)impl::AssignFunction<QElementType>);
    }
};

class ConcurrentQueueBase
{
protected:
    struct QueueContainerImpl
    {
        void* queue_impl;
    };

    ConcurrentQueueBase(int32_t nr_queue, uint32_t payload_size);

    virtual ~ConcurrentQueueBase() = default;

    int32_t Init(void* queue);

    int32_t Init(const string& name, uint32_t queue_size);

    void* GetProducerByIndex(int32_t index, void* assign);

    void* GetConsumerByIndex(int32_t index, void* assign);

    int32_t TryPush(const void* payload);

    int32_t Push(const void* payload);

    int32_t ReorderPush(const void* payload, uint64_t seq);

    int32_t TryPush(const void* payload, int index);

    int32_t BroadCast(const void* payload);

    int32_t TryPop(void* payload);

    int32_t TryPop(void* payload, int index);

    int32_t ReorderPop(void* payload);

    void reset_consumer_cursor();

    void reset_producer_cursor();

    void GetStats(std::vector<QueueStats>& stats);

protected:
    int32_t TryPush(void* const queue_impl, const void* payload);

    int32_t Push(void* const queue_impl, const void* payload);

    int32_t TryPop(void* const queue_impl, void* payload);

    int32_t  nr_queues_;
    uint32_t payload_size_;
    void*    assign_;

    string   queue_name_;
    bool     release_alert_;
    uint64_t producer_cursor_;
    uint64_t consumer_cursor_;
    QueueContainerImpl* spsc_queues_;

    template<typename QElementType, int fanout>
    friend class Connector;
};

template<int32_t nr_queues, typename QElementType>
class ConcurrentQueue : public ConcurrentQueueBase
{
public:
    ConcurrentQueue() : ConcurrentQueueBase(nr_queues, sizeof(QElementType))
    {
        assign_ = (void*)impl::AssignFunction<QElementType>;
    }

    ~ConcurrentQueue() = default;

    int32_t Init(SPSCQueue<QElementType>* queue)
    {
        return ConcurrentQueueBase::Init(queue);
    }

    int32_t Init(const string& name, uint32_t queue_size)
    {
        return ConcurrentQueueBase::Init(name, queue_size);
    }

    ConcurrentQueue<1, QElementType>* GetProducerByIndex(int32_t index)
    {
        return reinterpret_cast<ConcurrentQueue<1, QElementType>*>(ConcurrentQueueBase::GetProducerByIndex(index, assign_));
    }

    ConcurrentQueue<1, QElementType>* GetConsumerByIndex(int32_t index)
    {
        return reinterpret_cast<ConcurrentQueue<1, QElementType>*>(ConcurrentQueueBase::GetConsumerByIndex(index, assign_));
    }

    inline int32_t TryPush(QElementType& payload)
    {
        return ConcurrentQueueBase::TryPush((const void*)&payload);
    }

    inline int32_t Push(QElementType& payload)
    {
        return ConcurrentQueueBase::Push((const void*)&payload);
    }

    inline int32_t ReorderPush(QElementType& payload, uint64_t seq)
    {
        return ConcurrentQueueBase::ReorderPush((const void*)*payload, seq);
    }

    inline int32_t TryPush(QElementType& payload, int index)
    {
        return ConcurrentQueueBase::TryPush((const void*)&payload, index);
    }

    inline int32_t BroadCast(QElementType& payload)
    {
        return ConcurrentQueueBase::BroadCast((const void*)&payload);
    }

    inline int32_t TryPop(QElementType& payload)
    {
        return ConcurrentQueueBase::TryPop((void*)&payload);
    }

    inline int32_t TryPop(QElementType& payload, int index)
    {
        return ConcurrentQueueBase::TryPop((void*)&payload, index);
    }

    inline int32_t ReorderPop(QElementType& payload)
    {
        return ConcurrentQueueBase::ReorderPop((void*)&payload);
    }

    void reset_consumer_cursor()
    {
        return ConcurrentQueueBase::reset_consumer_cursor();
    }

    void reset_producer_cursor()
    {
        return ConcurrentQueueBase::reset_producer_cursor();
    }

    void GetStats(std::vector<QueueStats>& stats)
    {
        return ConcurrentQueueBase::GetStats(stats);
    }
};

template<typename QElementType>
struct Task
{
    QElementType    element;
    uint32_t        seq;
    short           dim;
    short           idx;
    uint64_t        total_order_seq_num;
};

class ConnectorBase
{
protected:
    void Delay();
};

template<typename QElementType, int fanout = 1>
class Connector : public ConnectorBase
{
public:
    Connector() : ccq_(nullptr), seq_(0), lb_cursor_(0)
    {
    }

    Connector(ConcurrentQueue<fanout, Task<QElementType>>* ccq) : ccq_(ccq), seq_(0)
    {
    }

    ~Connector() = default;

    void Init(const string& name, uint32_t depth)
    {
        ccq_ = new ConcurrentQueue<fanout, Task<QElementType>>();
        ccq_->Init(name, depth);
    }

    inline int32_t Forward(QElementType& element)
    {
        Task<QElementType> task = {element, 0, 1, 0, pipeline_total_order_seq_num()};
        int32_t ec;
        while (ADK_UNLIKELY((ec = ccq_->TryPush(task)) != ErrorCode::kSuccess))
        {
            Delay();
        }
        return ec;
    }

    inline int32_t Forward(QElementType& element, short dim, short idx)
    {
        assert(fanout == 1);
        Task<QElementType> task = {element, 0, dim, idx, pipeline_total_order_seq_num()};
        int32_t ec;
        while ((ec = ccq_->TryPush(task)) != ErrorCode::kSuccess)
        {
            Delay();
        }
        return ec;
    }

    inline int32_t Forward(QElementType& element, int partition)
    {
        ++seq_;
        Task<QElementType> task;
        task.element = element;
        *(uint64_t*)(&task.seq) = seq_;
        task.total_order_seq_num = pipeline_total_order_seq_num();

        int32_t ec;
        while ((ec = ccq_->TryPush(task, partition)) != ErrorCode::kSuccess)
        {
            Delay();
        }
        return ec;
    }

    inline int32_t ReorderForward(QElementType& element, uint64_t seq)
    {
        assert(fanout == 1);
        Task<QElementType> task = {element, 0, 1, 0, pipeline_total_order_seq_num()};
        return ccq_->ReorderPush(task, seq);
    }

    inline int32_t SequencialForward(QElementType& element)
    {
        Task<QElementType> task = {element, 0, 1, 0, pipeline_total_order_seq_num()};
        return ccq_->Push(task);
    }

    inline int32_t LoadBalanceForward(QElementType& element)
    {
        int32_t ec;
        Task<QElementType> task = {element, 0, 1, 0, pipeline_total_order_seq_num()};
        while ((ec = ccq_->TryPush(task)) != ErrorCode::kSuccess)
        {
            Delay();
        }
        return ec;
    }

    inline int32_t Fanout(QElementType& element, short dim)
    {
        Task<QElementType> task = {element, 0, dim, 0, pipeline_total_order_seq_num()};
        ccq_->reset_producer_cursor();

        while (task.idx != task.dim)
        {
            ++(ccq_->producer_cursor_);
            if (ccq_->producer_cursor_ == fanout)
            {
                ccq_->producer_cursor_ = 0;
            }

            while (ccq_->TryPush(task, ccq_->producer_cursor_) != ErrorCode::kSuccess)
            {
                Delay();
            }

            ++task.idx;
        }
        return ErrorCode::kSuccess;
    }

    inline int32_t Assemble(QElementType& element)
    {
        Task<QElementType> task;
        ccq_->reset_consumer_cursor();

        if (ccq_->TryPop(task, ccq_->consumer_cursor_) != ErrorCode::kSuccess)
        {
            return ErrorCode::kQueueEmpty;
        }

        while (task.idx + 1 != task.dim)
        {
            ++(ccq_->consumer_cursor_);
            if (ccq_->consumer_cursor_ == fanout)
            {
                ccq_->consumer_cursor_ = 0;
            }

            while (ccq_->TryPop(task, ccq_->consumer_cursor_) != ErrorCode::kSuccess)
            {
                Delay();
            }
        }

        element = task.element;
        set_pipeline_total_order_seq_num(task.total_order_seq_num);
        return ErrorCode::kSuccess;
    }

    inline int32_t SequencialReceive(QElementType& element)
    {
        Task<QElementType> task;
        int32_t ec = ccq_->TryPop(task, lb_cursor_);
        if (ec == ErrorCode::kSuccess)
        {
            ++lb_cursor_;
            if (lb_cursor_ == fanout)
            {
                lb_cursor_ = 0;
            }
            element = task.element;
            set_pipeline_total_order_seq_num(task.total_order_seq_num);
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
            set_pipeline_total_order_seq_num(task.total_order_seq_num);
            return ErrorCode::kSuccess;
        }

        if ((ec = ccq_->TryPop(task_)) != ErrorCode::kSuccess)
        {
            return ec;
        }

        element = task_.element;
        set_pipeline_total_order_seq_num(task_.total_order_seq_num);
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
            set_pipeline_total_order_seq_num(task_.total_order_seq_num);
        }
        return ec;
    }

    inline short dim()
    {
        if (fanout == 1)
        {
            return task_.dim;
        }
        else
        {
            return 0;
        }
    }

    inline short idx()
    {
        if (fanout == 1)
        {
            return task_.idx;
        }
        else
        {
            return 0;
        }
    }

    inline uint64_t total_order_seq_num()
    {
        return task_.total_order_seq_num;
    }

    Connector<QElementType, 1>* GetConnectorByIndex(int index)
    {
        ConcurrentQueue<1, Task<QElementType> >* ccq;

        ccq = ccq_->GetProducerByIndex(index);

        Connector<QElementType, 1>* connecotr_temp = new Connector<QElementType, 1>(ccq);
        return connecotr_temp;
    }

    void GetStats(std::vector<QueueStats>& stats)
    {
        ccq_->GetStats(stats);
    }

private:
    ConcurrentQueue<fanout, Task<QElementType>>* ccq_;
    uint32_t                                     seq_;
    uint32_t                                     lb_cursor_;
    Task<QElementType>                           task_;
};

} // adk

#endif // ADK_LOCK_FREE_MSG_QUEUE_H_
