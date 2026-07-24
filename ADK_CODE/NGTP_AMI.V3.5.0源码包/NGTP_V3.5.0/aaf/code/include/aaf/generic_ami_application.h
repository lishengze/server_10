// Copyright (c) 2018 Archforce Financial Technology. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are not permitted.
//
// For more information about Archforce, welcome to archforce.cn.

#ifndef AAF_GENERIC_AMI_APPLICATION_H_
#define AAF_GENERIC_AMI_APPLICATION_H_

#include <string>

#include <aaf/error_code.h>
#include <aaf/log_code_base.h>
#include <aaf/generic_application.h>

#include <ami.h>

namespace ami
{
class Event;
class Message;
class RxEndpoint;
class TxEndpoint;
class Property;
}

/**
 * application name format : ${NAME_STRING}_${PARTITION_NO}_${SITE_ID}_${REPLICA_ID}
 * default log file path :    "/home/${LOGIN_USER}/log/"
 * default indicator file path : "/home/${LOGIN_USER}/log/"
 * default lock/pid file path :    "/home/${LOGIN_USER}/lock/"
 */

namespace aaf
{

namespace constant
{
static const int32_t kInvalidContextId = -1;
} // constant

/**
 * @brief      AAF框架封装的endpoint句柄，与AAF框架配合使用
 */
struct EndpointHandler
{
public:
    EndpointHandler()
        :   endpoint_(NULL),
            context_(NULL)
    {}

    ~EndpointHandler()
    {}

    /**
     * @brief      向该主题上发送消息，零拷贝模式
     *
     * @param      msg   待发送的消息对象
     *
     * @return     发送成功时返回ErrorCode::kSuccess
     */
    virtual int32_t SendMsg(ami::Message* const msg)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(msg);
        }
        return ErrorCode::kFailure;
    }

    virtual int32_t SendMsg(ami::Message* const msg, ami::TraceRecord record)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(msg, record);
        }
        return ErrorCode::kFailure;
    }

    /**
     * @brief      向该主题上发送消息，零拷贝模式
     *
     * @param      msg              待发送的消息对象
     * @param      partition_no     消息所发往的分区编号
     *
     * @return     发送成功时返回ErrorCode::kSuccess
     */
    virtual int32_t SendMsg(ami::Message* const msg, int32_t partition_no)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(msg, partition_no);
        }
        return ErrorCode::kFailure;
    }

    virtual int32_t SendMsg(ami::Message* const msg, int32_t partition_no, ami::TraceRecord record)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(msg, partition_no, record);
        }
        return ErrorCode::kFailure;
    }

    /**
     * @brief      向该主题上发送消息，拷贝模式
     *
     * @param[in]  data  待发送的byte buffer地址
     * @param[in]  len   待发送的byte buffer的长度
     *
     * @return     发送成功时返回ErrorCode::kSuccess
     */
    virtual int32_t SendMsg(const void* data, uint32_t len)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(data, len);
        }
        return ErrorCode::kFailure;
    }

    virtual int32_t SendMsg(const void* data, uint32_t len, ami::TraceRecord record)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(data, len, record);
        }
        return ErrorCode::kFailure;
    }

    /**
     * @brief      向该主题上发送消息，拷贝模式
     *
     * @param[in]  data             待发送的byte buffer地址
     * @param[in]  len              待发送的byte buffer的长度
     * @param[in]  partition_no     消息所发往的分区编号
     *
     * @return     发送成功时返回ErrorCode::kSuccess
     */
    virtual int32_t SendMsg(const void* data, uint32_t len, int32_t partition_no)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(data, len, partition_no);
        }
        return ErrorCode::kFailure;
    }

    virtual int32_t SendMsg(const void* data, uint32_t len, int32_t partition_no, ami::TraceRecord record)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(data, len, partition_no, record);
        }
        return ErrorCode::kFailure;
    }

    /**
     * @brief      向该主题上发送消息，拷贝模式
     *
     * @param[in]  data_str         待发送的字符串
     *
     * @return     发送成功时返回ErrorCode::kSuccess
     */
    virtual int32_t SendMsg(const std::string& data_str)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(data_str);
        }
        return ErrorCode::kFailure;
    }

    virtual int32_t SendMsg(const std::string& data_str, ami::TraceRecord record)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(data_str, record);
        }
        return ErrorCode::kFailure;
    }

    /**
     * @brief      向该主题上发送消息，拷贝模式
     *
     * @param[in]  data_str         待发送的字符串
     * @param[in]  partition_no     消息所发往的分区编号
     *
     * @return     发送成功时返回ErrorCode::kSuccess
     */
    virtual int32_t SendMsg(const std::string& data_str, int32_t partition_no)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(data_str, partition_no);
        }
        return ErrorCode::kFailure;
    }

    virtual int32_t SendMsg(const std::string& data_str, int32_t partition_no, ami::TraceRecord record)
    {
        if (endpoint_ != nullptr)
        {
            return endpoint_->SendMsg(data_str, partition_no, record);
        }
        return ErrorCode::kFailure;
    }

    EndpointHandler* operator=(const EndpointHandler* other)
    {
        endpoint_ = other->endpoint_;
        context_ = other->context_;
        return this;
    }

    ami::TxEndpoint*     endpoint_;
    ami::Context*        context_;

private:
    EndpointHandler(const EndpointHandler& other);
};

/**
 * @brief      transport描述结构
 */
struct TransportInfo
{
    std::string tier_name;                  ///< transport发送端的集群名称
    std::string endpoint_name;              ///< transport对应的endpoint名称
    std::string transport_name;             ///< transport名称
    int32_t     endpoint_id;                ///< transport对应的endpoint的ID
    int32_t     transport_partition;        ///< transport所述的分区编号
    int32_t     transport_id;               ///< transport的ID值
    int32_t     transport_direction;        ///< transport的收发方向
};

/**
 * @brief      ami性能配置类型
 */
enum AfPerformanceType
{
    kStandard = 0,              ///< ami默认配置
    kHighThroughput,            ///< 高吞吐配置
    kLowLatency,                ///< 低时延配置
    kLowUtilization,            ///< 低资源使用率配置
};


class GenericAmiApplicationImpl;
/**
 * @brief      AMI应用的基类，该基类已经实现了AMI应用的通用初始化流程
 *
 * @note       一个应用程序，只能实例化一次该基类的派生类
 */
class GenericAmiApplication : public GenericApplication
{
public:
    GenericAmiApplication();

    GenericAmiApplication(bool flag);

    ~GenericAmiApplication()
    {}

    /**
     * @brief         SetAmiAppOption用于添加程序启动参数
     *
     * @note          应用可通过使用AddOption、AddOptionWithArgument、AddOptionWithAcceptor、
     *                AddOptionWithCallback接口添加启动参数
     */
    virtual void SetAmiAppOption(){}

    /**
     * @brief         解析程序选项时，将依次传入各个参数名
     *
     * @note          函数体中一般通过使用GetOptionArgument获取对应选项的值
     *
     * @param[in]     程序选项名
     */
    virtual void OnAmiAppOption(const std::string& option_name) {}

    /**
     * @brief         OnConfigureFramework用于配置AAF框架的属性
     *
     * @note          通过使用Property::SetValue接口对fw_props设置相关属性，可设置的属性定义
     *                在config_key.h中
     *
     * @param[in|out] AAF的框架属性
     */
    virtual void OnConfigureFramework(ami::Property& fw_props)        // to overwrite the etcd configuration!
    {}

    /**
     * @brief         OnFrameworkInitBegin在OnInin后、加强跟跑与分片进程初始化前被调用
     *
     * @return        成功时返回aaf::kSuccess / 失败时返回相应错误码
     */
    virtual int32_t OnFrameworkInitBegin()
    {
        return ErrorCode::kSuccess;
    }

    /**
     * @brief         OnAmiInitBegin在AMI开始初始化前被调用
     *
     * @note          可在OnAmiInitBegin中完成需要在AMI初始化前完成的动作，例如应用变量初始化等
     *
     * @return        成功时返回aaf::kSuccess / 失败时返回相应错误码
     */
    virtual int32_t OnAmiInitBegin()
    {
        return ErrorCode::kSuccess;
    }

    /**
     * @brief         OnTxEndpointCreationBegin在开始创建所有TxEndpoint前被调用
     *
     * @note          在开始创建TxEndpoint之前所要做的工作，可在此函数中完成
     *
     * @return        成功时返回aaf::kSuccess / 失败时返回相应错误码
     */
    virtual int32_t OnTxEndpointCreationBegin()
    {
        return ErrorCode::kSuccess;
    }

    /**
     * @brief         每创建一个TxEndpoint后，OnTxEndpointCreation被调用
     *
     * @note          在创建TxEndpoint之后所要做的工作，可在此函数中完成
     *
     * @param[in]     ep_hdl是AMI创建TxEndpoint关联的消息处理句柄，
     *                ep_name是所创建的TxEndpoint名称
     *
     * @return        成功时返回aaf::kSuccess / 失败时返回相应错误码
     */
    virtual int32_t OnTxEndpointCreation(EndpointHandler* ep_hdl, const std::string& ep_name)
    {
        return ErrorCode::kSuccess;
    }

    /**
     * @brief         OnRxEndpointCreationBegin在开始创建所有RxEndpoint前被调用
     *
     * @note          在开始创建RxEndpoint之前所要做的工作，可在此函数中完成
     *
     * @return        成功时返回aaf::kSuccess / 失败时返回相应错误码
     */
    virtual int32_t OnRxEndpointCreationBegin()
    {
        return ErrorCode::kSuccess;
    }

    /**
     * @brief         每创建一个RxEndpoint前，OnRxEndpointCreation被调用
     *
     * @note          在创建RxEndpoint之后所要做的工作，可在此函数中完成，
     *                应用在函数体主要是向AMI提供消息处理句柄msg_hdl，也可以不提供，AMI会使用默认消息句柄。
     *
     * @param[in]     ep_name是所创建的RxEndpoint名称，
     *                is_ha_ctx表示context是否是高可用
     *
     * @param[in|out] msg_hdl是需要提供的RxEndpoint关联的消息处理句柄，
     *
     * @return        成功时返回aaf::kSuccess / 失败时返回相应错误码
     */
    virtual int32_t OnRxEndpointCreation(const std::string& ep_name, ami::MessageHandler** msg_hdl, bool is_ha_ctx)
    {
        msg_hdl = NULL;
        return ErrorCode::kSuccess;
    }

    /**
     * @brief         OnAmiInitEnd在AMI初始化完成后被调用
     *
     * @note          通知用户AMI初始化已经完成，即将启动程序处理流程，所有的程序初始化工作都必须在此接口内完成。
     *
     * @return        成功时返回aaf::kSuccess / 失败时返回相应错误码
     */
    virtual int32_t OnAmiInitEnd()
    {
        return ErrorCode::kSuccess;
    }

    /**
     * @brief         启动程序处理流程
     *
     * @note          程序的主要处理逻辑在此函数中实现。
     *                当OnRun成功结束，则程序进入OnIdle进行空闲态处理流程；
     *                当OnIdle成功结束，主循环会返回OnRun。主程序根据is_running控制循环。
     *
     * @return        成功时返回ErrorCode::kPassed / 失败时返回相应错误码
     */
    virtual int32_t OnRun()
    {
        return ErrorCode::kPassed;
    }

    /**
     * @brief        程序空闲态处理逻辑
     *
     * @note         OnRun执行成功后接着执行OnIdle
     */
    virtual void OnIdle()
    {
        sleep(1);
    }

    /**
     * @brief        OnAmiExitBegin在程序执行退出流程时首先调用
     *
     * @note         通知用户AMI准备开始退出，AMI退出之前的清理工作可在该接口内完成
     */
    virtual void OnAmiExitBegin() {}

    /**
     * @brief         OnAmiRxExitEnd在所有的RxEndPoint退出完成后被调用
     *
     * @note          通知用户RxEndPoint已经退出完成，准备退出TxEndPoint。
     *                在TxEndPoint退出之前，需要完成的工作可在该接口内完成。
     */
    virtual void OnAmiRxExitEnd() {}            // stop ami rx path, stop app, stop ami tx path
                                                // stop app here

    /**
     * @brief         OnAmiExitEnd在完成AMI退出流程后被调用
     *
     * @note          通知用户AMI退出已经完成，程序所有的资源清空工作必须在此函数内完成。
     */
    virtual void OnAmiExitEnd() {}

    /**
     * @brief         AMI提供的默认消息处理函数
     *
     * @note          当RxEndpoint接收到消息，AMI会回调该函数将消息提交给应用。
     *                OnMessage是针对于高可用context的消息回调接口，
     *
     * @param[in]     AMI消息
     */
    virtual void OnMessage(ami::Message*) {}

    /**
     * @brief         AMI提供的默认消息处理函数
     *
     * @note          当RxEndpoint接收到消息，AMI会回调该函数将消息提交给应用。
     *                OnMessage是针对于单例context的消息回调接口，
     *
     * @param[in]     AMI消息
     */
    virtual void OnMessageSingleton(ami::Message*) {}

    /**
     * @brief        OnRoleChangeToLeader在发生kRoleChanged事件时被调用，通知应用的角色转变为Leader
     */
    virtual void OnRoleChangeToLeader() {}

    /**
     * @brief        OnMemberLost在发生kMemberLost事件时被调用，通知应用有集群成员退出
     *
     * @param[in]    退出集群的所有成员名称
     */
    virtual void OnMemberLost(const std::vector<std::string>& lost_members) {}

    /**
     * @brief        Recovery开始事件通知应用
     */
    virtual void OnRecoveryBegin() {}

    /**
     * @brief        Recovery成功事件通知应用
     */
    virtual void OnRecoverySuccess() {}

    /**
     * @brief        Recovery开始事件通知应用
     */
    virtual void OnSingletonRecoveryBegin() {}

    /**
     * @brief        Recovery成功事件通知应用
     */
    virtual void OnSingletonRecoverySuccess() {}

    /**
     * @brief        设置context的名称
     *
     * @note         MakeHighAvailableContextName是设置高可用context的名称，默认实现返回“实例名”
     *
     * @return       context名称
     */
    virtual std::string MakeHighAvailableContextName();

    /**
     * @brief        设置context的名称
     *
     * @note         MakeSingletonContextName是设置单例context的名称，默认实现返回“实例名_Singleton”
     *
     * @return       context名称
     */
    virtual std::string MakeSingletonContextName();

    /**
     * @brief      特例化Context的创建属性
     *
     * @param[in]  context_name  Context名称
     * @param[in]  is_ha_ctx     指示当前创建的Context是否为高可用Context
     * @param[out] props         输出参数，用于特例化Context的属性
     */
    virtual void OnConfigureContextProperty(const std::string& context_name,
                                            bool is_ha_ctx,
                                            ami::Property& props)
    {}

    /**
     * @brief      作为灾备集群时，当主集群离线时通过该回调通知应用
     */
    virtual void OnMasterOffline() {};

    /**
     * @brief      作为灾备集群时，当该集群暂停从主集群同步数据时通知应用
     */
    virtual void OnStopMasterSync() {};

    /**
     * @brief      作为灾备集群时，当该集群转变为主集群时通知应用
     */
    virtual void OnRoleChangeToMaster() {};

    /**
     * @note       placeholder，请不要使用该接口，请使用OnRoleChangeToMaster()
     */
    virtual void OnSingletonRoleChangeToMaster()
    {}

    /**
     * @brief      用户设置应用版本信息
     *
     * @return     应用的版本信息
     *
     * @note       用户实现该接口，将所设置的应用版本信息作为返回值
     */
    virtual std::string GetAppVersion() { return "unknown"; }

    /**
     * @brief      获取应用依赖的AMI版本信息
      *
     * @return     应用依赖的AMI版本信息
     *
     * @note       用户实现该接口，将所设置的应用AMI版本信息作为返回值
     */
    virtual std::string GetAmiVersion() { return "unknown"; }

    /**
     * @brief      logger初始化就绪

     */
    virtual int32_t OnLoggerReady() { return aaf::ErrorCode::kSuccess; }

    /**
     * @brief        创建ami消息
     *
     * @note         使用该函数可申请AMI消息结构，用于零拷贝发送
     *
     * @param[in]    ep_hdl         endpoint消息处理句柄
     * @param[in]    byte_buf_len   消息体长度
     *
     * @note         该接口线程安全
     *
     * @return       所创建的ami消息
     */
    static inline ami::Message* NewMessage(EndpointHandler& ep_hdl, uint32_t byte_buf_len)
    {
        return ep_hdl.context_->NewMessage(byte_buf_len);
    }

    /**
     * @brief        创建ami消息
     *
     * @note         使用该函数可申请AMI消息结构，用于零拷贝发送
     *
     * @param[in]    ep_hdl         endpoint消息处理句柄，
     * @param[in]    byte_buf_len   消息体长度
     *
     * @return       所创建的ami消息
     */
    static inline ami::Message* NewMessage(EndpointHandler* ep_hdl, uint32_t byte_buf_len)
    {
        return ep_hdl->context_->NewMessage(byte_buf_len);
    }

    /**
     * @brief        删除ami消息
     *
     * @note         默认情况下程序未获得ami消息控制权，当消息发送成功时会被AMI自动释放,
     *               当程序获得ami消息控制权，使用该函数可以通知AMI释放该消息。
     *               使用EndpointHandler所申请的AMI消息，可以通过任意同属于同一Context
     *               的EndpointHandler释放
     *
     * @param[in]    ep_hdl     endpoint处理句柄，
     * @param[in]    msg        所要删除的ami消息
     *
     * @return       成功时返回aaf::kSuccess, 失败时返回相应错误码
     */
    static inline int32_t DeleteMessage(EndpointHandler& ep_hdl, ami::Message* msg)
    {
        return ep_hdl.context_->DeleteMessage(msg);
    }

    /**
     * @brief        删除ami消息
     *
     * @note         默认情况下程序未获得ami消息控制权，当消息发送成功时会被AMI自动释放,
     *               当程序获得ami消息控制权，使用该函数可以通知AMI释放该消息。
     *               使用EndpointHandler所申请的AMI消息，可以通过任意同属于同一Context
     *               的EndpointHandler释放
     *
     * @param[in]    ep_hdl     endpoint处理句柄，
     * @param[in]    msg        所要删除的ami消息
     *
     * @return       成功时返回aaf::kSuccess, 失败时返回相应错误码
     */
    static inline int32_t DeleteMessage(EndpointHandler* ep_hdl, ami::Message* msg)
    {
        return ep_hdl->context_->DeleteMessage(msg);
    }

    /**
     * @brief         创建TxEndpoint
     *
     * @note          注意该接口需要在OnAmiInitEnd之后方可使用，但该接口可以和框架的创建流程中的
     *                OnTxEndpointCreation混用
     *
     * @param[in]     tx_ep_nam     endpoint的名称
     *
     * @return        成功返回TxEndpoint对象，失败返回NULL
     */
    EndpointHandler* CreateTxEndpoint(const std::string& tx_ep_name);

    /**
     * @brief         创建RxEndpoint
     *
     * @note          注意该接口需要在OnAmiInitEnd之后方可使用，但该接口可以和框架的创建流程中的
     *                OnRxEndpointCreation混用
     *
     * @param[in]     rx_ep_name    endpoint的名称
     *
     * @return        成功时返回ErrorCode::kSuccess，失败时返回ErrorCode::kFailure
     */
    int32_t CreateRxEndpoint(const std::string& rx_ep_name);

    /**
     * @brief         终止程序
     */
    static void StopAmiApp()
    {
        GenericApplication::Stop();
    }

    /**
     * @brief        获取实例名称
     *
     * @return       返回实例名称
     */
    std::string GetNameString();

    /**
     * @brief        获取数据中心ID
     *
     * @return       返回数据中心ID
     */
    uint32_t GetSiteID();

    /**
     * @brief        获取副本ID
     *
     * @return       返回副本ID
     */
    uint32_t GetReplicaID();

    /**
     * @brief        获取分区编号
     *
     * @return       返回分区编号
     */
    uint32_t GetPartitionNo();

    /**
     * @brief      获取可以修改的框架属性容器对象
     *
     * @return     返回可以修改的框架属性容器对象
     */
    ami::Property& MutableFrameworkConfiguration();

    /**
     * @brief         获取高可用Context的ID值
     *
     * @note          GetContextId 适用于高可用场景，GetSingletonContextId 适用于非高可用场景；
     *                这两个函数应该在 OnAmiInitBegin() 之后调用;
     *
     * @return        返回context id值
     */
    int32_t GetContextId()
    {
        if (ha_ctx_id_ != constant::kInvalidContextId)
        {
            return ha_ctx_id_;
        }
        return ha_ctx_id_ = DoGetContextId();
    }

    /**
     * @brief      获取高可用Context的ID值
     *
     * @return     返回高可用Context的ID值
     */
    int32_t GetHighAvailableContextId()
    {
        return GetContextId();
    }

    /**
     * @brief      获取单例Context的ID值
     *
     * @return     返回单例Context的ID值
     */
    int32_t GetSingletonContextId()
    {
        if (singleton_ctx_id_ != constant::kInvalidContextId)
        {
            return singleton_ctx_id_;
        }
        return singleton_ctx_id_ = DoGetSingletonContextId();
    }

    /**
     * @brief         根据transport_id获取transport信息
     *
     * @param[in]     id 为transport的标识
     *
     * @return        返回transport的信息
     */
    const TransportInfo* GetTransportInfo(int32_t id);

    /**
     * @brief         获取应用创建的所有rx transport_id的集合
     *
     * @note          该接口在OnTxEndpointCreationBegin及之后可以使用
     *
     * @return        返回transport_id的集合
     */
    std::set<int32_t>& GetRxTransportIDs() { return GetRxStreamIDs(); }

    /**
     * @brief         获取应用创建的所有tx transport_id的集合
     *
     * @note          该接口在OnTxEndpointCreationBegin及之后可以使用
     *
     * @return        返回transport_id的集合
     */
    std::set<int32_t>& GetTxTransportIDs() { return GetTxStreamIDs(); }

    /**
     * @brief         获取实例创建的所有rx stream_id集合
     *
     * @note          该接口在OnTxEndpointCreationBegin及之后可以使用
     *
     * @param[out]    返回stream_id的集合
     */
    std::set<int32_t>& GetRxStreamIDs();

    /**
     * @brief         获取实例创建的所有tx stream_id集合
     *
     * @note          该接口在OnTxEndpointCreationBegin及之后可以使用
     *
     * @param[out]    返回stream_id的集合
     */
    std::set<int32_t>& GetTxStreamIDs();

    /**
     * @brief         获取实例所有RxEndpoint名称的集合，包含Master的RxEndpoint
     *
     * @note          该接口在OnTxEndpointCreationBegin及之后可以使用
     *
     * @param[out]    返回endpoint名称的集合
     */
    std::set<std::string>& GetRxEndpointSet();

    /**
     * @brief         获取实例所有TxEndpoint名称的集合，包含Master的TxEndpoint
     *
     * @note          该接口在OnTxEndpointCreationBegin及之后可以使用
     *
     * @param[out]    返回endpoint名称的集合
     */
    std::set<std::string>& GetTxEndpointSet();

    /**
     * @brief         获取应用某一TxEndpoint的分区列表, 可以作用于Master的TxEndpoint
     *
     * @param[in]     ep_nam        endpoind名称，
     *
     * @param[in|out] partitions    获取到的分区id集合
     *
     * @return        成功返回 kSuccess， 失败返回对应的错误码
     */
    int32_t GetTxEndpointPartitions(const std::string& ep_name, std::vector<int32_t>& partitions);

    /**
     * @brief         获取应用某一RxEndpoint的分区列表, 可以作用于Master的RxEndpoint
     *
     * @param[in]     ep_nam        endpoind名称，
     *
     * @param[in|out] partitions    获取到的分区id集合
     *
     * @return        成功返回 kSuccess， 失败返回对应的错误码
     */
    int32_t GetRxEndpointPartitions(const std::string& ep_name, std::vector<int32_t>& partitions);

    /**
     * @brief         获取TxEndpoint负载均衡组（load balance group）信息
     *
     * @param[in]     ep_name   endpoint名称
     *
     * @return        成功时返回ErrorCode::kSuccess，失败时返回对应的错误码
     */
    int32_t GetTxEndpointLBGs(const std::string& ep_name, std::vector<int32_t>& lb_groups);

    /**
     * @brief         获取程序高可用Context的启动状态(init-status)
     *
     * @return        返回"Bootstrap"或"Recovery"
     */
    std::string GetInitStatus();

    /**
     * @brief         获取程序高可用Context的启动状态(init-status)
     *
     * @return        返回"Bootstrap"或"Recovery"
     */
    std::string GetHighAvailableInitStatus() { return GetInitStatus(); }

    /**
     * @brief         获取程序单例Context的启动状态(init-status)
     *
     * @return        返回"Bootstrap"或"Recovery"
     */
    std::string GetSingletonInitStatus();

    /**
     * @brief         获取高可用的context对象
     *
     * @return        返回Context对象
     */
    inline ami::Context* GetContext() { return ha_context_; }

    /**
     * @brief         获取高可用的context对象
     *
     * @return        返回Context对象
     */
    inline ami::Context* GetHighAvailableContext() { return ha_context_; }

    /**
     * @brief         获取单例的context对象
     *
     * @return        返回Context对象
     */
    inline ami::Context* GetSingletonContext() { return singleton_context_; }

    /**
     * @brief      判断某一RxEndpoint是否为主集群的RxEndpoint
     *
     * @param[in]  ep_name  endpoint名称
     *
     * @return     如果是主集群的RxEndpoint则返回true，否则返回false
     */
    bool IsMasterRxEndpoint(const std::string& ep_name);

    /**
     * @brief      获取recorder的持久化路径
     *
     * @return     recorder的持久化路径
     */
    static const std::string& GetRecorderDataPath();

    /**
     * @brief      向框架注册高可用Context的消息处理句柄
     *
     * @note       在OnCreateRxEndpointBegin()内及之前调用该接口方可生效，
     *             使用该接口时OnMessage不再生效
     *             应用需保证在OnAmiExitEnd()接口回调之前，该句柄始终有效
     *
     * @param      mh    高可用Context的消息处理句柄
     */
    static void RegisterHighAvailableHandler(ami::MessageHandler* mh);

     /**
     * @brief      向框架注册单例Context的消息处理句柄
     *
     * @note       在OnCreateRxEndpointBegin()内及之前调用该接口方可生效，
     *             使用该接口时OnMessageSingleton不再生效
     *             应用需保证在OnAmiExitEnd()接口回调之前，该句柄始终有效
     *
     * @param      mh    单例Context的消息处理句柄
     */
    static void RegisterSingletonHandler(ami::MessageHandler* mh);

    /**
     * @brief      根据环境变量AF_PERFORMANCE获取ami性能配置类型
     *
     * @param      AfPerformanceType    ami性能配置类型：标准，高吞吐，低时延，低资源使用率
     */
    static AfPerformanceType GetArchforcePerformance();

    /**
     * @brief      用于白夜市切换,该接口和CreateAllContext配合使用.
     *             调用该接口时, 会清除上一场的context资源, 并重置aaf的缓存内容;
     * @param      timeout_milli: 超时时间，单位ms. 默认 30000.
     * @return     成功返回 kSuccess, 返回其他则失败
     */
    int32_t DeleteAllContext(uint32_t timeout_milli = 30000);

    /**
     * @brief      用于白夜市切换,该接口和DeleteAllContext配合使用.
     *             调用该接口时, 会重新进行aaf的初始化流程
     * @return     成功返回 kSuccess, 返回其他则失败
     */
    int32_t CreateAllContext();

    /**
     * @brief      返回应用使用的 DomainServer IP地址和端口
     *
     * @return     DomainServer IP地址和端口
     */
    std::string& domain_server();

    /**
     * @brief      检查应用是否为灾备
     *
     * @return     若应用为灾备则返回true
     */
    static bool is_disaster_backup();

    static ami::Context*    ha_context_;
    static ami::Context*    singleton_context_;

    static ami::Property ha_context_property();

private:
    virtual int32_t OnParseProgramOptionEnd();
    virtual int32_t OnDaemonizeEnd();
    virtual int32_t SetSingletonLockFileDirectory(std::string& file_path);
    virtual int32_t OnLogInit(std::string& log_dir, std::string& log_name);
    virtual int32_t OnInit();
    virtual void OnExit();
    virtual void SetProgramOption();
    virtual void OnProgramOption(const std::string& option_name);
    int32_t  DoGetContextId();
    int32_t  DoGetSingletonContextId();
    virtual void OnAAFSingnal(int sig_num, int value);
    void OnRoleChangeToMasterInternal();
    void OnStopMasterSyncInternal();
    void OnMasterOfflineInternal();

    void OnSignal(int sig_num, int value) override;
    int32_t ha_ctx_id_;
    int32_t singleton_ctx_id_;

public:
    /**
     * @brief        OnRoleChangeToLeader在发生kRoleChanged事件时被调用，通知应用的角色转变为member
     */
    virtual void OnRoleChangeToMember() {}

    /**
     * @brief        Recovery开始重演输入消息事件
     */
    virtual void OnRecoveryMessage() {}
    virtual void OnSingletonRecoveryMessage() {}

    /**
     * @brief        丢弃消息的事件
     */
    virtual void OnDiscardMessage(std::string& msg) {}

    /**
     * @brief       Rejoin成功事件通知应用(Leader和Member)
     *
     * @param       rejoin_member       rejoin 的成员
     */
    virtual void OnRejoinSuccess(const std::string& rejoin_member) {}

    /**
     * @brief        Rejoin失败事件通知Leader
     * 
     * @param       rejoin_member       rejoin 的成员
     * 
     * @note        rejoin 成员的 rejoin 失败是错误事件，直接进入退出流程
     */
    virtual void OnRejoinFailedLeader(const std::string& rejoin_member) {}

    /**
     * @brief        无接收端事件
     * 
     * @param       endpoint_name      主题名称
     * @param       partition          分区
     * 
     */
    virtual void OnNoReceiver(const std::string& endpoint_name,
                              const std::string& partition) {}

    /**
     * @brief        接收端上线事件
     * 
     * @param       endpoint_name      主题名称
     * @param       partition          分区
     * 
     */
    virtual void OnReceiverUp(const std::string& endpoint_name,
                              const std::string& partition) {}

    /**
     * @brief        无发送端事件
     * 
     * @param       tier_name            发送端集群名称
     * @param       endpoint_name        主题名称
     * @param       partition            分区
     * 
     * @note        以 transport 维度上报事件，即上游集群中所有成员掉线后触发该事件
     */
    virtual void OnNoTransmitter(const std::string& tier_name,
                                 const std::string& endpoint_name,
                                 const std::string& partition) {}

    /**
     * @brief        发送端上线事件
     * 
     * @param       tier_name            发送端集群名称
     * @param       endpoint_name        主题名称
     * @param       partition            分区
     * 
     * @note        以 transport 维度上报事件，即上游集群上线时触发该事件
     */
    virtual void OnTransmitterUp(const std::string& tier_name,
                                 const std::string& endpoint_name,
                                 const std::string& partition) {}

    bool IsAdvanceFollower() override;

    /**
     * @brief DoRoute 分片号计算函数;该函数会去解析消息，从而获取到客户号，然后通过分片号算法，计算分片号
     *
     * @param[in] msg 消息
     * @param[in] sharding_num 分片号数量 
     * @return 分片号  0: 递交给所有分片， >0 递交给对应编号的分片
    */
    virtual int32_t DoRoute(ami::Message* msg, const uint32_t sharding_num)
    {
        return 0;
    }

    /**
     * @brief 获取分片总数
     *
     * @return 分片总数
    */
    int32_t GetShardingNum();

    static int32_t GetShardingIndex();

    /**
     * @brief        丢弃消息的全局序号
     */
    virtual void OnDiscardMessageTotalOrderSqn(uint64_t discard_msg_sqn) {}

    friend GenericAmiApplicationImpl;
};

/**
 * @brief      申请AMI消息，用于零拷贝场景
 *
 * @param      ep_hdl        endpoint handler对象
 * @param[in]  byte_buf_len  消息长度
 *
 * @return     成功时返回ami::Message对象，失败时返回nullptr
 */
inline ami::Message* NewMessage(EndpointHandler& ep_hdl, uint32_t byte_buf_len)
{
    return GenericAmiApplication::NewMessage(ep_hdl, byte_buf_len);
}

/**
 * @brief      申请AMI消息，用于零拷贝场景
 *
 * @param      ep_hdl        endpoint handler对象
 * @param[in]  byte_buf_len  消息长度
 *
 * @return     成功时返回ami::Message对象，失败时返回nullptr
 */
inline ami::Message* NewMessage(EndpointHandler* ep_hdl, uint32_t byte_buf_len)
{
    return GenericAmiApplication::NewMessage(ep_hdl, byte_buf_len);
}

/**
 * @brief      删除AMI消息对象
 *
 * @param      ep_hdl  endpoint handler对象
 * @param      msg     待删除的AMI消息
 *
 * @note         默认情况下程序未获得ami消息控制权，当消息发送成功时会被AMI自动释放,
 *               当程序获得ami消息控制权，使用该函数可以通知AMI释放该消息。
 *               使用EndpointHandler所申请的AMI消息，可以通过任意同属于同一Context
 *               的EndpointHandler释放
 *
 * @return     释放成功时返回ErrorCode::kSuccess
 */
inline int32_t DeleteMessage(EndpointHandler& ep_hdl, ami::Message* msg)
{
    return GenericAmiApplication::DeleteMessage(ep_hdl, msg);
}

/**
 * @brief      删除AMI消息对象
 *
 * @param      ep_hdl  endpoint handler对象
 * @param      msg     待删除的AMI消息
 *
 * @note         默认情况下程序未获得ami消息控制权，当消息发送成功时会被AMI自动释放,
 *               当程序获得ami消息控制权，使用该函数可以通知AMI释放该消息。
 *               使用EndpointHandler所申请的AMI消息，可以通过任意同属于同一Context
 *               的EndpointHandler释放
 *
 * @return     释放成功时返回ErrorCode::kSuccess
 */
inline int32_t DeleteMessage(EndpointHandler* ep_hdl, ami::Message* msg)
{
    return GenericAmiApplication::DeleteMessage(ep_hdl, msg);
}

#define DefaultMakeSingletonContextName() aaf::GenericAmiApplication::MakeSingletonContextName()

#define DefaultMakeHighAvailableContextName() aaf::GenericAmiApplication::MakeHighAvailableContextName()

} // aaf

#endif // AAF_GENERIC_AMI_APPLICATION_H_
