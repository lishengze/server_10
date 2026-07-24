/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_RECORD_CLIENT_H_
#define AMI_RECORD_CLIENT_H_

///< cpp std
#include <string>
#include <vector>

///< BOOST
#include <boost/asio/io_service.hpp>
#include <boost/filesystem.hpp>

///< ami public
#include <ami/error_code.h>
#include <ami/property.h>

///< impl
#include "record_agent.h"
#include "record_channel.h"

namespace ami
{

/**
 * @brief 持久化客户端抽象类
 *
 * @par 线程安全
 * 由于事先本接口的类以线程单例的形式存在，因此要求其线程安全
 */
class RecordClient
{
public:
    RecordClient() {}
    virtual ~RecordClient() {}

    /**
     * 初始化
     *
     * @param recorder_id 拟连接的recorder的id
     * @param ctx_props 
     * @param ios 如果不为nullptr，则使用本参数作为线程池（请保证该io_service线程池中
     * 有且只有一个线程）；如果为nullptr，则使用自己的创建的线程。
     *
     * @return kSuccess - 成功，kFailure - 失败
     *
     * @par 入参ctx_props确定的配置中，生效的配置如下：
     * @li config::context::recorder::kPort
     *
     */
    virtual ErrorCode_def Init(const RecorderId& recorder_id, const Property& ctx_props,
                               const RecordAgent::OnRcdNotExistType& on_recorder_ne =
                                   RecordAgent::OnRcdNotExistType(),
                               boost::asio::io_service* ios = nullptr) = 0;

    virtual ErrorCode_def Start(std::string& data_path,
                                const std::string& ctx_name,
                                bool is_recovery) = 0;

    virtual std::vector<RxRecordChannel*>
    CreateMergeChannels(const std::string& context_name,
                        const Property& prop) = 0;

    virtual TxRecordChannel* CreateMessageChannel(const std::string& context_name, const std::string& transport_name,
                                                  const Property& prop) = 0;

    virtual StRecordChannel*
    CreateStatusChannel(const std::string& context_name,
                        const Property& prop) = 0;

    virtual void Stop() = 0;

    virtual void DetachSharedMemory() = 0;
    
    virtual int32_t IncreSyncIdMaps(Property& id_objects) = 0;
    
private:
    RecordClient(const RecordClient&) = delete;
    RecordClient& operator=(const RecordClient&) = delete;
};

}  // namespace ami

#endif /* AMI_RECORD_CLIENT_H_ */
