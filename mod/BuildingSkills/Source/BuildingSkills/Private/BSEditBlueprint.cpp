#include "BSEditModeComponent.h"
#include "BSBlueprintSave.h"
#include "BuildingSkills.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "Equipment/FGBuildGun.h"
#include "InputMappingContext.h"
#include "EnhancedInputSubsystems.h"
#include "Blueprint/WidgetTree.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"
#include "EngineUtils.h"

void UBSEditModeComponent::OnSave()
{
    UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save input: editing=%d transform=%d allowed=%d pending=%d opening=%d window=%d"),
        IsEditing(), bTransforming, InputAllowed(), bRequestPending, bOpeningBlueprint, BlueprintWindow != nullptr);
    if (!IsEditing() || bTransforming || !InputAllowed() || bRequestPending || bOpeningBlueprint || BlueprintWindow) return;
    const auto Current = EffectiveSelection();
    UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save selection: groups=%d individuals=%d"), Current.Groups.Num(), Current.Buildings.Num());
    if (Current.Groups.Num() != 1 || !Current.Buildings.IsEmpty()) return;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    auto* RCO = PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    if (!RCO || !Groups) return;
    bOpeningBlueprint = true;
    BlueprintSelection = Current;
    BlueprintRequestId = FGuid::NewGuid();
    BlueprintRequestDeadline = FPlatformTime::Seconds() + 15;
    BlueprintNextSessionCheck = 0;
    Feedback = TEXT("Opening blueprint window...");
    RCO->Server_OpenBlueprintSave(Current.Groups[0], Groups->Revision, BlueprintRequestId);
}

void UBSEditModeComponent::OnBlueprintSaveReply(FGuid RequestId, const FString& Error)
{
    if (!bOpeningBlueprint || RequestId != BlueprintRequestId) return;
    if (!Error.IsEmpty()) { Feedback = Error; CloseBlueprintSave(); }
}

void UBSEditModeComponent::OpenBlueprintSave(ABSBlueprintSaveSession* Session, const FString& Error)
{
    UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save open: session=%s error=%s"), *GetNameSafe(Session), *Error);
    bOpeningBlueprint = false;
    BlueprintSession = Session;
    if (!IsValid(Session) || !IsEditing()) { Feedback = Error; CloseBlueprintSave(); return; }
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* UI = Player ? Player->GetGameUI() : nullptr;
    auto* Class = LoadClass<UFGInteractWidget>(nullptr, TEXT("/Game/FactoryGame/Buildable/-Shared/Widgets/Widget_BlueprintDesigner.Widget_BlueprintDesigner_C"));
    if (!UI || !Class) { Feedback = TEXT("The blueprint interface is unavailable."); CloseBlueprintSave(); return; }
    Feedback.Reset();
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
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    if (bOpeningBlueprint)
    {
        const double Now = FPlatformTime::Seconds();
        if (!Player || !IsEditing() || Now >= BlueprintRequestDeadline)
        {
            Feedback = TEXT("The server did not provide the blueprint window. Try Save again.");
            UE_LOG(LogBuildingSkills, Warning, TEXT("Blueprint Save session replication timed out or Edit closed."));
            CloseBlueprintSave();
        }
        else if (Now >= BlueprintNextSessionCheck && InputAllowed())
        {
            BlueprintNextSessionCheck = Now + .1;
            for (TActorIterator<ABSBlueprintSaveSession> It(GetWorld()); It; ++It)
                if (It->RequestId == BlueprintRequestId && It->GetOwner() == Player->GetController())
                { OpenBlueprintSave(*It, TEXT("")); break; }
        }
    }
    const auto Current = EffectiveSelection();
    // Keep consuming the configured Save key while the server/actor is in flight.
    // Otherwise a held S becomes backward movement after sending the request.
    const bool Enabled = IsEditing() && !bTransforming && !BlueprintWindow && InputAllowed() &&
        (bOpeningBlueprint || (Current.Groups.Num() == 1 && Current.Buildings.IsEmpty()));
    if (InputSubsystem.IsValid())
    {
        if (Enabled && !SaveContext)
            if (auto* Source = LoadObject<UInputMappingContext>(nullptr, TEXT("/BuildingSkills/Inputs/MC_Save.MC_Save")))
            { SaveContext = DuplicateObject<UInputMappingContext>(Source, this); InputSubsystem->AddMappingContext(SaveContext, 1000); }
        if (!Enabled && SaveContext) { InputSubsystem->RemoveMappingContext(SaveContext); SaveContext = nullptr; }
    }
    if (!BlueprintWindow) return;
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
    if (BlueprintRequestId.IsValid() && PC)
        if (auto* RCO = PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>()) RCO->Server_CloseBlueprintSave(BlueprintRequestId);
    BlueprintSession = nullptr;
    BlueprintRequestId.Invalidate();
    if (!ResumeEdit)
    {
        if (SaveContext && InputSubsystem.IsValid()) InputSubsystem->RemoveMappingContext(SaveContext);
        SaveContext = nullptr;
    }
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
