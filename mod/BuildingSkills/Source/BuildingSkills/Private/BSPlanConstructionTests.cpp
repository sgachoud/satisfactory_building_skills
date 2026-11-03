#include "BSPlan.h"
#include "BSStick.h"
#include "BSConnected.h"
#include "BSGroups.h"
#include "FGPlayerController.h"
#include "FGPlayerState.h"
#include "BuildingSkills.h"
#include "FGCharacterPlayer.h"
#include "FGGameState.h"
#include "FGRecipeManager.h"
#include "FGRecipe.h"
#include "FGCustomizationRecipe.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Equipment/FGBuildGunPaint.h"
#include "FGBuildableSubsystem.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableRailroadSignal.h"
#include "Buildables/FGBuildableBlueprintDesigner.h"
#include "FGDismantleInterface.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"

bool RunBuildingSkillsPlanParallelChecks(TFunctionRef<void(const TCHAR*,bool)>);

// Explicit temporary-world regression only. Never invoked in normal gameplay.
bool RunBuildingSkillsPlanConstructionChecks(AFGCharacterPlayer* Player,TFunctionRef<void(const TCHAR*,bool)> Check)
{
    auto* World=Player->GetWorld(); auto* State=World->GetGameState<AFGGameState>();
    if (!State) return false;
    const bool Free=State->GetCheatNoCost(); State->SetCheatNoCost(true);
    TSet<AActor*> Before; for (TActorIterator<AFGBuildable> It(World);It;++It) Before.Add(*It);
    ON_SCOPE_EXIT {
        TArray<AActor*> Added;
        for (TActorIterator<AFGBuildable> It(World);It;++It) if (!Before.Contains(*It)) Added.Add(*It);
        for (int32 I=Added.Num()-1;I>=0;--I) if (IsValid(Added[I])) IFGDismantleInterface::Execute_Dismantle(Added[I]);
        State->SetCheatNoCost(Free);
    };
    bool Passed=true;
    auto Test=[&](const TCHAR* Name,bool Value) { Check(Name,Value); Passed &= Value; };
    Passed &= RunBuildingSkillsPlanParallelChecks(Test);
    auto* Store=ABSGroupSubsystem::Get(World); auto* PC=Cast<AFGPlayerController>(Player->GetController());
    auto* Remote=PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    if (Store && Remote)
    {
        FBSRailPlan Saved; Saved.Id=FGuid::NewGuid(); Saved.Name=TEXT("Delete plan regression"); Saved.Revision=2;
        FBSPlanOwnership::Assign(Saved,Player->GetPlayerState<AFGPlayerState>());
        Store->RailPlans.Add(Saved); Store->PlanHistories.FindOrAdd(Saved.Id).Undo.Add(Saved);
        FBSPlanCommand Command; Command.Plan=Saved.Id; Command.Action=EBSPlanAction::DeletePlan; Command.Revision=1;
        Remote->Server_PlanCommand(Command);
        Test(TEXT("Stale plan deletion preserves the plan"),Store->RailPlans.ContainsByPredicate([&](const auto& V) { return V.Id==Saved.Id; }));
        Command.Revision=2; Remote->Server_PlanCommand(Command);
        auto* Empty=NewObject<UBSPlanComponent>(Player); Empty->Active=true; Empty->CurrentId=Saved.Id; Empty->Plans={Saved};
        Empty->Receive({}, {}, {});
        Test(TEXT("Deletion snapshot leaves the saved-plan list empty"),Empty->Plans.IsEmpty() && !Empty->Current());
        Test(TEXT("Confirmed current deletion removes plan and its history"),!Store->RailPlans.ContainsByPredicate([&](const auto& V) { return V.Id==Saved.Id; }) && !Store->PlanHistories.Contains(Saved.Id));
    }
    else Test(TEXT("Authoritative deletion fixture is available"),false);
    const FVector Origin=Player->GetActorLocation()+FVector(0,0,100000);
    for (int32 I=0;I<=int32(EBSPlanBuildable::Hypertube);++I)
    {
        const auto Type=EBSPlanBuildable(I); const auto Recipe=FBSPlanBuild::Recipe(Type);
        Test(*FString::Printf(TEXT("Plan recipe exists: %s"),FBSPlanBuild::Name(Type)),Recipe!=nullptr);
        if (!Recipe) continue;
        AFGRecipeManager::Get(World)->AddAvailableRecipe(Recipe);
        FBSRailPlan Plan; Plan.Id=FGuid::NewGuid(); Plan.Name=TEXT("Spline construction regression");
        Plan.Construction.Main=Type; Plan.SegmentLength=3000;
        Plan.Construction.PipeLength=Plan.Construction.BeltLength=Plan.Construction.HypertubeLength=3000;
        FBSPlanNode A; A.Id=FGuid::NewGuid(); A.Position=Origin+FVector(0,I*10000,0);
        auto B=A; B.Id=FGuid::NewGuid(); B.Position.X+=9000; Plan.Nodes={A,B};
        FBSPlanEdge E; E.Id=FGuid::NewGuid(); E.Start=A.Id; E.End=B.Id; E.Trace=EBSPlanTrace::Straight; Plan.Edges={E};
        if (I%2) Plan.ReversedEdges.Add(E.Id);
        FString Error;
        const bool Built=FBSPlanBuild::Build(Player,Plan,Error);
        UE_LOG(LogBuildingSkills,Display,TEXT("PLAN TYPE TEST: %s result=%d %s"),FBSPlanBuild::Name(Type),Built,*Error);
        Test(*FString::Printf(TEXT("Native segmented construction: %s"),FBSPlanBuild::Name(Type)),Built);
        if (!Built) continue;
        const auto Class=AFGBuildable::GetBuildableClassFromRecipe(Recipe);
        int32 Count=0,Joined=0; bool Flow=true;
        for (TActorIterator<AFGBuildable> It(World,Class);It;++It)
        {
            if (Before.Contains(*It) || FMath::Abs(It->GetActorLocation().Y-A.Position.Y)>1) continue;
            ++Count;
            for (auto* C:TInlineComponentArray<UFGFactoryConnectionComponent*>(*It))
            {
                if (C->IsConnected()) ++Joined;
                if (C->GetDirection()==EFactoryConnectionDirection::FCD_OUTPUT) Flow &= (I%2 ? C->GetForwardVector().X<-.999 : C->GetForwardVector().X>.999);
            }
            for (auto* C:TInlineComponentArray<UFGPipeConnectionComponentBase*>(*It)) if (C->GetConnection()) ++Joined;
        }
        Test(TEXT("Plan builds three native segments"),Count==3);
        if (Type!=EBSPlanBuildable::Rail)
        {
            Test(TEXT("Both interior spline joins are connected reciprocally"),Joined==4);
            int32 Supports=0;
            for (TActorIterator<AFGBuildable> It(World);It;++It)
            {
                if (Before.Contains(*It) || It->IsA(Class)) continue;
                // Pipe support actors have an offset origin. Count their snap
                // ports at the route, not the decorative mesh's origin.
                auto OnLine=[&](FVector At) { return FMath::Abs(At.Y-A.Position.Y)<1 && FMath::Abs(At.Z-A.Position.Z)<1 && At.X>=A.Position.X-1 && At.X<=B.Position.X+1; };
                bool OnRoute=false;
                for (auto* C:TInlineComponentArray<UFGFactoryConnectionComponent*>(*It))
                    OnRoute |= C->GetDirection()==EFactoryConnectionDirection::FCD_SNAP_ONLY && OnLine(C->GetComponentLocation());
                for (auto* C:TInlineComponentArray<UFGPipeConnectionComponentBase*>(*It))
                    OnRoute |= C->GetPipeConnectionType()==EPipeConnectionType::PCT_SNAP_ONLY && OnLine(C->GetComponentLocation());
                Supports+=OnRoute;
            }
            UE_LOG(LogBuildingSkills,Display,TEXT("PLAN SUPPORT TEST: %s supports=%d"),FBSPlanBuild::Name(Type),Supports);
            Test(TEXT("Three spline segments share four native endpoint supports"),Supports==4);
        }
        Test(TEXT("Belt flow follows the plan direction arrow"),Flow);
        // Extend the same line into a port already present in the world.
        Plan.Nodes[0].Position=Plan.Nodes[1].Position; Plan.Nodes[1].Position.X+=9000;
        Test(TEXT("Plan extension connects to the existing same-type spline"),FBSPlanBuild::Build(Player,Plan,Error));
        if (Type!=EBSPlanBuildable::Rail)
        {
            TMap<UFGFactoryConnectionComponent*,UFGFactoryConnectionComponent*> BeltLinks;
            TMap<UFGPipeConnectionComponentBase*,UFGPipeConnectionComponentBase*> PipeLinks;
            int32 BeforeCount=0;
            for (TActorIterator<AFGBuildable> It(World);It;++It)
            {
                ++BeforeCount;
                for (auto* C:TInlineComponentArray<UFGFactoryConnectionComponent*>(*It)) BeltLinks.Add(C,C->GetConnection());
                for (auto* C:TInlineComponentArray<UFGPipeConnectionComponentBase*>(*It)) PipeLinks.Add(C,C->GetConnection());
            }
            Test(TEXT("An occupied world endpoint rejects construction before mutation"),!FBSPlanBuild::Build(Player,Plan,Error) && Error.Contains(TEXT("occupied")));
            int32 AfterCount=0; for (TActorIterator<AFGBuildable> It(World);It;++It) ++AfterCount;
            bool Intact=BeforeCount==AfterCount;
            for (const auto& Link:BeltLinks) Intact &= IsValid(Link.Key) && Link.Key->GetConnection()==Link.Value;
            for (const auto& Link:PipeLinks) Intact &= IsValid(Link.Key) && Link.Key->GetConnection()==Link.Value;
            Test(TEXT("Rejected duplicate placement preserves existing buildings and networks"),Intact);
        }
    }
    {
        FBSRailPlan Mixed; Mixed.Id=FGuid::NewGuid(); Mixed.Name=TEXT("Mixed native build");
        Mixed.Construction.Parallels={EBSPlanBuildable::PipeMk2,EBSPlanBuildable::BeltMk6,EBSPlanBuildable::Hypertube};
        Mixed.Parallels={FVector(0,1000,0),FVector(0,2000,0),FVector(0,3000,0)};
        Mixed.Construction.PipeLength=3000; Mixed.Construction.BeltLength=4000; Mixed.Construction.HypertubeLength=5000;
        Mixed.ParallelReversed={false,true,false}; Mixed.PlaceBlockSignal=true;
        const auto Signal=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/Train/Signal/Recipe_RailroadBlockSignal.Recipe_RailroadBlockSignal_C"));
        if (Signal) AFGRecipeManager::Get(World)->AddAvailableRecipe(Signal);
        FBSPlanNode A; A.Id=FGuid::NewGuid(); A.Position=Origin+FVector(0,120000,0);
        auto B=A; B.Id=FGuid::NewGuid(); B.Position.X+=24000; Mixed.Nodes={A,B};
        FBSPlanEdge E; E.Id=FGuid::NewGuid(); E.Start=A.Id; E.End=B.Id; E.Trace=EBSPlanTrace::Straight; Mixed.Edges={E};
        FString Error; const bool Built=FBSPlanBuild::Build(Player,Mixed,Error);
        UE_LOG(LogBuildingSkills,Display,TEXT("PLAN MIXED TEST: result=%d %s"),Built,*Error);
        Test(TEXT("One plan constructs all four native spline families together"),Built);
        int32 Signals=0; for (TActorIterator<AFGBuildableRailroadSignal> It(World);It;++It) if (!Before.Contains(*It)) ++Signals;
        Test(TEXT("Mixed plan block signals are placed only on its rail joints"),Built && Signals==2);
    }
    // Designer foundations remain actor-backed; they must not depend on the
    // ordinary world spatial index. Use native membership, not test-only refs.
    const auto DesignerClass=LoadClass<AFGBuildableBlueprintDesigner>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/BlueprintDesigner/Build_BlueprintDesigner.Build_BlueprintDesigner_C"));
    auto* System=AFGBuildableSubsystem::Get(World);
    const FTransform Pose(Origin+FVector(20000,0,0));
    auto* Designer=DesignerClass ? Cast<AFGBuildableBlueprintDesigner>(System->BeginSpawnBuildable(DesignerClass,Pose)) : nullptr;
    if (Designer) Designer->FinishSpawning(Pose);
    Test(TEXT("Designer connected fixture is available"),Designer!=nullptr);
    if (Designer)
    {
        auto Class=LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x1_01.Build_Foundation_8x1_01_C"));
        TArray<AFGBuildable*> Members;
        for (int32 I=0;Class && I<2;++I)
        {
            const FTransform At(Pose.GetLocation()+FVector(I*800,0,100));
            auto* B=System->BeginSpawnBuildable(Class,At);
            if (!B) continue;
            B->SetInsideBlueprintDesigner(Designer); B->FinishSpawning(At);
            if (!Designer->GetBuildablesInBlueprintDesigner().Contains(B)) Designer->OnBuildableConstructedInsideDesigner(B); Members.Add(B);
        }
        FString Error; FBSConnectedSettings Settings;
        const auto Found=Members.IsEmpty() ? TArray<FBSBuildingRef>() : FBSConnected::Find(World,FBSBuildingRef::FromActor(Members[0]),Settings,Error);
        Test(TEXT("Connected includes both touching actor-backed designer foundations"),Members.Num()==2 && Found.Num()==2 && Found.Contains(FBSBuildingRef::FromActor(Members[1])));
        auto* MachineClass=LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/ConstructorMk1/Build_ConstructorMk1.Build_ConstructorMk1_C"));
        auto* Outside=MachineClass ? System->BeginSpawnBuildable(MachineClass,Pose) : nullptr;
        if (Outside)
        {
            Outside->FinishSpawning(Pose); Settings.Scope=EBSConnectedScope::All; Settings.IgnoreSwatch=true; Settings.IgnoreMaterial=true;
            const auto Crossing=FBSConnected::Find(World,FBSBuildingRef::FromActor(Outside),Settings,Error);
            Test(TEXT("Connected All preserves touching behavior across the designer boundary"),Members.Num()==2 && Crossing.Contains(FBSBuildingRef::FromActor(Members[0])) && Crossing.Contains(FBSBuildingRef::FromActor(Members[1])));
        }
        else Test(TEXT("External touching designer fixture is available"),false);
        if (Remote && Members.Num()==2)
        {
            auto* Gun=Player->GetBuildGun();
            const auto Position=Player->GetActorLocation();
            auto* PreviousBuild=Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState());
            auto* PreviousPaint=Cast<UFGBuildGunStatePaint>(Gun->GetCurrentState());
            const TSubclassOf<UFGRecipe> PreviousRecipe=PreviousBuild ? PreviousBuild->GetActiveRecipe() : nullptr;
            const TSubclassOf<UFGCustomizationRecipe> PreviousSwatch=PreviousPaint ? PreviousPaint->GetActiveRecipe() : nullptr;
            ON_SCOPE_EXIT {
                Player->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
                if (PreviousBuild) Gun->GotoBuildState(PreviousRecipe);
                else if (PreviousPaint) Gun->GotoPaintState(PreviousSwatch,true);
                else Gun->GotoDismantleState();
            };
            Player->SetActorLocation(Pose.GetLocation()+FVector(0,-500,500),false,nullptr,ETeleportType::TeleportPhysics);
            const auto Swatch=LoadClass<UFGCustomizationRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/-Shared/Customization/Swatches/Recipe_Swatch_Slot1.Recipe_Swatch_Slot1_C"));
            const auto Material=LoadClass<UFGCustomizationRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/-Shared/Customization/Materials/Recipe_Material_Foundation_Concrete.Recipe_Material_Foundation_Concrete_C"));
            AFGRecipeManager::Get(World)->AddAvailableRecipe(Swatch); AFGRecipeManager::Get(World)->AddAvailableRecipe(Material);
            Settings.Scope=EBSConnectedScope::Strict;
            Gun->GotoPaintState(Swatch,true); Remote->LastConnectedPaint=-100;
            Remote->Server_PaintConnected(FBSBuildingRef::FromActor(Members[0]),Settings,Swatch);
            const auto Desc=UFGCustomizationRecipe::GetCustomizationDescriptor(Swatch);
            Test(TEXT("Connected paint applies swatch to both designer foundations"),Desc && Members[0]->GetCustomizationData_Native().SwatchDesc==Desc && Members[1]->GetCustomizationData_Native().SwatchDesc==Desc);
            Gun->GotoPaintState(Material,true); Remote->LastConnectedPaint=-100;
            Remote->Server_PaintConnected(FBSBuildingRef::FromActor(Members[0]),Settings,Material);
            int32 Replaced=0;
            for (auto* Member:Designer->GetBuildablesInBlueprintDesigner())
                if (IsValid(Member) && !Members.Contains(Member) && Member->GetBlueprintDesigner()==Designer) ++Replaced;
            Test(TEXT("Connected material replacement retains both designer members"),Replaced==2);
        }
    }
    return Passed;
}
