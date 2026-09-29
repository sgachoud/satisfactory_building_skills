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
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Components/Image.h"
#include "Blueprint/WidgetTree.h"

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
            GridSlot->SetRow(LastRow);
            GridSlot->SetColumn(LastColumn + 1);
            // The native shortcut canvas has a fixed width. Grow leftward so
            // the extra column stays inside its bottom-right screen margin.
            Grid->SetRenderTranslation(FVector2D(-70, 0));
            GridSlot->SetHorizontalAlignment(HAlign_Center);
            GridSlot->SetVerticalAlignment(VAlign_Center);
        }
}

TSharedRef<SWidget> UBSEditShortcutWidget::RebuildWidget()
{
    return SNew(SBox).WidthOverride(64).HeightOverride(58)
    [
        SNew(SBorder).Padding(FMargin(4, 2))
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f, 0.65f))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
            [
                SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                .Text_Lambda([this]()
                {
                    // Both Alt keys activate vanilla's Alt chord action.
                    FString Key = UFGInputLibrary::GetInputActionNameAsText(GetOwningPlayer(), TEXT("BuildingSkills.ToggleEditMode")).ToString();
                    Key.ReplaceInline(TEXT("Right Alt"), TEXT("Alt"));
                    Key.ReplaceInline(TEXT("Left Alt"), TEXT("Alt"));
                    return FText::FromString(Key);
                })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
            [SNew(SBox).HeightOverride(1)[SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush"))]]
            + SVerticalBox::Slot().FillHeight(1).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)).Text(NSLOCTEXT("BuildingSkills", "EditShortcutLabel", "Edit"))]
        ]
    ];
}

#define LOCTEXT_NAMESPACE "BuildingSkills"

TSharedRef<SWidget> UBSEditModeWidget::RebuildWidget()
{
    return SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(35, 0, 0, 0)
        [
            SNew(SBorder).Padding(FMargin(16, 12))
            .BorderBackgroundColor(FLinearColor(0.03f, 0.03f, 0.03f, 0.9f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
                [
                    SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 15)).Justification(ETextJustify::Left).Text_Lambda([this]()
                    {
                        auto* Pawn = GetOwningPlayerPawn();
                        auto* Edit = Pawn ? Pawn->FindComponentByClass<UBSEditModeComponent>() : nullptr;
                        return Edit ? Edit->GetStatusText() : FText::GetEmpty();
                    })
                    .ColorAndOpacity(FLinearColor(1.0f, 0.45f, 0.08f))
                ]

            ]
        ]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 120)
        [SAssignNew(HintHost, SBox).WidthOverride(1100).HeightOverride(64)];
}

void UBSEditModeWidget::SetHints(UUserWidget* Hints)
{
    if (HintHost) HintHost->SetContent(Hints ? Hints->TakeWidget() : SNullWidget::NullWidget);
}

void UBSEditModeWidget::ReleaseSlateResources(bool ReleaseChildren)
{
    Super::ReleaseSlateResources(ReleaseChildren);
    HintHost.Reset();
}

#undef LOCTEXT_NAMESPACE
