#include "BSEditModeComponent.h"
#include "BSEditModeWidget.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildable.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "FGBlueprintProxy.h"
#include "FGCharacterPlayer.h"
#include "FGHealthComponent.h"
#include "FGInputLibrary.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGPlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Patching/NativeHookManager.h"
#include "PlayerMappableKeySettings.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "Blueprint/WidgetTree.h"
#include "Components/WrapBox.h"
#include "Components/CanvasPanelSlot.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"

#define LOCTEXT_NAMESPACE "BuildingSkills"

namespace
{
    void SetSelectionLimit(UFGDismantleModeDescriptor* Mode)
    {
        if (auto* Limit = FindFProperty<FIntProperty>(UFGDismantleModeDescriptor::StaticClass(), TEXT("mMultiDismantleLimit")))
            Limit->SetPropertyValue_InContainer(Mode, 2048);
    }
}
UBSGroupModeDescriptor::UBSGroupModeDescriptor() { mDisplayName = LOCTEXT("GroupSelection", "Group"); SetSelectionLimit(this); }
UBSEditModeDescriptor::UBSEditModeDescriptor() { mDisplayName = LOCTEXT("NormalSelection", "Normal"); SetSelectionLimit(this); }
UBSEditBlueprintModeDescriptor::UBSEditBlueprintModeDescriptor() { mDisplayName = LOCTEXT("BlueprintSelection", "Blueprint"); }
UBSEditGroupModeDescriptor::UBSEditGroupModeDescriptor() { mDisplayName = LOCTEXT("GroupSelection", "Group"); }

UBSEditModeComponent::UBSEditModeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

bool UBSEditModeComponent::IsEditDescriptor(TSubclassOf<UFGBuildGunModeDescriptor> Mode)
{
    return Mode && Mode->IsChildOf(UBSEditModeDescriptor::StaticClass());
}

UBSEditModeComponent* UBSEditModeComponent::ForGun(AFGBuildGun* Gun)
{
    auto* Player = Gun ? Gun->GetInstigatorCharacter() : nullptr;
    return Player && Player->IsLocallyControlled() ? Player->FindComponentByClass<UBSEditModeComponent>() : nullptr;
}

void UBSEditModeComponent::InstallHooks()
{
    if (!WITH_EDITOR) {
    SUBSCRIBE_METHOD(AFGBuildGun::OnModeSelectPressed, [](auto& Scope, AFGBuildGun* Gun)
    {
        if (auto* Edit = ForGun(Gun); Edit && Edit->bTransforming)
        { if (Edit->InputAllowed()) Edit->CycleTransform(1); Scope.Cancel(); }
    });
    SUBSCRIBE_METHOD(AFGBuildGun::OnModeSelectReleased, [](auto& Scope, AFGBuildGun* Gun)
    { if (auto* Edit = ForGun(Gun); Edit && Edit->bTransforming) Scope.Cancel(); });
    SUBSCRIBE_METHOD(AFGBuildGun::CycleBuildMode, [](auto& Scope, AFGBuildGun* Gun, int32 Delta)
    { if (auto* Edit = ForGun(Gun); Edit && Edit->bTransforming) { Edit->CycleTransform(Delta); Scope.Cancel(); } });
    SUBSCRIBE_METHOD(AFGBuildGun::Scroll, [](auto& Scope, AFGBuildGun* Gun, int32 Delta)
    { if (auto* Edit = ForGun(Gun); Edit && Edit->bTransforming) { Edit->ScrollTransform(Delta); Scope.Cancel(); } });
    SUBSCRIBE_METHOD(UFGGameUI::Native_HandlePauseGamePressed, [](auto& Scope, UFGGameUI* UI)
    {
        auto* Pawn = UI->GetOwningPlayerPawn();
        auto* Edit = Pawn ? Pawn->FindComponentByClass<UBSEditModeComponent>() : nullptr;
        if (Edit && Edit->bTransforming && Edit->InputAllowed()) { Edit->EndTransform(); Scope.Cancel(); }
    });
    // Editor FactoryGame methods are stubs. Hook real game binaries only.
    SUBSCRIBE_METHOD(UFGBuildGunStateDismantle::UpdatePeekDismantleRefunds,
        [](auto& Scope, UFGBuildGunStateDismantle* State, bool NoBuildCost)
        {
            if (auto* Edit = ForGun(State->GetBuildGun()))
            {
                if (Edit->bRefundNoBuildCost != NoBuildCost) Edit->bSelectionDirty = true;
                Edit->bRefundNoBuildCost = NoBuildCost;
                // Each AddPending call otherwise rebuilds the complete refund UI.
                // Edit needs no refunds; dismantle refreshes once after a batch.
                if (Edit->EditGun.IsValid() || Edit->bSyncing ||
                    (Edit->bNativeTick && !Edit->Highlighted.IsEmpty())) { Scope.Cancel(); return; }
                ++Edit->RefundRefreshes;
            }
        });
    SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGBuildGunStateDismantle, GetSupportedBuildModes_Implementation,
        [](const UFGBuildGunStateDismantle* State, TArray<TSubclassOf<UFGBuildGunModeDescriptor>>& Modes)
        {
            auto* Edit = ForGun(State->GetBuildGun());
            if ((Edit && Edit->EditGun.IsValid()) || IsEditDescriptor(State->GetBuildGun()->GetCurrentBuildGunMode()))
            {
                Modes.Reset();
                Modes.Add(UBSEditModeDescriptor::StaticClass());
                Modes.Add(UBSEditBlueprintModeDescriptor::StaticClass());
                Modes.Add(UBSEditGroupModeDescriptor::StaticClass());
            }
            else
            {
                Modes.AddUnique(UBSGroupModeDescriptor::StaticClass());
                // A remote server copy must accept the client's first Edit mode
                // before that mode has become the replicated current value.
                if (!Edit && State->GetBuildGun()->HasAuthority())
                {
                    Modes.AddUnique(UBSEditModeDescriptor::StaticClass());
                    Modes.AddUnique(UBSEditBlueprintModeDescriptor::StaticClass());
                    Modes.AddUnique(UBSEditGroupModeDescriptor::StaticClass());
                }
            }
        });
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle, OnBuildGunModeChanged_Implementation,
        [](auto& Scope, UFGBuildGunStateDismantle* State, TSubclassOf<UFGBuildGunModeDescriptor> Mode)
        {
            auto* Edit = ForGun(State->GetBuildGun());
            if (Edit) Edit->bChangingMode = true;
            Scope(State, Mode);
            // Blueprint selection expands the building's proxy membership. Trace
            // its actual buildings rather than the vanilla whole-proxy hit volume.
            State->GetBuildGun()->SetAllowRayBlueprintProxyHit(false);
            State->mCurrentMultiDismantleLimit = 2048;
            if (IsEditDescriptor(Mode)) State->mLastUsedDismantleMode = State->mDefaultDismantleMode;
            if (Edit) { Edit->bChangingMode = false; Edit->bSelectionDirty = true; Edit->LastHover = {}; }
        });
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle, TickState_Implementation,
        [](auto& Scope, UFGBuildGunStateDismantle* State, float Delta)
        {
            auto* Gun = State->GetBuildGun();
            auto* Edit = ForGun(Gun);
            if (!Edit) return;
            Edit->ActiveState = State;
            // Retain vanilla tracing, timing, disqualifiers and refunds. Replace only
            // its target expansion and mass-selection with the shared persistent list.
            const auto Mode = Gun->mCurrentBuildGunMode;
            const bool Multi = State->mIsMultiSelectActive;
            Gun->mCurrentBuildGunMode = State->mDefaultDismantleMode;
            State->mIsMultiSelectActive = false;
            Edit->bNativeTick = true;
            Scope(State, Delta);
            Edit->bNativeTick = false;
            if (Gun->GetCurrentState() != State) { Edit->ClearSelection(); return; }
            Gun->mCurrentBuildGunMode = Mode;
            State->mIsMultiSelectActive = Multi;
            State->mCurrentMultiDismantleLimit = 2048;
            Edit->UpdateSelection(State);
        });
    SUBSCRIBE_METHOD(UFGBuildGunStateDismantle::ClearPendingSelectedActors,
        [](auto& Scope, UFGBuildGunStateDismantle* State)
        {
            if (auto* Edit = ForGun(State->GetBuildGun()))
            {
                if (Edit->bChangingMode) { Scope.Cancel(); return; }
                if (!Edit->bNativeTick && !Edit->bSyncing) Edit->ClearSelection();
            }
        });
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle, ResetBuildGunDelay,
        [](auto& Scope, UFGBuildGunStateDismantle* State)
        {
            // Native hover changes must not restart the hold timer for an unchanged
            // persistent selection. Explicit selection changes still reset it below.
            if (auto* Edit = ForGun(State->GetBuildGun()); Edit && !Edit->Highlighted.IsEmpty() &&
                (Edit->bNativeTick || Edit->bSyncing)) Scope.Cancel();
        });
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle, PrimaryFire_Implementation,
        [](auto& Scope, UFGBuildGunStateDismantle* State)
        {
            // Never issue a dismantle request in Edit, including on server copies.
            auto* Edit = ForGun(State->GetBuildGun());
            if (IsEditDescriptor(State->GetBuildGun()->GetCurrentBuildGunMode()) || (Edit && Edit->EditGun.IsValid()))
            { Scope.Cancel(); return; }
            if (Edit)
            {
                if (!Edit->InputAllowed()) { Scope.Cancel(); return; }
                Edit->SyncDismantle(State);
            }
        });
    SUBSCRIBE_METHOD(AFGLightweightBuildableSubsystem::RemoveByInstanceIndex,
        [](auto&, AFGLightweightBuildableSubsystem* LW, TSubclassOf<AFGBuildable> Class, int32 Index)
        { if (auto* Groups = ABSGroupSubsystem::Get(LW->GetWorld())) Groups->RemoveLightweight(Class, Index); });
    SUBSCRIBE_METHOD(AFGLightweightBuildableSubsystem::InvalidateRuntimeInstanceDataForIndex,
        [](auto&, AFGLightweightBuildableSubsystem* LW, TSubclassOf<AFGBuildable> Class, int32)
        { if (auto* Groups = ABSGroupSubsystem::Get(LW->GetWorld())) Groups->InvalidateLightweightLookup(Class); });
    SUBSCRIBE_METHOD(AFGLightweightBuildableSubsystem::AddFromBuildableInstanceData,
        [](auto&, AFGLightweightBuildableSubsystem* LW, TSubclassOf<AFGBuildable> Class, FRuntimeBuildableInstanceData&, bool, int32, uint16, AActor*, int32)
        { if (auto* Groups = ABSGroupSubsystem::Get(LW->GetWorld())) Groups->InvalidateLightweightLookup(Class); });
    SUBSCRIBE_METHOD(AFGLightweightBuildableSubsystem::AddFromReplicatedData,
        [](auto&, AFGLightweightBuildableSubsystem* LW, TSubclassOf<AFGBuildable> Class, TSubclassOf<UFGRecipe>, const FLightweightBuildableReplicationItem&, int32, AActor*, int32)
        { if (auto* Groups = ABSGroupSubsystem::Get(LW->GetWorld())) Groups->InvalidateLightweightLookup(Class); });
    SUBSCRIBE_UOBJECT_METHOD(AFGBuildable, Dismantle_Implementation,
        [](auto&, AFGBuildable* Building)
        { if (auto* Groups = ABSGroupSubsystem::Get(Building->GetWorld())) Groups->RemoveBuilding(Building); });
    // RPC implementations are virtual too: SML needs the class default object
    // to resolve their vtable entry in the packaged game.
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle, Server_DismantleActors_Implementation,
        [](auto& Scope, UFGBuildGunStateDismantle* State, const TArray<AActor*>&, const TArray<FDismantleLightweightBundle>&)
        {
            auto* Edit = ForGun(State->GetBuildGun());
            if (IsEditDescriptor(State->GetBuildGun()->GetCurrentBuildGunMode()) || (Edit && Edit->EditGun.IsValid())) Scope.Cancel();
        });
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle, CanBeginBuildGunDelay,
        [](auto& Scope, const UFGBuildGunStateDismantle* State)
        {
            auto* Edit = ForGun(State->GetBuildGun());
            if (IsEditDescriptor(State->GetBuildGun()->GetCurrentBuildGunMode()) || (Edit && Edit->EditGun.IsValid())) Scope.Override(false);
        });
    }
}

void UBSEditModeComponent::InitializeInput(UEnhancedInputComponent* Input)
{
    ReleaseInput();
    LeaveEditMode(false);
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
    if (!PC) return;
    // Equipment input can block the pawn's component. Keep Edit commands above
    // that component while allowing unrelated native controls to pass through.
    Input = NewObject<UEnhancedInputComponent>(PC);
    Input->Priority = 1000;
    Input->bBlockInput = false;
    Input->RegisterComponent();
    PC->PushInputComponent(Input);
    BoundInput = Input;
    InputSubsystem = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    if (auto* Source = LoadObject<UInputMappingContext>(nullptr, TEXT("/BuildingSkills/Inputs/MC_BuildingSkills.MC_BuildingSkills")); Source && InputSubsystem.IsValid())
    {
        // Own a runtime copy so native player-context changes cannot remove it.
        // Mappable names and actions remain identical to the settings assets.
        ToggleContext = DuplicateObject<UInputMappingContext>(Source, this);
        InputSubsystem->AddMappingContext(ToggleContext, 1000);
    }
    bToggleHeld = false;
    ToggleAction = LoadObject<UInputAction>(nullptr, TEXT("/BuildingSkills/Inputs/IA_ToggleEditMode.IA_ToggleEditMode"));
    MultiSelectAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/Dismantle/IA_DismantleMultiSelect.IA_DismantleMultiSelect"));
    ModeAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/IA_ModeSelect.IA_ModeSelect"));
    if (ToggleAction)
    {
        Input->BindAction(ToggleAction, ETriggerEvent::Triggered, this, &ThisClass::OnToggleTriggered);
        Input->BindAction(ToggleAction, ETriggerEvent::Completed, this, &ThisClass::OnToggleReleased);
        Input->BindAction(ToggleAction, ETriggerEvent::Canceled, this, &ThisClass::OnToggleReleased);
    }
    const auto Bind = [&](const TCHAR* Path, auto Handler)
    { if (auto* Action = LoadObject<UInputAction>(nullptr, Path)) Input->BindAction(Action, ETriggerEvent::Started, this, Handler); };
    Bind(TEXT("/BuildingSkills/Inputs/IA_Transform.IA_Transform"), &ThisClass::OnTransform);
    for (int32 Direction : {1, -1})
    {
        const TCHAR* Name = Direction > 0 ? TEXT("ScrollUp") : TEXT("ScrollDown");
        const FString Path = FString::Printf(TEXT("/BuildingSkills/Inputs/IA_%s.IA_%s"), Name, Name);
        if (auto* Action = LoadObject<UInputAction>(nullptr, *Path))
            Input->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::ScrollTransform, Direction);
    }
    const TCHAR* MoveNames[] = {TEXT("Forward"), TEXT("Backward"), TEXT("Left"), TEXT("Right"), TEXT("Up"), TEXT("Down")};
    for (int32 I = 0; I < 6; ++I)
    {
        const FString Path = FString::Printf(TEXT("/BuildingSkills/Inputs/IA_Move%s.IA_Move%s"), MoveNames[I], MoveNames[I]);
        if (auto* Action = LoadObject<UInputAction>(nullptr, *Path))
        {
            Input->BindAction(Action, ETriggerEvent::Triggered, this, &ThisClass::OnMove, I);
            Input->BindAction(Action, ETriggerEvent::Completed, this, &ThisClass::OnMoveReleased, I);
            Input->BindAction(Action, ETriggerEvent::Canceled, this, &ThisClass::OnMoveReleased, I);
        }
    }
    Bind(TEXT("/BuildingSkills/Inputs/IA_Group.IA_Group"), &ThisClass::OnGroup);
    Bind(TEXT("/BuildingSkills/Inputs/IA_Merge.IA_Merge"), &ThisClass::OnMerge);
    Bind(TEXT("/BuildingSkills/Inputs/IA_Ungroup.IA_Ungroup"), &ThisClass::OnUngroup);
    Bind(TEXT("/BuildingSkills/Inputs/IA_Save.IA_Save"), &ThisClass::OnSave);
}

bool UBSEditModeComponent::InputAllowed() const
{
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    if (!Player || !Player->IsLocallyControlled() || Player->IsDrivingVehicle() || Player->IsInPortal() ||
        UGameplayStatics::IsGamePaused(this) || !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead()) return false;
    // The Build Gun itself opens an interact widget. Only other interaction
    // windows (inventory, machine settings, etc.) should suspend gun input.
    if (auto* UI = Player->GetGameUI())
        for (auto* Widget : UI->GetInteractWidgetStack())
            if (Widget && (Widget->mDisableBuildGunActions || Widget->mUseKeyboard || Widget->mUseMouse)) return false;
    return true;
}

bool UBSEditModeComponent::IsEditing() const
{
    return EditGun.IsValid() && IsEditDescriptor(EditGun->GetCurrentBuildGunMode());
}

void UBSEditModeComponent::OnToggleTriggered()
{
    if (bToggleHeld) return;
    bToggleHeld = true;
    if (!InputAllowed()) return;
    if (EditGun.IsValid()) { LeaveEditMode(true); return; }
    auto* Player = CastChecked<AFGCharacterPlayer>(GetOwner());
    if (!Player->CanEquipBuildGunForDismantle()) return;
    auto* Gun = Player->GetBuildGun();
    if (!IsValid(Gun)) return;
    ClearSelection();
    EditGun = Gun;
    bEnteringEdit = true;
    EditEntryTime = 0.0f;
    // HotKeyDismantle handles the deferred equipment transition. Apply the Edit
    // descriptor once the equipped gun has actually entered that state.
    if (!Player->IsBuildGunEquipped() || !Gun->IsInState(EBuildGunState::BGS_DISMANTLE)) Player->HotKeyDismantle();
    UE_LOG(LogBuildingSkills, Display, TEXT("Edit mode requested; waiting for equipped dismantle state."));
}

void UBSEditModeComponent::OnToggleReleased() { bToggleHeld = false; }
void UBSEditModeComponent::OnGroup() { Request(EBSGroupOperation::Group); }
void UBSEditModeComponent::OnMerge() { Request(EBSGroupOperation::Merge); }
void UBSEditModeComponent::OnUngroup() { Request(EBSGroupOperation::Ungroup); }

void UBSEditModeComponent::Request(EBSGroupOperation Operation)
{
    if (!IsEditing() || bTransforming || !InputAllowed() || bRequestPending || bOpeningBlueprint || BlueprintWindow) return;
    auto* System = ABSGroupSubsystem::Get(GetWorld());
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    auto* RCO = PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    if (!System || !RCO) { Feedback = TEXT("Grouping is not ready yet."); return; }
    const FBSSelection Selected = EffectiveSelection();
    if (Operation == EBSGroupOperation::Ungroup && (Selected.Groups.Num() != 1 || !Selected.Buildings.IsEmpty())) return;
    if (Selected.IsEmpty()) { Feedback = TEXT("Select buildings or groups first."); return; }
    bRequestPending = true;
    RCO->Server_Apply(Operation, Selected, System->Revision);
}

void UBSEditModeComponent::OnOperationResult(bool Success, const FBSSelection& Result, const FString& Message, int32 Revision)
{
    Feedback = Message;
    if (!IsEditing()) { bRequestPending = false; return; }
    if (Success)
    {
        PendingResult = Result;
        PendingRevision = Revision;
        Feedback = TEXT("Group updated.");
    }
    else bRequestPending = false;
}

FBSSelection UBSEditModeComponent::EffectiveSelection() const
{
    // Selection is normalized when it changes or the replicated tree changes.
    // HUD reads must not rescan every group for every selected building.
    return Selection.IsEmpty() ? Hover : Selection;
}

TArray<FBSBuildingRef> UBSEditModeComponent::ResolveSelection(const FBSSelection& Value) const
{
    TArray<FBSBuildingRef> Result;
    for (const auto& B : Value.Buildings) Result.AddUnique(B);
    if (auto* System = ABSGroupSubsystem::Get(GetWorld()))
        for (FGuid Id : Value.Groups) FBSGroupTree::Collect(System->Groups, Id, Result);
    Result.RemoveAll([&](const FBSBuildingRef& B) { return !B.IsAlive(GetWorld()); });
    return Result;
}

FBSSelection UBSEditModeComponent::HoverSelection(UFGBuildGunStateDismantle* State)
{
    FBSSelection Result;
    // Read the completed native trace. Cancelling SetAimedAtActor prevents the
    // native tick from establishing a valid target in the first place.
    auto* Building = Cast<AFGBuildable>(State->GetSelectedActor());
    // Already-pending buildings may have no separate native selected actor.
    // They are still valid targets for removing a unit from our selection.
    if (!Building && State->mPendingDismantleActors.Contains(State->mCurrentlyAimedAtActor))
        Building = Cast<AFGBuildable>(State->mCurrentlyAimedAtActor);
    if (!IsValid(Building)) return Result;
    const auto Mode = State->GetBuildGun()->GetCurrentBuildGunMode();
    auto* System = ABSGroupSubsystem::Get(GetWorld());
    const int32 Revision = System ? System->Revision : INDEX_NONE;
    if (!Hover.IsEmpty() && HoverActor == Building && HoverMode == Mode.Get() && HoverRevision == Revision) return Hover;
    HoverActor = Building;
    HoverMode = Mode.Get();
    HoverRevision = Revision;
    FBSBuildingRef Ref = FBSBuildingRef::FromActor(Building);
    if (!Ref.IsAlive(GetWorld())) return Result;
    if (Mode == UBSEditBlueprintModeDescriptor::StaticClass() || Mode == State->mBlueprintDismantleMode)
    {
        if (auto* Proxy = Ref.Blueprint(GetWorld()))
        {
            if (!Proxy->AreProxyBuildingsRegisteredAndValid())
            { Feedback = TEXT("Blueprint members are still loading."); return Result; }
            for (auto* B : Proxy->GetBuildables())
                if (IsValid(B)) Result.Buildings.AddUnique(FBSBuildingRef::FromActor(B));
            for (const auto& Bundle : Proxy->GetLightweightClassAndIndices())
                for (int32 Index : Bundle.Indices)
                    Result.Buildings.AddUnique(FBSBuildingRef::FromLightweight(GetWorld(), Bundle.BuildableClass, Index));
        }
        return Result;
    }
    if (Mode == UBSGroupModeDescriptor::StaticClass() || Mode == UBSEditGroupModeDescriptor::StaticClass())
    {
        if (System)
        {
            FGuid Root = FBSGroupTree::Root(System->Groups, FBSGroupTree::Owner(System->Groups, Ref));
            if (Root.IsValid()) Result.Groups.Add(Root);
        }
        return Result;
    }
    Result.Buildings.Add(Ref);
    return Result;
}

void UBSEditModeComponent::UpdateSelection(UFGBuildGunStateDismantle* State)
{
    if (!InputAllowed()) { State->SetAimedAtActor(nullptr); return; }
    auto* System = ABSGroupSubsystem::Get(GetWorld());
    if (System && ObservedRevision != System->Revision)
    {
        ObservedRevision = System->Revision;
        bSelectionDirty = true;
        Selection = FBSGroupTree::Normalize(System->Groups, Selection);
    }
    if (PendingRevision != INDEX_NONE && System && System->Revision >= PendingRevision)
    {
        if (bWaitingForTransformReplication)
        {
            TArray<FBSBuildingRef> Members = PendingResult.Buildings;
            for (const auto& Id : PendingResult.Groups) FBSGroupTree::Collect(System->Groups, Id, Members);
            for (const auto& Ref : Members) if (!Ref.IsAlive(GetWorld())) return;
            bWaitingForTransformReplication = false;
        }
        Selection = PendingResult;
        if (bTransforming)
        {
            const auto Members = ResolveSelection(Selection);
            TransformPivot = FVector::ZeroVector;
            for (const auto& Ref : Members) TransformPivot += Ref.Location();
            if (!Members.IsEmpty()) TransformPivot /= Members.Num();
        }
        PendingResult = {};
        PendingRevision = INDEX_NONE;
        bRequestPending = false;
        bSelectionDirty = true;
    }
    if (bWaitingForTransformReplication) return;
    if (Selection.Buildings.RemoveAll([&](const FBSBuildingRef& B) { return !B.IsAlive(GetWorld()); }) > 0) bSelectionDirty = true;
    if (bTransforming)
    {
        Hover = {};
        SyncDismantle(State);
        return;
    }
    Hover = HoverSelection(State);
    // This is driven by the game's existing multi-select input binding, including remaps.
    const bool Multi = State->mIsMultiSelectActive;
    if (Multi && !bRequestPending && !Hover.IsEmpty())
    {
        if (!bMultiHeld)
        {
            bRemoving = !Selection.IsEmpty();
            for (FGuid Id : Hover.Groups) bRemoving &= Selection.Groups.Contains(Id);
            for (const auto& B : Hover.Buildings) bRemoving &= Selection.Buildings.Contains(B);
        }
        if (!bMultiHeld || Hover.Groups != LastHover.Groups || Hover.Buildings != LastHover.Buildings)
        {
            if (bRemoving)
            {
                for (FGuid Id : Hover.Groups) Selection.Groups.Remove(Id);
                for (const auto& B : Hover.Buildings) Selection.Buildings.Remove(B);
            }
            else
            {
                for (FGuid Id : Hover.Groups) Selection.Groups.AddUnique(Id);
                for (const auto& B : Hover.Buildings) Selection.Buildings.AddUnique(B);
            }
            if (System) Selection = FBSGroupTree::Normalize(System->Groups, Selection);
            bSelectionDirty = true;
        }
    }
    if (Selection.IsEmpty() && (Hover.Groups != LastHover.Groups || Hover.Buildings != LastHover.Buildings)) bSelectionDirty = true;
    bMultiHeld = Multi;
    LastHover = Hover;
    SyncDismantle(State);
}

void UBSEditModeComponent::SyncDismantle(UFGBuildGunStateDismantle* State)
{
    // Ordinary single-building targeting belongs to vanilla. Only replace its
    // pending list when there is an explicit multi-selection or expanded mode.
    const auto Mode = State->GetBuildGun()->GetCurrentBuildGunMode();
    if (!IsEditing() && Selection.IsEmpty() && Mode == State->mDefaultDismantleMode)
    {
        ResolvedCount = Hover.Buildings.Num();
        if (!Highlighted.IsEmpty())
        {
            bSyncing = true;
            State->ClearPendingSelectedActors();
            ReleaseHighlights();
            bSyncing = false;
        }
        return;
    }
    // Vehicles and other non-building dismantlables keep the native single-target path.
    if (!IsEditing() && Selection.IsEmpty() && Hover.IsEmpty() &&
        IsValid(State->mCurrentlySelectedActor) && !State->mCurrentlySelectedActor->IsA<AFGBuildable>()) return;
    // A pinned selection never silently gains the unrelated building under the cursor.
    bSyncing = true;
    State->SetAimedAtActor(nullptr);
    bSyncing = false;
    bool Changed = bSelectionDirty || State->mPendingDismantleActors.Num() != ExpectedPendingCount;
    if (!Changed)
        for (AFGBuildable* B : Highlighted) if (!IsValid(B) || B->GetIsDismantled()) { Changed = true; break; }
    if (!Changed) return;
    TArray<FBSBuildingRef> Buildings = ResolveSelection(EffectiveSelection());
    ResolvedCount = Buildings.Num();
    if (Buildings.Num() > 2048)
    {
        Buildings.Empty();
        Feedback = TEXT("Selection exceeds 2048 buildings; nothing will be dismantled.");
        bSelectionDirty = true;
    }
    ++SelectionRebuilds;
    State->ResetBuildGunDelay();
    bSyncing = true;
    State->ClearPendingSelectedActors();
    ReleaseHighlights();
    State->mShouldRemoveFromMultiSelect = false;
    State->mCurrentMultiDismantleLimit = 2048;
    const bool WasMulti = State->mIsMultiSelectActive;
    State->mIsMultiSelectActive = true;
    for (const auto& Ref : Buildings)
    {
        if (auto* B = Ref.ResolveActor(GetWorld()))
        {
            if (B->GetIsLightweightTemporary()) B->SetBlockCleanupOfTemporary(true);
            Highlighted.Add(B);
            State->AddPendingDismantleActor(B);
        }
    }
    State->OnPendingDismantleActorsChanged.Broadcast();
    State->mIsMultiSelectActive = WasMulti;
    bSyncing = false;
    bSelectionDirty = false;
    ExpectedPendingCount = State->mPendingDismantleActors.Num();
    if (!IsEditing()) State->UpdatePeekDismantleRefunds(bRefundNoBuildCost);
}

void UBSEditModeComponent::ReleaseHighlights()
{
    for (AFGBuildable* B : Highlighted)
        if (IsValid(B) && B->GetIsLightweightTemporary()) B->SetBlockCleanupOfTemporary(false);
    Highlighted.Empty();
}

void UBSEditModeComponent::ClearSelection()
{
    Selection = {}; Hover = {}; LastHover = {}; PendingResult = {};
    HoverActor.Reset(); HoverMode.Reset(); HoverRevision = INDEX_NONE;
    ResolvedCount = 0;
    ExpectedPendingCount = 0;
    PendingRevision = INDEX_NONE;
    bWaitingForTransformReplication = false;
    bRequestPending = false; bMultiHeld = false; bSelectionDirty = true;
    ReleaseHighlights();
}

void UBSEditModeComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Type, Function);
    TickBlueprintSave();
    if (BlueprintSession || bOpeningBlueprint)
    {
        UpdateControlHints(false);
        if (Indicator) Indicator->SetVisibility(bOpeningBlueprint && InputAllowed() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        return;
    }
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* Gun = Player ? Player->GetBuildGun() : nullptr;
    auto* State = Gun && Player->IsBuildGunEquipped() ? Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState()) : nullptr;
    if (bEnteringEdit && EditGun.IsValid())
    {
        EditEntryTime += Delta;
        if (State && Gun == EditGun.Get())
        {
            Gun->SetCurrentBuildGunMode(UBSEditModeDescriptor::StaticClass());
            if (IsEditing())
            {
                bEnteringEdit = false;
                if (InputSubsystem.IsValid())
                {
                    auto* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/BuildingSkills/Inputs/MC_EditOperations.MC_EditOperations"));
                    if (Context)
                    {
                        OperationsContext = DuplicateObject<UInputMappingContext>(Context, this);
                        InputSubsystem->AddMappingContext(OperationsContext, 1000);
                    }
                }
                UE_LOG(LogBuildingSkills, Display, TEXT("Edit mode active: %s"), *GetNameSafe(Gun->GetCurrentBuildGunMode().Get()));
            }
        }
        if (bEnteringEdit && EditEntryTime > 5.0f)
        {
            UE_LOG(LogBuildingSkills, Warning, TEXT("Edit entry timed out in state %s, mode %s."),
                *GetNameSafe(Gun ? Gun->GetCurrentState() : nullptr), *GetNameSafe(Gun ? Gun->GetCurrentBuildGunMode().Get() : nullptr));
            LeaveEditMode(true);
            return;
        }
    }
    if (!bEnteringEdit && EditGun.IsValid() && (!State || !IsEditDescriptor(Gun->GetCurrentBuildGunMode()) ||
        !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead())) LeaveEditMode(false);
    if (!State)
    {
        if (ActiveState.IsValid()) { ClearSelection(); ActiveState.Reset(); }
        if (Indicator) { Indicator->RemoveFromParent(); Indicator = nullptr; }
        return;
    }
    ActiveState = State;
    if (!Indicator)
    {
        if (auto* PC = Cast<APlayerController>(Player->GetController()))
        {
            Indicator = CreateWidget<UBSEditModeWidget>(PC, UBSEditModeWidget::StaticClass());
            if (Indicator) { Indicator->ForceVolatile(true); Indicator->AddToPlayerScreen(); }
        }
    }
    TickTransform();
    UpdateNativeGunHUD(IsEditing());
    UpdateControlHints(IsEditing() && InputAllowed());
    if (Indicator) Indicator->SetVisibility(InputAllowed() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UBSEditModeComponent::UpdateNativeGunHUD(bool Editing)
{
    if (!Editing)
    {
        if (NativeGunHUD.IsValid()) NativeGunHUD->SetVisibility(NativeGunHUDVisibility);
        NativeGunHUD.Reset();
        return;
    }
    if (!NativeGunHUD.IsValid())
        if (auto* Player = Cast<AFGCharacterPlayer>(GetOwner()))
            if (auto* UI = Player->GetGameUI())
                for (auto* Widget : UI->GetInteractWidgetStack())
                    if (Widget && Widget->GetClass()->GetFName() == TEXT("Widget_DismantleMode_C"))
                    {
                        NativeGunHUD = Widget;
                        NativeGunHUDVisibility = Widget->GetVisibility();
                        break;
                    }
    // Keep the native state/input bindings, but replace its dismantle-only UI.
    if (NativeGunHUD.IsValid()) NativeGunHUD->SetVisibility(ESlateVisibility::Collapsed);
}

void UBSEditModeComponent::LeaveEditMode(bool Unequip)
{
    CloseBlueprintSave(false);
    EndTransform();
    UpdateNativeGunHUD(false);
    UpdateControlHints(false);
    if (InputSubsystem.IsValid() && OperationsContext) InputSubsystem->RemoveMappingContext(OperationsContext);
    OperationsContext = nullptr;
    bEnteringEdit = false;
    if (auto* Player = Cast<AFGCharacterPlayer>(GetOwner()))
    {
        if (EditGun.IsValid() && Unequip) Player->UnequipBuildGun();
    }
    EditGun.Reset();
    ClearSelection();
}

void UBSEditModeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseInput();
    LeaveEditMode(false);
    if (Indicator) { Indicator->RemoveFromParent(); Indicator = nullptr; }
    Super::EndPlay(Reason);
}

void UBSEditModeComponent::ReleaseInput()
{
    if (InputSubsystem.IsValid())
    {
        if (SaveContext) InputSubsystem->RemoveMappingContext(SaveContext);
        if (TransformContext) InputSubsystem->RemoveMappingContext(TransformContext);
        if (ToggleContext) InputSubsystem->RemoveMappingContext(ToggleContext);
        if (OperationsContext) InputSubsystem->RemoveMappingContext(OperationsContext);
    }
    TransformContext = nullptr;
    SaveContext = nullptr;
    ToggleContext = nullptr;
    OperationsContext = nullptr;
    if (!BoundInput) return;
    if (auto* Player = Cast<AFGCharacterPlayer>(GetOwner()))
        if (auto* PC = Cast<APlayerController>(Player->GetController())) PC->PopInputComponent(BoundInput);
    BoundInput->ClearBindingsForObject(this);
    BoundInput->DestroyComponent();
    BoundInput = nullptr;
}

FText UBSEditModeComponent::GetStatusText() const
{
    if (bTransforming)
    {
        const FVector Steps = TransformSteps();
        return FText::FromString(FString::Printf(TEXT("EDIT  |  TRANSFORM\n%s  |  %d buildings\nMove: %g m  |  Rotate: %g°  |  Scale: ×%g%s%s"),
            *FBSTransformMath::Name(TransformMode), ResolvedCount, Steps.X / 100, Steps.Y, Steps.Z,
            Feedback.IsEmpty() ? TEXT("") : TEXT("\n"), *Feedback));
    }
    auto* State = ActiveState.Get();
    auto Mode = State ? State->GetBuildGun()->GetCurrentBuildGunMode() : nullptr;
    const FBSSelection Current = EffectiveSelection();
    FString ModeName = TEXT("Normal");
    if (Mode == UBSGroupModeDescriptor::StaticClass() || Mode == UBSEditGroupModeDescriptor::StaticClass()) ModeName = TEXT("Group");
    else if (Mode == UBSEditBlueprintModeDescriptor::StaticClass() || (State && Mode == State->mBlueprintDismantleMode)) ModeName = TEXT("Blueprint");
    return FText::FromString(FString::Printf(TEXT("%s  |  %s\nGroups: %d  |  Individuals: %d\nSelected buildings: %d%s%s"),
        IsEditing() ? TEXT("EDIT") : TEXT("DISMANTLE"), *ModeName, Current.Groups.Num(), Current.Buildings.Num(),
        ResolvedCount, Feedback.IsEmpty() ? TEXT("") : TEXT("\n"), *Feedback));
}

TArray<TPair<FText, FText>> UBSEditModeComponent::GetControlHints() const
{
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
    auto Key = [&](FName Name) { return UFGInputLibrary::GetInputActionNameAsText_Keyboard(PC, Name); };
    auto MappingName = [](const UInputAction* Action, const TCHAR* ContextPath) -> FName
    {
        // Vanilla actions put their rebindable name on the context mapping,
        // rather than on the input action asset itself.
        auto* Context = LoadObject<UInputMappingContext>(nullptr, ContextPath);
        if (Context && Action)
            for (const auto& Mapping : Context->GetMappings())
                if (Mapping.Action == Action && !Mapping.Key.IsGamepadKey()) return Mapping.GetMappingName();
        return NAME_None;
    };
    TArray<TPair<FText, FText>> Hints;
    if (bTransforming)
    {
        Hints.Emplace(FText::FromString(TEXT("Transform mode")), Key(MappingName(ModeAction, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/MC_Buildgun.MC_Buildgun"))));
        Hints.Emplace(FText::FromString(FBSTransformMath::Name(TransformMode)), FText::FromString(Key(TEXT("BuildingSkills.ScrollUp")).ToString() + TEXT(" / ") + Key(TEXT("BuildingSkills.ScrollDown")).ToString()));
        const FString Movement = Key(TEXT("BuildingSkills.MoveForward")).ToString() + TEXT(" / ") + Key(TEXT("BuildingSkills.MoveBackward")).ToString() + TEXT(" / ") + Key(TEXT("BuildingSkills.MoveLeft")).ToString() + TEXT(" / ") + Key(TEXT("BuildingSkills.MoveRight")).ToString();
        Hints.Emplace(FText::FromString(TEXT("Move")), FText::FromString(Movement));
        Hints.Emplace(FText::FromString(TEXT("Up / Down")), FText::FromString(Key(TEXT("BuildingSkills.MoveUp")).ToString() + TEXT(" / ") + Key(TEXT("BuildingSkills.MoveDown")).ToString()));
        for (const auto* Name : {TEXT("Tiny"), TEXT("Small"), TEXT("Large")})
            Hints.Emplace(FText::FromString(Name), Key(FName(FString(TEXT("BuildingSkills.")) + Name)));
        Hints.Emplace(FText::FromString(TEXT("Return to Edit")), FText::FromString(TEXT("Esc")));
        return Hints;
    }
    Hints.Emplace(LOCTEXT("MultiSelectHint", "Multi-select"),
        Key(MappingName(MultiSelectAction, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/Dismantle/MC_BuildGunDismantle.MC_BuildGunDismantle"))));
    Hints.Emplace(LOCTEXT("SelectionModeHint", "Selection mode"),
        Key(MappingName(ModeAction, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/MC_Buildgun.MC_Buildgun"))));
    if (IsEditing())
    {
        Hints.Emplace(LOCTEXT("TransformHint", "Transform"), Key(TEXT("BuildingSkills.Transform")));
        Hints.Emplace(LOCTEXT("GroupHint", "Group"), Key(TEXT("BuildingSkills.Group")));
        Hints.Emplace(LOCTEXT("MergeHint", "Merge"), Key(TEXT("BuildingSkills.Merge")));
        const FBSSelection Current = EffectiveSelection();
        if (Current.Groups.Num() == 1 && Current.Buildings.IsEmpty())
        {
            Hints.Emplace(LOCTEXT("UngroupHint", "Ungroup"), Key(TEXT("BuildingSkills.Ungroup")));
            Hints.Emplace(LOCTEXT("SaveHint", "Save"), Key(TEXT("BuildingSkills.Save")));
        }
        Hints.Emplace(LOCTEXT("CloseEditHint", "Close Edit"), Key(TEXT("BuildingSkills.ToggleEditMode")));
    }
    return Hints;
}

FText UBSEditModeComponent::GetControlsText() const
{
    FString Text;
    for (const auto& Hint : GetControlHints()) Text += Hint.Value.ToString() + TEXT(": ") + Hint.Key.ToString() + TEXT("  ");
    return FText::FromString(Text);
}

void UBSEditModeComponent::UpdateControlHints(bool Editing)
{
    if (!Editing)
    {
        if (ControlHints && Indicator) Indicator->SetHints(nullptr);
        ControlHints = nullptr;
        LastControlHints.Empty();
        return;
    }
    if (!ControlHints)
    {
        auto* Class = LoadClass<UUserWidget>(nullptr, TEXT("/Game/FactoryGame/Interface/UI/InGame/-Shared/KeybindingHints/Widget_HintContainer.Widget_HintContainer_C"));
        auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
        auto* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
        if (!Class || !PC) return;
        ControlHints = CreateWidget<UUserWidget>(PC, Class);
        // Embed equipment hints just as the native Build Gun does. Registering
        // this child widget directly with the viewport creates a separate UI layer.
        if (Indicator) Indicator->SetHints(ControlHints);
        if (ControlHints->WidgetTree)
        {
            // Use the keyboard hint branch. Edit currently exposes rebindable
            // keyboard commands only.
            if (auto* Gamepad = ControlHints->WidgetTree->FindWidget(TEXT("mButtonHintBar"))) Gamepad->SetVisibility(ESlateVisibility::Collapsed);
            if (auto* Wrap = Cast<UWrapBox>(ControlHints->WidgetTree->FindWidget(TEXT("mHintContainer"))))
            {
                Wrap->SetHorizontalAlignment(HAlign_Center);
                Wrap->SetExplicitWrapSize(false);
                if (auto* CanvasSlot = Cast<UCanvasPanelSlot>(Wrap->Slot))
                {
                    CanvasSlot->SetAnchors(FAnchors(0, 0, 1, 1));
                    CanvasSlot->SetOffsets(FMargin(0));
                    CanvasSlot->SetAlignment(FVector2D::ZeroVector);
                }
            }
        }
    }
    const auto Hints = GetControlHints();
    FString Fingerprint;
    for (const auto& Hint : Hints) Fingerprint += Hint.Key.ToString() + TEXT("|") + Hint.Value.ToString() + TEXT(";");
    if (Fingerprint == LastControlHints) return;
    auto* Function = ControlHints->FindFunction(TEXT("SetKeybindingHints"));
    if (!Function) return;
    // Use the vanilla equipment hint container and its public Blueprint function.
    // The scoped parameter storage also destroys its array and FTexts after dispatch.
    FStructOnScope Parameters(Function);
    for (TFieldIterator<FArrayProperty> It(Function); It; ++It)
    {
        if (!It->HasAnyPropertyFlags(CPF_Parm)) continue;
        auto* Inner = CastField<FStructProperty>(It->Inner);
        if (!Inner) continue;
        FTextProperty* Action = nullptr;
        FTextProperty* Key = nullptr;
        for (TFieldIterator<FTextProperty> Field(Inner->Struct); Field; ++Field)
        {
            if (Field->GetName().StartsWith(TEXT("Action_"))) Action = *Field;
            if (Field->GetName().StartsWith(TEXT("KeyBinding_"))) Key = *Field;
        }
        if (!Action || !Key) continue;
        FScriptArrayHelper Array(*It, It->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory()));
        for (const auto& Hint : Hints)
        {
            auto* Item = Array.GetRawPtr(Array.AddValue());
            Action->SetPropertyValue_InContainer(Item, Hint.Key);
            Key->SetPropertyValue_InContainer(Item, Hint.Value);
        }
        ControlHints->ProcessEvent(Function, Parameters.GetStructMemory());
        LastControlHints = Fingerprint;
        break;
    }
}

#undef LOCTEXT_NAMESPACE
