#pragma once
#include "CoreMinimal.h"
#include "UI/FGInteractWidget.h"
#include "BSTransformers.generated.h"

USTRUCT()
struct BUILDINGSKILLS_API FBSTransformer
{
    GENERATED_BODY()
    UPROPERTY() FGuid Id;
    UPROPERTY() FTransform Frame;
    UPROPERTY() FTransform OriginalAnchor;
    UPROPERTY() bool RelativeToOriginalAnchor = false;
    UPROPERTY() bool Rotation = false;
    UPROPERTY() bool Mirror = true;
    UPROPERTY() double Angle = 90;
    UPROPERTY() double Radius = 64; // meters
    UPROPERTY() FLinearColor Color = FLinearColor(.65f,.15f,1.f,1.f);
    UPROPERTY() bool RepeatPlayer = true;
    UPROPERTY() bool RepeatTransformers = false;
    bool Valid() const;
    bool Contains(const FVector& Center) const;
    FVector Point(const FVector& World) const;
    FVector Vector(const FVector& World) const;
    FTransform Pose(const FTransform& World, const FVector& LocalForward = FVector::ForwardVector) const;
    FTransform DisplayFrame() const;
    void SetDisplayFrame(const FTransform& Value);
};

// Each node is one whole placement; its parent is always earlier in this array.
struct FBSTransformerNode
{
    int32 Parent = INDEX_NONE;
    int32 Transformer = INDEX_NONE;
    FVector Center = FVector::ZeroVector;
};

class BUILDINGSKILLS_API FBSTransformers
{
public:
    static constexpr int32 MaxTransformers = 16;
    static constexpr int32 MaxCopies = 64;
    static constexpr int32 MaxBuildings = 2048;
    static TArray<FBSTransformerNode> Plan(const TArray<FBSTransformer>& List, const FVector& Center, bool& Limited);
    static void Install();
    static FVector MirrorForward(const AActor* Building);
    static FTransform BuildingPose(const FBSTransformer& Rule, const FTransform& Pose, const AActor* Building);
    static bool ReconnectMirrorPorts(const FBSTransformer& Rule,
        const TMap<class AFGBuildable*, class AFGBuildable*>& Placed, FString& Error);
    static void Replicate(class UBSGroupRemoteCallObject* Owner, const TArray<struct FBSBuildingRef>& Original);
    static bool BuildAsBlueprint(class UBSGroupRemoteCallObject* Owner, const TArray<struct FBSBuildingRef>& Buildings);
    static TArray<struct FBSBuildingRef> Matching(UWorld* World, const TArray<FBSTransformer>& Rules,
        const TArray<struct FBSBuildingRef>& Original, FString& Message);
    static bool ExpandTransforms(UWorld* World, const TArray<FBSTransformer>& Rules,
        TArray<struct FBSNativeTransform>& Changes, FString& Message);
    static bool Copy(class UBSGroupRemoteCallObject* Owner, const TArray<struct FBSBuildingRef>& Source,
        const FBSTransformer& Rule, TArray<struct FBSBuildingRef>& Result, FString& Error);
};

UCLASS()
class BUILDINGSKILLS_API UBSTransformersWidget : public UFGInteractWidget
{
    GENERATED_BODY()
public:
    UBSTransformersWidget(const FObjectInitializer& Initializer);
    UPROPERTY(Transient) TArray<FBSTransformer> Draft;
    bool BuildAsBlueprint = false;
    int32 Selected = 0;
    int32 Revision = 0;
    virtual void OnEscapePressed_Implementation() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    TSharedPtr<class SVerticalBox> ListBox;
    void RebuildList();
    FBSTransformer* Current() { return Draft.IsValidIndex(Selected) ? &Draft[Selected] : nullptr; }
};
