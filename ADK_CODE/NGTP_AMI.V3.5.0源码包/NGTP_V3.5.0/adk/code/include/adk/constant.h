#ifndef ADK_IMPL_CONSTANT_H_
#define ADK_IMPL_CONSTANT_H_

namespace adk_impl
{

#ifndef ADK_CACHE_LINE_SIZE
#if defined(__aarch64__)
#define ADK_CACHE_LINE_SIZE                 128
#else
#define ADK_CACHE_LINE_SIZE                 64
#endif
#endif

#define ADK_MAX_NAME_LEN                    256UL
#define ADK_NAME_LEN_LIMIT                  (ADK_MAX_NAME_LEN - 1)
#define ADK_LAST_NAME_INDEX                 (ADK_MAX_NAME_LEN - 1)

} // adk

#endif // ADK_CONSTANT_H_


