#include <assert.h>

#include <iostream>
#include <stdexcept>

#include <aaf.h>

#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

using namespace aaf;

class AppMsgHdl : public ami::MessageHandler
{
public:
    virtual ~AppMsgHdl() {}

    virtual void OnMessage(ami::Message*)
    {
        std::cout << "app message handler" << std::endl;
    }
};

class MyMessageHandler : public ami::MessageHandler
{
public:
    virtual void OnMessage(ami::Message* msg)
    {
        std::cout << "MyMessageHandler sqn : " << msg->str() << std::endl;
    }
}g_my_msg_hdl;

class AmiApp : public GenericAmiApplication 
{
public:
    AmiApp()
    {
        sqn_ = 0;
        total_messages_ = 0;
    }

    ~AmiApp()
    {}

    // configure program options
    virtual void SetAmiAppOption()
    {
        AddOptionWithAcceptor("sqn", "set the first sqn, default 1", uint32_t(1), sqn_);
        AddOptionWithAcceptor("total-messages", "set the total messages to tx/rx", uint32_t(-1u), total_messages_);
    }

    // =======================================================================================
    
    // configure aaf
    void OnConfigureFramework(ami::Property& fw_props)
    {
        // fw_props.SetValue(config::kEnableSingletonContext, true);
        fw_props.SetValue(config::kEnableHighAvailableContext, true);
        fw_props.SetValue(config::kEnableAppNameCheck, false);
    }

    void OnRoleChangeToLeader()
    {
        std::cout << "role change to leader" << std::endl;
    }

    // =======================================================================================

    // create TxEndpoints
    virtual int32_t OnTxEndpointCreation(EndpointHandler* ep_hdl, const std::string& ep_name)
    {
        std::cout << "tx ep_name: " << ep_name << std::endl;
        return ErrorCode::kSuccess;
    }

    // create RxEndpoints
    virtual int32_t OnRxEndpointCreation(const std::string& ep_name, ami::MessageHandler** msg_hdl, bool is_ha_ctx)
    {
        std::cout << "rx ep_name: " << ep_name;
        if (IsMasterRxEndpoint(ep_name))
        {
            std::cout << ", is master rxep" << std::endl;
            return ErrorCode::kSuccess;
        }
        else
        {
            std::cout << std::endl;
        }

        *msg_hdl = &g_my_msg_hdl;
        return ErrorCode::kSuccess;
    }

    // =======================================================================================
    virtual int32_t OnAmiInitEnd()
    {
        ep_hdl_test_ = CreateTxEndpoint("MXXX");

        auto& rxep_set = GetRxEndpointSet();
        std::cout << "rxep = " << boost::algorithm::join(rxep_set, ",") << std::endl;

        auto& txep_set = GetTxEndpointSet();
        std::cout << "txep = " << boost::algorithm::join(txep_set, ",") << std::endl;

        auto& tx_sid_set = GetTxStreamIDs();
        std::cout << "tx_sid: ";
        for (auto sid : tx_sid_set)
        {
            std::cout << sid << ", ";
        }
        std::cout << std::endl;

        for (auto tp_id : tx_sid_set)
        {
            auto* tp_info = GetTransportInfo(tp_id);
            assert(tp_info != NULL);
            auto& transport_info = *tp_info;
            std::cout << "======================================================================" << std::endl;
            std::cout << "transport_info.transport_id = " << transport_info.transport_id << ", " << std::endl;
            std::cout << "transport_info.tier_name = " << transport_info.tier_name << ", " << std::endl;
            std::cout << "transport_info.endpoint_name = " << transport_info.endpoint_name << ", " << std::endl;
            std::cout << "transport_info.transport_partition = " << transport_info.transport_partition << ", " << std::endl;
            std::cout << "transport_info.transport_name = " << transport_info.transport_name << ", " << std::endl;
            std::cout << "transport_info.transport_direction = " << transport_info.transport_direction << std::endl;
        }

        auto& rx_sid_set = GetRxStreamIDs();
        std::cout << "rx_sid: ";
        for (auto sid : rx_sid_set)
        {
            std::cout << sid << ", ";
        }
        std::cout << std::endl;

        for (auto tp_id : rx_sid_set)
        {
            auto* tp_info = GetTransportInfo(tp_id);
            assert(tp_info != NULL);
            auto& transport_info = *tp_info;
            std::cout << "======================================================================" << std::endl;
            std::cout << "transport_info.transport_id = " << transport_info.transport_id << ", " << std::endl;
            std::cout << "transport_info.tier_name = " << transport_info.tier_name << ", " << std::endl;
            std::cout << "transport_info.endpoint_name = " << transport_info.endpoint_name << ", " << std::endl;
            std::cout << "transport_info.transport_partition = " << transport_info.transport_partition << ", " << std::endl;
            std::cout << "transport_info.transport_name = " << transport_info.transport_name << ", " << std::endl;
            std::cout << "transport_info.transport_direction = " << transport_info.transport_direction << std::endl;
        }

        return aaf::ErrorCode::kSuccess;
    }

    // =======================================================================================

    virtual int32_t OnRun()
    {
        if (ep_hdl_test_ == NULL)
        {
            StopAmiApp();
            return ErrorCode::kPassed;
        }

        ami::Message* msg = aaf::NewMessage(ep_hdl_test_, 128);
        std::string sqn_str = (boost::format("%1%") % sqn_).str();
        msg->append(sqn_str.c_str(), sqn_str.size());
        ep_hdl_test_->SendMsg(msg);
        ++sqn_;
        if (sqn_ == total_messages_ + 1)
            StopAmiApp();
        return ErrorCode::kPassed;
    }

    virtual std::string MakeHighAvailableContextName()
    {
        return GetApplicationName();
    }

    virtual std::string MakeSingletonContextName()
    {
        return GetApplicationName() + "_S";
    }

private:
    uint32_t         sqn_;
    uint32_t         total_messages_;
    EndpointHandler* ep_hdl_test_;
} g_ami_app;
