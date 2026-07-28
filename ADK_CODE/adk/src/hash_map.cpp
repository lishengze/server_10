#include <adk/hash_map.h>

namespace adk_impl
{

thread_local bool CasLock::is_lock = false;

}