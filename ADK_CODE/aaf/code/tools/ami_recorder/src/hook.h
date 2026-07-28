#ifndef ABF_HOOK_H_
#define ABF_HOOK_H_

namespace ami
{
namespace recorder
{
class Hook
{
public:
    Hook();
    static Hook* Instance();
    virtual void OnRecorderExit() = 0;
};
}
}

#endif  // ABF_HOOK_H_
