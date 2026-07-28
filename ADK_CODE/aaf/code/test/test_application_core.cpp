#include <aaf.h>
#include <adk/util.h>

#include <time.h>
#include <map>
#include <vector>
#include <string>
#include <cstdlib>

#include <boost/algorithm/string.hpp>

using namespace aaf;

class AAFApp : public GenericApplication
{
    ADK_LOG_DECLARE_AC(300000);

public:

    AAFApp()
    {}

    ~AAFApp()
    {}

    virtual int32_t OnRun()
    {
        raise(11);
        std::cout << "OnRun" << std::endl;  
        abort();
        return ErrorCode::kSuccess;
    }

    virtual void OnSignal(int sig_num, int value) 
    {
        std::cout << "OnSignal" << std::endl;
        std::cout << "sig_num: " << sig_num << ", value: " << value << std::endl;
    }
}g_app;

ADK_LOG_DEFINE(AAFApp);

int main(int argc, char const *argv[])
{
    std::cout << "self define main function" << std::endl;
    return aaf::GenericApplication::Main(argc, argv);
}