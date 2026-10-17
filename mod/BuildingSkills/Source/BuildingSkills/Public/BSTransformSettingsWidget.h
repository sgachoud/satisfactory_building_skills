#pragma once
#include "UI/FGInteractWidget.h"
#include "BSTransformSettingsWidget.generated.h"

UCLASS()
class BUILDINGSKILLS_API UBSTransformSettingsWidget : public UFGInteractWidget
{
    GENERATED_BODY()
public:
    UBSTransformSettingsWidget(const FObjectInitializer& Initializer);
    virtual void OnEscapePressed_Implementation() override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
};
