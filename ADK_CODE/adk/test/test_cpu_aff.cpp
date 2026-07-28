#include <adk/util.h>
#include <unistd.h>

#include <boost/thread.hpp>

void Sleep()
{
    sleep(10000);
}

int main(int argc, char const *argv[])
{
    std::string desc;
    adk::GetCpuNodeInfo(desc);
    std::cout << desc << std::endl;

    boost::thread t1 = boost::thread(Sleep);
    boost::thread t2 = boost::thread(Sleep);
    boost::thread t3 = boost::thread(Sleep);

    sleep(10000);
    return 0;
}
