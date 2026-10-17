#pragma once
#include "CoreMinimal.h"

class AFGBlueprintSubsystem;
class UFGBlueprintDescriptor;
class AFGBuildable;
class AFGCharacterPlayer;
struct FBlueprintHeader;
struct FBlueprintRecord;
struct FBlueprintSaveData;
struct FActorSaveHeader;

void InstallBuildingSkillsBlueprintScale();
bool ReadBuildingSkillsBlueprintHeaders(const FBlueprintSaveData& Data, TArray<FActorSaveHeader>& Headers);
bool RestoreBuildingSkillsBlueprintArchiveScales(AFGBlueprintSubsystem* System);
bool RunBuildingSkillsBlueprintScaleChecks(AFGCharacterPlayer* Player);
// Available only inside the opt-in isolated runtime test. Exercises the original
// loader against the same file to check compatibility without scale restoration.
void LoadBuildingSkillsBlueprintVanillaForTest(AFGBlueprintSubsystem* System,
    UFGBlueprintDescriptor* Descriptor, TArray<AFGBuildable*>& Buildings);
FBlueprintHeader WriteBuildingSkillsBlueprintForTest(AFGBlueprintSubsystem* System,
    const FBlueprintRecord& Record, const FTransform& Origin, const TArray<AFGBuildable*>& Buildings);

bool WriteBuildingSkillsClipboard(AFGBlueprintSubsystem* System, const FBlueprintRecord& Record,
    const FTransform& Origin, const TArray<AFGBuildable*>& Buildings, FIntVector Dimensions, FBlueprintSaveData& Out);
bool ReadBuildingSkillsBlueprintObjectPaths(const FBlueprintSaveData& Data, TMap<int32, FString>& Paths);
bool TestBuildingSkillsArchiveObjects(AFGBuildable* Selected, UObject* Dependency);
