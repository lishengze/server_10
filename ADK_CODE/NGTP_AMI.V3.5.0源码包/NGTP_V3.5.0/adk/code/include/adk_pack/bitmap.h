/** 
*  Copyright (c) 2022 Archforce Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
* */

#pragma once

#include <stdint.h>

namespace adk
{

class BitMap
{
public:
    BitMap() = delete;
    ~BitMap() = delete;
    /**
     * @brief      构造BitMap对象
     *
     * @param[in]  nr_valid_bits  BitMap的大小为 2^nr_valid_bits，其中 nr_valid_bits 目前限制在8~32范围
     */
    static BitMap* Create(uint32_t nr_valid_bits);

    /**
     * @brief      在指定内存地址构造BitMap对象
     *
     * @param[in]  addr           准备构造的地址
     * @param[in]  nr_valid_bits  BitMap的大小为 2^nr_valid_bits，其中 nr_valid_bits 目前限制在8~32范围
     */
    static BitMap* Create(void* addr, uint32_t nr_valid_bits);


    /**
     * @brief      销毁BitMap对象
     *
     * @param[in]  inst           准备销毁的Bitmap对象实例的地址
     */
    static void Destroy(BitMap* inst);

    /**
     * @brief      设置第pos个比特位
     *
     * @param[in]  pos   比特的编号
     * 
     * @note 该接口线程不安全
     */
    void SetUnsafe(uint64_t pos);

    /**
     * @brief      设置第pos个比特位
     *
     * @param[in]  pos   比特的编号
     * 
     * @note 该接口线程安全
     */
    void Set(uint64_t pos);

    /**
     * @brief      清理第pos个比特位
     *
     * @param[in]  pos   比特的编号
     * 
     * @note 该接口线程不安全
     */
    void ClearUnsafe(uint64_t pos);

    /**
     * @brief      清理第pos个比特位
     *
     * @param[in]  pos   比特的编号
     * 
     * @note 该接口线程安全
     */
    void Clear(uint64_t pos);

    /**
     * @brief      设置从 start 开始，len 个比特位，即区间为 [start, start + len)
     *
     * @param[in]  start  区间开始，第start个比特位
     * @param[in]  len    区间长度
     */
    void SetRange(uint64_t start, uint64_t len);

    /**
     * @brief      清理从 start 开始，len 个比特位，即区间为 [start, start + len)
     *
     * @param[in]  start  区间开始，第start个比特位
     * @param[in]  len    区间长度
     */
    void ClearRange(uint64_t start, uint64_t len);

    /**
     * @brief      获取第pos位置的比特位
     *
     * @param[in]  pos   比特位的位置
     *
     * @return     返回pos的比特位
     * 
     * @note       如果要判断比特位是否被只为可以使用表达式 if (Get(x) != 0)
     */
    uint64_t Get(uint64_t pos);  
};

}
