/** 
*  Copyright (c) 2022 Archforce Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
* */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>

#include <atomic>

#include "constant.h"
#include "arch/generic.h"

#define ADK_BITMAP_NOALLOC_FLAG 0x1000000000000ul
#define ADK_ADDRESS_MASK        0xFFFFFFFFFFFFul

namespace adk_impl
{

class BitMap
{
private:
    BitMap() = default;     
public:
    ~BitMap() = delete;
    /**
     * @brief      构造BitMap对象
     *
     * @param[in]  nr_valid_bits  BitMap的大小为 2^nr_valid_bits，其中 nr_valid_bits 目前限制在8~32范围
     */
    static BitMap* Create(uint32_t nr_valid_bits)
    {
        if (nr_valid_bits < 8)
        {
            nr_valid_bits = 8;
        }

        if (nr_valid_bits > 32)
        {
            nr_valid_bits = 32;
        }

        auto result = (BitMap*)::memalign(ADK_CACHE_LINE_SIZE, (1ul << nr_valid_bits) / 8);
        return result;
    }

    /**
     * @brief      在指定内存地址构造BitMap对象
     *
     * @param[in]  addr           准备构造的地址
     * @param[in]  nr_valid_bits  BitMap的大小为 2^nr_valid_bits，其中 nr_valid_bits 目前限制在8~32范围
     */
    static BitMap* Create(void* addr, uint32_t nr_valid_bits)
    {
        if (nr_valid_bits < 8)
        {
            nr_valid_bits = 8;
        }

        if (nr_valid_bits > 32)
        {
            nr_valid_bits = 32;
        }
        ::memset(addr, 0, (nr_valid_bits + 7) / 8); //提前验证内存区域有效性
        return (BitMap*)((uint64_t)addr | (ADK_BITMAP_NOALLOC_FLAG));
    }

    static void Destroy(BitMap* inst)
    {
        if(((uint64_t)inst & (ADK_BITMAP_NOALLOC_FLAG)) != 0)
        {
            return;//Not allocated by Create(), do nothing.
        }
        ::free(inst);
    }


    /**
     * @brief      设置第pos个比特位
     *
     * @param[in]  pos   比特的编号
     * 
     * @note 该接口线程不安全
     */
    ADK_ALWAYS_INLINE void SetUnsafe(uint64_t pos)
    {
        uint64_t* base_ = (uint64_t*)((uint64_t)this & ADK_ADDRESS_MASK); 
        uint32_t bit_in_last_word = pos & kWordBitMask;
        uint64_t* p = &base_[pos >> 6];
        *p |= (1ul << bit_in_last_word);
    }

    /**
     * @brief      设置第pos个比特位
     *
     * @param[in]  pos   比特的编号
     * 
     * @note 该接口线程安全
     */
    ADK_ALWAYS_INLINE void Set(uint64_t pos)
    {
        uint64_t* base_ = (uint64_t*)((uint64_t)this & ADK_ADDRESS_MASK); 
        uint32_t bits_in_last_word = pos & kWordBitMask;
        volatile uint64_t* p = &base_[pos >> 6];
        AtomicSet(p, (1ul << bits_in_last_word));
    }

    /**
     * @brief      清理第pos个比特位
     *
     * @param[in]  pos   比特的编号
     * 
     * @note 该接口线程不安全
     */
    ADK_ALWAYS_INLINE void ClearUnsafe(uint64_t pos)
    {
        uint64_t* base_ = (uint64_t*)((uint64_t)this & ADK_ADDRESS_MASK); 
        uint32_t bit_in_last_word = pos & kWordBitMask;
        uint64_t* p = &base_[pos >> 6];
        *p &= ~(1ul << bit_in_last_word);
    }

    /**
     * @brief      清理第pos个比特位
     *
     * @param[in]  pos   比特的编号
     * 
     * @note 该接口线程安全
     */
    ADK_ALWAYS_INLINE void Clear(uint64_t pos)
    {
        uint64_t* base_ = (uint64_t*)((uint64_t)this & ADK_ADDRESS_MASK); 
        uint32_t bits_in_last_word = pos & kWordBitMask;
        volatile uint64_t* p = &base_[pos >> 6];
        AtomicClear(p, (1ul << bits_in_last_word));
    }

    /**
     * @brief      设置从 start 开始，len 个比特位，即区间为 [start, start + len)
     *
     * @param[in]  start  区间开始，第start个比特位
     * @param[in]  len    区间长度
     */
    void SetRange(uint64_t start, uint64_t len)
    {
        uint64_t* base_ = (uint64_t*)((uint64_t)this & ADK_ADDRESS_MASK); 
        uint64_t* p = base_ + (start >> 6);
        const uint64_t end = start + len;
        uint64_t bits_to_set = kPerWordBits - (start & kWordBitMask);
        uint64_t bits_mask = ((~0ul) << (start & kWordBitMask));

        while (len >= bits_to_set)
        {
            AtomicSet(p, bits_mask);
            len -= bits_to_set;
            bits_to_set = kPerWordBits;
            bits_mask = ~0ul;
            ++p;
        }

        if (len > 0)
        {
            bits_mask &= ((~0ul) >> ((-(end)) & kWordBitMask));
            AtomicSet(p, bits_mask);
        }
    }

    /**
     * @brief      清理从 start 开始，len 个比特位，即区间为 [start, start + len)
     *
     * @param[in]  start  区间开始，第start个比特位
     * @param[in]  len    区间长度
     */
    void ClearRange(uint64_t start, uint64_t len)
    {
        uint64_t* base_ = (uint64_t*)((uint64_t)this & ADK_ADDRESS_MASK); 
        uint64_t* p = base_ + (start >> 6);
        const uint64_t end = start + len;
        uint64_t bits_to_set = kPerWordBits - (start & kWordBitMask);
        uint64_t bits_mask = ((~0ul) << (start & kWordBitMask));

        while (len >= bits_to_set)
        {
            AtomicClear(p, bits_mask);
            len -= bits_to_set;
            bits_to_set = kPerWordBits;
            bits_mask = ~0ul;
            ++p;
        }

        if (len > 0)
        {
            bits_mask &= ((~0ul) >> ((-(end)) & kWordBitMask));
            AtomicClear(p, bits_mask);
        }
    }

    /**
     * @brief      获取第pos位置的比特位
     *
     * @param[in]  pos   比特位的位置
     *
     * @return     返回pos的比特位
     * 
     * @note       如果要判断比特位是否被只为可以使用表达式 if (Get(x) != 0)
     */
    ADK_ALWAYS_INLINE uint64_t Get(uint64_t pos)
    {
        uint64_t* base_ = (uint64_t*)((uint64_t)this & ADK_ADDRESS_MASK); 
        return base_[pos >> 6] & (1ul << (pos & kWordBitMask));
    }

private:

    static constexpr int kPerWordBits = 64; 
    static constexpr uint64_t kWordBitMask = 63ul; // 64 - 1

    ADK_ALWAYS_INLINE void AtomicSet(volatile uint64_t* p, uint64_t bits_mask)
    {
        __sync_fetch_and_or(p, bits_mask);
    }

    ADK_ALWAYS_INLINE void AtomicClear(volatile uint64_t* p, uint64_t bits_mask)
    {
        uint64_t clear_mask = ~bits_mask;
        __sync_fetch_and_and(p, clear_mask);
    }
  
};

}
