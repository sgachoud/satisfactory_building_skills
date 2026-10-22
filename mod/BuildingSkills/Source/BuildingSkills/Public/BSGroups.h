#pragma once

#include "CoreMinimal.h"
#include "FGSaveInterface.h"
#include "FGRemoteCallObject.h"
#include "Subsystem/ModSubsystem.h"
#include "BSTransform.h"
#include "BSConnectedSettings.h"
#include "BSTransformers.h"
#include "BSPlan.h"
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
    // Session-only concurrency token; never written into game saves.
    UPROPERTY(Transient) uint64 TransformVersion = 0;
    // Snapshot identity survives receipt before the native actor becomes relevant.
    UPROPERTY(Transient) uint64 NetActorId = 0;
    AFGBuildable* ResolveNetworkActor(UWorld* World) const;

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
struct FBSGroupFragment
{
    GENERATED_BODY()
    UPROPERTY() FBSGroup Group;
    UPROPERTY() int32 Offset = 0;
    UPROPERTY() int32 TotalMembers = 0;
};

struct BUILDINGSKILLS_API FBSGroupTransfer
{
    static bool Pack(const TArray<FBSGroup>& Tree, int32& Group, int32& Member, TArray<FBSGroupFragment>& Out);
    static bool Append(TArray<FBSGroup>& Tree, int32& ExpectedMembers, const TArray<FBSGroupFragment>& Batch, int32 TotalGroups, bool Final);
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

// 0..2: group operations; 3: Save; 4: Copy; 5: group result.
USTRUCT()
struct FBSSelectionPacket
{
    GENERATED_BODY()
    UPROPERTY() FGuid Request;
    UPROPERTY() uint8 Action = 0;
    UPROPERTY() int32 Revision = 0;
    UPROPERTY() int32 TotalGroups = 0;
    UPROPERTY() int32 TotalBuildings = 0;
    UPROPERTY() int32 Offset = 0;
    UPROPERTY() FBSSelection Items;
    UPROPERTY() FBSBuildingRef Anchor;
    UPROPERTY() bool Success = false;
    UPROPERTY() FString Message;
};

struct BUILDINGSKILLS_API FBSSelectionTransfer
{
    static constexpr int32 BatchSize = 32;
    static bool Pack(const FBSSelection& Source, int32 Offset, FBSSelection& Batch);
    static bool Append(FBSSelectionPacket& Received, const FBSSelectionPacket& Packet);
    static bool Complete(const FBSSelectionPacket& Packet)
    { return Packet.Items.Groups.Num()==Packet.TotalGroups && Packet.Items.Buildings.Num()==Packet.TotalBuildings; }
};

USTRUCT()
struct FBSNativeTransform
{
    GENERATED_BODY()
    UPROPERTY() FBSBuildingRef Building;
    UPROPERTY() FTransform Target;
    UPROPERTY() uint64 Serial = 0;
    UPROPERTY() bool HasBaseline = false;
    UPROPERTY() FTransform Baseline;
    UPROPERTY() int32 InstanceIndex = INDEX_NONE;
    UPROPERTY() TArray<FSplinePointData> Spline;
    UPROPERTY() bool HasLift = false;
    UPROPERTY() FTransform LiftTop;
    UPROPERTY() bool HasWire = false;
    UPROPERTY() FVector WireStart = FVector::ZeroVector;
    UPROPERTY() FVector WireEnd = FVector::ZeroVector;
    UPROPERTY() TArray<FVector> WirePoints;
};

USTRUCT()
struct FBSTransformHistory
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<AFGCharacterPlayer> Player;
    UPROPERTY() FGuid Gesture;
    UPROPERTY() TArray<FBSNativeTransform> Before;
    UPROPERTY() TArray<FBSNativeTransform> After;
    UPROPERTY() FBSSelection Selection;
    TArray<uint64> Versions;
    uint32 Connections = 0;
    double Time = 0;
    bool Undone = false;
};

class BUILDINGSKILLS_API FBSTransformNative
{
public:
    static bool UpdateReference(FBSBuildingRef& Ref, const FBSNativeTransform& Change, bool CatchUp = false);
    static void UpdateGroupTransforms(TArray<FBSGroup>& Tree, const TArray<FBSNativeTransform>& Changes, bool CatchUp = false);
    static FBox Bounds(UWorld* World, const FBSBuildingRef& Ref);
    static void QueryLightweightBounds(UWorld* World, const FBox& Box, TArray<FBSBuildingRef>& Out, int32& Examined);
    static uint64 Key(const FBSBuildingRef& Ref, int32 Index = INDEX_NONE);
    static bool Matches(UWorld* World, const FBSNativeTransform& State);
    static uint32 Connections(const TArray<FBSNativeTransform>& Changes);
    static bool Apply(UWorld* World, const FBSNativeTransform& Change);
    static bool Supports(AFGBuildable* Actor, FString& Error);
    static TArray<double> SplineFractions(const TArray<FSplinePointData>& Points, const FTransform& Transform);
    static bool PlanNetworks(TArray<FBSNativeTransform>& Changes, FString& Error);
    static FBSNativeTransform Capture(AFGBuildable* Actor);
    static void PrepareNetworks(UWorld* World, const TArray<FBSNativeTransform>& Changes);
    static void ApplyGeometry(AFGBuildable* Actor, const FBSNativeTransform& Change, const FTransform& Before);
    static void RefreshNetworks(UWorld* World, const TArray<FBSNativeTransform>& Changes);
    static void RebuildSpline(AFGBuildable* Actor);
    static bool RunNetworkChecks(AFGCharacterPlayer* Player);
    static bool RunSettledNetworkChecks(AFGCharacterPlayer* Player, int32 Phase);
    static bool RunPooledVisualChecks(AFGCharacterPlayer* Player, int32 Phase);
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
        EBSTransformMode Mode, double Amount, FVector Translation, FBSSelection& Result, FString& Error,
        const FBSTransformOptions* Options = nullptr, const TArray<FBSBuildingRef>* ExpectedMembers = nullptr);
    bool RestoreTransform(AFGCharacterPlayer* Player, bool Redo, FBSSelection& Result, FString& Error);
    void PublishTransforms(const TArray<FBSNativeTransform>& Changes, FBSSelection& Result);
    void RecordTransform(AFGCharacterPlayer* Player, const FBSTransformOptions* Options, const TArray<FBSNativeTransform>& Before,
        const TArray<FBSNativeTransform>& After, const FBSSelection& Selection);
    bool CommitTransforms(const TArray<FBSNativeTransform>& Changes, const TArray<FBSNativeTransform>& Undo, FString& Error);
    UPROPERTY(Transient) TArray<FBSTransformHistory> TransformHistory;
    TMap<uint64, uint64> BuildingVersions;
    uint64 TransformSerial = 0;
    TMap<uint64, int32> SnapshotIndices;
    TMap<uint64, uint64> ReceivedVersions;
    UFUNCTION(NetMulticast, Reliable) void Multicast_BlueprintPose(AFGBlueprintProxy* Proxy, FTransform Pose);
    UFUNCTION(NetMulticast, Reliable) void Multicast_Transforms(const TArray<FBSNativeTransform>& Changes);
    UFUNCTION() void OnRep_TransformSnapshot();
    virtual void Tick(float DeltaSeconds) override;
    // Native actors/instance records save their own transforms. This transient
    // snapshot only brings late-joining clients up to date in this session.
    UPROPERTY(Transient) TArray<FBSNativeTransform> TransformSnapshot;
    UPROPERTY(Transient) TArray<FBSNativeTransform> PendingTransforms;
    UPROPERTY(SaveGame) TArray<FBSGroup> Groups;
    // Never replicated as a public property: owner-filtered RPC snapshots only.
    UPROPERTY(SaveGame) TArray<FBSRailPlan> RailPlans;
    UPROPERTY(Transient) TMap<FGuid,FBSPlanHistory> PlanHistories;
    UPROPERTY(SaveGame) int32 SchemaVersion = 1;
    // Revision describes the locally committed tree, never an unfinished transfer.
    UPROPERTY() int32 Revision = 0;
    UPROPERTY(Replicated) int32 PublishedGroupRevision = 0;
    bool HasReceivedGroups = false;
    void PublishGroups() { PublishedGroupRevision = ++Revision; ForceNetUpdate(); }
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
    UFUNCTION(Server, Reliable) void Server_PlanCommand(const FBSPlanCommand& Command);
    UFUNCTION(Server, Reliable) void Server_PlanSnapshot(bool Force = false);
    UFUNCTION(Server, Reliable) void Server_PlanNext(FGuid Transfer, int32 Sequence);
    UFUNCTION(Client, Reliable) void Client_PlanChunk(const FBSPlanChunk& Chunk);
    UFUNCTION(Client, Reliable) void Client_PlanResult(FGuid Plan, const FString& Message);
    void SendPlanChunk();
    UPROPERTY(Transient) TArray<FBSRailPlan> OutgoingPlans;
    UPROPERTY(Transient) TArray<FBSRailPlan> IncomingPlans;
    FGuid PlanTransfer, ReceivingPlanTransfer;
    int32 PlanSequence=0, PlanIndex=0, PlanNodeIndex=0, PlanEdgeIndex=0, ReceivingPlanSequence=0;
    double PlanTransferDeadline=0, LastPlanPoll=-100, LastPlanCommand=-100;
    uint32 DeliveredPlanHash=0, OutgoingPlanHash=0;
    bool PlanTransferFinal=false;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(Replicated) bool ReplicationAnchor = true;
    void RequestSelection(uint8 Action, const FBSSelection& Selection, FGuid Request, int32 Revision, const FBSBuildingRef& Anchor = {});
    void TickSelectionTransfer();
    void SendSelectionPacket(bool Result);
    void SendGroupResult(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision);
    UFUNCTION(Server, Reliable) void Server_SelectionPacket(const FBSSelectionPacket& Packet);
    UFUNCTION(Client, Reliable) void Client_SelectionAck(FGuid Request, int32 Next, const FString& Error);
    UFUNCTION(Client, Reliable) void Client_SelectionResultPacket(const FBSSelectionPacket& Packet);
    UFUNCTION(Server, Reliable) void Server_SelectionResultAck(FGuid Request, int32 Next);
    UPROPERTY(Transient) FBSSelectionPacket SelectionUpload;
    UPROPERTY(Transient) FBSSelectionPacket SelectionAssembly;
    UPROPERTY(Transient) FBSSelectionPacket SelectionReply;
    UPROPERTY(Transient) FBSSelectionPacket SelectionResult;
    int32 SelectionUploadNext = 0, SelectionReplyNext = 0;
    double SelectionUploadDeadline = 0, SelectionAssemblyDeadline = 0, SelectionReplyDeadline = 0;
    FGuid ExpectedSelectionResult, SelectionDispatch, CompletedSelectionUpload, CompletedSelectionResult;
    void TickGroupTransfer();
    UFUNCTION(Server, Reliable) void Server_GroupSnapshot(FGuid Request);
    UFUNCTION(Server, Reliable) void Server_GroupSnapshotNext(FGuid Request, int32 Sequence);
    UFUNCTION(Client, Reliable) void Client_GroupSnapshot(FGuid Request, int32 Sequence, int32 Revision, int32 TotalGroups, const TArray<FBSGroupFragment>& Batch, bool Final, bool Failed);
    void SendGroupSnapshotBatch();
    UPROPERTY(Transient) TArray<FBSGroup> OutgoingGroups;
    UPROPERTY(Transient) TArray<FBSGroup> IncomingGroups;
    FGuid OutgoingGroupRequest, IncomingGroupRequest;
    int32 OutgoingGroupIndex = 0, OutgoingMemberIndex = 0, OutgoingGroupSequence = 0, OutgoingGroupRevision = 0;
    int32 IncomingGroupSequence = 0, IncomingGroupRevision = 0, IncomingGroupCount = 0, IncomingGroupMembers = 0;
    double GroupTransferDeadline = 0, NextGroupPoll = 0, LastGroupRequest = -100;
    double NextGroupResolve = 0;
    TArray<FIntPoint> PendingGroupActors;
    UFUNCTION(Server, Reliable) void Server_PaintConnected(const FBSBuildingRef& Seed, const FBSConnectedSettings& Settings, TSubclassOf<class UFGCustomizationRecipe> Recipe);
    UFUNCTION(Server, Reliable) void Server_PaintTransformers(const FBSBuildingRef& Seed, TSubclassOf<class UFGCustomizationRecipe> Recipe);
    UFUNCTION(Client, Reliable) void Client_ConnectedMessage(const FString& Message);
    double LastConnectedPaint = -100;
    UPROPERTY(Transient) TArray<FBSTransformer> Transformers;
    UPROPERTY(Transient) bool TransformersBuildAsBlueprint = false;
    int32 TransformerRevision = 0;
    UFUNCTION(Server, Reliable) void Server_AddTransformer(const FBSBuildingRef& Anchor);
    UFUNCTION(Server, Reliable) void Server_SetTransformers(const TArray<FBSTransformer>& List, int32 ExpectedRevision, bool BuildAsBlueprint);
    UFUNCTION(Client, Reliable) void Client_Transformers(const TArray<FBSTransformer>& List, int32 NewRevision, const FString& Message, bool BuildAsBlueprint);
    UPROPERTY(Transient) TArray<TObjectPtr<class UBSActionRecord>> BuildHistory;
    UPROPERTY(Transient) TArray<TObjectPtr<class UBSActionRecord>> DeleteHistory;
    UPROPERTY(Transient) TArray<TObjectPtr<class UBSActionRecord>> BuildRedoHistory;
    UPROPERTY(Transient) TArray<TObjectPtr<class UBSActionRecord>> DeleteRedoHistory;
    UFUNCTION(Server, Reliable) void Server_UndoGunAction(bool Deletion);
    UFUNCTION(Server, Reliable) void Server_RedoGunAction(bool Deletion);
    double LastActionUndo = -100;
    UPROPERTY(Transient) TArray<TObjectPtr<class UBSClipboard>> Clipboards;
    UFUNCTION(Server, Reliable) void Server_CopySelection(const FBSSelection& Selection, FGuid RequestId, const FBSBuildingRef& Anchor);
    UFUNCTION(Server, Reliable) void Server_CopyChunk(FGuid RequestId, int32 Offset);
    UFUNCTION(Client, Reliable) void Client_CopyChunk(FGuid RequestId, int32 Offset, int32 Total, const TArray<uint8>& Bytes, const FString& Error);

    UFUNCTION(Server, Reliable) void Server_OpenBlueprintSave(const FBSSelection& Selection, FGuid RequestId, const FBSBuildingRef& Anchor);
    UFUNCTION(Client, Reliable) void Client_OpenBlueprintSave(FGuid RequestId, const FString& Error);
    UFUNCTION(Server, Reliable) void Server_CloseBlueprintSave(FGuid RequestId);
    UFUNCTION(Client, Reliable) void Client_BlueprintMessage(const FString& Message);
    UFUNCTION(Server, Reliable)
    void Server_Apply(EBSGroupOperation Operation, const FBSSelection& Selection, int32 ExpectedRevision);
    UFUNCTION(Client, Reliable)
    void Client_Result(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision);
    UFUNCTION(Server, Reliable)
    void Server_Transform(const FBSSelection& Selection, int32 ExpectedRevision, EBSTransformMode Mode, double Amount, FVector Translation);
    UFUNCTION(Server, Reliable)
    void Server_TransformAdvanced(const FBSSelection& Selection, const TArray<FBSBuildingRef>& ExpectedMembers,
        FBSTransformOptions Options, EBSTransformMode Mode, double Amount, FVector Translation);
    UFUNCTION(Server, Reliable) void Server_TransformHistory(bool Redo);
    UFUNCTION(Server, Reliable) void Server_TransformSnapshot();
    UFUNCTION(Client, Reliable) void Client_TransformSnapshot(const TArray<FBSNativeTransform>& Changes, bool More);
    UFUNCTION(Server, Reliable) void Server_TransformSnapshotNext();
    void SendSnapshotBatch();
    UPROPERTY(Transient) TArray<FBSNativeTransform> OutgoingSnapshot;
    int32 NextSnapshotIndex = 0;
    UFUNCTION(Client, Reliable) void Client_TransformSettled(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision, const TArray<FBSBuildingRef>& Targets);
    UFUNCTION(Server, Reliable) void Server_BeginTransform(FGuid Request, const TArray<FGuid>& Groups, FBSTransformOptions Options,
        EBSTransformMode Mode, double Amount, FVector Translation, int32 ExpectedCount, int32 DirectCount);
    UFUNCTION(Server, Reliable) void Server_TransformChunk(FGuid Request, const TArray<FBSBuildingRef>& Members, bool Direct, bool Final);
    UFUNCTION(Client, Reliable) void Client_TransformMembers(const TArray<FBSBuildingRef>& Members, bool Direct, bool Reset);
    void SendTransformResult(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision);
    UPROPERTY(Transient) FBSSelection IncomingSelection;
    UPROPERTY(Transient) TArray<FBSBuildingRef> IncomingMembers;
    UPROPERTY(Transient) FBSSelection ReceivedSelection;
    UPROPERTY(Transient) TArray<FBSBuildingRef> ReceivedMembers;
    FBSTransformOptions IncomingOptions;
    EBSTransformMode IncomingMode = EBSTransformMode::RotateX;
    double IncomingAmount = 0;
    FVector IncomingTranslation = FVector::ZeroVector;
    FGuid IncomingRequest;
    int32 IncomingExpectedCount = 0, IncomingDirectCount = 0;
    double IncomingDeadline = 0;
    double LastSnapshotRequest = -100;
    UFUNCTION(Client, Reliable)
    void Client_TransformResult(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision);
};
