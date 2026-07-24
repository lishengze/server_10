#include <aaf.h>

#include <vector>
#include <unordered_map>
#include <mutex>

#include <boost/algorithm/string.hpp>

using namespace aaf;

struct RxEndpointInfo
{
public:
    ami::Message::IDType ep_id_;			 //endpoint ID
    std::string  ep_name_;        		     //endpoint名称
    std::vector<int32_t> partitions_;	     //endpoint分区
    ami::Message::SqnType msg_cnt_ = 0;		 //消息数量统计

    RxEndpointInfo() = default;
	RxEndpointInfo(ami::Message::IDType ep_id,
                  const std::string& ep_name,
                  std::vector<int32_t> partitions)
    			: ep_id_(ep_id),
            ep_name_(ep_name),
            partitions_(partitions)
    {}
};

struct TxEndpointInfo : public RxEndpointInfo
{
public:
    aaf::EndpointHandler* ep_hdl_ = nullptr; //tx_endpoint消息处理句柄
    
    TxEndpointInfo() = default;
    TxEndpointInfo(ami::Message::IDType ep_id,
                   const std::string& ep_name,
                   std::vector<int32_t> partitions,
                   aaf::EndpointHandler* ep_hdl)
    			: RxEndpointInfo(ep_id, ep_name, partitions),
            ep_hdl_(ep_hdl)
    {}
};

class AmiForward : public aaf::GenericAmiApplication
{
public:
    typedef std::unordered_map<std::string, RxEndpointInfo> RxInfoNameMap;
    typedef std::unordered_map<std::string, TxEndpointInfo> TxInfoNameMap;
    
    typedef std::unordered_map<std::string, std::vector<int32_t>> TxInfo;
    typedef std::unordered_map<std::string, TxInfoNameMap> ForwardList;
    
    static const int32_t kLogAmiForward = 400000;
    ADK_LOG_DECLARE_AC(kLogAmiForward);

public:
    AmiForward() {}
    ~AmiForward() {}
    
    virtual void SetAmiAppOption()
    {
        AddOptionWithArgument<std::string>("forward-list",
                              "the forward endpoint map, for example a:1-b:1,2+c:1/a:2-c:1",
                              "");
    }

    void OnAmiAppOption(const std::string& option_name)
    {
        if (option_name == "forward-list")
        {
            std::string forward_list = GetOptionArgument<std::string>(option_name);
            if (forward_list.empty())
            {
                return;
            }
            ADK_LOG_DEBUG_AC_TF(">>>>>", "<{1}>", forward_list);

            std::vector<std::string> ep_info_vec;
            boost::split(ep_info_vec, forward_list, boost::is_any_of("/"), boost::token_compress_on);

            for(const auto& ep_info : ep_info_vec) //a:1-b:1,2;c:1
            {
                ADK_LOG_DEBUG_AC_TF(">>>>>", "<{1}>", ep_info);

                std::vector<std::string> info;
                boost::split(info, ep_info, boost::is_any_of("-"), boost::token_compress_on);
                std::string rx_info = info[0];  //a:1
                std::string all_tx_info = info[1]; //b:1,2;c:1
                ADK_LOG_DEBUG_AC_TF(">>>>>", "<{1}> <{2}>", rx_info, all_tx_info);

                std::vector<std::string> tx_info_vec;
                boost::split(tx_info_vec, all_tx_info, boost::is_any_of("+"), boost::token_compress_on);
                TxInfoNameMap tx_ep_info;
                for (const auto& tx_info : tx_info_vec) //b:1,2
                {
                    ADK_LOG_DEBUG_AC_TF(">>>>>", "<{1}> ", tx_info);
                    std::vector<std::string> v;
                    boost::split(v, tx_info, boost::is_any_of(":"), boost::token_compress_on);
                    std::string tx_name = v[0];
                    ADK_LOG_DEBUG_AC_TF(">>>>>", "<{1}> ", tx_name);
                    std::vector<int32_t> partitions;
                    std::vector<std::string> tx_partitions;
                    boost::split(tx_partitions, v[1], boost::is_any_of(","), boost::token_compress_on);
                    for (const auto& partition : tx_partitions)
                    {
                        ADK_LOG_DEBUG_AC_TF(">>>>>", "<{1}> ", partition);
                        int32_t p = std::stoi(partition);
                        partitions.push_back(p);
                    }

                    TxEndpointInfo value(-1, tx_name, partitions, nullptr);
                    tx_ep_info.emplace(std::make_pair(tx_name, value));
                    partitions.clear();
                }

                forward_list_.emplace(std::make_pair(rx_info, tx_ep_info));
                tx_ep_info.clear();
            }
        }
    }

    virtual void OnConfigureFramework(ami::Property& fw_props)
    {
		fw_props.SetValue(aaf::config::kEnableHighAvailableContext, true);
		fw_props.SetValue(aaf::config::kEnableAppNameCheck, false);
    }

    virtual int32_t OnAmiInitBegin()
    {
        ADK_LOG_INFO_AC_TF("application forward endpoint map: ", "");
        ADK_LOG_INFO_AC_TF("++++++++++++++++++++++++++++++++++++++++","");
        if (forward_list_.empty())
        {
            ADK_LOG_INFO_AC_TF("every rx endpoint will send all tx endpoint", "");
        }
        else
        {
            for (auto& info : forward_list_)
            {
                std::string source = info.first;
                std::string dest = "";
                for (auto& tx_info : info.second)
                {
                    dest += tx_info.first;
                    dest += ",";
                }
                ADK_LOG_INFO_AC_TF("+++", "<{1}>  --->  <{2}>", source, dest);
            }            
        }
        ADK_LOG_INFO_AC_TF("++++++++++++++++++++++++++++++++++++++++","");
        return aaf::kSuccess;
    }

    virtual int32_t OnTxEndpointCreation(aaf::EndpointHandler* ep_hdl, const std::string& ep_name)
    {
        //获取Tx_endpoint的id
    	auto ep_id = 0;
        GetContext()->PropertyAt(ami::config::context::kTxEndpoint, ep_name)
                                (ami::config::endpoint::kId).GetValue(ep_id);
        //获取tx_endpoint包含的分区
        std::vector<int32_t> partitions_no; 
        GetTxEndpointPartitions(ep_name, partitions_no);

        tx_info_name_map_.emplace(
            std::make_pair(ep_name, TxEndpointInfo(ep_id, ep_name, partitions_no, ep_hdl)));

	    return aaf::kSuccess;
    }
    
    virtual int32_t OnRxEndpointCreationBegin()
    {
        //校验需要转发的tx是否存在
        for (auto& iter : forward_list_)
        {
            for (auto& tx_fwd_info : iter.second)
            {
                std::string tx_name = tx_fwd_info.first;
                std::vector<int32_t> partitions = tx_fwd_info.second.partitions_;
                ADK_LOG_DEBUG_AC_TF(">>>>>>>>>>>>>", "tx_name <{1}>", tx_name);

                TxInfoNameMap::iterator tx_info;
                for (tx_info = tx_info_name_map_.begin(); tx_info != tx_info_name_map_.end(); tx_info++)  //已创建的tx
                {
                    ADK_LOG_DEBUG_AC_TF(">>>>>>>>>>>>>", "tx_name <{1}>, <{2}>", tx_name, tx_info->second.ep_name_);

                    if (tx_name == tx_info->second.ep_name_)
                    {
                        tx_fwd_info.second.ep_id_ = tx_info->second.ep_id_;
                        tx_fwd_info.second.ep_hdl_ = tx_info->second.ep_hdl_;

                        for (uint32_t i = 0; i < partitions.size(); ++i)
                        {
                            ADK_LOG_DEBUG_AC_TF(">>>>>>>>>>>>>", "partition <{1}>", partitions[i]);
                            uint32_t j = 0;
                            for (j = 0; j < tx_info->second.partitions_.size(); ++j)
                            {
                                if (partitions[i] == tx_info->second.partitions_[j])
                                {
                                    ADK_LOG_DEBUG_AC_TF(">>>>>>>>>>>>>", "partition <{1}> <{2}>", partitions[i], tx_info->second.partitions_[j]);
                                    break;
                                }
                            }
                            if (j == tx_info->second.partitions_.size())
                            {
                                ADK_LOG_ERROR_AC_TF("param error", 
                               "cant find endpoint_name <{1}> partition <{2}>",
                                tx_name, partitions[i]);
                                return aaf::kFailure;
                            }
                        } 

                        break;  
                    }
                }
                if (tx_info == tx_info_name_map_.end())
                {
                    ADK_LOG_ERROR_AC_TF("param error", 
                        "cant find endpoint_name <{1}>", tx_name);
                    return aaf::kFailure;
                } 
            }
        }

        //获取Rx列表
    	auto& ep_set = this->GetRxEndpointSet();
		for (const std::string ep_name : ep_set)
		{
			//获取rx_endpoint的id
			int32_t ep_id = 0;
			GetContext()->PropertyAt(ami::config::context::kRxEndpoint, ep_name)
	            	                (ami::config::endpoint::kId).GetValue(ep_id);
	    	//获取rx_endpoint包含的分区
			std::vector<int32_t> partitions_no;	
			GetRxEndpointPartitions(ep_name, partitions_no);
			rx_info_name_map_.emplace(
		        std::make_pair(ep_name, RxEndpointInfo(ep_id, ep_name, partitions_no)));
		}

        //校验需要转发的Rx是否存在
        for (auto& iter : forward_list_)
        {
            std::string ep_info = iter.first;
            std::vector<std::string> info;
            boost::split(info, ep_info, boost::is_any_of(":"), boost::token_compress_on);
            std::string rx_name = info[0];  //a
            int32_t partition = std::stoi(info[1]); //2
            ADK_LOG_DEBUG_AC_TF(">>>>>>>>>>>>>", "rx_name <{1}>", rx_name);

            RxInfoNameMap::iterator rx_info;
            for (rx_info = rx_info_name_map_.begin(); rx_info != rx_info_name_map_.end(); ++rx_info)  //已创建的rx
            {
                ADK_LOG_DEBUG_AC_TF(">>>>>>>>>>>>>", "rx_name <{1}>, <{2}>", rx_name, rx_info->second.ep_name_);
                if (rx_name == rx_info->second.ep_name_)
                {    
                    ADK_LOG_DEBUG_AC_TF(">>>>>>>>>>>>>", "partition <{1}> ", partition);
                    uint32_t j = 0;
                    for (j = 0; j < rx_info->second.partitions_.size(); ++j)
                    {
                        ADK_LOG_DEBUG_AC_TF(">>>>>>>>>>>>>", "partition <{1}> <{2}>", partition, rx_info->second.partitions_[j]);
                        if (partition == rx_info->second.partitions_[j])
                        {
                            break;
                        }
                    }
                    if (j == rx_info->second.partitions_.size())
                    {
                        ADK_LOG_ERROR_AC_TF("param error", 
                       "cant find endpoint_name <{1}> partition <{2}>",
                        rx_name, partition);
                        return aaf::kFailure;
                    } 

                    break; 
                }

                if (rx_info == rx_info_name_map_.end())
                {
                    ADK_LOG_ERROR_AC_TF("param error", 
                        "cant find rx endpoint name <{1}>", rx_name);
                    return aaf::kFailure;
                } 
            }
        }

		return aaf::kSuccess;
    }


    virtual void OnMessage(ami::Message* msg)
	{
        std::lock_guard<std::mutex> lck(mtx_);

        ++rx_info_name_map_[msg->get_endpoint_name()].msg_cnt_;

        if (forward_list_.empty())
        {
            //转发到所有tx
            for (auto& tx_info : tx_info_name_map_)
            {
                EndpointHandler* ep_hdl = tx_info.second.ep_hdl_;
                assert(ep_hdl != nullptr);
                std::vector<int32_t> partitions = tx_info.second.partitions_;
                for (const auto& partition : partitions)
                {
                    ep_hdl->SendMsg(msg->const_data(), msg->size(), partition);
                    ++tx_info.second.msg_cnt_;
                }
            }
        }
        else
        {
            std::string key = msg->get_endpoint_name() + ":" + std::to_string(msg->get_partition_no());
            for (const auto& iter : forward_list_)
            {
                if (iter.first == key)
                {
                    //转发到指定tx
                    for (auto& tx_fwd_info : iter.second)
                    {
                        EndpointHandler* ep_hdl = tx_fwd_info.second.ep_hdl_;
                        assert(ep_hdl != nullptr);
                        std::vector<int32_t> partitions = tx_fwd_info.second.partitions_;
                        for (const auto& partition : partitions)
                        {
                            ep_hdl->SendMsg(msg->const_data(), msg->size(), partition);
                            ++tx_info_name_map_[tx_fwd_info.first].msg_cnt_;
                        }
                    }
                }
            } 
        }
        
	}

	virtual int32_t OnRun()
	{
        std::string stats;
        while (is_running())
        {
            sleep(1);

            std::lock_guard<std::mutex> lck(mtx_);
            stats = boost::format("\n=====================================================\nRxEndpoint Info:").str();
            for (auto& rx_info : rx_info_name_map_)
            {
                stats.append((boost::format("\n\tEndpoint: <%1%>, ID: <%2%>, rx messages: %3%")
                                     % rx_info.second.ep_name_ % rx_info.second.ep_id_ % rx_info.second.msg_cnt_).str());
            }
            stats.append("\nTxEndpoint Info:");
            for (auto& tx_info : tx_info_name_map_)
            {
                stats.append((boost::format("\n\tEndpoint: <%1%>, ID: <%2%>, tx messages: %3%")
                                     % tx_info.second.ep_name_ % tx_info.second.ep_id_ % tx_info.second.msg_cnt_).str());
            }
            stats.append("\n=====================================================\n");
            ADK_LOG_INFO_AC_TF("stats", stats);
        }
		return aaf::kPassed;
	}

    
private:
    RxInfoNameMap       rx_info_name_map_;
    TxInfoNameMap       tx_info_name_map_;
    ForwardList         forward_list_;
    std::mutex          mtx_;
}g_ami_forward;

ADK_LOG_DEFINE(AmiForward);
