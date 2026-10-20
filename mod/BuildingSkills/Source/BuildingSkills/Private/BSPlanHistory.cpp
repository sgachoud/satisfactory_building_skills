#include "BSPlan.h"

namespace
{
    uint64 GraphBytes(const FBSRailPlan& P)
    {
        uint64 Size=sizeof(P)+P.Nodes.GetAllocatedSize()+P.Edges.GetAllocatedSize()+P.Parallels.GetAllocatedSize()+P.Name.GetAllocatedSize();
        Size+=P.ParallelNodeOffsets.GetAllocatedSize()+P.ParallelStartOffsets.GetAllocatedSize();
        for (const auto& E:P.Edges) Size+=E.Controls.GetAllocatedSize()+E.ControlDirections.GetAllocatedSize()+E.Route.GetAllocatedSize();
        return Size;
    }
}
void FBSPlanHistory::Trim(int32 Limit)
{
    Limit=FMath::Clamp(Limit,0,256);
    if (Undo.Num()>Limit) Undo.RemoveAt(0,Undo.Num()-Limit);
    if (Redo.Num()>Limit) Redo.RemoveAt(0,Redo.Num()-Limit);
    uint64 Bytes=0; for (const auto& P:Undo) Bytes+=GraphBytes(P); for (const auto& P:Redo) Bytes+=GraphBytes(P);
    // A complex graph cannot multiply into an unbounded session history.
    while (Bytes>16*1024*1024 && (!Undo.IsEmpty() || !Redo.IsEmpty()))
    { auto& Stack=Undo.Num()>=Redo.Num() ? Undo : Redo; Bytes-=GraphBytes(Stack[0]); Stack.RemoveAt(0); }
}
void FBSPlanHistory::Record(const FBSRailPlan& Before,const FBSRailPlan& After,int32 Limit)
{
    auto Compare=After; Compare.Revision=Before.Revision;
    if (FBSRailPlan::StaticStruct()->CompareScriptStruct(&Before,&Compare,0)) return;
    Redo.Reset(); if (Limit>0) Undo.Add(Before); Trim(Limit);
}
bool FBSPlanHistory::Restore(FBSRailPlan& Plan,bool Forward,int32 Revision,int32 Limit,FString& Error)
{
    if (Revision!=Plan.Revision) { Error=TEXT("This plan changed. Review its latest version before undoing or redoing."); return false; }
    Trim(Limit); auto& Source=Forward ? Redo : Undo; auto& Destination=Forward ? Undo : Redo;
    if (Source.IsEmpty()) { Error=Forward ? TEXT("Nothing to redo in this plan.") : TEXT("Nothing to undo in this plan."); return false; }
    Destination.Add(Plan); auto Previous=Source.Pop();
    Plan.Nodes=MoveTemp(Previous.Nodes); Plan.Edges=MoveTemp(Previous.Edges); Plan.Parallels=MoveTemp(Previous.Parallels);
    Plan.SegmentLength=Previous.SegmentLength; Plan.Remainder=Previous.Remainder;
    Plan.PlaceBlockSignal=Previous.PlaceBlockSignal; Plan.SignalEvery=Previous.SignalEvery; Plan.LeftSideSignal=Previous.LeftSideSignal;
    Plan.ParallelReversed=MoveTemp(Previous.ParallelReversed); Plan.ReversedEdges=MoveTemp(Previous.ReversedEdges);
    Plan.ParallelNodeOffsets=MoveTemp(Previous.ParallelNodeOffsets); Plan.ParallelStartOffsets=MoveTemp(Previous.ParallelStartOffsets);
    ++Plan.Revision; Trim(Limit); return true;
}
