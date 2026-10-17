#pragma once

#include "Blueprint/UserWidget.h"
#include "BSEditModeWidget.generated.h"

UCLASS()
class BUILDINGSKILLS_API UBSEditModeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetHints(UUserWidget* Hints);
    void UpdateSpreadMode(bool Enabled, const FText& Text);
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool Enabled) const override;
protected:
    virtual void NativeDestruct() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool ReleaseChildren) override;
    TSharedPtr<class SBox> HintHost;
    TWeakObjectPtr<class UTextBlock> NativeSpreadText;
    TWeakObjectPtr<UWidget> NativeSpreadContainer;
    TWeakObjectPtr<UWidget> NativeSpreadIcon;
    FText PreviousSpreadText;
    FText LastSpreadText;
    ESlateVisibility PreviousSpreadVisibility = ESlateVisibility::Collapsed;
    ESlateVisibility PreviousSpreadIconVisibility = ESlateVisibility::Collapsed;
    void RestoreSpreadMode();
};

/** Row injected into the game's actual bottom-right keyboard shortcut list. */
UCLASS()
class BUILDINGSKILLS_API UBSEditShortcutWidget : public UUserWidget
{
    GENERATED_BODY()
protected:
    virtual void NativeConstruct() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
