#include <adk/random.h>
#include <adk_pack/random.h>

namespace adk
{

int16_t Random(int16_t min, int16_t max)
{
    return adk_impl::Random(min, max);
}

uint16_t Random(uint16_t min, uint16_t max)
{
    return adk_impl::Random(min, max);
}

int32_t Random(int32_t min, int32_t max)
{
    return adk_impl::Random(min, max);
}

uint32_t Random(uint32_t min, uint32_t max)
{
    return adk_impl::Random(min, max);
}

int64_t Random(int64_t min, int64_t max)
{
    return adk_impl::Random(min, max);
}

uint64_t Random(uint64_t min, uint64_t max)
{
    return adk_impl::Random(min, max);
}

double Random(double min, double max)
{
    return adk_impl::Random(min, max);
}

}