#include "BSEditModeWidget.h"
#include "FGInputLibrary.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "BuildingSkills"

TSharedRef<SWidget> UBSEditModeWidget::RebuildWidget()
{
    return SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 100, 0, 0)
        [
            SNew(SBorder).Padding(FMargin(24, 12))
            .BorderBackgroundColor(FLinearColor(0.03f, 0.03f, 0.03f, 0.9f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [
                    SNew(STextBlock).Text(LOCTEXT("EditIndicator", "EDIT"))
                    .ColorAndOpacity(FLinearColor(1.0f, 0.45f, 0.08f))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
                [
                    SNew(STextBlock).Text_Lambda([this]()
                    {
                        const FText Keys = UFGInputLibrary::GetInputActionNameAsText(
                            GetOwningPlayer(), TEXT("BuildingSkills.ToggleEditMode"));
                        return FText::Format(LOCTEXT("CloseEditHint", "{0}: Close"), Keys);
                    })
                ]
            ]
        ];
}

#undef LOCTEXT_NAMESPACE
