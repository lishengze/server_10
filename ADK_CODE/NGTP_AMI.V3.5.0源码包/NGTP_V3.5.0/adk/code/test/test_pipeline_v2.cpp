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
using boost::assign::list_of;

#define TOTAL_MESSAGES              (1000*1000*10000UL)
#define PRINT_CYCLE                 (1000UL*1000UL*100UL)

class AppStageWorker : public StageWorker<ADK_INPUT(int64_t), ADK_OUTPUT(2, int64_t)>
{
public:
    AppStageWorker(const std::string& name)
        :   StageWorker<ADK_INPUT(int64_t), ADK_OUTPUT(2, int64_t)>(name)
    {}

    ~AppStageWorker()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        cout << "message = " << message << endl;
        Forward(message);
    }
};

class AppStageWorker1 : public StageWorker<ADK_INPUT(int64_t)>
{
public:
    AppStageWorker1(const std::string& name)
        :   StageWorker<ADK_INPUT(int64_t)>(name)
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
    stage_worker_1.SetBackoffPolicy<policy::Pause>();

    uint32_t val = 8192;
    if (stage_worker_1.ConfigBackoffPolicy(ADK_BACKOFF_LIMIT, &val, sizeof val) != adk::ErrorCode::kSuccess)
    {
        return 1;
    }

    AppStageWorker1 stage_worker1_1("stage_worker1_1");
    AppStageWorker1 stage_worker1_2("stage_worker1_2");

    Pipeline pipeline;

    // pipeline.ConnectOneToMany(stage_worker_1.GetNext<2, int64_t>(), list_of(stage_worker1_1.Prev())
    //                                                                     (stage_worker1_2.Prev()));
    // pipeline.ConnectOneToMany(stage_worker_1.Next0(), pipeline::list_of(stage_worker1_1.Prev(),
    //                                                                     stage_worker1_2.Prev()));
    // pipeline.ConnectOneToMany(stage_worker_1, pipeline::list_of(stage_worker1_1.Prev(),
    //                                                             stage_worker1_2.Prev()));

    pipeline.ConnectOneToMany(stage_worker_1, pipeline::list_of(stage_worker1_1,
                                                                stage_worker1_2));
    auto entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(stage_worker_1);

    pipeline.Start();

    std::ostringstream oss;
    pipeline.Dump(oss);
    std::cout << oss.str() << std::endl;

    int64_t counter = 0;
    boost::property_tree::ptree pl_stats_ptree;
    
    while (1)
    {
        pl_stats_ptree.clear();
        oss.clear();
        oss.str("");
        if (entrance->Forward(counter) != kSuccess)
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
