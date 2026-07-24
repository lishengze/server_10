/**
 * @file
 * @brief      monitor framework
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017/02/13
 */
#ifndef ADK_IMPL_MONITOR_H_
#define ADK_IMPL_MONITOR_H_

#include "../entry_wrapper.h"

#include <map>
#include <string>
#include <chrono>
#include <sstream>

#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/asio/steady_timer.hpp>

#include <adk/libadk.h>

namespace adk_impl
{

// query url : "heir:A@a/B@b/C@c"
// query url : "flat:a,b,c"
// query url : "cfg:A@a/key=value"
// A,B,C class name
// a,b,c object name

constexpr uint32_t kDefaultCollectionIntervalMilli = 3000;
const int32_t kInvalidShardingIndex = -1; 

#ifndef ADK_DEFAULT_COLLECTION_INTERVAL_MILLI
#define ADK_DEFAULT_COLLECTION_INTERVAL_MILLI   3000
#endif

/**
 * @brief      监控框架调用该functor以获得查询请求相关的要素，这些要素以输出参数的形式返回给监控框架
 * @param[out]	query_key	查询关键字，整个系统内唯一，用于唯一标识一条监控查询
 * @param[out]	url			查询对象的url，用于唯一标识一个监控查询对象
 * @param[out]	query_condition		查询的条件，该参数会透传给查询对象
 * @param[out]	query_type	查询类型，该参数会透传给查询对象
 */
typedef boost::function<bool (uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type)> QueryFunctorType;

/**
 * @brief      监控框架调用该function以获得查询请求相关的要素，这些要素以输出参数的形式返回给监控框架
 * @param[in]	user		该function绑定的上下文
 * @param[out]	query_key	查询关键字，整个系统内唯一，用于唯一标识一条监控查询
 * @param[out]	url			查询对象的url，用于唯一标识一个监控查询对象
 * @param[out]	query_condition		查询的条件，该参数会透传给查询对象
 * @param[out]	query_type	查询类型，该参数会透传给查询对象
 */
typedef bool (*QueryFunctionType)(void* user, uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type);

/**
 * @brief      监控框架调用该functor以获得推送事件的属性，属性以输出参数的形式返回给监控框架
 * @param[in]	ptree		推送的事件的属性
 */
typedef boost::function<bool (boost::property_tree::ptree& ptree)> EventFunctorType;

/**
 * @brief      监控框架调用该function以获得推送事件的属性，属性以输出参数的形式返回给监控框架
 * @param[out]	user 		该function绑定的上下文
 * @param[in]	ptree		推送的事件的属性
 */
typedef bool (*EventFunctionType)(void* user, boost::property_tree::ptree& ptree);

/**
 * @brief      被监控对象通过该结构向监控框架注册指标收集接口和查询处理接口
 */
struct ADK_API MonitorOps
{
	bool 		is_collection_indicator;			/**< true:收集该对象的监控指标, false:不收集该对象的监控指标, 默认值:false */
	int32_t		collection_interval_milli;			/**< 监控指标收集的周期，将会被取整到100ms的整数倍，默认值:3000 */
	boost::function<bool (boost::property_tree::ptree& indicator)> on_collection_indicator;		/**< 监控对象向框架注册的指标收集接口 */
	boost::function<bool (const int32_t query_type, 	\
						  const boost::property_tree::ptree& query_condition,	\
						  boost::property_tree::ptree& reply)> on_query;	/**< 监控对象向框架注册的指标查询接口 */

	MonitorOps();
};

struct ObjectNode;
typedef std::chrono::system_clock::time_point time_point;
typedef std::map<std::string, ObjectNode*> ObjectNodeMap;
struct ClassNode
{
	bool 							is_collection_indicator;
	uint32_t						collection_interval_milli;

	boost::asio::steady_timer* 		timer;
	ObjectNodeMap					object_map;

	ClassNode()
	{
		collection_interval_milli = ADK_DEFAULT_COLLECTION_INTERVAL_MILLI;
		timer = NULL;
	}
};

// std::string class name
typedef std::map<std::string, ClassNode*> MonitorClassMap;

// std::string object name
typedef std::map<std::string, ObjectNodeMap> MonitorObjectMap;

class EventChannel;
struct ObjectNode
{
    MonitorOps  monitor_ops;
    EventChannel* channel;
    ObjectNode* parent;
    MonitorClassMap* childs;

    ObjectNode()
    {
        channel = nullptr;
        parent = nullptr;
        childs = nullptr;
    }
};

/**
 * @brief      应用可以使用EventChannel向监控框架推送事件
 */
class EventChannel
{
public:
    EventChannel()
    {
        event_sinker_ = NULL;
    }

	~EventChannel() = default;

	// int32_t PushEvent(boost::property_tree::ptree* ptree);			// zero copy
	
	/**
	 * @brief      向监控框架推送事件
	 *
	 * @param[in]  ptree  事件属性树
	 *
	 * @return     成功时返回ErrorCode::kSuccess, 失败时应用需要再次推送，应用需要控制事件
	 * 			   的数量，避免框架过于繁忙，对于非关键事件(Warning)，可以在失败时丢弃，事
	 * 			   件属性树将会被监控框架拷贝
	 */
	int32_t PushEvent(const boost::property_tree::ptree& ptree);

	/**
	 * @brief      向监控框架提供事件解析functor，由框架调用该functor获取相应的推送事件
	 *
	 * @param[in]  func functor对象
	 *
	 * @return     functor将在框架的线程内调用，应用需保证在框架调用该functor时相关的上下文
	 * 			   依然有效
	 */
	int32_t PushEvent(const EventFunctorType& func);

	/**
	 * @brief      向监控框架提供事件解析function，由框架调用该function获取相应的推送事件
	 *
	 * @param[in]  func function指针
	 * @param[in]  user function所绑定的上下文
	 *
	 * @return     function将在框架的线程内调用，应用需保证在框架调用该function时相关的上下文
	 * 			   依然有效
	 */
	int32_t PushEvent(EventFunctionType func, void* user);

	int32_t PushIndicator(const boost::property_tree::ptree& ptree);

private:
	void*			event_sinker_;
	std::string 	class_name_;				// FIXME: using reference?
	std::string 	object_name_;
	friend class Monitor;
};

/**
 * @brief      监控框架将运行期间的所有指标、查询结果、事件递交给该Sinker完成进一步处理(写入文件、或数据库、或进一步传递给其它服务)
 */
class IMonitorSinker
{
public:
	/**
	 * @brief      sinker所支持内容的类型定义
	 */
	enum Type
	{
		kIndicator,			/**< 指标 */
		kEvent,				/**< 事件 */
		kAmiQuery,			/**< 来自ami的查询请求 */
		kHttpQuery,			/**< 来自http的查询请求 */
	};

	IMonitorSinker()
	{}

	virtual ~IMonitorSinker()
	{}

	/**
	 * @brief      sinker用于从框架接收内容的接口，应用需要实现该接口以完成对内容的处理
	 *
	 * @param[in]  type       	内容的类型
	 * @param[in]  query_key  	查询关键字
	 * @param[in]  content  	内容属性树
	 */
	virtual void Receive(IMonitorSinker::Type type, uint64_t query_key, const boost::property_tree::ptree& content)
	{}

	/**
	 * @brief      获取内容类型的描述
	 *
	 * @param[in]  type  内容类型
	 *
	 * @return     内容类型描述
	 */
	const char* GetTypeDesc(IMonitorSinker::Type type)
	{
		switch(type)
		{
			case kIndicator:
				return "Indicator";
			case kEvent:
				return "Event";
			case kAmiQuery:
				return "AmiQuery";
			case kHttpQuery:
				return "HttpQuery";
			default:
				return "Unknown";
		}
	}
};

/**
 * @brief      监控框架
 */
class ADK_API Monitor
{
public:
	/**
	 * @brief      注册监控对象
	 *
	 * @param[in]  class_name    监控对象的类别
	 * @param[in]  object_name   监控对象名
	 * @param      monitor_ops   监控对象所支持的监控操作
	 * @param[in]  parent_class  监控对象的父对象类别
	 * @param[in]  parent_name   监控对象的父对象名
	 *
	 * @return     注册成功时返回EventChannel，用于向监控框架推送事件
	 */
	static EventChannel* RegisterObject(const std::string& class_name, const std::string& object_name, MonitorOps* monitor_ops,
										const std::string& parent_class = std::string(""), const std::string& parent_name = std::string(""));

	/**
	 * @brief      注销监控对象
	 *
	 * @param[in]  class_name    监控对象的类别
	 * @param[in]  object_name   监控对象名
	 * @param      monitor_ops   监控对象所支持的监控操作
	 * @param[in]  parent_class  监控对象的父对象类别
	 * @param[in]  parent_name   监控对象的父对象名
	 *
	 * @return     注册成功时返回ErrorCode::kSuccess
	 */
	static int32_t UnregisterObject(const std::string& class_name, const std::string& object_name,
									const std::string& parent_class = std::string(""), const std::string& parent_name = std::string(""));

	/**
	 * @brief      为监控框架插入Sinker，用于进一步处理指标、查询结果、事件
	 *
	 * @param      sinker  sinker对象
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t PluginSinker(IMonitorSinker* sinker);

	/**
	 * @brief      从监控框架拔出Sinker
	 *
	 * @param      sinker  sinker对象
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t PlugoutSinker(IMonitorSinker* sinker);

	/**
	 * @brief      该接口用于启动监控框架。若监控框架已经启动，再次调用该接口时，该接口会直接返回ErrorCode::kSuccess
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t Start();

	/**
	 * @brief      该接口用于停止监控框架。若监控框架已经停止，再次调用该接口时，该接口会直接返回ErrorCode::kSuccess
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t Stop();

	/**
	 * @brief      该接口用于暂停监控框架。同步调用
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t Suspend();

	/**
	 * @brief      该接口用于恢复监控框架。异步调用
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static void Resume();

	/**
	 * @brief      该接口用于应用分片进程场景下，为每个分片进程单独设置对应的索引以作区分
	 * 
	 * @param      sharding_index  标识每个应用分片进程的索引值
	 * @param      sharding_number 应用分片进程的总数
	 *
	 * @note       传入的参数sharding_index值必须>=0，否则不生效
	 */
	static void SetShardingIndex(int32_t sharding_index, int32_t sharding_number);

	// static int32_t SubmitRequest(const std::string& url, const boost::property_tree::ptree* query_condition = NULL, const int32_t query_type = 0);	
	
	/**
	 * @brief      向监控框架递交查询请求
	 *
	 * @param[in]  query_key        请求关键字
	 * @param[in]  url              请求对象的url
	 * @param[in]  query_condition  查询条件
	 * @param[in]  query_type       查询请求的类型
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t SubmitRequest(const uint64_t query_key, const std::string& url, const boost::property_tree::ptree& query_condition = boost::property_tree::ptree(), const int32_t query_type = 0);
	
	/**
	 * @brief      向监控框架递交用于解析查询请求的functor
	 *
	 * @param[in]  func  functor对象
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t SubmitRequest(const QueryFunctorType& func);

	/**
	 * @brief      向监控框架递交用于解析查询请求的function
	 *
	 * @param[in]  func  function指针
	 * @param      user  function绑定的上下文
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t SubmitRequest(QueryFunctionType func, void* user);

	/**
	 * @brief      向监控框架递交查询请求
	 *
	 * @param[in]  from             标识查询请求的来源(kHttpQuery, kAmiQuery)
	 * @param[in]  query_key        查询请求的关键字
	 * @param[in]  url              查询对象的url
	 * @param[in]  query_condition  查询条件
	 * @param[in]  query_type       查询请求的类型
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t SubmitRequest(IMonitorSinker::Type from, const uint64_t query_key, const std::string& url, const boost::property_tree::ptree& query_condition = boost::property_tree::ptree(), const int32_t query_type = 0);

	/**
	 * @brief      向监控框架递交用于解析查询请求的functor
	 *
	 * @param[in]  from  标识查询请求的来源(kHttpQuery, kAmiQuery)
	 * @param[in]  func  functor对象
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t SubmitRequest(IMonitorSinker::Type from, const QueryFunctorType& func);

	/**
	 * @brief      向监控框架递交用于解析查询请求的function
	 *
	 * @param[in]  from  标识查询请求的来源(kHttpQuery, kAmiQuery)
	 * @param[in]  func  function指针
	 * @param      user  function绑定的上下文
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t SubmitRequest(IMonitorSinker::Type from, QueryFunctionType func, void* user);

	// helper functions
	
	/**
	 * @brief      修改收集指标的周期
	 *
	 * @param[in]  class_name  对象的类别
	 * @param[in]  milli       新的收集周期，单位为ms
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t ChangeCollectionInterval(const std::string& class_name, uint32_t milli);

	/**
	 * @brief      打开指标收集开关，监控框架下次收集指标时包含该对象的指标
	 *
	 * @param[in]  class_name   对象的类别
	 * @param[in]  object_name  对象名
	 * @param[in]  milli        收集周期，单位为ms
	 *
	 * @return     成功时返回ErrorCode::kSuccess
	 */
	static int32_t EnableCollection(const std::string& class_name, const std::string& object_name, uint32_t milli);
};

/**
 * @brief      辅助应用完成监控对象的注册，该接口注册监控对象位于url中的第一层(即没有父节点)
 *
 * @param[in]      type      监控对象的类型，注意这里是C++语言中的类名，非类名字符串
 * @param[in]      obj_name  监控对象名
 * @param[in]      ops_ptr   监控对象所支持的监控操作
 *
 * @return     成功时返回ErrorCode::kSuccess
 */
#ifndef REGISTER_OBJECT
#define REGISTER_OBJECT(type, obj_name, ops_ptr) adk_impl::Monitor::RegisterObject(#type, obj_name, (ops_ptr));
#endif

#ifndef UNREGISTER_OBJECT
#define UNREGISTER_OBJECT(type, obj_name) adk_impl::Monitor::UnregisterObject(#type, obj_name);
#endif


/**
 * @brief      辅助应用完成监控对象的注册
 *
 * @param[in]      type             监控对象的类型，注意这里是C++语言中的类名，非类名字符串
 * @param[in]      obj_name         监控对象名
 * @param[in]      ops_ptr          监控空对象所支持的监控操作
 * @param[in]      parent_type      监控对象的父对象所属类型，注意这里是C++语言中的类名，非类名字符串
 * @param[in]      parent_obj_name  父对象名
 *
 * @return     成功时返回ErrorCode::kSuccess
 */
#ifndef REGISTER_OBJECT_AND_PARENT
#define REGISTER_OBJECT_AND_PARENT(type, obj_name, ops_ptr, parent_type, parent_obj_name) adk_impl::Monitor::RegisterObject(#type, obj_name, (ops_ptr), #parent_type, parent_obj_name)
#endif

} // adk

#endif // ADK_MONITOR_H_
