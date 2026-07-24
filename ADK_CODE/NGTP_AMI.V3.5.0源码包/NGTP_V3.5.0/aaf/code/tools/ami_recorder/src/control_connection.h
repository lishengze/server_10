/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

#ifndef AMI_CONTROL_CONNECTION_H_
#define AMI_CONTROL_CONNECTION_H_

///< cpp std
#include <stdint.h>

///< boost
#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/thread.hpp>
#include <boost/utility.hpp>

///< adk, ami public
#include <ami/error_code.h>
#include <ami/property.h>

#include <adk/entry_wrapper.h>

///< ami impl
#include "../log.h"

namespace ami
{

/**
 * 做为ControlClient和ControlServer的基类，实现一些两者可以复用的功能
 */
class ControlConnection : private boost::noncopyable
{
public:
    typedef boost::function<bool(const void* request_buf,
                                 uint32_t request_len,
                                 void* reply_buf,
                                 uint32_t* reply_len)>
        OnRequestHandler;
    typedef boost::function<void()> OnPeerNEHandlerType;

    static const uint32_t kMaxMsgBodyLen = 4 * 1024u * 1024u;
    static constexpr uint32_t kMaxReplyMsgBodyLen = 4u * 1024u;

protected:
    typedef boost::asio::local::stream_protocol::endpoint EndpointType;
    typedef boost::asio::local::stream_protocol::socket SocketType;

    struct Header
    {
        uint32_t length;
        uint32_t type;
        uint64_t request_sqn;
    };

    static const uint32_t kRequest          = 1;
    static const uint32_t kReply            = 2;
    static const uint32_t MAX_MSG_TOTAL_LEN = kMaxMsgBodyLen + sizeof(Header);

protected:
    ControlConnection() {}
    virtual ~ControlConnection() {}
    ControlConnection(const ControlConnection&) = delete;
    ControlConnection& operator=(const ControlConnection&) = delete;

    /**
     * 初始化
     *
     * @param props
     * @param on_request
     * @param io_service 如果不传入外部io_service，则自己创建io_service
     * 并会在调用Start以后，创建驱动线程
     @ @param on_peer_ne 对端退出本端收到eof以后调用
     *
     * @return 错误码
     */
    ErrorCode_def Init(const OnRequestHandler& on_request, boost::asio::io_service* io_service,
                       const OnPeerNEHandlerType& on_peer_ne);
    ErrorCode_def Start();
    void Stop();

    ErrorCode_def SendRequest(const void* request_buf, uint32_t request_len);
    void WaitHeader();
    void WaitRequest();
    void WaitReply();

    void HandleHeader(
        const boost::system::error_code& ec,
        std::size_t bytes_transferred);

    void HandleRequest(
        const boost::system::error_code& ec,
        std::size_t bytes_transferred);

    void HandleReply(
        const boost::system::error_code& ec,
        std::size_t bytes_transferred);

    void CallPeerNECb()
    {
        if (!on_peer_not_exist_.empty())
        {
            try
            {
                on_peer_not_exist_();
            }
            catch (...)
            {
            }
        }
    }

private:
    virtual std::string WhoAmI() const = 0;

protected:
    boost::asio::io_service* io_service_   = nullptr;
    boost::asio::io_service::work* io_work = nullptr;
    bool have_thread_                      = false;
    boost::thread io_service_thread_;
    SocketType* socket_ = nullptr;

    /***********************
     * 发送请求
     */
    Header tx_request_header_ = {0, kRequest, 0};
    /************************/

    /***********************
     * 接收请求
     */
    Header rx_header_ = {0, 0, 0};
    char* rx_buf_     = nullptr;
    OnRequestHandler on_request_;
    /************************/

    /***********************
     * 发送响应
     */
    Header tx_reply_header_ = {0, kReply, 0};
    char* tx_reply_buf_     = nullptr;
    /************************/

    /***********************
     * 接收响应
     */
    char* reply_buf_    = nullptr;
    uint32_t reply_len_ = 0;
    bool response_got_  = false;
    /************************/

    OnPeerNEHandlerType on_peer_not_exist_;

    LOG_DECLARE
};

}  // namespace ami

#endif /* AMI_CONTROL_CONNECTION_H_ */
