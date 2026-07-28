#include <boost/thread/thread.hpp>
#include <adk/rate_limit.h>

static void AppLogFunc(const std::string& message)
{
    std::cout << message << std::endl;
}

void Func1()
{
    while (1)
    {
        ADK_DEFINE_RATELIMIT_STATE_DEFAULT(_rs);
        if (!adk::IsRateLimit(_rs))
            std::cout << __FUNCTION__ << std::endl;    
        usleep(500000);
    }
}

void Func2()
{
    while (1)
    {
        ADK_DEFINE_RATELIMIT_STATE(_rs, 1000000UL, 1);
        if (!adk::IsRateLimit(_rs, AppLogFunc))
            std::cout << __FUNCTION__ << std::endl;    
        usleep(500000);
    }
}

int main(int argc, char const *argv[])
{
    //boost::thread t1 = boost::thread(boost::bind(Func1));
    boost::thread t2 = boost::thread(boost::bind(Func2));
    // t1.join();
    t2.join();
    return 0;
}


