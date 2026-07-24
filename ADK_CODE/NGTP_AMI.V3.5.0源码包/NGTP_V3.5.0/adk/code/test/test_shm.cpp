#include <string>
#include <iostream>
#include <adk/shm.h>
#include <adk/arch/generic.h>


struct RxEpSyncInfo
{
    int32_t  rxep_id __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t rxep_sqn;
    uint64_t total_order_sqn;
};

struct LaunchInfoLayout
{   
    int32_t dr_agent_failover_status = 0; 
    int32_t dr_tx_alignment_saved = 0;
    char    dr_tx_alignment[1024u * 1024u];

    int32_t rxep_size;
    int32_t padding;
    char rxep_sync_sqn[];

    template<typename GetSyncInfo>
    void for_each(const GetSyncInfo& get_sync_info)
    {
        char* sync_info = rxep_sync_sqn;
        for (int32_t rxep_index = 0; rxep_index < rxep_size; ++rxep_index)
        {
            get_sync_info((RxEpSyncInfo*)sync_info);
            sync_info += sizeof(struct RxEpSyncInfo);
        }
    }

    void reset(int32_t ep_size)
    {
        dr_agent_failover_status = 0;
        dr_tx_alignment_saved = 0;

        rxep_size = ep_size;
        char* const rxep_sync = rxep_sync_sqn;
        memset(rxep_sync, 0, ep_size * sizeof(struct RxEpSyncInfo));
    }

    static size_t CalcLayoutSize(int32_t ep_size)
    {
        return sizeof(struct LaunchInfoLayout) + sizeof(struct RxEpSyncInfo) * ep_size;
    }
};


int main(int argc, char* argv[])
{
    const std::string  shm_file_name = "test_launch_info";
    LaunchInfoLayout* launch_info = nullptr;

    // const uint32_t shm_size = ADK_ROUND_UP(LaunchInfoLayout::CalcLayoutSize(10), ADK_PAGE_SIZE);

    adk::ShmFactory::Destroy(shm_file_name);

    auto cur_shm_size = adk::ShmFactory::Size(shm_file_name);  // get the current shm size
    if (cur_shm_size != -1ul)   // shm object exist
    {
        if (adk::ShmFactory::Destroy(shm_file_name) != adk::ErrorCode::kSuccess)
        {
            std::cerr << "destroy the stale share memory <"
                      << shm_file_name << "> failed" << std::endl;
            return -1;
        }
    }

    // create 4G + 4KB  and will write zero failed
    launch_info = (LaunchInfoLayout*)adk::ShmFactory::Create("test_launch_info", 4 * 1024ul * 1024ul * 1024ul);
    if (nullptr == launch_info)
    {
        std::cerr << "create launch info share memory failed" << std::endl;
        launch_info = (LaunchInfoLayout*)adk::ShmFactory::Attach(shm_file_name);
        if (nullptr == launch_info)
        {
            std::cerr << "attach launch info share memory failed" << std::endl;
            return -1;
        }
        std::cout << "attach launch info share memory success" << std::endl;
        // return -1;
    }

    uint32_t id = 1;
    launch_info->for_each([&](RxEpSyncInfo* sync_info) {
        sync_info->rxep_id         = id;
        sync_info->rxep_sqn        = id * 1000;
        sync_info->total_order_sqn = id * 55;
    });

    // launch_info = (LaunchInfoLayout*)adk::ShmFactory::Attach(shm_file_name);
    // if (nullptr == launch_info)
    // {
    //     std::cerr << "attach launch info share memory failed" << std::endl;
    //     return -1;
    // }

    return 0;
}