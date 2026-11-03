#include "BSPlan.h"
#include "BSStick.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGPipeConnectionComponentHyper.h"
#include "Algo/Reverse.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
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
#include "Hologram/FGPipelineHologram.h"
#include "Hologram/FGRailroadSignalHologram.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Buildables/FGBuildableRailroadSignal.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "Equipment/FGBuildGun.h"
#include "Misc/ScopeExit.h"
#include "BuildingSkills.h"
#include "FGConstructDisqualifier.h"

namespace
{
    AFGSplineHologram* PlanDefaults(EBSPlanBuildable Type)
    {
        const auto Class=AFGBuildable::GetBuildableClassFromRecipe(FBSPlanBuild::Recipe(Type));
        const auto Hologram=Class ? Class.GetDefaultObject()->mHologramClass : nullptr;
        return Hologram ? Cast<AFGSplineHologram>(Hologram.GetDefaultObject()) : nullptr;
    }
    int32 PlanFamily(EBSPlanBuildable Type)
    { return Type==EBSPlanBuildable::Rail ? 0 : Type==EBSPlanBuildable::Hypertube ? 3 : Type==EBSPlanBuildable::Pipe || Type==EBSPlanBuildable::PipeMk2 ? 1 : 2; }
    struct FPlanPort
    {
        USceneComponent* Component=nullptr;
        int32 Family=0;
        bool SnapOnly() const
        {
            if (auto* C=Cast<UFGFactoryConnectionComponent>(Component)) return C->GetDirection()==EFactoryConnectionDirection::FCD_SNAP_ONLY;
            if (auto* C=Cast<UFGPipeConnectionComponentBase>(Component)) return C->GetPipeConnectionType()==EPipeConnectionType::PCT_SNAP_ONLY;
            return false;
        }
        bool Occupied() const
        {
            if (auto* C=Cast<UFGFactoryConnectionComponent>(Component)) return C->IsConnected();
            if (auto* C=Cast<UFGPipeConnectionComponentBase>(Component)) return C->GetConnection()!=nullptr;
            return false;
        }
        FVector Position() const { const auto* R=Cast<UFGRailroadTrackConnectionComponent>(Component); return R ? R->GetConnectorLocation() : Component->GetComponentLocation(); }
        FVector Normal() const { const auto* R=Cast<UFGRailroadTrackConnectionComponent>(Component); return R ? R->GetConnectorNormal() : Component->GetForwardVector(); }
    };
    struct FPlanPortIndex
    {
        TMap<FIntVector,TArray<int32>> Cells;
        static FIntVector Cell(FVector P) { return FIntVector(FMath::FloorToInt(P.X/100),FMath::FloorToInt(P.Y/100),FMath::FloorToInt(P.Z/100)); }
        void Add(const FPlanPort& Port,int32 Index) { Cells.FindOrAdd(Cell(Port.Position())).Add(Index); }
        explicit FPlanPortIndex(const TArray<FPlanPort>& Ports)
        { for (int32 I=0;I<Ports.Num();++I) Cells.FindOrAdd(Cell(Ports[I].Position())).Add(I); }
        TArray<int32> Near(FVector P) const
        {
            TArray<int32> Result; const auto A=Cell(P-FVector(.5)),B=Cell(P+FVector(.5));
            for (int32 X=A.X;X<=B.X;++X) for (int32 Y=A.Y;Y<=B.Y;++Y) for (int32 Z=A.Z;Z<=B.Z;++Z)
                if (const auto* Items=Cells.Find(FIntVector(X,Y,Z))) Result.Append(*Items);
            return Result;
        }
    };
    void PlanPorts(AActor* Actor,TArray<FPlanPort>& Ports)
    {
        TInlineComponentArray<UFGRailroadTrackConnectionComponent*> Rails(Actor);
        for (auto* C:Rails) Ports.Add({C,0});
        TInlineComponentArray<UFGFactoryConnectionComponent*> Belts(Actor);
        for (auto* C:Belts) Ports.Add({C,2});
        TInlineComponentArray<UFGPipeConnectionComponentBase*> Pipes(Actor);
        for (auto* C:Pipes) Ports.Add({C,C->IsA<UFGPipeConnectionComponentHyper>() ? 3 : 1});
    }
    bool ConnectPlanPorts(const FPlanPort& A,const FPlanPort& B)
    {
        if (auto* X=Cast<UFGRailroadTrackConnectionComponent>(A.Component))
        {
            auto* Y=CastChecked<UFGRailroadTrackConnectionComponent>(B.Component);
            if (!X->GetConnections().Contains(Y) && X->CanConnectTo(Y)) X->AddConnection(Y);
            return X->GetConnections().Contains(Y) && Y->GetConnections().Contains(X);
        }
        if (auto* X=Cast<UFGFactoryConnectionComponent>(A.Component))
        {
            auto* Y=CastChecked<UFGFactoryConnectionComponent>(B.Component);
            if (X->GetConnection()==Y && Y->GetConnection()==X) return true;
            if (X->IsConnected() || Y->IsConnected() || !X->CanConnectTo(Y)) return false;
            X->SetConnection(Y); return X->GetConnection()==Y && Y->GetConnection()==X;
        }
        auto* X=CastChecked<UFGPipeConnectionComponentBase>(A.Component);
        auto* Y=CastChecked<UFGPipeConnectionComponentBase>(B.Component);
        if (X->GetConnection()==Y && Y->GetConnection()==X) return true;
        if (X->GetConnection() || Y->GetConnection() || !X->CanConnectTo(Y)) return false;
        X->SetConnection(Y); return X->GetConnection()==Y && Y->GetConnection()==X;
    }
}
const TCHAR* FBSPlanBuild::Name(EBSPlanBuildable Type)
{
    const TCHAR* Names[]={TEXT("Rail"),TEXT("Pipe Mk.1"),TEXT("Pipe Mk.2"),TEXT("Belt Mk.1"),TEXT("Belt Mk.2"),TEXT("Belt Mk.3"),TEXT("Belt Mk.4"),TEXT("Belt Mk.5"),TEXT("Belt Mk.6"),TEXT("Hypertube")};
    return uint8(Type)<UE_ARRAY_COUNT(Names) ? Names[uint8(Type)] : TEXT("Unknown");
}
TSubclassOf<UFGRecipe> FBSPlanBuild::Recipe(EBSPlanBuildable Type)
{
    const TCHAR* Names[]={TEXT("Recipe_RailroadTrack"),TEXT("Recipe_Pipeline"),TEXT("Recipe_PipelineMK2"),TEXT("Recipe_ConveyorBeltMk1"),TEXT("Recipe_ConveyorBeltMk2"),TEXT("Recipe_ConveyorBeltMk3"),TEXT("Recipe_ConveyorBeltMk4"),TEXT("Recipe_ConveyorBeltMk5"),TEXT("Recipe_ConveyorBeltMk6"),TEXT("Recipe_PipeHyper")};
    if (uint8(Type)>=UE_ARRAY_COUNT(Names)) return nullptr;
    static TMap<EBSPlanBuildable,TWeakObjectPtr<UClass>> Cache;
    if (auto* Cached=Cache.Find(Type); Cached && Cached->IsValid()) return Cached->Get();
    const auto* N=Names[uint8(Type)];
    auto* Class=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/%s.%s_C"),N,N));
    if (Class) Cache.Add(Type,Class); return Class;
}
double FBSPlanBuild::MaximumLength(EBSPlanBuildable Type)
{
    auto* H=PlanDefaults(Type);
    // Native validation rejects the exact floating-point boundary.
    return H ? FMath::FloorToDouble(FBSStick::MaxLength(H)-.1) : 0;
}
double FBSPlanBuild::MinimumLength(EBSPlanBuildable Type)
{
    if (PlanFamily(Type)==1 || PlanFamily(Type)==3) return AFGPipelineHologram::MINIMUM_HOLOGRAM_LENGTH;
    if (Type!=EBSPlanBuildable::Rail) return 100;
    const auto Class=AFGBuildable::GetBuildableClassFromRecipe(Recipe(Type));
    const auto* Track=Class ? Cast<AFGBuildableRailroadTrack>(Class.GetDefaultObject()) : nullptr;
    return Track ? FMath::CeilToDouble(Track->GetMeshLength()) : 0;
}
FVector FBSPlanBuild::ShapeLimits(EBSPlanBuildable Type)
{
    auto Limits=FBSStick::PlanLimits(PlanDefaults(Type)); Limits.Z=MinimumLength(Type); return Limits;
}
bool FBSPlanBuild::SnapConnection(UWorld* World,EBSPlanBuildable Type,FVector& Position,FVector& Direction,AActor* Hit)
{
    TSet<AFGBuildable*> Actors;
    if (auto* B=Cast<AFGBuildable>(Hit)) Actors.Add(B);
    TArray<FOverlapResult> Nearby;
    World->OverlapMultiByObjectType(Nearby,Position,FQuat::Identity,FCollisionObjectQueryParams::AllObjects,FCollisionShape::MakeSphere(200));
    for (const auto& Result:Nearby) if (auto* B=Cast<AFGBuildable>(Result.GetActor())) Actors.Add(B);
    TArray<FPlanPort> Ports; for (auto* A:Actors) if (IsValid(A) && !A->GetIsDismantled()) PlanPorts(A,Ports);
    double Nearest=200*200; const FVector Target=Position; bool Snapped=false;
    for (const auto& Port:Ports)
    {
        if (Port.Family!=PlanFamily(Type)) continue;
        if (auto* C=Cast<UFGFactoryConnectionComponent>(Port.Component); C && C->IsConnected()) continue;
        if (auto* C=Cast<UFGPipeConnectionComponentBase>(Port.Component); C && C->GetConnection()) continue;
        const double D=FVector::DistSquared(Target,Port.Position()); if (D>=Nearest) continue;
        Nearest=D; Position=Port.Position(); Direction=Port.Normal(); Snapped=true;
    }
    return Snapped;
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
    auto* Recipes=AFGRecipeManager::Get(P->GetWorld()); auto* Inventory=P->GetInventory();
    if (!Inventory || !Recipes) { Error=TEXT("Player inventory or recipes are unavailable."); return false; }
    TArray<FBSPlanLengthLimits> LineLimits;
    bool HasRails=false;
    for (int32 I=0;I<=Plan.Parallels.Num();++I)
    {
        const auto Type=Plan.Construction.Line(I); const auto R=Recipe(Type);
        if (!R || !Recipes->IsRecipeAvailable(R)) { Error=FString::Printf(TEXT("Unlock %s before building this plan."),Name(Type)); return false; }
        LineLimits.Add({MinimumLength(Type),MaximumLength(Type),Plan.Construction.Length(Type,Plan.SegmentLength)});
        HasRails |= Type==EBSPlanBuildable::Rail;
    }
    TArray<AFGSplineHologram*> Holograms; Holograms.Add(nullptr);
    struct FSignalRequest { int32 Track,End; AFGRailroadSignalHologram* Hologram; };
    TArray<FSignalRequest> Signals;
    struct FRequiredJoin { int32 A,B; FVector Position; };
    TArray<FRequiredJoin> RequiredJoins;
    struct FSupportPair { UObject* Start=nullptr; UObject* End=nullptr; };
    TArray<FSupportPair> SupportPairs; SupportPairs.Add({});
    TSet<USceneComponent*> ClaimedWorldPorts;
    TArray<FPlanPort> Supports; FPlanPortIndex SupportIndex(Supports);
    TArray<AActor*> Built;
    bool Success=false;
    ON_SCOPE_EXIT {
        for (auto* H:Holograms) if (IsValid(H)) H->Destroy();
        for (const auto& S:Signals) if (IsValid(S.Hologram)) S.Hologram->Destroy();
        if (!Success) for (int32 I=Built.Num()-1;I>=0;--I) if (auto* A=Built[I]; IsValid(A) && A->Implements<UFGDismantleInterface>()) IFGDismantleInterface::Execute_Dismantle(A);
    };
    TArray<FItemAmount> Cost;
    TSubclassOf<UFGRecipe> SignalRecipe;
    if (Plan.PlaceBlockSignal && HasRails)
    {
        SignalRecipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/Train/Signal/Recipe_RailroadBlockSignal.Recipe_RailroadBlockSignal_C"));
        if (!SignalRecipe || !Recipes->IsRecipeAvailable(SignalRecipe)) { Error=TEXT("Unlock Block Signals before building this specification."); return false; }
    }
    // Resolve world connections at build time; no plan stores live actors.
    TArray<FPlanPort> Existing;
    TArray<UFGRailroadTrackConnectionComponent*> ExistingPorts;
    for (TActorIterator<AFGBuildable> It(P->GetWorld());It;++It)
        if (IsValid(*It) && !It->GetIsDismantled() && !It->GetIsLightweightTemporary() && !It->GetBlueprintDesigner()) PlanPorts(*It,Existing);
    for (const auto& Port:Existing) if (Port.Family==0) ExistingPorts.Add(CastChecked<UFGRailroadTrackConnectionComponent>(Port.Component));
    const FPlanPortIndex ExistingIndex(Existing);
    TSet<FGuid> Junctions;
    TArray<FBSPlanPath> PreviewPaths;
    if (!FBSPlanSegments::Paths(Plan,PreviewPaths,Error,false)) return false;
    for (const auto& Path:PreviewPaths) for (const auto& Node:Path.Nodes)
    {
        const FVector Forward=FBSPlanMath::ConnectionDirection(Plan,Node.Id);
        for (int32 Index:ExistingIndex.Near(Node.Position))
        {
            const auto& Port=Existing[Index];
            if (Port.Family==PlanFamily(Plan.Construction.Line(Path.Parallel)) && FMath::Abs(FVector::DotProduct(Port.Normal(),Forward))>.999 && Port.Position().Equals(Node.Position,.5)) Junctions.Add(Node.Id);
        }
    }
    TArray<FBSPlanSegmentedPath> Paths;
    if (!FBSPlanSegments::Prepare(Plan,LineLimits[0].Minimum,LineLimits[0].Maximum,Paths,Error,Junctions,LineLimits)) return false;
    for (const auto& Segmented:Paths)
    {
        const auto& Path=Segmented.Path;
        const auto Type=Plan.Construction.Line(Path.Parallel);
        const auto RecipeClass=Recipe(Type); const FVector Limits=ShapeLimits(Type);
        const auto& Curve=Path.Points;
        if (Curve.Num()<2) { Error=TEXT("A path has no usable curve."); return false; }
        if ((Path.Start.IsValid() && FMath::Abs(FVector::DotProduct(Curve[0].LeaveTangent.GetSafeNormal(),FBSPlanMath::ConnectionDirection(Plan,Path.Start)))<.999) ||
            (Path.End.IsValid() && FMath::Abs(FVector::DotProduct(Curve.Last().ArriveTangent.GetSafeNormal(),FBSPlanMath::ConnectionDirection(Plan,Path.End)))<.999))
        { Error=TEXT("A path does not meet its node tangent continuously. Use Curve or Free, or adjust the nodes."); return false; }
        for (int32 I=1;I<Curve.Num();++I) for (int32 Sample=0;Sample<=32;++Sample)
            if (!FBSPlanMath::ShapeValid(Curve[I-1],Curve[I],double(Sample)/32,Limits.X,Limits.Y))
            { Error=TEXT("A red section exceeds the selected building type's curvature or grade limit. Adjust it before building."); return false; }
        const auto& Parts=Segmented.Parts;
        if (Holograms.Num()+Parts.Num()>2049)
        { Error=TEXT("This plan exceeds the build limit of 2048 segments."); return false; }
        const int32 FirstIndex=Holograms.Num();
        for (int32 I=0;I<Parts.Num();++I)
        {
            auto Points=Parts[I];
            if (PlanFamily(Type)==2 && Path.SignalReverse)
            { Algo::Reverse(Points); for (auto& Point:Points) { Swap(Point.ArriveTangent,Point.LeaveTangent); Point.ArriveTangent*=-1; Point.LeaveTangent*=-1; } }
            auto* H=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(RecipeClass,P->GetBuildGun(),Points[0].Location,P));
            if (!H) { Error=TEXT("Could not prepare every segment."); return false; }
            const int32 Index=Holograms.Add(H);
            if (I>0) RequiredJoins.Add({Index-1,Index,Parts[I][0].Location});
            FBSStick::PlanGeometry(H,Points);
            FSupportPair Pair;
            if (Type!=EBSPlanBuildable::Rail) for (int32 End=0;End<2;++End)
            {
                const FVector At=End==0 ? Points[0].Location : Points.Last().Location;
                const FVector Normal=(End==0 ? -Points[0].LeaveTangent : Points.Last().ArriveTangent).GetSafeNormal();
                UObject* Target=nullptr;
                // Prefer a real transport connection over a decorative support.
                for (bool SnapOnly:{false,true}) for (int32 PortIndex:ExistingIndex.Near(At))
                {
                    const auto& Port=Existing[PortIndex];
                    if (!Target && Port.Family==PlanFamily(Type) && Port.SnapOnly()==SnapOnly && Port.Position().Equals(At,.5) &&
                        (SnapOnly || FVector::DotProduct(Port.Normal(),Normal)<-.999)) Target=Port.Component;
                }
                if (Target)
                {
                    const FPlanPort Port{CastChecked<USceneComponent>(Target),PlanFamily(Type)};
                    if (!Port.SnapOnly())
                    {
                        // Reject before native Construct can touch a live
                        // network. One transport port accepts one new endpoint.
                        if (Port.Occupied() || ClaimedWorldPorts.Contains(Port.Component))
                        { Error=TEXT("An existing connection is occupied or requested more than once. Nothing was built."); return false; }
                        ClaimedWorldPorts.Add(Port.Component);
                    }
                }
                if (!Target) for (int32 PortIndex:SupportIndex.Near(At))
                {
                    const auto& Port=Supports[PortIndex];
                    if (Port.Family==PlanFamily(Type) && Port.Position().Equals(At,.5)) { Target=Port.Component; break; }
                }
                UObject* Support=FBSStick::PlanSupport(H,End,Target);
                if (!Support) { Error=TEXT("Could not prepare a native spline support. Nothing was built."); return false; }
                if (!Target)
                { const int32 Added=Supports.Add({CastChecked<USceneComponent>(Support),PlanFamily(Type)}); SupportIndex.Add(Supports[Added],Added); }
                (End==0 ? Pair.Start : Pair.End)=Support;
            }
            SupportPairs.Add(Pair);
            Cost.Append(H->GetCost(true));
        }
        if (Path.Start.IsValid() && Path.Start==Path.End && Parts.Num()>1) RequiredJoins.Add({FirstIndex,Holograms.Num()-1,Parts[0][0].Location});
        if (Plan.PlaceBlockSignal && Type==EBSPlanBuildable::Rail) for (int32 I=Plan.SignalEvery-1;I<Parts.Num();I+=Plan.SignalEvery)
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
        auto* H=Holograms[I];
        if (!FBSStick::ValidatePlan(H,Inventory,SupportPairs[I].Start,SupportPairs[I].End))
        {
            TArray<TSubclassOf<UFGConstructDisqualifier>> Reasons; H->GetConstructDisqualifiers(Reasons);
            for (const auto& Reason:Reasons) UE_LOG(LogBuildingSkills,Display,TEXT("Plan spline validation: %s"),*GetNameSafe(Reason.Get()));
            Error=TEXT("A segment fails native placement or affordability checks. Nothing was built."); return false;
        }
    }
    TArray<AFGBuildable*> Tracks; Tracks.SetNum(Holograms.Num());
    for (int32 I=1;I<Holograms.Num();++I)
    {
        TArray<AActor*> Children; auto* Track=Cast<AFGBuildable>(Holograms[I]->Construct(Children,FNetConstructionID()));
        Built.Append(Children); if (Track) Built.AddUnique(Track); Tracks[I]=Track;
        if (!Track) { Error=TEXT("Construction failed; the incomplete plan was removed."); return false; }
    }
    // Connect exact endpoints within each native family, including world
    // fittings. An occupied or incompatible join rolls back the whole build.
    for (const auto& Joint:RequiredJoins) if (!FBSStick::Connect(Tracks[Joint.A],Tracks[Joint.B],Joint.Position))
    { Error=TEXT("An internal spline join could not connect. The incomplete plan was removed."); return false; }
    TArray<FPlanPort> NewPorts;
    for (auto* Track:Tracks) if (Track) PlanPorts(Track,NewPorts);
    const FPlanPortIndex NewIndex(NewPorts);
    auto Join=[&](const FPlanPort& A,const FPlanPort& B) {
        if (!IsValid(A.Component) || !IsValid(B.Component)) return true;
        if (A.SnapOnly() || B.SnapOnly() || A.Family!=B.Family || A.Component->GetOwner()==B.Component->GetOwner() ||
            !A.Position().Equals(B.Position(),.5) || FVector::DotProduct(A.Normal(),B.Normal())>-.999) return true;
        return ConnectPlanPorts(A,B);
    };
    for (int32 I=0;I<NewPorts.Num();++I)
    {
        for (int32 J:NewIndex.Near(NewPorts[I].Position())) if (J<I && !Join(NewPorts[I],NewPorts[J]))
        { Error=TEXT("A planned join cannot connect. Belts and tubes need fittings for branches. The incomplete plan was removed."); return false; }
        for (int32 Index:ExistingIndex.Near(NewPorts[I].Position())) if (!Join(NewPorts[I],Existing[Index]))
        { Error=TEXT("An existing connection is occupied or incompatible. The incomplete plan was removed."); return false; }
    }
    for (const auto& S:Signals)
    {
        auto* Port=CastChecked<AFGBuildableRailroadTrack>(Tracks[S.Track])->GetConnection(S.End); auto* H=S.Hologram;
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
    Success=true; Error=FString::Printf(TEXT("Built %d segments and %d block signals. The plan is retained."),Tracks.Num()-1,Signals.Num()); return true;
}
