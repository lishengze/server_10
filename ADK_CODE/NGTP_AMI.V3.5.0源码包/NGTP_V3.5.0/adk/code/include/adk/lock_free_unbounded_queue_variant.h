/**
 * @file       adk/lock_free_unbounded_queue_variant.h
 * @brief      lock free unbounded queue variant
 * @note       SPSC\MPSC
 * @author     zhangwei, zhangwei@archforce.com.cn
 * @date       2017/07/05
 */

#ifndef ADK_IMPL_LOCK_FREE_UNBOUNDED_QUEUE_VARIANT_H_
#define ADK_IMPL_LOCK_FREE_UNBOUNDED_QUEUE_VARIANT_H_

#include "lock_free_queue_variant.h"

#define CHECK_QUEUE_EMPTY(entry_ptr)\
    do\
    {\
        if (ADK_UNLIKELY(ACCESS_ONCE((entry_ptr)->pos) == QUEUE_CURSOR_INVALID_VALUE))\
        {\
            return kQueueEmpty;\
        }\
    }while(false)

#define CHECK_QUEUE_FULL(entry_ptr)\
    do\
    {\
        if (ADK_UNLIKELY(ACCESS_ONCE((entry_ptr)->pos) != QUEUE_CURSOR_INVALID_VALUE))\
        {\
            return kQueueFull;\
        }\
    }while(false)

namespace adk_impl
{

namespace variant
{

struct QNode
{
    QNode*   next;
    QNode*   prev;
    uint64_t node_hold;
    char     queue_buffer[];

    struct VariantEntry* entries()
    {
        return (struct VariantEntry*)queue_buffer;
    }
};

static thread_local uint64_t s_monotonic_index = QUEUE_CURSOR_INVALID_VALUE;

template<typename QElementType>
struct UnboundedQueueIterator
{
    typedef QElementType element_type;

    UnboundedQueueIterator& operator++()
    {
        ++index;
        if (ADK_UNLIKELY(index >= (node_ptr->node_hold + node_size)))
        {
            node_ptr = node_ptr->next;
        }
        return *this;
    }

    UnboundedQueueIterator operator++(int)
    {
        UnboundedQueueIterator local_value = *this;
        ++*this;
        return local_value;
    }

    UnboundedQueueIterator& operator--()
    {
        --index;
        if (ADK_UNLIKELY(index < node_ptr->node_hold))
        {
            node_ptr = node_ptr->prev;
        }
        return *this;
    }

    UnboundedQueueIterator operator--(int)
    {
        UnboundedQueueIterator local_value = *this;
        --(*this);
        return local_value;
    }

    operator element_type*()
    {
        if (ADK_UNLIKELY(index >= (node_ptr->node_hold + node_size)))
        {
            return nullptr;
        }

        struct VariantEntry* entry_ptr = ptr_add(node_ptr->entries(), (index&node_mask) << entry_bits);
		if (ADK_UNLIKELY(ACCESS_ONCE(entry_ptr->pos) == QUEUE_CURSOR_INVALID_VALUE))
		{
			return nullptr;
		}

        return (element_type*)(entry_ptr->buffer);
    }

    element_type* operator->()
    {
        return (element_type*)(*this);
    }

    element_type& operator*()
    {
        return *((element_type*)(*this));
    }

    bool operator == (const UnboundedQueueIterator& iter_input) const
    {
        return (index == iter_input.index);
    }

    bool operator != (const UnboundedQueueIterator& iter_input) const
    {
        return (index != iter_input.index);
    }

    bool operator < (const UnboundedQueueIterator& iter_input) const
    {
        return (index < iter_input.index);
    }

    bool operator > (const UnboundedQueueIterator& iter_input) const
    {
        return (index > iter_input.index);
    }

    int64_t operator - (const UnboundedQueueIterator& iter_input) const
    {
        return index - iter_input.index;
    }

    UnboundedQueueIterator& operator += (int32_t adden)
    {
        index += adden;
        while (ADK_UNLIKELY(index >= (node_ptr->node_hold + node_size)))
        {
            node_ptr = node_ptr->next;
        }

        return *this;
    }

    uint64_t index;
    QNode*   node_ptr;
    uint32_t entry_bits;
    uint32_t node_size;
    uint64_t node_mask;
};

template<typename QElementType, ADK_TTPARAM(Derived)>
class UnboundedQueueBase
{
public:
    typedef SPSCQueue<QNode*>     cache_type;
    typedef QElementType          element_type;
    typedef Derived<QElementType> derived_type;
    typedef UnboundedQueueIterator<QElementType>       iterator;

    ~UnboundedQueueBase() = default;

    /**
     * @brief      创建unbounded队列
     *
     * @param[in]  name 队列名
     * @param[in]  node_size 节点队列的大小
     * @param[in]  cache_size cache的存储大小
     * @param[in]  cache_init 初始化申请cache节点队列的个数
     *
     * @return     成功返回对象指针/失败返回NULL
     */
    static derived_type* Create(const std::string& name, uint32_t node_size = 1024, uint32_t cache_size = 10, uint32_t cache_init = 2, uint32_t reserve_size = 0)
    {
        derived_type* unbounded_queue = (derived_type*)aligned_malloc(ADK_CACHE_LINE_SIZE, sizeof(derived_type));
        if (nullptr != unbounded_queue)
        {
            new (unbounded_queue) derived_type();

            if (kSuccess != unbounded_queue->Init(name, std::max<uint32_t>(node_size, 2), cache_size, cache_init, reserve_size))
            {
                unbounded_queue->~derived_type();
                aligned_free(unbounded_queue);
                unbounded_queue = nullptr;
            }
        }
        return unbounded_queue;
    }

    static void Delete(derived_type* const queue)
    {
        if (nullptr != queue)
        {
            queue->Delete();
            queue->~derived_type();
            aligned_free(queue);
        }
    }

    /**
     * @brief      初始化unbounded队列
     *
     * @param[in]  name 队列名
     * @param[in]  node_size 节点队列的大小
     * @param[in]  cache_size cache的存储大小
     * @param[in]  cache_init 初始化申请cache节点队列的个数
     *
     * @return     成功返回kSuccess/失败返回相应的ErrorCode
     */
    int32_t Init(const std::string& name, uint32_t node_size, uint32_t cache_size, uint32_t cache_init, uint32_t reserve_size)
    {
        entry_size_ = VariantEntry::CalcEntrySize(sizeof(element_type) + reserve_size);
        entry_bits_ = GetBits(entry_size_);

        node_size_ = ADK_ROUND_TO_POWER_OF_2(node_size);
        producer_mask_ = node_size_ - 1;
        consumer_mask_ = producer_mask_;
        consumer_bits_ = GetBits(node_size_);

        cache_ = cache_type::Create(name, cache_size);
        if (cache_ == NULL)
        {
            return ErrorCode::kNoMemory;
        }

        uint32_t cache_init_size = cache_init < cache_size ? cache_init : cache_size;
        for (uint32_t i = 0; i < cache_init_size; ++i)
        {
            ADK_CHECK_RET_SUCCESS(cache_->Push(AllocateNewNode()));
        }

        tail_ = QUEUE_CURSOR_INIT_VALUE;
        head_ = QUEUE_CURSOR_INIT_VALUE;
        tail_threshold_ = node_size_;
        
        tail_ptr_ = NewNode();
        tail_ptr_->node_hold = 0;
        tail_ptr_->prev = NULL;
        head_ptr_ = tail_ptr_;
        return ErrorCode::kSuccess;
    }

    void Delete()
    {
        QNode* free_node;
        while (NULL != head_ptr_)
        {
            free_node = head_ptr_;
            head_ptr_ = free_node->next;
            FreeUnusedNode(free_node);
        }

        while (kSuccess == cache_->Pop(free_node))
        {
            FreeUnusedNode(free_node);
        }

        cache_type::Delete(cache_);
        cache_ = NULL;
    }

    /**
     * @brief      向队列中插入一个元素
     *
     * @param[in]  payload将要插入的元素
     *
     * @return     成功时返回kSuccess, 失败返回相应的ErrorCode
     */
    inline int32_t Push(const element_type& payload)
    {
        VariantEntry* entry;
        ADK_CHECK_RET_SUCCESS(AllocEntry(&entry));
        char* const buffer = entry->buffer;
        *(element_type*)(buffer) = payload;
        return PostEntry(entry);
    }

    inline int32_t Push(element_type&& payload)
    {
        VariantEntry* entry;
        ADK_CHECK_RET_SUCCESS(AllocEntry(&entry));
        char* const buffer = entry->buffer;
        *((element_type*)buffer) = std::forward<element_type>(payload);
        return PostEntry(entry);
    }

    /**
     * @brief      从队列中获取一个元素
     *
     * @param[out] & payload出队元素
     *
     * @return     成功时返回kSuccess, 失败返回相应的ErrorCode
     */
    inline int32_t Pop(element_type& payload)
    {
        VariantEntry* entry;
        ADK_CHECK_RET_SUCCESS(WaitEntry(&entry));
        char* const buffer = entry->buffer;
        payload = std::move(*(element_type*)(buffer));
        return FreeEntry(entry);
    }

    inline int32_t Pop(uint32_t num)
    {
        VariantEntry* entry;
        for (uint32_t index=0; index<num; ++index)
        {
            ADK_CHECK_RET_SUCCESS(WaitEntry(&entry));
            FreeEntry(entry);
        }
        return ErrorCode::kSuccess;   
    }

	const element_type* Head()
	{
		struct VariantEntry* entry_ptr = ptr_add(head_ptr_->entries(), (head_ & consumer_mask_) << entry_bits_);
		if (QUEUE_CURSOR_INVALID_VALUE == entry_ptr->pos)
		{
			return NULL;
		}
		return (element_type*)(entry_ptr->buffer);
	}

	/**
	* @brief      当前的队列长度，即队列中携带队列元素的Entry数量
	*
	* @return     队列长度
	*/
    inline uint64_t length()
    {
        const uint64_t head = head_;
        ADK_BARRIER();
        const uint64_t tail = tail_;
        return tail - head;
    }

    inline uint64_t last_push_sqn() const
    {
        return tail_ - QUEUE_CURSOR_INIT_VALUE;
    }

    inline uint64_t last_pop_sqn() const
    {
        return head_ - QUEUE_CURSOR_INIT_VALUE;
    }

    iterator Begin()
    {
        return{ head_, head_ptr_, entry_bits_, node_size_, consumer_mask_ };
    }

    iterator End()
    {
        return{ tail_, tail_ptr_, entry_bits_, node_size_, consumer_mask_ };
    }

    void Clear()
    {
        QNode* free_node;
        while (NULL != head_ptr_)
        {
            free_node = head_ptr_;
            head_ptr_ = free_node->next;
            DeleteNode(free_node);
        }

        tail_ = QUEUE_CURSOR_INIT_VALUE;
        head_ = QUEUE_CURSOR_INIT_VALUE;
        tail_threshold_ = node_size_;
        
        tail_ptr_ = NewNode();
        tail_ptr_->node_hold = 0;
        tail_ptr_->prev = NULL;
        head_ptr_ = tail_ptr_;
    }

protected:
    UnboundedQueueBase()
        :   tail_ptr_(NULL),
            tail_(0),
            tail_threshold_(0),
            head_ptr_(NULL),
            head_(0),
            cache_(NULL),
            producer_mask_(0),
            consumer_mask_(0),
            consumer_bits_(0),
            node_size_(0)
    {
    }

    inline int32_t SP_AllocEntry(struct VariantEntry** entry_pptr)
    {
        uint64_t tail_index = tail_ & producer_mask_;
        *entry_pptr = ptr_add(tail_ptr_->entries(), tail_index << entry_bits_);
        
        if (ADK_UNLIKELY(tail_index == producer_mask_))
        {
            tail_ptr_->next = NewNode();
            if (ADK_UNLIKELY(NULL == tail_ptr_->next))
            {
                return ErrorCode::kNoMemory;
            }
            tail_ptr_->next->node_hold = tail_ptr_->node_hold + node_size_;
            tail_ptr_ = tail_ptr_->next;
        }
        return kSuccess;
    }

    inline int32_t SP_PostEntry(struct VariantEntry* entry_ptr)
    {
        ADK_BARRIER();
        entry_ptr->pos = tail_++;
        return kSuccess;
    }

    inline int32_t SC_WaitEntry(struct VariantEntry** entry_pptr)
    {
        struct VariantEntry* entry_ptr = ptr_add(head_ptr_->entries(), (head_ & consumer_mask_) << entry_bits_);
        CHECK_QUEUE_EMPTY(entry_ptr);

        *entry_pptr = entry_ptr;
        return kSuccess;
    }

    inline int32_t SC_FreeEntry(struct VariantEntry* entry_ptr)
    {
        entry_ptr->pos = QUEUE_CURSOR_INVALID_VALUE;
        ADK_BARRIER();

        if (ADK_UNLIKELY((head_++ & consumer_mask_) == consumer_mask_))
        {
            while (ADK_UNLIKELY(NULL == head_ptr_->next))
            {
                ADK_PAUSE();
            }

            QNode* unused_node = head_ptr_;
            head_ptr_ = head_ptr_->next;
            DeleteNode(unused_node);
        }
        return kSuccess;
    }

    inline int32_t MP_AllocEntry(struct VariantEntry** entry_pptr)
    {
        s_monotonic_index = __sync_fetch_and_add(&tail_, 1);
        uint64_t tail_index = s_monotonic_index & producer_mask_;
        
        /*wait for another thread to alloc memory*/
        while (ADK_UNLIKELY(s_monotonic_index >= ACCESS_ONCE(tail_threshold_)))
        {
            ADK_PAUSE();
        }

        /*searching the node in range*/
        QNode* tail_ptr_temp = tail_ptr_;
        while (ADK_UNLIKELY(s_monotonic_index < tail_ptr_temp->node_hold))
        {
            tail_ptr_temp = tail_ptr_temp->prev;
        }        

        *entry_pptr = ptr_add(tail_ptr_temp->entries(), tail_index << entry_bits_);

        if (ADK_UNLIKELY(tail_index == producer_mask_))
        {
            QNode* node_ptr = NewNode();
            if (ADK_UNLIKELY(NULL == node_ptr))
            {
                return ErrorCode::kNoMemory;
            }

            node_ptr->node_hold = tail_ptr_->node_hold + node_size_;
            node_ptr->prev = tail_ptr_;
            tail_ptr_->next = node_ptr;
            tail_ptr_ = node_ptr;
            ADK_BARRIER();
            tail_threshold_ += node_size_;
        }
        return kSuccess;
    }

    inline int32_t MP_PostEntry(struct VariantEntry* entry_ptr)
    {
        ADK_BARRIER();
        entry_ptr->pos = s_monotonic_index;
        return kSuccess;
    }

    inline element_type* UnsafeAbsAt(uint64_t index)
    {
        const uint64_t nr_head = (head_ & (~consumer_mask_)) >> consumer_bits_;
        const uint64_t nr_nodes = (index & (~consumer_mask_)) >> consumer_bits_;
        const uint64_t inner_index = index & consumer_mask_;

        QNode* tmp_node = head_ptr_;
        for (uint64_t i = nr_head; i < nr_nodes; ++i)
        {
            tmp_node = tmp_node->next;
        }

        struct VariantEntry* entry_ptr = ptr_add(tmp_node->entries(), inner_index << entry_bits_);
        return (element_type*)(entry_ptr->buffer);
    }

    /**
    * @brief      获取指定绝对位置的队列元素的引用
    *
    * @param[in]  index 要获取元素在队列中的位置
    *
    * @param[out] * ref_element指向该元素的指针
    *
    * @return     成功时返回kSuccess, 失败返回相应的ErrorCode
    */
    element_type* ElementAbsAt(uint64_t index)
    {
        if (ADK_UNLIKELY(index < head_))
        {
            return (element_type*)(-1);
        }

        if (ADK_UNLIKELY(index >= tail_threshold_))
        {
            return NULL;
        }

        return UnsafeAbsAt(index);
    }

    inline const element_type* UnsafeAt(uint64_t index)
    {
        return UnsafeAbsAt(head_ + index);
    }

    inline const element_type* ElementAt(uint64_t index)
    {
        uint64_t index_in_queue = head_ + index;
        if (index_in_queue >= tail_)
        {
            return NULL;
        }

        return UnsafeAbsAt(index_in_queue);
    }

private:
    inline int32_t AllocEntry(struct VariantEntry** entry_pptr)
    {
        return static_cast<derived_type*>(this)->AllocEntry(entry_pptr);
    }

    inline int32_t PostEntry(struct VariantEntry* entry)
    {
        return static_cast<derived_type*>(this)->PostEntry(entry);
    }

    inline int32_t WaitEntry(struct VariantEntry** entry_pptr)
    {
        return static_cast<derived_type*>(this)->WaitEntry(entry_pptr);
    }

    inline int32_t FreeEntry(struct VariantEntry* entry)
    {
        return static_cast<derived_type*>(this)->FreeEntry(entry);
    }

    struct QNode* NewNode()
    {
        QNode* new_node;
        if (kSuccess != cache_->Pop(new_node))
        {
            return AllocateNewNode();
        }
        new_node->next = NULL;
		ADK_BARRIER();
        return new_node;
    }

    void DeleteNode(QNode* node)
    {
        if (ADK_UNLIKELY(ErrorCode::kSuccess != cache_->Push(node)))
        {
            FreeUnusedNode(node);
        }
    }

    void FreeUnusedNode(QNode* node)
    {
        if (std::is_class<element_type>::value)
        {
            struct VariantEntry* current_entry = node->entries();
            char* const buffer = current_entry->buffer;
            ((element_type*)(buffer))->~element_type();
            for (uint32_t i = 1; i < node_size_; ++i)
            {
                current_entry = ptr_add(current_entry, entry_size_);
                char* const buffer = current_entry->buffer;
                ((element_type*)(buffer))->~element_type();
            }
        }
        aligned_free(node);
    }

    struct QNode* AllocateNewNode()
    {
        uint32_t total_size = sizeof(struct QNode) + (node_size_ << entry_bits_);
        struct QNode* new_node = (struct QNode*)aligned_malloc(ADK_CACHE_LINE_SIZE, total_size);
        if (NULL == new_node)
        {
            return NULL;
        }

        new_node->next = NULL;

        struct VariantEntry* current_entry = new_node->entries();
        current_entry->pos = QUEUE_CURSOR_INVALID_VALUE;
        if (std::is_class<element_type>::value)
        {
            new (current_entry->buffer) element_type();
        }

        for (uint32_t i = 1; i < node_size_; ++i)
        {
            current_entry = ptr_add(current_entry, entry_size_);
            current_entry->pos = QUEUE_CURSOR_INVALID_VALUE;
            if (std::is_class<element_type>::value)
            {
                new (current_entry->buffer) element_type();
            }
        }
        return new_node;
    }

    alignas(ADK_CACHE_LINE_SIZE) QNode*   tail_ptr_;

    uint64_t tail_/* __attribute__((aligned(ADK_CACHE_LINE_SIZE)))*/;
    uint64_t tail_threshold_/* __attribute__((aligned(ADK_CACHE_LINE_SIZE)))*/;

    alignas(ADK_CACHE_LINE_SIZE) QNode*   head_ptr_;

    uint64_t head_ /* __attribute__((aligned(ADK_CACHE_LINE_SIZE)))*/;

    alignas(ADK_CACHE_LINE_SIZE) cache_type* cache_;

    uint64_t producer_mask_;
    uint64_t consumer_mask_;
    uint64_t consumer_bits_;

    uint32_t node_size_;
    uint32_t entry_size_;
    //设定entry_size为2的N次方
    uint32_t entry_bits_;
};

template<typename QElementType>
class SPSCUnboundedQueue : public UnboundedQueueBase<QElementType, SPSCUnboundedQueue>
{
public:
    typedef QElementType element_type;
    typedef UnboundedQueueBase<QElementType, SPSCUnboundedQueue> base_class;

    inline int32_t AllocEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::SP_AllocEntry(entry_pptr);
    }

    inline int32_t PostEntry(struct VariantEntry* entry)
    {
        return base_class::SP_PostEntry(entry);
    }

    inline int32_t WaitEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::SC_WaitEntry(entry_pptr);
    }

    inline int32_t FreeEntry(struct VariantEntry* entry)
    {
        return base_class::SC_FreeEntry(entry);
    }

    inline element_type* UnsafeAbsAt(uint64_t index)
    {
        return base_class::UnsafeAbsAt(index);
    }

    inline element_type* ElementAbsAt(uint64_t index)
    {
        return base_class::ElementAbsAt(index);
    }

    inline const element_type* UnsafeAt(uint64_t index)
    {
        return base_class::UnsafeAt(index);
    }

    inline const element_type* ElementAt(uint64_t index)
    {
        return base_class::ElementAt(index);
    }
};

template<typename QElementType>
class MPSCUnboundedQueue : public UnboundedQueueBase<QElementType, MPSCUnboundedQueue>
{
public:
    typedef QElementType element_type;
    typedef UnboundedQueueBase<QElementType, MPSCUnboundedQueue> base_class;

    inline int32_t AllocEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::MP_AllocEntry(entry_pptr);
    }

    inline int32_t PostEntry(struct VariantEntry* entry)
    {
        return base_class::MP_PostEntry(entry);
    }

    inline int32_t WaitEntry(struct VariantEntry** entry_pptr)
    {
        return base_class::SC_WaitEntry(entry_pptr);
    }

    inline int32_t FreeEntry(struct VariantEntry* entry)
    {
        return base_class::SC_FreeEntry(entry);
    }
};

} //variant
} //adk

#endif