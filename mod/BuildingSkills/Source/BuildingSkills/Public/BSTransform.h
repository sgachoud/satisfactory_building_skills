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

USTRUCT()
struct BUILDINGSKILLS_API FBSTransformSettings
{
    GENERATED_BODY()
    UPROPERTY() float ActionHistorySize = 32;
    UPROPERTY() float Move = 100;
    UPROPERTY() float TinyMove = 20;
    UPROPERTY() float SmallMove = 50;
    UPROPERTY() float LargeMove = 250;
    UPROPERTY() float Rotate = 15;
    UPROPERTY() float TinyRotate = 1;
    UPROPERTY() float SmallRotate = 5;
    UPROPERTY() float LargeRotate = 45;
    UPROPERTY() float Scale = 1.1f;
    UPROPERTY() float TinyScale = 1.01f;
    UPROPERTY() float SmallScale = 2;
    UPROPERTY() float LargeScale = 11;
    UPROPERTY() float RepeatDelay = 0.15f;
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
