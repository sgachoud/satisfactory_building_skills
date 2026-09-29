#include "BSGroups.h"
#include "BSEditModeComponent.h"
#include "Buildables/FGBuildable.h"
#include "Components/SplineComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/InstancedStaticMesh.h"
#include "Equipment/FGBuildGun.h"
#include "FGCharacterPlayer.h"
#include "FGHealthComponent.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGCircuitConnectionComponent.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGBuildableSubsystem.h"

// These octree template callbacks are declared in the SDK without DLL exports.
// Supply them for this module's inline RemoveElement/AddElement instantiations;
// the native tree and its actor-to-element map remain owned by FactoryGame.
FBoxCenterAndExtent FBuildableOctreeSemantics::GetBoundingBox(const AFGBuildable* Actor)
{
    FVector Origin, Extent;
    Actor->GetActorBounds(false, Origin, Extent);
    return FBoxCenterAndExtent(Origin, Extent);
}
void FBuildableOctreeSemantics::SetElementId(FOctree& Tree, const AFGBuildable* Actor, FOctreeElementId2 Id)
{
    static_cast<FBuildableComponentsOctree&>(Tree).mElementIdMap.Add(Actor, Id);
}

namespace
{
    using FInstanceTargets = TArray<TPair<FInstanceOwnerHandlePtr, FTransform>>;
    FInstanceTargets InstanceTargets(const TArray<FInstanceOwnerHandlePtr>& Handles, const FTransform& Before, const FTransform& After)
    {
        FInstanceTargets Targets;
        for (const auto& Handle : Handles)
            if (Handle && Handle->IsInstanced())
                if (auto* Mesh = Handle->GetInstanceComponent())
                    Targets.Emplace(Handle, (Handle->GetWorldTransform().GetRelativeTransform(Before) * After).GetRelativeTransform(Mesh->GetComponentTransform()));
        return Targets;
    }
    void MoveInstances(const FInstanceTargets& Targets)
    { for (const auto& Target : Targets) Target.Key->SetLocalTransform(Target.Value); }
    bool MoveActor(AFGBuildable* Actor, const FTransform& Target)
    {
        auto* Root = Actor->GetRootComponent();
        if (!Root) return false;
        // A placed building is normally static. Permit this one explicit teleport
        // while retaining its usual mobility after the component update finishes.
        TGuardValue<TEnumAsByte<EComponentMobility::Type>> Mobility(Root->Mobility, EComponentMobility::Movable);
        Actor->SetActorTransform(Target, false, nullptr, ETeleportType::TeleportPhysics);
        return Actor->GetActorTransform().Equals(Target, 0.01);
    }
    bool SameNativeBuilding(const FBSNativeTransform& A, const FBSNativeTransform& B)
    {
        return A.Building.LightweightClass ? A.Building.LightweightClass == B.Building.LightweightClass && A.InstanceIndex == B.InstanceIndex : A.Building.Actor == B.Building.Actor;
    }
}

bool FBSTransformNative::Supports(AFGBuildable* Actor, FString& Error)
{
    if (!IsValid(Actor)) return false;
    return true;
}

bool FBSTransformNative::Apply(UWorld* World, const FBSNativeTransform& Change)
{
    const auto& Ref = Change.Building;
    if (Ref.LightweightClass)
    {
        auto* LW = AFGLightweightBuildableSubsystem::Get(World);
        auto* Data = LW ? LW->GetRuntimeDataForBuildableClassAndIndex(Ref.LightweightClass, Change.InstanceIndex) : nullptr;
        if (!Data || !Data->IsValid() || Data->BuiltWithRecipe != Ref.Recipe) return false;
        if (Data->Transform.Equals(Change.Target, 0.0001)) return true;
        // A delayed update must never adopt a reused native slot.
        if (!Data->Transform.Equals(Ref.Transform, 0.01)) return false;
        const FTransform Before = Data->Transform;
        MoveInstances(InstanceTargets(Data->Handles, Before, Change.Target));
        // Re-index the existing native instance; keep its recipe, customization,
        // built-by data, blueprint membership and temporary actor identity intact.
        for (const auto& Id : Data->GridElementIds)
            if (auto* Cell = LW->mBuildableSpatialGrid.Find(Id.Key); Cell && Cell->IsValidIndex(Id.Value)) Cell->RemoveAt(Id.Value);
        Data->GridElementIds.Reset();
        Data->Transform = Change.Target;
        LW->RegisterBuildableInSpatialGrid(Ref.LightweightClass, Change.InstanceIndex);
        if (auto* Temporary = LW->FindTemporaryByBuildableClassAndIndex(Ref.LightweightClass, Change.InstanceIndex))
            MoveActor(Temporary, Change.Target);
        if (auto* Groups = ABSGroupSubsystem::Get(World)) Groups->InvalidateLightweightLookup(Ref.LightweightClass);
        return true;
    }
    auto* Actor = Ref.Actor.Get();
    if (!IsValid(Actor) || Actor->GetIsDismantled()) return false;
    const FTransform Before = Actor->GetActorTransform();
    if (Before.Equals(Change.Target, 0.0001) && Change.Spline.IsEmpty() && !Change.HasLift && !Change.HasWire) return true;
    // Capture/update instanced meshes before moving components. They live in
    // AbstractInstance's shared components, not in the actor's scene hierarchy.
    auto Targets = InstanceTargets(Actor->mInstanceHandles, Before, Change.Target);
    Targets.Append(InstanceTargets(Actor->mDynamicInstanceHandles, Before, Change.Target));
    if (!MoveActor(Actor, Change.Target)) return false;
    MoveInstances(Targets);
    ApplyGeometry(Actor, Change, Before);
    if (auto* Buildables = AFGBuildableSubsystem::Get(World))
    {
        auto& Tree = Buildables->mBuildableComponentsOctree;
        if (auto* Id = Tree.mElementIdMap.Find(Actor))
        {
            const auto OldId = *Id;
            Tree.mElementIdMap.Remove(Actor);
            if (Tree.IsValidElementId(OldId)) Tree.RemoveElement(OldId);
            Tree.AddElement(Actor);
        }
    }
    Actor->ForceNetUpdate();
    return Actor->GetActorTransform().Equals(Change.Target, 0.01);
}

bool ABSGroupSubsystem::TransformSelection(AFGCharacterPlayer* Player, const FBSSelection& Selected, int32 ExpectedRevision,
    EBSTransformMode Mode, double Amount, FVector Translation, FBSSelection& Result, FString& Error)
{
    if (!HasAuthority() || SchemaVersion != 1 || !IsValid(Player) || !Player->IsBuildGunEquipped() ||
        !Player->GetBuildGun() || !UBSEditModeComponent::IsEditDescriptor(Player->GetBuildGun()->GetCurrentBuildGunMode()) ||
        Player->IsDrivingVehicle() || Player->IsInPortal() || !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead())
    { Error = TEXT("Open Edit to transform buildings."); return false; }
    if (ExpectedRevision != Revision) { Error = TEXT("Selection changed. Try the transform again."); return false; }
    if (Selected.Buildings.Num() + Selected.Groups.Num() > 2048 || uint8(Mode) > 9 || !FMath::IsFinite(Amount) ||
        Translation.ContainsNaN() || Translation.Size() > 10000 || FMath::Abs(Amount) > 360 ||
        (Translation.IsNearlyZero() && uint8(Mode) >= 4 && (Amount < 0.001 || Amount > 100)))
    { Error = TEXT("Transform exceeds the supported step limits."); return false; }
    TArray<FBSBuildingRef> Members;
    for (const auto& Ref : Selected.Buildings) Members.AddUnique(Ref);
    for (FGuid Id : Selected.Groups)
    {
        if (!FBSGroupTree::Find(Groups, Id)) { Error = TEXT("A selected group no longer exists."); return false; }
        FBSGroupTree::Collect(Groups, Id, Members);
    }
    if (Members.IsEmpty() || Members.Num() > 2048) { Error = TEXT("Select between 1 and 2048 buildings."); return false; }
    FVector Pivot = FVector::ZeroVector;
    bool InReach = false;
    for (const auto& Ref : Members)
    {
        if (!Ref.IsAlive(GetWorld())) { Error = TEXT("A selected building is missing or ambiguous. Reselect it."); return false; }
        auto* Actor = Ref.LightweightClass ? Ref.LightweightClass->GetDefaultObject<AFGBuildable>() : Ref.Actor.Get();
        if (!FBSTransformNative::Supports(Actor, Error)) return false;
        Pivot += Ref.Location();
        InReach |= FVector::DistSquared(Ref.Location(), Player->GetActorLocation()) <= FMath::Square(Player->GetBuildGun()->GetBuildGunRange() + 1000.0f);
    }
    if (!InReach) { Error = TEXT("Move within Build Gun range of the selection."); return false; }
    Pivot /= Members.Num();
    TArray<FBSNativeTransform> Changes;
    for (const auto& Ref : Members)
    {
        FBSNativeTransform Change;
        Change.Building = Ref;
        Change.InstanceIndex = Ref.LightweightClass ? Ref.ResolveIndex(GetWorld()) : INDEX_NONE;
        const FTransform Before = Ref.LightweightClass ? Ref.Transform : Ref.Actor->GetActorTransform();
        Change.Building.Transform = Before;
        Change.Target = FBSTransformMath::Apply(Before, Pivot, Mode, Amount, Translation);
        const FVector Scale = Change.Target.GetScale3D();
        if (!Change.Target.IsValid() || Scale.GetMin() < 0.01 || Scale.GetMax() > 100 || Change.Target.GetLocation().GetAbsMax() > 2000000)
        { Error = TEXT("Transform would exceed world or scale limits (0.01 to 100). No buildings changed."); return false; }
        Changes.Add(Change);
    }
    if (!FBSTransformNative::PlanNetworks(Changes, Error)) return false;
    TArray<FBSNativeTransform> Undo;
    for (const auto& Change : Changes)
    {
        auto Restore = Change.Building.Actor ? FBSTransformNative::Capture(Change.Building.Actor) : Change;
        Restore.Target = Change.Building.Transform;
        Restore.Building.Transform = Change.Target;
        Undo.Add(Restore);
    }
    FBSTransformNative::PrepareNetworks(GetWorld(), Changes);
    // Every member was validated. If a native setter unexpectedly fails, restore
    // the entire batch before publishing new group identities.
    int32 Applied = 0;
    for (const auto& Change : Changes)
    {
        if (!FBSTransformNative::Apply(GetWorld(), Change))
        {
            for (int32 I = 0; I <= Applied; ++I)
            {
                FBSTransformNative::Apply(GetWorld(), Undo[I]);
            }
            FBSTransformNative::RefreshNetworks(GetWorld(), Undo);
            Error = TEXT("A native building could not be moved; the transform was rolled back."); return false;
        }
        ++Applied;
    }
    FBSTransformNative::RefreshNetworks(GetWorld(), Changes);
    Result = Selected;
    for (auto& G : Groups)
        for (auto& Ref : G.Buildings)
            for (const auto& Change : Changes)
                if (Ref == Change.Building) { Ref.Transform = Change.Target; Ref.RuntimeIndex = Change.InstanceIndex; break; }
    for (auto& Ref : Result.Buildings)
        for (const auto& Change : Changes)
            if (Ref == Change.Building) { Ref.Transform = Change.Target; Ref.RuntimeIndex = Change.InstanceIndex; break; }
    for (auto& G : Groups)
    {
        TArray<FBSBuildingRef> Descendants;
        FBSGroupTree::Collect(Groups, G.Id, Descendants);
        G.Pivot = FVector::ZeroVector;
        for (const auto& Ref : Descendants) G.Pivot += Ref.Location();
        if (!Descendants.IsEmpty()) G.Pivot /= Descendants.Num();
    }
    for (const auto& Change : Changes)
    {
        auto* Existing = TransformSnapshot.FindByPredicate([&](const FBSNativeTransform& Item) { return SameNativeBuilding(Item, Change); });
        if (Existing) *Existing = Change; else TransformSnapshot.Add(Change);
    }
    // Keep reliable RPC packets bounded for selections of thousands of members.
    for (int32 Start = 0; Start < Changes.Num();)
    {
        int32 End=Start, Bytes=0;
        while(End<Changes.Num() && End-Start<48)
        {
            const int32 Size=512+Changes[End].Spline.Num()*80+Changes[End].WirePoints.Num()*32;
            if(End>Start && Bytes+Size>24000) break;
            Bytes+=Size; ++End;
        }
        TArray<FBSNativeTransform> Batch;
        Batch.Append(Changes.GetData() + Start, End-Start);
        Multicast_Transforms(Batch);
        Start=End;
    }
    ++Revision;
    ForceNetUpdate();
    return true;
}

void ABSGroupSubsystem::Multicast_Transforms_Implementation(const TArray<FBSNativeTransform>& Changes)
{
    if (HasAuthority()) return;
    for (const auto& Change : Changes)
    {
        PendingTransforms.RemoveAll([&](const FBSNativeTransform& Item) { return SameNativeBuilding(Item, Change); });
        if (!FBSTransformNative::Apply(GetWorld(), Change)) PendingTransforms.Add(Change);
    }
    FBSTransformNative::RefreshNetworks(GetWorld(), Changes);
    SetActorTickEnabled(!PendingTransforms.IsEmpty());
}
void ABSGroupSubsystem::OnRep_TransformSnapshot() { Multicast_Transforms_Implementation(TransformSnapshot); }
void ABSGroupSubsystem::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    PendingTransforms.RemoveAll([&](const FBSNativeTransform& Change) { return FBSTransformNative::Apply(GetWorld(), Change); });
    if (PendingTransforms.IsEmpty()) SetActorTickEnabled(false);
}
void UBSGroupRemoteCallObject::Server_Transform_Implementation(const FBSSelection& Selection, int32 ExpectedRevision, EBSTransformMode Mode, double Amount, FVector Translation)
{
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    FBSSelection Result;
    FString Error;
    const bool Success = Groups && Groups->TransformSelection(GetOwnerPlayerCharacter(), Selection, ExpectedRevision, Mode, Amount, Translation, Result, Error);
    Client_TransformResult(Success, Result, Error, Groups ? Groups->Revision : 0);
}
void UBSGroupRemoteCallObject::Client_TransformResult_Implementation(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision)
{
    auto* Player = GetOwnerPlayerCharacter();
    if (auto* Edit = Player ? Player->FindComponentByClass<UBSEditModeComponent>() : nullptr)
        Edit->OnTransformResult(Success, Selection, Message, Revision);
}
