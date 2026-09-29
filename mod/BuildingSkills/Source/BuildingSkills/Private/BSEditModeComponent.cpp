#include "BSEditModeComponent.h"
#include "BSEditModeWidget.h"
#include "BuildingSkills.h"
#include "EnhancedInputComponent.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGCharacterPlayer.h"
#include "FGHealthComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMESPACE "BuildingSkills"

UBSEditModeDescriptor::UBSEditModeDescriptor()
{
    mDisplayName = LOCTEXT("EditModeName", "Edit");
}

UBSEditModeComponent::UBSEditModeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UBSEditModeComponent::InitializeInput(UEnhancedInputComponent* Input)
{
    if (BoundInput.IsValid()) BoundInput->ClearBindingsForObject(this);
    LeaveEditMode(false);
    bToggleHeld = false;
    BoundInput = Input;
    ToggleAction = LoadObject<UInputAction>(nullptr,
        TEXT("/BuildingSkills/Inputs/IA_ToggleEditMode.IA_ToggleEditMode"));
    if (!ToggleAction)
    {
        UE_LOG(LogBuildingSkills, Error, TEXT("Edit mode input asset is missing."));
        return;
    }

    // The chord stays triggered while held, so it continues blocking plain E.
    // Latch the callback instead of adding a one-frame Pressed trigger to the asset.
    Input->BindAction(ToggleAction, ETriggerEvent::Triggered, this, &ThisClass::OnToggleTriggered);
    Input->BindAction(ToggleAction, ETriggerEvent::Completed, this, &ThisClass::OnToggleReleased);
    Input->BindAction(ToggleAction, ETriggerEvent::Canceled, this, &ThisClass::OnToggleReleased);
}

void UBSEditModeComponent::OnToggleTriggered()
{
    if (bToggleHeld) return;
    bToggleHeld = true;

    AFGCharacterPlayer* Player = Cast<AFGCharacterPlayer>(GetOwner());
    if (!Player || !Player->IsLocallyControlled() || Player->IsDrivingVehicle() ||
        Player->IsInPortal() || Player->IsInteractWidgetOpen() ||
        UGameplayStatics::IsGamePaused(this) ||
        !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead()) return;

    if (EditGun.IsValid())
    {
        LeaveEditMode(true);
        return;
    }
    if (!Player->CanEquipBuildGun()) return;

    AFGBuildGun* Gun = Player->GetBuildGun();
    if (!IsValid(Gun)) return;
    PreviousMode = Gun->GetCurrentBuildGunMode();
    if (!Player->IsBuildGunEquipped()) Player->EquipBuildGunAndGoToMenuState();
    if (!Player->IsBuildGunEquipped()) return;

    // Null is explicitly supported by this API and removes the active hologram.
    // Existing build/dismantle/paint states and saved recipes remain owned by the game.
    Gun->GotoBuildState(nullptr);
    Gun->SetCurrentBuildGunMode(UBSEditModeDescriptor::StaticClass());
    EditGun = Gun;
    SetComponentTickEnabled(true);

    if (APlayerController* Controller = Cast<APlayerController>(Player->GetController()))
    {
        Indicator = CreateWidget<UBSEditModeWidget>(Controller, UBSEditModeWidget::StaticClass());
        if (Indicator)
        {
            Indicator->SetVisibility(ESlateVisibility::HitTestInvisible);
            Indicator->AddToPlayerScreen();
        }
    }
    UE_LOG(LogBuildingSkills, Display, TEXT("Edit mode opened."));
}

void UBSEditModeComponent::OnToggleReleased()
{
    bToggleHeld = false;
}

void UBSEditModeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    const AFGCharacterPlayer* Player = Cast<AFGCharacterPlayer>(GetOwner());
    AFGBuildGun* Gun = EditGun.Get();
    const UFGBuildGunStateBuild* Build = Gun ? Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState()) : nullptr;
    if (!Player || !Player->IsLocallyControlled() || !Player->IsBuildGunEquipped() ||
        Player->IsDrivingVehicle() || Player->IsInPortal() || !Player->GetHealthComponent() ||
        Player->GetHealthComponent()->IsDead() || !Build || Build->GetActiveRecipe() ||
        Gun->GetCurrentBuildGunMode() != UBSEditModeDescriptor::StaticClass())
    {
        LeaveEditMode(false);
        return;
    }
    if (Indicator)
    {
        Indicator->SetVisibility(Player->IsInteractWidgetOpen() || UGameplayStatics::IsGamePaused(this)
            ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }
}

void UBSEditModeComponent::LeaveEditMode(bool bUnequip)
{
    if (Indicator)
    {
        Indicator->RemoveFromParent();
        Indicator = nullptr;
    }
    if (AFGBuildGun* Gun = EditGun.Get())
    {
        // Do not overwrite a newer mode chosen by the player or by another system.
        if (Gun->GetCurrentBuildGunMode() == UBSEditModeDescriptor::StaticClass())
            Gun->SetCurrentBuildGunMode(PreviousMode);
        if (bUnequip)
        {
            if (AFGCharacterPlayer* Player = Cast<AFGCharacterPlayer>(GetOwner()))
                Player->UnequipBuildGun();
        }
        UE_LOG(LogBuildingSkills, Display, TEXT("Edit mode closed."));
    }
    EditGun.Reset();
    PreviousMode = nullptr;
    SetComponentTickEnabled(false);
}

void UBSEditModeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (BoundInput.IsValid()) BoundInput->ClearBindingsForObject(this);
    // Teardown only removes local presentation; do not send equipment RPCs here.
    EditGun.Reset();
    LeaveEditMode(false);
    Super::EndPlay(EndPlayReason);
}

#undef LOCTEXT_NAMESPACE
