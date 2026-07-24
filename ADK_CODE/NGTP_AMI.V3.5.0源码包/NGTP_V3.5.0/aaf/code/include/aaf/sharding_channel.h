#pragma once
#include <string>

namespace sharding
{
    class ShardingProxy;
    class ShardingAgent;
}

namespace aaf
{

class ShardingChannel
{
public:
    /**
     * @brief 默认构造，实例化对象时，会完成注册回调
     * 
     * @note 需要在 OnFrameworkInitBegin回调结束前完成实例化。
     */
    ShardingChannel();

    /**
     * @brief 分片进程收到主进程的 Response
     * 
     * @param data 消息首地址
     * @param size 消息长度
     * 
     * @note 该回调和分片的高可用OnMessage是同一个线程
     */
    virtual void OnResponse(const void* data, uint32_t size) {}

    /**
     * @brief 分片进程向主进程发送消息
     * 
     * @param data 消息首地址
     * @param size 消息长度
     * @return int32_t aaf::ErrorCode
     */
    int32_t Request(const void* data, uint32_t size);

    /**
     * @brief 主进程收到分片进程的请求消息
     * 
     * @param data 消息首地址
     * @param size 消息长度
     * @param sharding_index 请求来源的分片号
     *
     * @note 该回调由内部高可用主题的发送线程触发，不能长时间阻塞回调
     */
    virtual void OnRequset(const void* data, uint32_t size, int32_t sharding_index) {}

    /**
     * @brief 主进程发送消息给分片进程
     * 
     * @param data 消息首地址
     * @param size 消息长度
     * @param sharding_index 发送到指定的分片上
     * @return int32_t aaf::ErrorCode
     */
    int32_t PostTo(const void* data, uint32_t size, int32_t sharding_index);

private:
    sharding::ShardingProxy* proxy_ = nullptr;
    sharding::ShardingAgent* agent_ = nullptr;

    friend class sharding::ShardingAgent;
};

}   // end of namespace aaf
