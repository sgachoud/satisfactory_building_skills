#include "BSEditModeComponent.h"
#include "BSEditModeWidget.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildable.h"
#include "EnhancedInputComponent.h"
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
    // Editor FactoryGame methods are stubs. Hook real game binaries only.
    SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGBuildGunStateDismantle, GetSupportedBuildModes_Implementation,
        [](const UFGBuildGunStateDismantle* State, TArray<TSubclassOf<UFGBuildGunModeDescriptor>>& Modes)
        {
            if (IsEditDescriptor(State->GetBuildGun()->GetCurrentBuildGunMode()))
            {
                Modes.Reset();
                Modes.Add(UBSEditModeDescriptor::StaticClass());
                Modes.Add(UBSEditBlueprintModeDescriptor::StaticClass());
                Modes.Add(UBSEditGroupModeDescriptor::StaticClass());
            }
            else Modes.AddUnique(UBSGroupModeDescriptor::StaticClass());
        });
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle, OnBuildGunModeChanged_Implementation,
        [](auto& Scope, UFGBuildGunStateDismantle* State, TSubclassOf<UFGBuildGunModeDescriptor> Mode)
        {
            auto* Edit = ForGun(State->GetBuildGun());
            if (Edit) Edit->bChangingMode = true;
            Scope(State, Mode);
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
            Edit->TracedTarget.Reset();
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
    SUBSCRIBE_METHOD(UFGBuildGunStateDismantle::SetAimedAtActor,
        [](auto& Scope, UFGBuildGunStateDismantle* State, AActor* Target)
        {
            if (auto* Edit = ForGun(State->GetBuildGun()); Edit && Edit->bNativeTick && !Edit->bSyncing)
            {
                Edit->TracedTarget = Target;
                // Avoid single-target transitions resetting the dismantle timer every frame.
                if (Edit->EditGun.IsValid() || !Edit->Selection.IsEmpty() || !Target || Target->IsA<AFGBuildable>()) Scope.Cancel();
            }
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
    SUBSCRIBE_UOBJECT_METHOD(AFGBuildable, Dismantle_Implementation,
        [](auto&, AFGBuildable* Building)
        { if (auto* Groups = ABSGroupSubsystem::Get(Building->GetWorld())) Groups->RemoveBuilding(Building); });
    SUBSCRIBE_METHOD(UFGBuildGunStateDismantle::Server_DismantleActors_Implementation,
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
    if (BoundInput.IsValid()) BoundInput->ClearBindingsForObject(this);
    LeaveEditMode(false);
    BoundInput = Input;
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
    Bind(TEXT("/BuildingSkills/Inputs/IA_Group.IA_Group"), &ThisClass::OnGroup);
    Bind(TEXT("/BuildingSkills/Inputs/IA_Merge.IA_Merge"), &ThisClass::OnMerge);
    Bind(TEXT("/BuildingSkills/Inputs/IA_Ungroup.IA_Ungroup"), &ThisClass::OnUngroup);
}

bool UBSEditModeComponent::InputAllowed() const
{
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    return Player && Player->IsLocallyControlled() && !Player->IsDrivingVehicle() && !Player->IsInPortal() &&
        !Player->IsInteractWidgetOpen() && !UGameplayStatics::IsGamePaused(this) &&
        Player->GetHealthComponent() && !Player->GetHealthComponent()->IsDead();
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
    if (IsEditing()) { LeaveEditMode(true); return; }
    auto* Player = CastChecked<AFGCharacterPlayer>(GetOwner());
    if (!Player->CanEquipBuildGunForDismantle()) return;
    auto* Gun = Player->GetBuildGun();
    if (!IsValid(Gun)) return;
    ClearSelection();
    if (!Player->IsBuildGunEquipped()) Player->EquipBuildGunAndGoToMenuState();
    Gun->GotoDismantleState();
    Gun->SetCurrentBuildGunMode(UBSEditModeDescriptor::StaticClass());
    EditGun = Gun;
    if (auto* PC = Cast<AFGPlayerController>(Player->GetController()))
    {
        auto* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/BuildingSkills/Inputs/MC_EditRoot.MC_EditRoot"));
        if (Context) OperationsContext = PC->BindMappingContext(Context, 1000, this);
    }
    UE_LOG(LogBuildingSkills, Display, TEXT("Edit mode opened."));
}

void UBSEditModeComponent::OnToggleReleased() { bToggleHeld = false; }
void UBSEditModeComponent::OnGroup() { Request(EBSGroupOperation::Group); }
void UBSEditModeComponent::OnMerge() { Request(EBSGroupOperation::Merge); }
void UBSEditModeComponent::OnUngroup() { Request(EBSGroupOperation::Ungroup); }

void UBSEditModeComponent::Request(EBSGroupOperation Operation)
{
    if (!IsEditing() || !InputAllowed() || bRequestPending) return;
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
    auto* System = ABSGroupSubsystem::Get(GetWorld());
    const FBSSelection& Value = Selection.IsEmpty() ? Hover : Selection;
    return System ? FBSGroupTree::Normalize(System->Groups, Value) : Value;
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
    auto* Building = Cast<AFGBuildable>(TracedTarget.Get());
    if (!Building) Building = Cast<AFGBuildable>(State->mCurrentlyAimedAtActor);
    if (!Building) Building = Cast<AFGBuildable>(State->GetSelectedActor());
    if (!IsValid(Building) || !State->CanDismantleActor(Building)) return Result;
    FBSBuildingRef Ref = FBSBuildingRef::FromActor(Building);
    if (!Ref.IsAlive(GetWorld())) return Result;
    const auto Mode = State->GetBuildGun()->GetCurrentBuildGunMode();
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
        if (auto* System = ABSGroupSubsystem::Get(GetWorld()))
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
        Selection = PendingResult;
        PendingResult = {};
        PendingRevision = INDEX_NONE;
        bRequestPending = false;
        bSelectionDirty = true;
    }
    if (Selection.Buildings.RemoveAll([&](const FBSBuildingRef& B) { return !B.IsAlive(GetWorld()); }) > 0) bSelectionDirty = true;
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
    // Vehicles and other non-building dismantlables keep the native single-target path.
    if (!IsEditing() && Selection.IsEmpty() && Hover.IsEmpty() &&
        IsValid(State->mCurrentlySelectedActor) && !State->mCurrentlySelectedActor->IsA<AFGBuildable>()) return;
    // A pinned selection never silently gains the unrelated building under the cursor.
    State->SetAimedAtActor(nullptr);
    TArray<FBSBuildingRef> Buildings = ResolveSelection(EffectiveSelection());
    if (Buildings.Num() > 2048)
    {
        Buildings.Empty();
        Feedback = TEXT("Selection exceeds 2048 buildings; nothing will be dismantled.");
        bSelectionDirty = true;
    }
    bool Changed = bSelectionDirty || State->mPendingDismantleActors.Num() != Buildings.Num();
    if (!Changed)
        for (AFGBuildable* B : Highlighted) if (!IsValid(B) || B->GetIsDismantled()) { Changed = true; break; }
    if (!Changed) return;
    bSyncing = true;
    State->ResetBuildGunDelay();
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
    TracedTarget.Reset();
    PendingRevision = INDEX_NONE;
    bRequestPending = false; bMultiHeld = false; bSelectionDirty = true;
    ReleaseHighlights();
}

void UBSEditModeComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Type, Function);
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* Gun = Player ? Player->GetBuildGun() : nullptr;
    auto* State = Gun && Player->IsBuildGunEquipped() ? Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState()) : nullptr;
    if (EditGun.IsValid() && (!State || !IsEditDescriptor(Gun->GetCurrentBuildGunMode()) ||
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
            if (Indicator) Indicator->AddToPlayerScreen();
        }
    }
    if (Indicator) Indicator->SetVisibility(InputAllowed() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UBSEditModeComponent::LeaveEditMode(bool Unequip)
{
    if (auto* Player = Cast<AFGCharacterPlayer>(GetOwner()))
    {
        if (auto* PC = Cast<AFGPlayerController>(Player->GetController())) PC->UnbindAllMappingContextsByObject(this);
        if (EditGun.IsValid() && Unequip) Player->UnequipBuildGun();
    }
    EditGun.Reset();
    ClearSelection();
}

void UBSEditModeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (BoundInput.IsValid()) BoundInput->ClearBindingsForObject(this);
    LeaveEditMode(false);
    if (Indicator) { Indicator->RemoveFromParent(); Indicator = nullptr; }
    Super::EndPlay(Reason);
}

FText UBSEditModeComponent::GetStatusText() const
{
    auto* State = ActiveState.Get();
    auto Mode = State ? State->GetBuildGun()->GetCurrentBuildGunMode() : nullptr;
    const FBSSelection Current = EffectiveSelection();
    FString ModeName = TEXT("Normal");
    if (Mode == UBSGroupModeDescriptor::StaticClass() || Mode == UBSEditGroupModeDescriptor::StaticClass()) ModeName = TEXT("Group");
    else if (Mode == UBSEditBlueprintModeDescriptor::StaticClass() || (State && Mode == State->mBlueprintDismantleMode)) ModeName = TEXT("Blueprint");
    return FText::FromString(FString::Printf(TEXT("%s  |  %s\n%d groups, %d individual buildings  (%d buildings total)%s%s"),
        IsEditing() ? TEXT("EDIT") : TEXT("DISMANTLE"), *ModeName, Current.Groups.Num(), Current.Buildings.Num(),
        ResolveSelection(Current).Num(), Feedback.IsEmpty() ? TEXT("") : TEXT("\n"), *Feedback));
}

FText UBSEditModeComponent::GetControlsText() const
{
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
    auto Key = [&](FName Name) { return UFGInputLibrary::GetInputActionNameAsText(PC, Name).ToString(); };
    auto MappingName = [](const UInputAction* Action)
    { const UPlayerMappableKeySettings* Settings = Action ? Action->GetPlayerMappableKeySettings().Get() : nullptr; return Settings ? Settings->Name : NAME_None; };
    FString Text = FString::Printf(TEXT("%s: Multi-select    %s: Selection mode"),
        *Key(MappingName(MultiSelectAction)), *Key(MappingName(ModeAction)));
    if (IsEditing())
    {
        Text += FString::Printf(TEXT("\n%s: Group    %s: Merge"), *Key(TEXT("BuildingSkills.Group")), *Key(TEXT("BuildingSkills.Merge")));
        const FBSSelection Current = EffectiveSelection();
        if (Current.Groups.Num() == 1 && Current.Buildings.IsEmpty()) Text += FString::Printf(TEXT("    %s: Ungroup"), *Key(TEXT("BuildingSkills.Ungroup")));
        Text += FString::Printf(TEXT("\n%s: Close"), *Key(TEXT("BuildingSkills.ToggleEditMode")));
    }
    return FText::FromString(Text);
}

#undef LOCTEXT_NAMESPACE
