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

class AppStageWorker : public StageWorker<ADK_INPUT(int64_t)>
{
public:
    AppStageWorker(const std::string& name)
        :   StageWorker<ADK_INPUT(int64_t)>(name)
    {}

    ~AppStageWorker()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        cout << "message = " << message << endl;
    }
};


int main(int argc, char const *argv[])
{
    AppStageWorker test_worker("test");

    Pipeline pipeline;
    auto entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(test_worker);
    
    pipeline.Start();

    std::ostringstream oss;
    pipeline.Dump(oss);
    std::cout << oss.str() << std::endl;

    int64_t counter = 0;
    while (1)
    {
        sleep(1);

        if (entrance->Forward(counter) != kSuccess)
            return 1;

        ++counter;
    }
    return 0;
}
