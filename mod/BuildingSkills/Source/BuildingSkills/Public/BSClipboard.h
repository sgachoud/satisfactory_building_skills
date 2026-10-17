#pragma once
#include "CoreMinimal.h"
#include "BSGroups.h"
#include "FGFactoryBlueprintTypes.h"
#include "BSClipboard.generated.h"

/** A server-authored placement snapshot. Never registered in the blueprint library or saved. */
UCLASS()
class BUILDINGSKILLS_API UBSClipboard : public UObject
{
    GENERATED_BODY()
public:
    static void InstallHooks();
    static void PreviewMemberCost(class AFGBlueprintSubsystem* System, AFGBuildable* Building, TArray<FItemAmount>& Cost);
    virtual UWorld* GetWorld() const override { return GetOuter() ? GetOuter()->GetWorld() : nullptr; }
    bool Capture(const FBSSelection& Selection, FString& Error, const FBSBuildingRef& Anchor = {}, bool RequireReach = true);
    bool Decode(FString& Error);
    void MakeDescriptor();
    FString Name() const;
    FGuid RequestId;
    FBlueprintSaveData Data;
    TArray<uint8> Bytes;
    int32 NextOffset = 0;
    UPROPERTY(Transient) TObjectPtr<class UFGBlueprintDescriptor> Descriptor;
    UPROPERTY(Transient) TArray<FBSGroup> Hierarchy;
    // Source member -> native archive reference, retained only on authority.
    TArray<FString> MemberPaths;
    UPROPERTY(Transient) TArray<FBSBuildingRef> Members;
    static UBSClipboard* Find(UWorld* World, const FString& Name);
    void RestoreGroups(const TMap<FString, FBSBuildingRef>& Placed, const FTransform& Origin);
    FTransform SourceOrigin;
};
