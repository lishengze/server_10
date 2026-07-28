#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>


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

struct TestStr
{
    double d;
    uint64_t ui ;
};

class AppStageWorker : public StageWorker<ADK_IO(int64_t,int64_t,TestStr)>
{
public:
    AppStageWorker(const std::string& name)
        : StageWorker<ADK_IO(int64_t,int64_t,TestStr)>(name)
    {}

    ~AppStageWorker()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        cout << "message = " << message << endl;
        if (message % 2 == 0)
            Forward(message);
        else
        {
            double d = message*1.0+0.000001 ;
            TestStr t ;
            t.d = d ;
            t.ui = message ;
            Forward(t);
        }    
    }
};


class AppStageWorker3 : public StageWorker<ADK_IO(TestStr,TestStr)>
{
public:
    AppStageWorker3(const std::string& name)
        : StageWorker<ADK_IO(TestStr,TestStr)>(name)
    {}

    ~AppStageWorker3()
    {}  

    virtual void OnMessage(TestStr& message, short dim, short idx)
    {
        cout<<name()<<endl;
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
        cout <<" name="<<name()<<std::fixed<< " message1 = " << message << endl;
    }
};

class AppStageWorker2 : public StageWorker<ADK_INPUT(TestStr)>
{
public:
    AppStageWorker2(const std::string& name)
        :   StageWorker<ADK_INPUT(TestStr)>(name)
    {}

    ~AppStageWorker2()
    {}  

    virtual void OnMessage(TestStr& message, short dim, short idx)
    {
        cout <<" name="<<name()<<std::fixed  \
            << "  message2.d = " << message.d <<"  message2.ui="<<message.ui<< endl;
    }
};


int main(int argc, char const *argv[])
{
    AppStageWorker test_worker("test");
    AppStageWorker1 test_worker1("test1");
   // AppStageWorker1 test_worker2("test2");
    AppStageWorker2 test_worker2("test2");
    AppStageWorker3 test_worker3("test3");
    AppStageWorker3 test_worker4("test4");

    
    test_worker.SetBackoffPolicy<policy::Event>() ;
    test_worker1.SetBackoffPolicy<policy::Event>() ;
    test_worker2.SetBackoffPolicy<policy::Event>() ;
    test_worker3.SetBackoffPolicy<policy::Event>() ;
    test_worker4.SetBackoffPolicy<policy::Event>() ;
    

    Pipeline pipeline;

    //pipeline.ConnectOneToMany(test_worker,pipeline::list_of(test_worker1,test_worker2));
    pipeline.Connect<Pipeline::kMessaging, int64_t>(test_worker,test_worker1) ;
    pipeline.Connect<Pipeline::kMessaging, TestStr>(test_worker,test_worker3);
    pipeline.Connect<Pipeline::kMessaging, TestStr>(test_worker3,test_worker4);
    pipeline.Connect<Pipeline::kMessaging, TestStr>(test_worker4,test_worker2);

    auto entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(test_worker);
    
    pipeline.Start();

    std::ostringstream oss;
    pipeline.Dump(oss);
    std::cout << oss.str() << std::endl;

    int64_t counter = 0;
    while (1)
    {
        sleep(10);

        if (entrance->Forward(counter) != kSuccess)
            return 1;

        ++counter;
    }
    return 0;
}
