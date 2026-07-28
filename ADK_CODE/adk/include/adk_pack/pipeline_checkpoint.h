/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/
#ifndef ADK_PIPELINE_HELPER_H_
#define ADK_PIPELINE_HELPER_H_

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

namespace adk
{

const uint64_t kShmCheckPointVer=0xABCD00010001DCBA;
/****** PageSize Align ******/
struct ShmCheckPoint
{
    char thread_name[128];
    uint64_t version;
    uint64_t total_order_sqn;
    pid_t thread_id;
    uint32_t business_info_size;

    char business_info[];
};

constexpr uint32_t kShmCheckPointSize = sizeof(ShmCheckPoint);

struct ShmCheckPointHeader
{
    uint32_t nr_checkpoints;
    uint32_t nr_checkpoints_used;

    uint32_t checkpoint_size;
    uint32_t check_points_offset;

    void Initialize(uint32_t nr_chkpoints, uint32_t chkpoint_size)
    {
        nr_checkpoints = nr_chkpoints;
        checkpoint_size = chkpoint_size;
        nr_checkpoints_used = 0;
    }

    ShmCheckPoint* At(uint32_t idx)
    {
        assert(idx < nr_checkpoints);
        assert(checkpoint_size >= kShmCheckPointSize);
        uint32_t offset = check_points_offset + checkpoint_size * idx;
        return (ShmCheckPoint*)((const char*)this + offset);
    }
    
    ShmCheckPoint* NewCheckPoint(const std::string& thread_name)
    {
        assert(nr_checkpoints_used < nr_checkpoints);
        if (nr_checkpoints_used > nr_checkpoints)
        {
            return nullptr;
        }
        ShmCheckPoint* chk_point = At(nr_checkpoints_used);
        ++nr_checkpoints_used;

        strncpy(chk_point->thread_name, thread_name.c_str(), sizeof(ShmCheckPoint::thread_name));
        chk_point->version = kShmCheckPointVer; 
        chk_point->total_order_sqn = 0;
        return chk_point;
    }


};

}

/**
 * @brief parser api for pipeline checkpoint
 * 
 * @param buf bussiness info
 * @param len bussiness info size
 * @return char* text for bussiness description
 */
typedef char* (*PipelineParser)(void* buf, uint32_t len);

#endif
