#include <iostream>

#include <adk/util.h>

static boost::function<void (void)> g_my_func = [](){
        std::cout << "world!";
    };

int main(int argc, char const *argv[])
{

    adk::OnExit<> do_on_exit([](){
        std::cout << " world!" << std::endl;
    });

    adk::OnExit<adk::policy::NoCopy> do_on_exit_v2(&g_my_func);

    std::cout << "hello ";
    return 0;
}
