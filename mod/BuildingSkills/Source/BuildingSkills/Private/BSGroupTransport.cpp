#include "BSGroups.h"
#include "BuildingSkills.h"
#include "BSEditModeComponent.h"
#include "Buildables/FGBuildable.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "FGCharacterPlayer.h"
#include "Engine/World.h"

bool FBSGroupTransfer::Pack(const TArray<FBSGroup>& Tree, int32& Group, int32& Member, TArray<FBSGroupFragment>& Out)
{
    Out.Reset();
    int32 Budget = 24000;
    while (Tree.IsValidIndex(Group) && Out.Num() < 8)
    {
        const auto& Source = Tree[Group];
        // Account conservatively for names, NetGUID exports and full transforms.
        const int32 HeaderCost = 512 + Source.Name.Len() * 4;
        if (HeaderCost > 16000 || Member < 0 || Member > Source.Buildings.Num()) return false;
        if (Budget < HeaderCost + 384) break;
        const int32 Count = FMath::Min3(48, Source.Buildings.Num() - Member, (Budget - HeaderCost) / 384);
        auto& Fragment = Out.AddDefaulted_GetRef();
        Fragment.Group.Id = Source.Id; Fragment.Group.Parent = Source.Parent;
        Fragment.Group.Name = Source.Name; Fragment.Group.Pivot = Source.Pivot;
        Fragment.Offset = Member; Fragment.TotalMembers = Source.Buildings.Num();
        if (Count) Fragment.Group.Buildings.Append(Source.Buildings.GetData() + Member, Count);
        Member += Count; Budget -= HeaderCost + Count * 384;
        if (Member == Source.Buildings.Num()) { ++Group; Member = 0; }
    }
    return Group == Tree.Num() || !Out.IsEmpty();
}

bool FBSGroupTransfer::Append(TArray<FBSGroup>& Tree, int32& ExpectedMembers, const TArray<FBSGroupFragment>& Batch, int32 TotalGroups, bool Final)
{
    if (TotalGroups < 0 || TotalGroups > 100000 || Batch.Num() > 8) return false;
    int32 Members = 0;
    for (const auto& F : Batch)
    {
        Members += F.Group.Buildings.Num();
        if (Members > 64 || F.Group.Name.Len() > 4000 || !F.Group.Id.IsValid() || F.TotalMembers < 0 || F.TotalMembers > 2000000 ||
            F.Offset < 0 || F.Offset > F.TotalMembers || F.Group.Buildings.Num() > F.TotalMembers - F.Offset) return false;
        if (F.Offset == 0)
        {
            if ((!Tree.IsEmpty() && Tree.Last().Buildings.Num() != ExpectedMembers) || Tree.Num() >= TotalGroups ||
                Tree.ContainsByPredicate([&](const FBSGroup& G) { return G.Id == F.Group.Id; })) return false;
            Tree.Add(F.Group); ExpectedMembers = F.TotalMembers;
        }
        else
        {
            if (Tree.IsEmpty()) return false;
            auto& G = Tree.Last();
            if (G.Id != F.Group.Id || G.Parent != F.Group.Parent || G.Name != F.Group.Name || G.Pivot != F.Group.Pivot ||
                G.Buildings.Num() != F.Offset || ExpectedMembers != F.TotalMembers) return false;
            G.Buildings.Append(F.Group.Buildings);
        }
    }
    return !Final || (Tree.Num() == TotalGroups && (Tree.IsEmpty() || Tree.Last().Buildings.Num() == ExpectedMembers) && FBSGroupTree::Validate(Tree));
}

void UBSGroupRemoteCallObject::TickGroupTransfer()
{
    TickSelectionTransfer();
    if (GetWorld()->GetNetMode() != NM_Client) return;
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    const double Now = GetWorld()->GetRealTimeSeconds();
    if (!Groups) return;
    if (Now >= NextGroupResolve && !PendingGroupActors.IsEmpty())
    {
        NextGroupResolve = Now + .25;
        bool Resolved = false;
        PendingGroupActors.RemoveAll([&](const FIntPoint& Index)
        {
            if (!Groups->Groups.IsValidIndex(Index.X) || !Groups->Groups[Index.X].Buildings.IsValidIndex(Index.Y)) return true;
            auto& Ref = Groups->Groups[Index.X].Buildings[Index.Y];
            if (!Ref.NetActorId || Ref.LightweightClass) return true;
            if (IsValid(Ref.Actor)) return false;
            auto* Previous = Ref.Actor.Get();
            Ref.Actor = Ref.ResolveNetworkActor(GetWorld());
            Resolved |= Previous != Ref.Actor.Get();
            // Keep tracking: a native actor may leave relevance and return later.
            return false;
        });
        if (Resolved)
            if (auto* Player = GetOwnerPlayerCharacter())
                if (auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>()) Edit->RefreshGroupReferences();
    }
    if (Now < NextGroupPoll) return;
    if (IncomingGroupRequest.IsValid() && Now < GroupTransferDeadline) return;
    IncomingGroupRequest.Invalidate(); IncomingGroups.Reset();
    if (Groups->HasReceivedGroups && Groups->Revision == Groups->PublishedGroupRevision) return;
    IncomingGroupRequest = FGuid::NewGuid(); IncomingGroupSequence = 0; IncomingGroupMembers = 0;
    GroupTransferDeadline = Now + 30; NextGroupPoll = Now + .25;
    Server_GroupSnapshot(IncomingGroupRequest);
}

void UBSGroupRemoteCallObject::Server_GroupSnapshot_Implementation(FGuid Request)
{
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    const double Now = GetWorld()->GetRealTimeSeconds();
    if (!Request.IsValid() || !Groups || !Groups->HasAuthority() || !GetOwnerPlayerCharacter() || Now - LastGroupRequest < .2) return;
    LastGroupRequest = Now;
    OutgoingGroupRequest = Request; OutgoingGroups = Groups->Groups; OutgoingGroupRevision = Groups->Revision;
    OutgoingGroupIndex = OutgoingMemberIndex = OutgoingGroupSequence = 0;
    SendGroupSnapshotBatch();
}

void UBSGroupRemoteCallObject::Server_GroupSnapshotNext_Implementation(FGuid Request, int32 Sequence)
{
    if (Request == OutgoingGroupRequest && Request.IsValid() && Sequence == OutgoingGroupSequence && GetOwnerPlayerCharacter()) SendGroupSnapshotBatch();
}

void UBSGroupRemoteCallObject::SendGroupSnapshotBatch()
{
    TArray<FBSGroupFragment> Batch;
    bool OK = FBSGroupTransfer::Pack(OutgoingGroups, OutgoingGroupIndex, OutgoingMemberIndex, Batch);
    // RPC references can time out before a distant actor's channel is created.
    // Send the native identity independently; never collapse missing actors to a
    // null reference or block every group while that actor is out of relevance.
    auto* Driver = GetWorld()->GetNetDriver();
    for (auto& Fragment : Batch) for (auto& Ref : Fragment.Group.Buildings)
        if (Ref.Actor && Driver && Driver->GuidCache.IsValid())
        {
            Ref.NetActorId = Driver->GuidCache->GetOrAssignNetGUID(Ref.Actor).ObjectId;
            OK &= Ref.NetActorId != 0;
            Ref.Actor = nullptr;
        }
    const bool Final = !OK || OutgoingGroupIndex == OutgoingGroups.Num();
    const auto Request = OutgoingGroupRequest;
    const int32 Sequence = OutgoingGroupSequence++, Total = OutgoingGroups.Num();
    if (Final) { OutgoingGroups.Reset(); OutgoingGroupRequest.Invalidate(); }
    Client_GroupSnapshot(Request, Sequence, OutgoingGroupRevision, Total, Batch, Final, !OK);
}

void UBSGroupRemoteCallObject::Client_GroupSnapshot_Implementation(FGuid Request, int32 Sequence, int32 Revision, int32 TotalGroups,
    const TArray<FBSGroupFragment>& Batch, bool Final, bool Failed)
{
    if (GetWorld()->GetNetMode() != NM_Client || !Request.IsValid() || Request != IncomingGroupRequest) return;
    if (Sequence == 0) { IncomingGroupRevision = Revision; IncomingGroupCount = TotalGroups; }
    if (Failed || Sequence != IncomingGroupSequence || Revision != IncomingGroupRevision || TotalGroups != IncomingGroupCount ||
        !FBSGroupTransfer::Append(IncomingGroups, IncomingGroupMembers, Batch, TotalGroups, Final))
    {
        IncomingGroupRequest.Invalidate(); IncomingGroups.Reset(); NextGroupPoll = GetWorld()->GetRealTimeSeconds() + 2;
        UE_LOG(LogBuildingSkills, Warning, TEXT("Group snapshot rejected; retrying without replacing the current tree."));
        return;
    }
    ++IncomingGroupSequence; GroupTransferDeadline = GetWorld()->GetRealTimeSeconds() + 30;
    if(auto* Player=GetOwnerPlayerCharacter())
        if(auto* Edit=Player->FindComponentByClass<UBSEditModeComponent>(); Edit && Edit->bRequestPending &&
            Edit->PendingRevision!=INDEX_NONE && Revision>=Edit->PendingRevision)
            Edit->GroupRequestDeadline=FPlatformTime::Seconds()+30;
    if (!Final) { Server_GroupSnapshotNext(Request, IncomingGroupSequence); return; }
    if (auto* Groups = ABSGroupSubsystem::Get(GetWorld()); Groups && (!Groups->HasReceivedGroups || Revision >= Groups->Revision))
    {
        Groups->Groups = MoveTemp(IncomingGroups); Groups->Revision = Revision; Groups->HasReceivedGroups = true;
        // A topology transfer can finish after newer transform multicasts.
        FBSTransformNative::UpdateGroupTransforms(Groups->Groups,Groups->TransformSnapshot,true);
        PendingGroupActors.Reset();
        for (int32 G = 0; G < Groups->Groups.Num(); ++G)
            for (int32 B = 0; B < Groups->Groups[G].Buildings.Num(); ++B)
            {
                auto& Ref = Groups->Groups[G].Buildings[B];
                if (Ref.NetActorId)
                {
                    if (!IsValid(Ref.Actor)) Ref.Actor = Ref.ResolveNetworkActor(GetWorld());
                    PendingGroupActors.Add(FIntPoint(G, B));
                }
            }
        UE_LOG(LogBuildingSkills, Display, TEXT("Group snapshot committed: revision=%d groups=%d chunks=%d"), Revision, Groups->Groups.Num(), IncomingGroupSequence);
    }
    IncomingGroupRequest.Invalidate(); IncomingGroups.Reset();
}
