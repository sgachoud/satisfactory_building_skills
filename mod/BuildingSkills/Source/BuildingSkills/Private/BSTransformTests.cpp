#if WITH_DEV_AUTOMATION_TESTS
#include "BSTransform.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSTransformMathTest, "BuildingSkills.Transform.PivotAndInverse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSTransformMathTest::RunTest(const FString&)
{
    const FVector Pivot(1000, 2000, -300);
    const FTransform Original(FRotator(30, 50, 70), Pivot + FVector(100,200,300), FVector(1,2,3));
    for (int32 I = 0; I < 10; ++I)
    {
        const auto Mode = EBSTransformMode(I);
        const auto Changed = FBSTransformMath::Apply(Original, Pivot, Mode, I < 4 ? 15 : 1.1, FVector::ZeroVector);
        const auto Restored = FBSTransformMath::Apply(Changed, Pivot, Mode, I < 4 ? -15 : 1.0/1.1, FVector::ZeroVector);
        TestTrue(*FString::Printf(TEXT("%s inverse returns position, rotation and scale"), *FBSTransformMath::Name(Mode)), Restored.Equals(Original, .0001));
        if (I < 4) TestTrue(TEXT("Rotation preserves distance to pivot"), FMath::IsNearlyEqual(FVector::Distance(Changed.GetLocation(), Pivot), FVector::Distance(Original.GetLocation(), Pivot), .0001));
        else
        {
            const auto Axes = FBSTransformMath::Axes(Mode);
            for (int32 Axis=0; Axis<3; ++Axis)
                TestTrue(TEXT("Only active scale components change"), FMath::IsNearlyEqual(Changed.GetScale3D()[Axis], Original.GetScale3D()[Axis] * (Axes[Axis] ? 1.1 : 1), .0001));
        }
    }
    const FTransform Rotated = FBSTransformMath::Apply(FTransform(Pivot + FVector(100,0,0)), Pivot, EBSTransformMode::RotateZ, 90, FVector::ZeroVector);
    TestTrue(TEXT("Z rotation uses shared pivot"), Rotated.GetLocation().Equals(Pivot + FVector(0,100,0), .0001));
    TestTrue(TEXT("Forward follows facing X"), FBSTransformMath::ViewTranslation(0,0).Equals(FVector(1,0,0),.0001));
    TestTrue(TEXT("Forward follows facing Y"), FBSTransformMath::ViewTranslation(90,0).Equals(FVector(0,1,0),.0001));
    TestTrue(TEXT("Right follows facing Y"), FBSTransformMath::ViewTranslation(90,3).Equals(FVector(-1,0,0),.0001));
    TestTrue(TEXT("Vertical is independent of facing"), FBSTransformMath::ViewTranslation(127,4).Equals(FVector::UpVector,.0001));
    return true;
}
#endif
