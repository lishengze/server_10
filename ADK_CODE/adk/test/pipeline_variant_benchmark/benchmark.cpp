//
// Created by lzn on 9/17/19.
//

#include <adk/lock_free_msg_queue.h>
#include <adk/pipeline_variant.h>
#include <stdio.h>
#include <boost/thread.hpp>
#include <unistd.h>
#include <adk/pipeline.h>

#define NS_PER_SECOND (1000000000UL)

uint64_t timediff(struct timespec end, struct timespec start)
{
    return end.tv_sec * NS_PER_SECOND + end.tv_nsec
           - (start.tv_sec * NS_PER_SECOND + start.tv_nsec);
}

using namespace adk;

timespec ref_ts_start;
timespec ref_ts_end;
timespec dry_ts_start;
timespec dry_ts_end;
timespec test_ts_end;
timespec test_ts_mid;
timespec test_ts_start;
timespec old_ts_start;
timespec old_ts_end;

class AppStageWorker : public StageWorker<ADK_INPUT(uint64_t), ADK_OUTPUT(2, uint64_t)>
{
public:
    AppStageWorker(const std::string& name, uint64_t total_msgs)
            :   StageWorker<ADK_INPUT(uint64_t), ADK_OUTPUT(2, uint64_t)>(name), total_messages(total_msgs)
    {}

    ~AppStageWorker()
    {}

    uint64_t total_messages;

    uint64_t counter = 0;

    virtual void OnMessage(uint64_t& message, short dim, short idx)
    {
        counter++;
        if(counter == total_messages)
        {
            clock_gettime(CLOCK_REALTIME, &old_ts_end);
        }
    }

    int64_t offset_;
};

class EntranceWorker : public StageWorker<ADK_INPUT(uint64_t), ADK_OUTPUT(uint64_t)>
{
public:
    EntranceWorker(const std::string& name)
            :   StageWorker<ADK_INPUT(uint64_t), ADK_OUTPUT(uint64_t)>(name)
    {}

    ~EntranceWorker()
    {}


    virtual void OnMessage(uint64_t& message, short dim, short idx)
    {
    }

    int64_t offset_;
};

struct Payload
{
    uint64_t placeholder;
    uint64_t data;
};

//SPSCQueue<Payload>
void Receiver(void* queue, uint64_t total_messages)
{
    auto mq = (SPSCQueue<Payload>*) queue;
    uint64_t counter = 0;
    while (counter != total_messages)
    {
        Payload value;
        if (mq->Pop(value) == ErrorCode::kSuccess)
        {
            ++counter;

        }
    }

    clock_gettime(CLOCK_REALTIME, &ref_ts_end);
}

int main()
{
    //Ref
    auto queue = SPSCQueue<Payload>::Create("test_spsc", 8192);


    constexpr uint64_t total_messages = 70000;


    boost::thread p = boost::thread(Receiver, queue, total_messages);

    clock_gettime(CLOCK_REALTIME, &ref_ts_start);
    ADK_BARRIER();
    {
        uint64_t counter = 0;
        volatile Payload data {0, 0};
        while (counter != total_messages)
        {
            ++counter;
            data.data = counter;
            while (queue->Push((const Payload &) data) != ErrorCode::kSuccess)
            {
                ADK_PAUSE();
            }
        }
    }
    p.join();

    clock_gettime(CLOCK_REALTIME, &dry_ts_start);
    ADK_BARRIER();
    {
        int counter = 0;
        for(int i = 0; i < total_messages + 2; i++)
        {
            counter++;
            if(counter == total_messages)
                clock_gettime(CLOCK_REALTIME, &dry_ts_end);
        }
    }


    //Pipeline
    uint64_t counter = 0;
    auto ent = Entrance<uint64_t>().Build();
    StageConfig cfg;
    uint64_t idle_Count = 0;
    cfg.queue_size = 1000000;
    cfg.is_same_context = false;
    auto stage = DefaultBuilder<uint64_t, uint64_t>(cfg).OnMessage([&](const uint64_t& val){
        counter++;
        if(counter == total_messages)
            clock_gettime(CLOCK_REALTIME, &test_ts_end);
    }).OnIdle([&](){idle_Count++;}).Build();
    *ent | stage;

    ent->Start();

    clock_gettime(CLOCK_REALTIME, &test_ts_start);
    ADK_BARRIER();
    MsgStatus stat;
    for(uint64_t i = 0; i < total_messages; i++)
    {
        ent->Forward(i);
    }
    clock_gettime(CLOCK_REALTIME, &test_ts_mid);
    ent->Stop();
    printf("%d\n", stage->stat_data.drop_msgs);

    //Old Pipeline
    {
        uint64_t counter = 0;
        EntranceWorker ent_worker("ent_worker");
        AppStageWorker stage_worker_1("stage_worker_1", total_messages);
        stage_worker_1.SetBackoffPolicy<policy::Pause>();
        ent_worker.SetBackoffPolicy<policy::Pause>();
        stage_worker_1.offset_ = 0;

        Pipeline pipeline;

        //pipeline.Connect<true, uint64_t>((NextStageWorker<1, uint64_t>&)ent_worker, (PrevStageWorkerBasic<1, uint64_t>&)stage_worker_1);
        auto entrance = pipeline.CreateEntrance<false>(stage_worker_1);

        pipeline.Start();
        clock_gettime(CLOCK_REALTIME, &old_ts_start);

        for(uint64_t i = 0; i < total_messages; i++)
        {
            entrance->Forward(i);
        }

        pipeline.Stop();
    }


    printf("Total Message: %lu\n", total_messages);
    printf("Ref Time spent : %lu \n", timediff(ref_ts_end, ref_ts_start));
    printf("Old Time spent : %lu \n", timediff(old_ts_end, old_ts_start));

    printf("Test Time spent: %lu \n", timediff(test_ts_end, test_ts_start));
    printf("Test Send spent: %lu \n", timediff(test_ts_mid, test_ts_start));
    printf("Dry Time spent : %lu \n", timediff(dry_ts_end, dry_ts_start));
    printf("Idle Count     : %lu \n", idle_Count);
    return 0;
}