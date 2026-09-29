#pragma once

#include "CoreMinimal.h"
#include "FGSaveInterface.h"
#include "FGRemoteCallObject.h"
#include "Subsystem/ModSubsystem.h"
#include "BSTransform.h"
#include "Components/SplineComponent.h"
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

USTRUCT()
struct FBSNativeTransform
{
    GENERATED_BODY()
    UPROPERTY() FBSBuildingRef Building;
    UPROPERTY() FTransform Target;
    UPROPERTY() int32 InstanceIndex = INDEX_NONE;
    UPROPERTY() TArray<FSplinePointData> Spline;
    UPROPERTY() bool HasLift = false;
    UPROPERTY() FTransform LiftTop;
    UPROPERTY() bool HasWire = false;
    UPROPERTY() FVector WireStart = FVector::ZeroVector;
    UPROPERTY() FVector WireEnd = FVector::ZeroVector;
    UPROPERTY() TArray<FVector> WirePoints;
};

class BUILDINGSKILLS_API FBSTransformNative
{
public:
    static bool Apply(UWorld* World, const FBSNativeTransform& Change);
    static bool Supports(AFGBuildable* Actor, FString& Error);
    static bool PlanNetworks(TArray<FBSNativeTransform>& Changes, FString& Error);
    static FBSNativeTransform Capture(AFGBuildable* Actor);
    static void PrepareNetworks(UWorld* World, const TArray<FBSNativeTransform>& Changes);
    static void ApplyGeometry(AFGBuildable* Actor, const FBSNativeTransform& Change, const FTransform& Before);
    static void RefreshNetworks(UWorld* World, const TArray<FBSNativeTransform>& Changes);
    static void RebuildSpline(AFGBuildable* Actor);
    static bool RunNetworkChecks(AFGCharacterPlayer* Player);
    static bool RunSettledNetworkChecks(AFGCharacterPlayer* Player, int32 Phase);
};

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
    void InvalidateLightweightLookup(TSubclassOf<AFGBuildable> Class) { LightweightLookup.Remove(Class); LightweightCounts.Remove(Class); }
    bool Execute(AFGCharacterPlayer* Player, EBSGroupOperation Operation, const FBSSelection& Selection,
        int32 ExpectedRevision, FBSSelection& Result, FString& Error);
    bool TransformSelection(AFGCharacterPlayer* Player, const FBSSelection& Selection, int32 ExpectedRevision,
        EBSTransformMode Mode, double Amount, FVector Translation, FBSSelection& Result, FString& Error);
    UFUNCTION(NetMulticast, Reliable) void Multicast_Transforms(const TArray<FBSNativeTransform>& Changes);
    UFUNCTION() void OnRep_TransformSnapshot();
    virtual void Tick(float DeltaSeconds) override;
    // Native actors/instance records save their own transforms. This transient
    // snapshot only brings late-joining clients up to date in this session.
    UPROPERTY(Transient, ReplicatedUsing=OnRep_TransformSnapshot) TArray<FBSNativeTransform> TransformSnapshot;
    UPROPERTY(Transient) TArray<FBSNativeTransform> PendingTransforms;
    UPROPERTY(SaveGame, Replicated) TArray<FBSGroup> Groups;
    UPROPERTY(SaveGame) int32 SchemaVersion = 1;
    UPROPERTY(Replicated) int32 Revision = 0;
private:
    // Keep spatial buckets until native instance membership changes.
    mutable TMap<TSubclassOf<AFGBuildable>, int32> LightweightCounts;
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
    UFUNCTION(Server, Reliable) void Server_OpenBlueprintSave(FGuid Group, int32 ExpectedRevision, FGuid RequestId);
    UFUNCTION(Client, Reliable) void Client_OpenBlueprintSave(FGuid RequestId, const FString& Error);
    UFUNCTION(Server, Reliable) void Server_CloseBlueprintSave(FGuid RequestId);
    UFUNCTION(Client, Reliable) void Client_BlueprintMessage(const FString& Message);
    UFUNCTION(Server, Reliable)
    void Server_Apply(EBSGroupOperation Operation, const FBSSelection& Selection, int32 ExpectedRevision);
    UFUNCTION(Client, Reliable)
    void Client_Result(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision);
    UFUNCTION(Server, Reliable)
    void Server_Transform(const FBSSelection& Selection, int32 ExpectedRevision, EBSTransformMode Mode, double Amount, FVector Translation);
    UFUNCTION(Client, Reliable)
    void Client_TransformResult(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision);
};
