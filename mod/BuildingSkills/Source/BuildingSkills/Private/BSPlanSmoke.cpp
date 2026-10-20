#include "BSPlan.h"
#include "BSGroups.h"
#include "BSStick.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGPlayerState.h"
#include "FGRecipeManager.h"
#include "FGInventoryComponent.h"
#include "FGInventoryLibrary.h"
#include "FGCentralStorageSubsystem.h"
#include "FGGameState.h"
#include "Misc/ScopeExit.h"
#include "FGDismantleInterface.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Hologram/FGRailroadTrackHologram.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Buildables/FGBuildableRailroadSignal.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "EnhancedPlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UI/FGGameUI.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Views/SListView.h"
#include "Misc/ConfigCacheIni.h"
#include "TimerManager.h"

bool RunBuildingSkillsPlanMathChecks(TFunctionRef<void(const TCHAR*,bool)> Check);
bool RunBuildingSkillsPlanPolishChecks(AFGCharacterPlayer*,TFunctionRef<void(const TCHAR*,bool)> Check);

// Only called by the explicit temporary-world runtime fixture.
static bool RunPlanPaymentChecks(AFGCharacterPlayer* Player,TFunctionRef<void(const TCHAR*,bool)> Check)
{
    auto* World=Player->GetWorld(); auto* Inventory=Player->GetInventory();
    auto* Central=AFGCentralStorageSubsystem::Get(World); auto* State=World->GetGameState<AFGGameState>();
    auto* Owner=Player->GetPlayerState<AFGPlayerState>();
    auto* Property=Central ? FindFProperty<FArrayProperty>(Central->GetClass(),TEXT("mStoredItems")) : nullptr;
    if (!Inventory || !Central || !State || !Owner || !Property) { Check(TEXT("Depot payment fixture is available"),false); return false; }
    auto* Stored=Property->ContainerPtrToValuePtr<TArray<FItemAmount>>(Central);
    const auto OriginalDepot=*Stored; const bool OriginalFree=State->GetCheatNoCost(),OriginalOrder=Owner->GetTakeFromInventoryBeforeCentralStorage();
    FBSRailPlan Loop; Loop.Id=FGuid::NewGuid(); Loop.Name=TEXT("Closed loop payment regression");
    Loop.PlaceBlockSignal=true; Loop.SignalEvery=2;
    auto SignalRecipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/Train/Signal/Recipe_RailroadBlockSignal.Recipe_RailroadBlockSignal_C"));
    if (!SignalRecipe) { Check(TEXT("Signal payment fixture recipe is available"),false); return false; }
    AFGRecipeManager::Get(World)->AddAvailableRecipe(SignalRecipe);
    const FVector Origin=Player->GetActorLocation()+FVector(0,0,30000);
    const FVector Directions[]={FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0),FVector(1,0,0)};
    const FVector Positions[]={FVector(10000,0,0),FVector(0,10000,0),FVector(-10000,0,0),FVector(0,-10000,0)};
    for (int32 I=0;I<4;++I) { FBSPlanNode N; N.Id=FGuid::NewGuid(); N.Position=Origin+Positions[I]; N.Direction=Directions[I]; Loop.Nodes.Add(N); }
    for (int32 I=0;I<4;++I) { const auto& A=Loop.Nodes[I]; const auto& B=Loop.Nodes[(I+1)%4]; FBSPlanEdge E; E.Id=FGuid::NewGuid(); E.Start=A.Id; E.End=B.Id; E.Trace=EBSPlanTrace::Bezier; E.Controls={A.Position+A.Direction*5522.847498,B.Position-B.Direction*5522.847498}; Loop.Edges.Add(E); }
    TArray<FItemAmount> Cost; int32 ExpectedSegments=0;
    auto* Template=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(FBSPlanBuild::Recipe(),Player->GetBuildGun(),Origin,Player));
    if (!Template) { Check(TEXT("Payment fixture creates a rail template"),false); return false; }
    const double MaxLength=FBSStick::MaxLength(Template),MinLength=FBSStick::PlanLimits(Template).Z; Template->Destroy();
    TArray<FBSPlanPath> Paths; FString PathError;
    if (!FBSPlanSegments::Paths(Loop,Paths,PathError)) { Check(TEXT("Closed-loop path assembles once"),false); return false; }
    for (const auto& Path:Paths)
    {
        TArray<TArray<FSplinePointData>> Parts;
        if (!FBSPlanSegments::Split(Path.Points,Loop.SegmentLength,Loop.Remainder,MinLength,MaxLength,Parts,PathError)) { Check(TEXT("Closed-loop cost fixture splits valid rails"),false); return false; }
        ExpectedSegments+=Parts.Num();
        for (const auto& Part:Parts)
        {
            auto* H=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(FBSPlanBuild::Recipe(),Player->GetBuildGun(),Part[0].Location,Player));
            if (!H) { Check(TEXT("Payment fixture prepares every segment"),false); return false; }
            FBSStick::PlanGeometry(H,Part); Cost.Append(H->GetCost(true)); H->Destroy();
        }
    }
    auto* SignalTemplate=AFGHologram::SpawnHologramFromRecipe(SignalRecipe,Player->GetBuildGun(),Origin,Player);
    if (!SignalTemplate) { Check(TEXT("Signal payment cost is available"),false); return false; }
    const int32 ExpectedSignals=ExpectedSegments/Loop.SignalEvery;
    for (int32 I=0;I<ExpectedSignals;++I) Cost.Append(SignalTemplate->GetCost(true));
    SignalTemplate->Destroy();
    UFGInventoryLibrary::ConsolidateItemsAmount(Cost);
    bool ValidCost=!Cost.IsEmpty(); for (const auto& Item:Cost) ValidCost &= Item.Amount>1;
    Check(TEXT("Closed-loop fixture has a positive full-plan material cost"),ValidCost); if (!ValidCost) return false;
    TArray<int32> OriginalInventory; for (const auto& Item:Cost) OriginalInventory.Add(Inventory->GetNumItems(Item.ItemClass));
    ON_SCOPE_EXIT {
        *Stored=OriginalDepot; State->SetCheatNoCost(OriginalFree); Owner->SetTakeFromInventoryBeforeCentralStorage(OriginalOrder);
        for (int32 I=0;I<Cost.Num();++I) { Inventory->Remove(Cost[I].ItemClass,Inventory->GetNumItems(Cost[I].ItemClass)); if (OriginalInventory[I]) Inventory->AddStack(FInventoryStack(OriginalInventory[I],Cost[I].ItemClass)); }
    };
    bool Passed=true;
    const TCHAR* Cases[]={TEXT("inventory only"),TEXT("Depot only"),TEXT("split inventory and Depot"),TEXT("inventory first"),TEXT("Depot first"),TEXT("insufficient combined materials"),TEXT("No Build Cost")};
    for (int32 Case=0;Case<7;++Case)
    {
        State->SetCheatNoCost(Case==6); Owner->SetTakeFromInventoryBeforeCentralStorage(Case!=4); Stored->Reset();
        TArray<int32> BeforeInventory,BeforeDepot;
        for (int32 I=0;I<Cost.Num();++I)
        {
            const auto& Item=Cost[I]; Inventory->Remove(Item.ItemClass,Inventory->GetNumItems(Item.ItemClass));
            const int32 Bag=Case==1 || Case==6 ? 0 : Case==2 || Case==5 ? Item.Amount/2 : Item.Amount;
            const int32 Depot=Case==0 || Case==6 ? 0 : Case==2 || Case==5 ? Item.Amount-Bag-(Case==5 && I==0 ? 1 : 0) : Item.Amount;
            if (Bag) Inventory->AddStack(FInventoryStack(Bag,Item.ItemClass)); if (Depot) Stored->Emplace(Item.ItemClass,Depot);
            BeforeInventory.Add(Bag); BeforeDepot.Add(Depot);
        }
        TSet<AFGBuildableRailroadTrack*> Existing;
        for (TActorIterator<AFGBuildableRailroadTrack> It(World);It;++It) if (!It->GetIsDismantled()) Existing.Add(*It);
        TSet<AFGBuildableRailroadSignal*> ExistingSignals;
        for (TActorIterator<AFGBuildableRailroadSignal> It(World);It;++It) if (!It->GetIsDismantled()) ExistingSignals.Add(*It);
        FString Error; const bool Built=FBSPlanBuild::Build(Player,Loop,Error),Expected=Case!=5;
        Check(*FString::Printf(TEXT("Closed-loop build: %s"),Cases[Case]),Built==Expected); Passed &= Built==Expected;
        if (Built!=Expected) UE_LOG(LogBuildingSkills,Display,TEXT("PLAN PAYMENT: %s: %s"),Cases[Case],*Error);
        bool Paid=true;
        for (int32 I=0;I<Cost.Num();++I)
        {
            const int32 Charge=Expected && Case!=6 ? Cost[I].Amount : 0;
            const int32 BagCharge=Case==4 ? FMath::Max(0,Charge-BeforeDepot[I]) : FMath::Min(Charge,BeforeInventory[I]);
            Paid &= Inventory->GetNumItems(Cost[I].ItemClass)==BeforeInventory[I]-BagCharge && Central->GetNumItemsFromCentralStorage(Cost[I].ItemClass)==BeforeDepot[I]-(Charge-BagCharge);
        }
        Check(*FString::Printf(TEXT("Exact material deduction: %s"),Cases[Case]),Paid); Passed &= Paid;
        TArray<AFGBuildableRailroadTrack*> Created; bool Connected=true;
        for (TActorIterator<AFGBuildableRailroadTrack> It(World);It;++It) if (!It->GetIsDismantled() && !Existing.Contains(*It))
        { Created.Add(*It); for (int32 End=0;End<2;++End) Connected &= It->GetConnection(End)->GetConnections().Num()==1; }
        TArray<AFGBuildableRailroadSignal*> CreatedSignals;
        for (TActorIterator<AFGBuildableRailroadSignal> It(World);It;++It) if (!It->GetIsDismantled() && !ExistingSignals.Contains(*It)) CreatedSignals.Add(*It);
        const bool Once=Created.Num()==(Expected ? ExpectedSegments : 0) && CreatedSignals.Num()==(Expected ? ExpectedSignals : 0) && Connected;
        Check(*FString::Printf(TEXT("Closed loop builds each segment once and closes every join: %s"),Cases[Case]),Once); Passed &= Once;
        if (Case==5) Check(TEXT("Insufficient materials report the item and both available counts"),Error.Contains(TEXT("inventory")) && Error.Contains(TEXT("Depot")) && Error.Contains(TEXT("need")));
        for (auto* Signal:CreatedSignals) IFGDismantleInterface::Execute_Dismantle(Signal);
        for (auto* Track:Created) IFGDismantleInterface::Execute_Dismantle(Track);
    }
    return Passed;
}

static bool RunPlanSegmentChecks(AFGCharacterPlayer* Player,TFunctionRef<void(const TCHAR*,bool)> Check)
{
    auto* World=Player->GetWorld(); auto* State=World->GetGameState<AFGGameState>();
    if (!State) { Check(TEXT("Segment build fixture has a game state"),false); return false; }
    const bool Free=State->GetCheatNoCost(); State->SetCheatNoCost(true); ON_SCOPE_EXIT { State->SetCheatNoCost(Free); };
    const double Max=FBSPlanBuild::MaximumLength(); bool Passed=Max>0;
    FBSRailPlan Fixture; Fixture.Id=FGuid::NewGuid(); Fixture.Name=TEXT("Whole path segmentation"); Fixture.SegmentLength=Max;
    const FVector Origin=Player->GetActorLocation()+FVector(0,0,60000);
    for (int32 I=0;I<3;++I) { FBSPlanNode N; N.Id=FGuid::NewGuid(); N.Position=Origin+FVector(I*Max*.75,0,0); Fixture.Nodes.Add(N); }
    for (int32 I=0;I<2;++I) { FBSPlanEdge E; E.Id=FGuid::NewGuid(); E.Start=Fixture.Nodes[I].Id; E.End=Fixture.Nodes[I+1].Id; E.Trace=EBSPlanTrace::Straight; Fixture.Edges.Add(E); }
    for (int32 Case=0;Case<4;++Case)
    {
        auto Candidate=Fixture; Candidate.Remainder=Case==0 ? EBSPlanRemainder::Last : Case==1 ? EBSPlanRemainder::Reduce : EBSPlanRemainder::Increase;
        if (Case==3) Candidate.SegmentLength=Max*.7;
        TSet<AFGBuildableRailroadTrack*> Existing;
        for (TActorIterator<AFGBuildableRailroadTrack> It(World);It;++It) if (!It->GetIsDismantled()) Existing.Add(*It);
        FString Error; const bool Built=FBSPlanBuild::Build(Player,Candidate,Error),Expected=Case!=2;
        TArray<AFGBuildableRailroadTrack*> Created;
        for (TActorIterator<AFGBuildableRailroadTrack> It(World);It;++It) if (!It->GetIsDismantled() && !Existing.Contains(*It)) Created.Add(*It);
        bool Geometry=Created.Num()==(Expected ? 2 : 0);
        if (Expected && Created.Num()==2)
        {
            Created.Sort([](const auto& A,const auto& B) { return A.GetLength()>B.GetLength(); });
            Geometry &= FMath::Abs(Created[0]->GetLength()-(Case==0 ? Max : Max*.75))<1 && FMath::Abs(Created[1]->GetLength()-(Case==0 ? Max*.5 : Max*.75))<1;
            int32 Connections=0; for (auto* Track:Created) for (int32 End=0;End<2;++End) Connections+=Track->GetConnection(End)->GetConnections().Num();
            Geometry &= Connections==2;
        }
        const bool Good=Built==Expected && Geometry;
        Check(*FString::Printf(TEXT("Whole-path native segment policy %d has exact lengths, connections and atomic failure"),Case),Good); Passed &= Good;
        if (!Good) UE_LOG(LogBuildingSkills,Display,TEXT("PLAN SEGMENTS case %d: built=%d tracks=%d: %s"),Case,Built,Created.Num(),*Error);
        for (auto* Track:Created) IFGDismantleInterface::Execute_Dismantle(Track);
    }
    const double Min=FBSPlanBuild::MinimumLength();
    for (int32 Case=0;Case<3;++Case)
    {
        auto Candidate=Fixture; Candidate.Nodes.SetNum(2); Candidate.Edges.SetNum(1); Candidate.Remainder=EBSPlanRemainder::Last;
        Candidate.SegmentLength=Case==1 ? Max : 8000;
        const double Length=Case==2 ? Min*.5 : Candidate.SegmentLength+Min*.5;
        Candidate.Nodes[1].Position=Candidate.Nodes[0].Position+FVector(Length,0,0);
        TSet<AFGBuildableRailroadTrack*> Existing;
        for (TActorIterator<AFGBuildableRailroadTrack> It(World);It;++It) if (!It->GetIsDismantled()) Existing.Add(*It);
        FString Error; const bool Built=FBSPlanBuild::Build(Player,Candidate,Error);
        TArray<AFGBuildableRailroadTrack*> Created;
        for (TActorIterator<AFGBuildableRailroadTrack> It(World);It;++It) if (!It->GetIsDismantled() && !Existing.Contains(*It)) Created.Add(*It);
        const int32 Count=Case==2 ? 0 : Case==1 ? 2 : 1;
        bool Good=Built==(Case!=2) && Created.Num()==Count;
        for (auto* Track:Created) Good &= FMath::Abs(Track->GetLength()-Length/Count)<1;
        Check(*FString::Printf(TEXT("Native short-tail fallback case %d preserves lengths or rejects atomically"),Case),Good); Passed &= Good;
        if (!Good) UE_LOG(LogBuildingSkills,Display,TEXT("PLAN TAIL case %d: min=%.2f built=%d tracks=%d: %s"),Case,Min,Built,Created.Num(),*Error);
        for (auto* Track:Created) IFGDismantleInterface::Execute_Dismantle(Track);
    }
    return Passed;
}

static bool RunPlanSignalChecks(AFGCharacterPlayer* Player,TFunctionRef<void(const TCHAR*,bool)> Check)
{
    auto* World=Player->GetWorld(); auto* State=World->GetGameState<AFGGameState>();
    auto Recipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/Train/Signal/Recipe_RailroadBlockSignal.Recipe_RailroadBlockSignal_C"));
    auto* Recipes=AFGRecipeManager::Get(World);
    if (!State || !Recipe || !Recipes) { Check(TEXT("Block signal fixture is available"),false); return false; }
    Recipes->AddAvailableRecipe(Recipe);
    const bool Free=State->GetCheatNoCost(); State->SetCheatNoCost(true); ON_SCOPE_EXIT { State->SetCheatNoCost(Free); };
    const double Length=FBSPlanBuild::MaximumLength()*.5; bool Passed=true;
    FBSRailPlan Fixture; Fixture.Id=FGuid::NewGuid(); Fixture.Name=TEXT("Signal specification"); Fixture.SegmentLength=Length; Fixture.PlaceBlockSignal=true;
    const FVector Origin=Player->GetActorLocation()+FVector(0,0,90000);
    FBSPlanNode A; A.Id=FGuid::NewGuid(); A.Position=Origin;
    auto B=A; B.Id=FGuid::NewGuid(); B.Position+=FVector(4*Length,0,0); Fixture.Nodes={A,B};
    FBSPlanEdge E; E.Id=FGuid::NewGuid(); E.Start=A.Id; E.End=B.Id; E.Trace=EBSPlanTrace::Straight; Fixture.Edges={E};
    for (int32 Case=0;Case<3;++Case)
    {
        auto Candidate=Fixture; Candidate.SignalEvery=Case==0 ? 1 : 2; Candidate.LeftSideSignal=Case==1;
        if (Case==2) { Candidate.ReversedEdges={E.Id}; Candidate.Parallels={FVector(0,600,0)}; Candidate.ParallelReversed={true}; }
        TSet<AFGBuildable*> Existing;
        for (TActorIterator<AFGBuildable> It(World);It;++It) if (!It->GetIsDismantled() && (It->IsA<AFGBuildableRailroadTrack>() || It->IsA<AFGBuildableRailroadSignal>())) Existing.Add(*It);
        FString Error; const bool Built=FBSPlanBuild::Build(Player,Candidate,Error);
        TArray<AFGBuildableRailroadSignal*> Signals; TArray<AFGBuildableRailroadTrack*> Tracks;
        for (TActorIterator<AFGBuildable> It(World);It;++It) if (!It->GetIsDismantled() && !Existing.Contains(*It))
        { if (auto* S=Cast<AFGBuildableRailroadSignal>(*It)) Signals.Add(S); else if (auto* T=Cast<AFGBuildableRailroadTrack>(*It)) Tracks.Add(T); }
        bool Correct=Built && Signals.Num()==(Case==0 ? 3 : Case==1 ? 1 : 2) && Tracks.Num()==(Case==2 ? 8 : 4);
        for (auto* S:Signals)
        {
            const FVector At=S->GetActorLocation()-Origin;
            const bool Reverse=Case==2 && FMath::Abs(At.Y)<1;
            const double Distance=Reverse ? 4*Length-At.X : At.X;
            Correct &= S->IsLeftHanded()==Candidate.LeftSideSignal && S->GetGuardedConnections().Num()>0 &&
                FVector::DotProduct(S->GetActorForwardVector(),Reverse ? -FVector::ForwardVector : FVector::ForwardVector)>.999 &&
                Distance>1 && FMath::Abs(Distance/(Length*Candidate.SignalEvery)-FMath::RoundToDouble(Distance/(Length*Candidate.SignalEvery)))<.001;
        }
        Check(*FString::Printf(TEXT("Native block signal spacing, side and parallel direction case %d"),Case),Correct); Passed &= Correct;
        if (!Correct) UE_LOG(LogBuildingSkills,Display,TEXT("PLAN SIGNALS case %d: built=%d rails=%d signals=%d: %s"),Case,Built,Tracks.Num(),Signals.Num(),*Error);
        for (auto* S:Signals) IFGDismantleInterface::Execute_Dismantle(S);
        for (auto* T:Tracks) IFGDismantleInterface::Execute_Dismantle(T);
    }
    return Passed;
}

class FBSPlanSmoke
{
public:
static bool Run(AFGCharacterPlayer* P,int32 Phase)
{
    auto* Plan=UBSPlanComponent::Find(P); auto* PC=Cast<AFGPlayerController>(P->GetController());
    auto* Input=PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
    auto* R=PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    auto* E=P->FindComponentByClass<UBSEditModeComponent>(); auto* Store=ABSGroupSubsystem::Get(P->GetWorld());
    if (!Plan || !Input || !R || !E || !Store) return false;
    bool Passed=true; auto Check=[&](const TCHAR* Name,bool OK) { UE_LOG(LogBuildingSkills,Display,TEXT("RUNTIME %s: Plan: %s"),OK ? TEXT("PASS") : TEXT("FAIL"),Name); Passed &= OK; };
    auto Key=[&](FKey K,bool Down) { Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,K,Down ? IE_Pressed : IE_Released,Down ? 1.f : 0.f,false,0)); };
    static FGuid SavedId,PrivateId; static TWeakObjectPtr<AFGPlayerState> OtherOwner;
    static FBSRailPlan BuiltPlan; static FVector BeforeMove;
    static bool FixtureReady=false;
    static FGuid ExtensionPlan,ExtensionNode; static FVector ExtensionPosition;
    static FGuid InsertedId,ControlEdge; static FVector InsertedPosition,OriginalDirection,ControlPosition; static int32 BeforeNodes=0,BeforeEdges=0;
    if (Phase>6 && !FixtureReady)
    {
        if (Phase==30 || Phase==46) { Plan->Close(true); Store->RailPlans.Reset(); Store->PlanHistories.Reset(); }
        return false;
    }
    if (Phase==0)
    {
        FixtureReady=false;
        Key(EKeys::E,false); Key(EKeys::LeftAlt,false); P->UnequipBuildGun();
        Passed &= RunBuildingSkillsPlanMathChecks(Check);
        AFGRecipeManager::Get(P->GetWorld())->AddAvailableRecipe(FBSPlanBuild::Recipe());
        PC->SetControlRotation(FRotator::ZeroRotator);
    }
    if (Phase==1) { Key(EKeys::LeftAlt,true); Key(EKeys::P,true); }
    if (Phase==2)
    {
        Key(EKeys::P,false); Key(EKeys::LeftAlt,false);
        Check(TEXT("Alt+P opens Plan without a rail hologram or active construction recipe"),Plan->Active && !FBSStick::Active(E) && Cast<UFGBuildGunStateBuild>(P->GetBuildGun()->GetCurrentState()) && !Cast<UFGBuildGunStateBuild>(P->GetBuildGun()->GetCurrentState())->GetActiveRecipe());
        E->StickMode=EBSStickMode::Default; E->StickDistance=64;
    }
    if (Phase==3) { Check(TEXT("A personal draft is created"),Plan->Current() && !Plan->Current()->Shared); Key(EKeys::LeftMouseButton,true); }
    if (Phase==4)
    {
        Key(EKeys::LeftMouseButton,false); Check(TEXT("Native gun click places a plan node"),Plan->Current() && Plan->Current()->Nodes.Num()==1);
        E->StickDistance=264;
    }
    if (Phase==5)
    {
        E->StickMode=EBSStickMode::Constrained;
        auto& StickSettings=P->GetWorld()->GetSubsystem<UBSStickSession>()->GetSettings();
        StickSettings.VerticalConstraint=true; StickSettings.HorizontalConstraint=true; StickSettings.StickSlope=0; StickSettings.StickRadius=0;
        int32 Holograms=0; for (TActorIterator<AFGRailroadTrackHologram> It(P->GetWorld());It;++It) if (IsValid(*It)) ++Holograms;
        Check(TEXT("Long Place preview creates no rail holograms, including split children"),Holograms==0);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsPlanPlace.png"),true,false);
        Key(EKeys::LeftMouseButton,true);
    }
    if (Phase==6)
    {
        Key(EKeys::LeftMouseButton,false);
        Check(TEXT("Second native click creates a spline, not a real rail"),Plan->Current() && Plan->Current()->Nodes.Num()==2 && Plan->Current()->Edges.Num()==1);
        Check(TEXT("Constrained Stick placement retains its captured spline path"),Plan->Current() && Plan->Current()->Edges.Num()==1 && Plan->Current()->Edges[0].Route.Num()>=2);
        bool OrderedRoute=Plan->Current() && Plan->Current()->Edges.Num()==1;
        if (OrderedRoute) { const auto& Route=Plan->Current()->Edges[0].Route; for (int32 I=1;I<Route.Num();++I) OrderedRoute &= Route[I].Location.X>Route[I-1].Location.X; }
        Check(TEXT("Straight constrained placement stores its route once without jumping back to the start"),OrderedRoute);
        FixtureReady=Plan->Active && Plan->Current() && Plan->Current()->Nodes.Num()==2 && Plan->Current()->Edges.Num()==1;
        int32 Rails=0; for (TActorIterator<AFGBuildableRailroadTrack> It(P->GetWorld());It;++It) if (!It->GetIsDismantled()) ++Rails;
        Check(TEXT("Planning does not construct world rails"),Rails==0);
        if (Plan->Current()) { SavedId=Plan->CurrentId; BuiltPlan=*Plan->Current(); }
        Check(TEXT("Place uses native Plan mode identity"),P->GetBuildGun()->GetCurrentBuildGunMode()==UBSPlanPlaceMode::StaticClass());
        P->GetBuildGun()->Input_ModeSelect(FInputActionValue(true));
        P->GetBuildGun()->Input_ModeSelect(FInputActionValue(false));
        Check(TEXT("Tapping the native mode action switches Place to Edit"),Plan->Editing && P->GetBuildGun()->GetCurrentBuildGunMode()==UBSPlanEditMode::StaticClass());
        P->GetBuildGun()->SetCurrentBuildGunMode(UBSPlanPlaceMode::StaticClass());
        Check(TEXT("Automatic first-item mode assignment cannot undo a Plan tap"),Plan->Editing && P->GetBuildGun()->GetCurrentBuildGunMode()==UBSPlanEditMode::StaticClass());
        Plan->SetMode(false);
        Key(EKeys::R,true);
        FTimerHandle Release;
        P->GetWorldTimerManager().SetTimer(Release,[Weak=TWeakObjectPtr<UEnhancedPlayerInput>(Input)]() {
            if (auto* I=Weak.Get()) I->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::R,IE_Released,0.f,false,0));
        },.1f,false);
    }
    if (Phase==7)
    {
        Check(TEXT("Real short R key press remains in Edit after deferred UI callbacks"),Plan->Editing && P->GetBuildGun()->GetCurrentBuildGunMode()==UBSPlanEditMode::StaticClass() && !P->GetBuildGun()->IsWaitingForBuildGunModeSelectUI());
        if (auto* Source=Store->RailPlans.FindByPredicate([&](const auto& V) { return V.Id==SavedId; }))
        {
            FBSRailPlan Restored; TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Save(Writer,false); Save.ArIsSaveGame=true;
            FBSRailPlan::StaticStruct()->SerializeItem(Save,Source,nullptr);
            FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Load(Reader,true); Load.ArIsSaveGame=true;
            FBSRailPlan::StaticStruct()->SerializeItem(Load,&Restored,nullptr);
            Check(TEXT("Save archive preserves plan identity, owner, nodes and spline geometry"),!Save.IsError() && !Load.IsError() && Restored.Id==Source->Id && Restored.Owner==Source->Owner && Restored.Nodes.Num()==2 && Restored.Nodes[1].Position.Equals(Source->Nodes[1].Position,.001) && Restored.Edges.Num()==1 && Restored.Edges[0].Route.Num()==Source->Edges[0].Route.Num() && (Restored.Edges[0].Route.IsEmpty() || Restored.Edges[0].Route.Last().Location.Equals(Source->Edges[0].Route.Last().Location,.001)));
        }
        Plan->Close(true);
        bool Hidden=true; for (auto& C:Plan->Visuals) Hidden &= C->GetInstanceCount()==0;
        Check(TEXT("Closing the gun hides every plan visual and retains its data"),Hidden && Plan->Current() && Plan->CurrentId==SavedId);
    }
    if (Phase==8) { Key(EKeys::LeftAlt,true); Key(EKeys::P,true); }
    if (Phase==9)
    {
        Key(EKeys::P,false); Key(EKeys::LeftAlt,false);
        Check(TEXT("Reopening restores the same plan"),Plan->Active && Plan->CurrentId==SavedId && Plan->Current() && Plan->Current()->Nodes.Num()==2);
        Plan->SetMode(true);
        if (Plan->Current() && !Plan->Current()->Nodes.IsEmpty()) Plan->Selection={Plan->Current()->Nodes[0].Id};
        TArray<TSubclassOf<UFGBuildGunModeDescriptor>> Modes; P->GetBuildGun()->GetCurrentState()->GetSupportedBuildModes(Modes);
        Check(TEXT("Native mode list contains only Place and Edit"),Modes.Num()==2 && Modes.Contains(UBSPlanPlaceMode::StaticClass()) && Modes.Contains(UBSPlanEditMode::StaticClass()));
        Key(EKeys::K,true);
    }
    if (Phase==10)
    {
        Key(EKeys::K,false);
        Check(TEXT("K opens the plan menu with mouse and keyboard capture"),Plan->Window && Plan->Window->mUseMouse && Plan->Window->mUseKeyboard && P->GetGameUI()->GetInteractWidgetStack().Contains(Plan->Window));
        if (auto* W=Plan->Window.Get(); W && W->Sections.Num()==6)
        {
            Check(TEXT("K lists every node and spline in the current plan"),W->ItemView && W->ListItems.Num()==Plan->Current()->Nodes.Num()+Plan->Current()->Edges.Num());
            if (W->ItemView && W->ListItems.Num()>=2)
            {
                W->ItemView->ClearSelection(); W->ItemView->SetItemSelection(W->ListItems[0],true); W->ItemView->SetItemSelection(W->ListItems[1],true);
                Check(TEXT("List multi-selection updates the world selection"),Plan->Selection.Num()==2 && Plan->Selection.Contains(W->ListItems[0]->Id) && Plan->Selection.Contains(W->ListItems[1]->Id));
                Plan->Draw(); Check(TEXT("Selected list nodes have highlighted world markers"),Plan->Visuals.IsValidIndex(5) && Plan->Visuals[5]->GetInstanceCount()>=2);
                W->ItemView->ClearSelection(); W->ItemView->SetItemSelection(W->ListItems[0],true);
            }

            W->Sections[TEXT("Plans")]->SetExpanded(false);
            W->Sections[TEXT("Stick")]->SetExpanded(false);
            W->Sections[TEXT("Selection")]->SetExpanded(true);
            W->Sections[TEXT("Parallels")]->SetExpanded(true);
            Check(TEXT("Plan sections collapse independently"),!W->Sections[TEXT("Plans")]->IsExpanded() && W->Sections[TEXT("Selection")]->IsExpanded());
            const FString File=FPaths::ProjectSavedDir()/TEXT("BuildingSkillsPlanTest.ini");
            FConfigFile Disk; Disk.Read(File);
            bool Stored=false; for (const auto& Entry:AsConst(Disk)) { bool Open=true; if (Disk.GetBool(*Entry.Key,TEXT("Plans"),Open) && !Open) Stored=true; }
            Check(TEXT("Collapsed Plan preference is flushed to local disk"),Stored);
            Plan->CloseSettings(); Plan->Settings();
            W=Plan->Window.Get();
            Check(TEXT("Reopened menu restores collapsed sections from disk"),W && W->Sections.Num()==6 && !W->Sections[TEXT("Plans")]->IsExpanded() && !W->Sections[TEXT("Stick")]->IsExpanded() && W->Sections[TEXT("Selection")]->IsExpanded() && W->Sections[TEXT("Parallels")]->IsExpanded());
        }
        else Check(TEXT("All five collapsible Plan sections exist"),false);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsPlanSettings.png"),true,false);
    }
    if (Phase==11)
    {
        Plan->CloseSettings();
        OtherOwner=P->GetWorld()->SpawnActor<AFGPlayerState>();
        FBSRailPlan Private=BuiltPlan; Private.Id=FGuid::NewGuid(); Private.Name=TEXT("Other player's private plan"); Private.Owner=OtherOwner.Get(); PrivateId=Private.Id; Store->RailPlans.Add(Private);
        R->DeliveredPlanHash=0; R->LastPlanPoll=-100; R->Server_PlanSnapshot();
        Check(TEXT("Other players' personal plans are absent from snapshots"),!Plan->Plans.ContainsByPredicate([&](const auto& V) { return V.Id==PrivateId; }));
        FBSPlanCommand Command; Command.Action=EBSPlanAction::Rename; Command.Plan=PrivateId; Command.Name=TEXT("Unauthorized"); R->Server_PlanCommand(Command);
        Check(TEXT("Private-plan mutation is denied"),Store->RailPlans.FindByPredicate([&](const auto& V) { return V.Id==PrivateId; })->Name==Private.Name);
        Command.Action=EBSPlanAction::Undo; R->Server_PlanCommand(Command);
        Check(TEXT("Private-plan history is inaccessible to another player"),!Store->PlanHistories.Contains(PrivateId));
    }
    if (Phase==12)
    {
        auto* Private=Store->RailPlans.FindByPredicate([&](const auto& V) { return V.Id==PrivateId; }); Private->Shared=true; Private->Revision++;
        R->DeliveredPlanHash=0; R->LastPlanPoll=-100; R->Server_PlanSnapshot();
    }
    if (Phase==13)
    {
        Check(TEXT("Shared plans appear in another player's snapshot"),Plan->Plans.ContainsByPredicate([&](const auto& V) { return V.Id==PrivateId; }));
        auto* Private=Store->RailPlans.FindByPredicate([&](const auto& V) { return V.Id==PrivateId; });
        FBSPlanCommand Command; Command.Action=EBSPlanAction::Rename; Command.Plan=PrivateId; Command.Revision=Private->Revision; Command.Name=TEXT("Shared edit"); R->Server_PlanCommand(Command);
        Check(TEXT("Another player may edit a shared plan"),Store->RailPlans.FindByPredicate([&](const auto& V) { return V.Id==PrivateId; })->Name==TEXT("Shared edit"));
        const FVector Previous=Private->Nodes[0].Position;
        Command.Action=EBSPlanAction::Move; Command.Revision=Private->Revision; Command.Selection={Private->Nodes[0].Id}; Command.Delta=FVector(100,0,0); R->Server_PlanCommand(Command);
        Command.Action=EBSPlanAction::Undo; Command.Revision=Private->Revision; R->Server_PlanCommand(Command);
        Check(TEXT("Shared plan geometry edits can be undone by another player"),Private->Nodes[0].Position==Previous && Private->Name==TEXT("Shared edit"));
        Command.Action=EBSPlanAction::Redo; R->Server_PlanCommand(Command);
        Check(TEXT("Server rejects stale shared history commands"),Private->Nodes[0].Position==Previous);
        Plan->Load(SavedId);
    }
    if (Phase==13)
    {
        // Use an explicit long straight graph so build validation is independent
        // of the camera fixture; include a parallel line and a continuous join.
        BuiltPlan={}; BuiltPlan.Id=FGuid::NewGuid(); BuiltPlan.Name=TEXT("Build regression");
        const FVector Origin=P->GetActorLocation()+FVector(0,10000,10000);
        for (int32 I=0;I<3;++I) { FBSPlanNode N; N.Id=FGuid::NewGuid(); N.Position=Origin+FVector(20000*I,0,0); BuiltPlan.Nodes.Add(N); }
        for (int32 I=0;I<2;++I) { FBSPlanEdge Edge; Edge.Id=FGuid::NewGuid(); Edge.Start=BuiltPlan.Nodes[I].Id; Edge.End=BuiltPlan.Nodes[I+1].Id; BuiltPlan.Edges.Add(Edge); }
        BuiltPlan.Parallels.Add(FVector(0,600,0));
        auto* H=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(FBSPlanBuild::Recipe(),P->GetBuildGun(),Origin,P));
        P->GetInventory()->Resize(150);
        if (H) { FBSStick::PlanGeometry(H,{FSplinePointData(Origin,FVector(10000,0,0)),FSplinePointData(Origin+FVector(10000,0,0),FVector(10000,0,0))}); for (const auto& Item:H->GetCost(true)) P->GetInventory()->AddStack(FInventoryStack(Item.Amount*20+100,Item.ItemClass)); H->Destroy(); }
        FString Error; Check(TEXT("Full-path build passes native validation and constructs parallel rails"),FBSPlanBuild::Build(P,BuiltPlan,Error));
        UE_LOG(LogBuildingSkills,Display,TEXT("PLAN BUILD: %s"),*Error);
        int32 Tracks=0,Connected=0;
        for (TActorIterator<AFGBuildableRailroadTrack> It(P->GetWorld());It;++It) if (!It->GetIsDismantled()) { ++Tracks; for (int32 End=0;End<2;++End) Connected+=It->GetConnection(End)->GetConnections().Num(); }
        Check(TEXT("Split and node joins have reciprocal connections"),Tracks>=8 && Connected==2*(Tracks-2));
    }
    if (Phase==14)
    {
        auto Invalid=BuiltPlan; Invalid.Nodes[1].Position.Z+=100000;
        FString Error; Check(TEXT("Invalid shape is rejected without construction"),!FBSPlanBuild::Build(P,Invalid,Error));
        for (TActorIterator<AFGBuildableRailroadTrack> It(P->GetWorld());It;++It) if (!It->GetIsDismantled()) IFGDismantleInterface::Execute_Dismantle(*It);
    }
    if (Phase==15)
    {
        Plan->Load(SavedId); Plan->SetMode(true);
        if (Plan->Current() && !Plan->Current()->Nodes.IsEmpty())
        { BeforeMove=Plan->Current()->Nodes[0].Position; FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View); PC->SetControlRotation((BeforeMove-Eye).Rotation()); }
        Key(EKeys::LeftControl,true);
    }
    if (Phase==16)
    {
        UE_LOG(LogBuildingSkills,Display,TEXT("PLAN SELECT: held=%d key=%d tick=%d hoverNode=%s hoverEdge=%s selected=%d"),Plan->MultiHeld,PC->IsInputKeyDown(EKeys::LeftControl),Plan->IsComponentTickEnabled(),*Plan->HoverNode.ToString(),*Plan->HoverEdge.ToString(),Plan->Selection.Num());
        Check(TEXT("Holding Ctrl selects the aimed node without clicking"),Plan->Selection.Num()==1 && Plan->Current() && FBSPlanMath::Node(*Plan->Current(),Plan->Selection[0]));
    }
    if (Phase==17)
    {
        Key(EKeys::LeftControl,false);
        Check(TEXT("Continuing to hold Ctrl does not toggle the same node repeatedly"),Plan->Selection.Num()==1 && Plan->Current() && FBSPlanMath::Node(*Plan->Current(),Plan->Selection[0]));
        PC->SetControlRotation(FRotator(70,90,0));
        Key(EKeys::Up,true);
    }
    if (Phase==18)
    {
        Key(EKeys::Up,false);
        const auto* N=Plan->Current() && Plan->Selection.Num()==1 ? FBSPlanMath::Node(*Plan->Current(),Plan->Selection[0]) : nullptr;
        UE_LOG(LogBuildingSkills,Display,TEXT("PLAN MOVE: delta=%s control=%s"),N ? *(N->Position-BeforeMove).ToString() : TEXT("missing"),*PC->GetControlRotation().ToString());
        Check(TEXT("Arrow movement follows gaze along Y without pitch drift"),N && N->Position.Y>BeforeMove.Y && FMath::IsNearlyEqual(N->Position.X,BeforeMove.X,.01) && FMath::IsNearlyEqual(N->Position.Z,BeforeMove.Z,.01));
        if (N) BeforeMove=N->Position;
        Key(EKeys::PageUp,true);
        Key(EKeys::R,true);
    }
    if (Phase==19)
    {
        Key(EKeys::PageUp,false);
        const auto* N=Plan->Current() && Plan->Selection.Num()==1 ? FBSPlanMath::Node(*Plan->Current(),Plan->Selection[0]) : nullptr;
        Check(TEXT("Page Up moves selected node on Z while camera is tilted"),N && N->Position.Z>BeforeMove.Z && FMath::IsNearlyEqual(N->Position.X,BeforeMove.X,.01) && FMath::IsNearlyEqual(N->Position.Y,BeforeMove.Y,.01));
        Check(TEXT("Native R hold opens the Plan mode selector"),P->GetBuildGun()->IsWaitingForBuildGunModeSelectUI());
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsPlanRadial.png"),true,false);
    }
    if (Phase==20)
    {
        Key(EKeys::R,false);
        P->GetBuildGun()->Input_ModeSelect(FInputActionValue(false));
        Check(TEXT("Releasing R closes the Plan mode selector"),!P->GetBuildGun()->IsWaitingForBuildGunModeSelectUI());
        Key(EKeys::F,true);
    }
    if (Phase==21)
    {
        Key(EKeys::F,false);
        Check(TEXT("F deletes the selected node and its incident spline without leaving Plan"),Plan->Active && P->GetBuildGun()->GetCurrentState()->IsA<UFGBuildGunStateBuild>() && Plan->Current() && Plan->Current()->Nodes.Num()==1 && Plan->Current()->Edges.IsEmpty());
        Key(EKeys::LeftControl,true); Key(EKeys::Z,true);
    }
    if (Phase==22)
    {
        Key(EKeys::Z,false); Key(EKeys::LeftControl,false);
        Check(TEXT("Ctrl+Z restores the deleted node and spline"),Plan->Current() && Plan->Current()->Nodes.Num()==2 && Plan->Current()->Edges.Num()==1);
    }
    if (Phase==23) { Key(EKeys::LeftControl,true); Key(EKeys::Y,true); }
    if (Phase==24)
    {
        Key(EKeys::Y,false); Key(EKeys::LeftControl,false);
        Check(TEXT("Ctrl+Y reapplies deletion"),Plan->Current() && Plan->Current()->Nodes.Num()==1 && Plan->Current()->Edges.IsEmpty());
    }
    if (Phase==25) { Key(EKeys::LeftControl,true); Key(EKeys::Z,true); }
    if (Phase==26)
    {
        Key(EKeys::Z,false); Key(EKeys::LeftControl,false);
        Plan->Selection.Reset(); Plan->SetMode(false);
        if (const auto* Current=Plan->Current(); Current && !Current->Edges.IsEmpty())
        {
            const auto Points=FBSPlanMath::Curve(*Current,Current->Edges[0]);
            const FVector Mid=FBSPlanMath::Point(Points[0],Points[1],.473);
            P->SetActorLocation(Mid+FVector(0,3000,-100));
            FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View); PC->SetControlRotation((Mid-Eye).Rotation());
        }
    }
    if (Phase==27)
    {
        // Aim after the camera has caught up with the fixture teleport.
        if (const auto* Current=Plan->Current(); Current && !Current->Edges.IsEmpty())
        {
            const auto Points=FBSPlanMath::Curve(*Current,Current->Edges[0]);
            const FVector Mid=FBSPlanMath::Point(Points[0],Points[1],.473);
            FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View); PC->SetControlRotation((Mid-Eye).Rotation());
        }
        Key(EKeys::LeftControl,true);
    }
    if (Phase==28)
    {
        Check(TEXT("Holding Ctrl selects the spline between samples in Place mode"),Plan->Current() && Plan->Current()->Edges.Num()==1 && Plan->Selection.Contains(Plan->Current()->Edges[0].Id));
        Key(EKeys::LeftControl,false);
    }
    if (Phase==29) Key(EKeys::LeftControl,true);
    if (Phase==30)
    {
        Key(EKeys::LeftControl,false);
        Check(TEXT("A new Ctrl gesture deselects the aimed spline"),Plan->Selection.IsEmpty());
        Plan->Close(false);
        P->HotKeyRecipe(FBSPlanBuild::Recipe());
    }
    if (Phase==31)
    {
        auto* H=FBSStick::Active(E);
        const auto* Build=Cast<UFGBuildGunStateBuild>(P->GetBuildGun()->GetCurrentState());
        Check(TEXT("Choosing a rail recipe after Plan creates its native construction hologram"),H && !Plan->Active && Build && Build->GetActiveRecipe()==FBSPlanBuild::Recipe());
        P->UnequipBuildGun();
    }
    if (Phase==32)
    {
        Plan->Toggle(); Plan->Load(SavedId); Plan->SetMode(false); E->StickMode=EBSStickMode::Off;
        if (const auto* Current=Plan->Current(); Current && !Current->Edges.IsEmpty())
        {
            const auto Points=FBSPlanMath::Curve(*Current,Current->Edges[0]);
            InsertedPosition=FBSPlanMath::Point(Points[0],Points[1],.43); BeforeNodes=Current->Nodes.Num(); BeforeEdges=Current->Edges.Num();
            P->SetActorLocation(InsertedPosition+FVector(0,-3000,-100));
            FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View); PC->SetControlRotation((InsertedPosition-Eye).Rotation());
        }
    }
    if (Phase==33)
    {
        FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View); PC->SetControlRotation((InsertedPosition-Eye).Rotation());
        // Input processing precedes the camera update; wait until the view has
        // caught up with the teleport and aim change before issuing the click.
        FTimerHandle Click;
        P->GetWorldTimerManager().SetTimer(Click,[Weak=TWeakObjectPtr<UEnhancedPlayerInput>(Input),WeakPlan=TWeakObjectPtr<UBSPlanComponent>(Plan)]() {
            if (auto* Planner=WeakPlan.Get()) {
                Planner->AimAndHit();
                UE_LOG(LogBuildingSkills,Display,TEXT("PLAN SPLIT INPUT: allowed=%d waiting=%d aim=%d edge=%s segment=%d time=%.6f"),Planner->Allowed(),Planner->Waiting,Planner->HaveAim,*Planner->HoverEdge.ToString(),Planner->HoverSegment,Planner->HoverTime);
            }
            if (auto* I=Weak.Get()) I->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::LeftMouseButton,IE_Pressed,1.f,false,0));
        },.2f,false);
    }
    if (Phase==34)
    {
        Key(EKeys::LeftMouseButton,false);
        const auto* Current=Plan->Current();
        UE_LOG(LogBuildingSkills,Display,TEXT("PLAN SPLIT RESULT: waiting=%d nodes=%d edges=%d message=%s"),Plan->Waiting,Current ? Current->Nodes.Num() : -1,Current ? Current->Edges.Num() : -1,*Plan->Message);
        Check(TEXT("Clicking an aimed spline snaps and splits it without constructing rails"),Current && Current->Nodes.Num()==BeforeNodes+1 && Current->Edges.Num()==BeforeEdges+1);
        if (Current && Current->Nodes.Num()==BeforeNodes+1)
        {
            InsertedId=Plan->LastNode; const auto* N=FBSPlanMath::Node(*Current,InsertedId);
            if (N) { InsertedPosition=N->Position; OriginalDirection=N->Direction; FBSPlanCommand Command; Command.Action=EBSPlanAction::Node; Command.Node=*N; Command.Node.Roll=13; Plan->Send(Command); }
        }
        Check(TEXT("Left-panel status shows distance without repeating Place/Edit"),Plan->Status().ToString().Contains(TEXT("Distance:")) && !Plan->Status().ToString().Contains(TEXT("Mode:")));
        Plan->SetMode(true); P->SetActorLocation(InsertedPosition+FVector(0,-2000,-100));
    }
    if (Phase==35 && InsertedId.IsValid())
    {
        FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View); PC->SetControlRotation((InsertedPosition+OriginalDirection*250-Eye).Rotation());
    }
    if (Phase==36 && InsertedId.IsValid())
    {
        Check(TEXT("Node direction dot can be targeted separately from the node"),Plan->HoverNode==InsertedId && Plan->HoverOrientation);
        Key(EKeys::LeftMouseButton,true);
    }
    if (Phase==37 && InsertedId.IsValid())
    {
        Check(TEXT("Clicking the direction dot starts an orientation drag"),Plan->DragNode==InsertedId && Plan->DragOrientation);
        FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View); PC->SetControlRotation((InsertedPosition+FVector(100,0,250)-Eye).Rotation());
    }
    if (Phase==38 && InsertedId.IsValid())
    {
        Key(EKeys::LeftMouseButton,false);
        const auto* Current=Plan->Current(); const auto* N=Current ? FBSPlanMath::Node(*Current,InsertedId) : nullptr;
        bool Continuous=N!=nullptr;
        if (N) for (const auto& Edge:Current->Edges) if (Edge.Start==InsertedId || Edge.End==InsertedId)
        { const auto Points=FBSPlanMath::Curve(*Current,Edge); const FVector D=Edge.Start==InsertedId ? Points[0].LeaveTangent : Points.Last().ArriveTangent; Continuous &= FMath::Abs(FVector::DotProduct(D.GetSafeNormal(),N->Direction.GetSafeNormal()))>.9999; }
        Check(TEXT("Direction dragging rotates adjoining tangents without moving the node or losing roll"),N && N->Position.Equals(InsertedPosition,.01) && !N->Direction.Equals(OriginalDirection,.01) && FMath::IsNearlyEqual(N->Roll,13.0) && Continuous);
    }
    if (Phase==39 && InsertedId.IsValid()) { Key(EKeys::LeftControl,true); Key(EKeys::Z,true); }
    if (Phase==40 && InsertedId.IsValid())
    {
        Key(EKeys::Z,false); Key(EKeys::LeftControl,false);
        const auto* N=Plan->Current() ? FBSPlanMath::Node(*Plan->Current(),InsertedId) : nullptr;
        Check(TEXT("Undo restores the complete node orientation gesture"),N && N->Direction.Equals(OriginalDirection,.0001) && FMath::IsNearlyEqual(N->Roll,13.0));
        if (Plan->Current() && !Plan->Current()->Edges.IsEmpty())
        {
            FBSPlanCommand Command; Command.Action=EBSPlanAction::Edge; Command.Edge=Plan->Current()->Edges[0]; Command.Edge.Trace=EBSPlanTrace::Free;
            const auto* A=FBSPlanMath::Node(*Plan->Current(),Command.Edge.Start); const auto* B=FBSPlanMath::Node(*Plan->Current(),Command.Edge.End);
            ControlPosition=(A->Position+B->Position)*.5+FVector(0,1000,0); Command.Edge.Controls={ControlPosition}; Command.Edge.ControlDirections.Reset();
            ControlEdge=Command.Edge.Id; Plan->Send(Command); P->SetActorLocation(ControlPosition+FVector(0,-2000,-100));
        }
    }
    if (Phase==41 && ControlEdge.IsValid())
    {
        const auto* Edge=Plan->Current()->Edges.FindByPredicate([&](const auto& V) { return V.Id==ControlEdge; });
        FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View); PC->SetControlRotation((ControlPosition+FBSPlanMath::ControlDirection(*Plan->Current(),*Edge,0)*250-Eye).Rotation());
    }
    if (Phase==42 && ControlEdge.IsValid())
    {
        Check(TEXT("Through-point direction dot is independently targetable"),Plan->HoverEdge==ControlEdge && Plan->HoverControl==0 && Plan->HoverOrientation);
        Key(EKeys::LeftMouseButton,true);
    }
    if (Phase==43 && ControlEdge.IsValid())
    {
        FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View); PC->SetControlRotation((ControlPosition+FVector(100,0,250)-Eye).Rotation());
    }
    if (Phase==44 && ControlEdge.IsValid())
    {
        Key(EKeys::LeftMouseButton,false);
        const auto* Edge=Plan->Current()->Edges.FindByPredicate([&](const auto& V) { return V.Id==ControlEdge; });
        Check(TEXT("Dragging a through-point direction changes its tangent without moving the point"),Edge && Edge->Controls[0].Equals(ControlPosition,.01) && Edge->ControlDirections.Num()==1 && !Edge->ControlDirections[0].IsNearlyZero());
    }
    if (Phase==45 && ControlEdge.IsValid())
    {
        const auto* Source=Plan->Current(); FBSRailPlan Restored; TArray<uint8> Bytes;
        FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Save(Writer,false); Save.ArIsSaveGame=true; FBSRailPlan::StaticStruct()->SerializeItem(Save,const_cast<FBSRailPlan*>(Source),nullptr);
        FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Load(Reader,true); Load.ArIsSaveGame=true; FBSRailPlan::StaticStruct()->SerializeItem(Load,&Restored,nullptr);
        const auto* Edge=Restored.Edges.FindByPredicate([&](const auto& V) { return V.Id==ControlEdge; }); const auto* N=FBSPlanMath::Node(Restored,InsertedId);
        Check(TEXT("Save archive preserves split nodes, roll and manual through-point tangents"),!Save.IsError() && !Load.IsError() && Edge && Edge->ControlDirections.Num()==1 && !Edge->ControlDirections[0].IsNearlyZero() && N && FMath::IsNearlyEqual(N->Roll,13.0));
        Plan->Selection={InsertedId}; Plan->Settings();
    }
    if (Phase==47) { Passed &= RunPlanPaymentChecks(P,Check); Passed &= RunPlanSegmentChecks(P,Check); Passed &= RunPlanSignalChecks(P,Check); }

    if (Phase==48)
    {
        FBSRailPlan Fixture; Fixture.Id=FGuid::NewGuid(); Fixture.Name=TEXT("Extension regression"); Fixture.Owner=P->GetPlayerState<AFGPlayerState>();
        FBSPlanNode A; A.Id=FGuid::NewGuid(); A.Position=P->GetActorLocation()+FVector(0,0,30000);
        auto B=A; B.Id=FGuid::NewGuid(); B.Position+=FVector(12000,10000,0);
        FBSPlanEdge Link; Link.Id=FGuid::NewGuid(); Link.Start=A.Id; Link.End=B.Id;
        Link.Route.Emplace(FSplinePointData(A.Position,FVector(12000,0,0))); Link.Route.Emplace(FSplinePointData(B.Position,FVector(0,12000,0)));
        Fixture.Nodes={A,B}; Fixture.Edges={Link}; Store->RailPlans={Fixture};
        Store->RailPlans[0].SegmentLength=FBSPlanBuild::MaximumLength()*.5; Store->RailPlans[0].Remainder=EBSPlanRemainder::Last;
        ExtensionPlan=Fixture.Id; ExtensionNode=B.Id; ExtensionPosition=B.Position;
        R->DeliveredPlanHash=0; R->LastPlanPoll=-100; Plan->Toggle(); Plan->Load(ExtensionPlan); Plan->SetMode(false); E->StickMode=EBSStickMode::Off; E->StickTiltSet=false;
        P->SetActorLocation(ExtensionPosition+FVector(0,-3000,-100));
    }
    if (Phase==49)
    {
        Check(TEXT("Plan snapshot transmits segment settings"),Plan->Current() && Plan->Current()->SegmentLength==FBSPlanBuild::MaximumLength()*.5 && Plan->Current()->Remainder==EBSPlanRemainder::Last);
        FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View); PC->SetControlRotation((ExtensionPosition-Eye).Rotation());
        FTimerHandle Click;
        P->GetWorldTimerManager().SetTimer(Click,[Weak=TWeakObjectPtr<UEnhancedPlayerInput>(Input)]() {
            if (auto* I=Weak.Get()) I->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::LeftMouseButton,IE_Pressed,1.f,false,0));
        },.2f,false);
    }
    if (Phase==50)
    {
        Key(EKeys::LeftMouseButton,false);
        Check(TEXT("Clicking an existing node starts an extension without duplicating the node"),Plan->LastNode==ExtensionNode && Plan->Current() && Plan->Current()->Nodes.Num()==2);
        E->StickMode=EBSStickMode::Default; E->StickDistance=64; PC->SetControlRotation(FRotator(0,30,0));
    }
    if (Phase==51)
    {
        Plan->AimAndHit();
        Check(TEXT("Off-axis extension preview inherits the existing spline endpoint direction"),Plan->HaveAim && Plan->Aim.Direction.Equals(FVector::RightVector,.0001) && !Plan->HoverNode.IsValid());
        Key(EKeys::LeftMouseButton,true);
    }
    if (Phase==52)
    {
        Key(EKeys::LeftMouseButton,false);
        const auto* Current=Plan->Current(); const auto* N=Current ? FBSPlanMath::Node(*Current,Plan->LastNode) : nullptr;
        bool Continuous=Current && Current->Nodes.Num()==3 && Current->Edges.Num()==2 && N && N->Direction.Equals(FVector::RightVector,.0001);
        if (Continuous) { const auto Added=FBSPlanMath::Curve(*Current,Current->Edges.Last()); Continuous &= Added.Num()>=2 && FMath::Abs(Added[0].LeaveTangent.GetSafeNormal().Y)>.9999 && FMath::Abs(Added.Last().ArriveTangent.GetSafeNormal().Y)>.9999; }
        Check(TEXT("Placed extension preserves the network tangent at its shared and new nodes"),Continuous);
        if (Current && !Current->Nodes.IsEmpty())
        {
            const auto First=Current->Nodes[0];
            Plan->LastNode=First.Id; E->StickMode=EBSStickMode::Constrained;
            P->SetActorLocation(First.Position+FVector(-1000,0,-100)); PC->SetControlRotation(FRotator(0,180,0));
        }
    }
    if (Phase==53)
    {
        // Let the native player camera update after moving/turning in phase 52.
        Plan->AimAndHit();
        FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View);
        UE_LOG(LogBuildingSkills,Display,TEXT("PLAN OUTWARD: points=%d eye=%s view=%s aim=%s tangent=%s"),Plan->AimRoute.Num(),*Eye.ToString(),*View.ToString(),*Plan->Aim.Position.ToString(),Plan->AimRoute.IsEmpty()?TEXT("none"):*Plan->AimRoute[0].LeaveTangent.ToString());
        Check(TEXT("Constrained extension from the first node follows the outward spline tangent"),Plan->AimRoute.Num()>=2 && FVector::DotProduct(Plan->AimRoute[0].LeaveTangent.GetSafeNormal(),-FVector::ForwardVector)>.9999);
        E->StickMode=EBSStickMode::Default;
        Plan->Selection.Reset(); Plan->Settings();
        if (auto* Window=Plan->Window.Get()) for (const auto& Section:Window->Sections) Section.Value->SetExpanded(Section.Key==TEXT("Segments"));
        auto* W=Plan->Window.Get();
        Check(TEXT("Build specification exposes length, remainder and signal defaults"),W && W->Sections.Contains(TEXT("Segments")) && W->SegmentLengthDraft==FBSPlanBuild::MaximumLength()*.5 && W->RemainderDraft==EBSPlanRemainder::Last && !W->PlaceBlockSignalDraft && W->SignalEveryDraft==1 && !W->LeftSideSignalDraft);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsPlanBuildSpecification.png"),true,false);
        if (const auto* Source=Plan->Current())
        {
            auto Copy=*Source; Copy.PlaceBlockSignal=true; Copy.SignalEvery=8; Copy.LeftSideSignal=true; Copy.Parallels={FVector(0,600,0)}; Copy.ParallelReversed={true}; Copy.ReversedEdges={Copy.Edges[0].Id};
            FBSRailPlan Restored; TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Save(Writer,false); Save.ArIsSaveGame=true; FBSRailPlan::StaticStruct()->SerializeItem(Save,&Copy,nullptr);
            FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Load(Reader,true); Load.ArIsSaveGame=true; FBSRailPlan::StaticStruct()->SerializeItem(Load,&Restored,nullptr);
            Check(TEXT("Save archive retains complete build specification and signal directions"),!Save.IsError() && !Load.IsError() && Restored.SegmentLength==Copy.SegmentLength && Restored.Remainder==Copy.Remainder && Restored.PlaceBlockSignal && Restored.SignalEvery==8 && Restored.LeftSideSignal && Restored.ParallelReversed==Copy.ParallelReversed && Restored.ReversedEdges==Copy.ReversedEdges);
        }
    }
    if (Phase==54)
    {
        if (auto* W=Plan->Window.Get())
        {
            W->SegmentLengthDraft=8000; W->SignalEveryDraft=3; W->SaveSpecification();
            const auto* Saved=Store->RailPlans.FindByPredicate([&](const auto& V) { return V.Id==Plan->CurrentId; });
            Check(TEXT("Menu field commit saves the build specification without an Apply button"),Saved && Saved->SegmentLength==8000 && Saved->SignalEvery==3);
        }
        Plan->Close(true); Passed &= RunBuildingSkillsPlanPolishChecks(P,Check); Store->RailPlans.Reset(); Store->PlanHistories.Reset();
    }
    if (Phase==46)
    {
        if (InsertedId.IsValid()) Check(TEXT("K shows the selected node yaw pitch and roll"),Plan->Window && Plan->Window->NodeRotation.Equals(FBSPlanMath::Rotation(*FBSPlanMath::Node(*Plan->Current(),InsertedId)),.001));
        Plan->Close(true); Store->RailPlans.Reset(); Store->PlanHistories.Reset(); if (OtherOwner.IsValid()) OtherOwner->Destroy();
    }
    return Passed;
}
};
bool RunBuildingSkillsPlanChecks(AFGCharacterPlayer* Player,int32 Phase) { return FBSPlanSmoke::Run(Player,Phase); }
