#pragma once
#include "Components/ActorComponent.h"
#include "Components/Widget.h"
#include "FGDismantleModeDescriptor.h"
#include "FGFactoryColoringTypes.h"
#include "BSGroups.h"
#include "BSVolume.h"
#include "BSDeduplication.h"
#include "BSStick.h"
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
UCLASS()
class BUILDINGSKILLS_API UBSConnectedModeDescriptor : public UFGDismantleModeDescriptor
{
    GENERATED_BODY()
public: UBSConnectedModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSEditConnectedModeDescriptor : public UBSEditModeDescriptor
{
    GENERATED_BODY()
public: UBSEditConnectedModeDescriptor();
};

// Local choices for the native Build Gun radial; never stored as the gun's replicated mode.
UCLASS()
class BUILDINGSKILLS_API UBSRotateXModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSRotateXModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSRotateYModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSRotateYModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSRotateZModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSRotateZModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSScaleXYZModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSScaleXYZModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSScaleXYModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSScaleXYModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSScaleXZModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSScaleXZModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSScaleYZModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSScaleYZModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSScaleXModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSScaleXModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSScaleYModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSScaleYModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSScaleZModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSScaleZModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSSpreadDefaultModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSSpreadDefaultModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSSpreadConnectedModeDescriptor : public UFGBuildGunModeDescriptor
{
    GENERATED_BODY()
public: UBSSpreadConnectedModeDescriptor();
};

/** Shared local selection for Edit and the vanilla dismantle gun. */
UCLASS()
class BUILDINGSKILLS_API UBSVolumeModeDescriptor : public UFGDismantleModeDescriptor
{
    GENERATED_BODY()
public: UBSVolumeModeDescriptor();
};
UCLASS()
class BUILDINGSKILLS_API UBSEditVolumeModeDescriptor : public UBSEditModeDescriptor
{
    GENERATED_BODY()
public: UBSEditVolumeModeDescriptor();
};

UCLASS()
class BUILDINGSKILLS_API UBSEditModeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    void OnAddTransformer();
    void OnTransformerSettings();
    void CloseTransformerSettings();
    void TickTransformers();
    void ClearTransformerVisuals();
    void OpenConnectedSettings();
    void OpenVolumeSettings();
    UPROPERTY(Transient) TObjectPtr<UBSTransformersWidget> TransformersWindow;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> TransformersContext;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> AddTransformerContext;
    UPROPERTY(Transient) TArray<TObjectPtr<class UStaticMeshComponent>> TransformerMarkers;
    UPROPERTY(Transient) TArray<TObjectPtr<class UStaticMeshComponent>> TransformerPreview;
    double NextTransformerPreview = 0;

    UBSEditModeComponent();
    static void InstallHooks();
    static bool IsEditDescriptor(TSubclassOf<UFGBuildGunModeDescriptor> Mode);
    void InitializeInput(UEnhancedInputComponent* Input);
    bool EnterPlanBuildState();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void RefreshTransformReferences(const TArray<FBSNativeTransform>& Changes);
    void RefreshGroupReferences() { ObservedRevision = HoverRevision = OutlinedRevision = INDEX_NONE; bSelectionDirty = true; }
    void OnOperationResult(bool Success, const FBSSelection& Result, const FString& Message, int32 Revision);
    FText GetStatusText() const;
    FText GetControlsText() const;
    TArray<TPair<FText, FText>> GetControlHints() const;
    bool IsEditing() const;
    bool IsTransforming() const { return bTransforming; }
    TArray<FVector> GetTransformStepValues() const;
    void SetTransformStepValue(int32 Tier, int32 Axis, double Value);
    void CloseTransformSettings();
    bool IsConnectedSelection() const;
    bool IsVolumeSelection() const;
    bool IsBlueprintBuilding() const;
    EBSStickMode StickMode = EBSStickMode::Off;
    double StickDistance = 64;
    double StickTilt = 0;
    bool StickTiltSet = false;
    double StickYaw = 0;
    bool StickYawSet = false;
    bool StickInitialized = false;
    FString StickStatus;
    void CloseStickSettings();
    void TickStick();
    void OnStick();
    void OnStickSettings();
    void ScrollStick(int32 Delta);
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> StickToggleContext;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> StickContext;
    UPROPERTY(Transient) TObjectPtr<UBSStickSettingsWidget> StickWindow;
    EBSDeduplication Deduplication = EBSDeduplication::Off;
    void CloseVolumeSettings();
    FBSVolumeSettings VolumeSettings;
    bool IsPainting() const;
    bool IsSpreadPainting() const;
    bool ToggleConnectedPaint();
    void OnConnectedSettings();
    void CloseConnectedSettings();
    void ConnectedMessage(const FString& Message) { Feedback = Message; HoverActor.Reset(); }
    UPROPERTY(Transient) FBSConnectedSettings ConnectedSettings;
    UPROPERTY(Transient) TObjectPtr<class UBSConnectedSettingsWidget> ConnectedWindow;
    bool bConnectedPaint = false;
    FVector GetTransformPivot() const { return TransformPivot; }
    FQuat GetTransformFrame() const;
    const TArray<FBox>& GetTransformPreview() const { return PreviewBoxes; }
    EBSTransformMode GetTransformMode() const { return TransformMode; }
    UPROPERTY(Transient) TArray<FBSBuildingRef> AwaitedTransforms;
    bool bRequestedTransformSnapshot = false;
    double TransformReplicationDeadline = 0;
    void OnTransformResult(bool Success, const FBSSelection& Result, const FString& Message, int32 Revision);
    void OpenBlueprintSave(class ABSBlueprintSaveSession* Session, const FString& Error);
    void OnBlueprintSaveReply(FGuid RequestId, const FString& Error);
    void BlueprintMessage(const FString& Message);
    void OnCopyReady(FGuid RequestId, class UFGBlueprintDescriptor* Descriptor, const FString& Error);
    bool IsCopyPending(FGuid RequestId) const { return bCopyPending && CopyRequestId == RequestId; }
private:
    friend class UBSGroupRemoteCallObject;
    friend class UBSPlanComponent;
    friend class FBSStick;
    friend class FBSRuntimeSmoke;
    friend class FBSPolishSmoke;
    friend class FBSTransformerSmoke;
    friend class UBSTransformersWidget;
    friend class FBSConnectedSmoke;
    friend class FBSConnectedPaint;
    friend class FBSVolumeSmoke;
    void TickDeduplication();
    void OnDeduplication();
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> DeduplicationContext;
    void TickVolume();
    void OnVolumeShape();
    void OnVolumeSettings();
    void ScrollVolume(int32 Delta);
    FBSSelection VolumeSelection();
    FBSVolumeShape Volume;
    bool bVolumeInitialized = false;
    double NextVolumeQuery = 0;
    UPROPERTY(Transient) FBSSelection VolumeMembers;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> VolumeContext;
    UPROPERTY(Transient) TObjectPtr<UBSVolumeSettingsWidget> VolumeWindow;
    UPROPERTY(Transient) TObjectPtr<class UStaticMeshComponent> VolumeMesh;
    UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> VolumeMaterial;
    void TickConnected();
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> ConnectedContext;
    double NextConnectedRefresh = 0;
    // Extra local previews only; the aimed seed remains owned by the native gun.
    UPROPERTY(Transient) TArray<FBSBuildingRef> ConnectedPaintPreviewMembers;
    TArray<TWeakObjectPtr<AFGBuildable>> ConnectedPaintPreviewActors;
    TArray<TWeakObjectPtr<AActor>> ConnectedPaintPreviewReplacements;
    TArray<bool> ConnectedPaintPreviewOriginalCleanupBlocks;
    UPROPERTY(Transient) TObjectPtr<class UFGOutlineComponent> ConnectedPaintPreviewOutline;
    UPROPERTY(Transient) TArray<FFactoryCustomizationData> ConnectedPaintPreviewOriginalData;
    UPROPERTY(Transient) FBSBuildingRef ConnectedPaintPreviewSeed;
    UPROPERTY(Transient) FFactoryCustomizationData ConnectedPaintPreviewCustomization;
    FBSConnectedSettings ConnectedPaintPreviewSettings;
    TWeakObjectPtr<UClass> ConnectedPaintPreviewRecipe;
    double NextConnectedPaintPreviewRefresh = 0;
    int32 ConnectedPaintPreviewTransformerRevision = -1;
    bool ConnectedPaintPreviewConnected = false;
    static UBSEditModeComponent* ForGun(AFGBuildGun* Gun);
    void OnToggleTriggered();
    void OnToggleReleased();
    void OnGroup();
    void OnMerge();
    void OnUngroup();
    void OnSave();
    void OnCopy();
    void OnBlueprintAnchor();
    void UpdateBlueprintAnchor();
    void ReleaseBlueprintAnchor();
    UPROPERTY(Transient) FBSBuildingRef BlueprintAnchor;
    UPROPERTY(Transient) TObjectPtr<AFGBuildable> BlueprintAnchorActor;
    UPROPERTY(Transient) TObjectPtr<class UFGOutlineComponent> AnchorOutline;
    UPROPERTY(Transient) TObjectPtr<class UPostProcessComponent> AnchorPostProcess;
    UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> AnchorMaterial;
    FTransform OutlinedAnchorTransform;
    bool bAnchorIncluded = false;
    bool bCopyPending = false;
    FGuid CopyRequestId;
    double CopyDeadline = 0;
    void TickBlueprintSave();
    void CloseBlueprintSave(bool ResumeEdit = true);
    UPROPERTY(Transient) FBSSelection BlueprintSelection;
    UPROPERTY(Transient) FBSBuildingRef BlueprintSaveAnchor;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> SaveContext;
    UPROPERTY(Transient) TObjectPtr<UFGInteractWidget> BlueprintWindow;
    UPROPERTY(Transient) TObjectPtr<class ABSBlueprintSaveSession> BlueprintSession;
    bool bOpeningBlueprint = false;
    FGuid BlueprintRequestId;
    double BlueprintRequestDeadline = 0;
    double BlueprintNextSessionCheck = 0;
    void OnTransform();
    void OnTransformSettings();
    UPROPERTY(Transient) TArray<FVector> TemporaryTransformSteps;
    UPROPERTY(Transient) TObjectPtr<class UBSTransformSettingsWidget> TransformSettingsWindow;
    void EndTransform();
    void CycleTransform(int32 Delta);
    void OnPivot();
    void OnAxisSpace();
    void OnAnchor();
    FString TransformPoseText() const;
    void OnUndo();
    void OnActionUndo();
    void OnActionRedo();
    void OnSettings();
    void TickActionHistory();
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> ActionHistoryContext;
    UPROPERTY(Transient) TObjectPtr<UUserWidget> ActionHistoryHint;
    UPROPERTY(Transient) TObjectPtr<UUserWidget> ActionRedoHint;
    FString LastActionHistoryHint;
    double NextActionHistoryHintRefresh = 0;
    void OnRedo();
    void RequestHistory(bool Redo);
    void RefreshTransformFrame(bool KeepPivot = false);
    void RefreshTransformPreview();
    void QueueTransform(EBSTransformMode Mode, double Amount, FVector Translation);
    FBSTransformOptions TransformOptions;
    double LastTransformInput = 0;
    double NextTransformSend = 0;
    UPROPERTY(Transient) TArray<FBSBuildingRef> TransformMembers;
    TArray<FBox> TransformBounds;
    TArray<FBox> PreviewBoxes;
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
    struct FTransformCommand { EBSTransformMode Mode; double Amount; FVector Translation; FBSTransformOptions Options; };
    TOptional<FTransformCommand> InFlightTransform;
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
    void UpdateHoverHighlights();
    UPROPERTY(Transient) TObjectPtr<class UFGOutlineComponent> HoverOutline;
    UPROPERTY(Transient) TArray<TObjectPtr<AFGBuildable>> HoverHighlighted;
    FBSSelection OutlinedHover;
    FBSSelection OutlinedSelection;
    int32 OutlinedRevision = INDEX_NONE;
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
    void UpdateEditColors(bool Editing);
    bool bEditColorsApplied = false;
    FLinearColor PreviousDismantleColor;
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
    double GroupRequestDeadline = 0;
    bool bSelectionDirty = true;
    bool bRefundNoBuildCost = false;
    int32 ResolvedCount = 0;
    int32 ExpectedPendingCount = 0;
    uint64 SelectionRebuilds = 0;
    uint64 RefundRefreshes = 0;
};
