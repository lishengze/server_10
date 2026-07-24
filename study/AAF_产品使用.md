# AAF (AMI Application Framework) 产品使用指南

## 1. 概述

本手册从产品使用的角度，详细说明如何使用 AAF 框架开发应用服务。内容包括：接口用法、注意事项、最佳实践和业务实现流程。

---

## 2. 快速开始

### 2.1 第一个 AAF 应用示例

创建一个最基础的 AAF 应用：

```cpp
#include "aaf/generic_ami_application.h"
#include "aaf/error_code.h"

using namespace aaf;
using namespace ami;

class MyApp : public GenericAmiApplication {
public:
    // 重写基类的回调函数
    virtual int32_t OnAmiInitBegin() override {
        ADK_LOG_INFO("Application initializing...");
        return ErrorCode::kSuccess;
    }
    
    virtual int32_t OnTxEndpointCreation(EndpointHandler* ep_hdl, 
                                         const std::string& ep_name) override {
        ADK_LOG_INFO("Created Tx endpoint: ", ep_name);
        return ErrorCode::kSuccess;
    }
    
    virtual int32_t OnRxEndpointCreation(const std::string& ep_name,
                                         MessageHandler** msg_hdl,
                                         bool is_ha_ctx) override {
        // 为 RxEndpoint 指定消息处理器
        *msg_hdl = this;  // 使用自身作为处理器
        ADK_LOG_INFO("Created Rx endpoint: ", ep_name);
        return ErrorCode::kSuccess;
    }
    
    virtual void OnMessage(Message* msg) override {
        // 处理收到的消息
        ADK_LOG_INFO("Received message, size: ", msg->size());
        
        // 回复消息示例
        EndpointHandler* reply_ep = FindTxEndpoint("reply_endpoint");
        if (reply_ep != nullptr) {
            Message* reply_msg = NewMessage(*reply_ep, msg->size());
            memcpy(reply_msg->data(), msg->data(), msg->size());
            reply_ep->context_->SendMessage(reply_msg);
        }
    }
    
    virtual int32_t OnRun() override {
        ADK_LOG_INFO("Application running...");
        
        // 主循环逻辑
        while (is_running()) {
            // 业务处理
            sleep(1);
            
            // 周期性操作
            OnIdle();
        }
        
        return ErrorCode::kPassed;
    }
    
    virtual void OnIdle() override {
        // 空闲时的处理，不会阻塞主线程
    }
};

int main(int argc, char* argv[]) {
    MyApp app;
    
    // 启动应用（会自动解析命令行参数）
    if (!app.Start(argc, argv)) {
        ADK_LOG_ERROR("Failed to start application");
        return -1;
    }
    
    return 0;
}
```

编译并运行：
```bash
g++ -o myapp myapp.cpp -laaf -ladk -lboost_thread -lboost_system
./myapp --instance_name=myapp
```

---

## 3. 核心接口详解

### 3.1 应用生命周期接口

#### 3.1.1 OnConfigureFramework

**用途**: 配置 AAF 框架的参数

```cpp
virtual void OnConfigureFramework(ami::Property& fw_props) override {
    // 设置 HA 相关配置
    fw_props.SetValue(config_key::kEnableHa, "true");
    fw_props.SetValue(config_key::kHaReplicaCount, "3");
    
    // 设置网络配置
    fw_props.SetValue(config_key::kBindAddress, "0.0.0.0");
    fw_props.SetValue(config_key::kPortRangeStart, "8000");
    
    // 设置日志配置
    fw_props.SetValue(config_key::kLogLevel, "INFO");
    fw_props.SetValue(config_key::kLogPath, "/var/log/myapp");
}
```

**注意事项**:
- 此函数在 AMI 初始化之前被调用
- Property 容器会被传递给 AMI 框架使用
- 可以修改 etcd 中读取的配置

#### 3.1.2 OnAmiInitBegin

**用途**: AMI 开始初始化前的准备

```cpp
virtual int32_t OnAmiInitBegin() override {
    // 初始化成员变量
    initialized_ = false;
    
    // 创建本地资源（不依赖 AMI）
    db_pool_ = new DatabasePool(init_db_config());
    
    // 验证配置
    if (!ValidateConfig()) {
        return ErrorCode::kFailure;
    }
    
    return ErrorCode::kSuccess;
}
```

**注意**: 不要在函数中创建任何 AMI 相关的对象

#### 3.1.3 OnTxEndpointCreation / OnRxEndpointCreation

**用途**: 配置 Tx/Rx Endpoint

```cpp
virtual int32_t OnTxEndpointCreation(EndpointHandler* ep_hdl, 
                                     const std::string& ep_name) override {
    ADK_LOG_INFO("Tx endpoint created: ", ep_name);
    
    // 可以在这里做一些额外操作
    // 例如：设置发送队列大小、超时时间等
    
    return ErrorCode::kSuccess;
}

virtual int32_t OnRxEndpointCreation(const std::string& ep_name,
                                     MessageHandler** msg_hdl,
                                     bool is_ha_ctx) override {
    ADK_LOG_INFO("Rx endpoint created: ", ep_name);
    
    // 必须提供消息处理器，或者返回 ErrorCode::kFailure
    *msg_hdl = this;
    
    // 可以为不同的 endpoint 设置不同的处理器
    if (ep_name == "special_handler") {
        *msg_hdl = &special_handler_;
    }
    
    return ErrorCode::kSuccess;
}
```

**注意事项**:
- `OnTxEndpointCreation` 在 Tx Endpoint 创建后回调，不能在此创建新的 Tx Endpoint
- `OnRxEndpointCreation` 必须提供消息处理器 (msg_hdl)，除非你不想接收该端口的消息
- 这些函数会在 `OnAmiInitEnd()` 之前被调用

#### 3.1.4 OnAmiInitEnd

**用途**: AMI 初始化完成，可以开始使用所有 AMI 功能

```cpp
virtual int32_t OnAmiInitEnd() override {
    // 此时可以安全地使用任何 AMI 功能
    
    // 动态创建 Endpoint
    EndpointHandler* dynamic_ep = CreateTxEndpoint("dynamic_ep");
    
    // 获取 Context 信息
    int32_t ctx_id = GetContextId();
    uint32_t partition_no = GetPartitionNo();
    
    // 发布服务
    RegisterService("myservice", tx_ep_map_["service_ep"]);
    
    return ErrorCode::kSuccess;
}
```

**关键点**:
- **这是使用 AMI API 的安全起点**
- 所有 Endpoint 都已创建完成
- 可以调用 `CreateTxEndpoint`、`CreateRxEndpoint` 动态创建

#### 3.1.5 OnRun / OnIdle

**用途**: 主循环和空闲处理

```cpp
virtual int32_t OnRun() override {
    ADK_LOG_INFO("Main loop started");
    
    while (is_running()) {
        // 主处理逻辑
        // 注意：这个循环应该尽快执行，不要长时间阻塞
        
        // 检查是否需要退出
        if (!is_running()) break;
        
        // 周期性任务
        ProcessPeriodicTasks();
    }
    
    return ErrorCode::kPassed;  // 告诉框架进入 OnIdle
}

virtual void OnIdle() override {
    // OnRun 返回 kPassed 后会调用此函数
    // 适合做轻量级的轮询或休眠
    
    // 简单示例：每秒休眠一次
    sleep(1);
    
    // 高级示例：使用条件变量等待事件
    boost::mutex::scoped_lock lock(mutex_);
    condition_.wait_for(lock, boost::posix_time::seconds(1));
}
```

**最佳实践**:
- `OnRun`: 应该处理耗时操作，但不要无限期阻塞
- `OnIdle`: 适合短时间的暂停或轻量级检查

---

### 3.2 消息处理接口

#### 3.2.1 OnMessage

**用途**: 接收高可用 Context 的消息

```cpp
virtual void OnMessage(Message* msg) override {
    // 1. 解析消息头
    MsgHeader* header = reinterpret_cast<MsgHeader*>(msg->data());
    
    // 2. 根据消息类型处理
    switch (header->msg_type) {
        case MSG_TYPE_REQUEST:
            HandleRequest(msg);
            break;
        case MSG_TYPE_RESPONSE:
            HandleResponse(msg);
            break;
        default:
            ADK_LOG_WARN("Unknown message type: ", header->msg_type);
            break;
    }
}

void MyApp::HandleRequest(Message* req) {
    // 处理请求
    // ...
    
    // 构造响应
    EndpointHandler* response_ep = FindTxEndpoint("response_endpoint");
    Message* resp = NewMessage(*response_ep, MAX_MSG_SIZE);
    
    MsgHeader* resp_header = reinterpret_cast<MsgHeader*>(resp->data());
    resp_header->type = MSG_TYPE_RESPONSE;
    resp_header->req_id = req_header->req_id;
    
    // 填充响应数据
    memcpy(resp->data() + sizeof(MsgHeader), response_data, data_len);
    
    // 发送响应
    response_ep->context_->SendMessage(resp);
    // 注意：SendMessage 后不需要手动删除消息，AM I 会自动管理
}
```

**重要提醒**:
- `OnMessage` 中的代码应该尽量高效
- 避免在 `OnMessage` 中执行阻塞操作
- 如需耗时处理，应异步化或放入工作线程池

#### 3.2.2 OnMessageSingleton

**用途**: 接收单例 Context 的消息（非 HA 场景）

```cpp
virtual void OnMessageSingleton(Message* msg) override {
    // 单例 Context 的消息处理
    // 同一时间只有一个实例运行此方法
}
```

---

### 3.3 消息发送接口

#### 3.3.1 发送消息基本用法

```cpp
// 方式 1: 通过 EndpointHandler 发送
EndpointHandler* ep = FindTxEndpoint("target_endpoint");
if (ep != nullptr) {
    Message* msg = NewMessage(*ep, buffer_size);
    
    // 填充消息
    memcpy(msg->data(), buffer, buffer_size);
    
    // 发送
    ep->context_->SendMessage(msg);
    // 无需 DeleteMessage
}

// 方式 2: 通过 Endpoint 指针
EndpointHandler* ep2 = CreateTxEndpoint("new_endpoint");
if (ep2 != nullptr) {
    Message* msg = NewMessage(ep2, buffer_size);
    // ...
    ep2->context_->SendMessage(msg);
}

// 方式 3: 直接调用 context 的 SendMessage
endpoint.context_->SendMessage(msg);
```

#### 3.3.2 零拷贝优化技巧

```cpp
// ❌ 低效：多次拷贝
std::string buffer = build_message();
Message* msg = NewMessage(*ep, buffer.size());
memcpy(msg->data(), buffer.data(), buffer.size());
ep->context_->SendMessage(msg);

// ✅ 高效：直接在 AMI 缓冲上操作
Message* msg = NewMessage(*ep, MAX_SIZE);
build_message_directly(msg->data(), msg->size());  // 用户自定义函数
ep->context_->SendMessage(msg);
```

#### 3.3.3 发送超时控制

```cpp
// 设置发送超时
uint64_t timeout_us = 100000;  // 100ms

// 发送并等待结果
bool send_success = ep->context_->SendMessageWithTimeout(msg, timeout_us);

if (send_success) {
    ADK_LOG_INFO("Message sent successfully");
} else {
    ADK_LOG_ERROR("Send timeout");
    // 可以选择重试或删除消息
    DeleteMessage(*ep, msg);
}
```

---

### 3.4 Endpoint 管理

#### 3.4.1 查找 Endpoint

```cpp
// 查找已创建的 Tx Endpoint
EndpointHandler* ep = FindTxEndpoint("endpoint_name");
if (ep == nullptr) {
    ADK_LOG_ERROR("Endpoint not found: endpoint_name");
}

// 批量查找
std::vector<std::string> ep_names = {"ep1", "ep2", "ep3"};
for (const auto& name : ep_names) {
    EndpointHandler* current_ep = FindTxEndpoint(name);
    if (current_ep != nullptr) {
        // 使用 endpoint
    }
}
```

#### 3.4.2 动态创建 Endpoint

**前提**: 必须在 `OnAmiInitEnd()` 之后才能调用

```cpp
virtual int32_t OnAmiInitEnd() override {
    // 动态创建 Tx Endpoint
    EndpointHandler* dynamic_ep = CreateTxEndpoint("dynamic_tx_ep");
    if (dynamic_ep == nullptr) {
        ADK_LOG_ERROR("Failed to create dynamic endpoint");
        return ErrorCode::kFailure;
    }
    
    // 保存引用供后续使用
    tx_ep_map_["dynamic_tx_ep"] = dynamic_ep;
    
    return ErrorCode::kSuccess;
}

// 后续使用
void MyApp::SendDynamicMessage() {
    auto it = tx_ep_map_.find("dynamic_tx_ep");
    if (it != tx_ep_map_.end()) {
        EndpointHandler* ep = it->second;
        Message* msg = NewMessage(*ep, msg_size);
        // ...
        ep->context_->SendMessage(msg);
    }
}
```

#### 3.4.3 获取 Endpoint 列表

```cpp
// 获取所有已创建的 Tx Endpoint 名称
std::set<std::string>& tx_ep_names = GetTxEndpointNames();

for (const auto& name : tx_ep_names) {
    EndpointHandler* ep = FindTxEndpoint(name);
    if (ep != nullptr) {
        // 处理 endpoint
    }
}
```

---

### 3.5 HA 功能使用

#### 3.5.1 配置 HA

```cpp
virtual void OnConfigureFramework(ami::Property& fw_props) override {
    // 启用 HA
    fw_props.SetValue(config_key::kEnableHa, "true");
    
    // 设置副本数量
    fw_props.SetValue(config_key::kHaReplicaCount, "3");
    
    // 设置 etcd 连接
    fw_props.SetValue(config_key::kEtcdHosts, "192.168.1.100:2379");
    fw_props.SetValue(config_key::kEtcdKeyPrefix, "/myapp/ha");
    
    // 设置恢复策略
    fw_props.SetValue(config_key::kRecoveryStrategy, "from_checkpoint");
    fw_props.SetValue(config_key::kCheckpointPath, "/var/lib/myapp/checkpoint");
}
```

#### 3.5.2 角色变化处理

```cpp
virtual void OnRoleChangeToLeader() override {
    ADK_LOG_INFO("This instance is now LEADER");
    
    // 切换到主动服务模式
    active_mode_ = true;
    
    // 停止 follower 的操作
    stop_follower_tasks();
}

virtual void OnRoleChangeToFollower() override {
    ADK_LOG_INFO("This instance is now FOLLOWER");
    
    // 切换到被动模式
    active_mode_ = false;
    
    // 同步 Leader 状态
    sync_from_leader();
}

virtual void OnMemberLost(const std::vector<std::string>& lost_members) override {
    ADK_LOG_WARN("Members lost: ", join_strings(lost_members));
    
    // 更新集群成员列表
    UpdateClusterMembers(lost_members);
    
    // 如果需要，触发选举
    TriggerElection();
}
```

#### 3.5.3 断点续传

```cpp
virtual void OnRecoveryBegin() override {
    ADK_LOG_INFO("Starting recovery process");
    
    // 可以记录日志或触发监控告警
}

virtual void OnRecoverySuccess() override {
    ADK_LOG_INFO("Recovery completed successfully");
    
    // 恢复完成后可以做一些清理工作
    post_recovery_cleanup();
}
```

---

### 3.6 分片功能使用 (Advanced)

#### 3.6.1 继承 ShardingAgent

```cpp
#include "aaf/sharding/sharding_agent.h"

class MyShardingApp : public ShardingAgent {
public:
    MyShardingApp() : ShardingAgent() {
        // 初始化
    }
    
    // 重写路由算法
    virtual int32_t DoRoute(ami::Message* msg, uint32_t sharding_num) override {
        // 简单的 hash 路由
        const char* data = static_cast<const char*>(msg->const_data());
        uint32_t len = msg->size();
        
        // 计算 hash
        uint32_t hash_value = 0;
        for (uint32_t i = 0; i < len; i++) {
            hash_value = hash_value * 31 + data[i];
        }
        
        // 映射到分片编号 [1, sharding_num]
        return (hash_value % sharding_num) + 1;
    }
};
```

#### 3.6.2 分片统计

```cpp
// 获取统计信息
uint64_t received_count = ctx_ind_[0].nr_message_received;
uint64_t route_failed_count = ctx_ind_[0].nr_route_failed;

ADK_LOG_INFO("Messages received: ", received_count);
ADK_LOG_INFO("Routing failures: ", route_failed_count);
```

---

### 3.7 日志使用

#### 3.7.1 基本日志

```cpp
// 不同级别的日志
ADK_LOG_TRACE("Trace message: value={1}", value);
ADK_LOG_DEBUG("Debug message with multiple args: {1}, {2}", arg1, arg2);
ADK_LOG_INFO("Info message without args");
ADK_LOG_WARN("Warning message: condition={1}", condition);
ADK_LOG_ERROR("Error message occurred");
ADK_LOG_FATAL("Fatal error, exiting");
```

#### 3.7.2 AC (Activity Context) 日志

用于追踪跨模块的请求链：

```cpp
// 在每个关键入口获取 AC id
#define ADK_LOG_DECLARE_AC(activity_id) adk::AcTracker ac_tracker(activity_id);

// 在 ShardingAgent 中
void ShardingAgent::OnMessage(Message* msg) {
    ADK_LOG_INFO_AC_TF("on_message_begin", "msg_id={1}", msg_id);
    
    // 处理逻辑
    
    ADK_LOG_INFO_AC_TF("on_message_end", "msg_id={1}, duration={2}us", 
                       msg_id, duration_us);
}
```

---

### 3.8 配置项说明

#### 3.8.1 常用配置键

| 配置键 | 说明 | 默认值 | 示例 |
|--------|------|--------|------|
| `config_key::kInstanceName` | 实例名称 | - | `"myapp_node1"` |
| `config_key::kSiteID` | 数据中心 ID | 0 | `"1"` |
| `config_key::kReplicaID` | 副本 ID | 0 | `"2"` |
| `config_key::kEnableHa` | 是否启用 HA | `"false"` | `"true"` |
| `config_key::kHaReplicaCount` | HA 副本数 | `"1"` | `"3"` |
| `config_key::kLogLevel` | 日志级别 | `"INFO"` | `"DEBUG"` |
| `config_key::kLogPath` | 日志路径 | `/var/log` | `"/opt/apps/myapp/log"` |
| `config_key::kBindAddress` | 绑定地址 | `"0.0.0.0"` | `"192.168.1.100"` |
| `config_key::kPortRangeStart` | 端口范围起始 | `"8000"` | `"9000"` |

#### 3.8.2 读取配置

```cpp
virtual void OnConfigureFramework(ami::Property& fw_props) override {
    // 读取配置（可以通过 Property 访问）
    std::string instance_name = fw_props.GetValue("instance_name", "default_instance");
    
    int32_t log_level = fw_props.GetValueAsInt("log_level", 2);  // 2=INFO
    
    ADK_LOG_INFO("Using instance name: ", instance_name);
    ADK_LOG_INFO("Log level: ", log_level);
}
```

---

## 4. 常见使用场景

### 4.1 实现 RPC 服务器

```cpp
class RpcServer : public GenericAmiApplication {
private:
    std::unordered_map<std::string, RpcHandler> handler_map_;

public:
    RpcServer() {
        // 注册处理器
        handler_map_["GetUser"] = std::bind(&RpcServer::OnGetUser, this, std::placeholders::_1);
        handler_map_["UpdateUser"] = std::bind(&RpcServer::OnUpdateUser, this, std::placeholders::_1);
    }
    
    int32_t OnRxEndpointCreation(const std::string& ep_name,
                                 MessageHandler** msg_hdl,
                                 bool is_ha_ctx) override {
        *msg_hdl = this;
        return ErrorCode::kSuccess;
    }
    
    void OnMessage(Message* msg) override {
        RpcRequest* req = parse_request(msg);
        
        auto it = handler_map_.find(req->method);
        if (it != handler_map_.end()) {
            // 调用对应 handler
            RpcResponse resp = it->second(req);
            
            // 发送响应
            send_response(resp);
        } else {
            ADK_LOG_ERROR("Unknown method: ", req->method);
        }
    }
};
```

### 4.2 实现消息订阅者

```cpp
class MessageSubscriber : public GenericAmiApplication {
public:
    int32_t OnRxEndpointCreation(const std::string& ep_name,
                                 MessageHandler** msg_hdl,
                                 bool is_ha_ctx) override {
        // 为不同的 topic 创建不同的 RxEndpoint
        *msg_hdl = this;
        return ErrorCode::kSuccess;
    }
    
    void OnMessage(Message* msg) override {
        TopicId topic_id = get_topic_id_from_endpoint(msg->get_endpoint_id());
        
        switch (topic_id) {
            case TOPIC_ORDER:
                OnOrderMessage(msg);
                break;
            case TOPIC_PAYMENT:
                OnPaymentMessage(msg);
                break;
            default:
                break;
        }
    }
};
```

### 4.3 定时任务

```cpp
class ScheduledTaskApp : public GenericAmiApplication {
private:
    boost::thread periodic_task_thread_;
    volatile bool should_stop_;

public:
    int32_t OnAmiInitEnd() override {
        should_stop_ = false;
        periodic_task_thread_ = boost::thread(&ScheduledTaskApp::PeriodicTask, this);
        return ErrorCode::kSuccess;
    }
    
    void OnAmiExitBegin() override {
        should_stop_ = true;
        if (periodic_task_thread_.joinable()) {
            periodic_task_thread_.join();
        }
    }
    
    void PeriodicTask() {
        while (!should_stop_) {
            // 执行定时任务
            ExecuteTask();
            
            // 每隔一段时间执行
            usleep(1000000);  // 1 秒
        }
    }
};
```

---

## 5. 调试与运维

### 5.1 调试模式

```bash
# 设置详细日志
export ADK_LOG_LEVEL=DEBUG

# 开启特定模块的日志
export ADK_LOG_MODULES=ami_executor,sharding

# 运行程序
./myapp --log_level=debug
```

### 5.2 常用命令行动参

```bash
./myapp \
  --instance_name=myapp_$(hostname) \
  --site_id=1 \
  --replica_id=2 \
  --log_level=INFO \
  --log_path=/var/log/myapp
```

### 5.3 指标监控

AAF 提供了丰富的运行时指标：

```cpp
// 在 OnIdle 中收集指标
virtual void OnIdle() override {
    boost::property_tree::ptree indicators;
    
    // 添加自定义指标
    indicators.put("messages.received", metrics_.received_count);
    indicators.put("messages.sent", metrics_.sent_count);
    indicators.put("latency.avg_us", metrics_.avg_latency_us);
    
    // 收集框架提供的指标
    GenericAmiApplication::OnCollectIndicator(false, indicators);
    
    // 上报给监控系统
    PushIndicators(indicators);
}
```

---

## 6. 常见问题与解决方案

### Q1: OnMessage 处理太慢导致消息堆积

**原因**: 业务逻辑耗时过长

**解决方案**:
1. 将耗时逻辑异步化
2. 使用线程池处理
3. 考虑增加消费者数量

### Q2: HA 切换时数据丢失

**原因**: CheckPoint 未正确保存

**解决方案**:
1. 提高 CheckPoint 频率
2. 确保 CheckPoint 成功后再确认
3. 使用 async_ami_executor 降低主循环阻塞

### Q3: 内存泄漏检测

**工具**: valgrind

```bash
valgrind --leak-check=full --show-leak-kinds=all ./myapp
```

### Q4: 性能问题排查

**步骤**:
1. 使用 perf 分析 CPU 热点
2. 查看延迟统计指标
3. 检查锁竞争情况

```bash
# 启用延迟统计
export ADK_ENABLE_LATENCY_STAT=true

# 查看统计输出
cat /var/log/myapp/indicator.log
```

---

## 7. 最佳实践

### 7.1 代码组织

```
src/
├── application.cpp       # 主应用类
├── handlers/             # 消息处理器
│   ├── request_handler.h/cpp
│   └── response_handler.h/cpp
├── services/             # 业务服务
│   ├── user_service.h/cpp
│   └── order_service.h/cpp
└── utils/                # 工具类
    └── message_builder.h/cpp
```

### 7.2 错误处理

```cpp
// 使用 RAII 保证资源释放
class ScopedResource {
public:
    ~ScopedResource() {
        Release();
    }
};

// 在 OnMessage 中使用
void OnMessage(Message* msg) {
    ScopedResource db_conn(get_db_connection());  // 自动管理
    ScopedResource lock(mutex_);                   // 自动解锁
    
    try {
        // 业务逻辑
        do_business_logic();
    } catch (const std::exception& e) {
        ADK_LOG_ERROR("Exception caught: ", e.what());
        // 恢复逻辑
    }
    // 即使异常，RAII 也会保证资源释放
}
```

### 7.3 配置管理

```cpp
// 集中管理配置
class AppConfig {
public:
    static AppConfig& Instance() {
        static AppConfig instance;
        return instance;
    }
    
    void Load(const ami::Property& props) {
        db_host_ = props.GetValue("db.host", "localhost");
        db_port_ = props.GetValueAsInt("db.port", 3306);
        // ...
    }
    
    // Getters
private:
    AppConfig() {}
    std::string db_host_;
    int32_t db_port_;
};

// 在 OnConfigureFramework 中加载
void OnConfigureFramework(ami::Property& props) {
    AppConfig::Instance().Load(props);
}
```

---

## 8. 总结

AAF 框架提供了强大的分布式应用开发能力：

1. **易用性**: 虚函数回调设计，易于理解和扩展
2. **高性能**: 零拷贝、共享内存等技术保障吞吐量
3. **高可用**: 完善的 HA 机制和断点续传
4. **可观测性**: 丰富的日志和监控指标

掌握上述接口和使用方法，你就可以快速构建企业级的分布式应用了！

---

## 附录：参考资源

- **源码示例**: `aaf/code/example/` 目录下的 demo 文件
- **使用说明书**: `aaf/doc/AAF 使用说明书.md`
- **API 文档**: 对应的头文件注释
