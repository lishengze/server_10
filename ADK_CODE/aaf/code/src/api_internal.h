#ifndef AAF_API_INTERNAL_H_
#define AAF_API_INTERNAL_H_

namespace aaf
{
typedef int32_t (*AAFInitHookType)(void*);
extern void RegisterAAFInitHookLoggerReady(AAFInitHookType hook, void* data);
} // aaf

#endif // AAF_API_INTERNAL_H_
