#include "BSEditModeComponent.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGInputLibrary.h"
#include "Equipment/FGBuildGun.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/WrapBox.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

void UBSEditModeComponent::OnActionUndo()
{
    if(IsEditing() || !InputAllowed()) return;
    auto* Player=Cast<AFGCharacterPlayer>(GetOwner()); auto* Gun=Player?Player->GetBuildGun():nullptr;
    auto* PC=Player?Cast<AFGPlayerController>(Player->GetController()):nullptr;
    auto* RCO=PC?PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>():nullptr;
    if(!Gun || !Player->IsBuildGunEquipped() || !RCO) return;
    if(Gun->IsInState(EBuildGunState::BGS_BUILD) || Gun->IsInState(EBuildGunState::BGS_DISMANTLE))
        RCO->Server_UndoGunAction(Gun->IsInState(EBuildGunState::BGS_DISMANTLE));
}
void UBSEditModeComponent::TickActionHistory()
{
    auto* Player=Cast<AFGCharacterPlayer>(GetOwner()); auto* Gun=Player?Player->GetBuildGun():nullptr;
    const bool Active=Gun && Player->IsBuildGunEquipped() && !IsEditing() && InputAllowed() &&
        (Gun->IsInState(EBuildGunState::BGS_BUILD) || Gun->IsInState(EBuildGunState::BGS_DISMANTLE));
    if(!Active)
    {
        if(ActionHistoryContext && InputSubsystem.IsValid()) InputSubsystem->RemoveMappingContext(ActionHistoryContext);
        ActionHistoryContext=nullptr;
        if(ActionHistoryHint) ActionHistoryHint->RemoveFromParent();
        ActionHistoryHint=nullptr; LastActionHistoryHint.Empty(); return;
    }
    if(!ActionHistoryContext && InputSubsystem.IsValid())
        if(auto* Source=LoadObject<UInputMappingContext>(nullptr,TEXT("/BuildingSkills/Inputs/MC_ActionHistory.MC_ActionHistory")))
        { ActionHistoryContext=DuplicateObject<UInputMappingContext>(Source,this); InputSubsystem->AddMappingContext(ActionHistoryContext,1001); }
    const double Now=GetWorld()->GetRealTimeSeconds();
    if(Now<NextActionHistoryHintRefresh) return;
    NextActionHistoryHintRefresh=Now+.1;
    auto* UI=Player->GetGameUI(); if(!UI) return;
    auto* HintClass=LoadClass<UUserWidget>(nullptr,TEXT("/Game/FactoryGame/Interface/UI/InGame/-Shared/KeybindingHints/Widget_Hint.Widget_Hint_C"));
    if(!HintClass) return;
    if(!ActionHistoryHint) ActionHistoryHint=CreateWidget<UUserWidget>(UI->GetOwningPlayer(),HintClass);
    if(!ActionHistoryHint) return;
    UWrapBox* Wrap=nullptr; TSet<UUserWidget*> Visited;
    TFunction<void(UUserWidget*)> Find=[&](UUserWidget* Widget)
    {
        if(!Widget || !Widget->IsVisible() || !Widget->WidgetTree || Visited.Contains(Widget) || Wrap) return;
        Visited.Add(Widget);
        if(Widget->GetClass()->GetFName()==TEXT("Widget_HintContainer_C")) Wrap=Cast<UWrapBox>(Widget->WidgetTree->FindWidget(TEXT("mHintContainer")));
        if(!Wrap) Widget->WidgetTree->ForEachWidget([&](UWidget* Child){ if(auto* Nested=Cast<UUserWidget>(Child)) Find(Nested); });
    };
    for(auto* Widget:UI->GetInteractWidgetStack()) Find(Widget);
    if(Wrap && ActionHistoryHint->GetParent()!=Wrap)
    {
        Wrap->AddChildToWrapBox(ActionHistoryHint);
        if(auto* Fn=ActionHistoryHint->FindFunction(TEXT("PlaySpawnAnim"))) { FStructOnScope Args(Fn); ActionHistoryHint->ProcessEvent(Fn,Args.GetStructMemory()); }
    }
    const FText Key=UFGInputLibrary::GetInputActionNameAsText_Keyboard(UI->GetOwningPlayer(),TEXT("BuildingSkills.ActionUndo"));
    const FString Label=Gun->IsInState(EBuildGunState::BGS_DISMANTLE)?TEXT("Undo deletion"):TEXT("Undo construction");
    const FString Fingerprint=Label+Key.ToString(); if(LastActionHistoryHint==Fingerprint) return;
    auto Set=[&](const TCHAR* Name,const FText& Value)
    {
        if(auto* Fn=ActionHistoryHint->FindFunction(Name))
        {
            FStructOnScope Args(Fn);
            for(TFieldIterator<FTextProperty> P(Fn); P; ++P)
                if(P->HasAnyPropertyFlags(CPF_Parm) && !P->HasAnyPropertyFlags(CPF_ReturnParm)) P->SetPropertyValue_InContainer(Args.GetStructMemory(),Value);
            ActionHistoryHint->ProcessEvent(Fn,Args.GetStructMemory());
        }
    };
    Set(TEXT("SetDescriptionText"),FText::FromString(Label)); Set(TEXT("SetKeyText"),Key); LastActionHistoryHint=Fingerprint;
}
