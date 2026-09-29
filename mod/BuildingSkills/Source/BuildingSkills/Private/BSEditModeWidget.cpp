#include "BSEditModeWidget.h"
#include "BSEditModeComponent.h"
#include "GameFramework/Pawn.h"
#include "FGInputLibrary.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"

void UBSEditShortcutWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (auto* Grid = Cast<UUniformGridPanel>(GetParent()))
        if (auto* GridSlot = Cast<UUniformGridSlot>(Slot))
        {
            int32 LastRow = 0, LastColumn = 0;
            for (auto* Child : Grid->GetAllChildren())
                if (Child != this)
                    if (auto* OtherSlot = Cast<UUniformGridSlot>(Child->Slot))
                    {
                        LastRow = FMath::Max(LastRow, OtherSlot->GetRow());
                        LastColumn = FMath::Max(LastColumn, OtherSlot->GetColumn());
                    }
            GridSlot->SetRow(LastRow + 1);
            GridSlot->SetColumn(LastColumn);
            GridSlot->SetHorizontalAlignment(HAlign_Center);
            GridSlot->SetVerticalAlignment(VAlign_Center);
        }
}

TSharedRef<SWidget> UBSEditShortcutWidget::RebuildWidget()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right).Padding(0, 3)
        [
            SNew(STextBlock)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
            .Justification(ETextJustify::Center)
            .ColorAndOpacity(FLinearColor::White)
            .Text_Lambda([this]()
            {
                return FText::Format(NSLOCTEXT("BuildingSkills", "EditShortcut", "Edit\n{0}"),
                    UFGInputLibrary::GetInputActionNameAsText(GetOwningPlayer(), TEXT("BuildingSkills.ToggleEditMode")));
            })
        ];
}

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
                    SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 18)).Justification(ETextJustify::Center).Text_Lambda([this]()
                    {
                        auto* Pawn = GetOwningPlayerPawn();
                        auto* Edit = Pawn ? Pawn->FindComponentByClass<UBSEditModeComponent>() : nullptr;
                        return Edit ? Edit->GetStatusText() : FText::GetEmpty();
                    })
                    .ColorAndOpacity(FLinearColor(1.0f, 0.45f, 0.08f))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
                [
                    SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 14)).Justification(ETextJustify::Center).Text_Lambda([this]()
                    {
                        auto* Pawn = GetOwningPlayerPawn();
                        auto* Edit = Pawn ? Pawn->FindComponentByClass<UBSEditModeComponent>() : nullptr;
                        return Edit ? Edit->GetControlsText() : FText::GetEmpty();
                    })
                ]
            ]
        ];
}

#undef LOCTEXT_NAMESPACE
