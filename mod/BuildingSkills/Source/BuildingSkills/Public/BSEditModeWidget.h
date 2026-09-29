#pragma once

#include "Blueprint/UserWidget.h"
#include "BSEditModeWidget.generated.h"

UCLASS()
class BUILDINGSKILLS_API UBSEditModeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetHints(UUserWidget* Hints);
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool Enabled) const override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool ReleaseChildren) override;
    TSharedPtr<class SBox> HintHost;
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
