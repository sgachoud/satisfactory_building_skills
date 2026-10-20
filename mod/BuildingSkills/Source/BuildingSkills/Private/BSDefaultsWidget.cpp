#include "BSDefaultsWidget.h"
#include "BSPlan.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"

UUserWidget* UBSDefaultsSection::CreateEditorWidget_Implementation(UUserWidget* ParentWidget) const
{
    if (!ParentWidget) return nullptr;
    auto* Widget=CreateWidget<UBSDefaultsWidget>(ParentWidget);
    if (Widget) Widget->Section=const_cast<UBSDefaultsSection*>(this);
    return Widget;
}
TSharedRef<SWidget> UBSDefaultsWidget::RebuildWidget()
{
    auto Root=SNew(SVerticalBox);
    if (!Section) return Root;
    Root->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(
        TEXT("Changes are saved automatically. Defaults initialize new sessions or new plans; K-menu overrides remain temporary. History size is controlled by the server in multiplayer.")))];
    const TCHAR* Categories[]={TEXT("History"),TEXT("Transform"),TEXT("Connected components"),TEXT("Volume"),TEXT("Stick"),TEXT("Rail planner")};
    for (int32 Category=0;Category<6;++Category)
    {
        auto Body=SNew(SVerticalBox);
        TArray<FString> Names; Section->SectionProperties.GetKeys(Names); Names.Sort();
        for (const auto& Name:Names)
        {
            const int32 Group=Name.StartsWith(TEXT("Action")) ? 0 : Name.StartsWith(TEXT("Connected")) ? 2 : Name.StartsWith(TEXT("Volume")) ? 3 :
                Name.StartsWith(TEXT("Stick")) || Name.EndsWith(TEXT("Constraint")) ? 4 : Name.StartsWith(TEXT("Plan")) ? 5 : 1;
            if (Group!=Category) continue;
            auto* Property=Section->SectionProperties.FindChecked(Name).Get();
            auto Row=SNew(SHorizontalBox);
            Row->AddSlot().FillWidth(1).Padding(6)[SNew(STextBlock).AutoWrapText(true).Text(Property->DisplayName)];
            if (auto* Number=Cast<UConfigPropertyFloat>(Property))
            {
                if (Name==TEXT("ConnectedScope") || Name==TEXT("PlanRemainder"))
                {
                    const TArray<FString> Labels=Name==TEXT("ConnectedScope") ? TArray<FString>{TEXT("All"),TEXT("Category"),TEXT("Subcategory"),TEXT("Strict")} : TArray<FString>{TEXT("Change only last"),TEXT("Reduce evenly"),TEXT("Increase evenly")};
                    auto Options=SNew(SVerticalBox);
                    for (int32 I=0;I<Labels.Num();++I) Options->AddSlot().AutoHeight()[SNew(SButton).Text(FText::FromString(Labels[I]))
                        .OnClicked_Lambda([this,Number,I]() { Number->Value=I; Section->MarkDirty(); return FReply::Handled(); })];
                    Row->AddSlot().AutoWidth().Padding(6)[SNew(SComboButton)
                        .ButtonContent()[SNew(STextBlock).Text_Lambda([Number,Labels]() { return FText::FromString(Labels[FMath::Clamp(FMath::RoundToInt(Number->Value),0,Labels.Num()-1)]); })].MenuContent()[Options]];
                }
                else
                Row->AddSlot().AutoWidth().Padding(6)[SNew(SNumericEntryBox<float>).AllowSpin(false).MinDesiredValueWidth(120)
                    .Value_Lambda([Number]() -> TOptional<float> { return Number->Value; })
                    .OnValueCommitted_Lambda([this,Number,Name](float V,ETextCommit::Type) {
                        if (!FMath::IsFinite(V)) return;
                        float Min=0,Max=100000;
                        if (Name.Contains(TEXT("History"))) Max=256;
                        else if (Name==TEXT("ConnectedLimit")) { Min=1; Max=2048; }
                        else if (Name==TEXT("PlanSegmentLength")) { Min=FBSPlanBuild::MinimumLength()/100; Max=FBSPlanBuild::MaximumLength()/100; }
                        else if (Name==TEXT("ConnectedScope")) Max=3;
                        else if (Name==TEXT("PlanRemainder")) Max=2;
                        else if (Name==TEXT("PlanSignalEvery")) { Min=1; Max=2048; }
                        else if (Name==TEXT("ConnectedGap")) Max=1000;
                        else if (Name==TEXT("VolumeStep") || Name==TEXT("StickStep")) { Min=.01f; Max=1000; }
                        else if (Name==TEXT("VolumeRotation")) { Min=.01f; Max=180; }
                        else if (Name==TEXT("StickTiltStep")) { Min=.01f; Max=90; }
                        else if (Name==TEXT("StickSlope")) { Min=-89; Max=89; }
                        else if (Name==TEXT("StickRadius")) Min=-100000;
                        else if (Name==TEXT("RepeatDelay")) { Min=.05f; Max=2; }
                        else if (Name.Contains(TEXT("Scale"))) { Min=1.001f; Max=100; }
                        else if (Name.Contains(TEXT("Rotate"))) { Min=.01f; Max=180; }
                        else if (Name.Contains(TEXT("Move"))) { Min=.01f; Max=10000; }
                        else if (Name!=TEXT("ConnectedGap") && Name!=TEXT("StickDistance")) Min=.01f;
                        if (Name==TEXT("StickRadius") && V!=0 && FMath::Abs(V)<.01f) V=V<0 ? -.01f : .01f;
                        Number->Value=FMath::Clamp(V,Min,Max); Number->MarkDirty(); Section->MarkDirty();
                    })];
                if (Name==TEXT("StickSlope")) Row->AddSlot().AutoWidth().Padding(6)[SNew(SNumericEntryBox<float>).AllowSpin(false).MinDesiredValueWidth(120)
                    .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("%")))]
                    .Value_Lambda([Number]() -> TOptional<float> { return FMath::Tan(FMath::DegreesToRadians(Number->Value))*100; })
                    .OnValueCommitted_Lambda([this,Number](float V,ETextCommit::Type) {
                        if (FMath::IsFinite(V)) { Number->Value=FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(V/100)),-89.f,89.f); Section->MarkDirty(); }
                    })];
            }
            else if (auto* Boolean=Cast<UConfigPropertyBool>(Property))
                Row->AddSlot().AutoWidth().Padding(6)[SNew(SCheckBox)
                    .IsChecked_Lambda([Boolean]() { return Boolean->Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this,Boolean](ECheckBoxState S) { Boolean->Value=S==ECheckBoxState::Checked; Boolean->MarkDirty(); Section->MarkDirty(); })];
            else if (auto* String=Cast<UConfigPropertyString>(Property))
                Row->AddSlot().FillWidth(1).Padding(6)[SNew(SEditableTextBox).Text_Lambda([String]() { return FText::FromString(String->Value); })
                    .OnTextCommitted_Lambda([this,String](const FText& T,ETextCommit::Type) { String->Value=T.ToString(); String->MarkDirty(); Section->MarkDirty(); })];
            Row->AddSlot().AutoWidth().Padding(6)[SNew(SButton).Text(FText::FromString(TEXT("Reset")))
                .OnClicked_Lambda([this,Property]() { Property->ResetToDefault(); Section->MarkDirty(); return FReply::Handled(); })];
            Body->AddSlot().AutoHeight()[Row];
        }
        Root->AddSlot().AutoHeight().Padding(4)[SNew(SExpandableArea).AreaTitle(FText::FromString(Categories[Category])).InitiallyCollapsed(false).BodyContent()[Body]];
    }
    return Root;
}
