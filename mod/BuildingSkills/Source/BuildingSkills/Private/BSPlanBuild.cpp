#include "BSPlan.h"
#include "BSStick.h"
#include "EngineUtils.h"
#include "Hologram/HologramHelpers.h"
#include "FGCharacterPlayer.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "FGInventoryComponent.h"
#include "FGInventoryLibrary.h"
#include "FGCentralStorageSubsystem.h"
#include "FGPlayerState.h"
#include "Resources/FGItemDescriptor.h"
#include "FGDismantleInterface.h"
#include "Hologram/FGRailroadTrackHologram.h"
#include "Hologram/FGRailroadSignalHologram.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Buildables/FGBuildableRailroadSignal.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "Equipment/FGBuildGun.h"
#include "Misc/ScopeExit.h"
#include "BuildingSkills.h"
#include "FGConstructDisqualifier.h"

TSubclassOf<UFGRecipe> FBSPlanBuild::Recipe()
{ return LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_RailroadTrack.Recipe_RailroadTrack_C")); }
double FBSPlanBuild::MaximumLength()
{
    const auto Class=AFGBuildable::GetBuildableClassFromRecipe(Recipe());
    const auto Hologram=Class ? Class.GetDefaultObject()->mHologramClass : nullptr;
    auto* H=Hologram ? Cast<AFGRailroadTrackHologram>(Hologram.GetDefaultObject()) : nullptr;
    // Native validation rejects the exact float boundary. Expose a usable
    // maximum at the menu's centimeter precision instead of an unbuildable value.
    return H ? FMath::FloorToDouble(FBSStick::MaxLength(H)-.1) : 0;
}
double FBSPlanBuild::MinimumLength()
{
    const auto Class=AFGBuildable::GetBuildableClassFromRecipe(Recipe());
    const auto* Track=Class ? Cast<AFGBuildableRailroadTrack>(Class.GetDefaultObject()) : nullptr;
    // Native BeginPlay initializes mMinLength from the buildable mesh length.
    // The hologram CDO still has zero, so it cannot provide this limit.
    return Track ? FMath::CeilToDouble(Track->GetMeshLength()) : 0;
}
void FBSPlanBuild::SnapFoundation(AFGBuildable* Floor,FVector& Position,FVector& Direction)
{
    const auto Class=AFGBuildable::GetBuildableClassFromRecipe(Recipe());
    auto* H=Class && Class.GetDefaultObject()->mHologramClass ? Cast<AFGBuildableHologram>(Class.GetDefaultObject()->mHologramClass.GetDefaultObject()) : nullptr;
    if (!Floor || !H) return;
    const float Yaw=FHologramHelpers::SnapToFloor(Floor,H->mGridSnapSize,true,Position);
    auto Rotation=Direction.Rotation(); Rotation.Yaw=Yaw+FMath::GridSnap(Rotation.Yaw-Yaw,double(H->GetRotationStep())); Direction=Rotation.Vector();
}
bool FBSPlanBuild::Build(AFGCharacterPlayer* P,const FBSRailPlan& Plan,FString& Error)
{
    if (!P || !P->HasAuthority() || !FBSPlanMath::Valid(Plan) || Plan.Edges.IsEmpty()) { Error=TEXT("Add a connected path before building."); return false; }
    auto RecipeClass=Recipe(); auto* Recipes=AFGRecipeManager::Get(P->GetWorld()); auto* Inventory=P->GetInventory();
    if (!Inventory || !Recipes || !Recipes->IsRecipeAvailable(RecipeClass)) { Error=TEXT("Unlock Railway before building this plan."); return false; }
    TArray<AFGRailroadTrackHologram*> Holograms;
    struct FSignalRequest { int32 Track,End; AFGRailroadSignalHologram* Hologram; };
    TArray<FSignalRequest> Signals;
    TArray<AActor*> Built;
    bool Success=false;
    ON_SCOPE_EXIT {
        for (auto* H:Holograms) if (IsValid(H)) H->Destroy();
        for (const auto& S:Signals) if (IsValid(S.Hologram)) S.Hologram->Destroy();
        if (!Success) for (int32 I=Built.Num()-1;I>=0;--I) if (auto* A=Built[I]; IsValid(A) && A->Implements<UFGDismantleInterface>()) IFGDismantleInterface::Execute_Dismantle(A);
    };
    struct FEndpoint { int32 Hologram; int32 End; FGuid Node; int32 Parallel; };
    TArray<FEndpoint> Ends;
    TArray<FItemAmount> Cost;
    TSubclassOf<UFGRecipe> SignalRecipe;
    if (Plan.PlaceBlockSignal)
    {
        SignalRecipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/Train/Signal/Recipe_RailroadBlockSignal.Recipe_RailroadBlockSignal_C"));
        if (!SignalRecipe || !Recipes->IsRecipeAvailable(SignalRecipe)) { Error=TEXT("Unlock Block Signals before building this specification."); return false; }
    }
    auto* Template=Cast<AFGRailroadTrackHologram>(AFGHologram::SpawnHologramFromRecipe(RecipeClass,P->GetBuildGun(),Plan.Nodes[0].Position,P));
    if (!Template) { Error=TEXT("Could not create the rail preview."); return false; }
    Holograms.Add(Template); const FVector Limits=FBSStick::PlanLimits(Template); const double Max=FMath::FloorToDouble(FBSStick::MaxLength(Template)-.1);
    // Discover the current world at build time. Plans store geometry, never
    // references to the rails from which it was sampled.
    TArray<UFGRailroadTrackConnectionComponent*> ExistingPorts;
    for (TActorIterator<AFGBuildableRailroadTrack> It(P->GetWorld());It;++It)
        if (IsValid(*It) && !It->GetIsDismantled()) for (int32 End=0;End<2;++End) if (auto* Port=It->GetConnection(End)) ExistingPorts.Add(Port);
    TSet<FGuid> Junctions;
    TArray<FBSPlanPath> PreviewPaths;
    if (!FBSPlanSegments::Paths(Plan,PreviewPaths,Error,false)) return false;
    for (const auto& Path:PreviewPaths) for (const auto& Node:Path.Nodes)
    {
        const FVector Forward=FBSPlanMath::ConnectionDirection(Plan,Node.Id);
        for (auto* Port:ExistingPorts) if (FMath::Abs(FVector::DotProduct(Port->GetConnectorNormal(),Forward))>.999)
            if (Port->GetConnectorLocation().Equals(Node.Position,.5)) Junctions.Add(Node.Id);
    }
    TArray<FBSPlanPath> Paths;
    if (!FBSPlanSegments::Paths(Plan,Paths,Error,true,Junctions)) return false;
    for (const auto& Path:Paths)
    {
        const auto& Curve=Path.Points;
        if (Curve.Num()<2) { Error=TEXT("A path has no usable curve."); return false; }
        if ((Path.Start.IsValid() && FMath::Abs(FVector::DotProduct(Curve[0].LeaveTangent.GetSafeNormal(),FBSPlanMath::ConnectionDirection(Plan,Path.Start)))<.999) ||
            (Path.End.IsValid() && FMath::Abs(FVector::DotProduct(Curve.Last().ArriveTangent.GetSafeNormal(),FBSPlanMath::ConnectionDirection(Plan,Path.End)))<.999))
        { Error=TEXT("A rail does not meet its node tangent continuously. Use Curve or Free, or adjust the nodes."); return false; }
        for (int32 I=1;I<Curve.Num();++I) for (int32 Sample=0;Sample<=32;++Sample)
            if (!FBSPlanMath::ShapeValid(Curve[I-1],Curve[I],double(Sample)/32,Limits.X,Limits.Y))
            { Error=TEXT("A red section exceeds the rail curvature or grade limit. Adjust it before building."); return false; }
        TArray<TArray<FSplinePointData>> Parts;
        if (!FBSPlanSegments::Split(Curve,Plan.SegmentLength,Plan.Remainder,MinimumLength(),Max,Parts,Error)) return false;
        if (Holograms.Num()+Parts.Num()>2049)
        { Error=TEXT("This plan exceeds the build limit of 2048 rail segments."); return false; }
        const int32 FirstIndex=Holograms.Num();
        for (int32 I=0;I<Parts.Num();++I)
        {
            auto* H=Cast<AFGRailroadTrackHologram>(AFGHologram::SpawnHologramFromRecipe(RecipeClass,P->GetBuildGun(),Parts[I][0].Location,P));
            if (!H) { Error=TEXT("Could not prepare every rail segment."); return false; }
            const int32 Index=Holograms.Add(H);
            FBSStick::PlanGeometry(H,Parts[I]);
            Cost.Append(H->GetCost(true));
            Ends.Add({Index,0,I==0 ? Path.Start : FGuid(),Path.Parallel});
            Ends.Add({Index,1,I==Parts.Num()-1 ? Path.End : FGuid(),Path.Parallel});
        }
        if (Plan.PlaceBlockSignal) for (int32 I=Plan.SignalEvery-1;I<Parts.Num();I+=Plan.SignalEvery)
        {
            if (I==Parts.Num()-1 && Path.Start!=Path.End)
            {
                const FGuid Id=Path.SignalReverse ? Path.Start : Path.End;
                int32 Degree=0;
                for (const auto& Edge:Plan.Edges) Degree+=Edge.Start==Id || Edge.End==Id;
                // Native block signals need a joint, not an unconnected open end.
                const auto& Endpoint=Path.SignalReverse ? Curve[0] : Curve.Last();
                const FVector Tangent=(Path.SignalReverse ? Endpoint.LeaveTangent : Endpoint.ArriveTangent).GetSafeNormal();
                const bool LiveJoint=ExistingPorts.ContainsByPredicate([&](const auto* Port) {
                    return Port->GetConnectorLocation().Equals(Endpoint.Location,.5) && FMath::Abs(FVector::DotProduct(Port->GetConnectorNormal(),Tangent))>.999;
                });
                if (Degree<3 && !LiveJoint) continue;
            }
            const int32 Part=Path.SignalReverse ? Parts.Num()-1-I : I,End=Path.SignalReverse ? 0 : 1;
            const auto& Point=End ? Parts[Part].Last() : Parts[Part][0];
            const FVector Direction=(End ? Point.ArriveTangent : -Point.LeaveTangent).GetSafeNormal();
            const bool Duplicate=Signals.ContainsByPredicate([&](const auto& S) { return S.Hologram->GetActorLocation().Equals(Point.Location,.1) && FVector::DotProduct(S.Hologram->GetActorForwardVector(),Direction)>.999; });
            if (Duplicate) continue;
            auto* H=Cast<AFGRailroadSignalHologram>(AFGHologram::SpawnHologramFromRecipe(SignalRecipe,P->GetBuildGun(),Point.Location,P));
            if (!H) { Error=TEXT("Could not prepare every block signal. Nothing was built."); return false; }
            Signals.Add({FirstIndex+Part,End,H}); H->SetActorLocationAndRotation(Point.Location,Direction.Rotation());
            H->SetLeftHanded(Plan.LeftSideSignal); H->SetActorHiddenInGame(false); Cost.Append(H->GetCost(true));
        }
    }
    UFGInventoryLibrary::ConsolidateItemsAmount(Cost);
    auto* Central=AFGCentralStorageSubsystem::Get(P->GetWorld());
    if (!Inventory->GetNoBuildCost()) for (const auto& Item:Cost)
    {
        const int32 Backpack=Inventory->GetNumItems(Item.ItemClass),Depot=Central ? Central->GetNumItemsFromCentralStorage(Item.ItemClass) : 0;
        if (int64(Backpack)+Depot<Item.Amount)
        {
            Error=FString::Printf(TEXT("Not enough %s: need %d, inventory %d, Depot %d. Nothing was built."),*UFGItemDescriptor::GetItemName(Item.ItemClass).ToString(),Item.Amount,Backpack,Depot);
            UE_LOG(LogBuildingSkills,Display,TEXT("Plan cost: %s"),*Error); return false;
        }
    }
    for (int32 I=1;I<Holograms.Num();++I)
    {
        auto* H=Holograms[I]; H->ResetConstructDisqualifiers(); H->ValidatePlacementAndCost(Inventory);
        if (!H->CanConstruct())
        {
            TArray<TSubclassOf<UFGConstructDisqualifier>> Reasons; H->GetConstructDisqualifiers(Reasons);
            for (const auto& Reason:Reasons) UE_LOG(LogBuildingSkills,Display,TEXT("Plan rail validation: %s"),*GetNameSafe(Reason.Get()));
            Error=TEXT("A rail segment fails native placement or affordability checks. Nothing was built."); return false;
        }
    }
    TArray<AFGBuildableRailroadTrack*> Tracks; Tracks.SetNum(Holograms.Num());
    for (int32 I=1;I<Holograms.Num();++I)
    {
        TArray<AActor*> Children; auto* Track=Cast<AFGBuildableRailroadTrack>(Holograms[I]->Construct(Children,FNetConstructionID()));
        Built.Append(Children); if (Track) Built.AddUnique(Track); Tracks[I]=Track;
        if (!Track) { Error=TEXT("Construction failed; the incomplete plan was removed."); return false; }
    }
    // Link coincident opposing ports, including split joins and branching nodes.
    // Same-facing branch ports must not connect directly to one another.
    for (int32 I=0;I<Ends.Num();++I)
    {
        const auto& E=Ends[I]; auto* Port=Tracks[E.Hologram]->GetConnection(E.End);
        for (int32 J=0;J<I;++J)
        {
            const auto& Other=Ends[J]; auto* Target=Tracks[Other.Hologram]->GetConnection(Other.End);
            if (E.Hologram==Other.Hologram || !Port->GetConnectorLocation().Equals(Target->GetConnectorLocation(),.5) ||
                FVector::DotProduct(Port->GetConnectorNormal(),Target->GetConnectorNormal())>-.999) continue;
            if (!Port->GetConnections().Contains(Target) && Port->CanConnectTo(Target)) Port->AddConnection(Target);
            if (!Port->GetConnections().Contains(Target) || !Target->GetConnections().Contains(Port))
            { Error=TEXT("A rail join could not be connected; the incomplete plan was removed."); return false; }
        }
        // Check every generated endpoint, including joins at interior nodes.
        // Only matching position AND opposing tangent can form a native joint.
        for (auto* Target:ExistingPorts)
        {
            if (!IsValid(Target) || !Port->GetConnectorLocation().Equals(Target->GetConnectorLocation(),.5) ||
                FVector::DotProduct(Port->GetConnectorNormal(),Target->GetConnectorNormal())>-.999) continue;
            if (!Port->GetConnections().Contains(Target) && Port->CanConnectTo(Target)) Port->AddConnection(Target);
            if (!Port->GetConnections().Contains(Target) || !Target->GetConnections().Contains(Port))
            { Error=TEXT("An existing rail could not be connected; the incomplete plan was removed."); return false; }
        }
    }
    for (const auto& S:Signals)
    {
        auto* Port=Tracks[S.Track]->GetConnection(S.End); auto* H=S.Hologram;
        if (Port->GetFacingSignal()) { Error=TEXT("A requested signal position already has a signal. The incomplete plan was removed."); return false; }
        // Configure the native connection-snap path. No track is split here:
        // every signal lies on an already constructed segment endpoint.
        H->mSnappedConnection=Port; H->mSnappedRailroadTrack=nullptr;
        H->SetActorTransform(Port->GetComponentTransform());
        H->ResetConstructDisqualifiers(); H->ValidatePlacementAndCost(Inventory);
        if (!H->CanConstruct())
        {
            TArray<TSubclassOf<UFGConstructDisqualifier>> Reasons; H->GetConstructDisqualifiers(Reasons);
            for (const auto& Reason:Reasons) UE_LOG(LogBuildingSkills,Display,TEXT("Plan signal validation: %s"),*GetNameSafe(Reason.Get()));
            Error=TEXT("A block signal fails native placement checks. The incomplete plan was removed."); return false;
        }
        TArray<AActor*> Children; auto* Signal=Cast<AFGBuildableRailroadSignal>(H->Construct(Children,FNetConstructionID()));
        Built.Append(Children); if (Signal) Built.AddUnique(Signal);
        if (!Signal || Port->GetFacingSignal()!=Signal || Signal->IsLeftHanded()!=Plan.LeftSideSignal)
        { Error=TEXT("A block signal could not connect correctly. The incomplete plan was removed."); return false; }
    }
    if (!Inventory->GetNoBuildCost())
    {
        const auto* State=P->GetPlayerState<AFGPlayerState>();
        for (const auto& Item:Cost) UFGInventoryLibrary::GrabItemsFromInventoryAndCentralStorage(Inventory,Central,!State || State->GetTakeFromInventoryBeforeCentralStorage(),Item.ItemClass,Item.Amount);
    }
    Success=true; Error=FString::Printf(TEXT("Built %d rail segments and %d block signals. The plan is retained."),Tracks.Num()-1,Signals.Num()); return true;
}
