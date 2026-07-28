#include <adk/simple_rate_controller.h>
#include <adk/util.h>
#include <adk/error_code.h>

namespace adk_impl
{
SimpleRateCtrl::SimpleRateCtrl(int32_t rate)
    :   rate_(rate)
{
    impl_ = new SimpleRateController<>(rate);
}

void SimpleRateCtrl::Wait()
{
    assert(impl_ != nullptr);
    ((SimpleRateController<>*)impl_)->Wait();
}

SimpleVariableRateCtrl::SimpleVariableRateCtrl(int32_t min_rate, int32_t max_rate)
    :   min_rate_(min_rate), max_rate_(max_rate)
{
    if (min_rate > max_rate || min_rate == 0)
    {
        impl_ = nullptr;
    }
    else
    {
        impl_ = new SimpleVariableRateController<>(min_rate, max_rate);
    }
}

void SimpleVariableRateCtrl::Wait()
{
    assert(impl_ != nullptr);
    ((SimpleVariableRateController<>*)impl_)->Wait();
}

int32_t SimpleVariableRateCtrl::GetCurrentRate()
{
    assert(impl_ != nullptr);
    return ((SimpleVariableRateController<>*)impl_)->cur_rate_;
}

} // adk
