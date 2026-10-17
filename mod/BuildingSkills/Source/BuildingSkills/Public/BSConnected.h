#pragma once
#include "BSGroups.h"
#include "BSConnectedSettings.h"

/** Reusable snapshot search, independent of gun state and UI. */
struct FBSConnectedSearchStats
{
    int32 SpatialQueries = 0;
    int32 CandidatesExamined = 0;
};

class BUILDINGSKILLS_API FBSConnected
{
public:
    static TArray<FBSBuildingRef> Find(UWorld* World, const FBSBuildingRef& Seed,
        const FBSConnectedSettings& Settings, FString& Message, FBSConnectedSearchStats* Stats = nullptr);
    static TArray<int32> Traverse(const TArray<FBox>& Boxes, int32 Seed, double GapMeters, int32 Limit, bool& Truncated);
    static FString BuildingName(UClass* Class, TSubclassOf<UFGRecipe> Recipe);
    static bool NameMatches(const FString& Name, const FBSConnectedSettings& Settings, FString& Error);
};

class FBSConnectedPaint
{
public:
    static void Install();
    static void ClearPreview(class UBSEditModeComponent* Edit, class AFGBuildable* PreserveNativeTarget = nullptr);
    static void RemovePreview(class UBSEditModeComponent* Edit, int32 Index, class AFGBuildable* PreserveNativeTarget = nullptr);
    static void TickPreview(class UBSEditModeComponent* Edit, class UFGBuildGunStatePaint* Paint);
    static void Execute(class UBSGroupRemoteCallObject* RCO, const FBSBuildingRef& Seed,
        const FBSConnectedSettings& Settings, TSubclassOf<class UFGCustomizationRecipe> Recipe, bool Connected = true);
};
