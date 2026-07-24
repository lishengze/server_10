#include <adk/simple_rate_controller.h>
#include <boost/thread.hpp>
#include <iostream>
#include <stdlib.h>

void Ob(int* i)
{
    auto i_save = *i;
    while (true)
    {
        sleep(1);
        auto i_tmp = *i;
        std::cout << i_tmp - i_save << std::endl;
        i_save = i_tmp;
    }
}

int main(int argc, char const *argv[])
{
    int32_t rate = atoi(argv[1]);
    adk::SimpleRateCtrl controller(rate);
    int i = 0;
    boost::thread(Ob, &i);
    for (i = 0; i < 10000000000; ++i)
    {
        controller.Wait();
    }
    return 0;
}
