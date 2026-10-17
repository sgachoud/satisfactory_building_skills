#if WITH_DEV_AUTOMATION_TESTS
#include "BSTransformers.h"
#include "BSGroups.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSTransformerMathTest,"BuildingSkills.Transformers.Geometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBSTransformerMathTest::RunTest(const FString&)
{
    FBSTransformer R; R.Id=FGuid::NewGuid();
    TestTrue(TEXT("Defaults mirror and repeat the player"),R.Valid() && R.Mirror && !R.Rotation && R.RepeatPlayer && !R.RepeatTransformers);
    TestTrue(TEXT("Mirror is across X/Z"),R.Point(FVector(100,200,300)).Equals(FVector(100,-200,300),.0001));
    const FTransform T(FRotator(22,37,11),FVector(1200,-2700,815),FVector(1.2,.8,1.7));
    R.Frame=FTransform(FRotator(31,-42,19),FVector(150,310,90));
    const FTransform M=R.Pose(T);
    TestTrue(TEXT("Mirror twice restores arbitrary tilted pose"),R.Pose(M).Equals(T,.0001));
    TestTrue(TEXT("Mirror keeps original positive nonuniform scale"),M.GetScale3D().Equals(T.GetScale3D()));
    TestTrue(TEXT("Mirrored orientation remains right handed"),FVector::DotProduct(FVector::CrossProduct(M.GetRotation().GetAxisX(),M.GetRotation().GetAxisY()),M.GetRotation().GetAxisZ())>.99999);
    TestTrue(TEXT("Mirror reflects heading and up directions"),M.GetRotation().GetAxisX().Equals(R.Vector(T.GetRotation().GetAxisX()),.0001) && M.GetRotation().GetAxisZ().Equals(R.Vector(T.GetRotation().GetAxisZ()),.0001));
    R.Mirror=false;R.Rotation=true;R.Angle=90;R.Frame=FTransform::Identity;
    TestTrue(TEXT("Rotation uses local Z and degrees"),R.Point(FVector(100,0,20)).Equals(FVector(0,100,20),.0001));
    R.Radius=8;
    TestTrue(TEXT("Radius measured in meters, boundary included"),R.Contains(FVector(800,0,0)) && !R.Contains(FVector(801,0,0)));
    R.Mirror=true;TestFalse(TEXT("Two modes cannot both be enabled"),R.Valid());
    auto* Field=FindFProperty<FArrayProperty>(UBSGroupRemoteCallObject::StaticClass(),TEXT("Transformers"));
    TestTrue(TEXT("Transformer lists are session-only"),Field && Field->HasAnyPropertyFlags(CPF_Transient) && !Field->HasAnyPropertyFlags(CPF_SaveGame));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSTransformerFrameTest,"BuildingSkills.Transformers.EditableCoordinates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBSTransformerFrameTest::RunTest(const FString&)
{
    FBSTransformer R; R.Id=FGuid::NewGuid();
    R.OriginalAnchor=FTransform(FRotator(25,48,-17),FVector(12300,-4700,9800)); R.Frame=R.OriginalAnchor;
    TestTrue(TEXT("World frame shown initially"),R.DisplayFrame().Equals(R.Frame,.00001));
    R.RelativeToOriginalAnchor=true;
    TestTrue(TEXT("Original anchor starts at zero relative pose"),R.DisplayFrame().Equals(FTransform::Identity,.00001));
    const FTransform Local(FRotator(-12,30,9),FVector(800,-450,260));
    R.SetDisplayFrame(Local);
    TestTrue(TEXT("Relative position and rotation compose with tilted original anchor"),R.Frame.Equals(Local*R.OriginalAnchor,.00001));
    TestTrue(TEXT("Relative display round trips"),R.DisplayFrame().Equals(Local,.00001));
    const auto World=R.Frame; R.RelativeToOriginalAnchor=false;
    TestTrue(TEXT("Changing coordinate checkbox does not move transformer"),R.Frame.Equals(World) && R.DisplayFrame().Equals(World));
    R.SetDisplayFrame(FTransform(FRotator(7,-31,4),FVector(900,1500,700)));
    TestTrue(TEXT("Editing world pose preserves original anchor snapshot"),R.OriginalAnchor.GetLocation().Equals(FVector(12300,-4700,9800)) && R.Valid());
    auto* Field=FindFProperty<FBoolProperty>(UBSGroupRemoteCallObject::StaticClass(),TEXT("TransformersBuildAsBlueprint"));
    TestTrue(TEXT("Shared blueprint option is session-only"),Field && Field->HasAnyPropertyFlags(CPF_Transient) && !Field->HasAnyPropertyFlags(CPF_SaveGame));
    R.Frame.SetScale3D(FVector(-1,1,1)); TestFalse(TEXT("Edited frame rejects mirrored scale"),R.Valid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSTransformerOrderTest,"BuildingSkills.Transformers.OrderAndRadius",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBSTransformerOrderTest::RunTest(const FString&)
{
    FBSTransformer A;A.Id=FGuid::NewGuid();A.Radius=8;
    FBSTransformer B=A;B.Id=FGuid::NewGuid();B.Mirror=false;B.Rotation=true;B.Angle=90;B.RepeatTransformers=true;
    bool Limited=false;auto Plan=FBSTransformers::Plan({A,B},FVector(0,500,0),Limited);
    TestEqual(TEXT("Player plus mirror plus rotation of each"),Plan.Num(),4);
    TestEqual(TEXT("Second transformer repeats earlier output"),Plan[3].Parent,1);
    TestTrue(TEXT("Chain has expected center"),Plan[3].Center.Equals(FVector(500,0,0),.001));
    B.RepeatPlayer=false;Plan=FBSTransformers::Plan({A,B},FVector(0,500,0),Limited);
    TestEqual(TEXT("Repeat player can be disabled independently"),Plan.Num(),3);
    Plan=FBSTransformers::Plan({B,A},FVector(0,500,0),Limited);TestEqual(TEXT("Order forbids repeating later transformers"),Plan.Num(),2);
    Plan=FBSTransformers::Plan({A,B},FVector(0,801,0),Limited);TestEqual(TEXT("Whole placement outside radius produces no copies"),Plan.Num(),1);
    TArray<FBSTransformer> Many;for(int32 I=0;I<16;++I){auto R=A;R.Id=FGuid::NewGuid();R.RepeatTransformers=true;Many.Add(R);}
    Plan=FBSTransformers::Plan(Many,FVector(0,500,0),Limited);
    TestTrue(TEXT("Bounded ordered expansion cannot loop"),Limited && Plan.Num()==FBSTransformers::MaxCopies+1);
    for(int32 I=1;I<Plan.Num();++I)TestTrue(TEXT("Every dependency precedes the copy"),Plan[I].Parent<I && Plan[I].Parent>=0);
    return true;
}
#endif
