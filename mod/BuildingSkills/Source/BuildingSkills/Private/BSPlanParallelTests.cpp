#include "BSPlan.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace
{
    USplineComponent* MeasureParallelTest(const TArray<FSplinePointData>& Points)
    {
        auto* S=NewObject<USplineComponent>(); S->ReparamStepsPerSegment=128; S->ClearSplinePoints(false);
        for (int32 I=0;I<Points.Num();++I) S->AddPoint(FSplinePoint(I,Points[I].Location,Points[I].ArriveTangent,
            Points[I].LeaveTangent,FRotator::ZeroRotator,FVector::OneVector,ESplinePointType::CurveCustomTangent),false);
        S->UpdateSpline(); return S;
    }
    FBSRailPlan TurnPlan(bool Loop=false)
    {
        FBSRailPlan P; P.Id=FGuid::NewGuid(); P.Name=TEXT("Synchronized turns"); P.SyncParallelsInTurns=true;
        const FVector Positions[]={FVector(0,0,0),FVector(20000,20000,0),FVector(0,40000,0),FVector(-20000,20000,0)};
        const FVector Directions[]={FVector(1,0,0),FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0)};
        for (int32 I=0;I<(Loop ? 4 : 2);++I)
        { auto& N=P.Nodes.AddDefaulted_GetRef(); N.Id=FGuid::NewGuid(); N.Position=Positions[I]; N.Direction=Directions[I]; }
        for (int32 I=0;I<(Loop ? 4 : 1);++I)
        {
            const auto& A=P.Nodes[I]; const auto& B=P.Nodes[(I+1)%P.Nodes.Num()];
            auto& E=P.Edges.AddDefaulted_GetRef(); E.Id=FGuid::NewGuid(); E.Start=A.Id; E.End=B.Id; E.Trace=EBSPlanTrace::Bezier;
            E.Controls={A.Position+A.Direction*(20000*.5522847498),B.Position-B.Direction*(20000*.5522847498)};
        }
        P.Parallels={FVector(0,4000,0),FVector(0,-4000,0)};
        return P;
    }
}

bool RunBuildingSkillsPlanParallelChecks(TFunctionRef<void(const TCHAR*,bool)> Check)
{
    bool Passed=true; auto Test=[&](const TCHAR* Name,bool Value) { Check(Name,Value); Passed &= Value; };
    FString Error; auto Plan=TurnPlan(); TArray<FBSPlanSegmentedPath> Built;
    Test(TEXT("Existing plans default to independent parallel segmentation"),!FBSRailPlan{}.SyncParallelsInTurns);
    auto WithinLimits=[&](double Min,double Max) {
        for (const auto& Path:Built) for (const auto& Part:Path.Parts)
        { const double Length=MeasureParallelTest(Part)->GetSplineLength(); if (Length<Min-.5 || Length>Max+.5) return false; }
        return !Built.IsEmpty();
    };
    auto Aligned=[&]() {
        if (Built.Num()!=3 || Built[0].Parts.IsEmpty()) return false;
        for (int32 P=1;P<Built.Num();++P)
        {
            if (Built[P].Parts.Num()!=Built[0].Parts.Num()) return false;
            for (int32 I=0;I<Built[0].Parts.Num();++I)
            {
                const auto& A=Built[0].Parts[I].Last(); const auto& B=Built[P].Parts[I].Last();
                if (FMath::Abs(FVector::DotProduct(B.Location-A.Location,A.ArriveTangent.GetSafeNormal()))>1) return false;
            }
        }
        return true;
    };
    Test(TEXT("Inner and outer turn joints share main-curve cross-sections"),FBSPlanSegments::Prepare(Plan,100,10000,Built,Error) && Aligned() && WithinLimits(100,10000));
    const int32 InitialCount=Built.IsEmpty() ? 0 : Built[0].Parts.Num();
    Test(TEXT("Outer rail maximum shortens main spacing and retains alignment"),FBSPlanSegments::Prepare(Plan,100,8000,Built,Error) &&
        Aligned() && Built[0].Parts.Num()>InitialCount && WithinLimits(100,8000));
    for (auto Policy:{EBSPlanRemainder::Last,EBSPlanRemainder::Increase})
    {
        auto PolicyPlan=Plan; PolicyPlan.Remainder=Policy; PolicyPlan.SegmentLength=6000;
        Test(TEXT("Last and Increase remainder policies synchronize within the maximum"),FBSPlanSegments::Prepare(PolicyPlan,100,7000,Built,Error) && Aligned() && WithinLimits(100,7000));
    }
    Plan.SyncParallelsInTurns=false;
    Test(TEXT("Disabling synchronization retains independent length-based splits"),FBSPlanSegments::Prepare(Plan,100,10000,Built,Error) && !Aligned() && WithinLimits(100,10000));
    Plan.SyncParallelsInTurns=true;
    for (double Offset:{400.0,-400.0,-50.0})
    {
        Plan.ParallelStartOffsets={Offset,Offset};
        const bool Ready=FBSPlanSegments::Prepare(Plan,100,8000,Built,Error);
        bool Shifted=Ready && Built.Num()==3 && WithinLimits(100,8000);
        if (Shifted)
        {
            auto* Main=MeasureParallelTest(Built[0].Path.Points);
            const double FirstJoint=MeasureParallelTest(Built[0].Parts[0])->GetSplineLength()+Offset;
            const auto At=Main->GetLocationAtDistanceAlongSpline(FirstJoint,ESplineCoordinateSpace::Local);
            const auto Direction=Main->GetDirectionAtDistanceAlongSpline(FirstJoint,ESplineCoordinateSpace::Local);
            for (int32 I=1;I<Built.Num();++I)
                Shifted &= FMath::Abs(FVector::DotProduct(Built[I].Parts[0].Last().Location-At,Direction))<1;
            TArray<FBSPlanPath> Preview;
            Shifted &= FBSPlanSegments::Paths(Plan,Preview,Error) && Preview.Num()==Built.Num();
            if (Preview.Num()==Built.Num()) for (int32 I=1;I<Built.Num();++I)
                Shifted &= Preview[I].Points[0].Location.Equals(Built[I].Parts[0][0].Location,1);
            if (Offset<0) Shifted &= Built[1].Parts[0][0].Location.Equals(FVector(Offset,4000,0),.01);
        }
        Test(TEXT("Signed offsets stagger turn joints in main-path distance and match the preview"),Shifted);
    }
    Plan.ParallelStartOffsets={400,-25000};
    Test(TEXT("Large negative stagger receives extra joints through the entire extension"),FBSPlanSegments::Prepare(Plan,100,8000,Built,Error) &&
        Built.Num()==3 && WithinLimits(100,8000) && Built[2].Parts[0][0].Location.Equals(FVector(-25000,-4000,0),.01));
    Plan=TurnPlan(true);
    bool Loop=FBSPlanSegments::Prepare(Plan,100,8000,Built,Error) && Aligned() && WithinLimits(100,8000);
    for (const auto& Path:Built) Loop &= Path.Path.Start==Path.Path.End && Path.Parts[0][0].Location.Equals(Path.Parts.Last().Last().Location,.01);
    Test(TEXT("Closed loops synchronize once and close every parallel"),Loop);

    Plan=TurnPlan();
    auto ExtraNode=Plan.Nodes.Last(); ExtraNode.Id=FGuid::NewGuid(); ExtraNode.Position+=FVector(0,10000,0);
    auto ExtraEdge=Plan.Edges[0]; ExtraEdge.Id=FGuid::NewGuid(); ExtraEdge.Start=ExtraNode.Id; ExtraEdge.End=Plan.Nodes.Last().Id;
    ExtraEdge.Trace=EBSPlanTrace::Straight; ExtraEdge.Controls.Reset(); Plan.Nodes.Add(ExtraNode); Plan.Edges.Add(ExtraEdge);
    Test(TEXT("Unequal curve sampling and reversed stored spans retain joint correspondence"),FBSPlanSegments::Prepare(Plan,100,8000,Built,Error) && Aligned() && WithinLimits(100,8000));
    Plan.ParallelNodeOffsets={200,-150};
    Test(TEXT("Node offsets participate in the synchronized length checks"),FBSPlanSegments::Prepare(Plan,100,8000,Built,Error) && WithinLimits(100,8000) &&
        Built[1].Parts[0][0].Location.Equals(FVector(200,4000,0),.01));

    FBSRailPlan Chain; Chain.Id=FGuid::NewGuid(); Chain.Name=TEXT("Synchronized junctions"); Chain.SyncParallelsInTurns=true;
    Chain.Parallels={FVector(0,600,0)};
    for (int32 I=0;I<3;++I) { auto& N=Chain.Nodes.AddDefaulted_GetRef(); N.Id=FGuid::NewGuid(); N.Position=FVector(I*7500,0,0); }
    for (int32 I=0;I<2;++I) { auto& E=Chain.Edges.AddDefaulted_GetRef(); E.Id=FGuid::NewGuid(); E.Start=Chain.Nodes[I].Id; E.End=Chain.Nodes[I+1].Id; E.Trace=EBSPlanTrace::Straight; }
    Swap(Chain.Edges[1].Start,Chain.Edges[1].End);
    bool Junction=FBSPlanSegments::Prepare(Chain,100,10000,Built,Error,{Chain.Nodes[1].Id}) && Built.Num()==2;
    for (const auto& Path:Built) Junction &= Path.Parts.Num()==2 && FMath::Abs(Path.Parts[0].Last().Location.X-7500)<.01;
    Test(TEXT("Live intersections remain mandatory joints across reversed stored edges"),Junction);
    auto Branch=Chain.Nodes[1]; Branch.Id=FGuid::NewGuid(); Branch.Position.Y=10000; Chain.Nodes.Add(Branch);
    auto Link=Chain.Edges[0]; Link.Id=FGuid::NewGuid(); Link.Start=Chain.Nodes[1].Id; Link.End=Branch.Id; Chain.Edges.Add(Link);
    bool Branches=FBSPlanSegments::Prepare(Chain,100,10000,Built,Error) && Built.Num()==6;
    for (const auto& Path:Built) Branches &= Path.Path.Start==Chain.Nodes[1].Id || Path.Path.End==Chain.Nodes[1].Id;
    Test(TEXT("Branches synchronize separately and preserve their junction endpoints"),Branches);
    Plan=TurnPlan(); Plan.ParallelStartOffsets={40000};
    Test(TEXT("Offsets consuming the main path fail atomically"),!FBSPlanSegments::Prepare(Plan,100,8000,Built,Error) && Built.IsEmpty());

    Plan=TurnPlan(); Plan.SyncParallelsInTurns=false; const auto Before=Plan;
    FBSPlanCommand C; C.Action=EBSPlanAction::Segments; C.SegmentLength=8000; C.SyncParallelsInTurns=true;
    Test(TEXT("Build specification applies synchronization"),FBSPlanMath::Apply(Plan,C,Error) && Plan.SyncParallelsInTurns);
    FBSPlanHistory History; History.Record(Before,Plan,10);
    Test(TEXT("Undo and redo restore the synchronization option"),History.Restore(Plan,false,Plan.Revision,10,Error) && !Plan.SyncParallelsInTurns &&
        History.Restore(Plan,true,Plan.Revision,10,Error) && Plan.SyncParallelsInTurns);
    TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Save(Writer,false); Save.ArIsSaveGame=true;
    FBSRailPlan::StaticStruct()->SerializeItem(Save,&Plan,nullptr);
    FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Load(Reader,true); Load.ArIsSaveGame=true;
    FBSRailPlan Restored; FBSRailPlan::StaticStruct()->SerializeItem(Load,&Restored,nullptr);
    Test(TEXT("World save retains synchronized parallel settings"),!Save.IsError() && !Load.IsError() && Restored.SyncParallelsInTurns);
    return Passed;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSPlanParallelTest,"BuildingSkills.Plan.SynchronizedParallels",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSPlanParallelTest::RunTest(const FString&)
{ return RunBuildingSkillsPlanParallelChecks([this](const TCHAR* Name,bool Value) { TestTrue(Name,Value); }); }
#endif
