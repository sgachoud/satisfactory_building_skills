#pragma once
#include "CoreMinimal.h"
#include "BSGroups.h"
#include "FGFactoryBlueprintTypes.h"
#include "FGInventoryComponent.h"
#include "FGConveyorItem.h"
#include "BSActionHistory.generated.h"

USTRUCT()
struct FBSUndoInventory
{
    GENERATED_BODY()
    UPROPERTY() FString Path;
    UPROPERTY() TArray<FInventoryStack> Slots;
};
USTRUCT()
struct FBSUndoPort
{
    GENERATED_BODY()
    UPROPERTY() FString Path;
    UPROPERTY() TWeakObjectPtr<UActorComponent> External;
    UPROPERTY() FString Internal;
    // -1: ordinary port; 0/1: wire end; 2/3: guarded/observed rail-signal port.
    UPROPERTY() int32 WireEnd = INDEX_NONE;
    UPROPERTY() FTransform ExternalTransform;
};
USTRUCT()
struct FBSUndoContents
{
    GENERATED_BODY()
    UPROPERTY() FString Path;
    UPROPERTY() TArray<FBSUndoInventory> Inventories;
    UPROPERTY() TArray<FConveyorBeltItem> Conveyor;
    UPROPERTY() float FluidContent = -1;
    UPROPERTY() TSubclassOf<UFGItemDescriptor> FluidDescriptor;
};
USTRUCT()
struct FBSUndoBlueprint
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<AFGBlueprintProxy> Existing;
    UPROPERTY() FTransform Transform;
    UPROPERTY() FBox Bounds;
    UPROPERTY() FText Name;
    UPROPERTY() TArray<int32> Members;
};
/** One authoritative gun action, retained only for this player's current session. */
UCLASS()
class BUILDINGSKILLS_API UBSActionRecord : public UObject
{
    GENERATED_BODY()
public:
    virtual UWorld* GetWorld() const override { return GetOuter() ? GetOuter()->GetWorld() : nullptr; }
    UPROPERTY() TArray<FBSBuildingRef> Members;
    UPROPERTY() TArray<FBSGroup> Groups;
    UPROPERTY() TArray<FBSUndoBlueprint> Blueprints;
    UPROPERTY() TArray<FBSUndoContents> Contents;
    UPROPERTY() TArray<FBSUndoPort> Ports;
    UPROPERTY() TArray<FInventoryStack> Refund;
    UPROPERTY() TArray<FString> Paths;
    UPROPERTY() TArray<FString> ActorNames;
    UPROPERTY() TArray<TWeakObjectPtr<UFGInventoryComponent>> RefundInventories;
    UPROPERTY() TObjectPtr<UFGBlueprintDescriptor> Descriptor;
    FBlueprintSaveData Data;
    FString Error;
    bool Deletion = false;
    bool Capture(AFGCharacterPlayer* Player, const TArray<FBSBuildingRef>& Buildings);
    bool Restore(AFGCharacterPlayer* Player, FString& Message);
};
class BUILDINGSKILLS_API FBSActionHistory
{
public:
    static void Install();
    static void Record(UBSGroupRemoteCallObject* RCO, UBSActionRecord* Entry);
    static bool Undo(UBSGroupRemoteCallObject* RCO, bool Deletion, FString& Message, bool Redo=false);
    static int32 Limit(UWorld* World);
};
