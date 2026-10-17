#if WITH_DEV_AUTOMATION_TESTS
#include "BSActionHistory.h"
#include "Misc/AutomationTest.h"
#include "FGPlayerController.h"
#include "Engine/World.h"
#include "Engine/Level.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSActionCountTest, "BuildingSkills.Undo.ActionCount", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSActionCountTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto* Controller=NewObject<AFGPlayerController>(World->PersistentLevel);
    auto* RCO=NewObject<UBSGroupRemoteCallObject>(Controller);
    for(int32 I=0; I<40; ++I)
    {
        auto* Entry=NewObject<UBSActionRecord>(RCO); Entry->Deletion=true; Entry->Members.SetNum(100);
        Entry->Error=FString::FromInt(I); FBSActionHistory::Record(RCO,Entry);
    }
    TestEqual(TEXT("Default history retains 32 actions, not 32 objects"),RCO->DeleteHistory.Num(),32);
    TestEqual(TEXT("Oldest actions are discarded first"),RCO->DeleteHistory[0]->Error,FString(TEXT("8")));
    auto* Construction=NewObject<UBSActionRecord>(RCO); Construction->Members.SetNum(4); FBSActionHistory::Record(RCO,Construction);
    TestEqual(TEXT("Build has an independent history"),RCO->BuildHistory.Num(),1);
    TestEqual(TEXT("Recording build does not evict deletion"),RCO->DeleteHistory.Num(),32);
    auto* Other=NewObject<UBSGroupRemoteCallObject>(Controller); TestTrue(TEXT("History belongs to its player"),Other->DeleteHistory.IsEmpty());
    World->DestroyWorld(false);
    return true;
}
#endif
