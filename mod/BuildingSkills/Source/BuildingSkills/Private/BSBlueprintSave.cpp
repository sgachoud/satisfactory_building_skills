#include "BSBlueprintSave.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGBlueprintSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGHealthComponent.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Equipment/FGBuildGun.h"
#include "EngineUtils.h"
#include "Patching/NativeHookManager.h"

ABSBlueprintSaveSession::ABSBlueprintSaveSession()
{
    mDisplayName = FText::FromString(TEXT("Save Group as Blueprint"));
    bReplicates = true;
    bOnlyRelevantToOwner = true;
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

bool ABSBlueprintSaveSession::Prepare(FString& Error)
{
    auto* Controller = Cast<AFGPlayerController>(GetOwner());
    auto* Player = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    if (!HasAuthority() || !Player || !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead() ||
        !Player->GetBuildGun() || Player->IsDrivingVehicle() || Player->IsInPortal() ||
        (!bOpened && (!Player->IsBuildGunEquipped() || !UBSEditModeComponent::IsEditDescriptor(Player->GetBuildGun()->GetCurrentBuildGunMode()))))
    { Error = TEXT("Open Edit to save a group."); return false; }
    if (!Groups || Groups->Revision != GroupRevision || !FBSGroupTree::Find(Groups->Groups, Group))
    { Error = TEXT("The group changed. Close this window and select it again."); return false; }
    Members.Reset();
    FBSGroupTree::Collect(Groups->Groups, Group, Members);
    if (Members.IsEmpty() || Members.Num() > 2048)
    { Error = TEXT("Select a group containing between 1 and 2048 buildings."); return false; }
    mBuildables.Reset();
    FBox Bounds(ForceInit);
    bool InReach = false;
    for (const auto& Member : Members)
    {
        if (!Member.IsAlive(GetWorld())) { Error = TEXT("A selected building is no longer available."); return false; }
        auto* Building = Member.ResolveActor(GetWorld());
        if (!IsValid(Building)) { Error = TEXT("A selected building could not be read."); return false; }
        mBuildables.AddUnique(Building);
        FBox MemberBounds(ForceInit);
        if (Member.LightweightClass)
        {
            // Lightweight actors are temporary interaction shells with no mesh
            // components. Their real geometry lives in the instance subsystem.
            auto* Lightweight = AFGLightweightBuildableSubsystem::Get(GetWorld());
            const int32 Index = Member.ResolveIndex(GetWorld());
            const auto* Data = Lightweight && Index != INDEX_NONE
                ? Lightweight->GetRuntimeDataForBuildableClassAndIndex(Member.LightweightClass, Index) : nullptr;
            if (Data && Data->BoundingBox.IsValid) MemberBounds = Data->BoundingBox.TransformBy(Data->Transform);
        }
        if (!MemberBounds.IsValid) MemberBounds = Building->GetComponentsBoundingBox(true);
        if (!MemberBounds.IsValid)
        { Error = TEXT("A selected building's geometry is not available yet. Try again after it finishes loading."); return false; }
        Bounds += MemberBounds;
        InReach |= FVector::DistSquared(Member.Location(), Player->GetActorLocation()) <= FMath::Square(Player->GetBuildGun()->GetBuildGunRange() + 1000.f);
    }
    UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save bounds: members=%d reachable=%d valid=%d size=%s"),
        Members.Num(), InReach, Bounds.IsValid, *Bounds.GetSize().ToString());
    if (!InReach) { Error = TEXT("Move within Build Gun range of the group."); return false; }
    // Center horizontally, with the bottom of the selection at blueprint floor height.
    BlueprintOrigin = FTransform(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z));
    const FVector Size = Bounds.GetSize() / TILE_SIZE;
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
    SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::GetOffsetTransform,
        [](auto& Scope, const AFGBuildableBlueprintDesigner* Designer, FTransform& Out)
        {
            if (auto* Session = Cast<ABSBlueprintSaveSession>(Designer)) { Out = Session->BlueprintOrigin; Scope.Cancel(); }
        });
    SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::SaveBlueprint,
        [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, FBlueprintRecord Record, AFGPlayerController* Controller, bool UpdateAuthor)
        {
            auto* Session = Cast<ABSBlueprintSaveSession>(Designer);
            if (!Session) return;
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
            Scope(Designer, Record, Controller, UpdateAuthor);
            Session->ShowSummaryOnly();
            Session->ForceNetUpdate();
            Session->Notify(TEXT("Blueprint save submitted."));
        });
    SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::DismantleCurrentBuildables,
        [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, AFGPlayerController*)
        { if (Cast<ABSBlueprintSaveSession>(Designer)) Scope.Cancel(); });
    SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::LoadBlueprintIntoDesigner,
        [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, UFGBlueprintDescriptor*, AFGPlayerController*)
        { if (Cast<ABSBlueprintSaveSession>(Designer)) Scope.Override(EBlueprintDesignerLoadResult::BDLR_Undefined); });
}

void UBSGroupRemoteCallObject::Server_OpenBlueprintSave_Implementation(FGuid Group, int32 ExpectedRevision)
{
    auto* Player = GetOwnerPlayerCharacter();
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    if (!PC) return;
    for (TActorIterator<ABSBlueprintSaveSession> It(GetWorld()); It; ++It)
        if (It->GetOwner() == PC) It->Destroy();
    FActorSpawnParameters Params;
    Params.Owner = PC; Params.ObjectFlags |= RF_Transient;
    auto* Session = GetWorld()->SpawnActor<ABSBlueprintSaveSession>(ABSBlueprintSaveSession::StaticClass(), Player->GetActorTransform(), Params);
    FString Error;
    if (Session)
    {
        Session->Group = Group; Session->GroupRevision = ExpectedRevision;
        if (Session->Prepare(Error)) { Session->bOpened = true; Session->ShowSummaryOnly(); Session->ForceNetUpdate(); Client_OpenBlueprintSave(Session, TEXT("")); return; }
        Session->Destroy();
    }
    Client_OpenBlueprintSave(nullptr, Error.IsEmpty() ? TEXT("Could not open blueprint saving.") : Error);
}
void UBSGroupRemoteCallObject::Client_OpenBlueprintSave_Implementation(ABSBlueprintSaveSession* Session, const FString& Error)
{
    if (auto* Player = GetOwnerPlayerCharacter())
        if (auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>()) Edit->OpenBlueprintSave(Session, Error);
}
void UBSGroupRemoteCallObject::Server_CloseBlueprintSave_Implementation(ABSBlueprintSaveSession* Session)
{
    auto* Player = GetOwnerPlayerCharacter();
    if (IsValid(Session) && Player && Session->GetOwner() == Player->GetController()) Session->Destroy();
}
void UBSGroupRemoteCallObject::Client_BlueprintMessage_Implementation(const FString& Message)
{
    if (auto* Player = GetOwnerPlayerCharacter())
        if (auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>()) Edit->BlueprintMessage(Message);
}
