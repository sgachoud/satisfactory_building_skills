#include "BSGroups.h"
#include "BSPlan.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerState.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "TimerManager.h"
#include "BSTransform.h"

namespace
{
    bool Visible(const FBSRailPlan& P,AFGPlayerState* Owner) { return Owner && (P.Shared || P.Owner==Owner); }
}
void UBSGroupRemoteCallObject::Server_PlanCommand_Implementation(const FBSPlanCommand& C)
{
    auto* P=GetOwnerPlayerCharacter(); auto* Store=ABSGroupSubsystem::Get(GetWorld());
    auto* Owner=P ? P->GetPlayerState<AFGPlayerState>() : nullptr;
    if (!Store || !Owner) { Client_PlanResult({},TEXT("Plan storage is not ready.")); return; }
    FString Error; FGuid Id=C.Plan;
    const float ConfigLimit=FBSTransformSettings::Get(GetWorld()).ActionHistorySize;
    const int32 HistoryLimit=FMath::IsFinite(ConfigLimit) ? FMath::Clamp(FMath::RoundToInt(ConfigLimit),0,256) : 32;
    if (C.Action==EBSPlanAction::New)
    {
        const FString Name=C.Name.TrimStartAndEnd();
        int32 Owned=0; for (const auto& Plan:Store->RailPlans) Owned+=Plan.Owner==Owner;
        if (Name.IsEmpty() || Name.Len()>80 || Owned>=32 || Store->RailPlans.Num()>=256) Error=TEXT("Use a name of 1–80 characters. Limit: 32 plans per player.");
        else if (!FMath::IsFinite(C.SegmentLength) || C.SegmentLength<FBSPlanBuild::MinimumLength() || C.SegmentLength>FBSPlanBuild::MaximumLength() ||
            uint8(C.Remainder)>2 || C.SignalEvery<1 || C.SignalEvery>2048)
            Error=TEXT("The new plan's build defaults are outside the supported limits.");
        else
        {
            auto& Plan=Store->RailPlans.AddDefaulted_GetRef(); Plan.Id=FGuid::NewGuid(); Plan.Name=Name; Plan.Owner=Owner; Id=Plan.Id;
            Plan.SegmentLength=C.SegmentLength; Plan.Remainder=C.Remainder;
            Plan.PlaceBlockSignal=C.PlaceBlockSignal; Plan.LeftSideSignal=C.LeftSideSignal;
            Plan.SignalEvery=C.SignalEvery;
        }
    }
    else if (C.Action==EBSPlanAction::Duplicate)
    {
        const auto* Source=Store->RailPlans.FindByPredicate([&](const auto& V) { return V.Id==C.Plan && Visible(V,Owner); });
        int32 Owned=0; for (const auto& Plan:Store->RailPlans) Owned+=Plan.Owner==Owner;
        if (!Source) Error=TEXT("This plan is not available to you.");
        else if (!FBSPlanMath::Valid(*Source)) Error=TEXT("The source plan is invalid and cannot be duplicated.");
        else if (Source->Revision!=C.Revision) Error=TEXT("The plan changed. Review its latest version before duplicating.");
        else if (Owned>=32 || Store->RailPlans.Num()>=256) Error=TEXT("Limit: 32 plans per player and 256 per world.");
        else
        {
            // Copy before growing storage, and give the independent graph new identities.
            auto Copy=*Source;
            Copy.Id=FGuid::NewGuid(); Copy.Owner=Owner; Copy.Shared=false; Copy.Revision=0;
            Copy.Name=Source->Name.Left(73)+TEXT(" (copy)");
            TMap<FGuid,FGuid> Nodes;
            for (auto& Node:Copy.Nodes) { const FGuid Old=Node.Id; Node.Id=FGuid::NewGuid(); Nodes.Add(Old,Node.Id); }
            Copy.ReversedEdges.Reset();
            for (auto& Edge:Copy.Edges)
            {
                const bool Reversed=Source->ReversedEdges.Contains(Edge.Id);
                Edge.Id=FGuid::NewGuid(); Edge.Start=Nodes.FindChecked(Edge.Start); Edge.End=Nodes.FindChecked(Edge.End);
                if (Reversed) Copy.ReversedEdges.Add(Edge.Id);
            }
            Id=Copy.Id;
            Store->RailPlans.Add(MoveTemp(Copy));
        }
    }
    else
    {
        auto* Plan=Store->RailPlans.FindByPredicate([&](const auto& V) { return V.Id==C.Plan && Visible(V,Owner); });
        if (!Plan) Error=TEXT("This plan is not available to you.");
        else if (C.Action==EBSPlanAction::Undo || C.Action==EBSPlanAction::Redo)
            Store->PlanHistories.FindOrAdd(Plan->Id).Restore(*Plan,C.Action==EBSPlanAction::Redo,C.Revision,HistoryLimit,Error);
        else if (C.Action==EBSPlanAction::Build)
        {
            const double Now=GetWorld()->GetRealTimeSeconds();
            if (Now-LastPlanCommand<1) Error=TEXT("Wait before building again.");
            else if (C.Revision!=Plan->Revision) Error=TEXT("The plan changed. Review its latest version before building.");
            else { LastPlanCommand=Now; FBSPlanBuild::Build(P,*Plan,Error); }
        }
        else
        {
            auto Command=C;
            const auto Before=*Plan;
            if (FBSPlanMath::Apply(*Plan,Command,Error))
            {
                if (Command.Action!=EBSPlanAction::Rename && Command.Action!=EBSPlanAction::Share)
                    Store->PlanHistories.FindOrAdd(Plan->Id).Record(Before,*Plan,HistoryLimit);
            }
            else if (Error.IsEmpty()) Error=TEXT("The edit was rejected; review the plan and retry.");
        }
    }
    Client_PlanResult(Id,Error);
    // Supersede any older snapshot. Each response is bounded and acknowledged.
    PlanTransfer.Invalidate(); DeliveredPlanHash=0; LastPlanPoll=-100; Server_PlanSnapshot_Implementation();
}
void UBSGroupRemoteCallObject::Server_PlanSnapshot_Implementation()
{
    const double Now=GetWorld()->GetRealTimeSeconds();
    if ((PlanTransfer.IsValid() && Now<PlanTransferDeadline) || Now-LastPlanPoll<.25) return;
    auto* P=GetOwnerPlayerCharacter(); auto* Store=ABSGroupSubsystem::Get(GetWorld());
    auto* Owner=P ? P->GetPlayerState<AFGPlayerState>() : nullptr;
    if (!Store || !Owner) return;
    LastPlanPoll=Now;
    uint32 Hash=17;
    for (const auto& Plan:Store->RailPlans) if (Visible(Plan,Owner)) { Hash=HashCombine(Hash,GetTypeHash(Plan.Id)); Hash=HashCombine(Hash,GetTypeHash(Plan.Revision)); Hash=HashCombine(Hash,Plan.Shared); }
    if (Hash==DeliveredPlanHash) return;
    OutgoingPlanHash=Hash; OutgoingPlans.Reset();
    for (const auto& Plan:Store->RailPlans) if (Visible(Plan,Owner)) { OutgoingPlans.Add(Plan); OutgoingPlans.Last().Owner=nullptr; }
    PlanTransfer=FGuid::NewGuid(); PlanSequence=PlanIndex=PlanNodeIndex=PlanEdgeIndex=0; SendPlanChunk();
}
void UBSGroupRemoteCallObject::SendPlanChunk()
{
    FBSPlanChunk Chunk; Chunk.Transfer=PlanTransfer; Chunk.Sequence=PlanSequence;
    if (OutgoingPlans.IsValidIndex(PlanIndex))
    {
        const auto& P=OutgoingPlans[PlanIndex]; Chunk.Start=PlanNodeIndex==0 && PlanEdgeIndex==0;
        Chunk.Plan.Id=P.Id; Chunk.Plan.Name=P.Name; Chunk.Plan.Shared=P.Shared; Chunk.Plan.Revision=P.Revision;
        if (Chunk.Start)
        {
            Chunk.Plan.Parallels=P.Parallels; Chunk.Plan.SegmentLength=P.SegmentLength; Chunk.Plan.Remainder=P.Remainder;
            Chunk.Plan.PlaceBlockSignal=P.PlaceBlockSignal; Chunk.Plan.SignalEvery=P.SignalEvery; Chunk.Plan.LeftSideSignal=P.LeftSideSignal;
            Chunk.Plan.ParallelReversed=P.ParallelReversed;
            Chunk.Plan.ParallelNodeOffsets=P.ParallelNodeOffsets; Chunk.Plan.ParallelStartOffsets=P.ParallelStartOffsets;
        }
        for (int32 I=0;I<16 && PlanNodeIndex<P.Nodes.Num();++I) Chunk.Plan.Nodes.Add(P.Nodes[PlanNodeIndex++]);
        for (int32 I=0;I<4 && PlanEdgeIndex<P.Edges.Num();++I)
        { const auto& Edge=P.Edges[PlanEdgeIndex++]; Chunk.Plan.Edges.Add(Edge); if (P.ReversedEdges.Contains(Edge.Id)) Chunk.Plan.ReversedEdges.Add(Edge.Id); }
        if (PlanNodeIndex==P.Nodes.Num() && PlanEdgeIndex==P.Edges.Num()) { ++PlanIndex; PlanNodeIndex=PlanEdgeIndex=0; }
    }
    Chunk.Final=PlanIndex==OutgoingPlans.Num(); PlanTransferDeadline=GetWorld()->GetRealTimeSeconds()+15;
    Client_PlanChunk(Chunk);
    if (Chunk.Final) { PlanTransfer.Invalidate(); OutgoingPlans.Reset(); DeliveredPlanHash=OutgoingPlanHash; }
}
void UBSGroupRemoteCallObject::Server_PlanNext_Implementation(FGuid Transfer,int32 Sequence)
{
    if (Transfer!=PlanTransfer || Sequence!=PlanSequence+1 || GetWorld()->GetRealTimeSeconds()>PlanTransferDeadline) return;
    ++PlanSequence; SendPlanChunk();
}
void UBSGroupRemoteCallObject::Client_PlanChunk_Implementation(const FBSPlanChunk& C)
{
    if (C.Sequence==0) { IncomingPlans.Reset(); ReceivingPlanTransfer=C.Transfer; ReceivingPlanSequence=0; }
    if (C.Transfer!=ReceivingPlanTransfer || C.Sequence!=ReceivingPlanSequence || C.Plan.Nodes.Num()>16 || C.Plan.Edges.Num()>4) return;
    ++ReceivingPlanSequence;
    if (C.Plan.Id.IsValid())
    {
        if (C.Start) { if (IncomingPlans.Num()>=256) return; IncomingPlans.Add(C.Plan); }
        else
        {
            if (IncomingPlans.IsEmpty() || IncomingPlans.Last().Id!=C.Plan.Id) return;
            IncomingPlans.Last().Nodes.Append(C.Plan.Nodes); IncomingPlans.Last().Edges.Append(C.Plan.Edges);
            IncomingPlans.Last().ReversedEdges.Append(C.Plan.ReversedEdges);
        }
    }
    if (C.Final)
    {
        for (const auto& P:IncomingPlans) if (!FBSPlanMath::Valid(P)) return;
        if (auto* Plan=UBSPlanComponent::Find(GetOwnerPlayerCharacter())) Plan->Receive(MoveTemp(IncomingPlans),{},FString());
        ReceivingPlanTransfer.Invalidate();
    }
    else
    {
        // Standalone RPCs execute synchronously. Yield between chunks instead
        // of recursively nesting hundreds of calls for a large graph.
        TWeakObjectPtr<UBSGroupRemoteCallObject> Weak=this; const FGuid Transfer=C.Transfer; const int32 Next=ReceivingPlanSequence;
        GetWorld()->GetTimerManager().SetTimerForNextTick([Weak,Transfer,Next]() { if (Weak.IsValid() && Weak->ReceivingPlanTransfer==Transfer) Weak->Server_PlanNext(Transfer,Next); });
    }
}
void UBSGroupRemoteCallObject::Client_PlanResult_Implementation(FGuid Id,const FString& Message)
{
    if (auto* Plan=UBSPlanComponent::Find(GetOwnerPlayerCharacter()))
    {
        if (!Message.IsEmpty()) Plan->MenuCommands.Reset();
        if (Id.IsValid() && Plan->CurrentId!=Id) Plan->Load(Id);
        Plan->Message=Message;
    }
}
