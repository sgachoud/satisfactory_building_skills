#include "BSTransformers.h"
#include "BSGroups.h"
#include "BuildingSkills.h"
#include "BSEditModeComponent.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "UI/FGGameUI.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Colors/SColorWheel.h"
#include "Styling/CoreStyle.h"

void UBSEditModeComponent::OnAddTransformer()
{
    if(FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsRuntimeTest"))) UE_LOG(LogBuildingSkills,Display,TEXT("Transformer add input: edit=%d transform=%d allowed=%d anchor=%d"),IsEditing(),bTransforming,InputAllowed(),BlueprintAnchor.IsAlive(GetWorld()));
    if(!IsEditing() || bTransforming || !InputAllowed() || !BlueprintAnchor.IsAlive(GetWorld())) return;
    auto* P=Cast<AFGCharacterPlayer>(GetOwner()); auto* PC=P?Cast<AFGPlayerController>(P->GetController()):nullptr;
    if(auto* RCO=PC?PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>():nullptr) RCO->Server_AddTransformer(BlueprintAnchor);
}
void UBSEditModeComponent::OnTransformerSettings()
{
    if(!IsEditing() || !InputAllowed() || TransformersWindow) return;
    auto* P=Cast<AFGCharacterPlayer>(GetOwner()); auto* UI=P?P->GetGameUI():nullptr;
    auto* PC=P?Cast<AFGPlayerController>(P->GetController()):nullptr;
    auto* RCO=PC?PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>():nullptr;
    if(!UI || !RCO) return;
    TransformersWindow=CreateWidget<UBSTransformersWidget>(PC);
    TransformersWindow->Draft=RCO->Transformers; TransformersWindow->Revision=RCO->TransformerRevision; TransformersWindow->mInteractObject=this;
    TransformersWindow->BuildAsBlueprint=RCO->TransformersBuildAsBlueprint;
    TransformersWindow->SetDefaultFocusWidget(TransformersWindow); UI->PushWidget(TransformersWindow); TransformersWindow->SetInputMode();
}
void UBSEditModeComponent::CloseTransformerSettings()
{
    auto* Window=TransformersWindow.Get(); TransformersWindow=nullptr;
    auto* P=Cast<AFGCharacterPlayer>(GetOwner());
    if(Window && P && P->GetGameUI()) P->GetGameUI()->PopWidget(Window);
}
UBSTransformersWidget::UBSTransformersWidget(const FObjectInitializer& I):Super(I)
{
    mUseKeyboard=true; mUseMouse=true; mMouseLockMode=EMouseLockMode::DoNotLock; mHideCursorDuringCapture=false;
    mDisableBuildGunActions=true; mSupportsCaching=false; mSupportsStacking=true;
    SetDesiredHorizontalAlignment(HAlign_Center); SetDesiredVerticalAlignment(VAlign_Center); SetIsFocusable(true);
}
void UBSTransformersWidget::OnEscapePressed_Implementation()
{ if(auto* E=Cast<UBSEditModeComponent>(mInteractObject)) E->CloseTransformerSettings(); }
FReply UBSTransformersWidget::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& K)
{ if(K.GetKey()==EKeys::Escape){OnEscapePressed_Implementation();return FReply::Handled();} return Super::NativeOnPreviewKeyDown(G,K); }
FReply UBSTransformersWidget::NativeOnKeyDown(const FGeometry&,const FKeyEvent&) {return FReply::Handled();}
void UBSTransformersWidget::RebuildList()
{
    ListBox->ClearChildren();
    for(int32 I=0;I<Draft.Num();++I)
        ListBox->AddSlot().AutoHeight().Padding(2)[SNew(SButton)
            .ButtonColorAndOpacity_Lambda([this,I](){return Selected==I?FLinearColor(.2f,.5f,.3f):FLinearColor(.08f,.08f,.08f);})
            .Text_Lambda([this,I](){return FText::FromString(FString::Printf(TEXT("%d. %s"),I+1,Draft[I].Mirror?TEXT("Mirror"):Draft[I].Rotation?TEXT("Rotation"):TEXT("Disabled")));})
            .OnClicked_Lambda([this,I](){Selected=I;return FReply::Handled();})];
}
TSharedRef<SWidget> UBSTransformersWidget::RebuildWidget()
{
    const auto Text=[](const TCHAR* T){return SNew(STextBlock).Text(FText::FromString(T));};
    auto Fields=SNew(SVerticalBox);
    auto Check=[&](const TCHAR* Label,bool FBSTransformer::*Field)
    {
        Fields->AddSlot().AutoHeight().Padding(4)[SNew(SCheckBox)
            .IsChecked_Lambda([this,Field](){return Current() && Current()->*Field?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this,Field](ECheckBoxState State){if(auto* C=Current()) {C->*Field=State==ECheckBoxState::Checked; if(C->*Field){if(Field==&FBSTransformer::Mirror) C->Rotation=false; if(Field==&FBSTransformer::Rotation) C->Mirror=false;}}})[Text(Label)]];
    };
    Check(TEXT("Original anchor"),&FBSTransformer::RelativeToOriginalAnchor);
    Fields->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true)
        .Text_Lambda([this](){return FText::FromString(Current() && Current()->RelativeToOriginalAnchor?
            TEXT("Coordinates relative to the original anchor at creation."):TEXT("World coordinates."));})];
    for(bool Rotation:{false,true})
    {
        Fields->AddSlot().AutoHeight().Padding(4)[Text(Rotation?TEXT("Rotation (degrees): X roll / Y pitch / Z yaw"):TEXT("Position (cm)"))];
        auto Row=SNew(SHorizontalBox);
        for(int32 Axis=0;Axis<3;++Axis)
            Row->AddSlot().FillWidth(1).Padding(4)[SNew(SNumericEntryBox<double>).MinDesiredValueWidth(100).AllowSpin(false)
                .Label()[Text(Axis==0?TEXT("X"):Axis==1?TEXT("Y"):TEXT("Z"))]
                .Value_Lambda([this,Rotation,Axis]()->TOptional<double>
                {
                    if(!Current()) return {};
                    const auto Frame=Current()->DisplayFrame();
                    if(!Rotation) return Frame.GetLocation()[Axis];
                    const auto R=Frame.Rotator(); return Axis==0?R.Roll:Axis==1?R.Pitch:R.Yaw;
                })
                .OnValueCommitted_Lambda([this,Rotation,Axis](double V,ETextCommit::Type)
                {
                    auto* C=Current(); if(!C || !FMath::IsFinite(V)) return;
                    auto Frame=C->DisplayFrame();
                    if(Rotation) { auto R=Frame.Rotator(); (Axis==0?R.Roll:Axis==1?R.Pitch:R.Yaw)=FMath::UnwindDegrees(V); Frame.SetRotation(R.Quaternion()); }
                    else { auto P=Frame.GetLocation(); P[Axis]=V; Frame.SetLocation(P); }
                    const auto Before=C->Frame; C->SetDisplayFrame(Frame); if(!C->Valid()) C->Frame=Before;
                })];
        Fields->AddSlot().AutoHeight()[Row];
    }
    Check(TEXT("Mirror across the X/Z plane"),&FBSTransformer::Mirror);
    Check(TEXT("Rotation around Z"),&FBSTransformer::Rotation);
    auto Number=[&](const TCHAR* Label,double FBSTransformer::*Field,double Min,double Max)
    {
        Fields->AddSlot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Text(Label)]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SNumericEntryBox<double>).MinDesiredValueWidth(150).AllowSpin(false).MinValue(Min).MaxValue(Max)
                .Value_Lambda([this,Field]()->TOptional<double>{return Current()?TOptional<double>(Current()->*Field):TOptional<double>();})
                .OnValueCommitted_Lambda([this,Field](double V,ETextCommit::Type){if(auto* C=Current()) C->*Field=V;})]];
    };
    Number(TEXT("Angle (degrees)"),&FBSTransformer::Angle,-360,360);
    Number(TEXT("Effect radius (meters)"),&FBSTransformer::Radius,0,1000);
    Check(TEXT("Repeat player"),&FBSTransformer::RepeatPlayer);
    Check(TEXT("Repeat earlier transformers"),&FBSTransformer::RepeatTransformers);
    Fields->AddSlot().AutoHeight().Padding(4)[Text(TEXT("Display color"))];
    Fields->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SBox).WidthOverride(160).HeightOverride(160)[SNew(SColorWheel)
        .SelectedColor_Lambda([this](){return Current()?Current()->Color.LinearRGBToHSV():FLinearColor::White;})
        .OnValueChanged_Lambda([this](FLinearColor HSV){if(auto* C=Current()){C->Color=HSV.HSVToLinearRGB();C->Color.A=1;}})]];
    Fields->AddSlot().AutoHeight().Padding(4)[SNew(SNumericEntryBox<float>).Label()[Text(TEXT("Brightness"))].MinValue(0.f).MaxValue(1.f).AllowSpin(true)
        .Value_Lambda([this]()->TOptional<float>{return Current()?Current()->Color.LinearRGBToHSV().B:1.f;})
        .OnValueChanged_Lambda([this](float V){if(auto* C=Current()){auto HSV=C->Color.LinearRGBToHSV();HSV.B=V;C->Color=HSV.HSVToLinearRGB();C->Color.A=1;}})];
    Fields->SetEnabled(TAttribute<bool>::CreateLambda([this](){return Current()!=nullptr;}));
    ListBox=SNew(SVerticalBox); RebuildList();
    auto Actions=SNew(SHorizontalBox);
    for(int32 Direction:{-1,1,0}) Actions->AddSlot().AutoWidth().Padding(3)[SNew(SButton).Text(FText::FromString(Direction<0?TEXT("Up"):Direction>0?TEXT("Down"):TEXT("Remove")))
        .IsEnabled_Lambda([this,Direction](){return Current() && (Direction==0 || Draft.IsValidIndex(Selected+Direction));})
        .OnClicked_Lambda([this,Direction](){if(Current()){if(Direction){Draft.Swap(Selected,Selected+Direction);Selected+=Direction;}else{Draft.RemoveAt(Selected);Selected=FMath::Min(Selected,Draft.Num()-1);}RebuildList();}return FReply::Handled();})];
    auto Other=SNew(SHorizontalBox);
    auto* E=Cast<UBSEditModeComponent>(mInteractObject);
    if(E && (E->IsConnectedSelection() || E->IsVolumeSelection() || E->bTransforming))
        Other->AddSlot().AutoWidth().Padding(3)[SNew(SButton).Text(FText::FromString(E->bTransforming?TEXT("Transform steps"):E->IsVolumeSelection()?TEXT("Volume settings"):TEXT("Connected settings")))
            .OnClicked_Lambda([this](){if(auto* Edit=Cast<UBSEditModeComponent>(mInteractObject)){Edit->CloseTransformerSettings();if(Edit->bTransforming)Edit->OnTransformSettings();else if(Edit->IsVolumeSelection())Edit->OpenVolumeSettings();else Edit->OpenConnectedSettings();}return FReply::Handled();})];
    Other->AddSlot().FillWidth(1)[SNew(SBox)];
    Other->AddSlot().AutoWidth().Padding(3)[SNew(SButton).Text(FText::FromString(TEXT("Cancel"))).OnClicked_Lambda([this](){OnEscapePressed_Implementation();return FReply::Handled();})];
    Other->AddSlot().AutoWidth().Padding(3)[SNew(SButton).Text(FText::FromString(TEXT("Apply")))
        .IsEnabled_Lambda([this](){for(const auto& R:Draft)if(!R.Valid())return false;return true;})
        .OnClicked_Lambda([this](){auto* Edit=Cast<UBSEditModeComponent>(mInteractObject);auto* PC=GetOwningPlayer<AFGPlayerController>();if(Edit && PC)if(auto* RCO=PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>()){RCO->Server_SetTransformers(Draft,Revision,BuildAsBlueprint);Edit->CloseTransformerSettings();}return FReply::Handled();})];
    return SNew(SBox).WidthOverride(940)[SNew(SBorder).Padding(20).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025f,.035f,.025f,.98f))[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",20)).Text(FText::FromString(TEXT("Edit settings — Transformers")))]
        +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SCheckBox)
            .IsChecked_Lambda([this](){return BuildAsBlueprint?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State){BuildAsBlueprint=State==ECheckBoxState::Checked;})[Text(TEXT("Build as blueprint"))]]
        +SVerticalBox::Slot().AutoHeight().Padding(4)[Text(TEXT("One blueprint per placement, including the original and all transformer copies."))]
        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(.4f).Padding(4)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(325)[SNew(SScrollBox)+SScrollBox::Slot()[ListBox.ToSharedRef()]]]+SVerticalBox::Slot().AutoHeight()[Actions]]
            +SHorizontalBox::Slot().FillWidth(.6f).Padding(10,0)[SNew(SBox).HeightOverride(470)[SNew(SScrollBox)+SScrollBox::Slot()[Fields]]]]
        +SVerticalBox::Slot().AutoHeight().Padding(4,12)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Top to bottom, once per action. Range uses the whole selection’s center. Repeats construction, deletion, materials, swatches and Edit transforms. Session only. Add at an anchor using the Add transformer shortcut.")))]
        +SVerticalBox::Slot().AutoHeight()[Other]]];
}
