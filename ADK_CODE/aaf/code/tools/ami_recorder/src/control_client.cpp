/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

/// ami public header
#include <ami/config_key.h>

/// ami impl
#include "../log.h"
#include "../util.h"

///< impl
#include "config_default_value.h"
#include "control_client.h"

namespace ba   = boost::asio;
namespace bae  = boost::asio::error;
namespace bai  = boost::asio::ip;
namespace bs   = boost::system;
namespace bl   = boost::locale;
namespace accr = ami::config::context::recorder;
namespace rcdv = ami::recorder::cdv;

namespace ami
{

LOG_DEFINE(ami::ControlClient)
constexpr const char* ControlClient::kRole;

ErrorCode_def
ControlClient::Init(const RecorderId& recorder_id, const OnRequestHandler& on_request,
                    boost::asio::io_service* io_service, const OnPeerNEHandlerType& on_peer_ne)
{
    IF_ERR_RET(ControlConnection::Init(on_request, io_service, on_peer_ne),
               LOG_ERROR("Init control client failed."));

    endpoint_ = EndpointType(UnixSockPath(recorder_id));
    return ami::kSuccess;
}

ErrorCode_def ControlClient::Start()
{
    LOG_DEBUG("starting...");

    IF_ERR_RET(Connect());

    DetectEOF();
    ControlConnection::Start();

    LOG_DEBUG("started");
    return ErrorCode::kSuccess;
}

void ControlClient::Stop()
{
    LOG_INFO("stopping...");

    bs::error_code ec;
    socket_->close(ec);

    ControlConnection::Stop();
    LOG_INFO("stopped ok");
}

ErrorCode_def ControlClient::Connect()
{
    bs::error_code ec;
    socket_->connect(endpoint_, ec);
    if (ec)
    {
        LOG_ERROR("connect to '{1}' error: {2}",
                  endpoint_.path(), ec.message());

        return ErrorCode::kFailure;
    }

    LOG_INFO("connect to '{1}' ok", endpoint_.path());
    return ErrorCode::kSuccess;
}

ErrorCode_def ControlClient::Request(const void* request_buf, uint32_t request_len,
                                     void* reply_buf, uint32_t* reply_len,
                                     const boost::function<void()>& on_idle)
{
    ///< 将探测recorder侧的异步读请求取消
    bs::error_code bs_ec;
    socket_->cancel(bs_ec);

    LOG_DEBUG("requesting...");
    response_got_             = false;
    tx_request_header_.length = request_len;
    ++tx_request_header_.request_sqn;
    IF_ERR_RET(SendRequest(request_buf, request_len));

    IntervalLogger inv_logger(5);
    while (false == response_got_)
    {
        on_idle();
        INV_LOG_DEBUG(inv_logger, "requesting...");
    }

    LOG_DEBUG("response got.");

    if (reply_len_ > 0)
    {
        memcpy(reply_buf, reply_buf_, (*reply_len = reply_len_));
        DetectEOF();
        return ErrorCode::kSuccess;
    }
    else
    {
        DetectEOF();
        return ErrorCode::kFailure;
    }
}

void ControlClient::HandleEOF(const bs::error_code& ec, std::size_t)
{
    if (!ec)
    {
        LOG_ERROR("nothing happened(should not happend!?)");
        DetectEOF();
    }
    else if (bae::operation_aborted == ec)
    {
        LOG_DEBUG("{1}", ec.message());
    }
    else if (bae::eof == ec)
    {
        LOG_INFO("peer not exist(tcp broken)");
        CallPeerNECb();
    }
    else
    {
        LOG_ERROR("error happened: {1}", ec.message());
        DetectEOF();
    }
}

void ControlClient::DetectEOF()
{
    ba::async_read(*socket_,
                   ba::buffer(&any_char_, 1u),
                   boost::bind(&ControlClient::HandleEOF, this,
                               ba::placeholders::error,
                               ba::placeholders::bytes_transferred));
    LOG_TRACE("detecting eof...");
}

}  //namespace ami
