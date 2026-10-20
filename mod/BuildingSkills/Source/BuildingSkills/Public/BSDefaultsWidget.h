#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "BSDefaultsWidget.generated.h"

/** Native SML properties have no default editor. Supply the root's editor. */
UCLASS()
class BUILDINGSKILLS_API UBSDefaultsSection : public UConfigPropertySection
{
    GENERATED_BODY()
public:
    virtual UUserWidget* CreateEditorWidget_Implementation(UUserWidget* ParentWidget) const override;
};
UCLASS()
class BUILDINGSKILLS_API UBSDefaultsWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<UConfigPropertySection> Section;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
