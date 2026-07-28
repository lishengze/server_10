#ifndef ADK_IMPL_WRITER_WAIT_FREE_RING_BUFFER_H_
#define ADK_IMPL_WRITER_WAIT_FREE_RING_BUFFER_H_

#include <string>

#include "shm.h"
#include "constant.h"
#include "seq_lock.h"
#include "lock_free_queue_variant.h"

namespace adk_impl
{

template<typename LockType>
struct WWFEntry
{
    LockType seq_lock;
    char     buffer[];
    static uint32_t CalcEntrySize(uint32_t payload_size)
    {
        uint32_t entry_size = ADK_ROUND_TO_POWER_OF_2(payload_size + sizeof(struct WWFEntry<LockType>));
        return entry_size;
        //return entry_size > ADK_CACHE_LINE_SIZE ? entry_size : ADK_CACHE_LINE_SIZE;
    }
};

struct WWFRingBufferHeader
{
    uint32_t    entry_size;
    uint32_t    ring_buffer_mask;
    uint32_t    ring_buffer_size;
    // offset from the header
    uint32_t    ring_buffer_entry_offset;
	int32_t     notify;
    uint64_t    tail __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t    head __attribute__((aligned(ADK_CACHE_LINE_SIZE)));

    void* entries()
    {
        return (void*)(ptr_add(this, ring_buffer_entry_offset)); 
    }
};

template<typename QElementType, typename LockType=SeqLock>
class WWFRingBuffer
{
public:
    typedef QElementType               element_type;
    typedef LockType                   lock_type;
    typedef struct WWFEntry<lock_type> entry_type;
    typedef WWFRingBuffer<element_type,lock_type> this_class;

    WWFRingBuffer() : entries_(NULL), 
                      ring_buffer_header_(NULL), 
                      ring_buffer_mask_(0),
                      ring_buffer_size_(0),
                      entry_bits_(0)
    {}

    static this_class *Create(uint32_t ring_buffer_size)
    {
		this_class* ring_buffer = (this_class*)malloc(sizeof(this_class));
		if (NULL == ring_buffer)
		{
			return NULL;
		}

		new (ring_buffer) this_class();

        const uint32_t entry_size = entry_type::CalcEntrySize(sizeof(element_type));
        const uint32_t ring_buffer_size_create = ADK_ROUND_TO_POWER_OF_2(ring_buffer_size);
        const uint32_t ring_buffer_header_size = ADK_ROUND_UP(sizeof(WWFRingBufferHeader), ADK_PAGE_SIZE);
        const uint64_t total_size = ring_buffer_header_size + ring_buffer_size_create * entry_size;
		WWFRingBufferHeader* ring_buffer_header = (WWFRingBufferHeader*)aligned_malloc(ADK_PAGE_SIZE, (size_t)total_size);
        if (NULL == ring_buffer_header)
        {
            ring_buffer->~this_class();
			free(ring_buffer);
            return NULL;
        }

        //fill ring_buffer header
        ring_buffer_header->entry_size = entry_size;
        ring_buffer_header->ring_buffer_mask = ring_buffer_size_create - 1;
        ring_buffer_header->ring_buffer_size = ring_buffer_size_create;
        ring_buffer_header->ring_buffer_entry_offset = ring_buffer_header_size;
		ring_buffer_header->notify = 0;
        ring_buffer_header->tail = 0;
        ring_buffer_header->head = 0;
        InitEntries(ring_buffer_header);
        
        ring_buffer->Init(ring_buffer_header);
        return ring_buffer;
    }

    int32_t Init(struct WWFRingBufferHeader* header)
    {
        assert(NULL != header);
        ring_buffer_header_ = header;

        entries_ = (entry_type*)(header->entries());
        ring_buffer_size_ = header->ring_buffer_size;
        ring_buffer_mask_ = header->ring_buffer_mask;
        entry_bits_ = GetBits(header->entry_size);
        ring_buffer_size_bits_ = GetBits(ring_buffer_size_);
        return kSuccess;
    }

    this_class *Duplicate()
    {
        this_class* ring_buffer = (this_class*)malloc(sizeof(this_class));
		if (NULL == ring_buffer)
		{
			return NULL;
		}

        new (ring_buffer) this_class();
        
        ring_buffer->Init(ring_buffer_header_);
        return ring_buffer;
    }

	int32_t* notify()
	{
		return &(ring_buffer_header_->notify);
	}

    int32_t Read(element_type& payload)
    {
        const uint64_t last_loop = (ring_buffer_header_->head >> ring_buffer_size_bits_) + 1;
     
        entry_type* entry = (entry_type*)ptr_add(entries_, (ring_buffer_header_->head & ring_buffer_mask_) << entry_bits_);
        const uint64_t start = (uint64_t)entry->seq_lock.ReadBegin();
        const uint64_t real_loop = (start >> 1);
        
        if (last_loop == real_loop)
        {
            char* const buffer = entry->buffer;
            payload = *((element_type*)buffer);
            
            if (entry->seq_lock.ReadResult(start))
            {
                ++ring_buffer_header_->head;
                return kSuccess;
            }
        }
        else if (last_loop > real_loop)
        {
             //ring buffer is empty
             return kFailure;
        }
  
        //the head pos was covered, throw data
        ThrowEntry();
        return Read(payload);
    }

    element_type* ReadBegin(uint64_t& start)
    {
        const uint32_t kRetryTime = 3;
        const uint64_t last_loop = (ring_buffer_header_->head >> ring_buffer_size_bits_) + 1;
        entry_type* entry = (entry_type*)ptr_add(entries_, (ring_buffer_header_->head & ring_buffer_mask_) << entry_bits_);
        const int64_t try_start = entry->seq_lock.TryReadBegin(kRetryTime);
        if (try_start < 0)
        {
            return NULL;
        }

        start = (const uint64_t)try_start;
        const uint64_t real_loop = (start >> 1);
        if (last_loop == real_loop)
        {
            return ((element_type*)entry->buffer);
        }
        else if (last_loop > real_loop)
        {
            //ring buffer is empty
            return NULL;
        }

        //the head pos was covered, throw data
        ThrowEntry();
        return ReadBegin(start);
    }

    bool ReadResult(uint64_t start)
    {
        entry_type* const entry = (entry_type*)ptr_add(entries_, (ring_buffer_header_->head & ring_buffer_mask_) << entry_bits_);
        if (entry->seq_lock.ReadResult(start))
        {
            ++ring_buffer_header_->head;
            return true;
        }
        return false;
    }

    int32_t TryRead(element_type& payload)
    {
        uint64_t start;
        element_type* buffer = ReadBegin(start);
        if (NULL == buffer)
        {
            return kFailure;
        }

        payload = *buffer;
        if (ReadResult(start))
        {
            return kSuccess;
        }
    
        //the head pos was covered, throw data
        ThrowEntry();
        return TryRead(payload);
    }

    void Write(const element_type& payload)
    {
        entry_type* entry = (entry_type*)ptr_add(entries_, (ring_buffer_header_->tail & ring_buffer_mask_) << entry_bits_);
        entry->seq_lock.WriteBegin();
        char* const buffer = entry->buffer;
        *((element_type*)buffer) = payload;
        ++ring_buffer_header_->tail;
        entry->seq_lock.WriteEnd();
    }

    element_type* WriteBegin()
    {
        entry_type* entry = (entry_type*)ptr_add(entries_, (ring_buffer_header_->tail & ring_buffer_mask_) << entry_bits_);
        entry->seq_lock.WriteBegin();
        return (element_type*)entry->buffer;
    }

    void WriteEnd()
    {
        entry_type* entry = (entry_type*)ptr_add(entries_, (ring_buffer_header_->tail++ & ring_buffer_mask_) << entry_bits_);
        entry->seq_lock.WriteEnd();
    }

    static void RollBack(struct WWFRingBufferHeader* header)
    {
        entry_type* entry_head = (entry_type*)header->entries();
        entry_type* entry = (entry_type*)ptr_add(entry_head, (header->tail & header->ring_buffer_mask) << GetBits(header->entry_size));
        entry->seq_lock.RollBack();
    }

    static void InitEntries(struct WWFRingBufferHeader* header)
    {
        assert(NULL != header);
        const uint32_t entry_num = header->ring_buffer_size;
        const uint32_t entry_size = header->entry_size;
        entry_type* current_entry = (entry_type*)header->entries();
        for (uint32_t i = 0; i < entry_num; ++i)
        {
            //init seq lock
            new (current_entry) entry_type();
            if (std::is_class<element_type>::value)
            {
                new (current_entry->buffer) element_type();
            }
            current_entry = ptr_add(current_entry, entry_size);
        }
    }

private:

    inline void ThrowEntry()
    {
        ring_buffer_header_->head = ring_buffer_header_->tail - ring_buffer_size_ + (ring_buffer_size_ >> 2);
    }

    entry_type*          entries_;
    WWFRingBufferHeader* ring_buffer_header_;
    uint64_t             ring_buffer_mask_;
    uint64_t             ring_buffer_size_;
    //设定entry_size为2的N次方
    uint32_t             entry_bits_;
    uint32_t             ring_buffer_size_bits_;
};

enum SHM_STATUS
{
    kShmStatusInvalid = 0,
    kShmStatusValid = 20170907,
    kShmStatusPause
};

struct ShareRingHeader
{
    char        shm_name[ADK_MAX_NAME_LEN + 1];
    uint32_t    shm_status;
    uint32_t    total_size;
    uint32_t    entry_size;
    uint32_t    each_ring_buf_size;
};

template<typename QElementType, typename LockType=SeqLock>
class ShareWWFRingBuffer
{
public:
    typedef QElementType               element_type;
    typedef LockType                   lock_type;
    typedef struct WWFEntry<lock_type> entry_type;
    typedef WWFRingBuffer<element_type,lock_type> ring_type;
    typedef ShareWWFRingBuffer<element_type,lock_type> this_class;

    ShareWWFRingBuffer() : share_memory_(NULL),
                           shm_header_size_(0),
                           ring_buf_max_num_(0),
                           each_ring_buf_size_(0)
    {}

    static this_class* Create(const std::string& shm_name, uint32_t ring_buf_size, uint32_t ring_buf_num)
    {
        if (shm_name.size() > ADK_MAX_NAME_LEN)
        {
            return NULL;
        }

        this_class* share_ring_buf = new this_class;
        if (NULL == share_ring_buf)
        {
            return NULL;
        }

        const uint32_t entry_size = entry_type::CalcEntrySize(sizeof(element_type));
        const uint32_t ring_buf_size_create = ADK_ROUND_TO_POWER_OF_2(ring_buf_size);
        const uint32_t ring_buf_header_size = ADK_ROUND_UP(sizeof(WWFRingBufferHeader), ADK_PAGE_SIZE);
        const uint32_t each_ring_buf_size = ring_buf_header_size + ring_buf_size_create * entry_size;

        const uint32_t shm_header_size = ADK_ROUND_UP(sizeof(ShareRingHeader), ADK_PAGE_SIZE);
        const uint32_t total_size = shm_header_size + each_ring_buf_size * ring_buf_num;

        void* share_memory = ShmFactory::Create(shm_name, total_size);
        if (NULL != share_memory)
        {
            ShareRingHeader* shm_header = (ShareRingHeader*)share_memory;
            shm_header->shm_status = kShmStatusInvalid;
            strcpy(shm_header->shm_name, shm_name.data());
            shm_header->total_size = total_size;
            shm_header->entry_size = entry_size;
            shm_header->each_ring_buf_size = each_ring_buf_size;

            //loop to fill ring_buffer header
            WWFRingBufferHeader* ring_buffer_header = (WWFRingBufferHeader*)ptr_add(share_memory, shm_header_size);
            for (uint32_t index=0; index < ring_buf_num; ++index)
            {
                ring_buffer_header->entry_size = entry_size;
                ring_buffer_header->ring_buffer_mask = ring_buf_size_create - 1;
                ring_buffer_header->ring_buffer_size = ring_buf_size_create;
                ring_buffer_header->ring_buffer_entry_offset = ring_buf_header_size;
                ring_buffer_header->tail = 0;
                ring_buffer_header->head = 0;
                ring_type::InitEntries(ring_buffer_header);
                ring_buffer_header = ptr_add(ring_buffer_header, each_ring_buf_size);
            }
        }
        else
        {
            share_memory = ShmFactory::Attach(shm_name);
            if (NULL == share_memory)
            {
                delete share_ring_buf;
                return NULL;
            }

            ShareRingHeader* shm_header = (ShareRingHeader*)share_memory;
            if ((shm_header->total_size != total_size) 
                || (shm_header->entry_size != entry_size) 
                || (shm_header->each_ring_buf_size != each_ring_buf_size))
            {
                delete share_ring_buf;
                return NULL;
            }

            //loop to rollback write lock
            WWFRingBufferHeader* ring_buffer_header = (WWFRingBufferHeader*)ptr_add(share_memory, shm_header_size);
            for (uint32_t index=0; index < ring_buf_num; ++index)
            {
                ring_type::RollBack(ring_buffer_header);
                ring_buffer_header = ptr_add(ring_buffer_header, each_ring_buf_size);
            }
        }

        share_ring_buf->share_memory_ = share_memory;
        share_ring_buf->shm_header_size_ = shm_header_size;
        share_ring_buf->ring_buf_max_num_ = ring_buf_num;
        share_ring_buf->each_ring_buf_size_ = each_ring_buf_size;
        ((ShareRingHeader*)share_memory)->shm_status = kShmStatusValid;
        return share_ring_buf;
    }

    static this_class* Attach(const std::string& shm_name)
    {
        void* share_memory = ShmFactory::Attach(shm_name);
        if (NULL == share_memory)
        {
            return NULL;
        }

        ShareRingHeader* shm_header = (ShareRingHeader*)share_memory;
        if ((kShmStatusValid != shm_header->shm_status) && (kShmStatusPause != shm_header->shm_status))
        {
            ShmFactory::Detach(shm_name);
            return NULL;
        }

        this_class* share_ring_buf = new this_class;
        if (NULL == share_ring_buf)
        {
            ShmFactory::Detach(shm_name);
            return NULL;
        }

        const uint32_t shm_header_size = ADK_ROUND_UP(sizeof(ShareRingHeader), ADK_PAGE_SIZE);
        uint32_t ring_buf_num = (shm_header->total_size - shm_header_size)/shm_header->each_ring_buf_size;
        share_ring_buf->share_memory_ = share_memory;
        share_ring_buf->shm_header_size_ = shm_header_size;
        share_ring_buf->ring_buf_max_num_ = ring_buf_num;
        share_ring_buf->each_ring_buf_size_ = shm_header->each_ring_buf_size;
        return share_ring_buf;
    }

	static void Detach(const string& shm_name)
	{
		ShmFactory::Detach(shm_name);
	}

    void Detach(uint32_t shm_status)
    {
        ShareRingHeader* shm_header = (ShareRingHeader*)(share_memory_);
        shm_header->shm_status = shm_status;
        ShmFactory::Detach(shm_header->shm_name);
    }

    uint32_t ShmStatus()
    {
        return ((ShareRingHeader*)share_memory_)->shm_status;
    }

    ring_type* AttachShareWWFRingBuffer(uint32_t index)
    {
        if (index >= ring_buf_max_num_)
        {
            return NULL;
        }

        ring_type* ring_buf = new ring_type;
        if (NULL != ring_buf)
        {
            WWFRingBufferHeader* ring_buf_header = (WWFRingBufferHeader*)ptr_add(share_memory_, shm_header_size_ + index * each_ring_buf_size_);

            if (ADK_UNLIKELY(kSuccess != ring_buf->Init(ring_buf_header)))
            {
                delete ring_buf;
                ring_buf = NULL;
            }
        }
        return ring_buf;
    }

    string GetShmName()
    {
        return ((ShareRingHeader*)share_memory_)->shm_name;
    }

private:
    void*    share_memory_;
    uint32_t shm_header_size_;
    uint32_t ring_buf_max_num_;
    uint32_t each_ring_buf_size_;
};

}



#endif