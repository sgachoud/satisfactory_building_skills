#include "BSPlan.h"
#include "FGPlayerState.h"

namespace
{
    bool HasOnlineIdentity(const FClientIdentityInfo& Identity)
    {
        for (const auto& Account:Identity.AccountIds) if (Account.Value.IsValid()) return true;
        return false;
    }
}
bool FBSPlanOwnership::HasIdentity(const FClientIdentityInfo& Identity)
{ return !Identity.OfflineId.IsEmpty() || HasOnlineIdentity(Identity); }

bool FBSPlanOwnership::Matches(const FClientIdentityInfo& Saved,const FClientIdentityInfo& Current)
{
    // Account handles are interpreted by the native identity serializer, never
    // persisted as process-local integers or compared by player display name.
    for (const auto& Account:Saved.AccountIds)
        if (Account.Value.IsValid())
            if (const auto* Other=Current.AccountIds.Find(Account.Key); Other && *Other==Account.Value) return true;
    // Do not let a shared offline identifier override conflicting online users.
    return !HasOnlineIdentity(Saved) && !HasOnlineIdentity(Current) &&
        !Saved.OfflineId.IsEmpty() && Saved.OfflineId==Current.OfflineId;
}

bool FBSPlanOwnership::Owns(const FBSRailPlan& Plan,AFGPlayerState* Player)
{
    if (!IsValid(Player)) return false;
    const auto& Identity=Player->GetClientIdentity();
    if (HasIdentity(Plan.OwnerIdentity)) return Matches(Plan.OwnerIdentity,Identity);
    // Legacy saves can still recover their owner's identity from the saved
    // PlayerState, even if the reconnecting player uses a different instance.
    return IsValid(Plan.Owner) && (Plan.Owner==Player || Matches(Plan.Owner->GetClientIdentity(),Identity));
}

void FBSPlanOwnership::Assign(FBSRailPlan& Plan,AFGPlayerState* Player)
{
    Plan.Owner=Player;
    Plan.OwnerIdentity=IsValid(Player) ? Player->GetClientIdentity() : FClientIdentityInfo{};
    Plan.OwnerIdentity.FallbackAccountNames.Reset();
}

void FBSPlanOwnership::CaptureLegacy(FBSRailPlan& Plan)
{
    if (!HasIdentity(Plan.OwnerIdentity) && IsValid(Plan.Owner)) Assign(Plan,Plan.Owner);
}
