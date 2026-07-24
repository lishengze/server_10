/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

#ifndef AMI_CONTROL_SERVER_H_
#define AMI_CONTROL_SERVER_H_

///< boost
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

///< ami public
#include <ami/error_code.h>
#include <ami/property.h>

///ami impl
#include "../log.h"

///< impl
#include "control_connection.h"

namespace ami
{

class ControlServer : public ControlConnection
{
private:
    typedef boost::asio::local::stream_protocol::acceptor AcceptorType;

public:
    ControlServer() {}
    ~ControlServer() {}

    ErrorCode_def Init(const OnRequestHandler& on_request,
                       boost::asio::io_service* io_service   = nullptr,
                       const OnPeerNEHandlerType& on_peer_ne = OnPeerNEHandlerType());

    ErrorCode_def Start();
    void Stop();

private:
    static constexpr const char* kRole = "ControlServer";

    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    virtual std::string WhoAmI() const
    {
        return std::string(kRole);
    }

    void Accept();
    void HandleAccept(const boost::system::error_code& ec);

    void WaitAcceptTimeOut(const boost::system::error_code &ec, boost::asio::steady_timer *timer);

    AcceptorType* acceptor_ = nullptr;
    bool is_hasaccept_ = false; 

    LOG_DECLARE
};

}  // namespace ami

#endif /* AMI_CONTROL_SERVER_H_ */
