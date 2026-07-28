/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_MESSAGE_POOL_VARIANT_H_
#define ADK_MESSAGE_POOL_VARIANT_H_

#include "lock_free_queue_variant.h"

#include <assert.h>

#include <map>
#include <utility>

namespace adk
{

namespace variant
{

class MemoryPoolSPSC
{
public:
    static MemoryPoolSPSC* Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property);

    static void Delete(MemoryPoolSPSC* memory_pool);

    void* NewMemoryBlock(size_t length);

    void* NewMemoryNonblock(size_t length);

    void DeleteMemory(void* buffer);
};

class MemoryPoolSPMC
{
public:
    static MemoryPoolSPMC* Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property);

    static void Delete(MemoryPoolSPMC* memory_pool);

    void* NewMemoryBlock(size_t length);

    void* NewMemoryNonblock(size_t length);

    void DeleteMemory(void* buffer);
};

class MemoryPoolMPSC
{
public:
    static MemoryPoolMPSC* Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property);

    static void Delete(MemoryPoolMPSC* memory_pool);

    void* NewMemoryBlock(size_t length);

    void* NewMemoryNonblock(size_t length);

    void DeleteMemory(void* buffer);
};

class MemoryPoolMPMC
{
public:
    static MemoryPoolMPMC* Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property);

    static void Delete(MemoryPoolMPMC* memory_pool);

    void* NewMemoryBlock(size_t length);

    void* NewMemoryNonblock(size_t length);

    void DeleteMemory(void* buffer);
};

template<template<typename> class CacheQueue>
class MemoryPool
{
public:
    static MemoryPool* Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property)
    {
        assert(false);
        return nullptr;
    }

    static void Delete(MemoryPool* memory_pool)
    {
        assert(false);
    }

    template<bool block = false>
    void* NewMemory(size_t length)
    {
        assert(false);
        return nullptr;
    }

    void DeleteMemory(void* buffer)
    {
        assert(false);
    }
};

template<>
class MemoryPool<adk::variant::SPSCQueue>
{
public:
    using this_type = MemoryPool<adk::variant::SPSCQueue>;
    using Implement = MemoryPoolSPSC;

    static this_type* Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property)
    {
        return (this_type*)(Implement::Create(memory_pool_property));
    }

    static void Delete(this_type* memory_pool)
    {
        Implement::Delete((Implement*)memory_pool);
    }

    template<bool block = false>
    inline void* NewMemory(size_t length)
    {
        if (block)
        {
            return ((Implement*)this)->NewMemoryBlock(length);
        }
        else
        {
            return ((Implement*)this)->NewMemoryNonblock(length);
        }
    }

    inline void DeleteMemory(void* buffer)
    {
        ((Implement*)this)->DeleteMemory(buffer);
    }
};

template<>
class MemoryPool<adk::variant::SPMCQueue>
{
public:
    using this_type = MemoryPool<adk::variant::SPMCQueue>;
    using Implement = MemoryPoolSPMC;

    static this_type* Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property)
    {
        return (this_type*)(Implement::Create(memory_pool_property));
    }

    static void Delete(this_type* memory_pool)
    {
        Implement::Delete((Implement*)memory_pool);
    }

    template<bool block = false>
    inline void* NewMemory(size_t length)
    {
        if (block)
        {
            return ((Implement*)this)->NewMemoryBlock(length);
        }
        else
        {
            return ((Implement*)this)->NewMemoryNonblock(length);
        }
    }

    inline void DeleteMemory(void* buffer)
    {
        ((Implement*)this)->DeleteMemory(buffer);
    }
};

template<>
class MemoryPool<adk::variant::MPSCQueue>
{
public:
    using this_type = MemoryPool<adk::variant::MPSCQueue>;
    using Implement = MemoryPoolMPSC;

    static this_type* Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property)
    {
        return (this_type*)(Implement::Create(memory_pool_property));
    }

    static void Delete(this_type* memory_pool)
    {
        Implement::Delete((Implement*)memory_pool);
    }

    template<bool block = false>
    inline void* NewMemory(size_t length)
    {
        if (block)
        {
            return ((Implement*)this)->NewMemoryBlock(length);
        }
        else
        {
            return ((Implement*)this)->NewMemoryNonblock(length);
        }
    }

    inline void DeleteMemory(void* buffer)
    {
        ((Implement*)this)->DeleteMemory(buffer);
    }
};

template<>
class MemoryPool<adk::variant::MPMCQueue>
{
public:
    using this_type = MemoryPool<adk::variant::MPMCQueue>;
    using Implement = MemoryPoolMPMC;

    static this_type* Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property)
    {
        return (this_type*)(Implement::Create(memory_pool_property));
    }

    static void Delete(this_type* memory_pool)
    {
        Implement::Delete((Implement*)memory_pool);
    }

    template<bool block = false>
    inline void* NewMemory(size_t length)
    {
        if (block)
        {
            return ((Implement*)this)->NewMemoryBlock(length);
        }
        else
        {
            return ((Implement*)this)->NewMemoryNonblock(length);
        }
    }

    inline void DeleteMemory(void* buffer)
    {
        ((Implement*)this)->DeleteMemory(buffer);
    }
};
}

}

#endif