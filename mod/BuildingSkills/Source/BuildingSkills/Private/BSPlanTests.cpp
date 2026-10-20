#include "BSPlan.h"
#include "BSStick.h"

bool RunBuildingSkillsPlanMathChecks(TFunctionRef<void(const TCHAR*,bool)> Check)
{
    bool Passed=true;
    auto Test=[&](const TCHAR* Name,bool Value) { Check(Name,Value); Passed &= Value; };
    bool Forward=true,Sideways=true,Vertical=true;
    const FVector Expected[]={FVector(1,0,0),FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0)};
    for (int32 Quadrant=0;Quadrant<4;++Quadrant) for (double Offset:{-44.0,0.0,44.0})
    {
        const double Yaw=Quadrant*90.0+Offset;
        Forward &= UBSPlanComponent::MovementDirection(0,Yaw)==Expected[Quadrant] && UBSPlanComponent::MovementDirection(1,Yaw)==-Expected[Quadrant];
        const FVector Right=FVector::CrossProduct(FVector::UpVector,Expected[Quadrant]);
        Sideways &= UBSPlanComponent::MovementDirection(2,Yaw)==-Right && UBSPlanComponent::MovementDirection(3,Yaw)==Right;
        Vertical &= UBSPlanComponent::MovementDirection(4,Yaw)==FVector::UpVector && UBSPlanComponent::MovementDirection(5,Yaw)==-FVector::UpVector;
    }
    Test(TEXT("Forward/backward arrows follow the nearest gaze axis in every quadrant"),Forward);
    Test(TEXT("Left/right arrows remain perpendicular horizontal axes"),Sideways);
    Test(TEXT("Page Up/Down always use world Z regardless of gaze"),Vertical);
    FBSRailPlan P; P.Id=FGuid::NewGuid(); P.Name=TEXT("Rail test");
    FBSPlanNode A; A.Id=FGuid::NewGuid(); A.Position=FVector(0,0,1000);
    auto B=A; B.Id=FGuid::NewGuid(); B.Position.X=20000;
    auto C=B; C.Id=FGuid::NewGuid(); C.Position.X=40000;
    P.Nodes={A,B,C};
    FBSPlanEdge E; E.Id=FGuid::NewGuid(); E.Start=A.Id; E.End=B.Id;
    auto E2=E; E2.Id=FGuid::NewGuid(); E2.Start=B.Id; E2.End=C.Id; P.Edges={E,E2};
    Test(TEXT("Plan graph accepts connected rails"),FBSPlanMath::Valid(P));
    const auto First=FBSPlanMath::Curve(P,E),Second=FBSPlanMath::Curve(P,E2);
    Test(TEXT("Adjacent curves share node position and tangent"),First.Last().Location==Second[0].Location && First.Last().ArriveTangent.GetSafeNormal().Equals(Second[0].LeaveTangent.GetSafeNormal()));
    const auto Offset=FBSPlanMath::Offset(First,FVector(0,600,100));
    Test(TEXT("Parallel uses local right and up in centimeters"),Offset[0].Location.Equals(A.Position+FVector(0,600,100),.01) && Offset.Last().Location.Equals(B.Position+FVector(0,600,100),.01));
    TArray<TArray<FSplinePointData>> Parts;
    Test(TEXT("Rail path divides beyond native segment length"),FBSStickPath::Split(First,10000,Parts) && Parts.Num()>=2);
    bool Joined=true; for (int32 I=1;I<Parts.Num();++I) Joined &= Parts[I-1].Last().Location.Equals(Parts[I][0].Location,.001) && Parts[I-1].Last().ArriveTangent.GetSafeNormal().Equals(Parts[I][0].LeaveTangent.GetSafeNormal(),.0001);
    Test(TEXT("Rail splits preserve continuous joins"),Joined);
    FString Error; FBSPlanCommand Command; Command.Action=EBSPlanAction::Place; Command.Node=B; Command.Node.Id=FGuid::NewGuid(); Command.Edge=E; Command.Edge.Start=C.Id;
    Test(TEXT("Placing on existing node reuses it without a duplicate edge"),FBSPlanMath::Apply(P,Command,Error) && P.Nodes.Num()==3 && P.Edges.Num()==2);
    Command={}; Command.Action=EBSPlanAction::Move; Command.Revision=P.Revision; Command.Selection={E.Id,E2.Id,B.Id}; Command.Delta=FVector(0,0,500);
    Test(TEXT("Selected connected edges move their common node only once"),FBSPlanMath::Apply(P,Command,Error) && P.Nodes[1].Position.Z==1500);
    Test(TEXT("Stale shared-plan edit is rejected"),!FBSPlanMath::Apply(P,Command,Error));
    E.Trace=EBSPlanTrace::Free; E.Controls={FVector(5000,5000,1500)};
    auto Free=FBSPlanMath::Curve(P,E);
    Test(TEXT("Free curve owns only interior controls and keeps node endpoints"),Free.Num()==3 && Free[0].Location==P.Nodes[0].Position && Free.Last().Location==P.Nodes[1].Position && Free[1].Location==E.Controls[0]);
    auto BezierPlan=P; auto BezierEdge=P.Edges[0]; BezierEdge.Trace=EBSPlanTrace::Bezier;
    BezierEdge.Controls={P.Nodes[0].Position+FVector(0,10000,0),P.Nodes[1].Position+FVector(0,10000,0)};
    FBSPlanCommand BezierCommand; BezierCommand.Action=EBSPlanAction::Edge; BezierCommand.Revision=BezierPlan.Revision; BezierCommand.Edge=BezierEdge;
    Test(TEXT("Bezier handles can be configured separately from through-points"),FBSPlanMath::Apply(BezierPlan,BezierCommand,Error));
    const auto Bezier=FBSPlanMath::Curve(BezierPlan,BezierPlan.Edges[0]);
    const FVector ExpectedMid=(P.Nodes[0].Position+P.Nodes[1].Position)*.5+FVector(0,7500,0);
    Test(TEXT("Cubic Bezier pulls toward handles without passing through them"),Bezier.Num()==2 && FBSPlanMath::Point(Bezier[0],Bezier[1],.5).Equals(ExpectedMid,.001));
    const auto Neighbor=FBSPlanMath::Curve(BezierPlan,BezierPlan.Edges[1]);
    Test(TEXT("Bezier edits preserve the adjacent node tangent axis"),FMath::Abs(FVector::DotProduct(Bezier.Last().ArriveTangent.GetSafeNormal(),Neighbor[0].LeaveTangent.GetSafeNormal()))>.9999);
    BezierCommand={}; BezierCommand.Action=EBSPlanAction::Move; BezierCommand.Revision=BezierPlan.Revision; BezierCommand.Selection={A.Id}; BezierCommand.Delta=FVector(100,0,0);
    Test(TEXT("Moving a Bezier endpoint carries only its own handle"),FBSPlanMath::Apply(BezierPlan,BezierCommand,Error) && BezierPlan.Edges[0].Controls[0]==BezierEdge.Controls[0]+BezierCommand.Delta && BezierPlan.Edges[0].Controls[1]==BezierEdge.Controls[1]);
    auto InvalidBezier=BezierPlan; InvalidBezier.Edges[0].Controls.Pop();
    Test(TEXT("Malformed Bezier data with a missing handle is rejected"),!FBSPlanMath::Valid(InvalidBezier));
    Test(TEXT("Straight rail is shape-valid"),FBSPlanMath::ShapeValid(First[0],First[1],.5,2000,35));
    FSplinePointData SharpA(FVector::ZeroVector,FVector(100,0,0)), SharpB(FVector(100,100,0),FVector(0,100,0));
    Test(TEXT("Tight rail corner is rejected locally"),!FBSPlanMath::ShapeValid(SharpA,SharpB,.5,2000,35));
    FSplinePointData SteepA(FVector::ZeroVector,FVector(100,0,1000)),SteepB(FVector(100,0,1000),FVector(100,0,1000));
    Test(TEXT("Excessive grade is rejected locally"),!FBSPlanMath::ShapeValid(SteepA,SteepB,.5,2000,35));
    P.Edges[0]=E; P.Parallels={FVector(0,600,100)};
    const auto BeforeDelete=P;
    Command={}; Command.Action=EBSPlanAction::Delete; Command.Revision=P.Revision; Command.Selection={B.Id};
    Test(TEXT("Removing a node removes its incident splines"),FBSPlanMath::Apply(P,Command,Error) && P.Nodes.Num()==2 && P.Edges.IsEmpty());
    FBSPlanHistory History;
    History.Record(BeforeDelete,P,2);
    Test(TEXT("Undo deletion restores incident free curves, controls and parallel offsets"),History.Restore(P,false,P.Revision,2,Error) && P.Nodes.Num()==3 && P.Edges.Num()==2 && P.Edges[0].Controls==E.Controls && P.Parallels==BeforeDelete.Parallels);
    Test(TEXT("Redo deletion removes the same incident curves atomically"),History.Restore(P,true,P.Revision,2,Error) && P.Nodes.Num()==2 && P.Edges.IsEmpty());
    History={};
    auto Before=P; Command={}; Command.Action=EBSPlanAction::Move; Command.Revision=P.Revision; Command.Selection={A.Id}; Command.Delta=FVector(100,0,0);
    FBSPlanMath::Apply(P,Command,Error); History.Record(Before,P,2);
    P.Shared=true; P.Name=TEXT("Current shared name"); const int32 Latest=P.Revision;
    Test(TEXT("Undo rejects stale shared revisions"),!History.Restore(P,false,Latest-1,2,Error) && History.Undo.Num()==1);
    Test(TEXT("Undo restores geometry and preserves current sharing and name"),History.Restore(P,false,P.Revision,2,Error) && P.Nodes[0].Position==Before.Nodes[0].Position && P.Shared && P.Name==TEXT("Current shared name") && P.Revision>Latest);
    Test(TEXT("Redo reapplies geometry with a new revision"),History.Restore(P,true,P.Revision,2,Error) && P.Nodes[0].Position==Before.Nodes[0].Position+FVector(100,0,0));
    History.Restore(P,false,P.Revision,2,Error); Before=P; Command.Revision=P.Revision; Command.Delta=FVector(0,100,0); FBSPlanMath::Apply(P,Command,Error); History.Record(Before,P,2);
    Test(TEXT("New geometry edits discard redo history"),History.Redo.IsEmpty());
    for (int32 I=0;I<4;++I) { Before=P; Command.Revision=P.Revision; FBSPlanMath::Apply(P,Command,Error); History.Record(Before,P,2); }
    Test(TEXT("Plan history is limited by edit actions"),History.Undo.Num()==2);
    Test(TEXT("Zero history setting disables undo"),!History.Restore(P,false,P.Revision,0,Error) && History.Undo.IsEmpty() && History.Redo.IsEmpty());

    for (int32 Type=0;Type<4;++Type)
    {
        FBSRailPlan Shape; Shape.Id=FGuid::NewGuid(); Shape.Name=TEXT("Split test"); Shape.Nodes={A,B};
        Shape.Nodes[0].Direction=FRotator(5,25,0).Vector(); Shape.Nodes[1].Direction=FRotator(-5,-25,0).Vector();
        FBSPlanEdge Link; Link.Id=FGuid::NewGuid(); Link.Start=A.Id; Link.End=B.Id; Link.Trace=EBSPlanTrace(Type);
        if (Type==2) Link.Controls={FVector(10000,3000,1800)};
        if (Type==3) Link.Controls={A.Position+FVector(5000,4000,500),B.Position+FVector(-5000,4000,500)};
        Shape.Edges={Link}; const auto BeforeSplit=Shape; const auto Curve=FBSPlanMath::Curve(Shape,Link);
        FBSPlanCommand Split; Split.Action=EBSPlanAction::Place; Split.Node.Id=FGuid::NewGuid(); Split.SplitEdge=Link.Id; Split.SplitSegment=0; Split.SplitTime=.37;
        Split.Node.Position=FBSPlanMath::Point(Curve[0],Curve[1],Split.SplitTime);
        int32 Segment=-1; double Time=0; FVector At;
        Test(TEXT("Spline projection finds the exact insertion position"),FBSPlanMath::Closest(Curve,Split.Node.Position,Segment,Time,At) && Segment==0 && At.Equals(Split.Node.Position,.01) && FMath::Abs(Time-.37)<.00001);
        const bool SplitOK=FBSPlanMath::Apply(Shape,Split,Error) && Shape.Nodes.Num()==3 && Shape.Edges.Num()==2;
        Test(TEXT("All trace types split into two links and one shared node"),SplitOK);
        if (SplitOK)
        {
            const auto Left=FBSPlanMath::Curve(Shape,Shape.Edges[0]),Right=FBSPlanMath::Curve(Shape,Shape.Edges[1]);
            bool Exact=Left.Num()==2 && Right.Num()==Curve.Num();
            for (int32 I=0;I<=20 && Exact;++I)
            {
                const double U=I/20.0;
                Exact &= FBSPlanMath::Point(Left[0],Left[1],U).Equals(FBSPlanMath::Point(Curve[0],Curve[1],U*.37),.001);
                Exact &= FBSPlanMath::Point(Right[0],Right[1],U).Equals(FBSPlanMath::Point(Curve[0],Curve[1],.37+U*.63),.001);
            }
            Test(TEXT("Splitting preserves exact geometry without straightening the curve"),Exact);
            const auto* Inserted=FBSPlanMath::Node(Shape,Split.Node.Id);
            Test(TEXT("Inserted node adopts the spline tangent for continuous branches"),Inserted && Inserted->Direction.Equals(FBSPlanMath::Tangent(Curve[0],Curve[1],.37).GetSafeNormal(),.00001));
            FBSPlanHistory H; H.Record(BeforeSplit,Shape,10);
            Test(TEXT("Split undo and redo restore the complete graph atomically"),H.Restore(Shape,false,Shape.Revision,10,Error) && Shape.Nodes.Num()==2 && Shape.Edges.Num()==1 && H.Restore(Shape,true,Shape.Revision,10,Error) && Shape.Nodes.Num()==3 && Shape.Edges.Num()==2);
        }
        auto Bad=BeforeSplit; Split.Node.Position+=FVector(100,0,0);
        Test(TEXT("Server rejects a split position that does not lie on its claimed spline"),!FBSPlanMath::Apply(Bad,Split,Error) && Bad.Nodes.Num()==2 && Bad.Edges.Num()==1);
    }
    FBSRailPlan Oriented; Oriented.Id=FGuid::NewGuid(); Oriented.Name=TEXT("Orientation"); Oriented.Nodes={A,B,C};
    FBSPlanEdge In; In.Id=FGuid::NewGuid(); In.Start=A.Id; In.End=B.Id; In.Trace=EBSPlanTrace::Bezier; In.Controls={A.Position+FVector(6000,0,0),B.Position-FVector(6000,0,0)};
    FBSPlanEdge Out; Out.Id=FGuid::NewGuid(); Out.Start=B.Id; Out.End=C.Id; Oriented.Edges={In,Out};
    FBSPlanCommand Rotate; Rotate.Action=EBSPlanAction::Node; Rotate.Node=B; Rotate.Node.Direction=FRotator(10,30,0).Vector(); Rotate.Node.Roll=25;
    Test(TEXT("Node yaw pitch and roll are editable without moving its origin"),FBSPlanMath::Apply(Oriented,Rotate,Error) && Oriented.Nodes[1].Position==B.Position && FBSPlanMath::Rotation(Oriented.Nodes[1]).Equals(FRotator(10,30,25),.001));
    const auto InCurve=FBSPlanMath::Curve(Oriented,Oriented.Edges[0]),OutCurve=FBSPlanMath::Curve(Oriented,Oriented.Edges[1]);
    Test(TEXT("Rotating a shared node aligns Bezier and automatic adjoining tangents"),FMath::Abs(FVector::DotProduct(InCurve.Last().ArriveTangent.GetSafeNormal(),OutCurve[0].LeaveTangent.GetSafeNormal()))>.9999);
    Out.Trace=EBSPlanTrace::Free; Out.Controls={(B.Position+C.Position)*.5}; Out.ControlDirections={FVector(0,1,0)};
    FBSPlanCommand TangentEdit; TangentEdit.Action=EBSPlanAction::Edge; TangentEdit.Revision=Oriented.Revision; TangentEdit.Edge=Out;
    Test(TEXT("Through-points accept independent tangent directions"),FBSPlanMath::Apply(Oriented,TangentEdit,Error) && FBSPlanMath::Curve(Oriented,Oriented.Edges[1])[1].LeaveTangent.GetSafeNormal().Equals(FVector(0,1,0)));
    const auto AtControl=FBSPlanMath::Curve(Oriented,Oriented.Edges[1]);
    FBSPlanCommand Boundary; Boundary.Action=EBSPlanAction::Place; Boundary.Revision=Oriented.Revision; Boundary.Node.Id=FGuid::NewGuid(); Boundary.Node.Position=AtControl[1].Location; Boundary.SplitEdge=Out.Id; Boundary.SplitSegment=0; Boundary.SplitTime=1;
    Test(TEXT("Splitting exactly at a through-point creates no zero-length span"),FBSPlanMath::Apply(Oriented,Boundary,Error) && FBSPlanMath::Curve(Oriented,Oriented.Edges[1]).Num()==2 && FBSPlanMath::Curve(Oriented,Oriented.Edges.Last()).Num()==2);
    const auto Rolled=FBSPlanMath::Offset(First,FVector(0,600,0),90,90);
    Test(TEXT("Node roll rotates the parallel-line frame around its tangent"),Rolled[0].Location.Equals(First[0].Location+FVector(0,0,600),.01));
    auto Extension=BeforeDelete; Extension.Edges.SetNum(1); Extension.Edges[0].Trace=EBSPlanTrace::Straight;
    Extension.Edges[0].Controls.Reset(); Extension.Edges[0].Route.Reset(); Extension.Nodes.SetNum(2);
    Extension.Nodes[0].Position=FVector(0,0,1000); Extension.Nodes[1].Position=FVector(0,20000,1000);
    Extension.Nodes[0].Direction=Extension.Nodes[1].Direction=FVector::ForwardVector;
    const FVector NetworkDirection=FBSPlanMath::ConnectionDirection(Extension,Extension.Nodes[1].Id);
    Test(TEXT("Extension reads the actual straight spline tangent instead of a stale node axis"),NetworkDirection.Equals(FVector::RightVector,.0001));
    FBSPlanCommand Add; Add.Action=EBSPlanAction::Place; Add.Revision=Extension.Revision;
    Add.Node.Id=FGuid::NewGuid(); Add.Node.Position=FVector(10000,40000,1000); Add.Node.Direction=NetworkDirection;
    Add.Edge.Id=FGuid::NewGuid(); Add.Edge.Start=Extension.Nodes[1].Id; Add.Edge.End=Add.Node.Id;
    const bool Added=FBSPlanMath::Apply(Extension,Add,Error);
    const auto Extended=Added ? FBSPlanMath::Curve(Extension,Extension.Edges.Last()) : TArray<FSplinePointData>();
    Test(TEXT("Off-axis extensions preserve the existing tangent at both endpoints"),Added && Extended.Num()==2 && Extended[0].LeaveTangent.GetSafeNormal().Equals(NetworkDirection,.0001) && Extended.Last().ArriveTangent.GetSafeNormal().Equals(NetworkDirection,.0001));
    auto Captured=Extension; Captured.Edges.SetNum(1); Captured.Edges[0].Trace=EBSPlanTrace::Curve;
    Captured.Edges[0].Route.Reset(); Captured.Edges[0].Route.Emplace(FSplinePointData(Captured.Nodes[0].Position,FVector(0,10000,0))); Captured.Edges[0].Route.Emplace(FSplinePointData(Captured.Nodes[1].Position,FVector(5000,10000,0)));
    Test(TEXT("Captured route extensions inherit the real endpoint tangent"),FBSPlanMath::ConnectionDirection(Captured,Captured.Nodes[1].Id).Equals(FVector(5000,10000,0).GetSafeNormal(),.0001));
    const auto Once=Captured.Edges[0].Route; Captured.Edges[0].Route.Append(Once);
    Test(TEXT("Previously duplicated constrained routes are evaluated once without losing the endpoint"),FBSPlanMath::Curve(Captured,Captured.Edges[0]).Num()==Once.Num() && FBSPlanMath::Curve(Captured,Captured.Edges[0]).Last().Location==Once.Last().Location);
    FBSPlanCommand Rename; Rename.Action=EBSPlanAction::Rename; Rename.Revision=Captured.Revision; Rename.Name=TEXT("Normalized route");
    Test(TEXT("Editing a duplicated captured route stores one continuous copy"),FBSPlanMath::Apply(Captured,Rename,Error) && Captured.Edges[0].Route.Num()==Once.Num());
    FBSRailPlan Chain; Chain.Id=FGuid::NewGuid(); Chain.Name=TEXT("Segment paths");
    for (int32 I=0;I<3;++I) { FBSPlanNode N; N.Id=FGuid::NewGuid(); N.Position=FVector(I*7500,0,1000); Chain.Nodes.Add(N); }
    for (int32 I=0;I<2;++I) { FBSPlanEdge Link; Link.Id=FGuid::NewGuid(); Link.Start=Chain.Nodes[I].Id; Link.End=Chain.Nodes[I+1].Id; Link.Trace=EBSPlanTrace::Straight; Chain.Edges.Add(Link); }
    // Reverse the stored second edge to exercise traversal independent of placement order.
    Swap(Chain.Edges[1].Start,Chain.Edges[1].End);
    TArray<FBSPlanPath> Paths; Parts.Reset();
    Test(TEXT("Degree-two nodes remain inside one connected path, including reversed edges"),FBSPlanSegments::Paths(Chain,Paths,Error) && Paths.Num()==1 && Paths[0].Points.Num()==3 && Paths[0].Start==Chain.Nodes[0].Id && Paths[0].End==Chain.Nodes[2].Id);
    if (Paths.Num()==1)
    {
        Test(TEXT("Requested length crosses intermediate plan nodes: 100m then 50m"),FBSPlanSegments::Split(Paths[0].Points,10000,EBSPlanRemainder::Last,100,10000,Parts,Error) && Parts.Num()==2 && Parts[0].Last().Location.Equals(FVector(10000,0,1000),.05) && Parts[0].Num()==3);
        Test(TEXT("Reduce evenly divides the whole 150m path into 75m rails"),FBSPlanSegments::Split(Paths[0].Points,10000,EBSPlanRemainder::Reduce,100,10000,Parts,Error) && Parts.Num()==2 && Parts[0].Last().Location.Equals(FVector(7500,0,1000),.05));
        Test(TEXT("Increase evenly rejects a result above the native maximum"),!FBSPlanSegments::Split(Paths[0].Points,10000,EBSPlanRemainder::Increase,100,10000,Parts,Error) && Parts.IsEmpty());
        Test(TEXT("Increase evenly can enlarge two 70m targets to 75m"),FBSPlanSegments::Split(Paths[0].Points,7000,EBSPlanRemainder::Increase,100,10000,Parts,Error) && Parts.Num()==2 && Parts[0].Last().Location.Equals(FVector(7500,0,1000),.05));
        Test(TEXT("Exact multiples do not leave a zero-length final piece"),FBSPlanSegments::Split(Paths[0].Points,7500,EBSPlanRemainder::Last,100,10000,Parts,Error) && Parts.Num()==2);
        Test(TEXT("Short remainder extends only the preceding segment"),FBSPlanSegments::Split(Paths[0].Points,7499,EBSPlanRemainder::Last,100,10000,Parts,Error) && Parts.Num()==2 && Parts[0].Last().Location.Equals(FVector(7499,0,1000),.05));
        Test(TEXT("Oversize combined tail splits into two equal segments"),FBSPlanSegments::Split(Paths[0].Points,7499,EBSPlanRemainder::Last,100,7500,Parts,Error) && Parts.Num()==3 && Parts[1].Last().Location.Equals(FVector(11249.5,0,1000),.05));
        Test(TEXT("Path below the native minimum is rejected"),!FBSPlanSegments::Split(Paths[0].Points,16000,EBSPlanRemainder::Last,15500,20000,Parts,Error) && Parts.IsEmpty());
        Test(TEXT("Requested segment length below minimum is rejected"),!FBSPlanSegments::Split(Paths[0].Points,99,EBSPlanRemainder::Last,100,10000,Parts,Error));
        Test(TEXT("Increase evenly cannot create a target longer than the entire path"),!FBSPlanSegments::Split(Paths[0].Points,16000,EBSPlanRemainder::Increase,100,20000,Parts,Error));
    }
    FBSPlanNode Branch; Branch.Id=FGuid::NewGuid(); Branch.Position=FVector(15000,5000,1000); Chain.Nodes.Add(Branch);
    FBSPlanEdge BranchEdge; BranchEdge.Id=FGuid::NewGuid(); BranchEdge.Start=Chain.Nodes[1].Id; BranchEdge.End=Branch.Id; Chain.Edges.Add(BranchEdge);
    bool BranchEnds=FBSPlanSegments::Paths(Chain,Paths,Error) && Paths.Num()==3;
    for (const auto& Path:Paths) BranchEnds &= Path.Start==Chain.Nodes[1].Id || Path.End==Chain.Nodes[1].Id;
    Test(TEXT("Every diverging path terminates a segment at the junction"),BranchEnds);
    Chain.Edges.Pop(); Chain.Nodes.Pop();
    FBSPlanEdge Closing; Closing.Id=FGuid::NewGuid(); Closing.Start=Chain.Nodes[2].Id; Closing.End=Chain.Nodes[0].Id;
    Closing.Trace=EBSPlanTrace::Bezier; Closing.Controls={FVector(25000,0,1000),FVector(-10000,0,1000)};
    Chain.Edges[0].Trace=Chain.Edges[1].Trace=EBSPlanTrace::Curve; Chain.Edges.Add(Closing);
    Test(TEXT("A continuous closed loop is traversed exactly once"),FBSPlanSegments::Paths(Chain,Paths,Error) && Paths.Num()==1 && Paths[0].Points.Num()==4 && Paths[0].Start==Paths[0].End);
    // Settings are saved with the graph and restored alongside geometry.
    auto BeforeSettings=Chain; FBSPlanCommand Settings; Settings.Action=EBSPlanAction::Segments; Settings.SegmentLength=7000; Settings.Remainder=EBSPlanRemainder::Increase;
    Test(TEXT("Segment settings apply atomically"),FBSPlanMath::Apply(Chain,Settings,Error) && Chain.SegmentLength==7000 && Chain.Remainder==EBSPlanRemainder::Increase);
    FBSPlanHistory SettingsHistory; SettingsHistory.Record(BeforeSettings,Chain,10);
    Test(TEXT("Undo and redo restore segment length and remainder policy"),SettingsHistory.Restore(Chain,false,Chain.Revision,10,Error) && Chain.SegmentLength==8000 && Chain.Remainder==EBSPlanRemainder::Reduce && SettingsHistory.Restore(Chain,true,Chain.Revision,10,Error) && Chain.SegmentLength==7000 && Chain.Remainder==EBSPlanRemainder::Increase);
    Settings.Revision=Chain.Revision; Settings.SegmentLength=-1;
    Test(TEXT("Invalid segment settings leave the plan unchanged"),!FBSPlanMath::Apply(Chain,Settings,Error) && Chain.SegmentLength==7000);
    const auto GeometryBefore=Chain.Edges; FBSPlanCommand Reverse; Reverse.Action=EBSPlanAction::ReverseDirection; Reverse.Revision=Chain.Revision; Reverse.Selection={Chain.Edges[0].Id};
    Test(TEXT("Reverse signal direction propagates across the selected connected path"),FBSPlanMath::Apply(Chain,Reverse,Error) && FBSPlanSegments::Paths(Chain,Paths,Error) && Paths.Num()==1 && Paths[0].SignalReverse && Chain.ReversedEdges.Num()==3);
    Test(TEXT("Signal reversal keeps node and spline geometry unchanged"),Chain.Edges[0].Start==GeometryBefore[0].Start && Chain.Edges[0].End==GeometryBefore[0].End && Chain.Edges.Last().Controls==GeometryBefore.Last().Controls);
    Chain.Parallels={FVector(0,600,0)}; Chain.ParallelReversed={true};
    Test(TEXT("Parallel direction can oppose its primary path without changing its offset"),FBSPlanSegments::Paths(Chain,Paths,Error,false) && Paths.Num()==2 && Paths[0].SignalReverse && !Paths[1].SignalReverse);
    Settings.Revision=Chain.Revision; Settings.SegmentLength=7000; Settings.PlaceBlockSignal=true; Settings.SignalEvery=8; Settings.LeftSideSignal=true;
    Test(TEXT("Build specification retains signal spacing and left-side choice"),FBSPlanMath::Apply(Chain,Settings,Error) && Chain.PlaceBlockSignal && Chain.SignalEvery==8 && Chain.LeftSideSignal);
    Settings.Revision=Chain.Revision; Settings.SignalEvery=0;
    Test(TEXT("A zero signal interval is rejected without changing the specification"),!FBSPlanMath::Apply(Chain,Settings,Error) && Chain.SignalEvery==8);
    // Exact signed tangents, including endpoints behind the existing tangent.
    // Comparing absolute dot products misses a 180-degree cusp.
    for (bool AtStart:{false,true}) for (bool Behind:{false,true})
    {
        FBSRailPlan Network; Network.Id=FGuid::NewGuid(); Network.Name=TEXT("Signed extension");
        auto Left=A,Right=B; Left.Position=FVector::ZeroVector; Right.Position=FVector(10000,10000,0);
        Left.Direction=FVector::ForwardVector; Right.Direction=FVector::RightVector;
        FBSPlanEdge Existing; Existing.Id=FGuid::NewGuid(); Existing.Start=Left.Id; Existing.End=Right.Id;
        Existing.Route={FBSPlanPoint(FSplinePointData(Left.Position,FVector(12000,0,0))),FBSPlanPoint(FSplinePointData(Right.Position,FVector(0,12000,0)))};
        Network.Nodes={Left,Right}; Network.Edges={Existing};
        const auto Base=AtStart ? Left : Right; const FVector ExpectedTangent=AtStart ? -FVector::ForwardVector : FVector::RightVector;
        FBSPlanCommand Extend; Extend.Action=EBSPlanAction::Place; Extend.Node=Base; Extend.Node.Id=FGuid::NewGuid();
        Extend.Node.Position=Base.Position+ExpectedTangent*(Behind ? -10000 : 10000)+FVector(0,0,1000);
        Extend.Edge.Id=FGuid::NewGuid(); Extend.Edge.Start=Base.Id;
        Test(TEXT("Extending either terminal preserves signed tangent even behind it"),FBSPlanMath::Apply(Network,Extend,Error) &&
            FVector::DotProduct(FBSPlanMath::Curve(Network,Network.Edges.Last())[0].LeaveTangent.GetSafeNormal(),ExpectedTangent)>.99999);
        FBSPlanCommand MoveEnd; MoveEnd.Action=EBSPlanAction::Move; MoveEnd.Revision=Network.Revision; MoveEnd.Selection={Extend.Node.Id}; MoveEnd.Delta=FVector(1000,1000,0);
        Test(TEXT("Moving an extension retains its signed start continuity"),FBSPlanMath::Apply(Network,MoveEnd,Error) &&
            FVector::DotProduct(FBSPlanMath::Curve(Network,Network.Edges.Last())[0].LeaveTangent.GetSafeNormal(),ExpectedTangent)>.99999);
    }
    Test(TEXT("Nodes retain geometry without a saved rail dependency"),!FindFProperty<FProperty>(FBSPlanNode::StaticStruct(),TEXT("Rail")));
    return Passed;
}
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSPlanMathTest,"BuildingSkills.Plan.GraphAndGeometry",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSPlanMathTest::RunTest(const FString&)
{ return RunBuildingSkillsPlanMathChecks([this](const TCHAR* Name,bool Value) { TestTrue(Name,Value); }); }
#endif
