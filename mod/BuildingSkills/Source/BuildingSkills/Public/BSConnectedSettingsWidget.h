#pragma once
#include "UI/FGInteractWidget.h"
#include "BSConnectedSettings.h"
#include "BSConnectedSettingsWidget.generated.h"

UCLASS()
class BUILDINGSKILLS_API UBSConnectedSettingsWidget : public UFGInteractWidget
{
    GENERATED_BODY()
public:
    UBSConnectedSettingsWidget(const FObjectInitializer& Initializer);
    UPROPERTY(Transient) FBSConnectedSettings Draft;
    virtual void OnEscapePressed_Implementation() override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    TArray<TSharedPtr<FString>> ScopeOptions;
    FString Error;
};
