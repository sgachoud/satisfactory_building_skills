#include "BSPlan.h"
#include "BSDefaultsWidget.h"
#include "BSTransform.h"
#include "BSStick.h"
#include "Configuration/ConfigManager.h"
#include "Engine/GameInstance.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGGameState.h"
#include "FGDismantleInterface.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Hologram/FGRailroadTrackHologram.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "Widgets/SBoxPanel.h"

bool RunBuildingSkillsPlanPolishChecks(AFGCharacterPlayer* Player,TFunctionRef<void(const TCHAR*,bool)> Check)
{
    auto* World=Player->GetWorld(); bool Passed=true;
    auto Test=[&](const TCHAR* Name,bool Value) { Check(Name,Value); Passed &= Value; };
    auto* Config=World->GetGameInstance()->GetSubsystem<UConfigManager>();
    auto* Root=Config ? Config->GetConfigurationRootSection({TEXT("BuildingSkills"),TEXT("")}) : nullptr;
    auto* Parent=CreateWidget<UBSDefaultsWidget>(Cast<APlayerController>(Player->GetController()));
    auto* Editor=Root ? Root->CreateEditorWidget(Parent) : nullptr;
    Test(TEXT("Mods defaults have a registered working editor with all setting families"),Cast<UBSDefaultsWidget>(Editor) && Root->SectionProperties.Num()>=35);
    if (Editor) Test(TEXT("Mods defaults build their complete widget tree"),Editor->TakeWidget()->GetChildren()->Num()>0);
    Test(TEXT("Rail defaults are 80 m and bounded by native minimum and maximum"),FBSRailPlan{}.SegmentLength==8000 && FBSPlanBuild::MinimumLength()>0 && FBSPlanBuild::MinimumLength()<8000 && FBSPlanBuild::MaximumLength()>=8000);
    auto* Native=Cast<AFGRailroadTrackHologram>(AFGHologram::SpawnHologramFromRecipe(FBSPlanBuild::Recipe(),Player->GetBuildGun(),Player->GetActorLocation(),Player));
    Test(TEXT("Menu minimum matches the initialized native rail hologram"),Native && FMath::IsNearlyEqual(FBSPlanBuild::MinimumLength(),FBSStick::PlanLimits(Native).Z,1.0));
    if (Native) Native->Destroy();
    auto* State=World->GetGameState<AFGGameState>(); if (!State) return false;
    const bool Free=State->GetCheatNoCost(); State->SetCheatNoCost(true); ON_SCOPE_EXIT { State->SetCheatNoCost(Free); };
    auto Alive=[&]() { TSet<AFGBuildableRailroadTrack*> Out; for (TActorIterator<AFGBuildableRailroadTrack> It(World);It;++It) if (!It->GetIsDismantled()) Out.Add(*It); return Out; };
    for (bool RemoveSource:{false,true})
    {
        const auto Before=Alive(); const FVector Origin=Player->GetActorLocation()+FVector(0,0,120000);
        FBSRailPlan Source; Source.Id=FGuid::NewGuid(); Source.Name=TEXT("Existing rail"); Source.Remainder=EBSPlanRemainder::Reduce;
        FBSPlanNode A; A.Id=FGuid::NewGuid(); A.Position=Origin+FVector(4000,0,0);
        auto B=A; B.Id=FGuid::NewGuid(); B.Position+=FVector(8000,8000,0); B.Direction=FVector::RightVector;
        FBSPlanEdge E; E.Id=FGuid::NewGuid(); E.Start=A.Id; E.End=B.Id;
        Source.Nodes={A,B}; Source.Edges={E}; FString Error;
        const bool SourceBuilt=FBSPlanBuild::Build(Player,Source,Error);
        TArray<AFGBuildableRailroadTrack*> Sources;
        for (auto* T:Alive()) if (!Before.Contains(T)) Sources.Add(T);
        UFGRailroadTrackConnectionComponent* Joint=nullptr;
        for (auto* T:Sources) for (int32 End=0;End<2;++End) if (T->GetConnection(End)->GetConnectorLocation().Equals(A.Position,.5)) Joint=T->GetConnection(End);
        Test(TEXT("Live rail intersection fixture builds"),SourceBuilt && Joint);
        if (RemoveSource) for (auto* T:Sources) IFGDismantleInterface::Execute_Dismantle(T);
        const auto BeforeMain=Alive();
        FBSRailPlan Main; Main.Id=FGuid::NewGuid(); Main.Name=TEXT("Detached geometry");
        for (double X:{0.,4000.,16000.}) { FBSPlanNode N; N.Id=FGuid::NewGuid(); N.Position=Origin+FVector(X,0,0); Main.Nodes.Add(N); }
        for (int32 I=1;I<3;++I) { FBSPlanEdge Edge; Edge.Id=FGuid::NewGuid(); Edge.Start=Main.Nodes[I-1].Id; Edge.End=Main.Nodes[I].Id; Edge.Trace=EBSPlanTrace::Straight; Main.Edges.Add(Edge); }
        const bool Built=FBSPlanBuild::Build(Player,Main,Error); int32 Count=0;
        for (auto* T:Alive()) if (!BeforeMain.Contains(T)) ++Count;
        Test(RemoveSource ? TEXT("Deleting a snapping source leaves the plan buildable") : TEXT("Build discovers a live rail at an interior node and forces a connected joint"),
            Built && Count==(RemoveSource ? 2 : 3) && (RemoveSource || (Joint && Joint->GetConnections().Num()>0)));
        for (auto* T:Alive()) if (!Before.Contains(T)) IFGDismantleInterface::Execute_Dismantle(T);
    }
    return Passed;
}
