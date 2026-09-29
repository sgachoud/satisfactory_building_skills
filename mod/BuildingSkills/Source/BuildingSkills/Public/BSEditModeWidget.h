#pragma once

#include "Blueprint/UserWidget.h"
#include "BSEditModeWidget.generated.h"

UCLASS()
class BUILDINGSKILLS_API UBSEditModeWidget : public UUserWidget
{
    GENERATED_BODY()
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
