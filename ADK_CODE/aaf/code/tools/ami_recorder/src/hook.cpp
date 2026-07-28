#include "hook.h"

namespace ami
{
namespace recorder
{
static Hook* g_rec_hook = nullptr;
Hook::Hook()
{
    g_rec_hook = this;
}

Hook* Hook::Instance()
{
    return g_rec_hook;
}
}
}
