#include "BSPlan.h"
#include "BSGroups.h"
#include "FGPlayerState.h"
#include "FGPlayerController.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UnrealType.h"

bool RunBuildingSkillsPlanOwnershipChecks(TFunctionRef<void(const TCHAR*,bool)> Check)
{
    bool Passed=true;
    auto Test=[&](const TCHAR* Name,bool Value) { Check(Name,Value); Passed &= Value; };
    FClientIdentityInfo Alice; Alice.OfflineId=TEXT("plan-test-alice");
    FClientIdentityInfo Bob; Bob.OfflineId=TEXT("plan-test-bob");
    Test(TEXT("Empty identities never claim personal plans"),!FBSPlanOwnership::Matches({},{}));
    Test(TEXT("Offline owner matches only the same nonempty identity"),FBSPlanOwnership::Matches(Alice,Alice) && !FBSPlanOwnership::Matches(Alice,Bob));
    auto OnlineA=Alice,OnlineB=Alice;
    using namespace UE::Online;
    OnlineA.AccountIds.Add(EOnlineServices::Steam,FAccountId(EOnlineServices::Steam,1));
    OnlineB.AccountIds.Add(EOnlineServices::Steam,FAccountId(EOnlineServices::Steam,2));
    Test(TEXT("Conflicting online users cannot claim a plan through a shared offline ID"),!FBSPlanOwnership::Matches(OnlineA,OnlineB));
    OnlineB.OfflineId=TEXT("different-local-installation");
    OnlineB.AccountIds=OnlineA.AccountIds;
    Test(TEXT("An online owner survives a changed local offline ID"),FBSPlanOwnership::Matches(OnlineA,OnlineB));

    const auto* IdentityProperty=FindFProperty<FStructProperty>(FBSRailPlan::StaticStruct(),TEXT("OwnerIdentity"));
    Test(TEXT("Plan owner identity is a saved native identity structure"),IdentityProperty &&
        IdentityProperty->HasAnyPropertyFlags(CPF_SaveGame) && IdentityProperty->Struct==FClientIdentityInfo::StaticStruct());
#if !WITH_EDITOR
    // The SDK editor stubs the native identity getter/setter, serializer and
    // remote-call GetWorld. Exercise those APIs only in the packaged fixture.
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto* Original=NewObject<AFGPlayerState>(World->PersistentLevel);
    auto* Rejoined=NewObject<AFGPlayerState>(World->PersistentLevel);
    auto* Other=NewObject<AFGPlayerState>(World->PersistentLevel);
    Original->SetClientIdentity(Alice); Rejoined->SetClientIdentity(Alice); Other->SetClientIdentity(Bob);
    FBSRailPlan Source; Source.Id=FGuid::NewGuid(); Source.Name=TEXT("Persistent personal plan");
    FBSPlanNode Node; Node.Id=FGuid::NewGuid(); Node.Position=FVector(1200,3400,5600); Source.Nodes.Add(Node);
    Source.Owner=Original;
    Test(TEXT("Legacy plans recognize the owner's replacement PlayerState"),FBSPlanOwnership::Owns(Source,Rejoined));
    FBSPlanOwnership::CaptureLegacy(Source);
    Source.Owner=nullptr; // Simulate losing the old actor when loading/rejoining.
    FBSRailPlan Loaded; TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Save(Writer,false); Save.ArIsSaveGame=true;
    FBSRailPlan::StaticStruct()->SerializeItem(Save,&Source,nullptr);
    FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Load(Reader,true); Load.ArIsSaveGame=true;
    FBSRailPlan::StaticStruct()->SerializeItem(Load,&Loaded,nullptr);
    Test(TEXT("Saved personal identity and geometry survive without the old owner actor"),!Save.IsError() && !Load.IsError() && !Loaded.Owner &&
        Loaded.Id==Source.Id && Loaded.Nodes.Num()==1 && Loaded.Nodes[0].Position==Node.Position && FBSPlanOwnership::Owns(Loaded,Rejoined));
    Test(TEXT("A different player cannot access the reloaded personal plan"),!FBSPlanOwnership::Owns(Loaded,Other));
    Loaded.Owner=Other;
    Test(TEXT("A reused actor reference cannot override the saved owner identity"),!FBSPlanOwnership::Owns(Loaded,Other) && FBSPlanOwnership::Owns(Loaded,Rejoined));
    FBSPlanOwnership::Assign(Loaded,Other);
    Test(TEXT("Duplicating assigns the new owner's identity"),FBSPlanOwnership::Owns(Loaded,Other) && !FBSPlanOwnership::Owns(Loaded,Rejoined));

    auto* Controller=NewObject<AFGPlayerController>(World->PersistentLevel);
    auto* RCO=NewObject<UBSGroupRemoteCallObject>(Controller);
    RCO->PlanTransfer=FGuid::NewGuid(); RCO->PlanSequence=2; RCO->PlanTransferFinal=true;
    RCO->PlanTransferDeadline=World->GetRealTimeSeconds()+15; RCO->OutgoingPlanHash=42;
    RCO->Server_PlanNext_Implementation(FGuid::NewGuid(),3);
    Test(TEXT("A stale snapshot acknowledgement cannot complete a transfer"),RCO->PlanTransfer.IsValid() && RCO->DeliveredPlanHash==0);
    RCO->Server_PlanNext_Implementation(RCO->PlanTransfer,3);
    Test(TEXT("Final snapshot acknowledgement commits delivery without sending another chunk"),!RCO->PlanTransfer.IsValid() && RCO->DeliveredPlanHash==42);
    World->DestroyWorld(false);
#endif
    return Passed;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSPlanOwnershipTest,"BuildingSkills.Plan.PersistentOwnership",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSPlanOwnershipTest::RunTest(const FString&)
{
    AddInfo(TEXT("Editor checks identity matching and SaveGame metadata. Native identity save/load and final delivery acknowledgement require the packaged Plan fixture."));
    return RunBuildingSkillsPlanOwnershipChecks([this](const TCHAR* Name,bool Value) { TestTrue(Name,Value); });
}
#endif
