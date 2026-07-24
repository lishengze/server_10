#ifndef ADK_SHM_CHANNEL_H_
#define ADK_SHM_CHANNEL_H_

#include <string>
#include <functional>

namespace adk
{

namespace sccl
{

struct Entry
{
public:
    void* Buffer();

    uint32_t BufferSize() const;

    uint16_t Index() const;
};

class AgentEventHandler
{
public:
    /**
     * @brief   有新的Proxy连接
     *
     * @param   process   proxy名称
     * @param   pid       proxy进程的进程ID
     * @param   tid       proxy线程的线程ID
     *
     * @return  true  : 接受该Proxy的数据
     *          false : 拒绝该Proxy的数据
     */
    virtual bool OnNewProxy(const std::string& process, int32_t pid, int32_t tid) = 0;

    /**
     * @brief   Proxy断开连接
     *
     * @param   process   proxy名称
     * @param   pid       proxy进程的进程ID
     * @param   tid       proxy线程的线程ID
     */
    virtual void OnProxyBroken(const std::string& process, int32_t pid, int32_t tid) = 0;
};

class Agent
{
public:
    static Agent* Create(const std::string& name, 
                         AgentEventHandler* event_handler = nullptr,
                         bool do_recovery = true,
                         uint32_t memory_size = 16 * 1024 * 1024, 
                         uint32_t max_message_size = 1 * 1024 * 1024);

    static void Destroy(Agent* agent);

    struct Entry* TryWaitEntry();

    void FreeEntry(struct Entry* entry_ptr);
};

class Proxy
{
public:
    /**
     * @brief   创建Proxy对象用于数据生产者
     * 
     * @oaram   agent_name  消费者agent的名称
     * @param   proxy_name  proxy的名称
     * @oaram   broken_cb   当agent退出时的回调，如果回调返回true则由Proxy自动发起重连
     * 
     * @return  成功返回对象 / 失败返回nullptr
     */
    static Proxy* Create(const std::string& agent_name, 
                         const std::string& proxy_name = "",
                         const std::function<bool()>& broken_cb = []() { return true; });

    static void Destroy(Proxy* proxy);

    void* AllocBuffer(uint32_t length);

    void PostBuffer(void* buffer, uint32_t buf_size);
};

}

}
#endif