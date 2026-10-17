#include "FGOutlineComponent.h"
#include "BSEditModeComponent.h"
#include "BSEditModeWidget.h"
#include "BSTransformSettingsWidget.h"
#include "BSConnected.h"
#include "BSConnectedSettingsWidget.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildable.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "Equipment/FGBuildGunPaint.h"
#include "FGCustomizationRecipe.h"
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
#include "UObject/Stack.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Blueprint/WidgetTree.h"
#include "Components/WrapBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
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
UBSConnectedModeDescriptor::UBSConnectedModeDescriptor() { mDisplayName = LOCTEXT("ConnectedSelection", "Connected"); SetSelectionLimit(this); }
UBSEditConnectedModeDescriptor::UBSEditConnectedModeDescriptor() { mDisplayName = LOCTEXT("ConnectedSelection", "Connected"); }
UBSVolumeModeDescriptor::UBSVolumeModeDescriptor() { mDisplayName = LOCTEXT("VolumeSelection", "Volume"); SetSelectionLimit(this); }
UBSEditVolumeModeDescriptor::UBSEditVolumeModeDescriptor() { mDisplayName = LOCTEXT("VolumeSelection", "Volume"); }


UBSRotateXModeDescriptor::UBSRotateXModeDescriptor() { mDisplayName = LOCTEXT("RotateXMode", "Rotate X"); }
UBSRotateYModeDescriptor::UBSRotateYModeDescriptor() { mDisplayName = LOCTEXT("RotateYMode", "Rotate Y"); }
UBSRotateZModeDescriptor::UBSRotateZModeDescriptor() { mDisplayName = LOCTEXT("RotateZMode", "Rotate Z"); }
UBSScaleXYZModeDescriptor::UBSScaleXYZModeDescriptor() { mDisplayName = LOCTEXT("ScaleXYZMode", "Scale XYZ"); }
UBSScaleXYModeDescriptor::UBSScaleXYModeDescriptor() { mDisplayName = LOCTEXT("ScaleXYMode", "Scale XY"); }
UBSScaleXZModeDescriptor::UBSScaleXZModeDescriptor() { mDisplayName = LOCTEXT("ScaleXZMode", "Scale XZ"); }
UBSScaleYZModeDescriptor::UBSScaleYZModeDescriptor() { mDisplayName = LOCTEXT("ScaleYZMode", "Scale YZ"); }
UBSScaleXModeDescriptor::UBSScaleXModeDescriptor() { mDisplayName = LOCTEXT("ScaleXMode", "Scale X"); }
UBSScaleYModeDescriptor::UBSScaleYModeDescriptor() { mDisplayName = LOCTEXT("ScaleYMode", "Scale Y"); }
UBSScaleZModeDescriptor::UBSScaleZModeDescriptor() { mDisplayName = LOCTEXT("ScaleZMode", "Scale Z"); }
UBSSpreadDefaultModeDescriptor::UBSSpreadDefaultModeDescriptor() { mDisplayName = LOCTEXT("SpreadDefaultMode", "Default"); }
UBSSpreadConnectedModeDescriptor::UBSSpreadConnectedModeDescriptor() { mDisplayName = LOCTEXT("SpreadConnectedMode", "Connected"); }

namespace
{
    FNativeFuncPtr OriginalSupportedModesThunk = nullptr;
    TMap<TWeakObjectPtr<AFGBuildGun>, uint64> PendingRadialCommits;
    void TraceModeInput(const TCHAR* Event, AFGBuildGun* Gun)
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("BuildingSkillsRuntimeTest")))
            UE_LOG(LogBuildingSkills, Display, TEXT("MODE INPUT: %s state=%s waiting=%d"), Event, *GetNameSafe(Gun->GetCurrentState()), Gun->IsWaitingForBuildGunModeSelectUI());
    }
    TArray<TSubclassOf<UFGBuildGunModeDescriptor>> TransformRadialModes()
    {
        return {UBSRotateXModeDescriptor::StaticClass(), UBSRotateYModeDescriptor::StaticClass(), UBSRotateZModeDescriptor::StaticClass(), UBSScaleXYZModeDescriptor::StaticClass(), UBSScaleXYModeDescriptor::StaticClass(), UBSScaleXZModeDescriptor::StaticClass(), UBSScaleYZModeDescriptor::StaticClass(), UBSScaleXModeDescriptor::StaticClass(), UBSScaleYModeDescriptor::StaticClass(), UBSScaleZModeDescriptor::StaticClass()};
    }
}

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
    SUBSCRIBE_METHOD(AFGBuildGun::OnPrimaryFirePressed, [](auto& Scope, AFGBuildGun* Gun)
    {
        auto* Edit = ForGun(Gun);
        auto* Paint = Cast<UFGBuildGunStatePaint>(Gun->GetCurrentState());
        auto* Player=Gun->GetInstigatorCharacter();
        auto* PC=Player?Cast<AFGPlayerController>(Player->GetController()):nullptr;
        auto* RCO=PC?PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>():nullptr;
        if (!Edit || (!Edit->bConnectedPaint && (!RCO || RCO->Transformers.IsEmpty())) || !Paint || !Paint->GetActiveRecipe()) return;
        const auto Desc = UFGCustomizationRecipe::GetCustomizationDescriptor(Paint->GetActiveRecipe());
        if (!Desc || !(Desc->IsChildOf(UFGFactoryCustomizationDescriptor_Swatch::StaticClass()) || Desc->IsChildOf(UFGFactoryCustomizationDescriptor_Material::StaticClass()))) return;
        // Native gun input forwards Server_PrimaryFire before invoking the local
        // state. Spread/transformer paint sends one atomic request; suppress that first RPC
        // so a multiplayer seed cannot be painted once before the snapshot search.
        Scope.Cancel();
        if (Gun->mWaitingForPrimaryFireRelease || !Edit->InputAllowed()) return;
        Gun->mWaitingForPrimaryFireRelease = true;
        Paint->PrimaryFire();
    });
    // Native tracing still establishes the target, but our persistent selection
    // and separate hover layer own its outlines. Avoid creating/destroying a
    // temporary native hover proxy every frame beside a pinned selection.
    SUBSCRIBE_METHOD(UFGOutlineComponent::ShowOutline,
        [](auto& Scope, UFGOutlineComponent* Outline, AActor* Actor, EOutlineColor Color, bool DefaultProxies, bool OnlyProxies)
        {
            auto* Player = Cast<AFGCharacterPlayer>(Outline->GetOwner());
            auto* Edit = Player ? Player->FindComponentByClass<UBSEditModeComponent>() : nullptr;
            if (!Edit || !Edit->ActiveState.IsValid() || !Player->IsBuildGunEquipped() ||
                Player->GetBuildGun()->GetCurrentState() != Edit->ActiveState.Get()) return;
            if (Outline == Player->GetOutline() && Edit->bNativeTick && !Edit->Selection.IsEmpty()) { Scope.Cancel(); return; }
            if (Outline == Player->GetOutline() || Outline == Edit->HoverOutline)
            {
                const bool Editing = Edit->EditGun.IsValid(); // Native tick temporarily uses the default mode.
                if (Editing && Actor == Edit->BlueprintAnchorActor) { Scope.Cancel(); return; }
                Edit->UpdateEditColors(Editing);
                Scope(Outline, Actor, Editing ? EOutlineColor::OC_DISMANTLE : Color, DefaultProxies, OnlyProxies);
            }
        });
    // Preserve vanilla's configured tap/hold input and radial lifetime. Custom
    // choices are local tools, so selecting one must not replace the replicated
    // dismantle mode (which would discard Edit and its persistent selection).
    SUBSCRIBE_METHOD(AFGBuildGun::OnModeSelectPressed, [](auto& Scope, AFGBuildGun* Gun)
    {
        TraceModeInput(TEXT("Pressed"), Gun);
        for (auto It = PendingRadialCommits.CreateIterator(); It; ++It)
            if (!It.Key().IsValid() || It.Value() < GFrameCounter || It.Key() == Gun) It.RemoveCurrent();
        if (auto* Edit = ForGun(Gun); Edit && (Edit->bTransforming || Edit->IsSpreadPainting()) && !Edit->InputAllowed()) Scope.Cancel();
    });
    SUBSCRIBE_METHOD(AFGBuildGun::Input_ModeSelect, [](auto& Scope, AFGBuildGun* Gun, const FInputActionValue& Value)
    {
        TraceModeInput(Value.Get<bool>() ? TEXT("Action press") : TEXT("Action release"), Gun);
        auto* Edit = ForGun(Gun);
        if (!Edit || Value.Get<bool>() || !(Edit->bTransforming || Edit->IsSpreadPainting())) return;
        const bool RadialWasOpen = Gun->IsWaitingForBuildGunModeSelectUI();
        if (RadialWasOpen) PendingRadialCommits.Add(Gun, GFrameCounter + 4);
        const bool Tap = Gun->mBuildModeSelectHoldTime >= 0 && !RadialWasOpen;
        const auto PreviousTransform = Edit->TransformMode;
        const bool PreviousSpread = Edit->bConnectedPaint;
        // Native release closes a held radial. A short release has no native
        // local-tool mode to advance, so cycle once after the normal release.
        Scope(Gun, Value);
        if (Tap && Edit->TransformMode == PreviousTransform && Edit->bConnectedPaint == PreviousSpread)
        {
            if (FParse::Param(FCommandLine::Get(), TEXT("BuildingSkillsRuntimeTest")))
            {
                UE_LOG(LogBuildingSkills, Display, TEXT("MODE TAP: allowed=%d transforming=%d spread=%d"), Edit->InputAllowed(), Edit->bTransforming, Edit->bConnectedPaint);
                if (auto* Player = Cast<AFGCharacterPlayer>(Edit->GetOwner()))
                    if (auto* UI = Player->GetGameUI())
                        for (auto* Widget : UI->GetInteractWidgetStack())
                            if (Widget && (Widget->mDisableBuildGunActions || Widget->mUseKeyboard || Widget->mUseMouse))
                                UE_LOG(LogBuildingSkills, Display, TEXT("MODE TAP BLOCKER: %s"), *GetNameSafe(Widget));
            }
            if (Edit->bTransforming) Edit->CycleTransform(1); else Edit->ToggleConnectedPaint();
            if (FParse::Param(FCommandLine::Get(), TEXT("BuildingSkillsRuntimeTest")))
                UE_LOG(LogBuildingSkills, Display, TEXT("MODE TAP RESULT: transform=%d spread=%d"), int32(Edit->TransformMode), Edit->bConnectedPaint);
        }
    });
    // The base virtual implementation is empty and too short for a native
    // detour. Wrap its reflected thunk instead; both Blueprint UI calls and
    // the C++ event wrapper use this entry. Forward first to preserve overrides.
    if (auto* Function = UFGBuildGunState::StaticClass()->FindFunctionByName(TEXT("GetSupportedBuildModes")); Function && !OriginalSupportedModesThunk)
    {
        OriginalSupportedModesThunk = Function->GetNativeFunc();
        Function->SetNativeFunc([](UObject* Context, FFrame& Stack, void* Result)
        {
            OriginalSupportedModesThunk(Context, Stack, Result);
            auto* State = Cast<UFGBuildGunState>(Context);
            if (!State || !Stack.MostRecentPropertyAddress || !CastField<FArrayProperty>(Stack.MostRecentProperty)) return;
            auto& Modes = *reinterpret_cast<TArray<TSubclassOf<UFGBuildGunModeDescriptor>>*>(Stack.MostRecentPropertyAddress);
            if (auto* Edit = ForGun(State->GetBuildGun()))
            {
                if (Edit->bTransforming && State->IsA<UFGBuildGunStateDismantle>()) Modes = TransformRadialModes();
                else if (State->IsA<UFGBuildGunStatePaint>() && Edit->IsSpreadPainting())
                { Modes.Reset(); Modes.Add(UBSSpreadDefaultModeDescriptor::StaticClass()); Modes.Add(UBSSpreadConnectedModeDescriptor::StaticClass()); }
            }
        });
    }
    // Native callers can execute a Blueprint override without entering the base
    // reflected thunk. Supply the same local choices after that dispatch too.
    SUBSCRIBE_METHOD_AFTER(UFGBuildGunState::GetSupportedBuildModes, [](const UFGBuildGunState* State, TArray<TSubclassOf<UFGBuildGunModeDescriptor>>& Modes)
    {
        if (auto* Edit = ForGun(State->GetBuildGun()))
        {
            if (Edit->bTransforming && State->IsA<UFGBuildGunStateDismantle>()) Modes = TransformRadialModes();
            else if (State->IsA<UFGBuildGunStatePaint>() && Edit->IsSpreadPainting())
                { Modes.Reset(); Modes.Add(UBSSpreadDefaultModeDescriptor::StaticClass()); Modes.Add(UBSSpreadConnectedModeDescriptor::StaticClass()); }
        }
    });
    SUBSCRIBE_METHOD(AFGBuildGun::CycleBuildMode, [](auto& Scope, AFGBuildGun* Gun, int32 Delta)
    {
        TraceModeInput(TEXT("Cycle"), Gun);
        if (auto* Edit = ForGun(Gun))
        {
            if (Edit->bTransforming) { Edit->CycleTransform(Delta); Scope.Cancel(); }
            else if (Edit->IsSpreadPainting()) { if (Delta % 2) Edit->ToggleConnectedPaint(); Scope.Cancel(); }
        }
    });
    SUBSCRIBE_METHOD(AFGBuildGun::IsCurrentBuildGunMode, [](auto& Scope, const AFGBuildGun* Gun, TSubclassOf<UFGBuildGunModeDescriptor> Mode)
    {
        if (auto* Edit = ForGun(const_cast<AFGBuildGun*>(Gun)))
        {
            if (Edit->bTransforming) Scope.Override(Mode == TransformRadialModes()[int32(Edit->TransformMode)]);
            else if (Edit->IsSpreadPainting()) Scope.Override(Mode == (Edit->bConnectedPaint ? UBSSpreadConnectedModeDescriptor::StaticClass() : UBSSpreadDefaultModeDescriptor::StaticClass()));
        }
    });
    SUBSCRIBE_METHOD(AFGBuildGun::SetCurrentBuildGunMode, [](auto& Scope, AFGBuildGun* Gun, TSubclassOf<UFGBuildGunModeDescriptor> Mode)
    {
        const int32 TransformIndex = TransformRadialModes().IndexOfByKey(Mode);
        const bool Spread = Mode == UBSSpreadDefaultModeDescriptor::StaticClass() || Mode == UBSSpreadConnectedModeDescriptor::StaticClass();
        if (TransformIndex == INDEX_NONE && !Spread) return;
        Scope.Cancel();
        auto* Edit = ForGun(Gun);
        if (!Edit) return;
        // Native release dispatches a deferred selection as well as resetting
        // its waiting flag. Accept that one commit, but reject the automatic
        // first-item assignment also emitted after taps: local tools retain the
        // underlying replicated selection descriptor intentionally.
        uint64 CommitDeadline = 0;
        const bool PendingCommit = PendingRadialCommits.RemoveAndCopyValue(Gun, CommitDeadline) && CommitDeadline >= GFrameCounter;
        if (!Gun->IsWaitingForBuildGunModeSelectUI() && !PendingCommit) return;
        if (!((TransformIndex != INDEX_NONE && Edit->bTransforming) || (Spread && Edit->IsSpreadPainting()))) return;
        // Match native ordering: closing the radial may commit its hovered item.
        // Apply the requested choice last so that callback cannot overwrite it.
        Gun->mBuildModeSelectHoldTime = -1;
        Gun->mIsWaitingForSelectionUI = false;
        Gun->CloseBuildGunModeSelectUI();
        if (TransformIndex != INDEX_NONE)
        {
            Edit->TransformMode = EBSTransformMode(TransformIndex);
            Edit->TransformOptions.Gesture.Invalidate();
        }
        else
        {
            FBSConnectedPaint::ClearPreview(Edit);
            Edit->bConnectedPaint = Mode == UBSSpreadConnectedModeDescriptor::StaticClass();
            Edit->Feedback.Reset();
        }
    });
    SUBSCRIBE_METHOD(AFGBuildGun::Scroll, [](auto& Scope, AFGBuildGun* Gun, int32 Delta)
    { if (auto* Edit = ForGun(Gun)) { if (Edit->bTransforming) { Edit->ScrollTransform(Delta); Scope.Cancel(); } else if (Edit->IsVolumeSelection()) Scope.Cancel(); } });
    SUBSCRIBE_METHOD(UFGGameUI::Native_HandlePauseGamePressed, [](auto& Scope, UFGGameUI* UI)
    {
        auto* Pawn = UI->GetOwningPlayerPawn();
        auto* Edit = Pawn ? Pawn->FindComponentByClass<UBSEditModeComponent>() : nullptr;
        if (Edit && Edit->TransformersWindow)
        {
            const auto& Stack=UI->GetInteractWidgetStack();
            if(!Stack.IsEmpty() && Stack.Last()==Edit->TransformersWindow) { Edit->CloseTransformerSettings(); Scope.Cancel(); return; }
        }
        if (Edit && Edit->VolumeWindow)
        {
            const auto& Stack = UI->GetInteractWidgetStack();
            if (!Stack.IsEmpty() && Stack.Last() == Edit->VolumeWindow)
            { Edit->CloseVolumeSettings(); Scope.Cancel(); return; }
        }
        if (Edit && Edit->ConnectedWindow)
        {
            const auto& Stack = UI->GetInteractWidgetStack();
            if (!Stack.IsEmpty() && Stack.Last() == Edit->ConnectedWindow)
            { Edit->CloseConnectedSettings(); Scope.Cancel(); return; }
        }
        if (Edit && Edit->TransformSettingsWindow)
        {
            const auto& Stack = UI->GetInteractWidgetStack();
            if (!Stack.IsEmpty() && Stack.Last() == Edit->TransformSettingsWindow)
            { Edit->CloseTransformSettings(); Scope.Cancel(); return; }
        }
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
            // Blueprint radial queries dispatch the virtual implementation, not
            // the C++ event wrapper; supply choices on this path as well.
            if (Edit && Edit->bTransforming) { Modes = TransformRadialModes(); return; }
            if ((Edit && Edit->EditGun.IsValid()) || IsEditDescriptor(State->GetBuildGun()->GetCurrentBuildGunMode()))
            {
                Modes.Reset();
                Modes.Add(UBSEditModeDescriptor::StaticClass());
                Modes.Add(UBSEditBlueprintModeDescriptor::StaticClass());
                Modes.Add(UBSEditGroupModeDescriptor::StaticClass());
                Modes.Add(UBSEditConnectedModeDescriptor::StaticClass());
                Modes.Add(UBSEditVolumeModeDescriptor::StaticClass());
            }
            else
            {
                Modes.AddUnique(UBSGroupModeDescriptor::StaticClass());
                Modes.AddUnique(UBSConnectedModeDescriptor::StaticClass());
                Modes.AddUnique(UBSVolumeModeDescriptor::StaticClass());
                // A remote server copy must accept the client's first Edit mode
                // before that mode has become the replicated current value.
                if (!Edit && State->GetBuildGun()->HasAuthority())
                {
                    Modes.AddUnique(UBSEditModeDescriptor::StaticClass());
                    Modes.AddUnique(UBSEditBlueprintModeDescriptor::StaticClass());
                    Modes.AddUnique(UBSEditGroupModeDescriptor::StaticClass());
                    Modes.AddUnique(UBSEditConnectedModeDescriptor::StaticClass());
                    Modes.AddUnique(UBSEditVolumeModeDescriptor::StaticClass());
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
    SUBSCRIBE_UOBJECT_METHOD(AFGBuildable, Destroyed,
        [](auto&, AActor* Actor)
        { if (auto* Building=Cast<AFGBuildable>(Actor); Building && !Building->GetIsLightweightTemporary()) if (auto* Groups = ABSGroupSubsystem::Get(Building->GetWorld())) Groups->RemoveBuilding(Building); });
    // Destroyed above owns actor-group cleanup. Do not also hook Dismantle's
    // interface-adjusting thunk: shared hook chains can deliver the interface
    // subobject address instead of the actor when another mod hooks that thunk.
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
    Bind(TEXT("/BuildingSkills/Inputs/IA_AddTransformer.IA_AddTransformer"), &ThisClass::OnAddTransformer);
    Bind(TEXT("/BuildingSkills/Inputs/IA_TransformerSettings.IA_TransformerSettings"), &ThisClass::OnTransformerSettings);
    Bind(TEXT("/BuildingSkills/Inputs/IA_TransformSettings.IA_TransformSettings"), &ThisClass::OnTransformSettings);
    Bind(TEXT("/BuildingSkills/Inputs/IA_ConnectedSettings.IA_ConnectedSettings"), &ThisClass::OnConnectedSettings);
    Bind(TEXT("/BuildingSkills/Inputs/IA_VolumeShape.IA_VolumeShape"), &ThisClass::OnVolumeShape);
    Bind(TEXT("/BuildingSkills/Inputs/IA_VolumeSettings.IA_VolumeSettings"), &ThisClass::OnVolumeSettings);
    Bind(TEXT("/BuildingSkills/Inputs/IA_Deduplication.IA_Deduplication"), &ThisClass::OnDeduplication);
    Bind(TEXT("/BuildingSkills/Inputs/IA_StickToggle.IA_StickToggle"), &ThisClass::OnStick);
    Bind(TEXT("/BuildingSkills/Inputs/IA_StickSettings.IA_StickSettings"), &ThisClass::OnStickSettings);
    for (int32 Direction : {1,-1})
    {
        const FString Name=Direction>0 ? TEXT("StickScrollUp") : TEXT("StickScrollDown");
        if (auto* Action=LoadObject<UInputAction>(nullptr,*FString::Printf(TEXT("/BuildingSkills/Inputs/IA_%s.IA_%s"),*Name,*Name)))
            Input->BindAction(Action,ETriggerEvent::Started,this,&ThisClass::ScrollStick,Direction);
    }
    Bind(TEXT("/BuildingSkills/Inputs/IA_Pivot.IA_Pivot"), &ThisClass::OnPivot);
    Bind(TEXT("/BuildingSkills/Inputs/IA_AxisSpace.IA_AxisSpace"), &ThisClass::OnAxisSpace);
    Bind(TEXT("/BuildingSkills/Inputs/IA_Anchor.IA_Anchor"), &ThisClass::OnAnchor);
    Bind(TEXT("/BuildingSkills/Inputs/IA_ActionUndo.IA_ActionUndo"), &ThisClass::OnActionUndo);
    Bind(TEXT("/BuildingSkills/Inputs/IA_Undo.IA_Undo"), &ThisClass::OnUndo);
    Bind(TEXT("/BuildingSkills/Inputs/IA_Redo.IA_Redo"), &ThisClass::OnRedo);
    for (int32 Direction : {1, -1})
    {
        const TCHAR* Name = Direction > 0 ? TEXT("ScrollUp") : TEXT("ScrollDown");
        const FString Path = FString::Printf(TEXT("/BuildingSkills/Inputs/IA_%s.IA_%s"), Name, Name);
        if (auto* Action = LoadObject<UInputAction>(nullptr, *Path))
            Input->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::ScrollTransform, Direction);
        const FString VolumePath = FString::Printf(TEXT("/BuildingSkills/Inputs/IA_Volume%s.IA_Volume%s"), Name, Name);
        if (auto* Action = LoadObject<UInputAction>(nullptr, *VolumePath))
            Input->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::ScrollVolume, Direction);
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
    Bind(TEXT("/BuildingSkills/Inputs/IA_Copy.IA_Copy"), &ThisClass::OnCopy);
    Bind(TEXT("/BuildingSkills/Inputs/IA_BlueprintAnchor.IA_BlueprintAnchor"), &ThisClass::OnBlueprintAnchor);
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
    if (!IsEditing() || bTransforming || !InputAllowed() || bRequestPending || bOpeningBlueprint || bCopyPending || BlueprintWindow) return;
    auto* System = ABSGroupSubsystem::Get(GetWorld());
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    auto* RCO = PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    if (!System || !RCO) { Feedback = TEXT("Grouping is not ready yet."); return; }
    const FBSSelection Selected = EffectiveSelection();
    if (Operation == EBSGroupOperation::Ungroup && (Selected.Groups.Num() != 1 || !Selected.Buildings.IsEmpty())) return;
    if (Selected.IsEmpty()) { Feedback = TEXT("Select buildings or groups first."); return; }
    bRequestPending = true;
    GroupRequestDeadline = FPlatformTime::Seconds() + 30;
    RCO->RequestSelection(uint8(Operation), Selected, FGuid::NewGuid(), System->Revision);
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
    else { bRequestPending = false; GroupRequestDeadline = 0; }
}

FBSSelection UBSEditModeComponent::EffectiveSelection() const
{
    // Selection is normalized when it changes or the replicated tree changes.
    // HUD reads must not rescan every group for every selected building.
    // With an anchor, inclusion is explicit: aiming at the anchor must not
    // silently turn an orientation reference into saved blueprint content.
    return Selection.IsEmpty() && !(BlueprintAnchor.Actor || BlueprintAnchor.LightweightClass) ? Hover : Selection;
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
    if (IsVolumeSelection())
    {
        HoverActor = Cast<AFGBuildable>(State->mCurrentlyAimedAtActor);
        return VolumeSelection();
    }
    FBSSelection Result;
    const auto Mode = State->GetBuildGun()->GetCurrentBuildGunMode();
    const bool BlueprintMode = Mode == UBSEditBlueprintModeDescriptor::StaticClass() || Mode == State->mBlueprintDismantleMode;
    auto ExpandBlueprint = [&](AFGBlueprintProxy* Proxy)
    {
        FBSSelection Expanded;
        if (!IsValid(Proxy)) return Expanded;
        if (!Proxy->AreProxyBuildingsRegisteredAndValid())
        { Feedback = TEXT("Blueprint members are still loading."); return Expanded; }
        for (auto* Building : Proxy->GetBuildables())
            if (IsValid(Building)) Expanded.Buildings.AddUnique(FBSBuildingRef::FromActor(Building));
        for (const auto& Bundle : Proxy->GetLightweightClassAndIndices())
            for (int32 Index : Bundle.Indices)
                Expanded.Buildings.AddUnique(FBSBuildingRef::FromLightweight(GetWorld(), Bundle.BuildableClass, Index));
        return Expanded;
    };
    // Native blueprint targeting may return the proxy instead of the traced
    // building. A proxy is an AActor, not an AFGBuildable.
    if (BlueprintMode)
        if (auto* Proxy = Cast<AFGBlueprintProxy>(State->GetSelectedActor())) return ExpandBlueprint(Proxy);
    // Read the completed native trace. Cancelling SetAimedAtActor prevents the
    // native tick from establishing a valid target in the first place.
    auto* Building = Cast<AFGBuildable>(State->GetSelectedActor());
    // Already-pending buildings may have no separate native selected actor.
    // They are still valid targets for removing a unit from our selection.
    if (!Building && State->mPendingDismantleActors.Contains(State->mCurrentlyAimedAtActor))
        Building = Cast<AFGBuildable>(State->mCurrentlyAimedAtActor);
    if (!IsValid(Building)) { HoverActor.Reset(); return Result; }
    auto* System = ABSGroupSubsystem::Get(GetWorld());
    const int32 Revision = System ? System->Revision : INDEX_NONE;
    if (!Hover.IsEmpty() && HoverActor == Building && HoverMode == Mode.Get() && HoverRevision == Revision &&
        (!IsConnectedSelection() || GetWorld()->GetRealTimeSeconds() < NextConnectedRefresh)) return Hover;
    HoverActor = Building;
    HoverMode = Mode.Get();
    HoverRevision = Revision;
    FBSBuildingRef Ref = FBSBuildingRef::FromActor(Building);
    if (!Ref.IsAlive(GetWorld())) return Result;
    if (IsConnectedSelection())
    {
        Result.Buildings = FBSConnected::Find(GetWorld(), Ref, ConnectedSettings, Feedback);
        NextConnectedRefresh = GetWorld()->GetRealTimeSeconds() + 1.0;
        return Result;
    }
    if (BlueprintMode) return ExpandBlueprint(Ref.Blueprint(GetWorld()));
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
    if (!InputAllowed()) { State->SetAimedAtActor(nullptr); Hover = {}; UpdateHoverHighlights(); return; }
    auto* System = ABSGroupSubsystem::Get(GetWorld());
    if (System && ObservedRevision != System->Revision)
    {
        ObservedRevision = System->Revision;
        bSelectionDirty = true;
        Selection = FBSGroupTree::Normalize(System->Groups, Selection);
    }
    if (bWaitingForTransformReplication && GetWorld()->GetRealTimeSeconds() >= TransformReplicationDeadline)
    {
        // An overlapping edit can supersede our target before its reply arrives.
        // Discard only local selection/preview state; never revert newer world data.
        EndTransform();
        ClearSelection();
        Feedback = TEXT("The selection changed or its update was delayed. Reselect buildings to continue.");
    }
    if (PendingRevision != INDEX_NONE && System && System->Revision >= PendingRevision)
    {
        if (bWaitingForTransformReplication)
        {
            TArray<FBSBuildingRef> Members = PendingResult.Buildings;
            for (const auto& Id : PendingResult.Groups) FBSGroupTree::Collect(System->Groups, Id, Members);
            for (const auto& Ref : Members) if (!Ref.IsAlive(GetWorld())) return;
            for (const auto& Ref : AwaitedTransforms)
                if (!Ref.IsAlive(GetWorld()) || (Ref.Actor && !Ref.Actor->GetActorTransform().Equals(Ref.Transform, .01))) return;
            AwaitedTransforms.Reset();
            bWaitingForTransformReplication = false;
        }
        Selection = PendingResult;
        const bool KeepPivot = InFlightTransform.IsSet();
        if (KeepPivot) TransformPivot += InFlightTransform->Translation;
        InFlightTransform.Reset();
        if (bTransforming) RefreshTransformFrame(KeepPivot);
        PendingResult = {};
        PendingRevision = INDEX_NONE;
        bRequestPending = false;
        GroupRequestDeadline = 0;
        bSelectionDirty = true;
    }
    if (bWaitingForTransformReplication) return;
    if (Selection.Buildings.RemoveAll([&](const FBSBuildingRef& B) { return !B.IsAlive(GetWorld()); }) > 0) bSelectionDirty = true;
    if (bTransforming)
    {
        Hover = {};
        UpdateHoverHighlights();
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
    UpdateHoverHighlights();
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
        if (IsValid(B) && B->GetIsLightweightTemporary() && B != BlueprintAnchorActor && !HoverHighlighted.Contains(B)) B->SetBlockCleanupOfTemporary(false);
    Highlighted.Empty();
}

void UBSEditModeComponent::ClearSelection()
{
    ReleaseBlueprintAnchor();
    BlueprintAnchor = {};
    Selection = {}; Hover = {}; LastHover = {}; PendingResult = {};
    AwaitedTransforms.Reset();
    HoverActor.Reset(); HoverMode.Reset(); HoverRevision = INDEX_NONE;
    ResolvedCount = 0;
    ExpectedPendingCount = 0;
    PendingRevision = INDEX_NONE;
    bWaitingForTransformReplication = false;
    bRequestPending = false; GroupRequestDeadline = 0; bMultiHeld = false; bSelectionDirty = true;
    ReleaseHighlights();
    UpdateHoverHighlights();
}

void UBSEditModeComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    if (!bRequestedTransformSnapshot && GetNetMode() == NM_Client && ABSGroupSubsystem::Get(GetWorld()))
        if (auto* Player = Cast<AFGCharacterPlayer>(GetOwner()))
            if (auto* PC = Cast<AFGPlayerController>(Player->GetController()))
                if (auto* RCO = PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>())
                { RCO->Server_TransformSnapshot(); bRequestedTransformSnapshot = true; }

    if (GetNetMode() == NM_Client)
        if (auto* OwnerPlayer = Cast<AFGCharacterPlayer>(GetOwner()))
            if (auto* OwnerController = Cast<AFGPlayerController>(OwnerPlayer->GetController()))
                if (auto* Remote = OwnerController->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>()) Remote->TickGroupTransfer();

    Super::TickComponent(Delta, Type, Function);
    if (bRequestPending && GroupRequestDeadline > 0 && FPlatformTime::Seconds() >= GroupRequestDeadline)
    {
        bRequestPending = false; PendingResult = {}; PendingRevision = INDEX_NONE;
        GroupRequestDeadline = 0;
        Feedback = TEXT("The group request timed out. Your selection is retained; try again.");
    }
    TickActionHistory();
    TickTransformers();
    TickConnected();
    TickVolume();
    TickDeduplication();
    TickStick();
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
    if (!State && ActiveState.IsValid()) { ClearSelection(); ActiveState.Reset(); }
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
    if (!State && !IsPainting() && !IsBlueprintBuilding() && !FBSStick::Active(this))
    {
        if (ActiveState.IsValid()) { ClearSelection(); ActiveState.Reset(); }
        // The hint container is embedded in the indicator's Slate tree. Retaining
        // it after removing that tree leaves a detached cached container when
        // Connected Dismantle or Paint is equipped again.
        UpdateControlHints(false);
        UpdateNativeGunHUD(false);
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
    UpdateBlueprintAnchor();
    UpdateEditColors(IsEditing());
    UpdateNativeGunHUD(IsEditing() && !Gun->IsWaitingForBuildGunModeSelectUI());
    UpdateControlHints((IsEditing() || IsConnectedSelection() || IsVolumeSelection() || IsSpreadPainting() || IsBlueprintBuilding() || FBSStick::Active(this)) && !Gun->IsWaitingForBuildGunModeSelectUI() && InputAllowed());
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
    bCopyPending = false;
    UpdateEditColors(false);
    CloseBlueprintSave(false);
    EndTransform();
    // Closing a focused numeric field can commit its text. Reset after popping it.
    TemporaryTransformSteps.Reset();
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
    FBSConnectedPaint::ClearPreview(this);
    CloseConnectedSettings();
    CloseVolumeSettings();
    CloseStickSettings();
    CloseTransformerSettings();
    ClearTransformerVisuals();
    ReleaseInput();
    LeaveEditMode(false);
    if (Indicator) { Indicator->RemoveFromParent(); Indicator = nullptr; }
    Super::EndPlay(Reason);
}

void UBSEditModeComponent::ReleaseInput()
{
    if (InputSubsystem.IsValid())
    {
        if (TransformersContext) InputSubsystem->RemoveMappingContext(TransformersContext);
        if (AddTransformerContext) InputSubsystem->RemoveMappingContext(AddTransformerContext);
        if (SaveContext) InputSubsystem->RemoveMappingContext(SaveContext);
        if (ActionHistoryContext) InputSubsystem->RemoveMappingContext(ActionHistoryContext);
        if (TransformContext) InputSubsystem->RemoveMappingContext(TransformContext);
        if (ToggleContext) InputSubsystem->RemoveMappingContext(ToggleContext);
        if (OperationsContext) InputSubsystem->RemoveMappingContext(OperationsContext);
        if (ConnectedContext) InputSubsystem->RemoveMappingContext(ConnectedContext);
        if (VolumeContext) InputSubsystem->RemoveMappingContext(VolumeContext);
        if (DeduplicationContext) InputSubsystem->RemoveMappingContext(DeduplicationContext);
        if (StickContext) InputSubsystem->RemoveMappingContext(StickContext);
        if (StickToggleContext) InputSubsystem->RemoveMappingContext(StickToggleContext);
    }
    TransformersContext = nullptr; AddTransformerContext = nullptr;
    ActionHistoryContext = nullptr;
    if (ActionHistoryHint) ActionHistoryHint->RemoveFromParent();
    ActionHistoryHint = nullptr;
    TransformContext = nullptr;
    ConnectedContext = nullptr;
    VolumeContext = nullptr;
    DeduplicationContext = nullptr;
    StickContext = nullptr;
    StickToggleContext = nullptr;
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
    if (FBSStick::Active(const_cast<UBSEditModeComponent*>(this)))
        return FText::FromString(FString::Printf(TEXT("Stick: %s | %.2f m | Vertical: %s | Horizontal: %s%s%s"),
            StickMode==EBSStickMode::Off ? TEXT("Off") : StickMode==EBSStickMode::Default ? TEXT("Default") : TEXT("Constrained"),
            StickDistance,*(StickTiltSet ? FString::Printf(TEXT("%+.1f deg"),StickTilt) : FString(TEXT("Auto"))),*(StickYawSet ? FString::Printf(TEXT("%+.1f deg"),StickYaw) : FString(TEXT("Auto"))),StickStatus.IsEmpty() ? TEXT("") : TEXT("\n"),*StickStatus));
    if (IsBlueprintBuilding()) return FText::FromString(FString::Printf(TEXT("Deduplication: %s"), Deduplication == EBSDeduplication::Replace ? TEXT("Replace") : Deduplication == EBSDeduplication::Skip ? TEXT("Skip") : TEXT("Off")));
    if (IsPainting()) return FText::FromString(FString::Printf(TEXT("Spread: %s%s%s"), bConnectedPaint ? TEXT("Connected") : TEXT("Default"), Feedback.IsEmpty() ? TEXT("") : TEXT("\n"), *Feedback));
    if (bTransforming)
    {
        const FVector Steps = TransformSteps();
        return FText::FromString(FString::Printf(TEXT("EDIT  |  TRANSFORM\n%s  |  %d buildings\nMove: %g m  |  Rotate: %g°  |  Scale: ×%g\n%s axes | %s pivot | Reference %d%s%s%s"),
            *FBSTransformMath::Name(TransformMode), ResolvedCount, Steps.X / 100, Steps.Y, Steps.Z,
            TransformOptions.Space == EBSTransformSpace::World ? TEXT("World") : TransformOptions.Space == EBSTransformSpace::Anchor ? TEXT("Anchor") : TEXT("Local"),
            TransformOptions.Pivot == EBSTransformPivot::Bounds ? TEXT("Bounds center") : TransformOptions.Pivot == EBSTransformPivot::Anchor ? TEXT("Reference") : TransformOptions.Pivot == EBSTransformPivot::Individual ? TEXT("Individual") : TEXT("Origins center"), TransformOptions.AnchorIndex + 1, *TransformPoseText(),
            Feedback.IsEmpty() ? TEXT("") : TEXT("\n"), *Feedback));
    }
    auto* State = ActiveState.Get();
    auto Mode = State ? State->GetBuildGun()->GetCurrentBuildGunMode() : nullptr;
    const FBSSelection Current = EffectiveSelection();
    FString ModeName = TEXT("Normal");
    if (IsConnectedSelection()) ModeName = TEXT("Connected");
    if (IsVolumeSelection()) ModeName = Volume.Cube ? TEXT("Volume: Cube") : TEXT("Volume: Sphere");
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
    if (FBSStick::Active(const_cast<UBSEditModeComponent*>(this)))
    {
        Hints.Emplace(FText::FromString(TEXT("Toggle stick")),Key(TEXT("BuildingSkills.StickToggle")));
        if (StickMode!=EBSStickMode::Off)
        {
            Hints.Emplace(FText::FromString(TEXT("Increase distance")),Key(TEXT("BuildingSkills.StickScrollUp")));
            Hints.Emplace(FText::FromString(TEXT("Decrease distance")),Key(TEXT("BuildingSkills.StickScrollDown")));
            Hints.Emplace(FText::FromString(TEXT("Vertical tilt (hold + scroll)")),Key(TEXT("BuildingSkills.StickTilt")));
            Hints.Emplace(FText::FromString(TEXT("Horizontal tilt (hold + scroll)")),Key(TEXT("BuildingSkills.StickHorizontalTilt")));
            Hints.Emplace(FText::FromString(TEXT("Stick settings")),Key(TEXT("BuildingSkills.StickSettings")));
        }
        return Hints;
    }
    if (IsBlueprintBuilding())
    {
        // The scoped action is shown by the native Build Gun hint list.
        // Do not duplicate it in the supplemental Edit/selection row.
        return Hints;
    }
    if (IsPainting())
    {
        if (IsSpreadPainting())
        {
            Hints.Emplace(LOCTEXT("ToggleSpreadHint", "Toggle spread"), Key(MappingName(ModeAction, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/MC_Buildgun.MC_Buildgun"))));
            if (bConnectedPaint) if(!IsEditing()) Hints.Emplace(FText::FromString(TEXT("Connected settings")), Key(TEXT("BuildingSkills.ConnectedSettings")));
        }
        return Hints;
    }
    if (IsConnectedSelection() && !bTransforming)
        if(!IsEditing()) Hints.Emplace(FText::FromString(TEXT("Connected settings")), Key(TEXT("BuildingSkills.ConnectedSettings")));
    if (IsVolumeSelection())
    {
        Hints.Emplace(FText::FromString(Volume.Cube ? TEXT("Shape: Cube") : TEXT("Shape: Sphere")), Key(TEXT("BuildingSkills.VolumeShape")));
        Hints.Emplace(FText::FromString(TEXT("Resize volume")), FText::FromString(TEXT("Scroll")));
        if (Volume.Cube)
        {
            Hints.Emplace(FText::FromString(TEXT("Opposite face")), Key(TEXT("BuildingSkills.VolumeOpposite")));
            Hints.Emplace(FText::FromString(TEXT("Rotate + scroll")), Key(TEXT("BuildingSkills.VolumeRotate")));
        }
        if(!IsEditing()) Hints.Emplace(FText::FromString(TEXT("Volume settings")), Key(TEXT("BuildingSkills.VolumeSettings")));
        if (!IsEditing()) Hints.Emplace(FText::FromString(TEXT("Set / clear anchor")), Key(TEXT("BuildingSkills.BlueprintAnchor")));
    }
    if ((IsConnectedSelection() || IsVolumeSelection()) && !IsEditing()) return Hints; // Native dismantle already supplies its other hints.
    if (bTransforming)
    {
        Hints.Emplace(FText::FromString(TEXT("Transform mode")), Key(MappingName(ModeAction, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/MC_Buildgun.MC_Buildgun"))));
        Hints.Emplace(FText::FromString(FBSTransformMath::Name(TransformMode)), FText::FromString(Key(TEXT("BuildingSkills.ScrollUp")).ToString() + TEXT(" / ") + Key(TEXT("BuildingSkills.ScrollDown")).ToString()));
        const FString Movement = Key(TEXT("BuildingSkills.MoveForward")).ToString() + TEXT(" / ") + Key(TEXT("BuildingSkills.MoveBackward")).ToString() + TEXT(" / ") + Key(TEXT("BuildingSkills.MoveLeft")).ToString() + TEXT(" / ") + Key(TEXT("BuildingSkills.MoveRight")).ToString();
        Hints.Emplace(FText::FromString(TEXT("Move")), FText::FromString(Movement));
        Hints.Emplace(FText::FromString(TEXT("Up / Down")), FText::FromString(Key(TEXT("BuildingSkills.MoveUp")).ToString() + TEXT(" / ") + Key(TEXT("BuildingSkills.MoveDown")).ToString()));
        for (const auto* Name : {TEXT("Tiny"), TEXT("Small"), TEXT("Large")})
            Hints.Emplace(FText::FromString(FString(Name) == TEXT("Anchor") ? TEXT("Reference") : Name), Key(FName(FString(TEXT("BuildingSkills.")) + Name)));
        for (const auto* Name : {TEXT("Pivot"), TEXT("AxisSpace"), TEXT("Anchor"), TEXT("Undo"), TEXT("Redo")})
            Hints.Emplace(FText::FromString(FString(Name) == TEXT("Anchor") ? TEXT("Reference") : Name), Key(FName(FString(TEXT("BuildingSkills.")) + Name)));
        Hints.Emplace(FText::FromString(TEXT("Step settings")), Key(TEXT("BuildingSkills.TransformSettings")));
        Hints.Emplace(FText::FromString(TEXT("Return to Edit")), FText::FromString(TEXT("Esc")));
        return Hints;
    }
    Hints.Emplace(LOCTEXT("MultiSelectHint", "Multi-select"),
        Key(MappingName(MultiSelectAction, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/Dismantle/MC_BuildGunDismantle.MC_BuildGunDismantle"))));
    Hints.Emplace(LOCTEXT("SelectionModeHint", "Selection mode"),
        Key(MappingName(ModeAction, TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/MC_Buildgun.MC_Buildgun"))));
    if (IsEditing())
    {
        Hints.Emplace(FText::FromString(TEXT("Undo")), Key(TEXT("BuildingSkills.Undo")));
        Hints.Emplace(FText::FromString(TEXT("Redo")), Key(TEXT("BuildingSkills.Redo")));
        Hints.Emplace(LOCTEXT("TransformHint", "Transform"), Key(TEXT("BuildingSkills.Transform")));
        Hints.Emplace(LOCTEXT("GroupHint", "Group"), Key(TEXT("BuildingSkills.Group")));
        Hints.Emplace(LOCTEXT("MergeHint", "Merge"), Key(TEXT("BuildingSkills.Merge")));
        Hints.Emplace(FText::FromString(TEXT("Edit settings")), Key(TEXT("BuildingSkills.TransformerSettings")));
        if(BlueprintAnchor.IsAlive(GetWorld())) Hints.Emplace(FText::FromString(TEXT("Add transformer")),Key(TEXT("BuildingSkills.AddTransformer")));
        Hints.Emplace(LOCTEXT("BlueprintAnchorHint", "Set / clear blueprint anchor"), Key(TEXT("BuildingSkills.BlueprintAnchor")));
        const FBSSelection Current = EffectiveSelection();
        if (Current.Groups.Num() == 1 && Current.Buildings.IsEmpty())
        {
            Hints.Emplace(LOCTEXT("UngroupHint", "Ungroup"), Key(TEXT("BuildingSkills.Ungroup")));
        }
        if (!Current.IsEmpty())
        {
            Hints.Emplace(LOCTEXT("SaveHint", "Save"), Key(TEXT("BuildingSkills.Save")));
            Hints.Emplace(LOCTEXT("CopyHint", "Copy"), Key(TEXT("BuildingSkills.Copy")));
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
    if (Indicator) Indicator->UpdateSpreadMode(Editing && IsSpreadPainting(), GetStatusText());
    if (!Editing)
    {
        if (Indicator) Indicator->SetHints(nullptr);
        if (ControlHints) ControlHints->RemoveFromParent();
        ControlHints = nullptr;
        LastControlHints.Empty();
        return;
    }
    if (IsBlueprintBuilding())
    {
        // Keyboard Build hints are an explicit Blueprint list; input mapping
        // flags alone only update the separate controller hint bar. Append one
        // vanilla hint widget to the actual keyboard wrap panel instead.
        if (Indicator) Indicator->SetHints(nullptr);
        auto* HintClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/FactoryGame/Interface/UI/InGame/-Shared/KeybindingHints/Widget_Hint.Widget_Hint_C"));
        auto* ContainerClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/FactoryGame/Interface/UI/InGame/-Shared/KeybindingHints/Widget_HintContainer.Widget_HintContainer_C"));
        auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
        auto* UI = Player ? Player->GetGameUI() : nullptr;
        if (!HintClass || !ContainerClass || !UI) return;
        if (ControlHints && ControlHints->GetClass() != HintClass)
        { ControlHints->RemoveFromParent(); ControlHints = nullptr; }
        if (!ControlHints)
        { ControlHints = CreateWidget<UUserWidget>(UI->GetOwningPlayer(), HintClass); LastControlHints.Empty(); }
        UWrapBox* NativeWrap = nullptr;
        TSet<UUserWidget*> Visited;
        TFunction<void(UUserWidget*)> Find = [&](UUserWidget* Widget)
        {
            if (!Widget || !Widget->IsVisible() || !Widget->WidgetTree || Visited.Contains(Widget) || NativeWrap) return;
            Visited.Add(Widget);
            if (Widget->GetClass() == ContainerClass)
                NativeWrap = Cast<UWrapBox>(Widget->WidgetTree->FindWidget(TEXT("mHintContainer")));
            if (!NativeWrap) Widget->WidgetTree->ForEachWidget([&](UWidget* Child) { if (auto* Nested = Cast<UUserWidget>(Child)) Find(Nested); });
        };
        for (auto* Widget : UI->GetInteractWidgetStack()) Find(Widget);
        if (NativeWrap && ControlHints->GetParent() != NativeWrap)
        {
            NativeWrap->AddChildToWrapBox(ControlHints);
            // Native entries start in their hidden spawn state. The container
            // animates its own batch; entries appended later need the same call.
            if (auto* Function = ControlHints->FindFunction(TEXT("PlaySpawnAnim")))
            {
                FStructOnScope Parameters(Function);
                ControlHints->ProcessEvent(Function, Parameters.GetStructMemory());
            }
        }
        const FText Key = UFGInputLibrary::GetInputActionNameAsText_Keyboard(UI->GetOwningPlayer(), TEXT("BuildingSkills.Deduplication"));
        if (LastControlHints != Key.ToString())
        {
            auto SetText = [&](const TCHAR* Name, const FText& Text)
            {
                if (auto* Function = ControlHints->FindFunction(Name))
                {
                    FStructOnScope Parameters(Function);
                    for (TFieldIterator<FTextProperty> Field(Function); Field; ++Field)
                        if (Field->HasAnyPropertyFlags(CPF_Parm) && !Field->HasAnyPropertyFlags(CPF_ReturnParm))
                            Field->SetPropertyValue_InContainer(Parameters.GetStructMemory(), Text);
                    ControlHints->ProcessEvent(Function, Parameters.GetStructMemory());
                }
            };
            SetText(TEXT("SetDescriptionText"), LOCTEXT("DeduplicationHint", "Toggle deduplication"));
            SetText(TEXT("SetKeyText"), Key);
            LastControlHints = Key.ToString();
        }
        return;
    }
    // The blueprint hint lives in the native Build HUD, rather than our panel.
    if (ControlHints && ControlHints->GetClass()->GetName() == TEXT("Widget_Hint_C"))
    { ControlHints->RemoveFromParent(); ControlHints = nullptr; LastControlHints.Empty(); }
    if (!ControlHints)
    {
        auto* Class = LoadClass<UUserWidget>(nullptr, TEXT("/Game/FactoryGame/Interface/UI/InGame/-Shared/KeybindingHints/Widget_HintContainer.Widget_HintContainer_C"));
        auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
        auto* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
        if (!Class || !PC) return;
        ControlHints = CreateWidget<UUserWidget>(PC, Class);
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
    // Reattach even when the hint text has not changed: the indicator's Slate
    // tree can be rebuilt independently of this cached UUserWidget.
    // Keep the native hint widget embedded, rather than adding a viewport layer.
    if (Indicator) Indicator->SetHints(ControlHints);
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

// Hover is visual only: never feed it into the pinned dismantle list.
void UBSEditModeComponent::UpdateHoverHighlights()
{
    // An external anchor deliberately suppresses implicit Save/Copy contents.
    // Volume still needs a visible preview before the first Ctrl selection.
    FBSSelection Desired = !bTransforming && (!Selection.IsEmpty() || IsVolumeSelection()) ? Hover : FBSSelection{};
    const auto* System = ABSGroupSubsystem::Get(GetWorld());
    const int32 Revision = System ? System->Revision : INDEX_NONE;
    // Group identity can stay the same while another player's edit changes its members.
    if (Revision == OutlinedRevision && Desired.Groups == OutlinedHover.Groups && Desired.Buildings == OutlinedHover.Buildings && Selection.Groups == OutlinedSelection.Groups && Selection.Buildings == OutlinedSelection.Buildings) return;
    OutlinedRevision = Revision;
    if (HoverOutline)
        for (AFGBuildable* Actor : HoverHighlighted)
            if (IsValid(Actor))
            {
                HoverOutline->HideOutline(Actor);
                if (Actor->GetIsLightweightTemporary() && Actor != BlueprintAnchorActor && !Highlighted.Contains(Actor)) Actor->SetBlockCleanupOfTemporary(false);
            }
    HoverHighlighted.Reset();
    OutlinedHover = Desired;
    OutlinedSelection = Selection;
    if (Desired.IsEmpty()) return;
    if (!HoverOutline)
    {
        HoverOutline = NewObject<UFGOutlineComponent>(GetOwner());
        HoverOutline->RegisterComponent();
    }
    for (const auto& Ref : ResolveSelection(Desired))
        if (auto* Actor = Ref.ResolveActor(GetWorld()); Actor && !Highlighted.Contains(Actor))
        {
            if (Actor->GetIsLightweightTemporary()) Actor->SetBlockCleanupOfTemporary(true);
            HoverHighlighted.Add(Actor);
            HoverOutline->ShowOutline(Actor, EOutlineColor::OC_DISMANTLE);
        }
}

void UBSEditModeComponent::UpdateEditColors(bool Editing)
{
    if (Editing == bEditColorsApplied || !GetWorld()) return;
    auto* Collection = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/FactoryGame/PostProcess/Materials/MPC_Hologram.MPC_Hologram"));
    auto* Instance = Collection ? GetWorld()->GetParameterCollectionInstance(Collection) : nullptr;
    if (!Instance) return;
    const FName Parameter(TEXT("DismantleHologramColour"));
    if (Editing && !Instance->GetVectorParameterValue(Parameter, PreviousDismantleColor)) return;
    Instance->SetVectorParameterValue(Parameter, Editing ? FLinearColor(.1f, 1.f, .2f) : PreviousDismantleColor);
    bEditColorsApplied = Editing;
}
