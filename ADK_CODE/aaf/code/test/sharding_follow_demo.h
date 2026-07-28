#pragma once
#include <stdint.h>

// 应用消息体
struct AppMessage
{
    uint64_t total_order_sqn;
    uint64_t sqn;            // 消息内容序列号
    bool is_core;       // 需要core的消息
    bool is_broadcast_msg = false;  // 需要广播
    char from_ep_name[32];   // 消息来源哪个主题
    uint16_t msg_len;        // 消息内容长度
    char msg[64];            // 消息内容
};