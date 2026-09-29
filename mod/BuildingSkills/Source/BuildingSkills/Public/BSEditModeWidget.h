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

/** Row injected into the game's actual bottom-right keyboard shortcut list. */
UCLASS()
class BUILDINGSKILLS_API UBSEditShortcutWidget : public UUserWidget
{
    GENERATED_BODY()
protected:
    virtual void NativeConstruct() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
