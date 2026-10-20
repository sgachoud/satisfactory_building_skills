#pragma once
#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "BSTransform.generated.h"

UENUM()
enum class EBSTransformMode : uint8 { RotateX, RotateY, RotateZ, ScaleXYZ, ScaleXY, ScaleXZ, ScaleYZ, ScaleX, ScaleY, ScaleZ };

UENUM()
enum class EBSTransformPivot : uint8 { Origins, Bounds, Anchor, Individual };
UENUM()
enum class EBSTransformSpace : uint8 { World, Local, Anchor };
USTRUCT()
struct BUILDINGSKILLS_API FBSTransformOptions
{
    GENERATED_BODY()
    UPROPERTY() EBSTransformPivot Pivot = EBSTransformPivot::Origins;
    UPROPERTY() EBSTransformSpace Space = EBSTransformSpace::Local;
    UPROPERTY() FQuat AnchorFrame = FQuat::Identity;
    UPROPERTY() int32 AnchorIndex = 0;
    UPROPERTY() FGuid Gesture;
    UPROPERTY() bool UseFixedPivot = false;
    UPROPERTY() FVector FixedPivot = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct BUILDINGSKILLS_API FBSTransformSettings
{
    GENERATED_BODY()
    // SML's config reflection ignores plain UPROPERTY fields.
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float ConnectedGap = 0;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float ConnectedLimit = 64;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float ConnectedScope = 3;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float PlanSegmentLength = 80;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float PlanRemainder = 1;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float PlanSignalEvery = 1;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") bool ConnectedIgnoreSwatch = false;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") bool ConnectedIgnoreMaterial = false;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") bool VerticalConstraint = true;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") bool HorizontalConstraint = true;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") bool PlanPlaceBlockSignal = false;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") bool PlanLeftSideSignal = false;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") FString ConnectedWhitelist;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") FString ConnectedBlacklist;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float ActionHistorySize = 32;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float Move = 100;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float TinyMove = 20;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float SmallMove = 50;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float LargeMove = 250;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float Rotate = 15;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float TinyRotate = 1;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float SmallRotate = 5;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float LargeRotate = 45;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float Scale = 1.1f;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float TinyScale = 1.01f;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float SmallScale = 2;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float LargeScale = 11;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float RepeatDelay = 0.15f;
    static FBSTransformSettings Get(UWorld* World);
};

UCLASS()
class BUILDINGSKILLS_API UBSTransformConfiguration : public UModConfiguration
{
    GENERATED_BODY()
public:
    UBSTransformConfiguration();
};

/** Pure transform math, shared by the local controls and authoritative server. */
struct BUILDINGSKILLS_API FBSTransformMath
{
    static FVector Axes(EBSTransformMode Mode);
    static FString Name(EBSTransformMode Mode);
    static FTransform Apply(const FTransform& Original, const FVector& Pivot, EBSTransformMode Mode, double Amount, const FVector& Translation);
    static FTransform ApplyInFrame(const FTransform& Original, const FVector& Pivot, const FQuat& Frame, EBSTransformMode Mode, double Amount, const FVector& Translation);
    static FVector ViewTranslation(float Yaw, int32 Direction);
    static FVector FrameTranslation(const FQuat& Frame, const FVector& ViewDirection, int32 Direction);
    static bool MatchesInitialReplication(const FTransform& Authority, const FTransform& Observed);
    // Combine compatible queued steps without converting cancelled movement
    // into rotation/scaling (zero translation is the wire protocol's sentinel).
    static bool CombineSteps(EBSTransformMode Mode, double& Amount, FVector& Translation,
        double NextAmount, const FVector& NextTranslation, bool& Cancelled);
};
