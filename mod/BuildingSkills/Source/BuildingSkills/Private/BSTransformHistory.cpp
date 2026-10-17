#include "BSGroups.h"
#include "BSEditModeComponent.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"
#include "FGCharacterPlayer.h"
#include "FGHealthComponent.h"
#include "Equipment/FGBuildGun.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "FGCircuitConnectionComponent.h"

uint64 FBSTransformNative::Key(const FBSBuildingRef& Ref, int32 Index)
{
    if (Ref.LightweightClass) return (uint64(Ref.LightweightClass->GetUniqueID()) << 32) | uint32((Index == INDEX_NONE ? Ref.RuntimeIndex : Index) + 1);
    return Ref.Actor ? uint64(Ref.Actor->GetUniqueID()) << 32 : 0;
}
FBox FBSTransformNative::Bounds(UWorld* World, const FBSBuildingRef& Ref)
{
    if (Ref.LightweightClass)
    {
        auto* LW = AFGLightweightBuildableSubsystem::Get(World);
        auto* Data = LW ? LW->GetRuntimeDataForBuildableClassAndIndex(Ref.LightweightClass, Ref.ResolveIndex(World)) : nullptr;
        if (Data && Data->BoundingBox.IsValid) return Data->BoundingBox.TransformBy(Data->Transform);
    }
    else if (IsValid(Ref.Actor))
    {
        const auto Box = Ref.Actor->GetComponentsBoundingBox(true);
        if (Box.IsValid) return Box;
    }
    return FBox(Ref.Location(), Ref.Location());
}
namespace
{
    bool SameGeometry(const FBSNativeTransform& A, const FBSNativeTransform& B)
    {
        if (!A.Target.Equals(B.Target, .01) || A.Spline.Num() != B.Spline.Num() || A.HasLift != B.HasLift || A.HasWire != B.HasWire) return false;
        if (A.HasLift && !A.LiftTop.Equals(B.LiftTop, .01)) return false;
        if (A.HasWire && (!A.WireStart.Equals(B.WireStart, .01) || !A.WireEnd.Equals(B.WireEnd, .01) || A.WirePoints.Num() != B.WirePoints.Num())) return false;
        for (int32 I = 0; I < A.WirePoints.Num(); ++I) if (!A.WirePoints[I].Equals(B.WirePoints[I], .01)) return false;
        for (int32 I = 0; I < A.Spline.Num(); ++I)
            if (!A.Spline[I].Location.Equals(B.Spline[I].Location, .01) || !A.Spline[I].ArriveTangent.Equals(B.Spline[I].ArriveTangent, .01) || !A.Spline[I].LeaveTangent.Equals(B.Spline[I].LeaveTangent, .01)) return false;
        return true;
    }
}
bool FBSTransformNative::Matches(UWorld* World, const FBSNativeTransform& State)
{
    if (State.Building.LightweightClass)
    {
        auto* LW = AFGLightweightBuildableSubsystem::Get(World);
        auto* Data = LW ? LW->GetRuntimeDataForBuildableClassAndIndex(State.Building.LightweightClass, State.InstanceIndex) : nullptr;
        return Data && Data->IsValid() && Data->BuiltWithRecipe == State.Building.Recipe && Data->Transform.Equals(State.Target, .01);
    }
    return IsValid(State.Building.Actor) && !State.Building.Actor->GetIsDismantled() && SameGeometry(Capture(State.Building.Actor), State);
}
uint32 FBSTransformNative::Connections(const TArray<FBSNativeTransform>& Changes)
{
    TArray<uint64> Edges;
    auto Add = [&](UActorComponent* A, UActorComponent* B)
    { if (A && B) Edges.Add((uint64(A->GetUniqueID()) << 32) | B->GetUniqueID()); };
    for (const auto& C : Changes)
        if (auto* Actor = C.Building.Actor.Get())
        {
            TArray<AActor*> Children;
            Actor->GetChildDismantleActors_Implementation(Children);
            for (auto* Child : Children) if (IsValid(Child)) Edges.Add((uint64(Actor->GetUniqueID()) << 32) | Child->GetUniqueID());
            if (auto* Wire = Cast<AFGBuildableWire>(Actor))
                for (int32 I = 0; I < 2; ++I)
                    if (auto* End = Wire->GetConnection(I)) Edges.Add((uint64(Wire->GetUniqueID()) << 32) | End->GetUniqueID());
            TInlineComponentArray<UFGConnectionComponent*> Components(Actor);
            for (auto* Component : Components)
            {
                if (auto* F = Cast<UFGFactoryConnectionComponent>(Component)) Add(F, F->GetConnection());
                if (auto* P = Cast<UFGPipeConnectionComponentBase>(Component)) Add(P, P->GetConnection());
                if (auto* R = Cast<UFGRailroadTrackConnectionComponent>(Component)) for (auto* Other : R->GetConnections()) Add(R, Other);
                if (auto* Circuit = Cast<UFGCircuitConnectionComponent>(Component))
                {
                    TArray<AFGBuildableWire*> Wires; Circuit->GetWires(Wires);
                    for (auto* Wire : Wires) if (IsValid(Wire)) Edges.Add((uint64(Circuit->GetUniqueID()) << 32) | Wire->GetUniqueID());
                }
            }
        }
    Edges.Sort();
    uint32 Hash = 0;
    for (uint64 Edge : Edges) Hash = HashCombineFast(Hash, GetTypeHash(Edge));
    return Hash;
}
void ABSGroupSubsystem::RecordTransform(AFGCharacterPlayer* Player, const FBSTransformOptions* Options,
    const TArray<FBSNativeTransform>& Before, const TArray<FBSNativeTransform>& After, const FBSSelection& Selection)
{
    TransformHistory.RemoveAll([&](const FBSTransformHistory& H) { return !H.Player.IsValid() || (H.Player == Player && H.Undone); });
    const FGuid Gesture = Options ? Options->Gesture : FGuid::NewGuid();
    const uint32 Connections = FBSTransformNative::Connections(After);
    FBSTransformHistory* Entry = nullptr;
    for (int32 I = TransformHistory.Num() - 1; I >= 0; --I)
        if (TransformHistory[I].Player == Player)
        {
            auto& H = TransformHistory[I];
            bool Match = Gesture.IsValid() && H.Gesture == Gesture && H.After.Num() == Before.Num() && H.Connections == Connections && GetWorld()->GetRealTimeSeconds() - H.Time < 2;
            for (int32 J = 0; Match && J < Before.Num(); ++J)
                Match &= FBSTransformNative::Key(H.After[J].Building, H.After[J].InstanceIndex) == FBSTransformNative::Key(Before[J].Building, Before[J].InstanceIndex) && SameGeometry(H.After[J], Before[J]);
            if (Match) Entry = &H;
            break;
        }
    if (!Entry)
    {
        Entry = &TransformHistory.AddDefaulted_GetRef();
        Entry->Player = Player; Entry->Gesture = Gesture; Entry->Before = Before;
    }
    Entry->After = After; Entry->Selection = Selection; Entry->Connections = Connections; Entry->Time = GetWorld()->GetRealTimeSeconds();
    Entry->Versions.Reset();
    for (int32 I = 0; I < After.Num(); ++I)
    {
        Entry->Before[I].Building.Transform = After[I].Target;
        Entry->Versions.Add(BuildingVersions.FindRef(FBSTransformNative::Key(After[I].Building, After[I].InstanceIndex)));
    }
    // Bound both operation count and geometry memory across players.
    int32 Count = 0; int64 Bytes = 0;
    for (int32 I = TransformHistory.Num() - 1; I >= 0; --I)
    {
        const auto& H = TransformHistory[I];
        if (H.Player == Player) ++Count;
        Bytes += (H.Before.Num() + H.After.Num()) * sizeof(FBSNativeTransform);
        for (const auto& C : H.Before) Bytes += C.Spline.GetAllocatedSize() + C.WirePoints.GetAllocatedSize();
        for (const auto& C : H.After) Bytes += C.Spline.GetAllocatedSize() + C.WirePoints.GetAllocatedSize();
        if (Count > 32 && H.Player == Player) TransformHistory.RemoveAt(I);
        else if (Bytes > 64 * 1024 * 1024) { TransformHistory.RemoveAt(0, I + 1); break; }
    }
}
bool ABSGroupSubsystem::RestoreTransform(AFGCharacterPlayer* Player, bool Redo, FBSSelection& Result, FString& Error)
{
    if (!HasAuthority() || !IsValid(Player) || !Player->IsBuildGunEquipped() || !Player->GetBuildGun() ||
        !UBSEditModeComponent::IsEditDescriptor(Player->GetBuildGun()->GetCurrentBuildGunMode()) ||
        !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead() || Player->IsDrivingVehicle() || Player->IsInPortal())
    { Error = TEXT("Open Edit to undo or redo a transform."); return false; }
    int32 Index = INDEX_NONE;
    for (int32 I = 0; I < TransformHistory.Num(); ++I)
        if (TransformHistory[I].Player == Player && TransformHistory[I].Undone == Redo)
        { Index = I; if (Redo) break; }
    if (Index == INDEX_NONE) { Error = Redo ? TEXT("Nothing to redo.") : TEXT("Nothing to undo."); return false; }
    auto& H = TransformHistory[Index];
    const auto Expected = Redo ? H.Before : H.After;
    if (H.Connections != FBSTransformNative::Connections(Expected))
    { Error = TEXT("Connections changed since this edit. Undo/redo was left unapplied."); return false; }
    bool InReach = false;
    for (int32 I = 0; I < Expected.Num(); ++I)
    {
        const auto& C = Expected[I];
        if (!H.Versions.IsValidIndex(I) || BuildingVersions.FindRef(FBSTransformNative::Key(C.Building, C.InstanceIndex)) != H.Versions[I] || !FBSTransformNative::Matches(GetWorld(), C))
        { Error = TEXT("An affected building was changed or removed. Undo/redo was left unapplied."); return false; }
        InReach |= FVector::DistSquared(C.Target.GetLocation(), Player->GetActorLocation()) <= FMath::Square(Player->GetBuildGun()->GetBuildGunRange() + 1000.f);
    }
    if (!InReach) { Error = TEXT("Move within Build Gun range of the edited buildings."); return false; }
    auto Changes = Redo ? H.After : H.Before;
    auto Rollback = Expected;
    for (int32 I = 0; I < Changes.Num(); ++I)
    {
        Changes[I].Building.Transform = Expected[I].Target;
        Rollback[I].Building.Transform = Changes[I].Target;
    }
    if (!CommitTransforms(Changes, Rollback, Error)) return false;
    Result = H.Selection;
    // The stored selection follows the state we are leaving, including lightweight fingerprints.
    for (auto& Ref : Result.Buildings)
        for (const auto& C : Changes)
            if (FBSTransformNative::Key(Ref) == FBSTransformNative::Key(C.Building, C.InstanceIndex)) Ref = C.Building;
    PublishTransforms(Changes, Result);
    H.Selection = Result; H.Undone = !Redo;
    for (int32 I = 0; I < Expected.Num(); ++I) H.Versions[I] = BuildingVersions.FindRef(FBSTransformNative::Key(Expected[I].Building, Expected[I].InstanceIndex));
    // Rebase compatible earlier/later states after our own history operation.
    // Interleaved edits to disjoint selections must not strand older undo steps.
    for (int32 EntryIndex = 0; EntryIndex < TransformHistory.Num(); ++EntryIndex)
    {
        auto& Next = TransformHistory[EntryIndex];
        if (Next.Player != Player || (Redo ? EntryIndex <= Index || !Next.Undone : EntryIndex >= Index || Next.Undone)) continue;
        const auto& State = Next.Undone ? Next.Before : Next.After;
        for (int32 I = 0; I < State.Num(); ++I)
            for (const auto& C : Changes)
                if (FBSTransformNative::Key(State[I].Building, State[I].InstanceIndex) == FBSTransformNative::Key(C.Building, C.InstanceIndex) && SameGeometry(State[I], C))
                    Next.Versions[I] = BuildingVersions.FindRef(FBSTransformNative::Key(C.Building, C.InstanceIndex));
    }
    Error = Redo ? TEXT("Transform redone.") : TEXT("Transform undone.");
    return true;
}
