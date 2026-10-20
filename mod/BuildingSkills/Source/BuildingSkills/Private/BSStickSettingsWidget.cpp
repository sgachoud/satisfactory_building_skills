#include "BSStick.h"
#include "BSEditModeComponent.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Styling/CoreStyle.h"

UBSStickSettingsWidget::UBSStickSettingsWidget(const FObjectInitializer& I) : Super(I)
{
    mUseKeyboard=true; mUseMouse=true; mMouseLockMode=EMouseLockMode::DoNotLock;
    mHideCursorDuringCapture=false; mDisableBuildGunActions=true; mSupportsCaching=false; mSupportsStacking=true;
    SetDesiredHorizontalAlignment(HAlign_Center); SetDesiredVerticalAlignment(VAlign_Center); SetIsFocusable(true);
}
void UBSStickSettingsWidget::OnEscapePressed_Implementation()
{ if (auto* Edit=Cast<UBSEditModeComponent>(mInteractObject)) Edit->CloseStickSettings(); }
FReply UBSStickSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if (E.GetKey()==EKeys::Escape) { OnEscapePressed_Implementation(); return FReply::Handled(); }
    return Super::NativeOnPreviewKeyDown(G,E);
}
FReply UBSStickSettingsWidget::NativeOnKeyDown(const FGeometry&,const FKeyEvent&) { return FReply::Handled(); }
TSharedRef<SWidget> UBSStickSettingsWidget::RebuildWidget()
{
    auto Grid=SNew(SGridPanel);
    auto Switches=SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(8)[SNew(SCheckBox)
            .IsChecked_Lambda([this]() { return Draft.VerticalConstraint ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Draft.VerticalConstraint=State==ECheckBoxState::Checked; Save(); })
            [SNew(STextBlock).Text(FText::FromString(TEXT("Vertical constraint (slope)")))]]
        +SHorizontalBox::Slot().AutoWidth().Padding(24,8,8,8)[SNew(SCheckBox)
            .IsChecked_Lambda([this]() { return Draft.HorizontalConstraint ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Draft.HorizontalConstraint=State==ECheckBoxState::Checked; Save(); })
            [SNew(STextBlock).Text(FText::FromString(TEXT("Horizontal constraint (radius)")))]];
    const TCHAR* Names[]={TEXT("Scroll step (m)"),TEXT("Upward slope (degrees)"),TEXT("Upward slope (%)"),TEXT("Horizontal radius (m)"),TEXT("Endpoint tilt step (degrees)")};
    for (int32 I=0; I<5; ++I)
    {
        const int32 Row=I==0 ? 0 : I==3 ? 2 : I==4 ? 3 : 1;
        const int32 Column=I==2 ? 2 : 0;
        Grid->AddSlot(Column,Row).Padding(8)[SNew(STextBlock).Text(FText::FromString(Names[I]))];
        Grid->AddSlot(Column+1,Row).Padding(8)[SNew(SNumericEntryBox<double>).AllowSpin(false).MinDesiredValueWidth(120)
            .IsEnabled_Lambda([this,I]() { return I==1 || I==2 ? Draft.VerticalConstraint : I==3 ? Draft.HorizontalConstraint : true; })
            .Value_Lambda([this,I]() -> TOptional<double> {
                return I==0 ? Draft.StickStep : I==1 ? Draft.StickSlope : I==2 ? 100*FMath::Tan(FMath::DegreesToRadians(double(Draft.StickSlope))) : I==3 ? Draft.StickRadius : Draft.StickTiltStep;
            }).OnValueCommitted_Lambda([this,I](double V,ETextCommit::Type) {
                if (!FMath::IsFinite(V)) return;
                if (I==0) Draft.StickStep=V;
                else if (I==1) Draft.StickSlope=V;
                else if (I==2) Draft.StickSlope=FMath::RadiansToDegrees(FMath::Atan(V/100));
                else if (I==3) Draft.StickRadius=V;
                else Draft.StickTiltStep=V;
                Save();
            })];
    }
    return SNew(SBox).WidthOverride(760)[SNew(SBorder).Padding(24).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.025,.035,.04,.98))[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",20)).Text(FText::FromString(TEXT("Stick settings")))]
        +SVerticalBox::Slot().AutoHeight()[Switches]
        +SVerticalBox::Slot().AutoHeight()[Grid]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Positive slope climbs; negative descends. Radius: positive turns right, negative turns left, zero stays straight. Unchecked constraints are free; their values are retained. With both unchecked, native routing is used. Constraints apply after placing the start. Changes last until you leave this save; use mod settings for numeric defaults.")))]
        +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().Padding(8)[SNew(SButton).Text(FText::FromString(TEXT("Close"))).OnClicked_Lambda([this]() { OnEscapePressed_Implementation(); return FReply::Handled(); })]
        ]]];
}

void UBSStickSettingsWidget::Save()
{ if (Draft.Valid()) if (auto* E=Cast<UBSEditModeComponent>(mInteractObject)) E->GetWorld()->GetSubsystem<UBSStickSession>()->GetSettings()=Draft; }
