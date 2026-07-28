#include <adk/net_utils.h>
#include <iostream>

using namespace adk;

int main(int argc, char const* argv[])
{
    if (argc != 2)
    {
        std::cout << "Usage: " << argv[0] << " [ip]" << std::endl;
        return -1;
    }

    std::string local;
    int32_t ec = GetConnectableIp(std::string(argv[1]), local);
    if (ec == ErrorCode::kSuccess)
    {
        std::cout << "find: " << local << std::endl;
    }
    else if (ec == ErrorCode::kDefaultGateway)
    {
        std::cout << "not find, default gateway: " << local << std::endl;
    }
    else
    {
        std::cout << "not found" << std::endl;
    }

    return 0;
}