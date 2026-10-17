#pragma once
#include "CoreMinimal.h"
#include "BSGroups.h"
class AFGBlueprintHologram;
class AFGBuildable;

enum class EBSDeduplication : uint8 { Off, Replace, Skip };
struct BUILDINGSKILLS_API FBSDeduplication
{
    static void Install();
    static bool SameBounds(const FBox& A, const FTransform& AT, const FBox& B, const FTransform& BT);
    static bool SameType(UWorld* World, UClass* A, UClass* B);
    static bool SkipObject(const FString& Path);
    static FString SourcePath(AFGBuildable* Building);
    static void RecordSource(AFGBuildable* Building, const FString& Path, bool Preview);
    static EBSDeduplication Mode(const AFGBlueprintHologram* Hologram);
    static bool DeferGroups(TFunction<void()> Restore);
};
