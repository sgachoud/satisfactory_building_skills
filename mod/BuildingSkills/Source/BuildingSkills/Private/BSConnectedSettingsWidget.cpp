#include "BSConnectedSettingsWidget.h"
#include "BSEditModeComponent.h"
#include "BSConnected.h"
#include "FGCharacterPlayer.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunPaint.h"
#include "FGCustomizationRecipe.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "UI/FGGameUI.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Styling/CoreStyle.h"

bool UBSEditModeComponent::IsPainting() const
{
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    return Player && Player->IsBuildGunEquipped() && Cast<UFGBuildGunStatePaint>(Player->GetBuildGun()->GetCurrentState());
}
bool UBSEditModeComponent::IsSpreadPainting() const
{
    if (!IsPainting()) return false;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* Paint = Cast<UFGBuildGunStatePaint>(Player->GetBuildGun()->GetCurrentState());
    const auto Recipe = Paint->GetActiveRecipe();
    const auto Desc = Recipe ? UFGCustomizationRecipe::GetCustomizationDescriptor(Recipe) : nullptr;
    return Desc && (Desc->IsChildOf(UFGFactoryCustomizationDescriptor_Swatch::StaticClass()) ||
        Desc->IsChildOf(UFGFactoryCustomizationDescriptor_Material::StaticClass()));
}
bool UBSEditModeComponent::IsConnectedSelection() const
{
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* Gun = Player && Player->IsBuildGunEquipped() ? Player->GetBuildGun() : nullptr;
    if (!Gun || !Gun->IsInState(EBuildGunState::BGS_DISMANTLE)) return false;
    const auto Mode = Gun->GetCurrentBuildGunMode();
    return Mode == UBSConnectedModeDescriptor::StaticClass() || Mode == UBSEditConnectedModeDescriptor::StaticClass();
}
bool UBSEditModeComponent::ToggleConnectedPaint()
{
    if (!IsSpreadPainting()) return false;
    if (InputAllowed()) { FBSConnectedPaint::ClearPreview(this); bConnectedPaint = !bConnectedPaint; Feedback.Reset(); }
    return true;
}
void UBSEditModeComponent::TickConnected()
{
    const bool Active = !bTransforming && (IsConnectedSelection() || (IsSpreadPainting() && bConnectedPaint));
    // Paint's own tick also clears previews when neither Connected nor any
    // transformers are active. Default spread can now have transformer previews.
    if (!IsSpreadPainting() || !InputAllowed()) FBSConnectedPaint::ClearPreview(this);
    if (!Active && ConnectedWindow) CloseConnectedSettings();
    if (!InputSubsystem.IsValid()) return;
    if (Active && !ConnectedContext)
        if (auto* Source = LoadObject<UInputMappingContext>(nullptr, TEXT("/BuildingSkills/Inputs/MC_Connected.MC_Connected")))
        {
            ConnectedContext = DuplicateObject<UInputMappingContext>(Source, this);
            InputSubsystem->AddMappingContext(ConnectedContext, 1001);
        }
    if (!Active && ConnectedContext)
    { InputSubsystem->RemoveMappingContext(ConnectedContext); ConnectedContext = nullptr; }
}
void UBSEditModeComponent::OnConnectedSettings()
{
    if(IsEditing()) { OnTransformerSettings(); return; }
    OpenConnectedSettings();
}
void UBSEditModeComponent::OpenConnectedSettings()
{
    if (bTransforming || !InputAllowed() || !(IsConnectedSelection() || (IsSpreadPainting() && bConnectedPaint))) return;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* UI = Player ? Player->GetGameUI() : nullptr;
    if (!UI) return;
    FBSConnectedPaint::ClearPreview(this);
    ConnectedWindow = CreateWidget<UBSConnectedSettingsWidget>(UI->GetOwningPlayer());
    if (!ConnectedWindow) return;
    ConnectedWindow->Draft = ConnectedSettings;
    ConnectedWindow->mInteractObject = this;
    ConnectedWindow->SetDefaultFocusWidget(ConnectedWindow);
    UI->PushWidget(ConnectedWindow);
    ConnectedWindow->SetInputMode();
}
void UBSEditModeComponent::CloseConnectedSettings()
{
    FBSConnectedPaint::ClearPreview(this);
    auto* Window = ConnectedWindow.Get(); ConnectedWindow = nullptr;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    if (Window && Player && Player->GetGameUI()) Player->GetGameUI()->PopWidget(Window);
    HoverActor.Reset(); NextConnectedRefresh = 0;
}
UBSConnectedSettingsWidget::UBSConnectedSettingsWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
    mUseKeyboard = true; mUseMouse = true;
    mMouseLockMode = EMouseLockMode::DoNotLock; mHideCursorDuringCapture = false;
    mDisableBuildGunActions = true; mSupportsCaching = false; mSupportsStacking = true;
    SetDesiredHorizontalAlignment(HAlign_Center); SetDesiredVerticalAlignment(VAlign_Center); SetIsFocusable(true);
}
void UBSConnectedSettingsWidget::OnEscapePressed_Implementation()
{ if (auto* Edit = Cast<UBSEditModeComponent>(mInteractObject)) Edit->CloseConnectedSettings(); }
FReply UBSConnectedSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape) { OnEscapePressed_Implementation(); return FReply::Handled(); }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
FReply UBSConnectedSettingsWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape) { OnEscapePressed_Implementation(); return FReply::Handled(); }
    // Children receive key events first. Consume unhandled modal keys here so
    // AltGr combinations and hotbar keys cannot fall through to the world.
    return FReply::Handled();
}
TSharedRef<SWidget> UBSConnectedSettingsWidget::RebuildWidget()
{
    ScopeOptions.Reset();
    for (const TCHAR* Name : {TEXT("All"), TEXT("Category"), TEXT("Subcategory"), TEXT("Strict")}) ScopeOptions.Add(MakeShared<FString>(Name));
    auto Grid = SNew(SGridPanel);
    const auto Font = FCoreStyle::GetDefaultFontStyle("Regular", 13);
    const TCHAR* Labels[] = {TEXT("Gap (meters)"), TEXT("Ignore swatch"), TEXT("Ignore material"), TEXT("Matching scope"), TEXT("Object limit"), TEXT("Whitelist regex"), TEXT("Blacklist regex")};
    for (int32 I = 0; I < 7; ++I) Grid->AddSlot(0, I).Padding(8)[SNew(STextBlock).Font(Font).Text(FText::FromString(Labels[I]))];
    Grid->AddSlot(1, 0).Padding(8)[SNew(SNumericEntryBox<double>).AllowSpin(false).MinValue(0).MaxValue(1000).MinDesiredValueWidth(300)
        .Value_Lambda([this]() -> TOptional<double> { return Draft.Gap; }).OnValueChanged_Lambda([this](double Value) { Draft.Gap = Value; })];
    for (int32 I = 1; I <= 2; ++I)
        Grid->AddSlot(1, I).Padding(8)[SNew(SCheckBox).IsChecked_Lambda([this,I]() { return (I == 1 ? Draft.IgnoreSwatch : Draft.IgnoreMaterial) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this,I](ECheckBoxState State) { (I == 1 ? Draft.IgnoreSwatch : Draft.IgnoreMaterial) = State == ECheckBoxState::Checked; })];
    Grid->AddSlot(1, 3).Padding(8)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&ScopeOptions).InitiallySelectedItem(ScopeOptions[int32(Draft.Scope)])
        .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(FText::FromString(*Item)); })
        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type) { const int32 I = ScopeOptions.IndexOfByKey(Item); if (I >= 0) Draft.Scope = EBSConnectedScope(I); })
        [SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(*ScopeOptions[int32(Draft.Scope)]); })]];
    Grid->AddSlot(1, 4).Padding(8)[SNew(SNumericEntryBox<int32>).AllowSpin(false).MinValue(1).MaxValue(2048)
        .Value_Lambda([this]() -> TOptional<int32> { return Draft.Limit; }).OnValueChanged_Lambda([this](int32 Value) { Draft.Limit = Value; })];
    for (int32 I = 5; I <= 6; ++I)
        Grid->AddSlot(1, I).Padding(8)[SNew(SEditableTextBox).Font(Font).Text(FText::FromString(I == 5 ? Draft.Whitelist : Draft.Blacklist))
            .OnTextChanged_Lambda([this,I](const FText& Text) { (I == 5 ? Draft.Whitelist : Draft.Blacklist) = Text.ToString(); })];
    return SNew(SBox).WidthOverride(640)[SNew(SBorder).Padding(22).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.025f,.035f,.025f,.98f))[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",20)).Text(FText::FromString(TEXT("Connected Components")))]
        + SVerticalBox::Slot().AutoHeight()[Grid]
        + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Font(Font).AutoWrapText(true).Text(FText::FromString(TEXT("Shared by Edit, Dismantle and painting. Regex matches display names (case-sensitive; use (?i) to ignore case). Empty filters are unrestricted; blacklist wins. Excluded objects stop the search.")))]
        + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(1,.3,.2)).Text_Lambda([this]() { return FText::FromString(Error); })]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(8)[SNew(SButton).Text(FText::FromString(TEXT("Cancel"))).OnClicked_Lambda([this]() { OnEscapePressed_Implementation(); return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(8)[SNew(SButton).Text(FText::FromString(TEXT("Apply"))).OnClicked_Lambda([this]()
            { if (Draft.Validate(Error)) if (auto* Edit = Cast<UBSEditModeComponent>(mInteractObject)) { Edit->ConnectedSettings = Draft; Edit->ConnectedMessage(FString()); Edit->CloseConnectedSettings(); } return FReply::Handled(); })]
        ]]];
}
