#ifndef ADK_IMPL_IO_ENGINE_LOCAL_PORTS_H_
#define ADK_IMPL_IO_ENGINE_LOCAL_PORTS_H_

#include <map>
#include <mutex>
#include <deque>

#include <assert.h>

#include <adk/arch/generic.h>
#include <adk/io_engine/property.h>
#include <adk/io_engine/config_key.h>

namespace adk_impl
{

namespace io_engine
{

class LocalPorts
{
public:
    LocalPorts(uint16_t range_low, uint16_t range_high);

    uint16_t AllocatePort()
    {
        std::lock_guard<std::mutex> _(public_lock_);
        auto ports_size = static_cast<int64_t>(local_ports_.size());
        if (ADK_UNLIKELY(0 == ports_size))
        {
            ports_size = static_cast<int64_t>(bind_fail_ports_.size());
            if (ADK_UNLIKELY(fail_nr_ >= ports_size))
            {
                return 0;
            }

            bind_fail_ports_.swap(local_ports_);
        }

        auto rand_index = (std::size_t)random() % ports_size;
        const auto iter = local_ports_.begin() + rand_index;
        const auto port_value = *iter;
        local_ports_.erase(iter);
        return port_value;
    }

    void ReservePort(uint16_t port_value, int32_t endpoint_id)
    {
        bool is_local_port = false;
        std::lock_guard<std::mutex> _(public_lock_);
        for (auto iter = local_ports_.begin(); iter != local_ports_.end(); ++iter)
        {
            if (port_value == *iter)
            {
                local_ports_.erase(iter);
                is_local_port = true;
                break;
            }
        }

        auto& port_info = in_use_ports_[endpoint_id];
        port_info.first = port_value;
        port_info.second = is_local_port;
    }

    void BindSuccess(uint16_t port_value, int32_t endpoint_id)
    {
        if (port_value > 0)
        {
            std::lock_guard<std::mutex> _(public_lock_);
            auto& port_info = in_use_ports_[endpoint_id];
            port_info.first = port_value;
            port_info.second = true;

            fail_nr_ = 0;
        }
    }

    void BindFail(uint16_t port_value)
    {
        assert(0 != port_value);
        std::lock_guard<std::mutex> _(public_lock_);
        bind_fail_ports_.push_back(port_value);

        ++fail_nr_;
    }

    void FreePort(int32_t endpoint_id)
    {
        std::lock_guard<std::mutex> _(public_lock_);
        const auto iter = in_use_ports_.find(endpoint_id);
        if (in_use_ports_.end() != iter)
        {
            if (iter->second.second)
            {
                local_ports_.push_back(iter->second.first);
                fail_nr_ = 0;
            }

            in_use_ports_.erase(iter);
        }
    }

#ifndef __ADK_UNIT_TEST__
private:
#endif
    int64_t              fail_nr_;
    std::mutex           public_lock_;
    std::deque<uint16_t> local_ports_;
    std::deque<uint16_t> bind_fail_ports_;
    std::map<int32_t, std::pair<uint16_t, bool>> in_use_ports_;
};

}

}

#endif