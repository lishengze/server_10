/**
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved.
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.
*  For more information about Archforce, welcome to archforce.cn.
**/

#include <adk/entry_wrapper.h>
#ifdef __GNUC__
#include <sys/prctl.h>
#endif

#ifdef _MSC_VER
#include <windows.h>
const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
	DWORD dwType; // Must be 0x1000.
	LPCSTR szName; // Pointer to name (in user addr space).
	DWORD dwThreadID; // Thread ID (-1=caller thread).
	DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

void SetThreadName(DWORD dwThreadID, const char* threadName) {
	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = threadName;
	info.dwThreadID = dwThreadID;
	info.dwFlags = 0;
#pragma warning(push)
#pragma warning(disable: 6320 6322)
	__try
	{
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
#pragma warning(pop)
}
#endif

namespace adk_impl {

void DoEntryWrapper(const char* short_name, const char* full_name, std::function<void()> f)
{
#ifdef __GNUC__
    prctl(PR_SET_NAME, short_name, 0, 0, 0);
#endif

#ifdef _MSC_VER
    if (full_name)
    {
        std::string   old(full_name);
        SetThreadName(DWORD(-1), full_name);
    }
#endif

    f();
}

void EntryWrapper(std::string* short_name, std::string* full_name, std::function<void()> f)
{
    DoEntryWrapper(short_name->c_str(), full_name->c_str(), f);
    delete short_name;
    delete full_name;
}

std::thread std_thread(const char* short_name, const char* full_name, std::function<void()> f)
{
    std::string* sname = new std::string(short_name);
    std::string* fname = new std::string(full_name);
    return std::thread(EntryWrapper, sname, fname, f);
}

boost::thread boost_thread(const char* short_name, const char* full_name, std::function<void()> f)
{
    std::string* sname = new std::string(short_name);
    std::string* fname = new std::string(full_name);
    return boost::thread(EntryWrapper, sname, fname, f);
}

}