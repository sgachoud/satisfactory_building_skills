#include "BSPlan.h"
#include "Algo/Reverse.h"
#include "Algo/BinarySearch.h"

namespace
{
    USplineComponent* MeasurePlanCurve(const TArray<FSplinePointData>& Points)
    {
        auto* Curve=NewObject<USplineComponent>(); Curve->ReparamStepsPerSegment=128; Curve->ClearSplinePoints(false);
        for (int32 I=0;I<Points.Num();++I)
            Curve->AddPoint(FSplinePoint(I,Points[I].Location,Points[I].ArriveTangent,Points[I].LeaveTangent,
                FRotator::ZeroRotator,FVector::OneVector,ESplinePointType::CurveCustomTangent),false);
        Curve->UpdateSpline(); return Curve;
    }
    TArray<FSplinePointData> SlicePlanCurve(const TArray<FSplinePointData>& Points,double A,double B)
    {
        TArray<double> Keys{A};
        for (int32 K=FMath::FloorToInt(A)+1;K<B-1.e-7;++K) Keys.Add(K);
        Keys.Add(B); TArray<FSplinePointData> Part;
        for (int32 K=0;K<Keys.Num();++K)
        {
            const double Key=Keys[K]; const int32 Index=FMath::Clamp(FMath::FloorToInt(Key),0,Points.Num()-2);
            const int32 Knot=FMath::RoundToInt(Key); FVector Location,Arrive,Leave;
            if (FMath::IsNearlyEqual(Key,double(Knot),1.e-6))
            { Location=Points[Knot].Location; Arrive=Points[Knot].ArriveTangent; Leave=Points[Knot].LeaveTangent; }
            else
            { Location=FBSPlanMath::Point(Points[Index],Points[Index+1],Key-Index); Arrive=Leave=FBSPlanMath::Tangent(Points[Index],Points[Index+1],Key-Index); }
            Part.Emplace(Location,Arrive*(K ? Key-Keys[K-1] : Keys[1]-Key),Leave*(K+1<Keys.Num() ? Keys[K+1]-Key : Key-Keys[K-1]));
        }
        return Part;
    }
}
bool FBSPlanSegments::OffsetStart(TArray<FSplinePointData>& Points,double Offset,FString& Error)
{
    if (Points.Num()<2 || !FMath::IsFinite(Offset)) { Error=TEXT("Invalid parallel placement offset."); return false; }
    if (Offset==0) return true;
    if (Offset<0)
    {
        const FVector Direction=Points[0].LeaveTangent.GetSafeNormal();
        if (Direction.IsNearlyZero()) { Error=TEXT("The parallel has no starting tangent to extend."); return false; }
        const FVector Tangent=Direction*(-Offset);
        const FSplinePointData Extra(Points[0].Location-Tangent,Tangent);
        Points[0].ArriveTangent=Tangent; Points.Insert(Extra,0); return true;
    }
    auto* Curve=MeasurePlanCurve(Points);
    if (Offset>=Curve->GetSplineLength()-.001) { Error=TEXT("The parallel placement offset consumes the whole path."); return false; }
    const double Key=Curve->GetInputKeyValueAtDistanceAlongSpline(Offset);
    const int32 Knot=FMath::RoundToInt(Key);
    if (FMath::IsNearlyEqual(Key,double(Knot),1.e-7)) { Points.RemoveAt(0,Knot); return true; }
    const int32 Index=FMath::Clamp(FMath::FloorToInt(Key),0,Points.Num()-2);
    const double T=Key-Index, Remaining=1-T;
    const FSplinePointData First(FBSPlanMath::Point(Points[Index],Points[Index+1],T),
        FBSPlanMath::Tangent(Points[Index],Points[Index+1],T)*Remaining);
    Points.RemoveAt(0,Index+1); Points[0].ArriveTangent*=Remaining; Points.Insert(First,0);
    return true;
}

bool FBSPlanSegments::Paths(const FBSRailPlan& Plan,TArray<FBSPlanPath>& Out,FString& Error,bool CheckContinuity,const TSet<FGuid>& ForcedEndpoints,bool Untrimmed)
{
    Out.Reset();
    if (!FBSPlanMath::Valid(Plan)) { Error=TEXT("The plan is invalid."); return false; }
    TMap<FGuid,TArray<int32>> Incident;
    for (int32 I=0;I<Plan.Edges.Num();++I)
    { Incident.FindOrAdd(Plan.Edges[I].Start).Add(I); Incident.FindOrAdd(Plan.Edges[I].End).Add(I); }
    auto Boundary=[&](FGuid Id) { return Incident.FindChecked(Id).Num()!=2; };
    TSet<int32> Visited;
    TArray<FVector> Offsets{FVector::ZeroVector}; Offsets.Append(Plan.Parallels);
    auto Walk=[&](FGuid Start,int32 First) -> bool {
        TArray<TPair<int32,bool>> Chain;
        FGuid At=Start; int32 Next=First;
        while (!Visited.Contains(Next))
        {
            Visited.Add(Next); const auto& E=Plan.Edges[Next]; const bool Forward=E.Start==At;
            Chain.Emplace(Next,Forward); At=Forward ? E.End : E.Start;
            if (Boundary(At) || At==Start) break;
            const auto& Links=Incident.FindChecked(At); Next=Links[0]==Next ? Links[1] : Links[0];
        }
        // Node insertion order must not reverse an established path when its
        // former first terminal becomes an interior node. Keep the oldest
        // edge's direction as the frame for arrows, offsets and construction.
        const auto* Oldest=&Chain[0];
        for (const auto& Step:Chain) if (Step.Key<Oldest->Key) Oldest=&Step;
        if (!Oldest->Value)
        {
            Algo::Reverse(Chain);
            for (auto& Step:Chain) Step.Value=!Step.Value;
            Swap(Start,At);
        }
        TArray<FSplinePointData> MainPoints;
        const bool NeedKeys=Untrimmed || Plan.SyncParallelsInTurns;
        for (int32 Parallel=0;Parallel<Offsets.Num();++Parallel)
        {
            FBSPlanPath Path; Path.Start=Start; Path.End=At; Path.Parallel=Parallel;
            Path.StartOffset=Parallel>0 && Plan.ParallelStartOffsets.IsValidIndex(Parallel-1) ? Plan.ParallelStartOffsets[Parallel-1] : 0;
            int32 FirstEdge=MAX_int32;
            int32 MainPoint=0;
            for (const auto& Step:Chain)
            {
                const auto& E=Plan.Edges[Step.Key];
                Path.Edges.Add(E.Id); FirstEdge=FMath::Min(FirstEdge,Step.Key);
                auto Points=FBSPlanMath::Curve(Plan,E);
                if (Points.Num()<2) { Error=TEXT("A path has no usable curve."); return false; }
                const int32 MainSpans=Points.Num()-1;
                TArray<double> Keys;
                if (Parallel>0) Points=FBSPlanMath::ParallelCurve(Plan,E,Parallel-1,Step.Value,NeedKeys ? &Keys : nullptr);
                else if (!Step.Value)
                {
                    TArray<FSplinePointData> Reversed;
                    for (int32 I=Points.Num()-1;I>=0;--I) Reversed.Emplace(Points[I].Location,-Points[I].LeaveTangent,-Points[I].ArriveTangent);
                    Points=MoveTemp(Reversed);
                }
                if (NeedKeys)
                {
                    if (Parallel==0) for (int32 I=0;I<Points.Num();++I) Keys.Add(I);
                    for (int32 I=Path.MainKeys.IsEmpty() ? 0 : 1;I<Keys.Num();++I) Path.MainKeys.Add(MainPoint+Keys[I]);
                    MainPoint+=MainSpans;
                }
                if (Path.Nodes.IsEmpty())
                {
                    auto Node=*FBSPlanMath::Node(Plan,Step.Value ? E.Start : E.End);
                    Node.Position=Points[0].Location;
                    Path.Nodes.Add(Node); Path.NodePointIndices.Add(0);
                }
                auto Node=*FBSPlanMath::Node(Plan,Step.Value ? E.End : E.Start);
                Node.Position=Points.Last().Location; Path.Nodes.Add(Node);
                Path.NodePointIndices.Add(Path.Points.Num()+Points.Num()-1-(Path.Points.IsEmpty() ? 0 : 1));
                if (!Path.Points.IsEmpty())
                {
                    if (CheckContinuity && (!Path.Points.Last().Location.Equals(Points[0].Location,.1) ||
                        FVector::DotProduct(Path.Points.Last().ArriveTangent.GetSafeNormal(),Points[0].LeaveTangent.GetSafeNormal())<.999))
                    { Error=TEXT("A path or parallel changes direction discontinuously at a node. Adjust it before building."); return false; }
                    Path.Points.Last().LeaveTangent=Points[0].LeaveTangent;
                    Points.RemoveAt(0);
                }
                Path.Points.Append(Points);
            }
            Path.SignalReverse=Plan.ReversedEdges.Contains(Plan.Edges[FirstEdge].Id) ^ (Parallel>0 && Plan.ParallelReversed.IsValidIndex(Parallel-1) && Plan.ParallelReversed[Parallel-1]);
            if (Untrimmed) { Out.Add(MoveTemp(Path)); continue; }
            if (Parallel==0 && Plan.SyncParallelsInTurns) MainPoints=Path.Points;
            // Apply a placement offset once to the whole connected path. Live
            // rail intersections still force joints, but do not restart the gap.
            int32 Begin=0; double RemainingOffset=Path.StartOffset;
            if (Plan.SyncParallelsInTurns && Parallel>0 && RemainingOffset>0)
            {
                auto* Reference=MeasurePlanCurve(MainPoints);
                if (RemainingOffset>=Reference->GetSplineLength()-.001)
                {
                    if (CheckContinuity) { Error=TEXT("The parallel placement offset consumes the whole main path. Nothing was built."); return false; }
                    Path.PlacementValid=false;
                }
                else
                {
                    const double Key=Reference->GetInputKeyValueAtDistanceAlongSpline(RemainingOffset);
                    const int32 A=FMath::Clamp(Algo::UpperBound(Path.MainKeys,Key)-1,0,Path.MainKeys.Num()-2);
                    const double ParallelKey=A+(Key-Path.MainKeys[A])/(Path.MainKeys[A+1]-Path.MainKeys[A]);
                    RemainingOffset=MeasurePlanCurve(Path.Points)->GetDistanceAlongSplineAtSplineInputKey(ParallelKey);
                }
            }
            for (int32 End=1;End<Path.Nodes.Num();++End)
            {
                if (End+1<Path.Nodes.Num() && !ForcedEndpoints.Contains(Path.Nodes[End].Id)) continue;
                FBSPlanPath Piece; Piece.Start=Path.Nodes[Begin].Id; Piece.End=Path.Nodes[End].Id;
                Piece.Parallel=Parallel; Piece.SignalReverse=Path.SignalReverse;
                Piece.PlacementValid=Path.PlacementValid;
                Piece.Nodes.Append(Path.Nodes.GetData()+Begin,End-Begin+1);
                Piece.Edges.Append(Path.Edges.GetData()+Begin,End-Begin);
                const int32 FirstPoint=Path.NodePointIndices[Begin],LastPoint=Path.NodePointIndices[End];
                Piece.Points.Append(Path.Points.GetData()+FirstPoint,LastPoint-FirstPoint+1);
                const double Length=RemainingOffset>0 ? MeasurePlanCurve(Piece.Points)->GetSplineLength() : 0;
                if (RemainingOffset>0 && RemainingOffset>=Length-.001 && End+1<Path.Nodes.Num())
                { RemainingOffset=FMath::Max(0.0,RemainingOffset-Length); Begin=End; continue; }
                Piece.StartOffset=RemainingOffset;
                FString OffsetError;
                if (!OffsetStart(Piece.Points,RemainingOffset,OffsetError))
                {
                    if (CheckContinuity) { Error=OffsetError+TEXT(" Nothing was built."); return false; }
                    Piece.PlacementValid=false;
                }
                else if (RemainingOffset!=0) Piece.Start.Invalidate();
                RemainingOffset=0; Out.Add(MoveTemp(Piece)); Begin=End;
            }
        }
        return true;
    };
    // Junctions and existing rail connections are mandatory segment endpoints.
    for (const auto& N:Plan.Nodes) if (Incident.Contains(N.Id) && Boundary(N.Id))
        for (int32 E:Incident.FindChecked(N.Id)) if (!Visited.Contains(E) && !Walk(N.Id,E)) return false;
    // The only remaining components are closed loops. Every edge is consumed once.
    for (int32 E=0;E<Plan.Edges.Num();++E) if (!Visited.Contains(E) && !Walk(Plan.Edges[E].Start,E)) return false;
    return Visited.Num()==Plan.Edges.Num();
}

bool FBSPlanSegments::Split(const TArray<FSplinePointData>& Points,double Requested,EBSPlanRemainder Policy,
    double Minimum,double Maximum,TArray<TArray<FSplinePointData>>& Out,FString& Error,TArray<double>* EndKeys)
{
    Out.Reset();
    if (EndKeys) EndKeys->Reset();
    auto Fail=[&](const TCHAR* Reason) { Error=FString(Reason)+TEXT(" Nothing was built. Change the segment length or remainder policy."); return false; };
    if (Points.Num()<2 || !FMath::IsFinite(Requested) || Requested<0 || !FMath::IsFinite(Maximum) || Maximum<=Minimum || uint8(Policy)>2)
        return Fail(TEXT("Invalid rail segment settings."));
    const double Target=Requested==0 ? Maximum : Requested;
    if (Target<Minimum || Target>Maximum) return Fail(TEXT("The requested length is outside the native rail limits."));
    auto* Curve=NewObject<USplineComponent>(); Curve->ReparamStepsPerSegment=128; Curve->ClearSplinePoints(false);
    for (int32 I=0;I<Points.Num();++I)
        Curve->AddPoint(FSplinePoint(I,Points[I].Location,Points[I].ArriveTangent,Points[I].LeaveTangent,
            FRotator::ZeroRotator,FVector::OneVector,ESplinePointType::CurveCustomTangent),false);
    Curve->UpdateSpline(); const double Length=Curve->GetSplineLength();
    if (!FMath::IsFinite(Length) || Length<Minimum || Length/Target>2048) return Fail(TEXT("The path is too short or exceeds 2048 segments."));
    const double Ratio=Length/Target;
    const int32 Exact=FMath::RoundToInt(Ratio);
    const bool Multiple=Exact>0 && FMath::Abs(Length-Exact*Target)<=.01;
    const int32 Count=Multiple ? Exact : Policy==EBSPlanRemainder::Increase ? FMath::FloorToInt(Ratio) : FMath::CeilToInt(Ratio);
    if (Count<1 || Count>2048) return Fail(TEXT("The chosen remainder policy cannot divide this path."));
    TArray<double> Lengths;
    if (Policy==EBSPlanRemainder::Last)
    {
        for (int32 I=1;I<Count;++I) Lengths.Add(Target);
        const double Tail=Length-(Count-1)*Target;
        if (Tail>=Minimum-.01) Lengths.Add(Tail);
        else if (!Lengths.IsEmpty())
        {
            const double Remaining=Lengths.Pop()+Tail;
            if (Remaining<=Maximum+.01) Lengths.Add(Remaining);
            else { Lengths.Add(Remaining*.5); Lengths.Add(Remaining*.5); }
        }
        else return Fail(TEXT("The full path is shorter than the minimum rail length."));
    }
    else for (int32 I=0;I<Count;++I) Lengths.Add(Length/Count);
    for (double Size:Lengths) if (Size<Minimum-.01 || Size>Maximum+.01)
        return Fail(TEXT("The chosen remainder policy produces a segment outside the native rail limits."));
    TArray<TArray<FSplinePointData>> Parts;
    double Distance=0;
    for (int32 I=0;I<Lengths.Num();++I)
    {
        const double A=I==0 ? 0 : Curve->GetInputKeyValueAtDistanceAlongSpline(Distance);
        Distance+=Lengths[I];
        const double B=I==Lengths.Num()-1 ? Points.Num()-1 : Curve->GetInputKeyValueAtDistanceAlongSpline(Distance);
        Parts.Add(SlicePlanCurve(Points,A,B));
        if (EndKeys) EndKeys->Add(B);
    }
    Out=MoveTemp(Parts); return true;
}

bool FBSPlanSegments::Prepare(const FBSRailPlan& Plan,double Minimum,double Maximum,
    TArray<FBSPlanSegmentedPath>& Out,FString& Error,const TSet<FGuid>& ForcedEndpoints)
{
    Out.Reset();
    TArray<FBSPlanSegmentedPath> Prepared;
    TArray<FBSPlanPath> PathsToSplit;
    const bool Sync=Plan.SyncParallelsInTurns && !Plan.Parallels.IsEmpty();
    if (!Paths(Plan,PathsToSplit,Error,true,ForcedEndpoints,Sync)) return false;
    auto Fail=[&](const TCHAR* Reason) { Error=FString(Reason)+TEXT(" Nothing was built."); return false; };
    if (!FMath::IsFinite(Minimum) || !FMath::IsFinite(Maximum) || Minimum<=0 || Maximum<=Minimum)
        return Fail(TEXT("Invalid native rail length limits."));
    int32 TotalParts=0;
    if (!Sync)
    {
        for (auto& Path:PathsToSplit)
        {
            auto& Result=Prepared.AddDefaulted_GetRef(); Result.Path=MoveTemp(Path);
            if (!Split(Result.Path.Points,Plan.SegmentLength,Plan.Remainder,Minimum,Maximum,Result.Parts,Error)) return false;
            TotalParts+=Result.Parts.Num();
            if (TotalParts>2048) return Fail(TEXT("This plan exceeds the build limit of 2048 rail segments."));
        }
        Out=MoveTemp(Prepared); return true;
    }
    // Each group is one connected main path followed by its parallel paths.
    // Mapping uses spline parameters carried by Offset, never a closest-point
    // search (which would confuse crossings, loops and neighbouring turns).
    for (int32 First=0;First<PathsToSplit.Num();)
    {
        int32 Last=First+1;
        while (Last<PathsToSplit.Num() && PathsToSplit[Last].Parallel!=0) ++Last;
        const auto& Main=PathsToSplit[First];
        auto* MainCurve=MeasurePlanCurve(Main.Points);
        const double MainLength=MainCurve->GetSplineLength();
        if (!FMath::IsFinite(MainLength) || MainLength<Minimum)
            return Fail(TEXT("A synchronized main path is shorter than the minimum rail length."));
        TArray<int32> SectionEnds;
        for (int32 I=1;I<Main.Nodes.Num();++I)
            if (I+1==Main.Nodes.Num() || ForcedEndpoints.Contains(Main.Nodes[I].Id)) SectionEnds.Add(Main.NodePointIndices[I]);
        struct FParallelMeasure
        {
            TArray<FSplinePointData> Points;
            USplineComponent* Curve=nullptr;
            int32 Leading=0;
        };
        TArray<FParallelMeasure> Measures;
        for (int32 I=First;I<Last;++I)
        {
            const auto& Path=PathsToSplit[I]; auto& M=Measures.AddDefaulted_GetRef(); M.Points=Path.Points;
            if (Path.MainKeys.Num()!=Path.Points.Num() || Path.StartOffset>=MainLength-.001)
                return Fail(TEXT("A parallel placement offset consumes the whole main path."));
            if (Path.StartOffset<0)
            {
                if (!OffsetStart(M.Points,Path.StartOffset,Error)) return false;
                M.Leading=1;
            }
            M.Curve=MeasurePlanCurve(M.Points);
        }
        auto KeyAt=[&](int32 Index,double Station) -> double {
            const auto& Path=PathsToSplit[First+Index]; const auto& M=Measures[Index];
            if (Station<0) return (Station-Path.StartOffset)/(-Path.StartOffset);
            const double Key=Station>=MainLength ? Main.Points.Num()-1 : MainCurve->GetInputKeyValueAtDistanceAlongSpline(FMath::Max(0.0,Station));
            const int32 A=FMath::Clamp(Algo::UpperBound(Path.MainKeys,Key)-1,0,Path.MainKeys.Num()-2);
            const double T=(Key-Path.MainKeys[A])/(Path.MainKeys[A+1]-Path.MainKeys[A]);
            return M.Leading+A+FMath::Clamp(T,0.0,1.0);
        };
        double Target=Plan.SegmentLength==0 ? Maximum : Plan.SegmentLength;
        bool Fitted=false;
        for (int32 Attempt=0;Attempt<128 && !Fitted;++Attempt)
        {
            TArray<double> ReferenceCuts{0}; int32 Begin=0;
            for (int32 End:SectionEnds)
            {
                TArray<FSplinePointData> Section; Section.Append(Main.Points.GetData()+Begin,End-Begin+1);
                TArray<TArray<FSplinePointData>> Ignored; TArray<double> Keys;
                if (!Split(Section,Target,Plan.Remainder,Minimum,Maximum,Ignored,Error,&Keys)) return false;
                for (double Key:Keys) ReferenceCuts.Add(MainCurve->GetDistanceAlongSplineAtSplineInputKey(Begin+Key));
                Begin=End;
            }
            ReferenceCuts.Last()=MainLength;
            // Negative offsets extend a parallel before the main start. Keep
            // the reference spacing going past the main end so that the whole
            // longer parallel still receives joints, even for a large stagger.
            double ReferenceEnd=MainLength;
            for (int32 I=First;I<Last;++I) ReferenceEnd=FMath::Max(ReferenceEnd,MainLength-PathsToSplit[I].StartOffset);
            const double EndSpacing=ReferenceCuts.Last()-ReferenceCuts[ReferenceCuts.Num()-2];
            if (ReferenceEnd>MainLength+.001)
            {
                if (EndSpacing<=.001 || (ReferenceEnd-MainLength)/EndSpacing>2048)
                    return Fail(TEXT("The parallel extension exceeds the rail segment limit."));
                for (double At=MainLength+EndSpacing;At<ReferenceEnd-.001;At+=EndSpacing) ReferenceCuts.Add(At);
                ReferenceCuts.Add(ReferenceEnd);
            }
            TArray<TArray<double>> AllKeys;
            double Longest=0; bool TooShort=false; int32 Count=0;
            for (int32 I=0;I<Measures.Num();++I)
            {
                const auto& Path=PathsToSplit[First+I]; const auto& M=Measures[I];
                struct FCut { double Station; bool Required; };
                TArray<FCut> Cuts{{Path.StartOffset,true},{MainLength,true}};
                for (double At:ReferenceCuts)
                {
                    const double Shifted=At+Path.StartOffset;
                    if (Shifted>Path.StartOffset+.001 && Shifted<MainLength-.001) Cuts.Add({Shifted,false});
                }
                // Existing connections are mandatory joints, including ones
                // inside a path. Offsets never erase or shift these joints.
                for (int32 N=0;N<Main.Nodes.Num();++N) if (ForcedEndpoints.Contains(Main.Nodes[N].Id))
                {
                    const double At=MainCurve->GetDistanceAlongSplineAtSplineInputKey(Main.NodePointIndices[N]);
                    if (At>Path.StartOffset+.001 && At<MainLength-.001) Cuts.Add({At,true});
                }
                Cuts.Sort([](const FCut& A,const FCut& B) { return A.Station<B.Station; });
                for (int32 K=Cuts.Num()-1;K>0;--K) if (FMath::Abs(Cuts[K].Station-Cuts[K-1].Station)<.001)
                { Cuts[K-1].Required |= Cuts[K].Required; Cuts.RemoveAt(K); }
                auto Size=[&](int32 A,int32 B) {
                    return double(M.Curve->GetDistanceAlongSplineAtSplineInputKey(KeyAt(I,Cuts[B].Station)))-
                        M.Curve->GetDistanceAlongSplineAtSplineInputKey(KeyAt(I,Cuts[A].Station));
                };
                // A stagger may leave a tiny terminal remainder. Merge that
                // optional last joint, then include the enlarged tail in the
                // shared maximum-length check; never remove a live junction.
                while (Cuts.Num()>2 && !Cuts[Cuts.Num()-2].Required && Size(Cuts.Num()-2,Cuts.Num()-1)<Minimum-.01)
                    Cuts.RemoveAt(Cuts.Num()-2);
                auto& Keys=AllKeys.AddDefaulted_GetRef();
                for (int32 K=0;K<Cuts.Num();++K)
                {
                    Keys.Add(KeyAt(I,Cuts[K].Station));
                    if (K>0) { const double Length=Size(K-1,K); Longest=FMath::Max(Longest,Length); TooShort |= Length<Minimum-.01; }
                }
                Count+=Keys.Num()-1;
            }
            if (TotalParts+Count>2048) return Fail(TEXT("Synchronized parallels exceed the build limit of 2048 rail segments."));
            if (Longest>Maximum+.01)
            {
                // Remainder policies still choose the cuts, but use a shorter
                // shared target when the outside track would be too long.
                const double Reduced=FMath::Max(Minimum,Target*FMath::Min(.95,Maximum/Longest*.999));
                if (Reduced>=Target-.001) return Fail(TEXT("The parallel offsets cannot fit within the rail length limits."));
                Target=Reduced; continue;
            }
            if (TooShort) return Fail(TEXT("Synchronizing these offsets produces a rail below the minimum length. Adjust the offsets or segment length, or disable synchronization."));
            for (int32 I=0;I<Measures.Num();++I)
            {
                auto& Result=Prepared.AddDefaulted_GetRef(); Result.Path=PathsToSplit[First+I];
                const auto& Keys=AllKeys[I]; const auto& Points=Measures[I].Points;
                Result.Path.Points=SlicePlanCurve(Points,Keys[0],Keys.Last());
                if (Result.Path.StartOffset!=0) Result.Path.Start.Invalidate();
                for (int32 K=1;K<Keys.Num();++K) Result.Parts.Add(SlicePlanCurve(Points,Keys[K-1],Keys[K]));
            }
            TotalParts+=Count; Fitted=true;
        }
        if (!Fitted) return Fail(TEXT("Could not fit synchronized rail segments within the native length limits."));
        First=Last;
    }
    Out=MoveTemp(Prepared); return true;
}
