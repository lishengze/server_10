#include <adk/util.h>
#include <adk/lock_free_queue_variant.h>

#include <iostream>

namespace adk_impl
{

namespace variant
{

uint32_t PtrQueueBase::s_entry_size_limit_ = (PtrEntry::PtrEntrySize() - sizeof(PtrEntry))/sizeof(void*);

PtrQueueBase::PtrQueueBase()
    :   queue_header_(NULL),
        entries_(NULL),
        queue_mask_(0),
        queue_size_(0),
        entry_bits_(0),
        release_alert_(false),
        head_threshold_(QUEUE_CURSOR_INIT_VALUE),
        tail_threshold_(QUEUE_CURSOR_INIT_VALUE)
{
    ptr_bits_ = GetBits(sizeof(void*));
}

PtrQueueBase::~PtrQueueBase()
{
    
}

int32_t PtrQueueBase::Init(struct VariantQueueHeader* header)
{
    assert(NULL != header);

    queue_header_ = header;
    entries_ = (struct PtrEntry*)(header->entries());
    queue_size_ = header->queue_size;
    queue_mask_ = header->queue_mask;
    entry_bits_ = GetBits(header->entry_size);
    head_threshold_ = header->tail;
    tail_threshold_ = header->head + queue_size_;
    return kSuccess;
}
 
PtrQueueBase *PtrQueueBase::Create(struct VariantQueueHeader* header)
{
    PtrQueueBase* queue_base = (PtrQueueBase*)malloc(sizeof(PtrQueueBase));
    if (NULL != queue_base)
    {
        new (queue_base) PtrQueueBase();
        queue_base->Init(header);
    }
    
    return queue_base;
}

PtrQueueBase *PtrQueueBase::Create(const std::string &name, uint32_t queue_size)
{
    //PtrQueueBase->VariantQueueHeader->...->ptrentry

    queue_size = ADK_ROUND_TO_POWER_OF_2(queue_size);

    //每个元素占用内存大小
    uint32_t entry_size = PtrEntry::PtrEntrySize();
    const auto queue_header_size = std::max(ADK_PAGE_SIZE, sizeof(PtrQueueBase) + sizeof(VariantQueueHeader));
    uint64_t total_size = queue_header_size + queue_size * entry_size;
    PtrQueueBase* queue_base = (PtrQueueBase*)aligned_malloc(ADK_PAGE_SIZE, total_size);
    if (NULL == queue_base)
    {
        return NULL;
    }

    new (queue_base) PtrQueueBase();

    VariantQueueHeader* queue_header = (VariantQueueHeader*)(queue_base + 1);
    NameCopy(queue_header->queue_name, name);
    queue_header->entry_size = entry_size;
    queue_header->queue_mask = queue_size - 1;
    queue_header->queue_size = queue_size;
    queue_header->queue_entry_offset = ptr_diff(ptr_add(queue_base, queue_header_size), queue_header);
    queue_header->tail = QUEUE_CURSOR_INIT_VALUE;
    queue_header->head = QUEUE_CURSOR_INIT_VALUE;

    InitEntries(queue_header);
    queue_base->Init(queue_header);
    return queue_base;
}

struct PtrEntry* PtrQueueBase::InitEntries(struct VariantQueueHeader* header)
{
    assert(NULL != header);

    uint32_t entry_num = header->queue_size;
    uint32_t entry_size = header->entry_size;
    struct PtrEntry* current_entry = (struct PtrEntry*)(header->entries());
    for (uint32_t i = 0; i < entry_num; ++i)
    {
        current_entry->size = PTR_ENTRY_EMPTY;
        current_entry = ptr_add(current_entry, entry_size);
    }

    return current_entry;
}

PtrQueueBase *PtrQueueBase::Duplicate(PtrQueueBase& queue)
{
   return Create(queue.queue_header_);
}

PtrQueueBase *PtrQueueBase::Duplicate()
{
    return Duplicate(*this);
}

int32_t PtrQueueBase::Delete(PtrQueueBase* queue_base)
{
    assert(NULL != queue_base);

    queue_base->~PtrQueueBase();

    /*
    VariantQueueHeader* queue_hear = queue_base->queue_header_;
    if (queue_hear == (VariantQueueHeader*)(queue_base + 1))
    {
        free(queue_base);
    }
    else
    {
        free(queue_base);
        free(queue_base);
    }
    */

    free(queue_base);
    return kSuccess;
}

}// variant 

}// adk