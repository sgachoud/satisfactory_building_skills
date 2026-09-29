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
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Rendering/DrawElements.h"
#include "GameFramework/PlayerController.h"

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


int32 UBSEditModeWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
    FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool Enabled) const
{
    const int32 Base = Super::NativePaint(Args, Geometry, CullingRect, Out, Layer, Style, Enabled);
    auto* Pawn = GetOwningPlayerPawn();
    auto* Edit = Pawn ? Pawn->FindComponentByClass<UBSEditModeComponent>() : nullptr;
    auto* PC = GetOwningPlayer();
    if (!Edit || !Edit->IsTransforming() || !PC) return Base;
    const FVector Center = Edit->GetTransformPivot();
    const auto Mode = Edit->GetTransformMode();
    const FVector Active = FBSTransformMath::Axes(Mode);
    FVector Eye; FRotator Rotation;
    PC->GetPlayerViewPoint(Eye, Rotation);
    const double Radius = FMath::Clamp(FVector::Distance(Eye, Center) * 0.075, 65.0, 1500.0);
    const FVector Axes[] = {FVector::ForwardVector, FVector::RightVector, FVector::UpVector};
    const FLinearColor Colors[] = {FLinearColor(1, .15f, .1f), FLinearColor(.15f, 1, .2f), FLinearColor(.2f, .5f, 1)};
    auto Project = [&](const FVector& World, FVector2D& Screen)
    { return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, World, Screen, true); };
    auto Line = [&](const FVector& A, const FVector& B, FLinearColor Color, float Thickness)
    {
        FVector2D P, Q;
        if (Project(A, P) && Project(B, Q))
            FSlateDrawElement::MakeLines(Out, Base + 1, Geometry.ToPaintGeometry(), {P,Q}, ESlateDrawEffect::None, Color, true, Thickness);
    };
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        const bool Selected = Active[Axis] > 0;
        const FLinearColor Color = Colors[Axis].CopyWithNewOpacity(Selected ? 1.f : .22f);
        const FVector End = Center + Axes[Axis] * Radius;
        if (uint8(Mode) < 3)
        {
            const FVector U = Axes[(Axis + 1) % 3], V = Axes[(Axis + 2) % 3];
            TArray<FVector2D> Ring;
            for (int32 Segment = 0; Segment <= 64; ++Segment)
            {
                const double Angle = Segment * UE_TWO_PI / 64;
                FVector2D Point;
                if (!Project(Center + Radius * (U * FMath::Cos(Angle) + V * FMath::Sin(Angle)), Point)) { Ring.Reset(); break; }
                Ring.Add(Point);
            }
            if (Ring.Num() > 1)
                FSlateDrawElement::MakeLines(Out, Base + 1, Geometry.ToPaintGeometry(), Ring,
                    ESlateDrawEffect::None, Color, true, Selected ? 3.f : 1.f);
        }
        else
        {
            Line(Center, End, Color, Selected ? 3.f : 1.f);
            const FVector Side = Axes[(Axis + 1) % 3] * Radius * .07;
            Line(End, End - Axes[Axis] * Radius * .18 + Side, Color, 3);
            Line(End, End - Axes[Axis] * Radius * .18 - Side, Color, 3);
        }
        FVector2D Label;
        if (Project(Center + Axes[Axis] * Radius * 1.18, Label))
            FSlateDrawElement::MakeText(Out, Base + 2, Geometry.ToPaintGeometry(FVector2D(40,30), FSlateLayoutTransform(Label)),
                Axis == 0 ? TEXT("X") : Axis == 1 ? TEXT("Y") : TEXT("Z"), FCoreStyle::GetDefaultFontStyle("Bold", 16), ESlateDrawEffect::None, Color);
    }
    return Base + 2;
}
