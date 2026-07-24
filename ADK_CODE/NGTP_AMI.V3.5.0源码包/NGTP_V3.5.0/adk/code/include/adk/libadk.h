#ifndef ADK_LIB_ADK_H_
#define ADK_LIB_ADK_H_

#if defined(_MSC_VER) && !defined(_LIB)
#ifdef ADK_EXPORTS
#define ADK_API __declspec(dllexport)
#else
#define ADK_API __declspec(dllimport)
#pragma comment(lib, "adk.lib")
#endif
#else
#define ADK_API
#endif

#endif
