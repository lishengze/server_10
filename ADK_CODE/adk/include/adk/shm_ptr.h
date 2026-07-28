#ifndef ADK_IMPL_SHM_PTR_H_
#define ADK_IMPL_SHM_PTR_H_

#include <stdint.h>

namespace adk_impl
{

#define ADK_SHM_PTR_OFFSET_BITS     48
#define ADK_SHM_PTR_IDX_BITS        16

#define ADK_SHM_PTR_IDX_SHIFT       ADK_SHM_PTR_OFFSET_BITS

#define ADK_SHM_PTR_OFFSET_MASK     ((1ULL << ADK_SHM_PTR_OFFSET_BITS) - 1UL)
#define ADK_SHM_PTR_IDX_MASK        (((1ULL << ADK_SHM_PTR_IDX_BITS) - 1) << ADK_SHM_PTR_IDX_SHIFT)

struct ShmPointer
{
    // uint32_t offset_low;
    // uint16_t offset_high;
    // uint16_t index;
    uint64_t value;

    uint16_t     mp_index() 
    {
        return (value & ADK_SHM_PTR_IDX_MASK) >> ADK_SHM_PTR_IDX_SHIFT; 
    }

    uint64_t    offset()
    {
        return value & ADK_SHM_PTR_OFFSET_MASK; 
    }

    void make_ptr(uint64_t index, int64_t offset)
    {
        value = (index << ADK_SHM_PTR_OFFSET_BITS) | offset;
    }

    inline void Reset()
    {
        value = 0;
    }
};

static inline uint64_t ptr_diff(void* ptr1, void* ptr2)
{
    return (char*)ptr1 - (char*)ptr2;
}

template<typename T>
inline T ptr_add(T ptr, uint64_t offset)
{
    return (T)((char*)ptr + offset);
}

}


#endif // ADK_SHM_PTR_H_
