#if WITH_DEV_AUTOMATION_TESTS
#include "BSTransform.h"
#include "BSGroups.h"
#include "Buildables/FGBuildable.h"
#include "FGRecipe.h"
#include "Misc/AutomationTest.h"
#include "Engine/NetSerialization.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSReplicatedTransformTest, "BuildingSkills.Transform.NativeSpawnPrecision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSReplicatedTransformTest::RunTest(const FString&)
{
    const FTransform Authority(FRotator(0, 32.715, 0), FVector(123456.749, -154732.249, 1789.049), FVector(1.234, .876, 1.111));
    FVector_NetQuantize10 Location(Authority.GetLocation()), Scale(Authority.GetScale3D());
    FRotator Rotation = Authority.Rotator();
    FBitWriter Writer(0, true); bool Success = true;
    Location.NetSerialize(Writer, nullptr, Success); Scale.NetSerialize(Writer, nullptr, Success); Rotation.NetSerialize(Writer, nullptr, Success);
    FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
    Location.NetSerialize(Reader, nullptr, Success); Scale.NetSerialize(Reader, nullptr, Success); Rotation.NetSerialize(Reader, nullptr, Success);
    FTransform Observed(Rotation, Location, Scale);
    TestFalse(TEXT("Native spawn quantization reproduces the old stale-selection failure"), Authority.Equals(Observed, .01));
    TestTrue(TEXT("Actual Unreal network serialization matches the initial building state"), FBSTransformMath::MatchesInitialReplication(Authority, Observed));
    Observed.AddToTranslation(FVector(1, 0, 0));
    TestFalse(TEXT("A real displacement is not accepted as spawn rounding"), FBSTransformMath::MatchesInitialReplication(Authority, Observed));
    for (double X : {-247199.75, -247199.74999, -247199.75001, 247199.75})
    {
        const FTransform Boundary(FRotator(0, 89.997, 0), FVector(X, -202787.666, -800.019));
        FVector_NetQuantize10 BoundaryLocation(Boundary.GetLocation());
        FRotator BoundaryRotation = Boundary.Rotator();
        FBitWriter BoundaryWriter(0, true);
        BoundaryLocation.NetSerialize(BoundaryWriter, nullptr, Success);
        BoundaryRotation.NetSerialize(BoundaryWriter, nullptr, Success);
        FBitReader BoundaryReader(BoundaryWriter.GetData(), BoundaryWriter.GetNumBits());
        BoundaryLocation.NetSerialize(BoundaryReader, nullptr, Success);
        BoundaryRotation.NetSerialize(BoundaryReader, nullptr, Success);
        const FTransform BoundaryObserved(BoundaryRotation, BoundaryLocation);
        TestTrue(*FString::Printf(TEXT("Native spawn midpoint rounding matches at X=%.5f"), X),
            FBSTransformMath::MatchesInitialReplication(Boundary, BoundaryObserved));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSTransformMathTest, "BuildingSkills.Transform.PivotAndInverse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSTransformMathTest::RunTest(const FString&)
{
    const FVector Pivot(1000, 2000, -300);
    const FTransform Original(FRotator(30, 50, 70), Pivot + FVector(100,200,300), FVector(1,2,3));
    for (int32 I = 0; I < 10; ++I)
    {
        const auto Mode = EBSTransformMode(I);
        const auto Changed = FBSTransformMath::Apply(Original, Pivot, Mode, I < 3 ? 15 : 1.1, FVector::ZeroVector);
        const auto Restored = FBSTransformMath::Apply(Changed, Pivot, Mode, I < 3 ? -15 : 1.0/1.1, FVector::ZeroVector);
        TestTrue(*FString::Printf(TEXT("%s inverse returns position, rotation and scale"), *FBSTransformMath::Name(Mode)), Restored.Equals(Original, .0001));
        if (I < 3) TestTrue(TEXT("Rotation preserves distance to pivot"), FMath::IsNearlyEqual(FVector::Distance(Changed.GetLocation(), Pivot), FVector::Distance(Original.GetLocation(), Pivot), .0001));
        else
        {
            const auto Axes = FBSTransformMath::Axes(Mode);
            TestTrue(TEXT("Scaling preserves building rotation"), Changed.GetRotation().Equals(Original.GetRotation(), .0001));
            const auto A = Changed.GetScale3D(), B = Original.GetScale3D();
            TestTrue(TEXT("Projected world stretch preserves the intended volume ratio"), FMath::IsNearlyEqual(A.X*A.Y*A.Z, B.X*B.Y*B.Z*FMath::Pow(1.1, Axes.X+Axes.Y+Axes.Z), .0001));
        }
    }
    const FTransform Rotated = FBSTransformMath::Apply(FTransform(Pivot + FVector(100,0,0)), Pivot, EBSTransformMode::RotateZ, 90, FVector::ZeroVector);
    const auto Uniform = FBSTransformMath::Apply(Original,Pivot,EBSTransformMode::ScaleXYZ,2,FVector::ZeroVector);
    TestTrue(TEXT("XYZ scales every local size component"),Uniform.GetScale3D().Equals(Original.GetScale3D()*2,.0001));
    TestTrue(TEXT("XYZ preserves rotation"),Uniform.GetRotation().Equals(Original.GetRotation(),.0001));
    TestTrue(TEXT("XYZ scales distance from shared pivot"),Uniform.GetLocation().Equals(Pivot+(Original.GetLocation()-Pivot)*2,.0001));
    TestTrue(TEXT("Z rotation uses shared pivot"), Rotated.GetLocation().Equals(Pivot + FVector(0,100,0), .0001));
    TestTrue(TEXT("Forward follows facing X"), FBSTransformMath::ViewTranslation(0,0).Equals(FVector(1,0,0),.0001));
    TestTrue(TEXT("Forward follows facing Y"), FBSTransformMath::ViewTranslation(90,0).Equals(FVector(0,1,0),.0001));
    TestTrue(TEXT("Right follows facing Y"), FBSTransformMath::ViewTranslation(90,3).Equals(FVector(-1,0,0),.0001));
    TestTrue(TEXT("Vertical is independent of facing"), FBSTransformMath::ViewTranslation(127,4).Equals(FVector::UpVector,.0001));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSFrameTest, "BuildingSkills.Transform.WorldLocalAndStretch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSFrameTest::RunTest(const FString&)
{
    const FTransform Quarter(FRotator(0,90,0), FVector(100,200,300));
    const auto World = FBSTransformMath::ApplyInFrame(Quarter, FVector::ZeroVector, FQuat::Identity, EBSTransformMode::ScaleX, 2, FVector::ZeroVector);
    TestTrue(TEXT("World X stretch changes local Y on a quarter-turned building"), World.GetScale3D().Equals(FVector(1,2,1), .0001));
    TestTrue(TEXT("World X stretch moves origins along world X"), World.GetLocation().Equals(FVector(200,200,300), .0001));
    const auto Local = FBSTransformMath::ApplyInFrame(Quarter, Quarter.GetLocation(), Quarter.GetRotation(), EBSTransformMode::ScaleX, 2, FVector::ZeroVector);
    TestTrue(TEXT("Individual local scaling keeps origin and changes local X"), Local.GetLocation().Equals(Quarter.GetLocation()) && Local.GetScale3D().Equals(FVector(2,1,1), .0001));
    for (const auto& Rotation : {FRotator(13,37,71), FRotator(0,90,0)})
        for (int32 Mode = 0; Mode < 10; ++Mode)
        {
            const FTransform Before(Rotation, FVector(30,70,-50), FVector(1,2,3));
            const auto After = FBSTransformMath::ApplyInFrame(Before, FVector(10,20,30), FQuat::Identity, EBSTransformMode(Mode), Mode < 3 ? 17 : 1.2, FVector::ZeroVector);
            const auto Restored = FBSTransformMath::ApplyInFrame(After, FVector(10,20,30), FQuat::Identity, EBSTransformMode(Mode), Mode < 3 ? -17 : 1/1.2, FVector::ZeroVector);
            TestTrue(TEXT("Frame transforms reverse on oblique and quarter-turned buildings"), Restored.Equals(Before, .0001));
        }
    const TArray<FSplinePointData> Points = {FSplinePointData(FVector(0,0,0), FVector(100,0,0)), FSplinePointData(FVector(100,0,0), FVector(100,0,0)), FSplinePointData(FVector(1000,0,0), FVector(900,0,0))};
    const auto Weights = FBSTransformNative::SplineFractions(Points, FTransform::Identity);
    TestTrue(TEXT("Uneven spline points use distance weighting, not index weighting"), Weights.Num() == 3 && FMath::IsNearlyEqual(Weights[1], .1, .001) && Weights.Last() == 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSQueuedStepsTest, "BuildingSkills.Transform.QueuedInputCancellation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSQueuedStepsTest::RunTest(const FString&)
{
    for (int32 I = 0; I < 10; ++I)
    {
        const auto Mode = EBSTransformMode(I);
        double Amount = 0;
        FVector Move(100, 0, 0);
        bool Cancelled = false;
        TestTrue(TEXT("Opposite queued arrows combine in every scroll mode"), FBSTransformMath::CombineSteps(Mode, Amount, Move, 0, FVector(-100,0,0), Cancelled));
        TestTrue(TEXT("Cancelled movement must be removed before interpreting zero as a scroll command"), Cancelled);
        Amount = I < 3 ? 15 : 1.1;
        Move = FVector::ZeroVector;
        TestTrue(TEXT("Reciprocal queued scroll steps combine"), FBSTransformMath::CombineSteps(Mode, Amount, Move, I < 3 ? -15 : 1/1.1, FVector::ZeroVector, Cancelled));
        TestTrue(TEXT("Reciprocal scroll avoids a redundant server edit and undo entry"), Cancelled);
    }
    double Amount = 0;
    FVector Move(100,0,0);
    bool Cancelled = false;
    TestFalse(TEXT("Keep movement and scaling as separate commands"), FBSTransformMath::CombineSteps(EBSTransformMode::ScaleXYZ, Amount, Move, 2, FVector::ZeroVector, Cancelled));
    TestTrue(TEXT("Perpendicular movement combines"), FBSTransformMath::CombineSteps(EBSTransformMode::ScaleXYZ, Amount, Move, 0, FVector(0,100,0), Cancelled));
    TestTrue(TEXT("Combined movement retains both directions"), !Cancelled && Move.Equals(FVector(100,100,0)));
    Move = FVector(10000,0,0);
    TestFalse(TEXT("Movement beyond server batch limit stays separate"), FBSTransformMath::CombineSteps(EBSTransformMode::RotateX, Amount, Move, 0, FVector(1,0,0), Cancelled));
    TestTrue(TEXT("Rejected merge leaves the queued step intact"), Move.Equals(FVector(10000,0,0)));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSTransformDeltaTest, "BuildingSkills.Transform.GroupDeltaCatchUp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSTransformDeltaTest::RunTest(const FString&)
{
    FBSBuildingRef Original;
    Original.LightweightClass=AFGBuildable::StaticClass(); Original.Recipe=UFGRecipe::StaticClass();
    Original.RuntimeIndex=7; Original.Transform=FTransform(FVector(100,200,300));
    FBSGroup Parent, Child;
    Parent.Id=FGuid::NewGuid(); Child.Id=FGuid::NewGuid(); Child.Parent=Parent.Id; Child.Buildings.Add(Original);
    TArray<FBSGroup> Tree={Parent,Child};
    FBSNativeTransform Change; Change.Building=Original; Change.InstanceIndex=7; Change.Serial=2;
    Change.Target=FTransform(FVector(200,200,300)); Change.HasBaseline=true; Change.Baseline=Original.Transform;
    FBSTransformNative::UpdateGroupTransforms(Tree,{Change});
    TestTrue(TEXT("Delta retains group hierarchy"),Tree.Num()==2 && Tree[1].Parent==Parent.Id && Tree[0].Id==Parent.Id);
    TestTrue(TEXT("Delta moves member and ancestor pivots"),Tree[1].Buildings[0].Transform.Equals(Change.Target) &&
        Tree[0].Pivot.Equals(Change.Target.GetLocation()) && Tree[1].Pivot.Equals(Tree[0].Pivot));
    TestTrue(TEXT("Delta retains per-building concurrency token"),Tree[1].Buildings[0].TransformVersion==2);
    const auto Snapshot=Tree;
    const auto First=Change;
    Change.Building=Tree[1].Buildings[0]; Change.Serial=3; Change.Target.SetLocation(FVector(300,200,300));
    FBSTransformNative::UpdateGroupTransforms(Tree,{Change});
    Change.Building=Tree[1].Buildings[0]; Change.Serial=4; Change.Target.SetLocation(FVector(400,200,300));
    FBSTransformNative::UpdateGroupTransforms(Tree,{Change});
    FBSTransformNative::UpdateGroupTransforms(Tree,{First});
    TestTrue(TEXT("Late older delta cannot roll back members or pivot"),Tree[1].Buildings[0].TransformVersion==4 && Tree[0].Pivot.Equals(Change.Target.GetLocation()));
    Tree=Snapshot;
    FBSTransformNative::UpdateGroupTransforms(Tree,{Change},true);
    TestTrue(TEXT("Topology snapshot catches up across several already-applied moves"),Tree[1].Buildings[0].TransformVersion==4 && Tree[0].Pivot.Equals(Change.Target.GetLocation()));
    auto ReusedSlot=Original; ReusedSlot.Transform.SetLocation(FVector(900,900,900));
    TestFalse(TEXT("Unversioned reused slot cannot adopt an unrelated move"),FBSTransformNative::UpdateReference(ReusedSlot,Change,true));
    auto StaleHint=Original; StaleHint.RuntimeIndex=99;
    TestTrue(TEXT("Exact fingerprint corrects a stale runtime index hint"),FBSTransformNative::UpdateReference(StaleHint,First) && StaleHint.RuntimeIndex==7);
    auto OtherRecipe=Original; OtherRecipe.Recipe=nullptr;
    TestFalse(TEXT("Same slot and position cannot bypass a different recipe"),FBSTransformNative::UpdateReference(OtherRecipe,First,true));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSFrameMovementTest, "BuildingSkills.Transform.FrameMovement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSFrameMovementTest::RunTest(const FString&)
{
    const FQuat Frame = FRotator(25, 37, 12).Quaternion();
    TestTrue(TEXT("Local is the default"), FBSTransformOptions().Space == EBSTransformSpace::Local);
    TestTrue(TEXT("Forward uses reference X"), FBSTransformMath::FrameTranslation(Frame, Frame.GetAxisX(), 0).Equals(Frame.GetAxisX(), .0001));
    TestTrue(TEXT("Facing reference Y makes right negative X"), FBSTransformMath::FrameTranslation(Frame, Frame.GetAxisY(), 3).Equals(-Frame.GetAxisX(), .0001));
    TestTrue(TEXT("Page up follows tilted reference Z"), FBSTransformMath::FrameTranslation(Frame, FVector::ForwardVector, 4).Equals(Frame.GetAxisZ(), .0001));
    TestTrue(TEXT("World mode retains original nudge behavior"), FBSTransformMath::FrameTranslation(FQuat::Identity, FRotator(35,127,0).Vector(), 0).Equals(FBSTransformMath::ViewTranslation(127,0), .0001));
    return true;
}
#endif
