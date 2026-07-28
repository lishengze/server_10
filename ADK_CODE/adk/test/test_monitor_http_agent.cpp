#include <boost/property_tree/ptree.hpp>
#include <adk/error_code.h>
#include <adk/monitor/monitor.h>
#include <adk/monitor/http_agent.h>

bool OnCollectContextIndicator(boost::property_tree::ptree& indicator)
{
    static int rx_msg_num = 0;
    indicator.put("rx_msg_num", ++rx_msg_num);
    indicator.put("role", "leader");
    return true;
}

bool OnCollectEndpointIndicator(boost::property_tree::ptree& indicator)
{
    static int tx_msg_num = 0;
    indicator.put("tx_msg_num", ++tx_msg_num);
    indicator.put("partition", "1,2,3");
    return true;
}

void OnError(const boost::system::system_error& error)
{
    std::cout << "Error: " << error.what() << std::endl;
    exit(1);
}

int main(int argc, char* argv[])
{
    adk::MonitorOps ctx_mon_ops;
    ctx_mon_ops.is_collection_indicator = true;
    ctx_mon_ops.on_collection_indicator = &OnCollectContextIndicator;
    adk::EventChannel *ec = adk::Monitor::RegisterObject(
                                "Context", "TE_1_1_11", &ctx_mon_ops);

    adk::MonitorOps ep_mon_ops_1;
    ep_mon_ops_1.is_collection_indicator = true;
    ep_mon_ops_1.on_collection_indicator = &OnCollectEndpointIndicator;
    adk::Monitor::RegisterObject("Endpoint", "Order", &ep_mon_ops_1);

    adk::MonitorOps ep_mon_ops_2;
    ep_mon_ops_1.is_collection_indicator = true;
    ep_mon_ops_1.on_collection_indicator = &OnCollectEndpointIndicator;
    adk::Monitor::RegisterObject("Endpoint", "Relay", &ep_mon_ops_1);

    if (adk::ErrorCode::kSuccess != adk::Monitor::Start())
    {
        std::cout << "Start monitor failed." << std::endl;
    }

    adk::monitor::HttpAgent agent;
    agent.Start(5002, std::string(), &OnError);

    boost::property_tree::ptree event;
    int time = 0;
    event.put("Source", "Context/TE_1_1_11");
    while (true)
    {
        event.put("Time", ++time);
        std::cout << ec->PushEvent(event) << std::endl;
        sleep(5);
    }

    return 0;
}
