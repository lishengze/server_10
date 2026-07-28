/**
 * @file
 * @brief      SPSC, MPSC lock-free unbounded message queue
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017/01/23
 */

#ifndef ADK_IMPL_LOCK_FREE_UNBOUNDED_QUEUE_H_
#define ADK_IMPL_LOCK_FREE_UNBOUNDED_QUEUE_H_

#include "shm_ptr.h"
#include "lock_free_msg_queue.h"

namespace adk_impl
{

template<typename QElementType>
struct QNode
{
    QNode<QElementType>*        next;
    QElementType*               ring;
};

template<typename QElementType>
class SPSCUnboundedQueue
{
public:
    SPSCUnboundedQueue()
    {
        tail_ = 1;
        tail_ptr_ = NULL;

        head_ = 1;
        head_threshold_ = 0;
        head_ptr_ = NULL;
        consumer_mask_ = 0;

        node_size_ = 0;
        producer_mask_ = 0;

        cache_ = NULL;
    }

    ~SPSCUnboundedQueue() {}

    static SPSCUnboundedQueue<QElementType>* Create(const string& name, uint32_t cache_size = 2, uint32_t node_size = 1024, uint32_t reserve_size = 0)
    {
        SPSCUnboundedQueue<QElementType>* queue = new SPSCUnboundedQueue<QElementType>();
        if (queue->Init(name, cache_size, node_size, reserve_size) != ErrorCode::kSuccess)
        {
            return nullptr;
        }
        return queue;
    }

    int32_t Push(QElementType& push_element)
    {
        const uint64_t ring_index = tail_ & producer_mask_;
        tail_ptr_->ring[ring_index] = push_element;

        if (ADK_UNLIKELY(ring_index == producer_mask_))
        {
            tail_ptr_->next = NewNode();
            if (ADK_UNLIKELY(tail_ptr_->next == NULL))
            {
                return ErrorCode::kNoMemory;
            }
            tail_ptr_ = tail_ptr_->next;
        }

        ADK_BARRIER();
        ++tail_;
        return ErrorCode::kSuccess;
    }

    QElementType* AllocEntry()
    {
        const uint64_t ring_index = tail_ & producer_mask_;
        return &(tail_ptr_->ring[ring_index]);
    }

    int32_t PostEntry()
    {
        const uint64_t ring_index = tail_ & producer_mask_;
        if (ADK_UNLIKELY(ring_index == producer_mask_))
        {
            tail_ptr_->next = NewNode();
            if (ADK_UNLIKELY(tail_ptr_->next == NULL))
            {
                return ErrorCode::kNoMemory;
            }
            tail_ptr_ = tail_ptr_->next;
        }

        ADK_BARRIER();
        ++tail_;
        return ErrorCode::kSuccess;
    }

    int32_t Pop(QElementType& pop_element)
    {
        if (head_ >= head_threshold_)
        {
            head_threshold_ = ACCESS_ONCE(tail_);
            if (head_ >= head_threshold_)
            {
                return ErrorCode::kQueueEmpty;
            }
        }

        const uint64_t ring_index = head_ & consumer_mask_;
        pop_element = head_ptr_->ring[ring_index];

        ADK_BARRIER();
        ++head_;
        if (ring_index == consumer_mask_)
        {
            QNode<QElementType>* unused_node = head_ptr_;
            assert(head_ptr_->next != NULL);
            head_ptr_ = head_ptr_->next;
            DeleteNode(unused_node);
        }
        return ErrorCode::kSuccess;
    }

    QElementType* WaitEntry()
    {
        if (head_ >= head_threshold_)
        {
            head_threshold_ = ACCESS_ONCE(tail_);
            if (head_ >= head_threshold_)
            {
                return nullptr;
            }
        }

        const uint64_t ring_index = head_ & consumer_mask_;
        return &(head_ptr_->ring[ring_index]);
    }

    void FreeEntry()
    {
        const uint64_t ring_index = head_ & consumer_mask_;
        ADK_BARRIER();

        ++head_;
        if (ring_index == consumer_mask_)
        {
            QNode<QElementType>* unused_node = head_ptr_;
            assert(head_ptr_->next != NULL);
            head_ptr_ = head_ptr_->next;
            DeleteNode(unused_node);
        }
    }

    QElementType* Head()
    {
        if (!HaveData())
        {
            return NULL;
        }

        return &(head_ptr_->ring[head_ & consumer_mask_]);
    }

    inline int32_t Pop()
    {
        const uint64_t ring_index = head_ & consumer_mask_;
        ADK_BARRIER();

        ++head_;
        if (ring_index == consumer_mask_)
        {
            QNode<QElementType>* unused_node = head_ptr_;
            assert(head_ptr_->next != NULL);
            head_ptr_ = head_ptr_->next;
            DeleteNode(unused_node);
        }
        return ErrorCode::kSuccess;
    }

    QElementType* ElementAt(uint64_t index)
    {
        if (index < head_)
        {
            return NULL;
        }

        if (index >= head_threshold_)
        {
            head_threshold_ = ACCESS_ONCE(tail_);
            if (index >= head_threshold_)
            {
                return NULL;
            }
        }

        const uint64_t head_nodes = (head_ & (~consumer_mask_)) >> consumer_bits_;
        const uint64_t nr_nodes = ((index & (~consumer_mask_)) >> consumer_bits_) - head_nodes;
        const uint64_t ring_index = index & consumer_mask_;
        QNode<QElementType>* tmp_node = head_ptr_;
        for (uint32_t i = 0; i < nr_nodes; ++i)
        {
            tmp_node = tmp_node->next;
        }
        return &(tmp_node->ring[ring_index]);
    }

    /**
     * @brief   foreach element
     *
     * @param   callback
     *          callback(QElementType* element)
     */
    template<typename CallbackType>
    void ForeachElement(const CallbackType& callback)
    {
        uint64_t index = head_;
        head_threshold_ = ACCESS_ONCE(tail_);
        QNode<QElementType>* tmp_node = head_ptr_;

        while (index < head_threshold_)
        {
        traverse:
            assert(tmp_node);
            const uint64_t ring_index = index & consumer_mask_;
            if (ADK_UNLIKELY(!callback(&(tmp_node->ring[ring_index]))))
            {
                return;
            }

            if (ADK_UNLIKELY(consumer_mask_ == ring_index))
            {
                tmp_node = tmp_node->next;
            }

            ++index;
        }

        head_threshold_ = ACCESS_ONCE(tail_);
        if (index < head_threshold_)
        {
            goto traverse;
        }
    }

private:
    QNode<QElementType>*    tail_ptr_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t                tail_;
    uint64_t                producer_mask_;

    uint64_t                head_threshold_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t                head_;
    uint64_t                node_head_;
    QNode<QElementType>*    head_ptr_;
    uint64_t                consumer_mask_;
    uint64_t                consumer_bits_;

    SPSCQueue<QNode<QElementType>* >* cache_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint32_t                node_size_;
    uint32_t                element_size_;

    QNode<QElementType>* NewNode()
    {
        QNode<QElementType>* new_node;
        if (cache_->Pop(new_node) != ErrorCode::kSuccess)
        {
            return AllocateNewNode();
        }
        new_node->next = NULL;
        return new_node;
    }

    QNode<QElementType>* AllocateNewNode()
    {
        QNode<QElementType>* new_node = (QNode<QElementType>*)malloc(sizeof(QNode<QElementType>) + node_size_ * element_size_);
        new_node->next = NULL;
        new_node->ring = (QElementType*)ptr_add(new_node, sizeof(QNode<QElementType>));

        memset(new_node->ring, 0, node_size_ * element_size_);
        for (uint32_t i = 0; i < node_size_; ++i)
        {
            QElementType* buffer = ptr_add(new_node->ring, i * element_size_);
            new ((void*)buffer) QElementType();
        }

        return new_node;
    }

    void DeleteNode(QNode<QElementType>* node)
    {
        if (cache_->Push(node) == ErrorCode::kSuccess)
            return;

        free(node);
    }

    int32_t Init(const string& name, uint32_t cache_size, uint32_t node_size, uint32_t reserve_size)
    {
        node_size_ = ADK_ROUND_TO_POWER_OF_2(node_size);
        producer_mask_ = node_size_ - 1;
        consumer_mask_ = producer_mask_;
        consumer_bits_ = GetBits(node_size_);
        element_size_ = sizeof(QElementType) + reserve_size;

        cache_ = SPSCQueue<QNode<QElementType>*>::Create(name, cache_size);
        if (cache_ == NULL)
            return ErrorCode::kNoMemory;

        for (uint32_t i = 0; i < cache_size; ++i)
        {
            QNode<QElementType>* new_node = AllocateNewNode();
            int32_t ec = cache_->Push(new_node);
            assert(ec == ErrorCode::kSuccess);
            (void) ec;
        }

        head_ptr_ = NewNode();
        tail_ptr_ = head_ptr_;

        return ErrorCode::kSuccess;
    }

    inline bool HaveData()
    {
        if (head_ >= head_threshold_)
        {
            head_threshold_ = ACCESS_ONCE(tail_);
            if (head_ >= head_threshold_)
            {
                return false;
            }
        }
        return true;
    }
};

// FIXME: MPSCUnboundedQueue?

// struct MPSCQueueInfo
// {
//     uint64_t        reserve __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
//     uint64_t        node_tail __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
//     void*           tail_ptr;
//     uint64_t        tail __attribute__((aligned(ADK_CACHE_LINE_SIZE)));    


//     uint64_t        head __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
//     uint64_t        release __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
//     uint64_t        node_head __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
//     void*           head_ptr;
//     void*           cache_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
// };

} // adk

#endif // ADK_LOCK_FREE_UNBOUNDED_QUEUE_H_
