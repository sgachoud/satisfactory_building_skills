#include "BSEditModeComponent.h"
#include "BSBlueprintSave.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "Equipment/FGBuildGun.h"
#include "InputMappingContext.h"
#include "EnhancedInputSubsystems.h"
#include "Blueprint/WidgetTree.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"

void UBSEditModeComponent::OnSave()
{
    if (!IsEditing() || bTransforming || !InputAllowed() || bRequestPending || bOpeningBlueprint || BlueprintWindow) return;
    const auto Current = EffectiveSelection();
    if (Current.Groups.Num() != 1 || !Current.Buildings.IsEmpty()) return;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    auto* RCO = PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    if (!RCO || !Groups) return;
    bOpeningBlueprint = true;
    BlueprintSelection = Current;
    RCO->Server_OpenBlueprintSave(Current.Groups[0], Groups->Revision);
}

void UBSEditModeComponent::OpenBlueprintSave(ABSBlueprintSaveSession* Session, const FString& Error)
{
    bOpeningBlueprint = false;
    BlueprintSession = Session;
    if (!IsValid(Session) || !IsEditing()) { Feedback = Error; CloseBlueprintSave(); return; }
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* UI = Player ? Player->GetGameUI() : nullptr;
    auto* Class = LoadClass<UFGInteractWidget>(nullptr, TEXT("/Game/FactoryGame/Buildable/-Shared/Widgets/Widget_BlueprintDesigner.Widget_BlueprintDesigner_C"));
    if (!UI || !Class) { Feedback = TEXT("The blueprint interface is unavailable."); CloseBlueprintSave(); return; }
    UI->OnToggleInteractWidget(Class, Session);
    Selection = BlueprintSelection;
    bSelectionDirty = true;
    TArray<UFGInteractWidget*> Windows;
    UI->GetInteractWidgetsOfInteractObject(Session, Windows);
    for (auto* Window : Windows) if (Window->IsA(Class)) { BlueprintWindow = Window; break; }
    if (!BlueprintWindow) { Feedback = TEXT("The blueprint interface could not be opened."); CloseBlueprintSave(); }
    TickBlueprintSave();
}

void UBSEditModeComponent::TickBlueprintSave()
{
    const auto Current = EffectiveSelection();
    const bool Enabled = IsEditing() && !bTransforming && !bOpeningBlueprint && !BlueprintWindow && InputAllowed() && Current.Groups.Num() == 1 && Current.Buildings.IsEmpty();
    if (InputSubsystem.IsValid())
    {
        if (Enabled && !SaveContext)
            if (auto* Source = LoadObject<UInputMappingContext>(nullptr, TEXT("/BuildingSkills/Inputs/MC_Save.MC_Save")))
            { SaveContext = DuplicateObject<UInputMappingContext>(Source, this); InputSubsystem->AddMappingContext(SaveContext, 1000); }
        if (!Enabled && SaveContext) { InputSubsystem->RemoveMappingContext(SaveContext); SaveContext = nullptr; }
    }
    if (!BlueprintWindow) return;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* UI = Player ? Player->GetGameUI() : nullptr;
    if (!IsValid(BlueprintSession) || !UI || !UI->GetInteractWidgetStack().Contains(BlueprintWindow)) { CloseBlueprintSave(); return; }
    // Keep the original details/save/overwrite flow. The proxy additionally blocks
    // load and clear at the native methods, including controller shortcuts.
    if (BlueprintWindow->WidgetTree)
        for (const auto* Name : {TEXT("mClearDesingerButton"), TEXT("mLoadButton"), TEXT("mRemoveBlueprintButton")})
            if (auto* Widget = BlueprintWindow->WidgetTree->FindWidget(FName(Name)))
            { Widget->SetIsEnabled(false); Widget->SetVisibility(ESlateVisibility::Collapsed); }
}

void UBSEditModeComponent::CloseBlueprintSave(bool ResumeEdit)
{
    const bool HadSession = BlueprintSession != nullptr;
    bOpeningBlueprint = false;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    if (BlueprintWindow && Player && Player->GetGameUI()) Player->GetGameUI()->PopWidget(BlueprintWindow);
    BlueprintWindow = nullptr;
    if (BlueprintSession && PC)
        if (auto* RCO = PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>()) RCO->Server_CloseBlueprintSave(BlueprintSession);
    BlueprintSession = nullptr;
    if (SaveContext && InputSubsystem.IsValid()) InputSubsystem->RemoveMappingContext(SaveContext);
    SaveContext = nullptr;
    if (ResumeEdit && HadSession && Player && Player->CanEquipBuildGunForDismantle())
    {
        EditGun = Player->GetBuildGun();
        bEnteringEdit = true;
        EditEntryTime = 0;
        Player->HotKeyDismantle();
        Selection = BlueprintSelection;
        bSelectionDirty = true;
    }
    BlueprintSelection = {};
}
