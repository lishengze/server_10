/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

// std headers
#include <chrono>

///< boost
#include <boost/locale/format.hpp>  //format

///< ami public
#include <ami/config_key.h>

///< ami impl
#include "../util.h"

///< impl
#include "config_default_value.h"
#include "control_server.h"
#include "recorder.h"

namespace ba  = boost::asio;
namespace bsi = boost::asio::ip;
namespace bs  = boost::system;
namespace bl  = boost::locale;

namespace ami
{

namespace bs   = boost::system;
namespace bae  = boost::asio::error;
namespace bf   = boost::filesystem;
namespace ccr  = config::context::recorder;
namespace rcdv = recorder::cdv;

LOG_DEFINE(ami::ControlServer)

constexpr const char* ControlServer::kRole;

ErrorCode_def ControlServer::Init(const OnRequestHandler& on_request,
                                  boost::asio::io_service* io_service, const OnPeerNEHandlerType& on_peer_ne)
{
    IF_ERR_RET(ControlConnection::Init(on_request, io_service, on_peer_ne),
               LOG_ERROR("Init control server failed."));

    return ErrorCode::kSuccess;
}

ErrorCode_def ControlServer::Start()
{
    LOG_INFO("starting...");

    try
    {
        bs::error_code ec;
        const auto unix_sock_path = UnixSockPath(Recorder::GetId());
        bf::remove(unix_sock_path, ec);

        acceptor_ = new AcceptorType(*io_service_, EndpointType(unix_sock_path));
        auto timer = new boost::asio::steady_timer(*io_service_);
        timer->expires_from_now(std::chrono::seconds(10));
        timer->async_wait(boost::bind(&ControlServer::WaitAcceptTimeOut, this, ba::placeholders::error, timer));
        Accept();
    }
    catch (const bs::system_error& e)
    {
        LOG_ERROR("{1}", e.what());
    }

    IF_ERR_RET(ControlConnection::Start());

    LOG_INFO("started, ready for connection request now.");
    return ErrorCode::kSuccess;
}

void ControlServer::Stop()
{
    LOG_INFO("stopping...");

    ControlConnection::Stop();

    bs::error_code ec;
    if (acceptor_ != nullptr)
        acceptor_->close(ec);
    bf::remove(UnixSockPath(Recorder::GetId()), ec);
    if (socket_ != nullptr)
        socket_->close(ec);

    // delete acceptor_;
    // acceptor_ = nullptr;
    LOG_INFO("stopped ok");
}

void ControlServer::Accept()
{
    acceptor_->async_accept(
        *socket_,
        boost::bind(
            &ControlServer::HandleAccept, this,
            ba::placeholders::error));

    LOG_INFO("accept at '{1}'", acceptor_->local_endpoint().path());
}

void ControlServer::HandleAccept(const bs::error_code& ec)
{
    if (ec)
    {
        if (bae::operation_aborted == ec)
        {
            LOG_INFO("should be quiting actively");
            return;
        }

        LOG_ERROR("{1}", ec.message());
        Accept();
    }
    else
    {
        is_hasaccept_ = true;
        LOG_INFO("accept ok");

        //连接建立成功以后就可以关闭acceptor并且删除文件了
        bs::error_code ec;
        acceptor_->close(ec);
        bf::remove(UnixSockPath(Recorder::GetId()), ec);

        WaitHeader();
    }
}

void ControlServer::WaitAcceptTimeOut(const boost::system::error_code &ec, boost::asio::steady_timer *timer)
{
    if (!is_hasaccept_)
    {
        LOG_ERROR("wait accept time out, recorder will stop");
        Recorder::Stop(false);
    }

    delete timer;
}

}  // namespace ami
