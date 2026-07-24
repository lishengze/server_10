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

class OOOStageWorker : public StageWorker<ADK_OOO_INPUT(int64_t), ADK_OUTPUT(int64_t)>
{
public:
    OOOStageWorker(int64_t tag)
        :   tag_(tag)
    {}

    ~OOOStageWorker()
    {}

    int64_t& set_tag(int64_t& message)
    {
        return message |= tag_;
    }

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        assert(message == (int64_t)orig_seq());

        ReorderForward(set_tag(message));
    }

private:
    int64_t tag_;
};

class ReorderStageWorker : public StageWorker<ADK_RO_INPUT(int64_t)>
{
public:
    ReorderStageWorker()
    {}

    ~ReorderStageWorker()
    {}

    int64_t get_tag(int64_t message)
    {
        switch (message & (7l << 61))
        {
            case (1l << 61):
            {
                return 1;
            }
            case (1l << 62):
            {
                return 2;
            }
            case (1l << 63):
            {
                return 3;
            }
            default:
                return 0;
        }
    }

    int64_t get_data(int64_t message)
    {
        return message & (~(7l << 61));
    }

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        std::cout << "tag = " << get_tag(message) << ", message = " << get_data(message) << std::endl;
    }
};

int main(int argc, char const *argv[])
{
    OOOStageWorker ooo_worker_1(1l << 61);
    OOOStageWorker ooo_worker_2(1l << 62);
    OOOStageWorker ooo_worker_3(1l << 63);

    ReorderStageWorker reorder_worker;

    Pipeline pipeline;

    // configure
    auto entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(ooo_worker_1,
                                            ooo_worker_2,
                                            ooo_worker_3);    

    pipeline.ConnectManyToOne(pipeline::list_of(ooo_worker_1,
                                                ooo_worker_2,
                                                ooo_worker_3),
                              reorder_worker);

    // // launch
    pipeline.Start();

    int64_t counter = 0;
    while (true)
    {
        int partition = counter % 3;
        ++counter;
        entrance->Forward(counter, partition);
        sleep(1);
    }
    
    return 0;
}
