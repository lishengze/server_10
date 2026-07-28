/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

///< posix

///< cpp std
#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>

///< BOOST
#include <boost/locale/format.hpp>  //format

///< adk, ami public
#include <ami/config_key.h>

///< ami impl
#include "../log.h"

///< impl
#include "config_default_value.h"
#include "control_connection.h"
#include "control_message_key.h"

//在日志中区分control_client还是control_server
#define RLOG_TRACE(...) \
    LOG_TRACE("{1} {2}", WhoAmI(), ami::FormatLog(__VA_ARGS__))
#define RLOG_DEBUG(...) \
    LOG_DEBUG("{1} {2}", WhoAmI(), ami::FormatLog(__VA_ARGS__))
#define RLOG_INFO(...) \
    LOG_INFO("{1} {2}", WhoAmI(), ami::FormatLog(__VA_ARGS__))
#define RLOG_WARN(...) \
    LOG_WARN("{1} {2}", WhoAmI(), ami::FormatLog(__VA_ARGS__))
#define RLOG_ERROR(...) \
    LOG_ERROR("{1} {2}", WhoAmI(), ami::FormatLog(__VA_ARGS__))
#define RLOG_FATAL(...) \
    LOG_FATAL("{1} {2}", WhoAmI(), ami::FormatLog(__VA_ARGS__))

namespace bs  = boost::system;
namespace ba  = boost::asio;
namespace bae = boost::asio::error;
namespace bl  = boost::locale;

namespace ami
{

LOG_DEFINE(ami::ControlConnection)

ErrorCode_def
ControlConnection::Init(const OnRequestHandler& on_request,
                        ba::io_service* io_service, const OnPeerNEHandlerType& on_peer_ne)
{
    if (io_service != nullptr)
    {
        io_service_ = io_service;
    }
    else
    {
        io_service_  = new ba::io_service;
        have_thread_ = true;
    }

    socket_            = new SocketType(*io_service_);
    on_request_        = on_request;
    on_peer_not_exist_ = on_peer_ne;

    rx_buf_       = new char[kMaxMsgBodyLen];
    tx_reply_buf_ = new char[kMaxReplyMsgBodyLen];
    reply_buf_    = new char[kMaxReplyMsgBodyLen];

    return ErrorCode::kSuccess;
}

ErrorCode_def ControlConnection::Start()
{
    if (have_thread_)
    {
        io_work            = new ba::io_service::work(*io_service_);
        io_service_thread_ = adk::boost_thread("ami-controlconn", "io service thread",
                                               boost::bind(&ba::io_service::run, io_service_));
    }

    return kSuccess;
}

void ControlConnection::Stop()
{
    if (have_thread_)
    {
        if (io_service_)
            io_service_->stop();
        if (io_service_thread_.joinable())
            io_service_thread_.join();

        // delete io_service_;
        // io_service_ = nullptr;

        // delete io_work;
        // io_work = nullptr;
    }
    else
    {
        io_service_ = nullptr;
    }

    // delete reply_buf_;
    // reply_buf_ = nullptr;
    // delete tx_reply_buf_;
    // tx_reply_buf_ = nullptr;
    // delete rx_buf_;
    // rx_buf_ = nullptr;
    // delete socket_;
    // socket_ = nullptr;
}

ErrorCode_def
ControlConnection::SendRequest(const void* request_buf,
                               uint32_t request_len)
{
    RLOG_TRACE("try send request(s[{2}]): {1}",
               std::string(
                   static_cast<const char*>(request_buf),
                   request_len),
               tx_request_header_.request_sqn);

    bs::error_code ec;
    std::vector<ba::const_buffer> msg {
        ba::const_buffer(&tx_request_header_, sizeof(Header)),
        ba::const_buffer(request_buf, request_len)};

    ba::write(*socket_, msg, ec);
    if (ec)
    {
        return kFailure;
    }
    else
    {
        WaitHeader();
        return kSuccess;
    }
}

void ControlConnection::WaitHeader()
{
    ba::async_read(*socket_,
                   ba::buffer(&rx_header_, sizeof(Header)),
                   ba::transfer_at_least(sizeof(Header)),
                   boost::bind(&ControlConnection::HandleHeader, this,
                               ba::placeholders::error,
                               ba::placeholders::bytes_transferred));
    RLOG_TRACE("waiting header...");
}

void ControlConnection::WaitRequest()
{
    ba::async_read(*socket_,
                   ba::buffer(rx_buf_, rx_header_.length),
                   ba::transfer_at_least(rx_header_.length),
                   boost::bind(&ControlConnection::HandleRequest, this,
                               ba::placeholders::error,
                               ba::placeholders::bytes_transferred));
    RLOG_TRACE("waiting request...");
}

void ControlConnection::WaitReply()
{
    ba::async_read(*socket_,
                   ba::buffer(rx_buf_, rx_header_.length),
                   ba::transfer_at_least(rx_header_.length),
                   boost::bind(&ControlConnection::HandleReply, this,
                               ba::placeholders::error,
                               ba::placeholders::bytes_transferred));
    RLOG_TRACE("waiting reply...");
}

void ControlConnection::HandleHeader(const bs::error_code& ec,
                                     std::size_t bytes_transferred)
{
    RLOG_TRACE("HandleHeader");

    if (ec)
    {
        if (bae::eof == ec)
        {
            RLOG_INFO("peer not exist");
            CallPeerNECb();
            reply_len_    = 0;
            response_got_ = true;
            return;
        }
        else if (bae::operation_aborted == ec)
        {
            RLOG_INFO("should be quiting actively");
            reply_len_    = 0;
            response_got_ = true;
            return;
        }
        else
        {
            RLOG_ERROR("error happened: {1}", ec.message());
            reply_len_    = 0;
            response_got_ = true;
            WaitHeader();
        }
    }
    else
    {
        if (rx_header_.length > kMaxMsgBodyLen)
        {
            std::ostringstream os;
            os.setf(std::ios_base::hex, std::ios_base::basefield);
            os.setf(std::ios_base::showbase);
            std::ostream_iterator<unsigned int> oi(os, " ");
            std::copy((const char*)(&rx_header_),
                      ((const char*)(&rx_header_) + sizeof(Header)),
                      oi);

            RLOG_ERROR("too large message length, "
                       "it's a corrupted message header: {1}, {2}",
                       os.str(),
                       std::string((const char*)&rx_header_, sizeof(Header)));
            WaitHeader();
            return;
        }

        switch (rx_header_.type)
        {
        case kRequest:
            WaitRequest();
            break;
        case kReply:
            WaitReply();
            break;
        }
    }
}

void ControlConnection::HandleRequest(const bs::error_code& ec,
                                      std::size_t bytes_transferred)
{
    if (ec)
    {
        if (bae::eof == ec)
        {
            RLOG_INFO("peer not exist");
            CallPeerNECb();
            reply_len_    = 0;
            response_got_ = true;
            return;
        }
        else if (bae::operation_aborted == ec)
        {
            RLOG_INFO("should be quiting actively");
            reply_len_    = 0;
            response_got_ = true;
            return;
        }
        else
        {
            RLOG_ERROR("error happened: {1}", ec.message());
            reply_len_    = 0;
            response_got_ = true;
        }
    }
    else
    {
        RLOG_TRACE("receive request(s[{2}]): {1}",
                   std::string(rx_buf_, bytes_transferred),
                   rx_header_.request_sqn);

        tx_reply_header_.request_sqn = rx_header_.request_sqn;
        if (on_request_(rx_buf_, rx_header_.length, tx_reply_buf_,
                        &(tx_reply_header_.length = kMaxReplyMsgBodyLen)))
        {
        }
        else
        {
            Property failed_reply;
            failed_reply.SetValue(kMessageType, ControlMessageType::kActionFail);
            std::string failed_reply_str = failed_reply.Dump(false);
            memcpy(tx_reply_buf_, failed_reply_str.c_str(),
                   (tx_reply_header_.length = failed_reply_str.length()));
        }

        RLOG_TRACE("try send reply(s[{2}]): {1}",
                   std::string(tx_reply_buf_, tx_reply_header_.length),
                   tx_reply_header_.request_sqn);
        std::vector<ba::const_buffer> reply_msg {
            ba::const_buffer(&tx_reply_header_, sizeof(Header)),
            ba::const_buffer(tx_reply_buf_, tx_reply_header_.length)};
        bs::error_code ec1;
        ba::write(*socket_, reply_msg, ec1);
    }

    WaitHeader();
}

void ControlConnection::HandleReply(const bs::error_code& ec,
                                    std::size_t bytes_transferred)
{
    if (ec)
    {
        if (bae::eof == ec)
        {
            RLOG_INFO("peer not exist");
            CallPeerNECb();
            reply_len_ = 0;
        }
        else if (bae::operation_aborted == ec)
        {
            RLOG_INFO("should be quiting actively");
            reply_len_ = 0;
        }
        else
        {
            RLOG_ERROR("error happened: {1}", ec.message());
            reply_len_ = 0;
        }
    }
    else
    {
        RLOG_TRACE("receive reply(s[{2}]): {1}",
                   std::string(rx_buf_, rx_header_.length),
                   rx_header_.request_sqn);

        if (rx_header_.request_sqn == tx_request_header_.request_sqn)
        {
            memcpy(reply_buf_, rx_buf_, (reply_len_ = rx_header_.length));
        }
        else
        {  //收到的回复消息序号不符合预期
            RLOG_WARN("unexpedted reply(s[{1}]), expecting s[{2}]",
                      rx_header_.request_sqn,
                      tx_request_header_.request_sqn);
            reply_len_ = 0;
        }
    }

    response_got_ = true;
}

}  // namespace ami
