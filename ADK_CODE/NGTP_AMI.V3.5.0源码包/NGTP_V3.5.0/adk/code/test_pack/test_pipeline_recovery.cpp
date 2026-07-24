#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <iostream>
#include <sstream>
#include <adk/util.h>
#include <adk_pack/pipeline.h>

#include <boost/assign/list_inserter.hpp> // for 'push_back()'
#include <boost/assign/list_of.hpp>       // for 'list_of()' and 'ref_list_of()'
#include <boost/property_tree/json_parser.hpp>

using namespace adk;
using std::endl;
using std::cout;

#define TOTAL_MESSAGES              (1000*1000*10000UL)
#define PRINT_CYCLE                 (1000UL*1000UL*100UL)


struct BussinessType
{
    uint32_t bussiness_id;
    uint32_t msg_len;
    char    msg[512];
};

class AppStageWorker : public StageWorker<ADK_IO(int64_t, int64_t)>
{
public:
    AppStageWorker(const std::string& name)
        :   StageWorker<ADK_IO(int64_t, int64_t)>(name)
    {}

    ~AppStageWorker()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        // cout << "message = " << message << endl;
        usleep(1);
        ++cnt_;
        // SetTotalOrderSqn(cnt_);
        std::string word("message_sqn=");
        word.append(std::to_string(cnt_));
        BussinessType* user = (BussinessType*)business_info();
        memcpy(user->msg, word.c_str(), word.size());
        user->msg_len = word.size();
        user->bussiness_id = syscall(SYS_gettid);;
        message += offset_;
        Forward(message);
    }

    int64_t offset_;
    uint64_t cnt_ = 0;
};

class AppStageWorker1 : public StageWorker<ADK_INPUT(1, int64_t, ADK_PL_MODE_SEQUENCIAL)>
{
public:
    AppStageWorker1(const std::string& name)
        :   StageWorker<ADK_INPUT(1, int64_t, ADK_PL_MODE_SEQUENCIAL)>(name)
    {}

    ~AppStageWorker1()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        usleep(1);
        ++cnt_;
        // SetTotalOrderSqn(cnt_);
        std::string word("message_sqn=");
        word.append(std::to_string(cnt_));
        BussinessType* user = (BussinessType*)business_info();
        memcpy(user->msg, word.c_str(), word.size());
        user->msg_len = word.size();
        user->bussiness_id = syscall(SYS_gettid);;
        // cout << "thread id = " << syscall(SYS_gettid) << ", message = " << message << ", dim = " << dim << ", idx = " << idx << endl;
    }
    uint64_t cnt_ = 0;
};

int main(int argc, char const *argv[])
{
    adk_impl::EnableShareMemoryDump(nullptr);
    AppStageWorker stage_worker_1("stage_worker_1");
    stage_worker_1.offset_ = 10000;
    stage_worker_1.SetCpuAffinity("5");

    AppStageWorker stage_worker_2("stage_worker_2");
    stage_worker_2.offset_ = 0;
    stage_worker_2.SetCpuAffinity("6");

    AppStageWorker stage_worker_3("stage_worker_3");
    stage_worker_3.offset_ = 0;
    stage_worker_3.SetCpuAffinity("3");
    stage_worker_3.SetBackoffPolicy<adk::policy::Delay>();

    AppStageWorker1 stage_worker1_1("stage_worker_sequencial");
    stage_worker1_1.SetCpuAffinity("4");

    Pipeline pipeline("ATP", "TE_Cash", 1024);

    pipeline.Connect<Pipeline::kInplace, int64_t>(stage_worker_1, stage_worker_2);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(stage_worker_2, stage_worker_3);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(stage_worker_3, stage_worker1_1);

    // pipeline.Connect<Pipeline::kInplace, int64_t>(stage_worker_1, stage_worker_2);
    // pipeline.Connect<Pipeline::kInplace, int64_t>(stage_worker_2, stage_worker_3);
    // pipeline.Connect<Pipeline::kInplace, int64_t>(stage_worker_3, stage_worker1_1);

    auto entrance = pipeline.CreateEntrance<Pipeline::kInplace>(stage_worker_1, 0, 1024);

    if (!pipeline.Start())
    {
        std::cout << "Pipeline Start fialed:" << pipeline.GetLastError() << std::endl;
        return 1;
    }
    // pipeline.Dump();

    int64_t counter = 0;
    boost::property_tree::ptree pl_stats_ptree;
    std::ostringstream oss;
    uint64_t seq = 1;
    while (1)
    {
        pl_stats_ptree.clear();
        oss.clear();
        oss.str("");
        adk::set_pipeline_total_order_seq_num(seq);
        if (entrance->Forward(counter) != kSuccess)
            continue;
        ++seq;
        if (counter == TOTAL_MESSAGES)
            break;
        
        ++counter;

        usleep(1);

        if (counter % 50000 == 0)
        {
            pipeline.GetStats(pl_stats_ptree);

            boost::property_tree::json_parser::write_json(oss, pl_stats_ptree, true);
            std::cout << oss.str() << std::endl;
            std::cout << stage_worker_1.cnt_ << "|" << stage_worker_2.cnt_ << "|" << stage_worker1_1.cnt_ << std::endl;
        }
    }

    sleep(100000);
    return 0;
}
