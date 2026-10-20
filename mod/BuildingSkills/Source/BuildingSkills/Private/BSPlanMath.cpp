#include "BSPlan.h"
#include "Algo/Reverse.h"

namespace
{
    bool Finite(FVector V) { return !V.ContainsNaN() && V.GetAbsMax() <= 1.e8; }
    int32 CapturedPointCount(const FBSPlanEdge& Edge)
    {
        const int32 Count=Edge.Route.Num();
        // 0.10.4 appended the same A-to-B route twice. Only collapse two exact
        // copies with a discontinuous B-to-A jump; legitimate closed graphs and
        // continuous looping splines are unaffected.
        if (Count>=4 && Count%2==0 && Edge.Route[0].Location!=Edge.Route[Count/2-1].Location)
        {
            bool Duplicate=true;
            for (int32 I=0;I<Count/2;++I)
            { const auto& A=Edge.Route[I]; const auto& B=Edge.Route[I+Count/2]; Duplicate &= A.Location==B.Location && A.ArriveTangent==B.ArriveTangent && A.LeaveTangent==B.LeaveTangent; }
            if (Duplicate) return Count/2;
        }
        return Count;
    }
    FVector Along(FVector Direction, FVector Chord)
    { return Direction.GetSafeNormal() * (FVector::DotProduct(Direction, Chord) < 0 ? -1 : 1); }
}
const FBSPlanNode* FBSPlanMath::Node(const FBSRailPlan& P, FGuid Id)
{ return P.Nodes.FindByPredicate([&](const auto& N) { return N.Id == Id; }); }
FVector FBSPlanMath::ConnectionDirection(const FBSRailPlan& P, FGuid Id)
{
    const auto* N=Node(P,Id); if (!N) return FVector::ForwardVector;
    // Straight and captured curves can have a tangent different from the
    // node's original aiming direction. Extend the actual existing path.
    for (const auto& E:P.Edges) if (E.Start==Id || E.End==Id)
    {
        const auto Points=Curve(P,E); if (Points.Num()<2) continue;
        const FVector D=E.Start==Id ? Points[0].LeaveTangent : Points.Last().ArriveTangent;
        if (!D.IsNearlyZero()) return Along(D,N->Direction);
    }
    return N->Direction.GetSafeNormal();
}
bool FBSPlanMath::Valid(const FBSRailPlan& P)
{
    if (!FMath::IsFinite(P.SegmentLength) || P.SegmentLength<0 || P.SegmentLength>1.e8 || uint8(P.Remainder)>2) return false;
    if (P.SignalEvery<1 || P.SignalEvery>2048 || P.ParallelReversed.Num()>P.Parallels.Num() || P.ReversedEdges.Num()>P.Edges.Num()) return false;
    if (P.ParallelNodeOffsets.Num()>P.Parallels.Num() || P.ParallelStartOffsets.Num()>P.Parallels.Num()) return false;
    for (double Offset:P.ParallelNodeOffsets) if (!FMath::IsFinite(Offset) || FMath::Abs(Offset)>100000) return false;
    for (double Offset:P.ParallelStartOffsets) if (!FMath::IsFinite(Offset) || FMath::Abs(Offset)>100000) return false;
    if (!P.Id.IsValid() || P.Name.IsEmpty() || P.Name.Len()>80 || P.Nodes.Num()>2048 || P.Edges.Num()>2048 || P.Parallels.Num()>16) return false;
    TSet<FGuid> IDs;
    for (const auto& N:P.Nodes)
    {
        if (!N.Id.IsValid() || IDs.Contains(N.Id) || !Finite(N.Position) || !Finite(N.Direction) || N.Direction.IsNearlyZero() || !FMath::IsFinite(N.Roll)) return false;
        IDs.Add(N.Id);
    }
    for (const auto& E:P.Edges)
    {
        if (FMath::Abs(E.StartTangentSign)>1 || FMath::Abs(E.EndTangentSign)>1) return false;
        if (!E.Id.IsValid() || IDs.Contains(E.Id) || E.Start==E.End || !Node(P,E.Start) || !Node(P,E.End) || uint8(E.Trace)>3 || E.Controls.Num()>32 || E.ControlDirections.Num()>E.Controls.Num() || E.Route.Num()>64 || (E.Trace==EBSPlanTrace::Bezier && E.Controls.Num()!=2)) return false;
        IDs.Add(E.Id);
        for (FVector C:E.Controls) if (!Finite(C)) return false;
        for (FVector D:E.ControlDirections) if (!Finite(D)) return false;
        for (const auto& C:E.Route) if (!Finite(C.Location) || !Finite(C.ArriveTangent) || !Finite(C.LeaveTangent)) return false;
    }
    for (FVector Offset:P.Parallels) if (!Finite(Offset) || Offset.GetAbsMax()>100000) return false;
    TSet<FGuid> Reversed;
    for (FGuid Id:P.ReversedEdges) { if (Reversed.Contains(Id) || !P.Edges.ContainsByPredicate([&](const auto& E) { return E.Id==Id; })) return false; Reversed.Add(Id); }
    return true;
}
TSet<FGuid> FBSPlanMath::SelectedNodes(const FBSRailPlan& P,const TArray<FGuid>& Selected)
{
    TSet<FGuid> Out;
    for (FGuid Id:Selected)
    {
        if (Node(P,Id)) Out.Add(Id);
        if (const auto* E=P.Edges.FindByPredicate([&](const auto& V) { return V.Id==Id; })) { Out.Add(E->Start); Out.Add(E->End); }
    }
    return Out;
}
bool FBSPlanMath::Apply(FBSRailPlan& P,const FBSPlanCommand& C,FString& Error)
{
    if (C.Revision!=P.Revision) { Error=TEXT("This plan changed. The latest version has been loaded; retry your edit."); return false; }
    FBSRailPlan Draft=P;
    for (auto& Edge:Draft.Edges) Edge.Route.SetNum(CapturedPointCount(Edge));
    if (C.Action==EBSPlanAction::Rename)
    { Draft.Name=C.Name.TrimStartAndEnd(); }
    else if (C.Action==EBSPlanAction::Share) Draft.Shared=true;
    else if (C.Action==EBSPlanAction::Segments)
    { Draft.SegmentLength=C.SegmentLength; Draft.Remainder=C.Remainder; Draft.PlaceBlockSignal=C.PlaceBlockSignal; Draft.SignalEvery=C.SignalEvery; Draft.LeftSideSignal=C.LeftSideSignal; }
    else if (C.Action==EBSPlanAction::ReverseSpline)
    {
        if (C.Selection.Num()!=1) { Error=TEXT("Select exactly one spline to reverse."); return false; }
        auto* Edge=Draft.Edges.FindByPredicate([&](const auto& E) { return E.Id==C.Selection[0]; });
        if (!Edge) { Error=TEXT("Select a spline to reverse its direction."); return false; }
        if (Edge->Trace==EBSPlanTrace::Curve || Edge->Trace==EBSPlanTrace::Free)
        {
            const auto Points=Curve(Draft,*Edge);
            Edge->Route.Reset();
            for (const auto& Point:Points) Edge->Route.Emplace(Point);
        }
        // Reverse parameterization, not the shared nodes. Both Hermite
        // tangents must swap and negate to keep the exact same curve.
        Swap(Edge->Start,Edge->End);
        Swap(Edge->StartTangentSign,Edge->EndTangentSign);
        Edge->StartTangentSign=-Edge->StartTangentSign;
        Edge->EndTangentSign=-Edge->EndTangentSign;
        Algo::Reverse(Edge->Controls);
        // Missing through-point directions mean automatic; retain their
        // association when a partially populated array is reversed.
        if (!Edge->ControlDirections.IsEmpty()) Edge->ControlDirections.SetNumZeroed(Edge->Controls.Num());
        Algo::Reverse(Edge->ControlDirections);
        for (auto& Direction:Edge->ControlDirections) Direction=-Direction;
        Algo::Reverse(Edge->Route);
        for (auto& Point:Edge->Route)
        {
            Swap(Point.ArriveTangent,Point.LeaveTangent);
            Point.ArriveTangent=-Point.ArriveTangent;
            Point.LeaveTangent=-Point.LeaveTangent;
        }
    }
    else if (C.Action==EBSPlanAction::ReverseDirection)
    {
        if (C.Selection.Num()>2048) return false;
        TArray<FBSPlanPath> Paths; if (!FBSPlanSegments::Paths(Draft,Paths,Error,false)) return false;
        for (const auto& Path:Paths) if (Path.Parallel==0)
        {
            bool Selected=C.Selection.IsEmpty();
            for (FGuid Id:Path.Edges) Selected |= C.Selection.Contains(Id);
            if (Selected) for (FGuid Id:Path.Edges) { if (Draft.ReversedEdges.Contains(Id)) Draft.ReversedEdges.Remove(Id); else Draft.ReversedEdges.Add(Id); }
        }
    }
    else if (C.Action==EBSPlanAction::Place)
    {
        FGuid Destination=C.Node.Id;
        if (C.SplitEdge.IsValid())
        {
            auto* Original=Draft.Edges.FindByPredicate([&](const auto& E) { return E.Id==C.SplitEdge; });
            if (!Original || !FMath::IsFinite(C.SplitTime)) return false;
            const auto Points=Curve(Draft,*Original); const int32 I=C.SplitSegment; const double T=C.SplitTime;
            if (I<0 || I+1>=Points.Num() || T<0 || T>1 || Node(Draft,Destination)) return false;
            const FVector At=Point(Points[I],Points[I+1],T), D=Tangent(Points[I],Points[I+1],T);
            if (!At.Equals(C.Node.Position,1) || D.IsNearlyZero() || At.Equals(Points[0].Location,1) || At.Equals(Points.Last().Location,1)) return false;
            auto N=C.Node; N.Position=At; N.Direction=D.GetSafeNormal();
            const auto* A=Node(Draft,Original->Start); const auto* B=Node(Draft,Original->End);
            N.Roll=A->Roll+FMath::FindDeltaAngleDegrees(A->Roll,B->Roll)*(I+T)/(Points.Num()-1);
            auto Left=*Original,Right=*Original; Right.Id=FGuid::NewGuid(); Left.End=Right.Start=N.Id;
            Left.EndTangentSign=Right.StartTangentSign=1;
            if (Draft.ReversedEdges.Contains(Original->Id)) Draft.ReversedEdges.Add(Right.Id);
            TArray<FSplinePointData> L,R;
            if (T==0 || T==1)
            {
                const int32 Boundary=I+(T==1 ? 1 : 0);
                for (int32 J=0;J<=Boundary;++J) L.Add(Points[J]);
                for (int32 J=Boundary;J<Points.Num();++J) R.Add(Points[J]);
            }
            else
            {
                for (int32 J=0;J<=I;++J) L.Add(Points[J]);
                L.Last().LeaveTangent*=T;
                L.Emplace(At,D*T);
                R.Emplace(At,D*(1-T));
                for (int32 J=I+1;J<Points.Num();++J) R.Add(Points[J]);
                R[1].ArriveTangent*=1-T;
            }
            auto Capture=[](FBSPlanEdge& E,const TArray<FSplinePointData>& P) {
                E.Route.Reset(); E.Controls.Reset(); E.ControlDirections.Reset();
                if (E.Trace==EBSPlanTrace::Bezier)
                    E.Controls={P[0].Location+P[0].LeaveTangent/3,P.Last().Location-P.Last().ArriveTangent/3};
                else if (E.Trace!=EBSPlanTrace::Straight)
                {
                    for (const auto& V:P) E.Route.Emplace(V);
                    if (E.Trace==EBSPlanTrace::Free) for (int32 J=1;J+1<P.Num();++J)
                    { E.Controls.Add(P[J].Location); E.ControlDirections.Add(P[J].LeaveTangent.GetSafeNormal()); }
                }
            };
            Capture(Left,L); Capture(Right,R);
            *Original=MoveTemp(Left); Draft.Edges.Add(MoveTemp(Right)); Draft.Nodes.Add(N);
        }
        if (!Node(Draft,Destination))
        {
            if (const auto* Existing=Draft.Nodes.FindByPredicate([&](const auto& N) { return N.Position.Equals(C.Node.Position,1); })) Destination=Existing->Id;
            else Draft.Nodes.Add(C.Node);
        }
        if (C.Edge.Start.IsValid() && C.Edge.Start!=Destination)
        {
            auto Edge=C.Edge; Edge.End=Destination;
            const bool Exists=Draft.Edges.ContainsByPredicate([&](const auto& E) { return (E.Start==Edge.Start && E.End==Edge.End) || (E.Start==Edge.End && E.End==Edge.Start); });
            if (!Exists)
            {
                auto* A=Draft.Nodes.FindByPredicate([&](const auto& N) { return N.Id==Edge.Start; });
                auto* B=Draft.Nodes.FindByPredicate([&](const auto& N) { return N.Id==Edge.End; });
                if (!A || !B) return false;
                // Extending a graph must not rotate shared nodes: their
                // directions also drive all previously placed curves.
                InheritTangents(Draft,Edge);
                Draft.Edges.Add(Edge);
            }
        }
    }
    else if (C.Action==EBSPlanAction::Move)
    {
        if (!Finite(C.Delta) || C.Delta.Size()>1.e7 || C.Selection.Num()>2048) return false;
        const auto Moving=SelectedNodes(Draft,C.Selection);
        for (auto& N:Draft.Nodes) if (Moving.Contains(N.Id)) { N.Position+=C.Delta; }
        for (auto& E:Draft.Edges)
        {
            const bool A=Moving.Contains(E.Start), B=Moving.Contains(E.End);
            if (E.Trace==EBSPlanTrace::Bezier && E.Controls.Num()==2)
            { if (A) E.Controls[0]+=C.Delta; if (B) E.Controls[1]+=C.Delta; E.Route.Reset(); }
            else if (A && B) { for (auto& Point:E.Controls) Point+=C.Delta; for (auto& Point:E.Route) Point.Location+=C.Delta; }
            else if (A || B)
            {
                // Two-point curves retain their inherited endpoint tangents
                // when one endpoint moves, including a tangent sampled from
                // an incident straight or captured spline.
                if (E.Trace==EBSPlanTrace::Curve && E.Route.Num()==2)
                {
                    const double Before=(E.Route[1].Location-E.Route[0].Location).Size();
                    if (A) E.Route[0].Location+=C.Delta;
                    if (B) E.Route[1].Location+=C.Delta;
                    const double Ratio=Before>UE_SMALL_NUMBER ? (E.Route[1].Location-E.Route[0].Location).Size()/Before : 1;
                    for (auto& Point:E.Route) { Point.ArriveTangent*=Ratio; Point.LeaveTangent*=Ratio; }
                }
                else E.Route.Reset();
            }
        }
    }
    else if (C.Action==EBSPlanAction::Node)
    {
        auto* N=Draft.Nodes.FindByPredicate([&](const auto& V) { return V.Id==C.Node.Id; });
        if (!N || !Finite(C.Node.Position) || !Finite(C.Node.Direction) || C.Node.Direction.IsNearlyZero() || !FMath::IsFinite(C.Node.Roll)) return false;
        const FVector Delta=C.Node.Position-N->Position;
        if (!Delta.IsNearlyZero())
        {
            FBSPlanCommand Move; Move.Action=EBSPlanAction::Move; Move.Revision=Draft.Revision; Move.Selection={N->Id}; Move.Delta=Delta;
            if (!Apply(Draft,Move,Error)) return false;
            Draft.Revision=P.Revision; N=Draft.Nodes.FindByPredicate([&](const auto& V) { return V.Id==C.Node.Id; });
        }
        N->Direction=C.Node.Direction.GetSafeNormal(); N->Roll=FRotator::NormalizeAxis(C.Node.Roll);
        AlignNode(Draft,N->Id);
    }
    else if (C.Action==EBSPlanAction::Edge)
    {
        auto* E=Draft.Edges.FindByPredicate([&](const auto& V) { return V.Id==C.Edge.Id; });
        if (!E || E->Start!=C.Edge.Start || E->End!=C.Edge.End) return false;
        *E=C.Edge; E->Route.Reset();
        if (E->Trace==EBSPlanTrace::Bezier && E->Controls.Num()==2)
        {
            auto* A=Draft.Nodes.FindByPredicate([&](const auto& N) { return N.Id==E->Start; });
            auto* B=Draft.Nodes.FindByPredicate([&](const auto& N) { return N.Id==E->End; });
            if (!A || !B) return false;
            const FVector Start=E->Controls[0]-A->Position, End=B->Position-E->Controls[1];
            if (Start.IsNearlyZero() || End.IsNearlyZero()) return false;
            A->Direction=Start.GetSafeNormal(); B->Direction=End.GetSafeNormal();
            AlignNode(Draft,A->Id); AlignNode(Draft,B->Id);
        }
    }
    else if (C.Action==EBSPlanAction::Parallels)
    {
        Draft.Parallels=C.Parallels; Draft.ParallelReversed=C.ParallelReversed;
        Draft.ParallelNodeOffsets=C.ParallelNodeOffsets; Draft.ParallelStartOffsets=C.ParallelStartOffsets;
    }
    else if (C.Action==EBSPlanAction::Delete)
    {
        Draft.Nodes.RemoveAll([&](const auto& N) { return C.Selection.Contains(N.Id); });
        Draft.Edges.RemoveAll([&](const auto& E) { return C.Selection.Contains(E.Id) || !Node(Draft,E.Start) || !Node(Draft,E.End); });
    }
    else return false;
    Draft.ReversedEdges.RemoveAll([&](FGuid Id) { return !Draft.Edges.ContainsByPredicate([&](const auto& E) { return E.Id==Id; }); });
    if (!Valid(Draft)) { Error=TEXT("Invalid plan data or plan limit exceeded (2048 nodes/links, 16 parallel lines)."); return false; }
    Draft.Revision++; P=MoveTemp(Draft); return true;
}
FVector FBSPlanMath::Point(const FSplinePointData& A,const FSplinePointData& B,double T)
{ return FMath::CubicInterp(A.Location,A.LeaveTangent,B.Location,B.ArriveTangent,T); }
FVector FBSPlanMath::Tangent(const FSplinePointData& A,const FSplinePointData& B,double T)
{ return (6*T*T-6*T)*A.Location+(3*T*T-4*T+1)*A.LeaveTangent+(-6*T*T+6*T)*B.Location+(3*T*T-2*T)*B.ArriveTangent; }
bool FBSPlanMath::ShapeValid(const FSplinePointData& A,const FSplinePointData& B,double T,double Radius,double Grade)
{
    const FVector D=Tangent(A,B,T);
    const FVector DD=(12*T-6)*A.Location+(6*T-4)*A.LeaveTangent+(-12*T+6)*B.Location+(6*T-2)*B.ArriveTangent;
    const double Speed=D.Size(), Cross=FVector::CrossProduct(D,DD).Size();
    return Speed>.01 && FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FMath::Abs(D.Z)/Speed,0.0,1.0)))<=Grade+.001 &&
        (Cross<.001 || Speed*Speed*Speed/Cross>=Radius-.1);
}
TArray<FSplinePointData> FBSPlanMath::Curve(const FBSRailPlan& P,const FBSPlanEdge& E)
{
    const auto* A=Node(P,E.Start); const auto* B=Node(P,E.End);
    if (!A || !B) return {};
    if ((E.Trace==EBSPlanTrace::Curve || E.Trace==EBSPlanTrace::Free) && E.Route.Num()>=2 && E.Route[0].Location.Equals(A->Position,1) && E.Route.Last().Location.Equals(B->Position,1))
    { TArray<FSplinePointData> Out; for (int32 I=0;I<CapturedPointCount(E);++I) { const auto& Saved=E.Route[I]; FSplinePointData Point(Saved.Location,Saved.LeaveTangent); Point.ArriveTangent=Saved.ArriveTangent; Out.Add(Point); } return Out; }
    const FVector Delta=B->Position-A->Position;
    if (E.Trace==EBSPlanTrace::Bezier && E.Controls.Num()==2)
        return {FSplinePointData(A->Position,3*(E.Controls[0]-A->Position)),FSplinePointData(B->Position,3*(B->Position-E.Controls[1]))};
    if (E.Trace==EBSPlanTrace::Straight) return {FSplinePointData(A->Position,Delta),FSplinePointData(B->Position,Delta)};
    TArray<FVector> Positions{A->Position};
    if (E.Trace==EBSPlanTrace::Free) Positions.Append(E.Controls);
    Positions.Add(B->Position);
    TArray<FSplinePointData> Out;
    for (int32 I=0;I<Positions.Num();++I)
    {
        FVector Tangent;
        if (I==0) Tangent=(E.StartTangentSign ? A->Direction.GetSafeNormal()*E.StartTangentSign : Along(A->Direction,Positions[1]-Positions[0]))*(Positions[1]-Positions[0]).Size();
        else if (I==Positions.Num()-1) Tangent=(E.EndTangentSign ? B->Direction.GetSafeNormal()*E.EndTangentSign : Along(B->Direction,Positions[I]-Positions[I-1]))*(Positions[I]-Positions[I-1]).Size();
        else Tangent=ControlDirection(P,E,I-1)*((Positions[I+1]-Positions[I-1])*.5).Size();
        Out.Emplace(Positions[I],Tangent);
    }
    return Out;
}
TArray<FSplinePointData> FBSPlanMath::Offset(const TArray<FSplinePointData>& In,FVector Offset,double StartRoll,double EndRoll)
{
    if (Offset.IsNearlyZero()) return In;
    TArray<FSplinePointData> Out;
    for (int32 I=1;I<In.Num();++I)
    {
        const auto& A=In[I-1]; const auto& B=In[I];
        const int32 Steps=FMath::Clamp(FMath::CeilToInt((B.Location-A.Location).Size()/400),8,128);
        auto At=[&](double T) {
            const auto Forward=Tangent(A,B,T).GetSafeNormal();
            const double Roll=StartRoll+FMath::FindDeltaAngleDegrees(StartRoll,EndRoll)*(I-1+T)/(In.Num()-1);
            const auto Right=FQuat(Forward,FMath::DegreesToRadians(Roll)).RotateVector(FVector::CrossProduct(FVector::UpVector,Forward).GetSafeNormal());
            const auto Up=FVector::CrossProduct(Forward,Right).GetSafeNormal();
            return Point(A,B,T)+Forward*Offset.X+Right*Offset.Y+Up*Offset.Z;
        };
        for (int32 J=0;J<=Steps;++J)
        {
            const double T=double(J)/Steps, L=FMath::Max(0.0,T-.0001), R=FMath::Min(1.0,T+.0001);
            const FVector D=(At(R)-At(L))/((R-L)*Steps);
            if (J==0 && !Out.IsEmpty()) Out.Last().LeaveTangent=D;
            else Out.Emplace(At(T),D);
        }
    }
    // Boundary transitions keep adjacent parallel paths tangent-continuous,
    // including offsets with a forward or vertical component.
    if (!Out.IsEmpty())
    {
        Out[0].ArriveTangent=Out[0].LeaveTangent=In[0].LeaveTangent.GetSafeNormal()*Out[0].LeaveTangent.Size();
        Out.Last().ArriveTangent=Out.Last().LeaveTangent=In.Last().ArriveTangent.GetSafeNormal()*Out.Last().ArriveTangent.Size();
    }
    return Out;
}

TArray<FSplinePointData> FBSPlanMath::ParallelCurve(const FBSRailPlan& P,const FBSPlanEdge& E,int32 Parallel,bool Forward)
{
    auto Points=Curve(P,E);
    if (Points.Num()<2 || !P.Parallels.IsValidIndex(Parallel)) return Points;
    const auto* A=Node(P,Forward ? E.Start : E.End);
    const auto* B=Node(P,Forward ? E.End : E.Start);
    if (!Forward)
    {
        Algo::Reverse(Points);
        for (auto& Point:Points)
        {
            Swap(Point.ArriveTangent,Point.LeaveTangent);
            Point.ArriveTangent=-Point.ArriveTangent; Point.LeaveTangent=-Point.LeaveTangent;
        }
    }
    Points=Offset(Points,P.Parallels[Parallel],A->Roll,B->Roll);
    const double Amount=P.ParallelNodeOffsets.IsValidIndex(Parallel) ? P.ParallelNodeOffsets[Parallel] : 0;
    if (Amount==0) return Points;
    // Shift only the replicated endpoints along their local X, then smoothly
    // deform the intervening rail. Zero endpoint derivatives preserve tangent
    // continuity across connected splines.
    const FVector StartShift=ConnectionDirection(P,A->Id)*Amount;
    const FVector EndShift=ConnectionDirection(P,B->Id)*Amount;
    const double Intervals=Points.Num()-1;
    for (int32 I=0;I<Points.Num();++I)
    {
        const double T=I/Intervals, Blend=T*T*(3-2*T);
        Points[I].Location+=FMath::Lerp(StartShift,EndShift,Blend);
        const FVector Derivative=(EndShift-StartShift)*(6*T*(1-T)/Intervals);
        Points[I].ArriveTangent+=Derivative; Points[I].LeaveTangent+=Derivative;
    }
    return Points;
}

void FBSPlanMath::InheritTangents(const FBSRailPlan& P,FBSPlanEdge& Edge)
{
    const auto* A=Node(P,Edge.Start); const auto* B=Node(P,Edge.End); if (!A || !B) return;
    auto Outward=[&](FGuid Id,FVector Toward) {
        TArray<FVector> Directions;
        for (const auto& E:P.Edges) if (E.Id!=Edge.Id && (E.Start==Id || E.End==Id))
        {
            const auto CurvePoints=Curve(P,E); if (CurvePoints.Num()<2) continue;
            Directions.Add((E.End==Id ? CurvePoints.Last().ArriveTangent : -CurvePoints[0].LeaveTangent).GetSafeNormal());
        }
        // At a terminal node continue away from the incident edge even when
        // the new endpoint lies behind it. Flipping toward that endpoint makes
        // a cusp and was the recurring extension-direction bug.
        if (Directions.Num()==1) return Directions[0];
        return Along(ConnectionDirection(P,Id),Toward);
    };
    const FVector Start=Outward(A->Id,B->Position-A->Position),End=-Outward(B->Id,A->Position-B->Position);
    Edge.StartTangentSign=FVector::DotProduct(Start,A->Direction)<0 ? -1 : 1;
    Edge.EndTangentSign=FVector::DotProduct(End,B->Direction)<0 ? -1 : 1;
    // A straight/captured incident curve need not use the node's displayed
    // orientation. Capture that exact direction on the new edge alone.
    if (Edge.Route.IsEmpty() && (Edge.Trace==EBSPlanTrace::Curve || Edge.Trace==EBSPlanTrace::Free))
        for (const auto& Point:Curve(P,Edge)) Edge.Route.Emplace(Point);
    if (Edge.Route.Num()>=2)
    {
        Edge.Route[0].LeaveTangent=Start*Edge.Route[0].LeaveTangent.Size();
        Edge.Route.Last().ArriveTangent=End*Edge.Route.Last().ArriveTangent.Size();
    }
}
FRotator FBSPlanMath::Rotation(const FBSPlanNode& N)
{ auto R=N.Direction.Rotation(); R.Roll=N.Roll; return R; }
FVector FBSPlanMath::ControlDirection(const FBSRailPlan& P,const FBSPlanEdge& E,int32 I)
{
    if (!E.Controls.IsValidIndex(I)) return FVector::ForwardVector;
    const auto* A=Node(P,E.Start); const auto* B=Node(P,E.End);
    if (!A || !B) return FVector::ForwardVector;
    if (E.Trace==EBSPlanTrace::Bezier) return (I==0 ? E.Controls[0]-A->Position : B->Position-E.Controls[I]).GetSafeNormal();
    if (E.ControlDirections.IsValidIndex(I) && !E.ControlDirections[I].IsNearlyZero()) return E.ControlDirections[I].GetSafeNormal();
    const auto Prev=I>0 ? E.Controls[I-1] : A->Position;
    const auto Next=I+1<E.Controls.Num() ? E.Controls[I+1] : B->Position;
    const auto D=(Next-Prev).GetSafeNormal(); return D.IsNearlyZero() ? A->Direction.GetSafeNormal() : D;
}
void FBSPlanMath::AlignNode(FBSRailPlan& P,FGuid Id)
{
    const auto* N=Node(P,Id); if (!N) return;
    for (auto& E:P.Edges)
    {
        const bool Start=E.Start==Id,End=E.End==Id; if (!Start && !End) continue;
        if (E.Trace==EBSPlanTrace::Bezier && E.Controls.Num()==2)
        {
            if (Start) { const FVector D=E.Controls[0]-N->Position; E.Controls[0]=N->Position+Along(N->Direction,D)*D.Size(); }
            if (End) { const FVector D=N->Position-E.Controls[1]; E.Controls[1]=N->Position-Along(N->Direction,D)*D.Size(); }
        }
        if (E.Route.Num()>=2)
        {
            if (Start) { auto& D=E.Route[0].LeaveTangent; D=Along(N->Direction,D)*D.Size(); }
            if (End) { auto& D=E.Route.Last().ArriveTangent; D=Along(N->Direction,D)*D.Size(); }
        }
    }
}
bool FBSPlanMath::Closest(const TArray<FSplinePointData>& Points,FVector Target,int32& Segment,double& Time,FVector& At)
{
    Segment=-1; double Best=TNumericLimits<double>::Max();
    for (int32 I=0;I+1<Points.Num();++I)
    {
        double Near=0,Dist=TNumericLimits<double>::Max();
        for (int32 J=0;J<=32;++J) { const double T=J/32.0,D=FVector::DistSquared(Point(Points[I],Points[I+1],T),Target); if (D<Dist) { Dist=D; Near=T; } }
        double L=FMath::Max(0.0,Near-1.0/32),R=FMath::Min(1.0,Near+1.0/32);
        for (int32 J=0;J<28;++J)
        {
            const double A=(2*L+R)/3,B=(L+2*R)/3;
            if (FVector::DistSquared(Point(Points[I],Points[I+1],A),Target)<FVector::DistSquared(Point(Points[I],Points[I+1],B),Target)) R=B; else L=A;
        }
        double T=(L+R)*.5; if (T<1.e-5) T=0; else if (T>1-1.e-5) T=1; const FVector V=Point(Points[I],Points[I+1],T); const double D=FVector::DistSquared(V,Target);
        if (D<Best) { Best=D; Segment=I; Time=T; At=V; }
    }
    return Segment>=0;
}
