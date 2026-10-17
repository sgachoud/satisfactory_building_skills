#include "BSBlueprintSave.h"
#include "BSBlueprintScale.h"
#include "BSBlueprintBounds.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGBlueprintSubsystem.h"
#include "FGActorSaveHeaderTypes.h"
#include "FGCharacterPlayer.h"
#include "FGHealthComponent.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Equipment/FGBuildGun.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "InstanceData.h"
#include "Net/UnrealNetwork.h"
#include "Patching/NativeHookManager.h"

namespace
{
    thread_local TWeakObjectPtr<ABSBlueprintSaveSession> ActiveBlueprintWriter;
    thread_local bool BlueprintWriteSucceeded = false;
    using FExpectedBlueprintTransforms = TMap<FString, FTransform>;
    thread_local FExpectedBlueprintTransforms ExpectedBlueprintTransforms;


}

ABSBlueprintSaveSession::ABSBlueprintSaveSession()
{
    mDisplayName = FText::FromString(TEXT("Save Selection as Blueprint"));
    bReplicates = true;
    bOnlyRelevantToOwner = true;
    // AFGBuildable defaults to DORM_Initial for map buildings. This dynamically
    // spawned interaction session must reach its owning client immediately.
    NetDormancy = DORM_Awake;
    SetReplicateMovement(false);
    SetActorEnableCollision(false);
    SetActorHiddenInGame(true);
    PrimaryActorTick.bCanEverTick = false;
}
void ABSBlueprintSaveSession::BeginPlay()
{
    // Do not register as a buildable/designer or create a platform and storage.
    AActor::BeginPlay();
    if (HasAuthority()) SetLifeSpan(1800);
}
void ABSBlueprintSaveSession::EndPlay(EEndPlayReason::Type Reason) { AActor::EndPlay(Reason); }
void ABSBlueprintSaveSession::Destroyed() { AActor::Destroyed(); }
void ABSBlueprintSaveSession::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ABSBlueprintSaveSession, RequestId);
}

bool ABSBlueprintSaveSession::Prepare(FString& Error)
{
    auto* Controller = Cast<AFGPlayerController>(GetOwner());
    auto* Player = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    if (!HasAuthority() || !Player || !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead() ||
        !Player->GetBuildGun() || Player->IsDrivingVehicle() || Player->IsInPortal() ||
        (!bOpened && RequireReach && (!Player->IsBuildGunEquipped() || !UBSEditModeComponent::IsEditDescriptor(Player->GetBuildGun()->GetCurrentBuildGunMode()))))
    { Error = TEXT("Open Edit to save or copy a selection."); return false; }
    if (!Groups || SourceSelection.Groups.Num() > 2048 || SourceSelection.Buildings.Num() > 2048)
    { Error = TEXT("The selection is unavailable or too large."); return false; }
    TArray<FBSBuildingRef> CurrentMembers;
    for (const auto& Member : SourceSelection.Buildings) CurrentMembers.AddUnique(Member);
    for (const FGuid Id : SourceSelection.Groups)
    {
        if (!FBSGroupTree::Find(Groups->Groups, Id))
        { Error = TEXT("A selected group changed. Select it again."); return false; }
        FBSGroupTree::Collect(Groups->Groups, Id, CurrentMembers);
    }
    for (const auto& Member : CurrentMembers)
        if (!Member.IsAlive(GetWorld()))
        { Error = TEXT("A selected building is no longer available."); return false; }
    if (bOpened)
    {
        bool Unchanged = CurrentMembers.Num() == Members.Num();
        for (const auto& Member : Members)
            Unchanged &= CurrentMembers.Contains(Member) && (!Member.Actor || Member.Actor->GetActorTransform().Equals(Member.Transform, .01));
        if (!Unchanged) { Error = TEXT("The selection changed. Close this window and select it again."); return false; }
    }
    Members = MoveTemp(CurrentMembers);
    for (auto& Member : Members) if (Member.Actor) Member.Transform = Member.Actor->GetActorTransform();
    if (Members.IsEmpty() || Members.Num() > 2048)
    { Error = TEXT("Select between 1 and 2048 buildings."); return false; }
    mBuildables.Reset();
    TArray<FVector> BoundsPoints;
    TArray<double> BuildingYaws;
    bool InReach = false;
    for (const auto& Member : Members)
    {
        if (!Member.IsAlive(GetWorld())) { Error = TEXT("A selected building is no longer available."); return false; }
        auto* Building = Member.ResolveActor(GetWorld());
        if (!IsValid(Building)) { Error = TEXT("A selected building could not be read."); return false; }
        mBuildables.AddUnique(Building);
        const int32 PreviousPoints = BoundsPoints.Num();
        FTransform MemberTransform = Building->GetActorTransform();
        if (Member.LightweightClass)
        {
            // Lightweight actors are temporary interaction shells with no mesh
            // components. Their real geometry lives in the instance subsystem.
            auto* Lightweight = AFGLightweightBuildableSubsystem::Get(GetWorld());
            const int32 Index = Member.ResolveIndex(GetWorld());
            const auto* Data = Lightweight && Index != INDEX_NONE
                ? Lightweight->GetRuntimeDataForBuildableClassAndIndex(Member.LightweightClass, Index) : nullptr;
            if (Data && Data->BoundingBox.IsValid)
            {
                MemberTransform = Data->Transform;
                BSBlueprintBounds::AppendBox(Data->BoundingBox, MemberTransform, BoundsPoints);
            }
        }
        if (BoundsPoints.Num() == PreviousPoints) BSBlueprintBounds::AppendActorGeometry(Building, BoundsPoints);
        if (BoundsPoints.Num() == PreviousPoints)
        { Error = TEXT("A selected building's geometry is not available yet. Try again after it finishes loading."); return false; }
        BuildingYaws.Add(MemberTransform.Rotator().Yaw);
        InReach |= FVector::DistSquared(Member.Location(), Player->GetActorLocation()) <= FMath::Square(Player->GetBuildGun()->GetBuildGunRange() + 1000.f);
    }
    if (RequireReach && !InReach) { Error = TEXT("Move within Build Gun range of the selection."); return false; }
    FVector BoundsSize;
    const bool HasAnchor = SourceAnchor.Actor || SourceAnchor.LightweightClass;
    FQuat Rotation = FQuat::Identity;
    TArray<FVector> AnchorPoints;
    if (HasAnchor)
    {
        auto* Anchor = SourceAnchor.IsAlive(GetWorld()) ? SourceAnchor.ResolveActor(GetWorld()) : nullptr;
        if (!Anchor || FVector::DistSquared(Anchor->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(Player->GetBuildGun()->GetBuildGunRange() + 1000.f))
        { Error = TEXT("The blueprint anchor is unavailable or out of range. Choose it again."); return false; }
        Rotation = Anchor->GetActorQuat();
        if (bOpened && !Anchor->GetActorTransform().Equals(AnchorTransform, .01))
        { Error = TEXT("The blueprint anchor changed. Close the window and try again."); return false; }
        AnchorTransform = Anchor->GetActorTransform();
        if (SourceAnchor.LightweightClass)
        {
            auto* Lightweight = AFGLightweightBuildableSubsystem::Get(GetWorld());
            const int32 Index = SourceAnchor.ResolveIndex(GetWorld());
            const auto* Data = Lightweight && Index != INDEX_NONE
                ? Lightweight->GetRuntimeDataForBuildableClassAndIndex(SourceAnchor.LightweightClass, Index) : nullptr;
            if (Data && Data->BoundingBox.IsValid) BSBlueprintBounds::AppendBox(Data->BoundingBox, Data->Transform, AnchorPoints);
        }
        if (AnchorPoints.IsEmpty()) BSBlueprintBounds::AppendActorGeometry(Anchor, AnchorPoints);
        if (AnchorPoints.IsEmpty()) { Error = TEXT("The anchor's geometry is not available yet."); return false; }
    }
    if (!(HasAnchor ? BSBlueprintBounds::FitFrame(BoundsPoints, Rotation, BlueprintOrigin, BoundsSize, AnchorPoints)
        : BSBlueprintBounds::FitUpright(BoundsPoints, BuildingYaws, BlueprintOrigin, BoundsSize)))
    { Error = TEXT("The selection's bounds could not be calculated."); return false; }
    UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save bounds: members=%d reachable=%d size=%s center=%s yaw=%.3f"),
        Members.Num(), InReach, *BoundsSize.ToString(), *BlueprintOrigin.GetLocation().ToString(), BlueprintOrigin.Rotator().Yaw);
    // The native designer derives its archive origin from its actor transform
    // plus FLOOR_HEIGHT. Set that source of truth instead of relying on hooking
    // GetOffsetTransform, which can be inlined by the Linux compiler.
    if (auto* Root = GetRootComponent())
    {
        TGuardValue<TEnumAsByte<EComponentMobility::Type>> Mobility(Root->Mobility, EComponentMobility::Movable);
        SetActorTransform(FTransform(BlueprintOrigin.GetRotation(), BlueprintOrigin.GetLocation() - FVector(0, 0, FLOOR_HEIGHT)), false, nullptr, ETeleportType::TeleportPhysics);
    }
    // Ignore sub-micrometre rotation roundoff at exact native tile boundaries.
    const FVector Size = (BoundsSize - FVector(1.e-4)).ComponentMax(FVector::ZeroVector) / TILE_SIZE;
    mDimensions = FIntVector(FMath::Max(1, FMath::CeilToInt(Size.X)), FMath::Max(1, FMath::CeilToInt(Size.Y)), FMath::Max(1, FMath::CeilToInt(Size.Z)));
    mCurrentCost.Reset();
    if (auto* Blueprints = AFGBlueprintSubsystem::Get(GetWorld())) Blueprints->CalculateBlueprintCost(mBuildables, mCurrentCost);
    return true;
}

void ABSBlueprintSaveSession::Notify(const FString& Message)
{
    if (auto* PC = Cast<AFGPlayerController>(GetOwner()))
        if (auto* RCO = PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>()) RCO->Client_BlueprintMessage(Message);
}

void ABSBlueprintSaveSession::InstallHooks()
{
    if (WITH_EDITOR) return;
    SUBSCRIBE_METHOD(AFGBlueprintSubsystem::WriteBlueprintToDisk,
        [](auto& Scope, AFGBlueprintSubsystem* System, const FBlueprintRecord& Record)
        {
            if (!ActiveBlueprintWriter.IsValid()) return;
            // Inspect the completed native TOC. Inline header-serializer hooks
            // are not observable reliably in the Linux shipping executable.
            TArray<FActorSaveHeader> Headers;
            TSet<FString> SerializedBlueprintMembers;
            bool BlueprintContentsValid = RestoreBuildingSkillsBlueprintArchiveScales(System) &&
                ReadBuildingSkillsBlueprintHeaders(System->mStoredBlueprintData, Headers);
            for (const auto& Header : Headers)
                if (const auto* Expected = ExpectedBlueprintTransforms.Find(Header.ObjectHeader.Reference.ToString()))
                {
                    const FString Key = Header.ObjectHeader.Reference.ToString();
                    BlueprintContentsValid &= !SerializedBlueprintMembers.Contains(Key) && FTransform(Header.Transform).Equals(*Expected, .1);
                    SerializedBlueprintMembers.Add(Key);
                }
            if (!BlueprintContentsValid || SerializedBlueprintMembers.Num() != ExpectedBlueprintTransforms.Num() || ExpectedBlueprintTransforms.IsEmpty())
            {
                UE_LOG(LogBuildingSkills, Error, TEXT("Blueprint Save rejected invalid archive: expected=%d serialized=%d transformsValid=%d"),
                    ExpectedBlueprintTransforms.Num(), SerializedBlueprintMembers.Num(), BlueprintContentsValid);
                Scope.Override(false);
                return;
            }
            UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save archive validated: expected=%d serialized=%d"),
                ExpectedBlueprintTransforms.Num(), SerializedBlueprintMembers.Num());
            const bool Success = Scope(System, Record);
            BlueprintWriteSucceeded = Success;
        });
    SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::SaveBlueprint,
        [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, FBlueprintRecord Record, AFGPlayerController* Controller, bool UpdateAuthor)
        {
            auto* Session = Cast<ABSBlueprintSaveSession>(Designer);
            if (!Session) return;
            UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save dispatch: authority=%d owner=%d"), Session->HasAuthority(), Controller == Session->GetOwner());
            // SaveBlueprint is also the native client entry point: it constructs
            // category records and sends Server_SaveBlueprintInDesigner. Never
            // run authority-only Prepare before that forwarding branch.
            if (!Session->HasAuthority())
            {
                if (Controller != Session->GetOwner()) { Scope.Cancel(); return; }
                if (auto* Player = Cast<AFGCharacterPlayer>(Controller->GetPawn()))
                    if (auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>()) Edit->BlueprintMessage(TEXT("Saving blueprint on server..."));
                return;
            }
            FString Error;
            if (Controller != Session->GetOwner() || Record.BlueprintName.IsEmpty() || Record.BlueprintName.Len() > 200 ||
                AFGBlueprintSubsystem::SanitizeBlueprintFileName(Record.BlueprintName) != Record.BlueprintName || !Session->Prepare(Error))
            {
                Session->ShowSummaryOnly();
                Session->Notify(Error.IsEmpty() ? TEXT("Invalid blueprint save request.") : Error);
                Scope.Cancel(); return;
            }
            // The vanilla writer handles serialization, descriptors, file transfer,
            // categories and overwrite semantics. Only its source set/origin differ.
            struct FMembership
            {
                AFGBuildable* Building;
                AFGBuildableBlueprintDesigner* Designer;
                AFGBlueprintProxy* Proxy;
                bool Inside;
            };
            TArray<FMembership> Membership;
            TGuardValue<FExpectedBlueprintTransforms> ExpectedGuard(ExpectedBlueprintTransforms, {});
            for (AFGBuildable* Building : Session->mBuildables)
            {
                Membership.Add({Building, Building->mBlueprintDesigner, Building->mBlueprintProxy, Building->mReplicatedBuiltInsideBlueprintDesigner});
                ExpectedBlueprintTransforms.Add(FObjectReferenceDisc(Building).ToString(), Building->GetActorTransform().GetRelativeTransform(Session->BlueprintOrigin));
            }
            TGuardValue<TWeakObjectPtr<ABSBlueprintSaveSession>> Writer(ActiveBlueprintWriter, Session);
            BlueprintWriteSucceeded = false;
            Scope(Designer, Record, Controller, UpdateAuthor);
            for (const auto& Entry : Membership)
                if (IsValid(Entry.Building))
                {
                    Entry.Building->mBlueprintDesigner = Entry.Designer;
                    Entry.Building->mBlueprintProxy = Entry.Proxy;
                    Entry.Building->mReplicatedBuiltInsideBlueprintDesigner = Entry.Inside;
                }
            Session->ShowSummaryOnly();
            Session->ForceNetUpdate();
            Session->Notify(BlueprintWriteSucceeded ? FString::Printf(TEXT("Blueprint saved: %s"), *Record.BlueprintName)
                : TEXT("Blueprint could not be saved correctly. Check the server log; no successful save was confirmed."));
            UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save disk result: success=%d members=%d"), BlueprintWriteSucceeded, Membership.Num());
        });
    SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::DismantleCurrentBuildables,
        [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, AFGPlayerController*)
        { if (Cast<ABSBlueprintSaveSession>(Designer)) Scope.Cancel(); });
    SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::LoadBlueprintIntoDesigner,
        [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, UFGBlueprintDescriptor*, AFGPlayerController*)
        { if (Cast<ABSBlueprintSaveSession>(Designer)) Scope.Override(EBlueprintDesignerLoadResult::BDLR_Undefined); });
}

void UBSGroupRemoteCallObject::Server_OpenBlueprintSave_Implementation(const FBSSelection& Selection, FGuid RequestId, const FBSBuildingRef& Anchor)
{
    auto* Player = GetOwnerPlayerCharacter();
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    if (!PC || !RequestId.IsValid()) return;
    for (TActorIterator<ABSBlueprintSaveSession> It(GetWorld()); It; ++It)
        if (It->GetOwner() == PC) It->Destroy();
    FActorSpawnParameters Params;
    Params.Owner = PC; Params.ObjectFlags |= RF_Transient;
    auto* Session = GetWorld()->SpawnActor<ABSBlueprintSaveSession>(ABSBlueprintSaveSession::StaticClass(), Player->GetActorTransform(), Params);
    FString Error;
    if (Session)
    {
        Session->SourceSelection = Selection;
        Session->SourceAnchor = Anchor;
        if (Session->Prepare(Error))
        {
            Session->bOpened = true;
            Session->ShowSummaryOnly();
            Session->RequestId = RequestId;
            Session->ForceNetUpdate();
            // Actor creation and this RCO use different network channels. Send
            // identity, then let the client wait for the replicated actor.
            Client_OpenBlueprintSave(RequestId, TEXT(""));
            return;
        }
        Session->Destroy();
    }
    Client_OpenBlueprintSave(RequestId, Error.IsEmpty() ? TEXT("Could not open blueprint saving.") : Error);
}
void UBSGroupRemoteCallObject::Client_OpenBlueprintSave_Implementation(FGuid RequestId, const FString& Error)
{
    if (auto* Player = GetOwnerPlayerCharacter())
        if (auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>()) Edit->OnBlueprintSaveReply(RequestId, Error);
}
void UBSGroupRemoteCallObject::Server_CloseBlueprintSave_Implementation(FGuid RequestId)
{
    auto* Player = GetOwnerPlayerCharacter();
    if (!Player || !RequestId.IsValid()) return;
    for (TActorIterator<ABSBlueprintSaveSession> It(GetWorld()); It; ++It)
        if (It->RequestId == RequestId && It->GetOwner() == Player->GetController()) It->Destroy();
}
void UBSGroupRemoteCallObject::Client_BlueprintMessage_Implementation(const FString& Message)
{
    if (auto* Player = GetOwnerPlayerCharacter())
        if (auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>()) Edit->BlueprintMessage(Message);
}
