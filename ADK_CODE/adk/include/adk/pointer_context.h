#ifndef ADK_IMPL_POINTER_CONTEXT_H_
#define ADK_IMPL_POINTER_CONTEXT_H_

#include <stdint.h>
#include <assert.h>

namespace adk_impl
{

#define ADK_PTR_CTX_MASK 			(7UL)
#define ADK_PTR_VAL_MASK 			(~ADK_PTR_CTX_MASK)

template<typename PtrType>
inline PtrType get_ptr_val(PtrType ptr)
{
	return (PtrType)((unsigned long)(ptr) & ADK_PTR_VAL_MASK);
}

template<typename PtrType>
inline uint64_t get_ptr_ctx(PtrType ptr)
{
	return ((unsigned long)(ptr) & ADK_PTR_CTX_MASK);
}

inline bool check_alignment(void* ptr)
{
	return (((unsigned long)(ptr) & ADK_PTR_CTX_MASK) == 0);
}

template<typename PtrType>
inline void set_ptr_ctx(PtrType& ptr, uint64_t status)
{
	assert(status < sizeof(char*));
	ptr = (PtrType)((unsigned long)(get_ptr_val(ptr)) | status);
}

} // adk

#endif // ADK_POINTER_CONTEXT_H_
