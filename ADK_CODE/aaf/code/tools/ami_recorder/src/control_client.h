/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

#ifndef AMI_CONTROL_CLIENT_H_
#define AMI_CONTROL_CLIENT_H_

///< boost
#include <boost/asio.hpp>

///< ami public
#include <ami/error_code.h>
#include <ami/property.h>

///< ami impl
#include "../log.h"

///< impl
#include "control_connection.h"
#include "recorder_base.h"

namespace ami
{

class ControlClient : public ControlConnection
{
public:
    ControlClient() {}
    ~ControlClient() {}

    ErrorCode_def Init(const RecorderId& recorder_id, const OnRequestHandler& on_request,
                       boost::asio::io_service* io_service   = nullptr,
                       const OnPeerNEHandlerType& on_peer_ne = OnPeerNEHandlerType());

    ErrorCode_def Start();
    void Stop();

    /**
     * 给recorder发送请求并获取响应消息
     *
     * @param req_buf
     * @param req_len
     * @param rep_buf[out]     响应消息的缓冲区间
     * @param rep_len[in, out] 响应消息缓冲区间的长度
     * 入参表示rep_buf的可用空间长度，出参表示实际收到的包长
     *
     * @note 同步方式发送请求并获取响应
     * 可能阻塞
     */
    ErrorCode_def Request(const void* req_buf,
                          uint32_t req_len,
                          void* rep_buf,
                          uint32_t* rep_len,
                          const boost::function<void()>& on_idle = []() { usleep(1000); });

private:
    static constexpr const char* kRole = "ControlClient";

    virtual std::string WhoAmI() const
    {
        return std::string(kRole);
    }

    ErrorCode_def Connect();
    void DetectEOF();

    void HandleEOF(const boost::system::error_code& ec,
                   std::size_t bytes_transferred);

    EndpointType endpoint_;
    char any_char_;

    LOG_DECLARE
};

}  // namespace ami

#endif /* AMI_CONTROL_CLIENT_H_ */
