#include "BSConnected.h"
#include "Misc/AutomationTest.h"
#include "Buildables/FGBuildable.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSConnectedGeometryTest, "BuildingSkills.Connected.Geometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSConnectedGeometryTest::RunTest(const FString&)
{
    auto Box = [](double X, double Y = 0, double Z = 0) { return FBox(FVector(X,Y,Z), FVector(X+100,Y+100,Z+100)); };
    bool Limited;
    TestEqual(TEXT("Contact traverses faces, edges, corners and vertical layers"), FBSConnected::Traverse({Box(0),Box(100),Box(200,100),Box(300,200,100)},0,0,20,Limited).Num(),4);
    TestEqual(TEXT("Tolerance accommodates 0.05 cm placement rounding"),FBSConnected::Traverse({Box(0),Box(100.05)},0,0,20,Limited).Num(),2);
    TestEqual(TEXT("Larger empty gap is excluded"),FBSConnected::Traverse({Box(0),Box(100.2)},0,0,20,Limited).Num(),1);
    TestEqual(TEXT("Meters convert to cm and expansion propagates from each neighbor"),FBSConnected::Traverse({Box(0),Box(200),Box(400)},0,1,20,Limited).Num(),3);
    TestEqual(TEXT("Expand one box only: a 1.5 m gap is not bridged by a 1 m setting"),FBSConnected::Traverse({Box(0),Box(250)},0,1,20,Limited).Num(),1);
    TestEqual(TEXT("Limit includes seed"),FBSConnected::Traverse({Box(0),Box(100),Box(200)},0,0,2,Limited).Num(),2);
    TestTrue(TEXT("Truncation reported"),Limited);
    FBSConnected::Traverse({Box(0),Box(100)},0,0,2,Limited);
    TestFalse(TEXT("Exact size is not truncation"),Limited);
    TestEqual(TEXT("Rejected middle object cannot bridge the search"),FBSConnected::Traverse({Box(0),FBox(ForceInit),Box(200)},0,0,20,Limited).Num(),1);
    TArray<FBox> Many; for (int32 I=0; I<10000; ++I) Many.Add(Box(I*100.0));
    TestEqual(TEXT("Large connected chain obeys cap"),FBSConnected::Traverse(Many,0,0,512,Limited).Num(),512);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSConnectedRegexTest, "BuildingSkills.Connected.Filters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSConnectedRegexTest::RunTest(const FString&)
{
    FBSConnectedSettings S; FString Error;
    TestTrue(TEXT("Empty filters allow names"),FBSConnected::NameMatches(TEXT("Foundation 2m"),S,Error));
    S.Whitelist=TEXT("(?i)foundation"); S.Blacklist=TEXT("4m$");
    TestTrue(TEXT("Whitelist regex supports flags"),FBSConnected::NameMatches(TEXT("Concrete Foundation 2m"),S,Error));
    TestFalse(TEXT("Blacklist overrides whitelist"),FBSConnected::NameMatches(TEXT("Concrete Foundation 4m"),S,Error));
    TestFalse(TEXT("Other names excluded"),FBSConnected::NameMatches(TEXT("Constructor"),S,Error));
    S.Blacklist.Reset(); S.Whitelist=TEXT("(?i)(?:belt|pipe)");
    for (const TCHAR* Name : {TEXT("Conveyor Belt Mk.1"), TEXT("Pipeline Mk.2")})
        TestTrue(FString::Printf(TEXT("Alternation includes %s"),Name),FBSConnected::NameMatches(Name,S,Error));
    for (const TCHAR* Name : {TEXT("Foundry"), TEXT("Power Pole Mk.1"), TEXT("Power Line"), TEXT("Resource Well Extractor")})
        TestFalse(FString::Printf(TEXT("Alternation excludes %s"),Name),FBSConnected::NameMatches(Name,S,Error));
    S.Whitelist=TEXT(".*[(?:Belt)(?:Pipe)].*");
    TestTrue(TEXT("Square brackets are a character class, not word alternatives"),FBSConnected::NameMatches(TEXT("Power Pole Mk.1"),S,Error));
    S.Whitelist=TEXT(".*[(?:Belt)(?:Pipe)].*");
    auto* Foundry = LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/FoundryMk1/Build_FoundryMk1.Build_FoundryMk1_C"));
    TestNotNull(TEXT("Native foundry class available"),Foundry);
    if (Foundry)
    {
        const FString Name=FBSConnected::BuildingName(Foundry,nullptr);
        TestEqual(TEXT("Recipe-less foundry uses its displayed name"),Name,FString(TEXT("Foundry")));
        TestFalse(TEXT("Character whitelist cannot match hidden Build_ class prefix"),FBSConnected::NameMatches(Name,S,Error));
    }
    S.Whitelist.Reset();
    S.Blacklist=TEXT("["); TestFalse(TEXT("Malformed blacklist fails closed"),S.Validate(Error)); TestFalse(TEXT("Error explained"),Error.IsEmpty());
    S.Blacklist.Reset(); S.Gap=-1; TestFalse(TEXT("Negative gap rejected"),S.Validate(Error));
    S.Gap=0; S.Limit=0; TestFalse(TEXT("Zero cap rejected"),S.Validate(Error));
    S.Limit=2049; TestFalse(TEXT("Native selection ceiling enforced"),S.Validate(Error));
    return true;
}
