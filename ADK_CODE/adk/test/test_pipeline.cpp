#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <iostream>
#include <sstream>

#include <adk/pipeline.h>

#include <boost/assign/list_inserter.hpp> // for 'push_back()'
#include <boost/assign/list_of.hpp>       // for 'list_of()' and 'ref_list_of()'
#include <boost/property_tree/json_parser.hpp>

using namespace adk;
using std::endl;
using std::cout;

#define TOTAL_MESSAGES              (1000*1000*10000UL)
#define PRINT_CYCLE                 (1000UL*1000UL*100UL)

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
        cout << "message = " << message << endl;
        message += offset_;
        Forward(message);
    }

    int64_t offset_;
};

class AppStageWorker1 : public StageWorker<ADK_INPUT(2, int64_t, ADK_PL_MODE_SEQUENCIAL)>
{
public:
    AppStageWorker1(const std::string& name)
        :   StageWorker<ADK_INPUT(2, int64_t, ADK_PL_MODE_SEQUENCIAL)>(name)
    {}

    ~AppStageWorker1()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        cout << "thread id = " << syscall(SYS_gettid) << ", message = " << message << ", dim = " << dim << ", idx = " << idx << endl;
    }
};

int main(int argc, char const *argv[])
{
    AppStageWorker stage_worker_1("stage_worker_1");
    stage_worker_1.offset_ = 10000;
    stage_worker_1.SetCpuAffinity("5");

    AppStageWorker stage_worker_2("stage_worker_2");
    stage_worker_2.offset_ = 0;
    stage_worker_2.SetCpuAffinity("6");

    AppStageWorker1 stage_worker1_1("stage_worker_sequencial");
    stage_worker1_1.SetCpuAffinity("7");

    Pipeline pipeline;

    // pipeline.ConnectManyToOne(boost::assign::list_of(stage_worker_1.Next<int64_t>())
    //                                                 (stage_worker_2.Next<int64_t>()),
    //                           stage_worker1_1.Prev());

    pipeline.ConnectManyToOne(pipeline::list_of(stage_worker_1,
                                                stage_worker_2),
                              stage_worker1_1);


    // auto entrance = pipeline.CreateEntrance<2>(boost::assign::list_of(stage_worker_1.Prev())
    //                                                                  (stage_worker_2.Prev()));
    // auto entrance = pipeline.CreateEntrance(pipeline::list_of(stage_worker_1.Prev(),
    //                                                           stage_worker_2.Prev()));

    auto entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(stage_worker_1,
                                                                  stage_worker_2);

    pipeline.Start();

    // pipeline.Dump();

    int64_t counter = 0;
    boost::property_tree::ptree pl_stats_ptree;
    std::ostringstream oss;
    while (1)
    {
        pl_stats_ptree.clear();
        oss.clear();
        oss.str("");
        if (entrance->SequencialForward(counter) != kSuccess)
            continue;

        if (counter == TOTAL_MESSAGES)
            break;
        
        ++counter;

        sleep(1);

        pipeline.GetStats(pl_stats_ptree);

        boost::property_tree::json_parser::write_json(oss, pl_stats_ptree, true);
        std::cout << oss.str() << std::endl;
    }

    sleep(100000);
    return 0;
}
