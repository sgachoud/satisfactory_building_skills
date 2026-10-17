#include "BSVolume.h"
#include "BSEditModeComponent.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"

UBSVolumeSettingsWidget::UBSVolumeSettingsWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
    mUseKeyboard = true; mUseMouse = true;
    mMouseLockMode = EMouseLockMode::DoNotLock; mHideCursorDuringCapture = false;
    mDisableBuildGunActions = true; mSupportsCaching = false; mSupportsStacking = true;
    SetDesiredHorizontalAlignment(HAlign_Center); SetDesiredVerticalAlignment(VAlign_Center); SetIsFocusable(true);
}
void UBSVolumeSettingsWidget::OnEscapePressed_Implementation()
{ if (auto* Edit = Cast<UBSEditModeComponent>(mInteractObject)) Edit->CloseVolumeSettings(); }
FReply UBSVolumeSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape) { OnEscapePressed_Implementation(); return FReply::Handled(); }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
FReply UBSVolumeSettingsWidget::NativeOnKeyDown(const FGeometry&, const FKeyEvent&) { return FReply::Handled(); }
TSharedRef<SWidget> UBSVolumeSettingsWidget::RebuildWidget()
{
    auto Grid = SNew(SGridPanel);
    const TCHAR* Labels[] = {TEXT("Size step (meters)"), TEXT("Rotation step (degrees)")};
    float* Values[] = {&Draft.VolumeStep, &Draft.VolumeRotation};
    for (int32 I = 0; I < 2; ++I)
    {
        Grid->AddSlot(0,I).Padding(8)[SNew(STextBlock).Text(FText::FromString(Labels[I]))];
        float* Value = Values[I];
        Grid->AddSlot(1,I).Padding(8)[SNew(SNumericEntryBox<float>).AllowSpin(false).MinDesiredValueWidth(180)
            .MinValue(.01f).MaxValue(I == 0 ? 1000.f : 180.f)
            .Value_Lambda([Value]() -> TOptional<float> { return *Value; }).OnValueChanged_Lambda([Value](float V) { *Value = V; })];
    }
    return SNew(SBox).WidthOverride(520)[SNew(SBorder).Padding(24).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.035f,.025f,.05f,.98f))[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",20)).Text(FText::FromString(TEXT("Volume selection")))]
        + SVerticalBox::Slot().AutoHeight()[Grid]
        + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Selects building centers inside the volume. These values apply to this session; change the mod settings for defaults.")))]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(8)[SNew(SButton).Text(FText::FromString(TEXT("Cancel"))).OnClicked_Lambda([this]() { OnEscapePressed_Implementation(); return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(8)[SNew(SButton).Text(FText::FromString(TEXT("Apply"))).IsEnabled_Lambda([this]() { return Draft.Valid(); }).OnClicked_Lambda([this]()
                { if (Draft.Valid()) if (auto* Edit = Cast<UBSEditModeComponent>(mInteractObject)) { Edit->VolumeSettings = Draft; Edit->CloseVolumeSettings(); } return FReply::Handled(); })]
        ]]];
}
