#pragma once

#include "CoreMinimal.h"
#include "FGSaveInterface.h"
#include "FGRemoteCallObject.h"
#include "Subsystem/ModSubsystem.h"
#include "BSGroups.generated.h"

class AFGBuildable;
class UFGRecipe;
class AFGCharacterPlayer;
class AFGBlueprintProxy;

/** Actor references are saved by the game. Lightweight indices are ONLY runtime hints.
 * On reload we resolve a unique class/recipe/transform fingerprint, never an old index.
 * Ambiguous overlapping instances fail closed instead of adopting an unrelated building. */
USTRUCT()
struct BUILDINGSKILLS_API FBSBuildingRef
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) TObjectPtr<AFGBuildable> Actor = nullptr;
    UPROPERTY(SaveGame) TSubclassOf<AFGBuildable> LightweightClass;
    UPROPERTY(SaveGame) TSubclassOf<UFGRecipe> Recipe;
    UPROPERTY(SaveGame) FTransform Transform;
    UPROPERTY(Transient) int32 RuntimeIndex = INDEX_NONE;

    static FBSBuildingRef FromActor(AFGBuildable* Building);
    static FBSBuildingRef FromLightweight(UWorld* World, TSubclassOf<AFGBuildable> Class, int32 Index);
    int32 ResolveIndex(UWorld* World) const;
    bool IsAlive(UWorld* World) const;
    AFGBuildable* ResolveActor(UWorld* World) const;
    AFGBlueprintProxy* Blueprint(UWorld* World) const;
    FVector Location() const;
    bool operator==(const FBSBuildingRef& Other) const;
};

USTRUCT()
struct BUILDINGSKILLS_API FBSGroup
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FGuid Id;
    UPROPERTY(SaveGame) FString Name;
    UPROPERTY(SaveGame) FGuid Parent;
    UPROPERTY(SaveGame) TArray<FBSBuildingRef> Buildings;
    UPROPERTY(SaveGame) FVector Pivot = FVector::ZeroVector;
};

USTRUCT()
struct BUILDINGSKILLS_API FBSSelection
{
    GENERATED_BODY()
    UPROPERTY() TArray<FGuid> Groups;
    UPROPERTY() TArray<FBSBuildingRef> Buildings;
    bool IsEmpty() const { return Groups.IsEmpty() && Buildings.IsEmpty(); }
};

UENUM()
enum class EBSGroupOperation : uint8 { Group, Merge, Ungroup };

/** World-independent hierarchy operations, also used by automation tests. */
struct BUILDINGSKILLS_API FBSGroupTree
{
    static const FBSGroup* Find(const TArray<FBSGroup>& Tree, FGuid Id);
    static bool IsWithin(const TArray<FBSGroup>& Tree, FGuid Child, FGuid Ancestor);
    static FGuid Owner(const TArray<FBSGroup>& Tree, const FBSBuildingRef& Building);
    static FGuid Root(const TArray<FBSGroup>& Tree, FGuid Id);
    static void Collect(const TArray<FBSGroup>& Tree, FGuid Id, TArray<FBSBuildingRef>& Out);
    static FBSSelection Normalize(const TArray<FBSGroup>& Tree, const FBSSelection& Selection);
    static bool Apply(TArray<FBSGroup>& Tree, EBSGroupOperation Operation, const FBSSelection& Selection,
        FBSSelection& Result, FString& Error);
    static void PruneEmpty(TArray<FBSGroup>& Tree);
    static bool Validate(const TArray<FBSGroup>& Tree);
};

UCLASS()
class BUILDINGSKILLS_API ABSGroupSubsystem : public AModSubsystem, public IFGSaveInterface
{
    GENERATED_BODY()
public:
    ABSGroupSubsystem();
    static ABSGroupSubsystem* Get(UWorld* World);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual bool ShouldSave_Implementation() const override { return true; }
    virtual bool NeedTransform_Implementation() override { return false; }
    virtual void GatherDependencies_Implementation(TArray<UObject*>& Out) override;
    virtual void PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
    virtual void PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
    void RemoveLightweight(TSubclassOf<AFGBuildable> Class, int32 Index);
    void RemoveBuilding(AFGBuildable* Building);
    int32 FindLightweightIndex(const FBSBuildingRef& Ref) const;
    bool Execute(AFGCharacterPlayer* Player, EBSGroupOperation Operation, const FBSSelection& Selection,
        int32 ExpectedRevision, FBSSelection& Result, FString& Error);
    UPROPERTY(SaveGame, Replicated) TArray<FBSGroup> Groups;
    UPROPERTY(SaveGame) int32 SchemaVersion = 1;
    UPROPERTY(Replicated) int32 Revision = 0;
private:
    // Build spatial buckets once per frame/class, not once per selected building.
    mutable uint64 LookupFrame = MAX_uint64;
    mutable TMap<TSubclassOf<AFGBuildable>, TMultiMap<FIntVector, int32>> LightweightLookup;
};

/** Owned by the player controller; all hierarchy mutations are validated on the server. */
UCLASS()
class BUILDINGSKILLS_API UBSGroupRemoteCallObject : public UFGRemoteCallObject
{
    GENERATED_BODY()
public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(Replicated) bool ReplicationAnchor = true;
    UFUNCTION(Server, Reliable)
    void Server_Apply(EBSGroupOperation Operation, const FBSSelection& Selection, int32 ExpectedRevision);
    UFUNCTION(Client, Reliable)
    void Client_Result(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision);
};
