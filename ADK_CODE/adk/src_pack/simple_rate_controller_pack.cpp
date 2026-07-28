#include <adk_pack/simple_rate_controller.h>
#include <adk/util.h>
#include <adk/error_code.h>

namespace adk
{
SimpleRateCtrl::SimpleRateCtrl(int32_t rate)
    :   rate_(rate)
{
    impl_ = new adk_impl::SimpleRateController<>(rate);
}

void SimpleRateCtrl::Wait()
{
    assert(impl_ != nullptr);
    ((adk_impl::SimpleRateController<>*)impl_)->Wait();
}
} // adk
