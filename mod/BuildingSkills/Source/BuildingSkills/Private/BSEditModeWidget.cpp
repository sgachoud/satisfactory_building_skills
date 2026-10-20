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
#include "FGCharacterPlayer.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Patching/NativeHookManager.h"

namespace
{
    // Only the active local Customizer mode labels are registered. Other native
    // and mod widgets keep their normal SetText behavior.
    TMap<TWeakObjectPtr<UTextBlock>, TWeakObjectPtr<UBSEditModeWidget>> SpreadTextOwners;
    struct FPlanHiddenWidget { TWeakObjectPtr<UBSEditModeWidget> Owner; ESlateVisibility Previous; };
    TMap<TWeakObjectPtr<UWidget>,FPlanHiddenWidget> PlanHiddenWidgets;
    void RestorePlanWidgets(UBSEditModeWidget* Owner)
    {
        TArray<TPair<TWeakObjectPtr<UWidget>,ESlateVisibility>> Restore;
        for (auto It=PlanHiddenWidgets.CreateIterator();It;++It)
            if (!It.Value().Owner.IsValid() || It.Value().Owner==Owner)
            { Restore.Emplace(It.Key(),It.Value().Previous); It.RemoveCurrent(); }
        for (const auto& Entry:Restore) if (Entry.Key.IsValid()) Entry.Key->SetVisibility(Entry.Value);
    }
}

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
    auto Card=[this](FName Action,FText Label) -> TSharedRef<SWidget> {
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
                .Text_Lambda([this,Action]()
                {
                    // Both Alt keys activate vanilla's Alt chord action.
                    FString Key = UFGInputLibrary::GetInputActionNameAsText(GetOwningPlayer(), Action).ToString();
                    Key.ReplaceInline(TEXT("Right Alt"), TEXT("Alt"));
                    Key.ReplaceInline(TEXT("Left Alt"), TEXT("Alt"));
                    return FText::FromString(Key);
                })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
            [SNew(SBox).HeightOverride(1)[SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush"))]]
            + SVerticalBox::Slot().FillHeight(1).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)).Text(Label)]
        ]
    ];
    };
    // The native helper has a free cell above our Edit card. Keep its original
    // grid dimensions, and show Stick there only while a spline is equipped.
    return SNew(SOverlay)
        +SOverlay::Slot()[Card(TEXT("BuildingSkills.ToggleEditMode"),NSLOCTEXT("BuildingSkills","EditShortcutLabel","Edit"))]
        +SOverlay::Slot()[SNew(SBox).WidthOverride(64).HeightOverride(58).RenderTransform(FSlateRenderTransform(FVector2D(0,-124)))
            [Card(TEXT("BuildingSkills.TogglePlan"),NSLOCTEXT("BuildingSkills","PlanShortcutLabel","Plan"))]]
        +SOverlay::Slot()[SNew(SBox).WidthOverride(64).HeightOverride(58)
            .RenderTransform(FSlateRenderTransform(FVector2D(0,-62)))
            .Visibility_Lambda([this]() {
                auto* P=GetOwningPlayerPawn(); auto* E=P ? P->FindComponentByClass<UBSEditModeComponent>() : nullptr;
                const auto* Plan=UBSPlanComponent::Find(Cast<AFGCharacterPlayer>(P));
                return FBSStick::Active(E) || (Plan && Plan->Active) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
            })[Card(TEXT("BuildingSkills.StickToggle"),NSLOCTEXT("BuildingSkills","StickShortcutLabel","Stick"))]];
}

#define LOCTEXT_NAMESPACE "BuildingSkills"

TSharedRef<SWidget> UBSEditModeWidget::RebuildWidget()
{
    return SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(35, 0, 0, 0)
        [
            SNew(SBorder).Padding(FMargin(16, 12))
            .BorderBackgroundColor(FLinearColor(0.03f, 0.03f, 0.03f, 0.9f))
            .Visibility_Lambda([this]()
            {
                auto* Pawn = GetOwningPlayerPawn();
                auto* Edit = Pawn ? Pawn->FindComponentByClass<UBSEditModeComponent>() : nullptr;
                // Painting uses the native below-cursor build-mode row.
                return Edit && Edit->IsPainting() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
            })
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
                    .ColorAndOpacity_Lambda([this]()
                    {
                        auto* Pawn = GetOwningPlayerPawn();
                        auto* Edit = Pawn ? Pawn->FindComponentByClass<UBSEditModeComponent>() : nullptr;
                        if (auto* Plan=UBSPlanComponent::Find(Cast<AFGCharacterPlayer>(Pawn)); Plan && Plan->Active) return FSlateColor(FLinearColor(.05f,.7f,1.f));
                        return FSlateColor(Edit && Edit->IsEditing() ? FLinearColor(.1f, 1.f, .2f) : FLinearColor(1.f, .45f, .08f));
                    })
                ]

            ]
        ]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom)
        .Padding(TAttribute<FMargin>::CreateLambda([this]()
        {
            auto* Pawn = GetOwningPlayerPawn();
            auto* Edit = Pawn ? Pawn->FindComponentByClass<UBSEditModeComponent>() : nullptr;
            // Edit replaces the native hints. Connected Dismantle and Paint
            // supplement them, so reserve a separate row immediately above.
            const auto* Plan=UBSPlanComponent::Find(Cast<AFGCharacterPlayer>(Pawn));
            return FMargin(0, 0, 0, (Plan && Plan->Active) || (Edit && Edit->IsEditing()) ? 120.f : Edit && FBSStick::Active(Edit) ? 155.f : 240.f);
        }))
        [SAssignNew(HintHost, SBox).WidthOverride(1100).HeightOverride(110)];
}

void UBSEditModeWidget::SetHints(UUserWidget* Hints)
{
    if (!HintHost) return;
    TSharedRef<SWidget> Content = Hints ? Hints->TakeWidget() : SNullWidget::NullWidget;
    if (HintHost->GetChildren()->GetChildAt(0) != Content) HintHost->SetContent(Content);
}

void UBSEditModeWidget::ReleaseSlateResources(bool ReleaseChildren)
{
    Super::ReleaseSlateResources(ReleaseChildren);
    HintHost.Reset();
}

void UBSEditModeWidget::UpdateSpreadMode(bool Enabled, const FText& Text)
{
    if (!WITH_EDITOR)
    {
        static bool HookInstalled = false;
        if (!HookInstalled)
        {
            HookInstalled = true;
            SUBSCRIBE_UOBJECT_METHOD(UWidget, SetVisibility, [](auto& Scope,UWidget* Target,ESlateVisibility Visibility)
            {
                if (const auto* Entry=PlanHiddenWidgets.Find(Target); Entry && Entry->Owner.IsValid())
                {
                    const auto* Plan=UBSPlanComponent::Find(Cast<AFGCharacterPlayer>(Entry->Owner->GetOwningPlayerPawn()));
                    if (Plan && Plan->Active) { Scope(Target,ESlateVisibility::Collapsed); return; }
                }
            });
            SUBSCRIBE_UOBJECT_METHOD(UTextBlock, SetText, [](auto& Scope, UTextBlock* Target, FText Incoming)
            {
                if (SpreadTextOwners.IsEmpty()) return;
                const auto* Found = SpreadTextOwners.Find(Target);
                auto* Owner = Found ? Found->Get() : nullptr;
                if (!Owner || Owner->NativeSpreadText.Get() != Target) return;
                auto* Pawn = Owner->GetOwningPlayerPawn();
                auto* Edit = Pawn ? Pawn->FindComponentByClass<UBSEditModeComponent>() : nullptr;
                const auto* Plan=UBSPlanComponent::Find(Cast<AFGCharacterPlayer>(Pawn));
                if (!Edit || (!Edit->IsSpreadPainting() && !(Plan && Plan->Active)))
                {
                    Owner->RestoreSpreadMode();
                    return;
                }
                // The native Blueprint also refreshes this label after the pawn
                // tick. Change that write itself instead of racing it each frame.
                // Remember its latest intended label for exact restoration.
                if (!Incoming.EqualTo(Owner->LastSpreadText)) Owner->PreviousSpreadText = Incoming;
                Scope(Target, Owner->LastSpreadText);
            });
        }
    }
    auto* Player = Cast<AFGCharacterPlayer>(GetOwningPlayerPawn());
    auto* UI = Player ? Player->GetGameUI() : nullptr;
    const auto* Plan=UBSPlanComponent::Find(Player);
    const bool Planning=Plan && Plan->Active;
    if (!Planning) RestorePlanWidgets(this);
    UTextBlock* NativeText = nullptr;
    UWidget* NativeContainer = nullptr;
    UWidget* NativeIcon = nullptr;
    UUserWidget* NativeModeWidget = nullptr;
    if ((Enabled || Planning) && UI)
    {
        // Paint and Build share the native build-mode widget. Use its existing
        // below-cursor row, so font, backdrop, positioning and UI scale stay native.
        TFunction<void(UUserWidget*)> FindModeRow = [&](UUserWidget* Widget)
        {
            if (!Widget || !Widget->WidgetTree || NativeText) return;
            if (Widget->GetClass()->GetFName() == TEXT("Widget_BuildMode_C"))
            {
                NativeText = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("mBuildModeText")));
                NativeContainer = Widget->WidgetTree->FindWidget(TEXT("mBuildModeContainer"));
                NativeIcon = Widget->WidgetTree->FindWidget(TEXT("mBuildModeIcon"));
                if (NativeText && NativeContainer) { NativeModeWidget=Widget; return; }
                NativeText = nullptr;
            }
            Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
            { if (auto* Nested = Cast<UUserWidget>(Child)) FindModeRow(Nested); });
        };
        const auto Stack = UI->GetInteractWidgetStack();
        for (int32 I = Stack.Num() - 1; I >= 0 && !NativeText; --I) FindModeRow(Stack[I]);
    }
    if (Planning && NativeModeWidget)
    {
        // Hide only the native construction branches. Preserve the mode row,
        // radial and other UI layers regardless of their parent hierarchy.
        for (const TCHAR* Name:{TEXT("mBuildHints"),TEXT("BuildingNameOverlay"),TEXT("mCostSlotsGrid"),TEXT("mCostSlotOverflow"),TEXT("mButtonHintsGamepad"),TEXT("mHologramButtonHintsGamepad"),TEXT("mMinimalButtonHintsGamepad")})
            if (auto* Sibling=NativeModeWidget->WidgetTree->FindWidget(Name))
            {
                if (!PlanHiddenWidgets.Contains(Sibling)) PlanHiddenWidgets.Add(Sibling,{this,Sibling->GetVisibility()});
                Sibling->SetVisibility(ESlateVisibility::Collapsed);
            }
    }
    if (NativeText != NativeSpreadText.Get()) RestoreSpreadMode();
    if (!NativeText) return;
    if (!NativeSpreadText.IsValid())
    {
        NativeSpreadText = NativeText;
        NativeSpreadContainer = NativeContainer;
        NativeSpreadIcon = NativeIcon;
        PreviousSpreadText = NativeText->GetText();
        PreviousSpreadVisibility = NativeContainer->GetVisibility();
        if (NativeIcon) PreviousSpreadIconVisibility = NativeIcon->GetVisibility();
    }
    LastSpreadText = Text;
    SpreadTextOwners.Add(NativeText, this);
    if (!NativeText->GetText().EqualTo(Text)) NativeText->SetText(Text);
    NativeContainer->SetVisibility(Enabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    // A retained Build/Dismantle mode icon would describe the previous gun state.
    if (NativeIcon) NativeIcon->SetVisibility(ESlateVisibility::Collapsed);
}

void UBSEditModeWidget::RestoreSpreadMode()
{
    // Unregister before SetText: restoration must pass through to native UMG.
    SpreadTextOwners.Remove(NativeSpreadText);
    if (NativeSpreadText.IsValid() && NativeSpreadText->GetText().EqualTo(LastSpreadText))
    {
        // If the native widget has already changed to a Build-mode label, it
        // owns the row again; never overwrite its newer text or visibility.
        NativeSpreadText->SetText(PreviousSpreadText);
        if (NativeSpreadContainer.IsValid()) NativeSpreadContainer->SetVisibility(PreviousSpreadVisibility);
        if (NativeSpreadIcon.IsValid()) NativeSpreadIcon->SetVisibility(PreviousSpreadIconVisibility);
    }
    NativeSpreadText.Reset(); NativeSpreadContainer.Reset(); NativeSpreadIcon.Reset();
    PreviousSpreadText = FText::GetEmpty(); LastSpreadText = FText::GetEmpty();
}

void UBSEditModeWidget::NativeDestruct()
{
    RestorePlanWidgets(this);
    RestoreSpreadMode();
    Super::NativeDestruct();
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
    const FQuat Frame = Edit->GetTransformFrame();
    const FVector Axes[] = {Frame.GetAxisX(), Frame.GetAxisY(), Frame.GetAxisZ()};
    const FLinearColor Colors[] = {FLinearColor(1, .15f, .1f), FLinearColor(.15f, 1, .2f), FLinearColor(.2f, .5f, 1)};
    auto Project = [&](const FVector& World, FVector2D& Screen)
    { return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, World, Screen, true); };
    auto Line = [&](const FVector& A, const FVector& B, FLinearColor Color, float Thickness)
    {
        FVector2D P, Q;
        if (Project(A, P) && Project(B, Q))
            FSlateDrawElement::MakeLines(Out, Base + 1, Geometry.ToPaintGeometry(), {P,Q}, ESlateDrawEffect::None, Color, true, Thickness);
    };
    // Pending input is previewed without moving native actors or factory simulation.
    TArray<FBox> DisplayBoxes;
    if (Edit->GetTransformPreview().Num() > 256)
    {
        FBox Combined(ForceInit);
        for (const auto& Box : Edit->GetTransformPreview()) Combined += Box;
        DisplayBoxes.Add(Combined);
    }
    const auto& Boxes = DisplayBoxes.IsEmpty() ? Edit->GetTransformPreview() : DisplayBoxes;
    for (const FBox& Box : Boxes)
    {
        FVector Corners[8];
        for (int32 I = 0; I < 8; ++I) Corners[I] = FVector(I & 1 ? Box.Max.X : Box.Min.X, I & 2 ? Box.Max.Y : Box.Min.Y, I & 4 ? Box.Max.Z : Box.Min.Z);
        for (int32 I = 0; I < 8; ++I)
            for (int32 Bit = 1; Bit <= 4; Bit <<= 1)
                if (!(I & Bit)) Line(Corners[I], Corners[I | Bit], FLinearColor(.1f, 1.f, .2f, .7f), 1);
    }
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
