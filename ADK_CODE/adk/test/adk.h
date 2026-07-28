
#ifdef VARIANT_TEST
#include <adk/lock_free_queue_variant.h>
#include <adk/lock_free_unbounded_queue_variant.h>
using namespace adk::variant;
#else
#include <adk/lock_free_msg_queue.h>
using namespace adk;
#endif

#include <adk/error_code.h>
#include <adk/arch/generic.h>
#include <adk/simple_rate_controller.h>

#include <boost/thread/thread.hpp>


