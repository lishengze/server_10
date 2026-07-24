#include <adk/fork_run.h>

using namespace adk;

#include <iostream>
#include <boost/lexical_cast.hpp>

int main(int argc, char const *argv[])
{
    uint32_t* counter = new uint32_t;
    *counter = 0;
    ForkRun fr;
    auto ec = fr.Launch(
        [counter](const std::string& input){
            std::cout << input << std::endl;
            ++(*counter);
        },

        [counter](std::string& output, std::string& err_desc){
            output = std::string("hello world ")
                     + boost::lexical_cast<std::string>(*counter);
            if (*counter == 2)
            {
                err_desc = "error test";
                return false;
            }
            return true;
        }
    );
    assert(ec == ErrorCode::kSuccess);

    ec = fr.Launch();
    assert(ec == ErrorCode::kSuccess);

    std::string err_desc;
    ec = fr.Launch(err_desc);
    assert(ec != ErrorCode::kSuccess);
    std::cout << "err_desc = " << err_desc << std::endl;

    std::cout << "counter = " << *counter << std::endl;
    sleep(10);
    return 0;
}

