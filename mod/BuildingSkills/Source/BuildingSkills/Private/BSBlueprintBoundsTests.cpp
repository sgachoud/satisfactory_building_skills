#if WITH_DEV_AUTOMATION_TESTS
#include "BSBlueprintBounds.h"
#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSBlueprintBoundsTest, "BuildingSkills.Blueprints.CenterAndDominantAlignment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSBlueprintBoundsTest::RunTest(const FString&)
{
    TArray<FVector> Points;
    const FTransform Frame(FRotator(0, 30, 0), FVector(300000, -150000, 700));
    const FBox Foundation(FVector(-400, -400, 0), FVector(400, 400, 400));
    for (int32 I = 0; I < 3; ++I)
        BSBlueprintBounds::AppendBox(Foundation,
            FTransform(FQuat::Identity, FVector(0, I * 800, 0), I == 1 ? FVector(.5, .75, .5) : FVector::OneVector) * Frame, Points);
    FTransform Origin;
    FVector Size;
    TestTrue(TEXT("Rotated foundation row can be bounded"), BSBlueprintBounds::FitUpright(Points, {30, 120, 210}, Origin, Size));
    TestTrue(TEXT("Rotated row has no world-axis bounding-box inflation"), Size.Equals(FVector(800, 2400, 400), .001));
    TestTrue(TEXT("Origin is the XY geometric center at the lowest Z"), Origin.GetLocation().Equals(Frame.TransformPosition(FVector(0, 800, 0)), .001));
    TestTrue(TEXT("Quarter-turn directions share the same horizontal alignment"), Origin.GetRotation().Equals(Frame.GetRotation(), .0001));
    FBox Local(ForceInit);
    for (const auto& Point : Points) Local += Origin.InverseTransformPosition(Point);
    TestTrue(TEXT("XY extents are symmetric and the lowest point is zero"), Local.Min.Equals(FVector(-Size.X / 2, -Size.Y / 2, 0), .001) && Local.Max.Equals(FVector(Size.X / 2, Size.Y / 2, Size.Z), .001));
    Algo::Reverse(Points);
    FTransform ReorderedOrigin;
    FVector ReorderedSize;
    BSBlueprintBounds::FitUpright(Points, {210, 30, 120}, ReorderedOrigin, ReorderedSize);
    TestTrue(TEXT("Selection order does not change alignment or bounds"), ReorderedOrigin.Equals(Origin, .001) && ReorderedSize.Equals(Size, .001));

    Points.Reset();
    const FBox LongBox(FVector(-1000, -100, -20), FVector(1000, 100, 20));
    BSBlueprintBounds::AppendBox(LongBox, FTransform(FRotator(0, 45, 0)), Points);
    BSBlueprintBounds::AppendBox(FBox(FVector(-10), FVector(10)), FTransform::Identity, Points);
    BSBlueprintBounds::FitUpright(Points, {0, 45}, Origin, Size);
    TestTrue(TEXT("Equal alignment counts choose the tighter box"), FMath::IsNearlyEqual(Origin.Rotator().Yaw, 45.0, .001) && Size.Equals(FVector(2000, 200, 40), .001));
    BSBlueprintBounds::FitUpright(Points, {0, 0, 45}, Origin, Size);
    TestTrue(TEXT("Majority alignment wins over a smaller diagonal box"), FMath::IsNearlyZero(Origin.Rotator().Yaw, .001) && Size.X > 1500 && Size.Y > 1500);

    Points.Reset();
    const FTransform Mirrored(FRotator(0, 17, 0), FVector(100, 200, 300), FVector(-2, .5, 3));
    BSBlueprintBounds::AppendBox(FBox(FVector::ZeroVector, FVector(100, 200, 300)), Mirrored, Points);
    BSBlueprintBounds::FitUpright(Points, {17}, Origin, Size);
    TestTrue(TEXT("Scaled and mirrored geometry determines the true center and size"),
        Size.Equals(FVector(200, 100, 900), .001) && Origin.GetLocation().Equals(Mirrored.TransformPosition(FVector(50, 100, 0)), .001));
    TestFalse(TEXT("Empty geometry is rejected"), BSBlueprintBounds::FitUpright({}, {0}, Origin, Size));
    Points.Reset();
    const FTransform Anchor(FRotator(21, 137, -18), FVector(300000, -150000, 700));
    BSBlueprintBounds::AppendBox(Foundation, Anchor, Points);
    TestTrue(TEXT("Anchor accepts pitch yaw and roll"), BSBlueprintBounds::FitFrame(Points, Anchor.GetRotation(), Origin, Size));
    TestTrue(TEXT("Anchor retains signed axes instead of quarter-turn canonicalization"), Origin.GetRotation().Equals(Anchor.GetRotation(), .0001));
    TestTrue(TEXT("Anchor bounds tightly enclose geometry in its coordinate frame"), Size.Equals(FVector(800,800,400), .001) && Origin.Equals(Anchor, .001));
    TestFalse(TEXT("Anchor rejects empty geometry"), BSBlueprintBounds::FitFrame({}, Anchor.GetRotation(), Origin, Size));
    TArray<FVector> FloorPoints;
    BSBlueprintBounds::AppendBox(FBox(FVector(-9000,-9000,-500), FVector(9000,9000,-100)), Anchor, FloorPoints);
    BSBlueprintBounds::FitFrame(Points, Anchor.GetRotation(), Origin, Size, FloorPoints);
    TestTrue(TEXT("Excluded lower anchor extends only the floor, keeping selected XY bounds"),
        Size.Equals(FVector(800,800,900), .001) && Origin.GetLocation().Equals(Anchor.TransformPosition(FVector(0,0,-500)), .001));
    FloorPoints.Reset();
    BSBlueprintBounds::AppendBox(FBox(FVector(-9000,-9000,500), FVector(9000,9000,900)), Anchor, FloorPoints);
    BSBlueprintBounds::FitFrame(Points, Anchor.GetRotation(), Origin, Size, FloorPoints);
    TestTrue(TEXT("Excluded higher anchor neither lifts the floor nor enlarges other bounds"), Size.Equals(FVector(800,800,400), .001) && Origin.Equals(Anchor, .001));
    return true;
}
#endif
