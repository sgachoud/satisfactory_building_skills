#pragma once
#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "UI/FGInteractWidget.h"
#include "BSStick.generated.h"

UENUM()
enum class EBSStickMode : uint8 { Off, Default, Constrained };

USTRUCT()
struct BUILDINGSKILLS_API FBSStickSettings
{
    GENERATED_BODY()
    UPROPERTY() float StickDistance = 64;
    UPROPERTY() float StickStep = 1;
    UPROPERTY() float StickTiltStep = 15;
    UPROPERTY() float StickSlope = 0;
    // Signed horizontal radius in meters. Zero means no horizontal turn.
    UPROPERTY() float StickRadius = 0;
    // K-menu switches are session-only; disabled values are retained.
    bool VerticalConstraint = true;
    bool HorizontalConstraint = true;
    bool Valid() const;
    static FBSStickSettings Get(UWorld* World);
};

// World lifetime, rather than equipment or pawn lifetime: respawns and gun
// changes retain temporary settings, leaving the save discards them.
UCLASS()
class BUILDINGSKILLS_API UBSStickSession : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    FBSStickSettings Settings;
    // Bound only by the isolated runtime regression.
    UFUNCTION() void ObserveConstruction(class AFGBuildable* Building);
    TArray<TWeakObjectPtr<class AFGBuildable>> ObservedConstruction;
};

struct BUILDINGSKILLS_API FBSStickPath
{
    FVector Start = FVector::ZeroVector;
    FVector Forward = FVector::ForwardVector;
    double Slope = 0; // Radians, positive uphill, relative to horizontal.
    double Radius = 0; // Signed centimeters; zero means straight.
    // Configure the enabled constraints and return the closest horizontal distance.
    double ResolveConstraints(const FBSStickSettings& Settings, const FVector& Target);
    FVector Position(double HorizontalDistance) const;
    FVector Derivative(double HorizontalDistance) const;
    double Closest(const FVector& Target) const;
    bool Segments(double Distance, double MaxLength, TArray<TArray<FSplinePointData>>& Out) const;
    static FVector EndpointDirection(const FVector& NativeTangent, bool OverridePitch, double Pitch, double YawOffset);
    static bool FitEndDirections(TArray<TArray<FSplinePointData>>& Parts, const FVector& StartDirection, const FVector& EndDirection, double MaxLength);
    static bool Split(const TArray<FSplinePointData>& Points, double MaxLength,
        TArray<TArray<FSplinePointData>>& Out);
};
class BUILDINGSKILLS_API FBSStick
{
public:
    static void Install();
    static void InstallRailCompatibility();
    static class AFGSplineHologram* Active(class UBSEditModeComponent* Edit);
    static void Restore(class AFGSplineHologram* Hologram, bool RemoveChildren = true);
    static void Update(class AFGSplineHologram* Hologram, class UBSEditModeComponent* Edit, const FVector& Target);
    static void SetPoints(class AFGSplineHologram* Hologram, const TArray<FSplinePointData>& Points);
    static double MaxLength(class AFGSplineHologram* Hologram);
    static bool ApplySegments(class AFGSplineHologram* Hologram, const TArray<TArray<FSplinePointData>>& Segments);
    static void ResizeChildren(class AFGSplineHologram* Hologram, int32 Count);
    static void Validate(class AFGSplineHologram* Hologram);
    static bool Connect(class AActor* A, class AActor* B, const FVector& At);
    static bool RunChecks(class AFGCharacterPlayer* Player, int32 Phase);
};

UCLASS()
class BUILDINGSKILLS_API UBSStickSettingsWidget : public UFGInteractWidget
{
    GENERATED_BODY()
public:
    UBSStickSettingsWidget(const FObjectInitializer& Initializer);
    FBSStickSettings Draft;
    virtual void OnEscapePressed_Implementation() override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override;
};
