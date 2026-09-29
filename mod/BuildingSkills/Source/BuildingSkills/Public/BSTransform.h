#pragma once
#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "BSTransform.generated.h"

UENUM()
enum class EBSTransformMode : uint8 { RotateX, RotateY, RotateZ, RotateXYZ, ScaleXY, ScaleXZ, ScaleYZ, ScaleX, ScaleY, ScaleZ };

USTRUCT()
struct BUILDINGSKILLS_API FBSTransformSettings
{
    GENERATED_BODY()
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
    static FVector ViewTranslation(float Yaw, int32 Direction);
};
