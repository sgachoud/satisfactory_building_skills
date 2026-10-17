#if WITH_DEV_AUTOMATION_TESTS
#include "BSVolume.h"
#include "BSDeduplication.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSVolumeMathTest, "BuildingSkills.Volume.Geometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSVolumeMathTest::RunTest(const FString&)
{
    FBSVolumeShape V;
    TestTrue(TEXT("Sphere includes boundary centers"), V.Contains(FVector(800,0,0)));
    TestFalse(TEXT("Sphere excludes box corner"), V.Contains(FVector(700,700,0)));
    V.Resize(FVector::ForwardVector,1,800,false); TestEqual(TEXT("Sphere radius grows by eight meters"), V.Radius,1600.0);
    V.Cube=true;
    V.Resize(FVector::ForwardVector,1,800,false);
    TestTrue(TEXT("Only positive X face moves"),V.Min==FVector(-800)&&V.Max==FVector(1600,800,800));
    V.Resize(FVector::ForwardVector,1,800,true);
    TestEqual(TEXT("Alt selects opposite face"),V.Min.X,-1600.0);
    V.Resize(-FVector::UpVector,-1,100,false);
    TestEqual(TEXT("Negative scroll shrinks negative Z face"),V.Min.Z,-700.0);
    V.Frame=FTransform(FRotator(0,90,0),FVector(2000,3000,4000));
    V.Resize(FVector::RightVector,1,100,false);
    TestEqual(TEXT("View follows rotated local X"),V.Max.X,1700.0);
    TestTrue(TEXT("Frame moves to anchor"),V.Contains(V.Frame.GetLocation()));
    TestFalse(TEXT("Outside transformed face excluded"),V.Contains(V.Frame.TransformPosition(FVector(1900,0,0))));
    FBSVolumeSettings Settings;
    TestTrue(TEXT("Default settings valid"),Settings.Valid());
    Settings.VolumeStep=0; TestFalse(TEXT("Zero resize rejected"),Settings.Valid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSDedupBoundsTest,"BuildingSkills.Blueprints.DeduplicationBounds",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSDedupBoundsTest::RunTest(const FString&)
{
    const FBox Box(FVector(-400,-400,-50),FVector(400,400,50));
    const FTransform A(FRotator(0,15,0),FVector(100000,200000,-3000));
    TestTrue(TEXT("Identical placed boxes match"),FBSDeduplication::SameBounds(Box,A,Box,A));
    FTransform B=A; B.AddToTranslation(FVector(.05,0,0));
    TestTrue(TEXT("Placement rounding tolerated"),FBSDeduplication::SameBounds(Box,A,Box,B));
    B=A; B.AddToTranslation(FVector(1,0,0));
    TestFalse(TEXT("One centimeter offset is not duplicate"),FBSDeduplication::SameBounds(Box,A,Box,B));
    B=A; B.SetRotation(FRotator(0,105,0).Quaternion());
    TestTrue(TEXT("Square foundation quarter-turn has same box"),FBSDeduplication::SameBounds(Box,A,Box,B));
    B=A; B.SetRotation(FRotator(0,45,0).Quaternion());
    TestFalse(TEXT("Diagonal overlap is not equality"),FBSDeduplication::SameBounds(Box,A,Box,B));
    B=A; B.SetScale3D(FVector(1,1,2));
    TestFalse(TEXT("Different height is not duplicate"),FBSDeduplication::SameBounds(Box,A,Box,B));
    return true;
}
#endif
