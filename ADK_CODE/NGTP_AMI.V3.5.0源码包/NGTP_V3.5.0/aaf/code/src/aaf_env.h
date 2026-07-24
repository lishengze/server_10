#ifndef AAF_AAF_ENV_H_
#define AAF_AAF_ENV_H_

#include <string>

namespace aaf
{
template<typename T>
inline void ExitByEnv(const std::string& env_name, const T& functor)
{
    char* env_value = std::getenv(env_name.c_str());
    if (env_value != nullptr
        && (*env_value == 'Y' || *env_value == 'y'))
    {
        // to keep incorrect application from coredump!
        functor();
        _exit(0);
    }
}
} // aaf

#endif // AAF_AAF_ENV_H_
