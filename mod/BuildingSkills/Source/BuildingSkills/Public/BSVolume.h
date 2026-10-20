#pragma once
#include "CoreMinimal.h"
#include "UI/FGInteractWidget.h"
#include "BSGroups.h"
#include "BSVolume.generated.h"

USTRUCT(BlueprintType)
struct BUILDINGSKILLS_API FBSVolumeSettings
{
    GENERATED_BODY()
    // Required by SML's writable Blueprint-reflection configuration bridge.
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float VolumeStep = 8;
    UPROPERTY(BlueprintReadWrite, Category="Building Skills") float VolumeRotation = 15;
    static FBSVolumeSettings Get(UWorld* World);
    bool Valid() const;
    static FLinearColor Color();
};

struct BUILDINGSKILLS_API FBSVolumeShape
{
    bool Cube = false;
    double Radius = 800;
    FVector Min = FVector(-800), Max = FVector(800);
    FTransform Frame = FTransform::Identity;
    bool Contains(const FVector& Point) const;
    void Resize(const FVector& View, int32 Delta, double Step, bool Opposite);
    FBox Bounds() const;
    static TArray<FBSBuildingRef> Find(UWorld* World, const FBSVolumeShape& Shape, FString& Error);
};

UCLASS()
class BUILDINGSKILLS_API UBSVolumeSettingsWidget : public UFGInteractWidget
{
    GENERATED_BODY()
public:
    UBSVolumeSettingsWidget(const FObjectInitializer& Initializer);
    FBSVolumeSettings Draft;
    void Save();
    virtual void OnEscapePressed_Implementation() override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override;
};
