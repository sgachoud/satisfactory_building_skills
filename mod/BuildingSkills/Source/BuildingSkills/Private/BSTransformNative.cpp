#include "FGBlueprintProxy.h"
#include "BSGroups.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildable.h"
#include "Components/SplineComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/InstancedStaticMesh.h"
#include "Equipment/FGBuildGun.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGHealthComponent.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGCircuitConnectionComponent.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGBuildableSubsystem.h"
#include "FGBackgroundThread.h"
#include "FGProximitySubsystem.h"
#include "FGOutlineComponent.h"
#include "GameFramework/PlayerController.h"

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
        TArray<TPair<USceneComponent*, FTransform>> Detached;
        const FTransform Before = Actor->GetActorTransform();
        TInlineComponentArray<USceneComponent*> Components(Actor);
        for (auto* Component : Components)
            if (Component != Root && !Component->GetAttachParent()) Detached.Emplace(Component, Component->GetComponentTransform().GetRelativeTransform(Before) * Target);
        // Outline proxies belong to local players, not to the building's scene
        // hierarchy. Capture their destination before moving attached components.
        for (auto It=Actor->GetWorld()->GetPlayerControllerIterator(); It; ++It)
            if (auto* PC=It->Get(); PC && PC->IsLocalController())
                if (auto* Player=Cast<AFGCharacterPlayer>(PC->GetPawn()))
                    for (auto* Outline:TInlineComponentArray<UFGOutlineComponent*>(Player))
                        if (const auto* State=Outline->GetImmutableOutlineStateForActor(Actor))
                        {
                            for (const auto& Pair:State->OutlineProxies) if (IsValid(Pair.Value))
                                Detached.Emplace(Pair.Value,Pair.Value->GetComponentTransform().GetRelativeTransform(Before)*Target);
                            for (const auto& Pair:State->InstancedOutlineProxies) if (IsValid(Pair.Value))
                                Detached.Emplace(Pair.Value,Pair.Value->GetComponentTransform().GetRelativeTransform(Before)*Target);
                        }
        Actor->SetActorTransform(Target, false, nullptr, ETeleportType::TeleportPhysics);
        for (const auto& Entry : Detached)
        {
            TGuardValue<TEnumAsByte<EComponentMobility::Type>> ComponentMobility(Entry.Key->Mobility, EComponentMobility::Movable);
            Entry.Key->SetWorldTransform(Entry.Value, false, nullptr, ETeleportType::TeleportPhysics);
        }
        return Actor->GetActorTransform().Equals(Target, 0.01);
    }
    bool SameNativeBuilding(const FBSNativeTransform& A, const FBSNativeTransform& B)
    {
        return A.Building.LightweightClass ? A.Building.LightweightClass == B.Building.LightweightClass && A.InstanceIndex == B.InstanceIndex : A.Building.Actor == B.Building.Actor;
    }
}

void FBSTransformNative::QueryLightweightBounds(UWorld* World, const FBox& Box, TArray<FBSBuildingRef>& Out, int32& Examined)
{
    auto* LW = AFGLightweightBuildableSubsystem::Get(World);
    if (!LW || !Box.IsValid) return;
    TArray<uint32> Chunks;
    LW->FindIntersectingBuildableGridChunks(Box, Chunks);
    TSet<uint64> Seen;
    for (uint32 Chunk : Chunks)
        if (const auto* Cell = LW->mBuildableSpatialGrid.Find(Chunk))
            for (const auto& Id : *Cell)
            {
                ++Examined;
                // Native grid entries already contain world bounds. Reject the
                // bulk of a dense cell before runtime-data lookup/transform work.
                if (Id.BoundingBox.IsValid && !Id.BoundingBox.Intersect(Box)) continue;
                FBSBuildingRef Ref; Ref.LightweightClass = Id.BuildableClass; Ref.RuntimeIndex = Id.RuntimeIndex;
                const uint64 Identity = Key(Ref);
                if (Seen.Contains(Identity)) continue;
                Seen.Add(Identity);
                const auto* Data = LW->GetRuntimeDataForBuildableClassAndIndex(Ref.LightweightClass, Ref.RuntimeIndex);
                if (!Data || !Data->IsValid() || !Data->BoundingBox.TransformBy(Data->Transform).Intersect(Box)) continue;
                Ref.Recipe = Data->BuiltWithRecipe; Ref.Transform = Data->Transform;
                Out.Add(Ref);
            }
}

bool FBSTransformNative::Supports(AFGBuildable* Actor, FString& Error)
{
    if (!IsValid(Actor)) { Error = TEXT("The selected building is unavailable."); return false; }
    // Lightweight validation uses its class default object; live actors require a root.
    if (!Actor->HasAnyFlags(RF_ClassDefaultObject) && (!Actor->GetRootComponent() || Actor->GetIsDismantled()))
    { Error = TEXT("The selected building has no movable root or was dismantled."); return false; }
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
        if (Data->Transform.Equals(Change.Target, 0.0001))
        {
            if (auto* Temporary = LW->FindTemporaryByBuildableClassAndIndex(Ref.LightweightClass, Change.InstanceIndex))
                MoveActor(Temporary, Change.Target);
            return true;
        }
        // A delayed update must never adopt a reused native slot.
        if (!Data->Transform.Equals(Ref.Transform, 0.01) && !(Change.HasBaseline && Data->Transform.Equals(Change.Baseline, .01))) return false;
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
    // Native BeginPlay registers factory proximity by location; EndPlay removes
    // it at the current location. A move must transfer that registration too,
    // otherwise later dismantling can assert while removing a nonexistent entry.
    if (Actor->HasActorBegunPlay() && !Actor->ShouldConvertToLightweight() &&
        Before.GetLocation() != Actor->GetActorLocation() && Actor->ShouldBeConsideredForBase())
        if (auto* Proximity = AFGProximitySubsystem::GetProximitySubsystem(World))
        {
            const float Weight = Actor->GetConsideredForBaseWeight();
            Proximity->RemoveFactoryBuildingToProximitySystem(Before.GetLocation(), Weight);
            Proximity->RegisterFactoryBuildingToProximitySystem(Actor->GetActorLocation(), Weight);
        }
    MoveInstances(Targets);
    // Decorator fog planes (and other pooled effects) cache world transforms in
    // the game's background pool; they are neither actor components nor Abstract
    // Instance handles. Retire the old entries through the native queues and let
    // the pool recreate them from the actor's new pose and current effect state.
    // An empty array also covers registration still pending: do not enqueue a
    // second registration while that request will already read the latest pose.
    if (World->GetNetMode() != NM_DedicatedServer && !Before.Equals(Change.Target, .0001) && !Actor->mPoolHandles.IsEmpty())
    {
        auto PreviousHandles = MoveTemp(Actor->mPoolHandles);
        FFGBackgroundThread::RemoveHandles(MoveTemp(PreviousHandles), Before.GetLocation(), World);
        FFGBackgroundThread::RegisterActor(Actor);
    }
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
    EBSTransformMode Mode, double Amount, FVector Translation, FBSSelection& Result, FString& Error,
    const FBSTransformOptions* Options, const TArray<FBSBuildingRef>* ExpectedMembers)
{
    if (!HasAuthority() || SchemaVersion != 1 || !IsValid(Player) || !Player->IsBuildGunEquipped() ||
        !Player->GetBuildGun() || !UBSEditModeComponent::IsEditDescriptor(Player->GetBuildGun()->GetCurrentBuildGunMode()) ||
        Player->IsDrivingVehicle() || Player->IsInPortal() || !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead())
    { Error = TEXT("Open Edit to transform buildings."); return false; }
    if (!ExpectedMembers && ExpectedRevision != Revision) { Error = TEXT("Selection changed. Try the transform again."); return false; }
    if (Selected.Buildings.Num() + Selected.Groups.Num() > 2048 || uint8(Mode) > 9 || !FMath::IsFinite(Amount) ||
        Translation.ContainsNaN() || Translation.Size() > 10000 || FMath::Abs(Amount) > 360 ||
        (Translation.IsNearlyZero() && uint8(Mode) >= 3 && (Amount < 0.001 || Amount > 100)))
    { Error = TEXT("Transform exceeds the supported step limits."); return false; }
    TArray<FBSBuildingRef> Members;
    for (const auto& Ref : Selected.Buildings) Members.AddUnique(Ref);
    for (FGuid Id : Selected.Groups)
    {
        if (!FBSGroupTree::Find(Groups, Id)) { Error = TEXT("A selected group no longer exists."); return false; }
        FBSGroupTree::Collect(Groups, Id, Members);
    }
    if (Members.IsEmpty() || Members.Num() > 2048) { Error = TEXT("Select between 1 and 2048 buildings."); return false; }
    if (ExpectedMembers)
    {
        if (ExpectedMembers->Num() != Members.Num()) { Error = TEXT("The selected group membership changed. Reselect it."); return false; }
        TMap<uint64, const FBSBuildingRef*> CurrentMembers;
        for (const auto& Member : Members)
        {
            const int32 Index = Member.LightweightClass ? Member.ResolveIndex(GetWorld()) : INDEX_NONE;
            if (Member.LightweightClass && Index == INDEX_NONE) { Error = TEXT("A selected building changed. Reselect it."); return false; }
            CurrentMembers.Add(FBSTransformNative::Key(Member, Index), &Member);
        }
        TSet<uint64> Seen;
        for (const auto& Expected : *ExpectedMembers)
        {
            const uint64 Key = FBSTransformNative::Key(Expected);
            const auto* Found = CurrentMembers.Find(Key);
            const auto* Member = Found ? *Found : nullptr;
            const FTransform Current = Member && Member->Actor ? Member->Actor->GetActorTransform() : Member ? Member->Transform : FTransform::Identity;
            const uint64 Version = BuildingVersions.FindRef(Key);
            const bool SameTransform = Current.Equals(Expected.Transform, .01) ||
                (Member && Member->Actor && Version == 0 && FBSTransformMath::MatchesInitialReplication(Current, Expected.Transform));
            if (!Member || Seen.Contains(Key) || !(Expected == *Member) || Version != Expected.TransformVersion || !SameTransform)
            {
                UE_LOG(LogBuildingSkills, Display, TEXT("Transform conflict: actor=%s version=%llu expected=%llu member=%d duplicate=%d locationDelta=%s scaleDelta=%s rotationDeltaDegrees=%.6f authority=%s observed=%s"),
                    *GetNameSafe(Expected.Actor), Version, Expected.TransformVersion,
                    Member != nullptr, Seen.Contains(Key),
                    *(Current.GetLocation() - Expected.Transform.GetLocation()).ToString(), *(Current.GetScale3D() - Expected.Transform.GetScale3D()).ToString(),
                    FMath::RadiansToDegrees(Current.GetRotation().AngularDistance(Expected.Transform.GetRotation())),
                    *Current.ToString(), *Expected.Transform.ToString());
                Error = TEXT("Another edit changed a selected building. Review the selection and retry."); return false;
            }
            Seen.Add(Key);
        }
        // Retain the client's stable order for the chosen anchor.
        Members = *ExpectedMembers;
    }
    if (Options && (uint8(Options->Pivot) > 3 || uint8(Options->Space) > 2 || Options->AnchorFrame.ContainsNaN() || !Options->AnchorFrame.IsNormalized() || !Members.IsValidIndex(Options->AnchorIndex) || Options->FixedPivot.ContainsNaN() || Options->FixedPivot.GetAbsMax() > 2000000))
    { Error = TEXT("Invalid transform pivot or axes."); return false; }
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
    if (Options && Options->Pivot == EBSTransformPivot::Bounds)
    {
        FBox Box(ForceInit);
        for (const auto& Ref : Members) Box += FBSTransformNative::Bounds(GetWorld(), Ref);
        if (Box.IsValid) Pivot = Box.GetCenter();
    }
    if (Options && Options->UseFixedPivot && (Options->Pivot == EBSTransformPivot::Bounds || Options->Pivot == EBSTransformPivot::Origins)) Pivot = Options->FixedPivot;
    if (Options && Options->Pivot == EBSTransformPivot::Anchor) Pivot = Members[Options->AnchorIndex].Location();
    const FTransform Anchor = Options ? (Members[Options->AnchorIndex].Actor ? Members[Options->AnchorIndex].Actor->GetActorTransform() : Members[Options->AnchorIndex].Transform) : FTransform::Identity;
    TArray<FBSNativeTransform> Changes;
    for (const auto& Ref : Members)
    {
        FBSNativeTransform Change;
        Change.Building = Ref;
        Change.InstanceIndex = Ref.LightweightClass ? Ref.ResolveIndex(GetWorld()) : INDEX_NONE;
        const FTransform Before = Ref.LightweightClass ? Ref.Transform : Ref.Actor->GetActorTransform();
        Change.Building.Transform = Before;
        const bool Individual = Options && Options->Pivot == EBSTransformPivot::Individual;
        const FQuat Frame = Options && Options->Space == EBSTransformSpace::Anchor ? Options->AnchorFrame : Options && Options->Space == EBSTransformSpace::Local ? (Individual ? Before.GetRotation() : Anchor.GetRotation()) : FQuat::Identity;
        Change.Target = Options ? FBSTransformMath::ApplyInFrame(Before, Individual ? Before.GetLocation() : Pivot, Frame, Mode, Amount, Translation)
            : FBSTransformMath::Apply(Before, Pivot, Mode, Amount, Translation);
        const FVector Scale = Change.Target.GetScale3D();
        if (!Change.Target.IsValid() || Scale.GetMin() < 0.01 || Scale.GetMax() > 100 || Change.Target.GetLocation().GetAbsMax() > 2000000)
        { Error = TEXT("Transform would exceed world or scale limits (0.01 to 100). No buildings changed."); return false; }
        Changes.Add(Change);
    }
    auto* PC=Cast<AFGPlayerController>(Player->GetController());
    auto* RCO=PC?PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>():nullptr;
    if(RCO && !FBSTransformers::ExpandTransforms(GetWorld(),RCO->Transformers,Changes,Error))return false;
    if (!FBSTransformNative::PlanNetworks(Changes, Error)) return false;
    TArray<FBSNativeTransform> Undo;
    for (const auto& Change : Changes)
    {
        auto Restore = Change.Building.Actor ? FBSTransformNative::Capture(Change.Building.Actor) : Change;
        Restore.Target = Change.Building.Transform;
        Restore.Building.Transform = Change.Target;
        Undo.Add(Restore);
    }
    if (!CommitTransforms(Changes, Undo, Error)) return false;
    Result = Selected;
    PublishTransforms(Changes, Result);
    RecordTransform(Player, Options, Undo, Changes, Result);
    return true;
}

bool ABSGroupSubsystem::CommitTransforms(const TArray<FBSNativeTransform>& Changes, const TArray<FBSNativeTransform>& Undo, FString& Error)
{
    FBSTransformNative::PrepareNetworks(GetWorld(), Changes);
    int32 Applied = 0;
    for (const auto& Change : Changes)
    {
        if (!FBSTransformNative::Apply(GetWorld(), Change))
        {
            for (int32 I = 0; I <= Applied; ++I) FBSTransformNative::Apply(GetWorld(), Undo[I]);
            FBSTransformNative::RefreshNetworks(GetWorld(), Undo);
            Error = TEXT("A building could not be moved; the operation was rolled back."); return false;
        }
        ++Applied;
    }
    FBSTransformNative::RefreshNetworks(GetWorld(), Changes);
    // A complete blueprint moves as one selectable unit. Keep its native saved
    // origin in step with its members, including undo/redo and lightweight pieces.
    TSet<AFGBlueprintProxy*> Proxies;
    for (const auto& C : Changes)
    { auto Ref=C.Building; Ref.Transform=C.Target; Ref.RuntimeIndex=C.InstanceIndex; if (auto* Proxy = Ref.Blueprint(GetWorld())) Proxies.Add(Proxy); }
    for (auto* Proxy : Proxies)
    {
        TArray<FBSBuildingRef> Members;
        for (auto* B : Proxy->GetBuildables()) if (IsValid(B)) Members.Add(FBSBuildingRef::FromActor(B));
        for (const auto& Bundle : Proxy->GetLightweightClassAndIndices())
            for (int32 I : Bundle.Indices) Members.Add(FBSBuildingRef::FromLightweight(GetWorld(), Bundle.BuildableClass, I));
        const FBSNativeTransform* First = nullptr; bool Complete = !Members.IsEmpty();
        for (const auto& M : Members)
        {
            const auto* C = Changes.FindByPredicate([&](const auto& Item) { return FBSTransformNative::Key(Item.Building, Item.InstanceIndex) == FBSTransformNative::Key(M); });
            if (!C) { Complete = false; break; }
            if (!First) First = C;
        }
        if (Complete && First)
            Multicast_BlueprintPose(Proxy, Proxy->GetActorTransform().GetRelativeTransform(First->Building.Transform) * First->Target);
    }
    return true;
}

bool FBSTransformNative::UpdateReference(FBSBuildingRef& Ref, const FBSNativeTransform& Change, bool CatchUp)
{
    if (Change.Serial && Ref.TransformVersion > Change.Serial) return false;
    bool Matches=Ref==Change.Building;
    if (Ref.LightweightClass)
    {
        if (Ref.LightweightClass!=Change.Building.LightweightClass || Ref.Recipe!=Change.Building.Recipe) return false;
        // The exact fingerprint can resolve a stale index hint. Otherwise require
        // the same runtime slot before accepting target/baseline catch-up.
        if (!Matches && Ref.RuntimeIndex!=INDEX_NONE && Ref.RuntimeIndex!=Change.InstanceIndex) return false;
        Matches |= Ref.Transform.Equals(Change.Target,.01) || (Change.HasBaseline && Ref.Transform.Equals(Change.Baseline,.01));
        // A completed topology snapshot can predate several already-applied moves.
        // Its versioned runtime identity remains valid within this world session.
        Matches |= CatchUp && Ref.RuntimeIndex==Change.InstanceIndex && Ref.TransformVersion>0 && Change.Serial>Ref.TransformVersion;
    }
    if (!Matches) return false;
    const bool Changed=!Ref.Transform.Equals(Change.Target,.0001) || Ref.TransformVersion!=Change.Serial;
    Ref.Transform=Change.Target; Ref.RuntimeIndex=Change.InstanceIndex;
    Ref.TransformVersion=Change.Serial;
    return Changed;
}
void FBSTransformNative::UpdateGroupTransforms(TArray<FBSGroup>& Tree, const TArray<FBSNativeTransform>& Changes, bool CatchUp)
{
    TMap<uint64,const FBSNativeTransform*> Lookup;
    for (const auto& Change:Changes) Lookup.Add(Key(Change.Building,Change.InstanceIndex),&Change);
    auto Update=[&](FBSBuildingRef& Ref) {
        if (const auto* Found=Lookup.Find(Key(Ref)); Found && UpdateReference(Ref,**Found,CatchUp)) return true;
        // Saved lightweight identities and not-yet-relevant actors lack a usable key.
        for (const auto& Change:Changes) if (UpdateReference(Ref,Change,CatchUp)) return true;
        return false;
    };
    TSet<FGuid> Dirty;
    TMap<FGuid, int32> GroupIndex;
    for (int32 I = 0; I < Tree.Num(); ++I) GroupIndex.Add(Tree[I].Id, I);
    for (auto& Group : Tree)
        for (auto& Ref : Group.Buildings) if (Update(Ref)) Dirty.Add(Group.Id);
    TArray<FGuid> Parents = Dirty.Array();
    for (int32 I = 0; I < Parents.Num(); ++I)
        if (const int32* Index = GroupIndex.Find(Parents[I]))
        {
            const FGuid Parent = Tree[*Index].Parent;
            if (Parent.IsValid() && !Dirty.Contains(Parent)) { Dirty.Add(Parent); Parents.Add(Parent); }
        }
    for (const auto& Id : Dirty)
        if (const int32* Index = GroupIndex.Find(Id))
        {
            auto& Group = Tree[*Index];
            TArray<FBSBuildingRef> Members;
            FBSGroupTree::Collect(Tree, Id, Members);
            Group.Pivot = FVector::ZeroVector;
            for (const auto& Ref : Members) Group.Pivot += Ref.Location();
            if (!Members.IsEmpty()) Group.Pivot /= Members.Num();
        }
}

void ABSGroupSubsystem::PublishTransforms(const TArray<FBSNativeTransform>& Changes, FBSSelection& Result)
{
    TArray<FBSNativeTransform> Published = Changes;
    ++TransformSerial;
    for (auto& C : Published)
    {
        C.Serial = TransformSerial;
        const uint64 Key = FBSTransformNative::Key(C.Building, C.InstanceIndex);
        const int32* Index = SnapshotIndices.Find(Key);
        C.Baseline = Index && TransformSnapshot.IsValidIndex(*Index) ? TransformSnapshot[*Index].Baseline : C.Building.Transform;
        C.HasBaseline = true;
    }
    for (const auto& C : Published)
    {
        const uint64 Key = FBSTransformNative::Key(C.Building, C.InstanceIndex);
        BuildingVersions.Add(Key, TransformSerial);
        if (const int32* Index = SnapshotIndices.Find(Key); Index && TransformSnapshot.IsValidIndex(*Index)) TransformSnapshot[*Index] = C;
        else SnapshotIndices.Add(Key, TransformSnapshot.Add(C));
    }
    FBSTransformNative::UpdateGroupTransforms(Groups,Published);
    for (auto& Ref:Result.Buildings)
        for (const auto& Change:Published) if (FBSTransformNative::UpdateReference(Ref,Change)) break;
    for (int32 Start = 0; Start < Changes.Num();)
    {
        int32 End = Start, Bytes = 0;
        while (End < Changes.Num() && End - Start < 48)
        {
            const int32 Size = 512 + Changes[End].Spline.Num() * 80 + Changes[End].WirePoints.Num() * 32;
            if (End > Start && Bytes + Size > 24000) break;
            Bytes += Size; ++End;
        }
        TArray<FBSNativeTransform> Batch;
        Batch.Append(Published.GetData() + Start, End - Start);
        Multicast_Transforms(Batch);
        Start = End;
    }
    // Membership did not change. The transform multicast updates group positions
    // directly; reserving topology revisions for membership avoids a full snapshot
    // roundtrip (and a stalled input queue) on every nudge.
}

namespace
{
    void FinishReceivedTransforms(ABSGroupSubsystem* Groups,const TArray<FBSNativeTransform>& Applied)
    {
        if (Applied.IsEmpty()) return;
        for (const auto& Change:Applied)
        {
            const uint64 Key=FBSTransformNative::Key(Change.Building,Change.InstanceIndex);
            if (const int32* Index=Groups->SnapshotIndices.Find(Key); Index && Groups->TransformSnapshot.IsValidIndex(*Index)) Groups->TransformSnapshot[*Index]=Change;
            else Groups->SnapshotIndices.Add(Key,Groups->TransformSnapshot.Add(Change));
        }
        FBSTransformNative::UpdateGroupTransforms(Groups->Groups,Applied);
        for (auto It=Groups->GetWorld()->GetPlayerControllerIterator(); It; ++It)
            if (auto* PC=It->Get(); PC && PC->IsLocalController())
                if (auto* Player=Cast<AFGCharacterPlayer>(PC->GetPawn()))
                    if (auto* Edit=Player->FindComponentByClass<UBSEditModeComponent>()) Edit->RefreshTransformReferences(Applied);
        FBSTransformNative::RefreshNetworks(Groups->GetWorld(),Applied);
    }
}

void ABSGroupSubsystem::Multicast_Transforms_Implementation(const TArray<FBSNativeTransform>& Changes)
{
    if (HasAuthority()) return;
    TArray<FBSNativeTransform> Applied;
    for (const auto& Change : Changes)
    {
        const uint64 Key = FBSTransformNative::Key(Change.Building, Change.InstanceIndex);
        if (Key && Change.Serial && Change.Serial < ReceivedVersions.FindRef(Key)) continue;
        if (Key && Change.Serial) ReceivedVersions.Add(Key, Change.Serial);
        PendingTransforms.RemoveAll([&](const FBSNativeTransform& Item) { return SameNativeBuilding(Item, Change); });
        if (!FBSTransformNative::Apply(GetWorld(), Change)) PendingTransforms.Add(Change);
        else Applied.Add(Change);
    }
    FinishReceivedTransforms(this,Applied);
    SetActorTickEnabled(!PendingTransforms.IsEmpty());
}
void ABSGroupSubsystem::OnRep_TransformSnapshot()
{
    const auto Snapshot = TransformSnapshot;
    Multicast_Transforms_Implementation(Snapshot);
}
void ABSGroupSubsystem::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TArray<FBSNativeTransform> Applied;
    PendingTransforms.RemoveAll([&](const FBSNativeTransform& Change)
    {
        if (!FBSTransformNative::Apply(GetWorld(), Change)) return false;
        Applied.Add(Change); return true;
    });
    FinishReceivedTransforms(this,Applied);
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

void UBSGroupRemoteCallObject::Server_TransformAdvanced_Implementation(const FBSSelection& Selection, const TArray<FBSBuildingRef>& ExpectedMembers,
    FBSTransformOptions Options, EBSTransformMode Mode, double Amount, FVector Translation)
{
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    FBSSelection Result; FString Error;
    const bool Success = Groups && Groups->TransformSelection(GetOwnerPlayerCharacter(), Selection, 0, Mode, Amount, Translation, Result, Error, &Options, &ExpectedMembers);
    SendTransformResult(Success, Result, Error, Groups ? Groups->Revision : 0);
}
void UBSGroupRemoteCallObject::Server_TransformHistory_Implementation(bool Redo)
{
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    FBSSelection Result; FString Error;
    const bool Success = Groups && Groups->RestoreTransform(GetOwnerPlayerCharacter(), Redo, Result, Error);
    SendTransformResult(Success, Result, Error, Groups ? Groups->Revision : 0);
}

void UBSGroupRemoteCallObject::Client_TransformSettled_Implementation(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision, const TArray<FBSBuildingRef>& Targets)
{
    auto* Player = GetOwnerPlayerCharacter();
    if (auto* Edit = Player ? Player->FindComponentByClass<UBSEditModeComponent>() : nullptr)
    {
        ReceivedSelection.Groups = Selection.Groups;
        Edit->AwaitedTransforms = MoveTemp(ReceivedMembers);
        Edit->AwaitedTransforms.Append(Targets);
        Edit->OnTransformResult(Success, ReceivedSelection, Message, Revision);
        ReceivedSelection = {}; ReceivedMembers.Reset();
    }
}
void UBSGroupRemoteCallObject::Server_TransformSnapshot_Implementation()
{
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    if (!Groups || !GetOwnerPlayerCharacter() || GetWorld()->GetRealTimeSeconds() - LastSnapshotRequest < 10) return;
    LastSnapshotRequest = GetWorld()->GetRealTimeSeconds();
    OutgoingSnapshot = Groups->TransformSnapshot;
    NextSnapshotIndex = 0;
    SendSnapshotBatch();
}
void UBSGroupRemoteCallObject::Server_TransformSnapshotNext_Implementation()
{
    if (GetOwnerPlayerCharacter()) SendSnapshotBatch();
}
void UBSGroupRemoteCallObject::SendSnapshotBatch()
{
    // One chunk in flight per client. Its receipt requests the next chunk, so a
    // large world's history cannot overflow the reliable channel at join time.
    if (!OutgoingSnapshot.IsValidIndex(NextSnapshotIndex)) return;
    const int32 Start = NextSnapshotIndex;
    int32 Bytes = 0;
    while (NextSnapshotIndex < OutgoingSnapshot.Num() && NextSnapshotIndex - Start < 48)
    {
        const auto& Change = OutgoingSnapshot[NextSnapshotIndex];
        const int32 Size = 512 + Change.Spline.Num() * 80 + Change.WirePoints.Num() * 32;
        if (NextSnapshotIndex > Start && Bytes + Size > 24000) break;
        Bytes += Size; ++NextSnapshotIndex;
    }
    TArray<FBSNativeTransform> Batch;
    Batch.Append(OutgoingSnapshot.GetData() + Start, NextSnapshotIndex - Start);
    const bool More = NextSnapshotIndex < OutgoingSnapshot.Num();
    if (!More) { OutgoingSnapshot.Reset(); NextSnapshotIndex = 0; }
    Client_TransformSnapshot(Batch, More);
}
void UBSGroupRemoteCallObject::Client_TransformSnapshot_Implementation(const TArray<FBSNativeTransform>& Changes, bool More)
{
    if (auto* Groups = ABSGroupSubsystem::Get(GetWorld())) Groups->Multicast_Transforms_Implementation(Changes);
    if (More) Server_TransformSnapshotNext();
}

void ABSGroupSubsystem::Multicast_BlueprintPose_Implementation(AFGBlueprintProxy* Proxy, FTransform Pose)
{
    if (!IsValid(Proxy) || !Pose.IsValid()) return;
    auto* Root = Proxy->GetRootComponent();
    const EComponentMobility::Type Mobility = Root ? Root->Mobility.GetValue() : EComponentMobility::Movable;
    if (Root) Root->Mobility = EComponentMobility::Movable;
    Proxy->SetActorTransform(Pose, false, nullptr, ETeleportType::TeleportPhysics);
    if (Root) Root->Mobility = Mobility;
    Proxy->ForceNetUpdate();
}
