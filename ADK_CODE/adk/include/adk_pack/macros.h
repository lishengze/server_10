/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
**/
#ifndef ADK_MACROS_H_
#define ADK_MACROS_H_

#ifndef ADK_GET_NARG
#define ADK_GET_NARG(...) ADK_GET_NARG_(__VA_ARGS__, ADK_GET_RSEQ_N_ARG())
#endif
#ifndef ADK_GET_NARG_
#define ADK_GET_NARG_(...) ADK_GET_N_ARG(__VA_ARGS__) 
#endif
#ifndef ADK_GET_N_ARG
#define ADK_GET_N_ARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, N, ...) N
#endif
#ifndef ADK_GET_RSEQ_N_ARG
#define ADK_GET_RSEQ_N_ARG() 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
#endif

#ifndef ADK_CONCATENATE
#define ADK_CONCATENATE(arg1, arg2)   ADK_CONCATENATE1(arg1, arg2)
#endif
#ifndef ADK_CONCATENATE1
#define ADK_CONCATENATE1(arg1, arg2)  ADK_CONCATENATE2(arg1, arg2)
#endif
#ifndef ADK_CONCATENATE2
#define ADK_CONCATENATE2(arg1, arg2)  arg1##arg2
#endif

#endif // ADK_MACROS_H_
