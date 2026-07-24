# ADK (Application Development Kit) 产品使用指南

## 1. 概述

本手册详细说明如何高效使用 ADK 构建高性能分布式应用。内容包括核心 API 用法、配置说明、常见问题解决等。

---

## 2. 快速开始

### 2.1 最小化示例 - 创建异步 TCP 服务器

```cpp
#include "adk/iot_context.h"
#include "adk/tcp_server.h"
#include "adk/logger.h"

using namespace adk;

class TcpHandler : public IoHandler {
public:
    void OnRead() override {
        // 接收数据
        Buffer buffer(4096);
        ssize_t n = socket_.Recv(buffer.GetWritePtr(), buffer.WritableBytes());
        
        if (n > 0) {
            buffer.HasWritten(n);
            ADK_LOG_INFO("Received ", n, " bytes");
            
            // 回显数据
            socket_.Send(buffer.Peek(), n);
        }
    }
    
    void OnWrite() override {
        ADK_LOG_DEBUG("Socket writable");
    }
    
    void OnError(int err_no) override {
        ADK_LOG_ERROR("Connection error: ", strerror(err_no));
    }
    
private:
    Socket socket_;
};

int main() {
    // 创建 IO 上下文
    IoContext io_ctx;
    io_ctx.Start();
    
    // 创建 TCP Server
    TcpServer server(&io_ctx, "0.0.0.0", 8080);
    
    // 设置连接处理器
    server.SetHandlerFactory([]() -> IoHandler* {
        return new TcpHandler();
    });
    
    // 启动服务
    server.Start();
    
    // 运行直到用户中断
    while (true) {
        usleep(1000000);  // 1 second
    }
    
    return 0;
}
```

编译运行：
```bash
g++ -o tcp_server tcp_server.cpp -ladk -lpthread
./tcp_server
```

---

## 3. 核心 API 使用

### 3.1 Logger 日志系统

#### 3.1.1 基本使用

```cpp
#include "adk/logger.h"

void MyFunction() {
    // 不同级别的日志
    ADK_LOG_TRACE("Trace message: value={1}", value);
    ADK_LOG_DEBUG("Debug message with args: %s, %d", str, num);
    ADK_LOG_INFO("Info level log");
    ADK_LOG_WARN("Warning: potential issue detected");
    ADK_LOG_ERROR("Error occurred: ", error_msg);
    ADK_LOG_FATAL("Fatal error, will exit");
}
```

#### 3.1.2 异步日志配置

```cpp
// 设置日志级别
Logger::Instance().SetLevel(Logger::DEBUG);

// 配置日志输出
LoggerConfig config;
config.log_path = "/var/log/myapp";
config.max_log_size = 100 * 1024 * 1024;  // 100MB
config.max_backup_count = 5;               // 保留 5 个备份文件
config.async_mode = true;                  // 启用异步写入
config.thread_pool_size = 2;               // 使用 2 个日志线程

Logger::Instance().Initialize(config);
```

#### 3.1.3 AC (Activity Context) 追踪

用于跨模块追踪请求链路：

```cpp
#define ADK_LOG_DECLARE_AC(activity_id) adk::AcTracker ac_tracker(activity_id);

void ProcessRequest(Message* msg) {
    ADK_LOG_DECLARE_AC(AC_REQUEST_PROCESSING);
    
    ADK_LOG_INFO_AC_TF("request_start", "req_id=%d", msg->id);
    
    // 业务逻辑...
    
    ADK_LOG_INFO_AC_TF("request_end", "duration=%dus", duration_us);
}
```

---

### 3.2 Property 属性容器

#### 3.2.1 配置加载

```cpp
#include "adk/property.h"

Property config;

// 从字符串解析
config.LoadFromString(R"(
{
    "host": "0.0.0.0",
    "port": 8080,
    "enable_tls": true,
    "max_connections": 1000
}
)");

// 读取配置
std::string host = config.GetValue("host", "localhost");
int port = config.GetValueAsInt("port", 8000);
bool enable_tls = config.GetValueAsBool("enable_tls", false);
int max_conn = config.GetValueAsInt("max_connections", 100);

ADK_LOG_INFO("Listening on ", host, ":", port);
```

#### 3.2.2 配置文件格式

支持 JSON 格式或简单的 key=value 格式:

```json
{
    "database": {
        "host": "localhost",
        "port": 3306,
        "username": "root"
    },
    "server": {
        "bind_address": "0.0.0.0",
        "thread_pool_size": 4
    }
}
```

---

### 3.3 MemPool 内存池

#### 3.3.1 对象池使用

```cpp
#include "adk/mem_pool.h"

// 定义消息结构
struct Message {
    int type;
    char data[1024];
    
    Message() : type(0) {}
    ~Message() {}
};

// 创建内存池（每个块分配 64 个对象）
MemPool<Message, 4096> msg_pool;

// 预分配 100 个对象供初始使用
msg_pool.PreAllocate(100);

// 使用
Message* msg = msg_pool.Acquire();
msg->type = MSG_TYPE_REQUEST;
memcpy(msg->data, buffer, len);

// 处理完后释放
msg_pool.Release(msg);
```

#### 3.3.2 内存池参数调优

```cpp
// 针对高频小对象
MemPool<SmallObj, 4096> small_obj_pool;
small_obj_pool.PreAllocate(1000);  // 预分配更多对象

// 针对大对象
MemPool<LargeObject, 65536> large_obj_pool;
large_obj_pool.PreAllocate(100);   // 较少的大对象
```

---

### 3.4 ThreadPool 线程池

#### 3.4.1 基本使用

```cpp
#include "adk/thread_pool.h"

ThreadPool task_pool(4);  // 4 个工作线程

// 提交同步任务
task_pool.Submit([]() {
    // 后台任务
    ProcessHeavyComputation();
});

// 提交带返回值的任务
auto future = task_pool.Submit([](int x, int y) {
    return x + y;
});

int result = future.Get();  // 等待结果并获取

// 等待所有任务完成
task_pool.WaitForIdle();
```

#### 3.4.2 Lambda 表达式

```cpp
// 捕获局部变量
int counter = 0;
task_pool.Submit([&counter]() {
    for (int i = 0; i < 100; i++) {
        counter++;
    }
});

// 多线程安全访问共享资源
boost::mutex shared_mutex;
task_pool.Submit([&]() {
    boost::mutex::scoped_lock lock(shared_mutex);
    DoSomethingWithSharedData();
});
```

---

### 3.5 EventChannel 事件通道

#### 3.5.1 发布 - 订阅模式

```cpp
#include "adk/event_channel.h"

EventChannel channel;

// 订阅者
channel.Subscribe(EventType::kRoleChange, [](const Event& e) {
    ADK_LOG_INFO("Role changed to: ", e.role);
});

// 发布者
channel.Publish(EventType::kRoleChange, role_event_data);
```

#### 3.5.2 事件队列管理

```cpp
// 设置事件队列大小
channel.SetQueueSize(1000);

// 阻塞模式或超时模式
bool success = channel.PublishWithOptions(event_type, data, 
                                          timeout_ms=1000);
```

---

### 3.6 SCCL 集群通信

#### 3.6.1 加入集群

```cpp
#include "adk/sccl/agent.h"

class MyAppAgent : public AgentEventHandler {
public:
    bool JoinCluster(const std::string& cluster_name) {
        Agent agent;
        
        // 配置集群参数
        sccl::Config config;
        config.cluster_name = cluster_name;
        config.etcd_hosts = "192.168.1.100:2379";
        config.instance_id = GenerateInstanceId();
        
        // 设置事件处理器
        agent.SetEventHandler(this);
        
        // 加入集群
        if (!agent.JoinCluster(config)) {
            ADK_LOG_ERROR("Failed to join cluster");
            return false;
        }
        
        agent_handle_ = &agent;
        return true;
    }
    
private:
    // 实现事件回调
    virtual void OnRoleChanged(RoleType old_role, RoleType new_role) override {
        ADK_LOG_INFO("Role changed from ", GetRoleString(old_role), 
                     " to ", GetRoleString(new_role));
        
        switch (new_role) {
            case kLeader:
                OnBecomeLeader();
                break;
            case kFollower:
                OnBecameFollower();
                break;
        }
    }
    
    virtual void OnMemberAdded(const std::string& member) override {
        ADK_LOG_INFO("New member joined: ", member);
        UpdateClusterMembers();
    }
    
    virtual void OnMemberRemoved(const std::string& member) override {
        ADK_LOG_WARN("Member removed: ", member);
        HandleMemberLoss(member);
    }
};
```

#### 3.6.2 Leader/Follower模式

```cpp
// Leader 节点执行主操作
void MyAppAgent::OnBecomeLeader() {
    start_leader_tasks();
    advertise_leadership();
}

// Follower 节点只同步数据
void MyAppAgent::OnBecameFollower() {
    stop_leader_tasks();
    sync_from_leader();
}
```

---

### 3.7 SharedMemory 共享内存

#### 3.7.1 创建和使用连续缓冲区

```cpp
#include "adk/shm/cont_memory.h"
#include "adk/shm/cont_channel.h"

// 创建共享内存段
ContMemoryManager memory_manager;

// 分配 1MB 连续内存
char* shm_buffer = memory_manager.Allocate(1024 * 1024);

// 创建双缓冲区通道
ShmContChannel<char, 4096> read_channel(shm_buffer + 0);
ShmContChannel<char, 4096> write_channel(shm_buffer + 4096);

// Producer 进程
write_channel.Produce(data_to_send);

// Consumer 进程  
char received_data[4096];
if (read_channel.Consume(received_data)) {
    // 处理接收到的数据
}
```

#### 3.7.2 多进程通信

```cpp
// 父进程 - 创建共享内存
ParentProcess() {
    ShmSegment segment("my_shm_segment", 1024 * 1024, true);  // true 表示创建
    
    segment.Attach();
    
    // 使用共享内存
    WriteToShm(segment.GetPointer(), data);
}

// 子进程 - 附加已存在的共享内存
ChildProcess() {
    ShmSegment segment("my_shm_segment", 1024 * 1024, false);  // false 表示附加
    
    segment.Attach();
    
    // 读取共享内存
    ReadFromShm(segment.GetPointer(), buffer);
}
```

---

### 3.8 SPSCQueue 无锁队列

#### 3.8.1 生产者 - 消费者模式

```cpp
#include "adk/variant/spsc_byte_buffer.h"

// 创建无锁队列（容量 1024）
SPSCQueue<int, 1024> queue;

// Producer 线程
void ProducerThread() {
    for (int i = 0; i < 10000; i++) {
        while (!queue.Push(i)) {
            // 队列满时自旋或休眠
            usleep(1);
        }
    }
}

// Consumer 线程
void ConsumerThread() {
    int value;
    while (running_) {
        if (queue.Pop(value)) {
            Process(value);
        } else {
            // 队列为空时休眠或自旋
            usleep(1);
        }
    }
}
```

#### 3.8.2 性能注意事项

- **仅支持单生产者单消费者**，多生产者需加锁或使用其他队列
- **避免频繁创建销毁**: 队列应该在程序启动时创建，运行时复用
- **容量规划**: 根据业务场景选择合适的队列容量

---

### 3.9 HighPerformanceClock 高性能时钟

#### 3.9.1 高精度时间获取

```cpp
#include "adk/high_performance_clock.h"

// 获取微秒级时间戳
int64_t timestamp = adk::timespec_now();

// 获取纳秒级时间戳（如果需要更高精度）
int64_t high_res_time = adk::high_resolution_clock::now();

// 计算耗时
int64_t start = timespec_now();
// ... do something ...
int64_t end = timespec_now();
int64_t elapsed_us = end - start;
```

#### 3.9.2 延迟统计

```cpp
// 测量函数执行时间
int64_t MeasureLatency(std::function<void()> func) {
    int64_t start = adk::timespec_now();
    func();
    int64_t end = adk::timespec_now();
    return end - start;  // 单位：微秒
}

// 多次测量取平均
template <typename Func>
double AverageLatency(Func func, int iterations) {
    int64_t total = 0;
    for (int i = 0; i < iterations; i++) {
        total += MeasureLatency(func);
    }
    return static_cast<double>(total) / iterations;
}
```

---

### 3.10 Hashmap 哈希表

#### 3.10.1 基本使用

```cpp
#include "adk/hash_map.h"

Hashmap<std::string, int> user_score_map;

// 插入
user_score_map.Insert("alice", 100);
user_score_map.Insert("bob", 200);

// 查找
int score;
if (user_score_map.Find("alice", &score)) {
    ADK_LOG_INFO("Alice's score: ", score);
}

// 删除
user_score_map.Remove("bob");
```

#### 3.10.2 自定义哈希

```cpp
// 自定义键类型
struct UserId {
    int id;
    
    bool operator==(const UserId& other) const {
        return id == other.id;
    }
};

// 自定义哈希函数
struct UserIdHasher {
    size_t operator()(const UserId& key) const {
        return std::hash<int>()(key.id);
    }
};

// 使用自定义 hash
Hashmap<UserId, UserData, UserIdHasher> user_map;
```

---

## 4. 实用工具和技巧

### 4.1 优雅退出处理

```cpp
volatile sig_atomic_t g_running = 1;

void SignalHandler(int sig) {
    ADK_LOG_INFO("Received signal: ", sig);
    g_running = 0;
}

int main() {
    // 注册信号处理
    struct sigaction sa;
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    
    // 主循环
    while (g_running) {
        DoWork();
        sleep(1);
    }
    
    // 清理资源
    CleanupResources();
    
    return 0;
}
```

### 4.2 异常安全编程

```cpp
#include "adk/synchronize.h"

void SafeOperation() {
    try {
        // 可能抛异常的代码
        risky_operation();
    } catch (const std::exception& e) {
        ADK_LOG_ERROR("Exception: ", e.what());
        handle_exception();
    } catch (...) {
        ADK_LOG_ERROR("Unknown exception");
        handle_unknown_exception();
    }
}

// RAII 资源管理
class ScopedLock {
public:
    explicit ScopedLock(boost::mutex& m) : mutex_(m) {
        lock_.reset(&m);
        lock_.lock();
    }
    
    ~ScopedLock() {
        lock_.unlock();
    }
    
private:
    boost::mutex* mutex_;
    LockGuard lock_;
};
```

### 4.3 性能优化建议

#### 4.3.1 内存池预热

```cpp
// 在应用启动时预分配对象
void WarmupMemoryPool() {
    for (int i = 0; i < 1000; i++) {
        Message* msg = msg_pool.Acquire();
        msg_pool.Release(msg);
    }
    
    ADK_LOG_INFO("Memory pool warmed up");
}
```

#### 4.3.2 批量处理

```cpp
// 批量发送减少系统调用
void BatchSend(Socket& sock, const std::vector<Buffer>& buffers) {
    struct iovec iov[buffers.size()];
    for (size_t i = 0; i < buffers.size(); i++) {
        iov[i].iov_base = buffers[i].GetReadPtr();
        iov[i].iov_len = buffers[i].ReadableBytes();
    }
    
    sendv(sock.fd(), iov, buffers.size());
}
```

### 4.4 调试技巧

#### 4.4.1 开启详细日志

```bash
export ADK_LOG_LEVEL=DEBUG
export ADK_LOG_MODULES=sccl,io_engine
```

#### 4.4.2 性能分析

```cpp
// 开启延迟统计
adk::LatencyStatistics stats;
stats.StartRecording();

// 记录关键操作
RecordLatencyPoint("operation_a");
do_work();
RecordLatencyPoint("operation_b");

stats.StopRecording();
stats.PrintReport();
```

---

## 5. 常见错误与解决方案

### E1: 内存泄漏

**症状**: 内存占用持续增长

**排查**:
```bash
valgrind --leak-check=full ./your_app
```

**解决**:
- 使用 MemPool 而不是直接 `new/delete`
- 确保所有 Acquired 对象都被 Release

### E2: 死锁

**症状**: 程序卡住

**排查**:
```bash
gdb -p <pid>
thread apply all bt
```

**预防**:
- 始终按固定顺序获取多个锁
- 优先使用 RAII 风格的锁 (LockGuard)
- 避免在持有锁时调用外部代码

### E3: 队列满导致丢消息

**症状**: 生产者频繁 Push 失败

**解决**:
- 增大队列容量
- 使用阻塞式 Push (带超时)
- 增加消费者数量

### E4: 网络 IO 性能瓶颈

**症状**: CPU 使用率高但吞吐量低

**排查**:
- 检查是否频繁使用 epoll_wait 轮询
- 确认启用了边缘触发 (ET) 模式
- 验证是否使用了零拷贝技术

---

## 6. 配置示例

### 完整的配置文件

```json
{
    "application": {
        "name": "my_service",
        "version": "1.0.0",
        "instance_id": "inst_001"
    },
    
    "logging": {
        "level": "INFO",
        "path": "/var/log/myapp",
        "async_mode": true,
        "thread_count": 2,
        "file_max_size": 104857600,
        "backup_count": 10
    },
    
    "network": {
        "bind_address": "0.0.0.0",
        "port_range_start": 8000,
        "io_threads": 4,
        "socket_buffer_size": 65536
    },
    
    "cluster": {
        "etcd_hosts": "192.168.1.100:2379",
        "cluster_name": "my_service_cluster",
        "lease_ttl": 30,
        "heartbeat_interval": 10
    },
    
    "memory": {
        "pool_size": 1024,
        "pre_allocate": 1000
    },
    
    "performance": {
        "enable_latency_stat": true,
        "stat_sample_rate": 100
    }
}
```

---

## 7. 总结

ADK 提供了丰富的工具类库来简化分布式应用的开发:

| 模块 | 用途 | 关键 API |
|------|------|---------|
| Logger | 日志系统 | ADK_LOG_INFO, AsyncLogger |
| Property | 配置管理 | GetValue, LoadFromString |
| MemPool | 内存池 | Acquire, Release |
| ThreadPool | 线程池 | Submit, WaitForIdle |
| EventChannel | 事件分发 | Subscribe, Publish |
| SCCL | 集群通信 | JoinCluster, SetEventHandler |
| SharedMemory | 共享内存 | ContMemoryManager, ShmContChannel |
| SPSCQueue | 无锁队列 | Push, Pop |
| HighPerformanceClock | 时间戳 | timespec_now |

熟练使用这些 API 将大大提升你的开发效率和程序性能!

---

## 附录：参考资源

- **源码示例**: `adk/code/example/` 目录
- **测试代码**: `adk/code/test/` 目录可查看各种 API 的使用示例
- **API 文档**: 对应头文件中的注释文档
