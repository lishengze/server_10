/**
 * @author ???(niuliangliang@af.local)
 */

#ifndef BRIDGE_TIER_CAHNNEL
#define BRIDGE_TIER_CHANNEL

#include <ami/tier_channel.h>

namespace ami
{
namespace bridge
{

class AmiBridge;
class BridgeTierChannelHandler : public TierChannelHandler
{
public:
    BridgeTierChannelHandler(AmiBridge* bridge) : bridge_(bridge) {}
    virtual ~BridgeTierChannelHandler() = default;
    virtual void ViewChange() {}
    virtual void RoleChange(int32_t tc_role, const Property& role_props);
    virtual void FailoverComplete() {}
    virtual void OnMessage(Message* const message) {}
    virtual void OnMessage(RepMessage* const message);

public:
    int32_t tc_role()
    {
        return tc_role_;
    }

public:
    int32_t tc_role_   = kTcRoleError;
    AmiBridge* bridge_ = nullptr;

    static const int32_t kLogCodeTier = 90000;
    ADK_LOG_DECLARE_AC(kLogCodeTier);
};

}
}

#endif
