#include "BSBlueprintScale.h"
#include "BSClipboard.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "FGActorSaveHeaderTypes.h"
#include "FGBlueprintSubsystem.h"
#include "FGBlueprintSettings.h"
#include "FGBlueprintProxy.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGFactoryConnectionComponent.h"
#include "FGDismantleInterface.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Patching/NativeHookManager.h"

namespace BSBlueprintNetworkSmoke
{
    FString FixturePath;
    const FString FixtureName = TEXT("BuildingSkills_ReadOnlyNetworkFixture");
    TArray<TWeakObjectPtr<AFGBuildableConveyorBase>> Source, Copies;
    TArray<TWeakObjectPtr<AFGConveyorChainActor>> SourceChains;
    TArray<TWeakObjectPtr<UFGFactoryConnectionComponent>> SourcePorts;
    int32 ExpectedConveyors = 0;

    bool SourceUnchanged()
    {
        if (Source.IsEmpty() || SourceChains.Num() != Source.Num() || SourcePorts.Num() != Source.Num()*2) return false;
        for (int32 I=0; I<Source.Num(); ++I)
        {
            auto* Belt=Source[I].Get();
            if (!Belt || !SourceChains[I].IsValid() || Belt->GetConveyorChainActor()!=SourceChains[I].Get() ||
                Belt->GetConnection0()->GetConnection()!=SourcePorts[I*2].Get() ||
                Belt->GetConnection1()->GetConnection()!=SourcePorts[I*2+1].Get()) return false;
        }
        return true;
    }
}

// This hook is installed only by the explicit, save-blocked test harness. The
// native disk reader receives bytes from the supplied file without importing it
// into the player's library, rewriting it, or opening any existing world save.
void InstallBuildingSkillsBlueprintNetworkSmoke()
{
    using namespace BSBlueprintNetworkSmoke;
    if (!FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsRuntimeTest")) ||
        !FParse::Value(FCommandLine::Get(),TEXT("BuildingSkillsBlueprintFixture="),FixturePath)) return;
    SUBSCRIBE_METHOD(AFGBlueprintSubsystem::LoadFileFromDisk,
        [](auto& Scope, AFGBlueprintSubsystem*, const FString& Name, const FString&, TArray<uint8>& Bytes)
        {
            if (Name==FixtureName) Scope.Override(FFileHelper::LoadFileToArray(Bytes,*FixturePath));
        });
}

bool RunBuildingSkillsBlueprintNetworkChecks(AFGCharacterPlayer* Player, int32 Phase)
{
    using namespace BSBlueprintNetworkSmoke;
    check(FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsRuntimeTest")));
    auto* World=Player->GetWorld();
    auto* System=AFGBlueprintSubsystem::Get(World);
    auto* PC=Cast<AFGPlayerController>(Player->GetController());
    auto* RCO=PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    bool Passed=true;
    auto Check=[&](const TCHAR* Label,bool OK)
    { Passed &= OK; UE_LOG(LogBuildingSkills,Display,TEXT("RUNTIME ASSERT: %s: %s"),Label,OK?TEXT("PASS"):TEXT("FAIL")); };
    if (!System || !RCO || FixturePath.IsEmpty()) return false;
    auto Place=[&](UBSClipboard* Clip,const FVector& Offset,TArray<TWeakObjectPtr<AFGBuildableConveyorBase>>& Out)
    {
        Clip->Data.BlueprintHeader.BlueprintName=Clip->Name();
        Clip->MakeDescriptor(); RCO->Clipboards.Add(Clip);
        TArray<AFGBuildable*> Preview;
        System->LoadStoredBlueprint(Clip->Descriptor,FTransform::Identity,Preview,true);
        bool Clean=true; int32 Count=0;
        for (auto* B:Preview) if (auto* Conveyor=Cast<AFGBuildableConveyorBase>(B))
        { ++Count; Clean &= Conveyor->GetConveyorChainActor()==nullptr; }
        Check(TEXT("Imported conveyor preview has no live-world simulation chain"),Clean && Count==ExpectedConveyors);
        if (Preview.IsEmpty()) return false;
        auto* Holo=World->SpawnActorDeferred<AFGBlueprintHologram>(AFGBlueprintHologram::StaticClass(),FTransform::Identity,Player,Player);
        if (!Holo) return false;
        Holo->SetRecipe(UFGBlueprintSettings::Get()->mBlueprintRecipeClass.LoadSynchronous());
        Holo->SetBuildClass(Preview[0]->GetClass()); Holo->SetConstructionInstigator(Player);
        Holo->SetBlueprintDescriptor(Clip->Descriptor); Holo->FinishSpawning(FTransform::Identity);
        Holo->SetActorTransform(FTransform(Player->GetActorLocation()+Offset));
        TArray<AActor*> Children;
        auto* Proxy=Cast<AFGBlueprintProxy>(Holo->Construct(Children,FNetConstructionID()));
        if (Proxy) for (auto* B:Proxy->GetBuildables()) if (auto* Conveyor=Cast<AFGBuildableConveyorBase>(B)) Out.Add(Conveyor);
        Holo->Destroy();
        Check(TEXT("Imported blueprint constructs every archived belt and lift"),Out.Num()==ExpectedConveyors);
        return Proxy && Out.Num()==ExpectedConveyors;
    };
    if (Phase==0)
    {
        Source.Reset(); Copies.Reset(); SourceChains.Reset(); SourcePorts.Reset(); ExpectedConveyors=0;
        const bool Read=System->ReadBlueprintFromDisc(FixtureName);
        Check(TEXT("Reported blueprint is decoded by the native disk reader"),Read);
        if (!Read) return false;
        auto* Clip=NewObject<UBSClipboard>(RCO); Clip->RequestId=FGuid::NewGuid(); Clip->Data=System->mStoredBlueprintData;
        TArray<FActorSaveHeader> Headers;
        if (!ReadBuildingSkillsBlueprintHeaders(Clip->Data,Headers)) return false;
        bool AllClasses=true;
        for (const auto& Header:Headers)
        {
            auto* Class=LoadClass<AFGBuildable>(nullptr,*Header.ObjectHeader.ClassName);
            if (!Class) UE_LOG(LogBuildingSkills,Error,TEXT("BLUEPRINT FIXTURE missing building class: %s"),*Header.ObjectHeader.ClassName);
            AllClasses &= Class!=nullptr;
            if (Class && Class->IsChildOf(AFGBuildableConveyorBase::StaticClass())) ++ExpectedConveyors;
        }
        UE_LOG(LogBuildingSkills,Display,TEXT("BLUEPRINT FIXTURE actors=%d conveyors=%d"),Headers.Num(),ExpectedConveyors);
        Check(TEXT("Every building class in the reported blueprint is installed"),AllClasses && ExpectedConveyors>0);
        if (!AllClasses || !ExpectedConveyors) return false;
        return Place(Clip,FVector(30000,0,10000),Source) && Passed;
    }
    if (Phase==5)
    {
        bool Live=Source.Num()==ExpectedConveyors && ExpectedConveyors>0;
        for (const auto& Weak:Source)
        {
            auto* Belt=Weak.Get(); auto* Chain=Belt ? Belt->GetConveyorChainActor() : nullptr;
            Live &= IsValid(Chain) && Chain->GetSegmentForConveyorBase(Belt)!=nullptr;
            SourceChains.Add(Chain);
            SourcePorts.Add(Belt ? Belt->GetConnection0()->GetConnection() : nullptr);
            SourcePorts.Add(Belt ? Belt->GetConnection1()->GetConnection() : nullptr);
        }
        Check(TEXT("All imported belts and lifts acquire valid native chain segments"),Live);
        if (!Live) return false;
        TArray<AFGBuildable*> Selected; for (const auto& Weak:Source) Selected.Add(Weak.Get());
        auto* Clip=NewObject<UBSClipboard>(RCO); Clip->RequestId=FGuid::NewGuid();
        FBlueprintRecord Record; Record.BlueprintName=Clip->Name();
        const bool Saved=WriteBuildingSkillsClipboard(System,Record,FTransform(Player->GetActorLocation()),Selected,FIntVector(16,16,16),Clip->Data);
        Check(TEXT("Running conveyor network captures without altering source chains"),Saved && SourceUnchanged());
        if (!Saved) return false;
        const bool Placed=Place(Clip,FVector(-30000,0,10000),Copies);
        Check(TEXT("Previewing and placing live conveyor copies preserves source chains and ports"),SourceUnchanged());
        return Placed && Passed;
    }
    if (Phase==10 || Phase==30)
    {
        bool Independent=Copies.Num()==ExpectedConveyors && ExpectedConveyors>0, Reciprocal=true;
        TSet<AActor*> Members; for (const auto& Weak:Copies) if (Weak.IsValid()) Members.Add(Weak.Get());
        for (const auto& Weak:Copies)
        {
            auto* Belt=Weak.Get(); auto* Chain=Belt ? Belt->GetConveyorChainActor() : nullptr;
            Independent &= IsValid(Chain) && Chain->GetSegmentForConveyorBase(Belt)!=nullptr && !SourceChains.Contains(Chain);
            if (!Belt) { Reciprocal=false; continue; }
            for (auto* Port:{Belt->GetConnection0(),Belt->GetConnection1()}) if (auto* Other=Port->GetConnection())
                Reciprocal &= Members.Contains(Other->GetOwner()) && Other->GetConnection()==Port;
        }
        Check(TEXT("Live conveyor copies have independent native simulation chains"),Independent);
        Check(TEXT("Live conveyor copies keep only reciprocal copied belt/lift connections"),Reciprocal);
        Check(TEXT("Original chain membership and machine connections remain unchanged"),SourceUnchanged());
        if (Phase==10) CollectGarbage(RF_NoFlags);
        if (Phase==30) for (const auto& Weak:Copies) if (auto* Belt=Weak.Get()) IFGDismantleInterface::Execute_Dismantle(Belt);
    }
    if (Phase==35) Check(TEXT("Dismantling copied belts and lifts leaves original simulation intact"),SourceUnchanged());
    return Passed;
}
