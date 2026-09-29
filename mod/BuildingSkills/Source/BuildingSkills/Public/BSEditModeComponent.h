#pragma once

#include "Components/ActorComponent.h"
#include "FGBuildGunModeDescriptor.h"
#include "BSEditModeComponent.generated.h"

class AFGBuildGun;
class UEnhancedInputComponent;
class UInputAction;
class UBSEditModeWidget;

/** Named Build Gun mode; it deliberately has no construction recipe. */
UCLASS()
class BUILDINGSKILLS_API UBSEditModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public:
    UBSEditModeDescriptor();
};

/** Local input and HUD lifetime. The game's Build Gun APIs handle equipment RPCs. */
UCLASS()
class BUILDINGSKILLS_API UBSEditModeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBSEditModeComponent();
    void InitializeInput(UEnhancedInputComponent* Input);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void OnToggleTriggered();
    void OnToggleReleased();
    void LeaveEditMode(bool bUnequip);

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ToggleAction;

    UPROPERTY(Transient)
    TObjectPtr<UBSEditModeWidget> Indicator;

    UPROPERTY(Transient)
    TSubclassOf<UFGBuildGunModeDescriptor> PreviousMode;

    TWeakObjectPtr<UEnhancedInputComponent> BoundInput;
    TWeakObjectPtr<AFGBuildGun> EditGun;
    bool bToggleHeld = false;
};
