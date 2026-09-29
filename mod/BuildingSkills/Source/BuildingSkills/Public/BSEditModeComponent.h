#pragma once
#include "Components/ActorComponent.h"
#include "Components/Widget.h"
#include "FGDismantleModeDescriptor.h"
#include "BSGroups.h"
#include "BSEditModeComponent.generated.h"

class AFGBuildGun;
class UEnhancedInputComponent;
class UInputAction;
class UBSEditModeWidget;
class UFGBuildGunStateDismantle;
class UFGInteractWidget;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;

UCLASS()
class BUILDINGSKILLS_API UBSGroupModeDescriptor : public UFGDismantleModeDescriptor
{
    GENERATED_BODY()
public: UBSGroupModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSEditModeDescriptor : public UFGDismantleModeDescriptor
{
    GENERATED_BODY()
public: UBSEditModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSEditBlueprintModeDescriptor : public UBSEditModeDescriptor
{
    GENERATED_BODY()
public: UBSEditBlueprintModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSEditGroupModeDescriptor : public UBSEditModeDescriptor
{
    GENERATED_BODY()
public: UBSEditGroupModeDescriptor();
};

/** Shared local selection for Edit and the vanilla dismantle gun. */
UCLASS()
class BUILDINGSKILLS_API UBSEditModeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBSEditModeComponent();
    static void InstallHooks();
    static bool IsEditDescriptor(TSubclassOf<UFGBuildGunModeDescriptor> Mode);
    void InitializeInput(UEnhancedInputComponent* Input);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void OnOperationResult(bool Success, const FBSSelection& Result, const FString& Message, int32 Revision);
    FText GetStatusText() const;
    FText GetControlsText() const;
    TArray<TPair<FText, FText>> GetControlHints() const;
    bool IsEditing() const;
    bool IsTransforming() const { return bTransforming; }
    FVector GetTransformPivot() const { return TransformPivot; }
    EBSTransformMode GetTransformMode() const { return TransformMode; }
    void OnTransformResult(bool Success, const FBSSelection& Result, const FString& Message, int32 Revision);
    void OpenBlueprintSave(class ABSBlueprintSaveSession* Session, const FString& Error);
    void OnBlueprintSaveReply(FGuid RequestId, const FString& Error);
    void BlueprintMessage(const FString& Message) { Feedback = Message; }
private:
    friend class FBSRuntimeSmoke;
    static UBSEditModeComponent* ForGun(AFGBuildGun* Gun);
    void OnToggleTriggered();
    void OnToggleReleased();
    void OnGroup();
    void OnMerge();
    void OnUngroup();
    void OnSave();
    void TickBlueprintSave();
    void CloseBlueprintSave(bool ResumeEdit = true);
    UPROPERTY(Transient) FBSSelection BlueprintSelection;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> SaveContext;
    UPROPERTY(Transient) TObjectPtr<UFGInteractWidget> BlueprintWindow;
    UPROPERTY(Transient) TObjectPtr<class ABSBlueprintSaveSession> BlueprintSession;
    bool bOpeningBlueprint = false;
    FGuid BlueprintRequestId;
    double BlueprintRequestDeadline = 0;
    double BlueprintNextSessionCheck = 0;
    void OnTransform();
    void EndTransform();
    void CycleTransform(int32 Delta);
    void ScrollTransform(int32 Delta);
    void OnMove(int32 Direction);
    void OnMoveReleased(int32 Direction);
    void TickTransform();
    int32 TransformStepTier() const;
    FVector TransformSteps() const;
    bool bTransforming = false;
    bool bWaitingForTransformReplication = false;
    EBSTransformMode TransformMode = EBSTransformMode::RotateX;
    FVector TransformPivot = FVector::ZeroVector;
    double NextMove[6] = {};
    struct FTransformCommand { EBSTransformMode Mode; double Amount; FVector Translation; };
    TArray<FTransformCommand> TransformCommands;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> TransformContext;
    void Request(EBSGroupOperation Operation);
    void LeaveEditMode(bool Unequip);
    void UpdateNativeGunHUD(bool Editing);
    void UpdateControlHints(bool Editing);
    void ReleaseInput();
    void UpdateSelection(UFGBuildGunStateDismantle* State);
    FBSSelection HoverSelection(UFGBuildGunStateDismantle* State);
    FBSSelection EffectiveSelection() const;
    TArray<FBSBuildingRef> ResolveSelection(const FBSSelection& Value) const;
    void SyncDismantle(UFGBuildGunStateDismantle* State);
    void ClearSelection();
    void ReleaseHighlights();
    bool InputAllowed() const;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ToggleAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> MultiSelectAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ModeAction;
    UPROPERTY(Transient) TObjectPtr<UBSEditModeWidget> Indicator;
    UPROPERTY(Transient) TObjectPtr<UUserWidget> ControlHints;
    FString LastControlHints;
    UPROPERTY(Transient) FBSSelection Selection;
    UPROPERTY(Transient) FBSSelection Hover;
    UPROPERTY(Transient) FBSSelection PendingResult;
    UPROPERTY(Transient) TArray<TObjectPtr<AFGBuildable>> Highlighted;
    UPROPERTY(Transient) TObjectPtr<UEnhancedInputComponent> BoundInput;
    TWeakObjectPtr<AFGBuildGun> EditGun;
    TWeakObjectPtr<UFGInteractWidget> NativeGunHUD;
    ESlateVisibility NativeGunHUDVisibility = ESlateVisibility::Visible;
    TWeakObjectPtr<UFGBuildGunStateDismantle> ActiveState;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> ToggleContext;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> OperationsContext;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
    FBSSelection LastHover;
    TWeakObjectPtr<AFGBuildable> HoverActor;
    TWeakObjectPtr<UClass> HoverMode;
    int32 HoverRevision = INDEX_NONE;
    FString Feedback;
    int32 PendingRevision = INDEX_NONE;
    int32 ObservedRevision = INDEX_NONE;
    bool bToggleHeld = false;
    bool bEnteringEdit = false;
    float EditEntryTime = 0.0f;
    bool bMultiHeld = false;
    bool bRemoving = false;
    bool bChangingMode = false;
    bool bNativeTick = false;
    bool bSyncing = false;
    bool bRequestPending = false;
    bool bSelectionDirty = true;
    bool bRefundNoBuildCost = false;
    int32 ResolvedCount = 0;
    int32 ExpectedPendingCount = 0;
    uint64 SelectionRebuilds = 0;
    uint64 RefundRefreshes = 0;
};
