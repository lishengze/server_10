#include <adk/pipeline_sharding.h>
#include <adk_pack/pipeline_sharding.h>

namespace adk_impl
{
namespace pipeline
{
namespace sharding
{
void set_is_sharding(bool value)
{
    adk::pipeline::sharding::g_is_shading = value;
}
} // namespace sharding
} // namespace pipeline
} // namespace adk_impl
