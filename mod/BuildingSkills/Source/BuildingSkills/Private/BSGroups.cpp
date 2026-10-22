#include "BSGroups.h"
#include "FGPlayerState.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildable.h"
#include "Equipment/FGBuildGun.h"
#include "FGBlueprintProxy.h"
#include "FGCharacterPlayer.h"
#include "FGHealthComponent.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "Subsystem/SubsystemActorManager.h"

FBSBuildingRef FBSBuildingRef::FromActor(AFGBuildable* Building)
{
    if (!IsValid(Building)) return {};
    if (Building->GetIsLightweightTemporary())
    {
        auto* LW = AFGLightweightBuildableSubsystem::Get(Building->GetWorld());
        return LW ? FromLightweight(Building->GetWorld(), Building->GetClass(), LW->GetRuntimeDataIndexForBuildable(Building)) : FBSBuildingRef{};
    }
    FBSBuildingRef Ref;
    Ref.Actor = Building;
    if (auto* Driver = Building->GetWorld()->GetNetDriver(); Driver && Driver->GuidCache.IsValid())
        Ref.NetActorId = Driver->GuidCache->GetNetGUID(Building).ObjectId;
    Ref.Transform = Building->GetActorTransform();
    if (const auto* Groups = ABSGroupSubsystem::Get(Building->GetWorld()))
        Ref.TransformVersion = (Groups->HasAuthority() ? Groups->BuildingVersions : Groups->ReceivedVersions).FindRef(FBSTransformNative::Key(Ref));
    return Ref;
}

FBSBuildingRef FBSBuildingRef::FromLightweight(UWorld* World, TSubclassOf<AFGBuildable> Class, int32 Index)
{
    auto* LW = AFGLightweightBuildableSubsystem::Get(World);
    auto* Data = LW ? LW->GetRuntimeDataForBuildableClassAndIndex(Class, Index) : nullptr;
    if (!Data || !Data->IsValid()) return {};
    FBSBuildingRef Ref;
    Ref.LightweightClass = Class;
    Ref.Recipe = Data->BuiltWithRecipe;
    Ref.Transform = Data->Transform;
    Ref.RuntimeIndex = Index;
    if (const auto* Groups = ABSGroupSubsystem::Get(World))
        Ref.TransformVersion = (Groups->HasAuthority() ? Groups->BuildingVersions : Groups->ReceivedVersions).FindRef(FBSTransformNative::Key(Ref));
    return Ref;
}

int32 FBSBuildingRef::ResolveIndex(UWorld* World) const
{
    if (const auto* Groups = ABSGroupSubsystem::Get(World)) return Groups->FindLightweightIndex(*this);
    auto* LW = World ? AFGLightweightBuildableSubsystem::Get(World) : nullptr;
    if (!LW || !LightweightClass || !Recipe) return INDEX_NONE;
    const auto* Instances = LW->GetAllLightweightBuildableInstances().Find(LightweightClass);
    if (!Instances) return INDEX_NONE;
    int32 Match = INDEX_NONE;
    // A full uniqueness check also handles compaction, reused slots, and exact overlapping copies.
    for (int32 I = 0; I < Instances->Num(); ++I)
    {
        const auto& Data = (*Instances)[I];
        if (Data.IsValid() && Data.BuiltWithRecipe == Recipe && Data.Transform.Equals(Transform, 0.01))
        {
            if (Match != INDEX_NONE) return INDEX_NONE;
            Match = I;
        }
    }
    return Match;
}

AFGBuildable* FBSBuildingRef::ResolveNetworkActor(UWorld* World) const
{
    if (IsValid(Actor)) return Actor;
    if (NetActorId && World && World->GetNetMode() == NM_Client)
        if (auto* Driver = World->GetNetDriver(); Driver && Driver->GuidCache.IsValid())
            { FNetworkGUID Id; Id.ObjectId = NetActorId; return Cast<AFGBuildable>(Driver->GuidCache->GetObjectFromNetGUID(Id, true)); }
    return nullptr;
}

bool FBSBuildingRef::IsAlive(UWorld* World) const
{
    if (LightweightClass) return ResolveIndex(World) != INDEX_NONE;
    auto* Building = ResolveNetworkActor(World);
    return IsValid(Building) && Building->GetWorld() == World && !Building->GetIsDismantled() && !Building->GetIsLightweightTemporary();
}

AFGBuildable* FBSBuildingRef::ResolveActor(UWorld* World) const
{
    if (!LightweightClass) return IsAlive(World) ? ResolveNetworkActor(World) : nullptr;
    int32 Index = ResolveIndex(World);
    if (Index == INDEX_NONE) return nullptr;
    FLightweightBuildableInstanceRef Native;
    Native.Initialize(AFGLightweightBuildableSubsystem::Get(World), LightweightClass, Index);
    return Native.SpawnTemporaryBuildable();
}

AFGBlueprintProxy* FBSBuildingRef::Blueprint(UWorld* World) const
{
    if (!LightweightClass) { auto* Building = ResolveNetworkActor(World); return IsValid(Building) ? Building->GetBlueprintProxy() : nullptr; }
    auto* LW = AFGLightweightBuildableSubsystem::Get(World);
    int32 Index = ResolveIndex(World);
    auto* Data = LW && Index != INDEX_NONE ? LW->GetRuntimeDataForBuildableClassAndIndex(LightweightClass, Index) : nullptr;
    return Data ? Data->BlueprintProxy.Get() : nullptr;
}

FVector FBSBuildingRef::Location() const { return IsValid(Actor) ? Actor->GetActorLocation() : Transform.GetLocation(); }

bool FBSBuildingRef::operator==(const FBSBuildingRef& Other) const
{
    if (LightweightClass || Other.LightweightClass)
        return LightweightClass == Other.LightweightClass && Recipe == Other.Recipe && Transform.Equals(Other.Transform, 0.01);
    if (IsValid(Actor) && IsValid(Other.Actor)) return Actor == Other.Actor;
    if (NetActorId || Other.NetActorId) return NetActorId && NetActorId == Other.NetActorId;
    return Actor == Other.Actor;
}

const FBSGroup* FBSGroupTree::Find(const TArray<FBSGroup>& Tree, FGuid Id)
{
    return Id.IsValid() ? Tree.FindByPredicate([Id](const FBSGroup& G) { return G.Id == Id; }) : nullptr;
}

bool FBSGroupTree::IsWithin(const TArray<FBSGroup>& Tree, FGuid Child, FGuid Ancestor)
{
    TSet<FGuid> Visited;
    while (Child.IsValid() && !Visited.Contains(Child))
    {
        if (Child == Ancestor) return true;
        Visited.Add(Child);
        const auto* Node = Find(Tree, Child);
        Child = Node ? Node->Parent : FGuid{};
    }
    return false;
}

FGuid FBSGroupTree::Owner(const TArray<FBSGroup>& Tree, const FBSBuildingRef& Building)
{
    for (const auto& G : Tree) if (G.Buildings.Contains(Building)) return G.Id;
    return {};
}

FGuid FBSGroupTree::Root(const TArray<FBSGroup>& Tree, FGuid Id)
{
    TSet<FGuid> Visited;
    while (const auto* G = Find(Tree, Id))
    {
        if (!G->Parent.IsValid() || Visited.Contains(Id)) return Id;
        Visited.Add(Id);
        Id = G->Parent;
    }
    return {};
}

void FBSGroupTree::Collect(const TArray<FBSGroup>& Tree, FGuid Id, TArray<FBSBuildingRef>& Out)
{
    if (!Id.IsValid()) return;
    // Walk descendants once instead of following every node's ancestry with a
    // linear lookup at each level. Keep the final emission in stored tree order:
    // selection order determines the transform anchor and merge destination.
    TMultiMap<FGuid, FGuid> Children;
    for (const auto& G : Tree) Children.Add(G.Parent, G.Id);
    TSet<FGuid> Descendants;
    TArray<FGuid> Pending = {Id};
    while (!Pending.IsEmpty())
    {
        const FGuid Next = Pending.Pop(EAllowShrinking::No);
        if (Descendants.Contains(Next)) continue;
        Descendants.Add(Next);
        for (auto It = Children.CreateConstKeyIterator(Next); It; ++It) Pending.Add(It.Value());
    }
    for (const auto& G : Tree)
        if (Descendants.Contains(G.Id))
            for (const auto& B : G.Buildings) Out.AddUnique(B);
}

FBSSelection FBSGroupTree::Normalize(const TArray<FBSGroup>& Tree, const FBSSelection& Selection)
{
    FBSSelection Result;
    for (FGuid Id : Selection.Groups)
    {
        if (!Find(Tree, Id)) continue;
        bool Covered = false;
        for (FGuid Other : Selection.Groups)
            if (Other != Id && IsWithin(Tree, Id, Other)) { Covered = true; break; }
        if (!Covered) Result.Groups.AddUnique(Id);
    }
    for (const auto& B : Selection.Buildings)
    {
        const FGuid Parent = Owner(Tree, B);
        bool Covered = false;
        for (FGuid Id : Result.Groups) if (IsWithin(Tree, Parent, Id)) { Covered = true; break; }
        if (!Covered) Result.Buildings.AddUnique(B);
    }
    return Result;
}

void FBSGroupTree::PruneEmpty(TArray<FBSGroup>& Tree)
{
    bool Changed;
    do
    {
        Changed = false;
        for (int32 I = Tree.Num() - 1; I >= 0; --I)
        {
            const FGuid Id = Tree[I].Id;
            if (Tree[I].Buildings.IsEmpty() && !Tree.ContainsByPredicate([Id](const FBSGroup& G) { return G.Parent == Id; }))
            {
                Tree.RemoveAt(I);
                Changed = true;
            }
        }
    } while (Changed);
}

bool FBSGroupTree::Validate(const TArray<FBSGroup>& Tree)
{
    TMap<FGuid, int32> Indices;
    for (int32 I = 0; I < Tree.Num(); ++I)
    {
        if (!Tree[I].Id.IsValid() || Indices.Contains(Tree[I].Id)) return false;
        Indices.Add(Tree[I].Id, I);
    }
    // Each parent edge is checked once. No recursion or repeated linear ancestor
    // searches, even for thousands of nested containers.
    TArray<uint8> State;
    State.Init(0, Tree.Num());
    for (int32 I = 0; I < Tree.Num(); ++I)
    {
        TArray<int32> Path;
        int32 Current = I;
        while (Current != INDEX_NONE && State[Current] == 0)
        {
            State[Current] = 1;
            Path.Add(Current);
            const FGuid Parent = Tree[Current].Parent;
            const int32* ParentIndex = Indices.Find(Parent);
            if (Parent.IsValid() && !ParentIndex) return false;
            Current = ParentIndex ? *ParentIndex : INDEX_NONE;
        }
        if (Current != INDEX_NONE && State[Current] == 1) return false;
        for (int32 Index : Path) State[Index] = 2;
    }
    TArray<FBSBuildingRef> Buildings;
    for (const auto& G : Tree)
    {
        for (const auto& B : G.Buildings)
        {
            if (Buildings.Contains(B)) return false;
            Buildings.Add(B);
        }
    }
    return true;
}

bool FBSGroupTree::Apply(TArray<FBSGroup>& Tree, EBSGroupOperation Operation, const FBSSelection& Input,
    FBSSelection& Result, FString& Error)
{
    Result = {};
    if (!Validate(Tree)) { Error = TEXT("The group hierarchy is invalid; no changes made."); return false; }
    FBSSelection Selected = Normalize(Tree, Input);
    if (Selected.IsEmpty()) { Error = TEXT("Select buildings or groups first."); return false; }
    if (Operation == EBSGroupOperation::Ungroup)
    {
        if (Selected.Groups.Num() != 1 || !Selected.Buildings.IsEmpty())
        { Error = TEXT("Ungroup requires exactly one group and no other selected items."); return false; }
        const FBSGroup Removed = *Find(Tree, Selected.Groups[0]);
        Result.Buildings = Removed.Buildings;
        for (auto& G : Tree)
        {
            if (G.Parent == Removed.Id) { G.Parent = Removed.Parent; Result.Groups.Add(G.Id); }
            if (G.Id == Removed.Parent) for (const auto& B : Removed.Buildings) G.Buildings.AddUnique(B);
        }
        Tree.RemoveAll([&](const FBSGroup& G) { return G.Id == Removed.Id; });
        return true;
    }
    if (Operation != EBSGroupOperation::Group && Operation != EBSGroupOperation::Merge)
    { Error = TEXT("Unknown group operation."); return false; }
    if (Operation == EBSGroupOperation::Merge && Selected.Groups.IsEmpty())
    { Error = TEXT("Merge requires at least one existing group."); return false; }

    // Preserve a common immediate parent. Cross-branch selections become root groups.
    FGuid Parent;
    bool First = true;
    auto IncludeParent = [&](FGuid Candidate)
    { if (First) { Parent = Candidate; First = false; } else if (Parent != Candidate) Parent.Invalidate(); };
    for (FGuid Id : Selected.Groups) IncludeParent(Find(Tree, Id)->Parent);
    for (const auto& B : Selected.Buildings) IncludeParent(Owner(Tree, B));

    TArray<FBSGroup> Updated = Tree;
    // Selecting in Normal/Blueprint mode deliberately extracts individual buildings.
    for (auto& G : Updated)
        G.Buildings.RemoveAll([&](const FBSBuildingRef& B) { return Selected.Buildings.Contains(B); });

    FGuid Target;
    if (Operation == EBSGroupOperation::Group)
    {
        FBSGroup New;
        New.Id = FGuid::NewGuid();
        New.Parent = Parent;
        New.Name = FString::Printf(TEXT("Group %s"), *New.Id.ToString(EGuidFormats::Short));
        New.Buildings = Selected.Buildings;
        Target = New.Id;
        for (auto& G : Updated) if (Selected.Groups.Contains(G.Id)) G.Parent = Target;
        Updated.Add(MoveTemp(New));
    }
    else
    {
        // Keep the first selected group ID. Dissolve other selected outer containers,
        // but retain their child groups, exactly as one-level ungroup would.
        Target = Selected.Groups[0];
        auto* Destination = Updated.FindByPredicate([&](const FBSGroup& G) { return G.Id == Target; });
        Destination->Parent = Parent;
        for (const auto& B : Selected.Buildings) Destination->Buildings.AddUnique(B);
        for (FGuid Id : Selected.Groups)
        {
            if (Id == Target) continue;
            const auto* Source = Find(Updated, Id);
            for (const auto& B : Source->Buildings) Destination->Buildings.AddUnique(B);
            for (auto& G : Updated) if (G.Parent == Id) G.Parent = Target;
        }
        Updated.RemoveAll([&](const FBSGroup& G) { return G.Id != Target && Selected.Groups.Contains(G.Id); });
    }
    TArray<FBSBuildingRef> Members;
    Collect(Updated, Target, Members);
    FVector Pivot = FVector::ZeroVector;
    for (const auto& B : Members) Pivot += B.Location();
    for (auto& G : Updated) if (G.Id == Target) G.Pivot = Members.IsEmpty() ? Pivot : Pivot / Members.Num();
    PruneEmpty(Updated);
    if (!Validate(Updated)) { Error = TEXT("This operation would create an invalid hierarchy."); return false; }
    Tree = MoveTemp(Updated);
    Result.Groups.Add(Target);
    return true;
}

ABSGroupSubsystem::ABSGroupSubsystem()
{
    ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer_Replicate;
    bAlwaysRelevant = true;
    SetReplicates(true);
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
}

ABSGroupSubsystem* ABSGroupSubsystem::Get(UWorld* World)
{
    auto* Manager = World ? World->GetSubsystem<USubsystemActorManager>() : nullptr;
    return Manager ? Manager->GetSubsystemActor<ABSGroupSubsystem>() : nullptr;
}

int32 ABSGroupSubsystem::FindLightweightIndex(const FBSBuildingRef& Ref) const
{
    auto* LW = AFGLightweightBuildableSubsystem::Get(GetWorld());
    const auto* Instances = LW ? LW->GetAllLightweightBuildableInstances().Find(Ref.LightweightClass) : nullptr;
    if (!Instances || !Ref.Recipe) return INDEX_NONE;
    if (LightweightCounts.FindRef(Ref.LightweightClass) != Instances->Num()) LightweightLookup.Remove(Ref.LightweightClass);
    auto Cell = [](const FVector& P) { return FIntVector(FMath::FloorToInt(P.X / 100), FMath::FloorToInt(P.Y / 100), FMath::FloorToInt(P.Z / 100)); };
    auto* Lookup = LightweightLookup.Find(Ref.LightweightClass);
    if (!Lookup)
    {
        Lookup = &LightweightLookup.Add(Ref.LightweightClass);
        LightweightCounts.Add(Ref.LightweightClass, Instances->Num());
        for (int32 I = 0; I < Instances->Num(); ++I)
            if ((*Instances)[I].IsValid()) Lookup->Add(Cell((*Instances)[I].Transform.GetLocation()), I);
    }
    int32 Match = INDEX_NONE;
    const FIntVector Center = Cell(Ref.Transform.GetLocation());
    for (int32 X = -1; X <= 1; ++X) for (int32 Y = -1; Y <= 1; ++Y) for (int32 Z = -1; Z <= 1; ++Z)
    {
        const FIntVector SearchCell = Center + FIntVector(X, Y, Z);
        for (auto It = Lookup->CreateConstKeyIterator(SearchCell); It; ++It)
        {
            const int32 I = It.Value();
            if (!Instances->IsValidIndex(I)) continue;
            const auto& Data = (*Instances)[I];
            if (Data.IsValid() && Data.BuiltWithRecipe == Ref.Recipe && Data.Transform.Equals(Ref.Transform, 0.01))
            { if (Match != INDEX_NONE) return INDEX_NONE; Match = I; }
        }
    }
    return Match;
}

void ABSGroupSubsystem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ABSGroupSubsystem, PublishedGroupRevision);

}

void ABSGroupSubsystem::GatherDependencies_Implementation(TArray<UObject*>& Out)
{
    if (auto* LW = AFGLightweightBuildableSubsystem::Get(GetWorld())) Out.AddUnique(LW);
    for (const auto& G : Groups) for (const auto& B : G.Buildings) if (IsValid(B.Actor)) Out.AddUnique(B.Actor);
    for (const auto& Plan : RailPlans)
    {
        if (IsValid(Plan.Owner)) Out.AddUnique(Plan.Owner);
    }
}

void ABSGroupSubsystem::PreSaveGame_Implementation(int32, int32)
{
    if (!HasAuthority() || SchemaVersion != 1) return;
    for (auto& Plan:RailPlans) FBSPlanOwnership::CaptureLegacy(Plan);
    UE_LOG(LogBuildingSkills,Display,TEXT("Saving rail plans: %d."),RailPlans.Num());
    bool Changed = false;
    const int32 PreviousCount = Groups.Num();
    for (auto& G : Groups)
        Changed |= G.Buildings.RemoveAll([&](const FBSBuildingRef& B) { return !B.IsAlive(GetWorld()); }) > 0;
    FBSGroupTree::PruneEmpty(Groups);
    if (Changed || Groups.Num() != PreviousCount) { PublishGroups(); }
}

void ABSGroupSubsystem::PostLoadGame_Implementation(int32, int32)
{
    for (auto& Plan:RailPlans) FBSPlanOwnership::CaptureLegacy(Plan);
    UE_LOG(LogBuildingSkills,Display,TEXT("Loaded rail plans: %d."),RailPlans.Num());
    LightweightLookup.Empty(); LightweightCounts.Empty();
    // Do not resolve buildings here: vanilla actors and lightweight records may still be loading.
    for (auto& G : Groups) for (auto& B : G.Buildings) B.RuntimeIndex = INDEX_NONE;
}

void ABSGroupSubsystem::RemoveLightweight(TSubclassOf<AFGBuildable> Class, int32 Index)
{
    TransformSnapshot.RemoveAll([&](const FBSNativeTransform& C) { return C.Building.LightweightClass == Class && C.InstanceIndex == Index; });
    FBSBuildingRef RemovedKey; RemovedKey.LightweightClass = Class; RemovedKey.RuntimeIndex = Index;
    BuildingVersions.Add(FBSTransformNative::Key(RemovedKey), ++TransformSerial);
    SnapshotIndices.Reset();
    for (int32 I = 0; I < TransformSnapshot.Num(); ++I) SnapshotIndices.Add(FBSTransformNative::Key(TransformSnapshot[I].Building, TransformSnapshot[I].InstanceIndex), I);
    PendingTransforms.RemoveAll([&](const FBSNativeTransform& C) { return C.Building.LightweightClass == Class && C.InstanceIndex == Index; });
    if (!HasAuthority()) return;
    const FBSBuildingRef Removed = FBSBuildingRef::FromLightweight(GetWorld(), Class, Index);
    if (!Removed.LightweightClass) return;
    bool Changed = false;
    for (auto& G : Groups) Changed |= G.Buildings.Remove(Removed) > 0;
    if (Changed) { FBSGroupTree::PruneEmpty(Groups); PublishGroups(); }
}

void ABSGroupSubsystem::RemoveBuilding(AFGBuildable* Building)
{
    TransformSnapshot.RemoveAll([&](const FBSNativeTransform& C) { return C.Building.Actor == Building; });
    BuildingVersions.Add(FBSTransformNative::Key(FBSBuildingRef::FromActor(Building)), ++TransformSerial);
    SnapshotIndices.Reset();
    for (int32 I = 0; I < TransformSnapshot.Num(); ++I) SnapshotIndices.Add(FBSTransformNative::Key(TransformSnapshot[I].Building, TransformSnapshot[I].InstanceIndex), I);
    PendingTransforms.RemoveAll([&](const FBSNativeTransform& C) { return C.Building.Actor == Building; });
    if (!HasAuthority() || !IsValid(Building) || Building->GetIsLightweightTemporary()) return;
    bool Changed = false;
    for (auto& G : Groups)
        Changed |= G.Buildings.RemoveAll([&](const FBSBuildingRef& B) { return B.Actor == Building; }) > 0;
    if (Changed) { FBSGroupTree::PruneEmpty(Groups); PublishGroups(); }
}

bool ABSGroupSubsystem::Execute(AFGCharacterPlayer* Player, EBSGroupOperation Operation,
    const FBSSelection& Selection, int32 ExpectedRevision, FBSSelection& Result, FString& Error)
{
    if (!HasAuthority() || SchemaVersion != 1 || !IsValid(Player) || !Player->IsBuildGunEquipped() ||
        !Player->GetBuildGun() || !UBSEditModeComponent::IsEditDescriptor(Player->GetBuildGun()->GetCurrentBuildGunMode()) ||
        Player->IsDrivingVehicle() || !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead())
    { Error = TEXT("Open Edit mode to change groups."); return false; }
    // Explicit buildings are validated below against current server identities.
    // Only selected containers depend on the client's snapshot of the hierarchy.
    if (!Selection.Groups.IsEmpty() && Revision != ExpectedRevision)
    { Error = TEXT("Groups changed. Review the selection and try again."); ForceNetUpdate(); return false; }
    constexpr int32 MaxSelection = 2048;
    if (Selection.Groups.Num() + Selection.Buildings.Num() > MaxSelection)
    { Error = TEXT("The selection exceeds the 2048-item limit."); return false; }
    for (FGuid Id : Selection.Groups)
        if (!FBSGroupTree::Find(Groups, Id)) { Error = TEXT("A selected group no longer exists."); return false; }
    TArray<FBSBuildingRef> Buildings = Selection.Buildings;
    for (FGuid Id : Selection.Groups) FBSGroupTree::Collect(Groups, Id, Buildings);
    if (Buildings.Num() > MaxSelection) { Error = TEXT("The selection exceeds the 2048-building limit."); return false; }
    bool InReach = false;
    for (const auto& B : Buildings)
    {
        if (!B.IsAlive(GetWorld())) { Error = TEXT("A building is missing or its lightweight identity is ambiguous. Reselect surviving items."); return false; }
        InReach |= FVector::DistSquared(B.Location(), Player->GetActorLocation()) <= FMath::Square(Player->GetBuildGun()->GetBuildGunRange() + 1000.0f);
    }
    if (!InReach) { Error = TEXT("Move within Build Gun range of the selection."); return false; }
    if (!FBSGroupTree::Apply(Groups, Operation, Selection, Result, Error)) return false;
    PublishGroups();
    return true;
}

void UBSGroupRemoteCallObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UBSGroupRemoteCallObject, ReplicationAnchor);
}

void UBSGroupRemoteCallObject::Server_Apply_Implementation(EBSGroupOperation Operation, const FBSSelection& Selection, int32 ExpectedRevision)
{
    auto* System = ABSGroupSubsystem::Get(GetWorld());
    FBSSelection Result;
    FString Message;
    bool Success = System && System->Execute(GetOwnerPlayerCharacter(), Operation, Selection, ExpectedRevision, Result, Message);
    if (!System) Message = TEXT("Grouping is not ready yet.");
    UE_LOG(LogBuildingSkills, Display, TEXT("Group operation: op=%d buildings=%d groups=%d expected=%d current=%d success=%d message=%s"),
        int32(Operation), Selection.Buildings.Num(), Selection.Groups.Num(), ExpectedRevision, System ? System->Revision : -1, Success, *Message);
    SendGroupResult(Success, Result, Message, System ? System->Revision : 0);
}

void UBSGroupRemoteCallObject::Client_Result_Implementation(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision)
{
    auto* Player = GetOwnerPlayerCharacter();
    if (auto* Edit = Player ? Player->FindComponentByClass<UBSEditModeComponent>() : nullptr)
        Edit->OnOperationResult(Success, Selection, Message, Revision);
}

