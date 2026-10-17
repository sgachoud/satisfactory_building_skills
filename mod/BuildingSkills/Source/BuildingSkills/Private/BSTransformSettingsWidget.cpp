#include "BSTransformSettingsWidget.h"
#include "BSEditModeComponent.h"
#include "FGCharacterPlayer.h"
#include "UI/FGGameUI.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"

UBSTransformSettingsWidget::UBSTransformSettingsWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
    mUseKeyboard = true;
    mUseMouse = true;
    mMouseLockMode = EMouseLockMode::DoNotLock;
    mHideCursorDuringCapture = false;
    mDisableBuildGunActions = true;
    mSupportsCaching = false;
    mSupportsStacking = true;
    SetDesiredHorizontalAlignment(HAlign_Center);
    SetDesiredVerticalAlignment(VAlign_Center);
    SetIsFocusable(true);
}

void UBSEditModeComponent::OnTransformSettings()
{
    if (!IsEditing() || !bTransforming || !InputAllowed()) return;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* UI = Player ? Player->GetGameUI() : nullptr;
    if (!UI) return;
    // Do not replay a queued key repeat when the menu closes.
    TransformCommands.Reset();
    for (auto& Next : NextMove) Next = 0;
    // The world-interaction toggle unequips the Build Gun. Push a child window
    // directly so this is an editor overlay, retaining the Transform state.
    TransformSettingsWindow = CreateWidget<UBSTransformSettingsWidget>(UI->GetOwningPlayer(), UBSTransformSettingsWidget::StaticClass());
    if (!TransformSettingsWindow) return;
    TransformSettingsWindow->mInteractObject = this;
    TransformSettingsWindow->SetDefaultFocusWidget(TransformSettingsWindow);
    UI->PushWidget(TransformSettingsWindow);
    // PushWidget alone does not run the designer Blueprint's input setup. Apply
    // the native interact-widget mode explicitly: show the cursor, release game
    // mouse capture and focus this window. PopWidget restores the next UI mode.
    TransformSettingsWindow->SetInputMode();
}

void UBSEditModeComponent::CloseTransformSettings()
{
    auto* Window = TransformSettingsWindow.Get();
    TransformSettingsWindow = nullptr;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    if (Window && Player && Player->GetGameUI()) Player->GetGameUI()->PopWidget(Window);
}

void UBSTransformSettingsWidget::OnEscapePressed_Implementation()
{
    if (auto* Edit = Cast<UBSEditModeComponent>(mInteractObject)) Edit->CloseTransformSettings();
}
FReply UBSTransformSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape) { OnEscapePressed_Implementation(); return FReply::Handled(); }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
FReply UBSTransformSettingsWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    // With focus on the window itself (before a field is clicked), consume the
    // leaf key event too. The native widget's fallback otherwise passes Escape
    // through to the Build Gun.
    if (Event.GetKey() == EKeys::Escape) { OnEscapePressed_Implementation(); return FReply::Handled(); }
    // Children receive key events first. Consume unhandled modal keys here so
    // AltGr combinations and hotbar keys cannot fall through to the world.
    return FReply::Handled();
}

TSharedRef<SWidget> UBSTransformSettingsWidget::RebuildWidget()
{
    const auto Font = FCoreStyle::GetDefaultFontStyle("Regular", 13);
    auto Grid = SNew(SGridPanel);
    const TCHAR* Headings[] = {TEXT("Step"), TEXT("Distance (cm)"), TEXT("Rotation (degrees)"), TEXT("Scale (multiplier)")};
    for (int32 Column = 0; Column < 4; ++Column)
        Grid->AddSlot(Column, 0).Padding(8)[SNew(STextBlock).Font(Font).Text(FText::FromString(Headings[Column]))];
    const TCHAR* Names[] = {TEXT("Normal"), TEXT("Tiny"), TEXT("Small"), TEXT("Large")};
    for (int32 Tier = 0; Tier < 4; ++Tier)
    {
        Grid->AddSlot(0, Tier + 1).Padding(8)[SNew(STextBlock).Font(Font).Text(FText::FromString(Names[Tier]))];
        for (int32 Axis = 0; Axis < 3; ++Axis)
            Grid->AddSlot(Axis + 1, Tier + 1).Padding(8)
            [
                SNew(SNumericEntryBox<double>).Font(Font).AllowSpin(false).MinDesiredValueWidth(145)
                .MinValue(Axis == 2 ? 1.001 : .01).MaxValue(Axis == 0 ? 10000.0 : Axis == 1 ? 180.0 : 100.0)
                .MaxFractionalDigits(4)
                .Value_Lambda([this, Tier, Axis]() -> TOptional<double>
                {
                    auto* Edit = Cast<UBSEditModeComponent>(mInteractObject);
                    return Edit ? TOptional<double>(Edit->GetTransformStepValues()[Tier][Axis]) : TOptional<double>();
                })
                .OnValueCommitted_Lambda([this, Tier, Axis](double Value, ETextCommit::Type)
                { if (auto* Edit = Cast<UBSEditModeComponent>(mInteractObject)) Edit->SetTransformStepValue(Tier, Axis, Value); })
            ];
    }
    return SNew(SBox).WidthOverride(710)
    [SNew(SBorder).Padding(22).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.025f, .035f, .025f, .98f))
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)
            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 20)).ColorAndOpacity(FLinearColor(.1f,1.f,.2f)).Text(FText::FromString(TEXT("Transform step settings")))]
            + SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("Transformers"))).OnClicked_Lambda([this]()
                { if(auto* E=Cast<UBSEditModeComponent>(mInteractObject)){E->CloseTransformSettings();E->OnTransformerSettings();}return FReply::Handled(); })]
            + SVerticalBox::Slot().AutoHeight()[Grid]
            + SVerticalBox::Slot().AutoHeight().Padding(8,18)
            [SNew(STextBlock).Font(Font).AutoWrapText(true).Text(FText::FromString(TEXT("Temporary for this Edit session. Closing Edit restores your configured values.")))]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
            [SNew(SButton).Text(FText::FromString(TEXT("Close"))).OnClicked_Lambda([this]()
                { OnEscapePressed_Implementation(); return FReply::Handled(); })]
        ]
    ];
}
