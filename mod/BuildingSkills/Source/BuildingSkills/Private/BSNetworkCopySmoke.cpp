#include "BSClipboard.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGBuildableSubsystem.h"
#include "FGBlueprintSubsystem.h"
#include "FGBlueprintSettings.h"
#include "FGInventoryLibrary.h"
#include "FGDismantleInterface.h"
#include "FGBlueprintProxy.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGRecipeManager.h"
#include "FGPowerConnectionComponent.h"
#include "FGPowerInfoComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGPipeSubsystem.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildableWire.h"
#include "Hologram/FGBlueprintHologram.h"

namespace
{
    TArray<TWeakObjectPtr<AFGBuildable>> CopySources, CopyPlaced;
    TArray<TPair<TWeakObjectPtr<UFGPipeConnectionComponentBase>, TWeakObjectPtr<UFGPipeConnectionComponentBase>>> OriginalPipeLinks;
    struct FCopyPowerLinks
    {
        TWeakObjectPtr<UFGCircuitConnectionComponent> Port;
        TArray<AFGBuildableWire*> Wires;
        TArray<UFGCircuitConnectionComponent*> Hidden;
    };
    TArray<FCopyPowerLinks> OriginalPowerLinks;
    void RememberCopyLinks(AFGBuildable* Building)
    {
        for (auto* P : TInlineComponentArray<UFGCircuitConnectionComponent*>(Building))
        {
            auto& Entry=OriginalPowerLinks.AddDefaulted_GetRef(); Entry.Port=P;
            P->GetWires(Entry.Wires); P->GetHiddenConnections(Entry.Hidden);
        }
        for (auto* P : TInlineComponentArray<UFGPipeConnectionComponentBase*>(Building))
            OriginalPipeLinks.Emplace(P,P->GetConnection());
    }
    bool OriginalCopyLinksUnchanged()
    {
        for (const auto& E:OriginalPowerLinks)
        {
            if (!E.Port.IsValid()) return false;
            TArray<AFGBuildableWire*> Wires; TArray<UFGCircuitConnectionComponent*> Hidden;
            E.Port->GetWires(Wires); E.Port->GetHiddenConnections(Hidden);
            if (Wires!=E.Wires || Hidden!=E.Hidden)
            { UE_LOG(LogBuildingSkills,Error,TEXT("COPY SOURCE CHANGED power=%s wires=%d/%d hidden=%d/%d"),*E.Port->GetPathName(),Wires.Num(),E.Wires.Num(),Hidden.Num(),E.Hidden.Num()); return false; }
        }
        for (const auto& E:OriginalPipeLinks)
            if (!E.Key.IsValid() || E.Key->GetConnection()!=E.Value.Get())
            { UE_LOG(LogBuildingSkills,Error,TEXT("COPY SOURCE CHANGED pipe=%s expected=%s actual=%s"),*GetPathNameSafe(E.Key.Get()),*GetPathNameSafe(E.Value.Get()),*GetPathNameSafe(E.Key.IsValid()?E.Key->GetConnection():nullptr)); return false; }
        return true;
    }
}

// Explicit fresh-world harness only; ordinary sessions never call this fixture.
bool RunBuildingSkillsNetworkCopyChecks(AFGCharacterPlayer* Player, int32 Phase)
{
    check(FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsRuntimeTest")));
    auto* World=Player->GetWorld();
    auto* PC=Cast<AFGPlayerController>(Player->GetController());
    auto* RCO=PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    auto* Factory=AFGBuildableSubsystem::Get(World);
    auto* Blueprints=AFGBlueprintSubsystem::Get(World);
    bool Passed=true;
    auto Check=[&](const TCHAR* Label,bool OK)
    { Passed &= OK; UE_LOG(LogBuildingSkills,Display,TEXT("RUNTIME ASSERT: %s: %s"),Label,OK?TEXT("PASS"):TEXT("FAIL")); };
    if (!RCO || !Factory || !Blueprints) return false;
    if (Phase==0)
    {
        CopySources.Reset(); CopyPlaced.Reset(); OriginalPowerLinks.Reset(); OriginalPipeLinks.Reset();
        const FVector Base=Player->GetActorLocation()+FVector(-4000,1000,1000);
        auto Spawn=[&](const TCHAR* Directory,const TCHAR* Name,const TCHAR* Recipe,const FVector& Offset,bool Deferred=false)
        {
            auto* Class=LoadClass<AFGBuildable>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/%s/%s.%s_C"),Directory,Name,Name));
            auto RecipeClass=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/%s.%s_C"),Recipe,Recipe));
            AFGBuildable* B=Class && RecipeClass ? Factory->BeginSpawnBuildable(Class,FTransform(Base+Offset)) : nullptr;
            if (B)
            {
                B->SetBuiltWithRecipe(RecipeClass);
                if (auto* Recipes=AFGRecipeManager::Get(World)) Recipes->AddAvailableRecipe(RecipeClass);
                if (!Deferred) B->FinishSpawning(B->GetActorTransform());
            }
            return B;
        };
        auto* Outlet=Spawn(TEXT("PowerPoleWallDouble"),TEXT("Build_PowerPoleWallDouble"),TEXT("Recipe_PowerPoleWallDouble"),{});
        auto* Machine1=Spawn(TEXT("ConstructorMk1"),TEXT("Build_ConstructorMk1"),TEXT("Recipe_ConstructorMk1"),FVector(1000,0,0));
        auto* Machine2=Spawn(TEXT("ConstructorMk1"),TEXT("Build_ConstructorMk1"),TEXT("Recipe_ConstructorMk1"),FVector(-1000,0,0));
        auto* External=Spawn(TEXT("ConstructorMk1"),TEXT("Build_ConstructorMk1"),TEXT("Recipe_ConstructorMk1"),FVector(0,-1500,0));
        TArray<UFGCircuitConnectionComponent*> OutletPorts;
        if (Outlet) for (auto* P:TInlineComponentArray<UFGCircuitConnectionComponent*>(Outlet)) if (!P->IsHidden()) OutletPorts.Add(P);
        auto* P1=Machine1?Machine1->FindComponentByClass<UFGPowerConnectionComponent>():nullptr;
        auto* P2=Machine2?Machine2->FindComponentByClass<UFGPowerConnectionComponent>():nullptr;
        auto* PE=External?External->FindComponentByClass<UFGPowerConnectionComponent>():nullptr;
        Check(TEXT("Double wall outlet and three real constructor power ports spawn"),!OutletPorts.IsEmpty() && P1 && P2 && PE);
        if (OutletPorts.IsEmpty() || !P1 || !P2 || !PE) return false;
        CopySources={Outlet,Machine1,Machine2};
        auto Wire=[&](UFGCircuitConnectionComponent* A,UFGCircuitConnectionComponent* B,bool Selected)
        {
            auto* W=Cast<AFGBuildableWire>(Spawn(TEXT("PowerLine"),TEXT("Build_PowerLine"),TEXT("Recipe_PowerLine"),{},true));
            if (!W || !A || !B) { if (W) W->Destroy(); return false; }
            const bool Connected=W->Connect(A,B); W->FinishSpawning(W->GetActorTransform());
            if (Selected) CopySources.Add(W);
            return Connected;
        };
        Check(TEXT("Double wall outlet connects two selected cables and one excluded branch"),Wire(OutletPorts[0],P1,true) && Wire(OutletPorts.Last(),P2,true) && Wire(OutletPorts[0],PE,false));
        auto* Pump=Spawn(TEXT("PipePump"),TEXT("Build_PipelinePump"),TEXT("Recipe_PipelinePump"),FVector(0,2500,0));
        TInlineComponentArray<UFGPipeConnectionComponent*> PumpPorts(Pump);
        Check(TEXT("Copy fixture pump exposes its native pipe ports"),Pump && PumpPorts.Num()==2);
        if (!Pump || PumpPorts.Num()!=2) return false;
        CopySources.Add(Pump);
        TArray<AFGBuildablePipeline*> SourcePipes;
        for (int32 I=0;I<2;++I)
        {
            auto* Pipe=Cast<AFGBuildablePipeline>(Spawn(TEXT("Pipeline"),TEXT("Build_Pipeline"),TEXT("Recipe_Pipeline"),{},true));
            if (!Pipe) return false;
            const FVector End=PumpPorts[I]->GetComponentLocation();
            const FVector Direction=PumpPorts[I]->GetForwardVector();
            Pipe->SetActorLocation(End);
            *Pipe->GetMutableSplinePointData()={FSplinePointData(FVector::ZeroVector,Direction*1000),FSplinePointData(Direction*1000,Direction*1000)};
            Pipe->FinishSpawning(Pipe->GetActorTransform());
            Pipe->GetPipeConnection0()->SetConnection(PumpPorts[I]);
            CopySources.Add(Pipe); SourcePipes.Add(Pipe);
        }
        auto* OutsidePipe=Cast<AFGBuildablePipeline>(Spawn(TEXT("Pipeline"),TEXT("Build_Pipeline"),TEXT("Recipe_Pipeline"),{},true));
        if (!OutsidePipe) return false;
        OutsidePipe->SetActorLocation(SourcePipes[0]->GetPipeConnection1()->GetComponentLocation());
        *OutsidePipe->GetMutableSplinePointData()={FSplinePointData(FVector::ZeroVector,FVector(1000,0,0)),FSplinePointData(FVector(1000,0,0),FVector(1000,0,0))};
        OutsidePipe->FinishSpawning(OutsidePipe->GetActorTransform());
        OutsidePipe->GetPipeConnection0()->SetConnection(SourcePipes[0]->GetPipeConnection1());
        RememberCopyLinks(OutsidePipe);
        Check(TEXT("Pump receives a selected power cable"),Wire(OutletPorts.Last(),Pump->FindComponentByClass<UFGPowerConnectionComponent>(),true));
        for (const auto& B:CopySources) RememberCopyLinks(B.Get());
        RememberCopyLinks(External);
        return Passed;
    }
    if (CopySources.IsEmpty()) return false;
    if (Phase==1)
    {
        FBSSelection Selection; for (const auto& B:CopySources) if (B.IsValid()) Selection.Buildings.Add(FBSBuildingRef::FromActor(B.Get()));
        auto* Clipboard=NewObject<UBSClipboard>(RCO); Clipboard->RequestId=FGuid::NewGuid();
        FString Error;
        const bool Captured=Clipboard->Capture(Selection,Error);
        Check(TEXT("Connected machines, double outlet, wires, pump and pipes capture"),Captured);
        Check(TEXT("Capturing connected buildings preserves all source links"),OriginalCopyLinksUnchanged());
        if (!Captured) { UE_LOG(LogBuildingSkills,Error,TEXT("Network copy fixture: %s"),*Error); return false; }
        RCO->Clipboards.Add(Clipboard);
        TArray<AFGBuildable*> Preview;
        Blueprints->LoadStoredBlueprint(Clipboard->Descriptor,FTransform::Identity,Preview,true);
        Check(TEXT("Network copy preview contains exactly selected buildings"),Preview.Num()==CopySources.Num());
        Check(TEXT("Loading network hologram preview leaves source connections intact"),OriginalCopyLinksUnchanged());
        auto* Holo=World->SpawnActorDeferred<AFGBlueprintHologram>(AFGBlueprintHologram::StaticClass(),FTransform::Identity,Player,Player);
        if (!Holo || Preview.IsEmpty()) { RCO->Clipboards.Remove(Clipboard); return false; }
        Holo->SetRecipe(UFGBlueprintSettings::Get()->mBlueprintRecipeClass.LoadSynchronous()); Holo->SetBuildClass(Preview[0]->GetClass());
        Holo->SetConstructionInstigator(Player); Holo->SetBlueprintDescriptor(Clipboard->Descriptor); Holo->FinishSpawning(FTransform::Identity);
        Holo->SetActorTransform(FTransform(FRotator(0,90,0),Player->GetActorLocation()+FVector(4000,3000,1000)));
        TArray<AActor*> Children;
        auto* Proxy=Cast<AFGBlueprintProxy>(Holo->Construct(Children,FNetConstructionID()));
        if (Proxy) for (auto* B:Proxy->GetBuildables()) CopyPlaced.Add(B);
        Check(TEXT("Network copy hologram constructs exactly the selected buildings"),CopyPlaced.Num()==CopySources.Num());
        TArray<FItemAmount> PreviewCost,PlacedCost;
        for (auto* Building : Preview)
        { TArray<FItemAmount> MemberCost; UBSClipboard::PreviewMemberCost(Blueprints,Building,MemberCost); PreviewCost.Append(MemberCost); }
        UFGInventoryLibrary::ConsolidateItemsAmount(PreviewCost);
        if (Proxy) Blueprints->CalculateBlueprintCost(Proxy->GetBuildables(),PlacedCost);
        bool SameCost=PreviewCost.Num()==PlacedCost.Num();
        for (const auto& Item : PlacedCost)
        {
            int32 Amount=0; for (const auto& Actual : PreviewCost) if (Actual.ItemClass==Item.ItemClass) Amount+=Actual.Amount;
            SameCost &= Amount==Item.Amount;
            if (Amount!=Item.Amount) UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK COST: %s preview=%d placed=%d"),*GetNameSafe(Item.ItemClass.Get()),Amount,Item.Amount);
        }
        Check(TEXT("Preview network ingredients equal native placed-network ingredients"),SameCost);
        if (auto* Edit=Player->FindComponentByClass<UBSEditModeComponent>())
        {
            TGuardValue<EBSDeduplication> ModeGuard(Edit->Deduplication,EBSDeduplication::Skip);
            auto* Overlap=World->SpawnActorDeferred<AFGBlueprintHologram>(AFGBlueprintHologram::StaticClass(),FTransform::Identity,Player,Player);
            Overlap->SetRecipe(UFGBlueprintSettings::Get()->mBlueprintRecipeClass.LoadSynchronous()); Overlap->SetBuildClass(Preview[0]->GetClass());
            Overlap->SetConstructionInstigator(Player); Overlap->SetBlueprintDescriptor(Clipboard->Descriptor); Overlap->FinishSpawning(FTransform::Identity);
            Overlap->SetActorTransform(Holo->GetActorTransform());
            Check(TEXT("Skip charges nothing for an overlapping pipe and power network"),Overlap->GetBaseCost().IsEmpty());
            TArray<AActor*> OverlapChildren;
            auto* Duplicate=Overlap->Construct(OverlapChildren,FNetConstructionID());
            Check(TEXT("Skip creates no duplicate machines, pipes or cables"),Duplicate==nullptr && OverlapChildren.IsEmpty());
            Overlap->Destroy();
            Edit->Deduplication=EBSDeduplication::Off;
            auto* Partial=World->SpawnActorDeferred<AFGBlueprintHologram>(AFGBlueprintHologram::StaticClass(),FTransform::Identity,Player,Player);
            Partial->SetRecipe(UFGBlueprintSettings::Get()->mBlueprintRecipeClass.LoadSynchronous()); Partial->SetBuildClass(Preview[0]->GetClass());
            Partial->SetConstructionInstigator(Player); Partial->SetBlueprintDescriptor(Clipboard->Descriptor); Partial->FinishSpawning(FTransform::Identity);
            FTransform Separate=Holo->GetActorTransform(); Separate.AddToTranslation(FVector(0,12000,0)); Partial->SetActorTransform(Separate);
            TArray<AActor*> SeparateChildren;
            auto* SeparateProxy=Cast<AFGBlueprintProxy>(Partial->Construct(SeparateChildren,FNetConstructionID()));
            AFGBuildablePipeline* Missing=nullptr;
            if (SeparateProxy) for (auto* Building : SeparateProxy->GetBuildables())
                if (auto* Pipe=Cast<AFGBuildablePipeline>(Building)) { Missing=Pipe; break; }
            Check(TEXT("Partial network overlap fixture resolves a separate pipe"),Missing!=nullptr);
            if (Missing)
            {
                TArray<FItemAmount> Expected; Blueprints->CalculateBlueprintCost({Missing},Expected);
                IFGDismantleInterface::Execute_Dismantle(Missing);
                Edit->Deduplication=EBSDeduplication::Skip;
                const auto Actual=Partial->GetBaseCost();
                bool Exact=Actual.Num()==Expected.Num();
                for (const auto& Item : Expected)
                { int32 Amount=0; for (const auto& Got : Actual) if (Got.ItemClass==Item.ItemClass) Amount+=Got.Amount; Exact &= Amount==Item.Amount; }
                Check(TEXT("Partial network Skip charges only the missing pipe's native cost"),Exact);
                const auto Repeated=Partial->GetBaseCost();
                bool Stable=Repeated.Num()==Actual.Num();
                for (const auto& Item : Actual)
                { int32 Amount=0; for (const auto& Got : Repeated) if (Got.ItemClass==Item.ItemClass) Amount+=Got.Amount; Stable &= Amount==Item.Amount; }
                Check(TEXT("Repeated overlap cost queries reuse the same correct price"),Stable);
            }
            Partial->Destroy();
        }
        else Check(TEXT("Network deduplication has a player edit component"),false);
        Holo->Destroy(); RCO->Clipboards.Remove(Clipboard);
    }
    Check(TEXT("Network copy leaves the original electrical and pipe graphs unchanged"),OriginalCopyLinksUnchanged());
    if (CopyPlaced.IsEmpty()) return false;
    TSet<AActor*> Placed; for (const auto& B:CopyPlaced) if (B.IsValid()) Placed.Add(B.Get());
    bool Independent=true, Reciprocal=true, PowerInfoLocal=true, ConnectionCounts=true;
    int32 WireCount=0, PipeLinkCount=0;
    TSet<int32> SourceCircuits,SourcePipeNetworks;
    for (const auto& B:CopySources) if (B.IsValid())
    {
        for (auto* P:TInlineComponentArray<UFGCircuitConnectionComponent*>(B.Get())) if (P->GetCircuitID()!=INDEX_NONE) SourceCircuits.Add(P->GetCircuitID());
        for (auto* P:TInlineComponentArray<UFGPipeConnectionComponent*>(B.Get())) if (P->GetPipeNetworkID()!=INDEX_NONE) SourcePipeNetworks.Add(P->GetPipeNetworkID());
    }
    bool IndependentIDs=true;
    for (const auto& Weak:CopyPlaced) if (auto* B=Weak.Get())
    {
        if (auto* W=Cast<AFGBuildableWire>(B))
        {
            ++WireCount;
            for (int32 I=0;I<2;++I)
            {
                auto* P=W->GetConnection(I);
                UE_LOG(LogBuildingSkills,Display,TEXT("COPY NETWORK wire=%s end=%d port=%s copied=%d"),*W->GetName(),I,*GetPathNameSafe(P),P && Placed.Contains(P->GetOwner()));
                Independent &= P && Placed.Contains(P->GetOwner());
                TArray<AFGBuildableWire*> Wires; if (P) P->GetWires(Wires);
                Reciprocal &= P && Wires.Contains(W) && Wires.Find(W)==Wires.FindLast(W);
            }
        }
        for (auto* P:TInlineComponentArray<UFGCircuitConnectionComponent*>(B))
        {
            TArray<AFGBuildableWire*> Wires; P->GetWires(Wires);
            ConnectionCounts &= P->GetNumConnections()==Wires.Num();
            UE_LOG(LogBuildingSkills,Display,TEXT("COPY NETWORK port=%s circuit=%d originalCircuit=%d wires=%d nativeCount=%d"),*P->GetPathName(),P->GetCircuitID(),SourceCircuits.Contains(P->GetCircuitID()),Wires.Num(),P->GetNumConnections());
            for (auto* W:Wires) { UE_LOG(LogBuildingSkills,Display,TEXT("COPY NETWORK port wire=%s copied=%d"),*GetPathNameSafe(W),W && Placed.Contains(W)); Independent &= W && Placed.Contains(W); Reciprocal &= W && (W->GetConnection(0)==P || W->GetConnection(1)==P); }
            TArray<UFGCircuitConnectionComponent*> Hidden; P->GetHiddenConnections(Hidden);
            for (auto* H:Hidden)
            {
                Independent &= H && Placed.Contains(H->GetOwner());
                TArray<UFGCircuitConnectionComponent*> Reverse; if (H) H->GetHiddenConnections(Reverse);
                Reciprocal &= H && Reverse.Contains(P) && Reverse.Find(P)==Reverse.FindLast(P);
            }
            if (auto* Power=Cast<UFGPowerConnectionComponent>(P);Power && Power->GetPowerInfo()) PowerInfoLocal &= Power->GetPowerInfo()->GetOwner()==B;
            IndependentIDs &= !SourceCircuits.Contains(P->GetCircuitID());
        }
        for (auto* P:TInlineComponentArray<UFGPipeConnectionComponent*>(B))
        {
            UE_LOG(LogBuildingSkills,Display,TEXT("COPY NETWORK pipe=%s network=%d originalNetwork=%d other=%s"),*P->GetPathName(),P->GetPipeNetworkID(),SourcePipeNetworks.Contains(P->GetPipeNetworkID()),*GetPathNameSafe(P->GetConnection()));
            if (auto* Other=P->GetConnection()) { ++PipeLinkCount; Independent &= Placed.Contains(Other->GetOwner()); Reciprocal &= Other->GetConnection()==P; }
            IndependentIDs &= !SourcePipeNetworks.Contains(P->GetPipeNetworkID());
            if (auto* Pipe=Cast<AFGBuildablePipeline>(B)) Independent &= P->GetFluidIntegrant()==Cast<IFGFluidIntegrantInterface>(Pipe);
        }
    }
    UE_LOG(LogBuildingSkills,Display,TEXT("COPY NETWORK totals wires=%d pipeLinks=%d independent=%d reciprocal=%d"),WireCount,PipeLinkCount,Independent,Reciprocal);
    Check(TEXT("Copied wires and pipe ports point only at copied buildings"),Independent && WireCount==3 && PipeLinkCount==4);
    Check(TEXT("Copied wire, hidden power and pipe connections are reciprocal without duplicates"),Reciprocal);
    Check(TEXT("Copied machines use their own power info components"),PowerInfoLocal);
    Check(TEXT("Copied power outlet connection counts exclude unselected cables"),ConnectionCounts);
    if (Phase>=2)
    {
        Check(TEXT("Settled copied power and pipe networks are distinct from the originals"),IndependentIDs);
        AFGBuildable* SourceOutlet=CopySources[0].Get();
        AFGBuildable* NewOutlet=nullptr;
        for (const auto& B:CopyPlaced) if (B.IsValid() && B->GetClass()==SourceOutlet->GetClass()) NewOutlet=B.Get();
        TArray<UFGCircuitConnectionComponent*> SourcePorts,NewPorts;
        if (NewOutlet) { SourceOutlet->GetComponents(SourcePorts); NewOutlet->GetComponents(NewPorts); }
        bool Internal=SourcePorts.Num()==2 && NewPorts.Num()==2 && SourcePorts[0]->GetCircuitID()!=INDEX_NONE && SourcePorts[0]->GetCircuitID()==SourcePorts[1]->GetCircuitID();
        Internal &= NewPorts.Num()==2 && NewPorts[0]->GetCircuitID()!=INDEX_NONE && NewPorts[0]->GetCircuitID()==NewPorts[1]->GetCircuitID();
        Check(TEXT("Both faces of the copied double outlet share one circuit like the original"),Internal);
        if (Phase==2)
        {
            // Supply a controlled producer to the native circuit simulation. No
            // no-power cheat: both outlet faces must receive real circuit power.
            bool Producer=false;
            auto Water=LoadClass<UFGItemDescriptor>(nullptr,TEXT("/Game/FactoryGame/Resource/RawResources/Water/Desc_Water.Desc_Water_C"));
            for (const auto& B:CopyPlaced) if (B.IsValid())
            {
                if (!Producer && B->GetClass()==CopySources[1]->GetClass())
                    if (auto* Port=B->FindComponentByClass<UFGPowerConnectionComponent>();Port && Port->GetPowerInfo())
                    { Port->GetPowerInfo()->SetBaseProduction(1000); Producer=true; }
                if (auto* Pipe=Cast<AFGBuildablePipeline>(B.Get()))
                {
                    AFGPipeSubsystem::Get(World)->TrySetNetworkFluidDescriptor(Pipe->GetPipeConnection0()->GetPipeNetworkID(),Water);
                    Pipe->GetFluidBox()->Content=2;
                }
            }
            Check(TEXT("Native copied-network simulation receives a controlled producer and water"),Producer && Water);
        }
        if (Phase>=3)
        {
            bool Powered=NewPorts.Num()==2, Fluids=true;
            for (auto* P:NewPorts) { auto* Power=Cast<UFGPowerConnectionComponent>(P); Powered &= Power && Power->HasPower(); }
            for (const auto& B:CopyPlaced) if (auto* Pipe=Cast<AFGBuildablePipeline>(B.Get()))
                Fluids &= FMath::IsFinite(Pipe->GetFluidBox()->Content) && Pipe->GetFluidBox()->Content>=0 && Pipe->GetPipeConnection0()->GetPipeNetworkID()!=INDEX_NONE;
            Check(TEXT("Both copied outlet faces retain power after thirty seconds of native simulation"),Powered);
            Check(TEXT("Copied pump and water pipes remain valid after background simulation"),Fluids);
        }
    }
    return Passed;
}
