//
// Created by lzn on 11/27/19.
//

#include "adk/pipeline_variant.h"

namespace adk_impl
{
    namespace pipeline_utils
    {
        void DoChangeToRealtime(int32_t priority, int32_t policy)
        {
            if (priority == 0)
                return;

            struct sched_param param;
            memset(&param, 0x00, sizeof(param));

            param.sched_priority = priority;
            ::sched_setscheduler(0, policy, &param);
        }

        void DoRestoreToOther()
        {
            struct sched_param param;
            memset(&param, 0x00, sizeof(param));

            param.sched_priority = 0;
            ::sched_setscheduler(0, SCHED_OTHER, &param);
        }
    }

    static std::atomic<int>& getCounter()
    {
        static std::atomic<int> counter;
        return counter;
    }

    int IndexGenerator::GetNewIndex()
    {
        auto& counter = getCounter();
        auto ret = counter++;
        return ret;
    }

    bool topologySort(StageType* base, std::vector<StageType*>& topologyOrder)
    {
        std::map<StageType*, uint32_t> stages;
        std::queue<StageType*> targets;
        int elements = 0;
        targets.push(base);
        for(;;)
        {
            if(targets.empty())
                break;
            auto current = targets.front();
            targets.pop();
            for (int i = 0; i < current->GetOutputsCount(); i++)
            {
                auto vec = current->GetNConnectors()[i];
                for (auto &&conn : vec)
                {
                    auto next = conn->GetTarget();
                    auto result = stages.find(next);
                    if(result == stages.end())
                    {
                        elements++;
                        //Note: following code do not  handle insert failed.
                        result = stages.insert({next, next->GetPrevConnectorsCount()}).first;
                    }
                    result->second -= 1;
                    if(result->second <= 0)
                    {
                        elements--;
                        targets.push(result->first);
                        topologyOrder.push_back(result->first);
                    }
                }
            }
        }
        if(elements != 0)
        {
            return false;
        }
        return true;
    }
}