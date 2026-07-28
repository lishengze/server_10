//
// Created by lzn on 9/25/19.
//

#include <adk/lock_free_msg_queue.h>
#include <adk/pipeline_variant.h>
#include <stdio.h>
#include <boost/thread.hpp>
#include <unistd.h>
#include <adk/pipeline.h>

using namespace adk;

/**
 * Latency Test Result[2019-09-25]
 * [lzn@localhost pipeline_variant_benchmark_2]$ numactl --cpubind=0 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/pipeline_variant_benchmark_2
Total Msg: 140000
Ref Latency: Avg:2508 Mid:486 95%:8040 99%:51582
PPL Latency: Avg:1555 Mid:588 95%:5478 99%:13818
[lzn@localhost pipeline_variant_benchmark_2]$ numactl --cpubind=0 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/pipeline_variant_benchmark_2
Total Msg: 140000
Ref Latency: Avg:4238 Mid:510 95%:9206 99%:145442
PPL Latency: Avg:2820 Mid:740 95%:10294 99%:38796
[lzn@localhost pipeline_variant_benchmark_2]$ numactl --cpubind=0 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/pipeline_variant_benchmark_2
Total Msg: 140000
Ref Latency: Avg:2528 Mid:490 95%:7608 99%:51530
PPL Latency: Avg:6096 Mid:798 95%:38144 99%:71760
[lzn@localhost pipeline_variant_benchmark_2]$ numactl --cpubind=0 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/pipeline_variant_benchmark_2
Total Msg: 140000
Ref Latency: Avg:2501 Mid:498 95%:8968 99%:45256
PPL Latency: Avg:2932 Mid:782 95%:11490 99%:38086
[lzn@localhost pipeline_variant_benchmark_2]$ numactl --cpubind=0 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/pipeline_variant_benchmark_2
Total Msg: 140000
Ref Latency: Avg:2647 Mid:528 95%:7250 99%:56782
PPL Latency: Avg:2973 Mid:768 95%:10536 99%:36234
[lzn@localhost pipeline_variant_benchmark_2]$ numactl --cpubind=0 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/pipeline_variant_benchmark_2
Total Msg: 140000
Ref Latency: Avg:2689 Mid:484 95%:10042 99%:52036
PPL Latency: Avg:2820 Mid:606 95%:10000 99%:46428

 Latency Test Result[2019-10-22]
[lzn@localhost pipeline_variant_benchmark_2]$ numactl --cpubind=0 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/pipeline_variant_benchmark_2
Total Msg: 2000
Ref Latency: Avg:6553 Mid:1806 95%:57774 99%:58028
PPL Latency: Avg:7453 Mid:1146 95%:106132 99%:109726
OPL Latency: Avg:3053 Mid:942 95%:12340 99%:17340
[lzn@localhost pipeline_variant_benchmark_2]$ numactl --cpubind=0 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/pipeline_variant_benchmark_2
Total Msg: 2000
Ref Latency: Avg:5895 Mid:1086 95%:65670 99%:66598
PPL Latency: Avg:7390 Mid:1140 95%:98488 99%:102008
OPL Latency: Avg:3383 Mid:1374 95%:12658 99%:13468
[lzn@localhost pipeline_variant_benchmark_2]$ numactl --cpubind=0 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/pipeline_variant_benchmark_2
Total Msg: 2000
Ref Latency: Avg:7123 Mid:1610 95%:75960 99%:76228
PPL Latency: Avg:7422 Mid:1182 95%:101464 99%:104632
OPL Latency: Avg:3912 Mid:1258 95%:14412 99%:14726
[lzn@localhost pipeline_variant_benchmark_2]$ numactl --cpubind=0 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/pipeline_variant_benchmark_2
Total Msg: 2000
Ref Latency: Avg:9209 Mid:1530 95%:92652 99%:92906
PPL Latency: Avg:9521 Mid:1244 95%:136366 99%:140850
OPL Latency: Avg:3529 Mid:1218 95%:19666 99%:21976
[lzn@localhost pipeline_variant_benchmark_2]$ numactl --cpubind=0 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/pipeline_variant_benchmark_2
Total Msg: 2000
Ref Latency: Avg:6097 Mid:1374 95%:65918 99%:66152
PPL Latency: Avg:7570 Mid:1184 95%:98738 99%:103108
OPL Latency: Avg:3576 Mid:1282 95%:13310 99%:15818
 */

#define NS_PER_SECOND (1000000000UL)

uint64_t timediff(struct timespec end, struct timespec start)
{
    return end.tv_sec * NS_PER_SECOND + end.tv_nsec
           - (start.tv_sec * NS_PER_SECOND + start.tv_nsec);
}

uint64_t* latency;
uint64_t* latency_ref;
uint64_t* latency_old;

bool is_running = true;


struct appPayload
{
    uint64_t data;
    uint64_t index;
};

void Receiver(SPSCQueue<appPayload>* mq, uint64_t total_messages)
{
    pipeline_utils::DoChangeToRealtime(0, SCHED_FIFO);
    //uint64_t counter = 0;
    appPayload value;
    while (ACCESS_ONCE(is_running))
    {
        if (mq->Pop(value) == ErrorCode::kSuccess)
        {
            uint64_t now;
            now = adk::GetTSC();
            latency_ref[value.index] = now - value.data;
            //++counter;
        }
        ADK_PAUSE();
    }
}

int CompareLatency(const void *a, const void *b)
{
    const uint64_t a_v = *reinterpret_cast<const uint64_t*>(a);
    const uint64_t b_v = *reinterpret_cast<const uint64_t*>(b);
    return a_v - b_v;
}



class AppStageWorker : public StageWorker<ADK_INPUT(appPayload), ADK_OUTPUT(2, appPayload)>
{
public:
    AppStageWorker(const std::string& name, uint64_t total_msgs)
            :   StageWorker<ADK_INPUT(appPayload), ADK_OUTPUT(2, appPayload)>(name), total_messages(total_msgs)
    {}

    ~AppStageWorker()
    {}

    uint64_t total_messages;

    uint64_t counter = 0;

    virtual void OnMessage(appPayload& message, short dim, short idx)
    {
        uint64_t now;
        now = adk::GetTSC();
        latency_old[counter] = now - message.data;
        counter++;
    }

    int64_t offset_;
};

class EntranceWorker : public StageWorker<ADK_INPUT(appPayload), ADK_OUTPUT(appPayload)>
{
public:
    EntranceWorker(const std::string& name)
            :   StageWorker<ADK_INPUT(appPayload), ADK_OUTPUT(appPayload)>(name)
    {}

    ~EntranceWorker()
    {}


    virtual void OnMessage(appPayload& message, short dim, short idx)
    {
    }

    int64_t offset_;
};

int main(int argc, char** argv)
{
    constexpr uint64_t total_messages = 32000;
    latency = new uint64_t[total_messages + 2];
    latency_ref = (uint64_t*)memalign(ADK_CACHE_LINE_SIZE, sizeof(uint64_t) * (total_messages + 2));
    latency_old = new uint64_t[total_messages + 2];

    memset(latency, 0, sizeof(uint64_t) * (total_messages + 2));
    memset(latency_ref, 0, sizeof(uint64_t) * (total_messages + 2));
    memset(latency_old, 0, sizeof(uint64_t) * (total_messages + 2));

    if(argc < 2)
        return 0;

    if(argv[1][0] == 'r' || argv[1][0] == 'a'){
        //Ref
        auto queue = SPSCQueue<appPayload>::Create("test_spsc", 8192);

        pipeline_utils::DoChangeToRealtime(50, SCHED_FIFO);
        boost::thread p = boost::thread(Receiver, queue, total_messages);
        pipeline_utils::DoRestoreToOther();
        usleep(70000);
        ADK_BARRIER();
        {
            uint64_t counter = 0;
            while (counter != total_messages)
            {
                ++counter;
                appPayload begin;
                begin.data = adk::GetTSC();
                begin.index = counter;
                ADK_BARRIER();
                while (queue->Push(begin) != ErrorCode::kSuccess)
                {
                    ADK_PAUSE();
                }

                if((counter % 100) == 0)
                    usleep(70000);
            }
        }
        sleep(2);
        is_running = false;
        p.join();
    }

    uint64_t counter = 0;
    auto ent = Entrance<appPayload>().Build();
    StageConfig cfg;
    uint64_t idle_Count = 0;
    cfg.queue_size = 8192;
    cfg.is_same_context = false;
    cfg.polling_nano = 99999999999;
    cfg.backoff_limit = 0;
    cfg.cpuAffinity= "0";
    auto stage = DefaultBuilder<appPayload, appPayload>(cfg).OnMessage([&](const appPayload& val){
        uint64_t now;
        now = adk::GetTSC();
        latency[counter] = now - val.data;
        //printf("l %lu %lu\n", now, val);
        counter++;
    }).Build();
    *ent | stage;

    ent->Start();
    usleep(70000);
    uint64_t total_time = 0;
    if(argv[1][0] == 'n' || argv[1][0] == 'a'){
        uint64_t counter = 0;
        uint64_t curr_time;
        curr_time = adk::GetTSC();
        while (counter != total_messages)
        {
            ++counter;
            appPayload send_now;
            send_now.data = adk::GetTSC();
            ADK_BARRIER();
            ent->Forward(send_now);
            ADK_BARRIER();
            uint64_t now_time = adk::GetTSC();
            total_time += now_time - send_now.data;

            if((counter % 25) == 0)
            {
                usleep(70000);
            }
        }
    }

    ent->Stop();

    uint64_t total_time_old = 0;
    //Old Pipeline
    if(argv[1][0] == 'o' || argv[1][0] == 'a'){
        EntranceWorker ent_worker("ent_worker");
        AppStageWorker stage_worker_1("stage_worker_1", total_messages);
        stage_worker_1.SetBackoffPolicy<policy::Pause>();
        ent_worker.SetBackoffPolicy<policy::Pause>();
        stage_worker_1.offset_ = 0;

        Pipeline pipeline;

        //pipeline.Connect<true, uint64_t>((NextStageWorker<1, uint64_t>&)ent_worker, (PrevStageWorkerBasic<1, uint64_t>&)stage_worker_1);
        auto entrance = pipeline.CreateEntrance<false>(stage_worker_1);

        pipeline.Start();
        usleep(70000);

        uint64_t counter = 0;
        uint64_t curr_time;
        curr_time = adk::GetTSC();
        while (counter != total_messages)
        {
            ++counter;
            appPayload send_now;
            send_now.data = adk::GetTSC();
            ADK_BARRIER();
            entrance->Forward(send_now);
            ADK_BARRIER();
            uint64_t now_time = adk::GetTSC();
            total_time_old += now_time - send_now.data;

            if((counter % 25) == 0)
            {
                usleep(70000);
                curr_time = adk::GetTSC();
            }
        }

        pipeline.Stop();
    }

    printf("Total Msg: %lu\n", total_messages);
    printf("Avg Send Time: Old: %lu New:%lu\n", total_time_old / (total_messages / 100), total_time / (total_messages / 100));

    long long unsigned int cnt = 0;
    //Result Analy
    qsort(latency_ref, total_messages, sizeof(uint64_t), CompareLatency);
    for(uint64_t i = 0; i < total_messages; i++)
    {
        cnt += latency_ref[i];
    }
    printf("Ref Latency: Avg:%llu Mid:%lu 95\%:%lu 99\%:%lu\n",
            cnt / total_messages,
            latency_ref[total_messages / 2],
            latency_ref[total_messages - total_messages / 20],
            latency_ref[total_messages - total_messages / 100]);

    cnt = 0;

    qsort(latency, total_messages, sizeof(uint64_t), CompareLatency);
    for(uint64_t i = 0; i < total_messages; i++)
    {
        cnt += latency[i];
    }
    printf("PPL Latency: Avg:%llu Mid:%lu 95\%:%lu 99\%:%lu\n",
           cnt / total_messages,
           latency[total_messages / 2],
           latency[total_messages - total_messages / 20],
           latency[total_messages - total_messages / 100]);

    cnt = 0;
    qsort(latency_old, total_messages, sizeof(uint64_t), CompareLatency);
    for(uint64_t i = 0; i < total_messages; i++)
    {
        cnt += latency_old[i];
    }
    printf("OPL Latency: Avg:%llu Mid:%lu 95\%:%lu 99\%:%lu\n",
           cnt / total_messages,
           latency_old[total_messages / 2],
           latency_old[total_messages - total_messages / 20],
           latency_old[total_messages - total_messages / 100]);


}