#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SplineComponent.h"
#include "FGBuildGunModeDescriptor.h"
#include "UI/FGInteractWidget.h"
#include "BSPlan.generated.h"

UENUM()
enum class EBSPlanTrace : uint8 { Curve, Straight, Free, Bezier };
UENUM()
enum class EBSPlanRemainder : uint8 { Last, Reduce, Increase };
USTRUCT()
struct FBSPlanPoint
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FVector Location = FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector ArriveTangent = FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector LeaveTangent = FVector::ZeroVector;
    FBSPlanPoint() = default;
    FBSPlanPoint(const FSplinePointData& P):Location(P.Location),ArriveTangent(P.ArriveTangent),LeaveTangent(P.LeaveTangent) {}
};
USTRUCT()
struct BUILDINGSKILLS_API FBSPlanNode
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FGuid Id;
    UPROPERTY(SaveGame) FVector Position = FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector Direction = FVector::ForwardVector;
    UPROPERTY(SaveGame) double Roll = 0;
};
USTRUCT()
struct BUILDINGSKILLS_API FBSPlanEdge
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FGuid Id;
    UPROPERTY(SaveGame) FGuid Start;
    UPROPERTY(SaveGame) FGuid End;
    UPROPERTY(SaveGame) EBSPlanTrace Trace = EBSPlanTrace::Curve;
    // Signed endpoint tangents are geometric continuity, not signal direction.
    // Zero retains automatic orientation for older plans and hand-made curves.
    UPROPERTY(SaveGame) int32 StartTangentSign = 0;
    UPROPERTY(SaveGame) int32 EndTangentSign = 0;
    UPROPERTY(SaveGame) TArray<FVector> Controls;
    // Zero means automatic; otherwise the direction of a through-point tangent.
    UPROPERTY(SaveGame) TArray<FVector> ControlDirections;
    // Captured native/Stick geometry, with endpoints owned by the nodes.
    UPROPERTY(SaveGame) TArray<FBSPlanPoint> Route;
};
USTRUCT()
struct BUILDINGSKILLS_API FBSRailPlan
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FGuid Id;
    UPROPERTY(SaveGame) FString Name;
    // Native saved player-state identity survives reconnects without name matching.
    UPROPERTY(SaveGame) TObjectPtr<class AFGPlayerState> Owner;
    UPROPERTY(SaveGame) bool Shared = false;
    UPROPERTY(SaveGame) int32 Revision = 0;
    UPROPERTY(SaveGame) TArray<FBSPlanNode> Nodes;
    UPROPERTY(SaveGame) TArray<FBSPlanEdge> Edges;
    // Local path axes: X forward, Y right, Z up. Stored in centimeters.
    UPROPERTY(SaveGame) TArray<FVector> Parallels;
    // Signed centimeters; missing entries retain zero for existing plans.
    UPROPERTY(SaveGame) TArray<double> ParallelNodeOffsets;
    UPROPERTY(SaveGame) TArray<double> ParallelStartOffsets;
    // Zero uses the native maximum. Length is measured along entire paths.
    UPROPERTY(SaveGame) double SegmentLength = 8000;
    UPROPERTY(SaveGame) EBSPlanRemainder Remainder = EBSPlanRemainder::Reduce;
    UPROPERTY(SaveGame) bool PlaceBlockSignal = false;
    UPROPERTY(SaveGame) int32 SignalEvery = 1;
    UPROPERTY(SaveGame) bool LeftSideSignal = false;
    UPROPERTY(SaveGame) TArray<bool> ParallelReversed;
    UPROPERTY(SaveGame) TArray<FGuid> ReversedEdges;
};
UENUM()
enum class EBSPlanAction : uint8 { New, Rename, Share, Place, Move, Edge, Parallels, Delete, Build, Undo, Redo, Node, Segments, ReverseDirection, ReverseSpline, Duplicate };
// Server-owned, session-only history. Sharing and ownership are never restored
// from an old geometry snapshot.
USTRUCT()
struct FBSPlanHistory
{
    GENERATED_BODY()
    UPROPERTY() TArray<FBSRailPlan> Undo;
    UPROPERTY() TArray<FBSRailPlan> Redo;
    void Record(const FBSRailPlan& Before, const FBSRailPlan& After, int32 Limit);
    bool Restore(FBSRailPlan& Plan, bool Forward, int32 Revision, int32 Limit, FString& Error);
    void Trim(int32 Limit);
};
USTRUCT()
struct FBSPlanCommand
{
    GENERATED_BODY()
    UPROPERTY() EBSPlanAction Action = EBSPlanAction::New;
    UPROPERTY() FGuid Plan;
    UPROPERTY() int32 Revision = 0;
    UPROPERTY() FString Name;
    UPROPERTY() FBSPlanNode Node;
    UPROPERTY() FBSPlanEdge Edge;
    UPROPERTY() TArray<FGuid> Selection;
    UPROPERTY() FVector Delta = FVector::ZeroVector;
    UPROPERTY() TArray<FVector> Parallels;
    UPROPERTY() TArray<double> ParallelNodeOffsets;
    UPROPERTY() TArray<double> ParallelStartOffsets;
    UPROPERTY() FGuid SplitEdge;
    UPROPERTY() int32 SplitSegment = -1;
    UPROPERTY() double SplitTime = 0;
    UPROPERTY() double SegmentLength = 0;
    UPROPERTY() EBSPlanRemainder Remainder = EBSPlanRemainder::Reduce;
    UPROPERTY() bool PlaceBlockSignal = false;
    UPROPERTY() int32 SignalEvery = 1;
    UPROPERTY() bool LeftSideSignal = false;
    UPROPERTY() TArray<bool> ParallelReversed;
};
USTRUCT()
struct FBSPlanChunk
{
    GENERATED_BODY()
    UPROPERTY() FGuid Transfer;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() FBSRailPlan Plan;
    UPROPERTY() bool Start = false;
    UPROPERTY() bool Final = false;
};
struct BUILDINGSKILLS_API FBSPlanMath
{
    static const FBSPlanNode* Node(const FBSRailPlan& Plan, FGuid Id);
    static FVector ConnectionDirection(const FBSRailPlan& Plan, FGuid Id);
    static void InheritTangents(const FBSRailPlan& Plan, FBSPlanEdge& Edge);
    static bool Valid(const FBSRailPlan& Plan);
    static bool Apply(FBSRailPlan& Plan, const FBSPlanCommand& Command, FString& Error);
    static TArray<FSplinePointData> Curve(const FBSRailPlan& Plan, const FBSPlanEdge& Edge);
    static TArray<FSplinePointData> Offset(const TArray<FSplinePointData>& Curve, FVector Offset, double StartRoll=0, double EndRoll=0);
    static TArray<FSplinePointData> ParallelCurve(const FBSRailPlan& Plan, const FBSPlanEdge& Edge, int32 Parallel, bool Forward=true);
    static bool Closest(const TArray<FSplinePointData>& Curve, FVector Position, int32& Segment, double& Time, FVector& Point);
    static FRotator Rotation(const FBSPlanNode& Node);
    static FVector ControlDirection(const FBSRailPlan& Plan, const FBSPlanEdge& Edge, int32 Index);
    static void AlignNode(FBSRailPlan& Plan, FGuid Node);
    static FVector Point(const FSplinePointData& A, const FSplinePointData& B, double T);
    static FVector Tangent(const FSplinePointData& A, const FSplinePointData& B, double T);
    static bool ShapeValid(const FSplinePointData& A, const FSplinePointData& B, double T, double Radius, double Grade);
    static TSet<FGuid> SelectedNodes(const FBSRailPlan& Plan, const TArray<FGuid>& Selection);
};
struct FBSPlanPath
{
    FGuid Start, End;
    int32 Parallel = 0;
    TArray<FSplinePointData> Points;
    TArray<FGuid> Edges;
    // Linked node markers remain present even when the start is trimmed.
    TArray<FBSPlanNode> Nodes;
    TArray<int32> NodePointIndices;
    double StartOffset = 0;
    bool PlacementValid = true;
    bool SignalReverse = false;
};
struct BUILDINGSKILLS_API FBSPlanSegments
{
    static bool Paths(const FBSRailPlan& Plan, TArray<FBSPlanPath>& Paths, FString& Error, bool CheckContinuity=true, const TSet<FGuid>& ForcedEndpoints={});
    static bool Split(const TArray<FSplinePointData>& Points, double Requested, EBSPlanRemainder Policy,
        double Minimum, double Maximum, TArray<TArray<FSplinePointData>>& Parts, FString& Error);
    static bool OffsetStart(TArray<FSplinePointData>& Points, double Offset, FString& Error);
};
class BUILDINGSKILLS_API FBSPlanBuild
{
public:
    static TSubclassOf<class UFGRecipe> Recipe();
    static double MaximumLength();
    static double MinimumLength();
    static void SnapFoundation(class AFGBuildable* Floor, FVector& Position, FVector& Direction);
    static bool Build(class AFGCharacterPlayer* Player, const FBSRailPlan& Plan, FString& Error);
};
UCLASS()
class BUILDINGSKILLS_API UBSPlanPlaceMode : public UFGBuildGunModeDescriptor
{ GENERATED_BODY() public: UBSPlanPlaceMode(); };
UCLASS()
class BUILDINGSKILLS_API UBSPlanEditMode : public UFGBuildGunModeDescriptor
{ GENERATED_BODY() public: UBSPlanEditMode(); };

UCLASS()
class BUILDINGSKILLS_API UBSPlanComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBSPlanComponent();
    static UBSPlanComponent* For(class AFGBuildGun* Gun);
    static UBSPlanComponent* Find(class AFGCharacterPlayer* Player);
    void Initialize(class UEnhancedInputComponent* Input);
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    void Toggle();
    void Close(bool Unequip);
    void Primary(bool Release);
    void Secondary();
    void DeleteSelected();
    void Undo();
    void Redo();
    void Scroll(int32 Delta);
    void Move(int32 Axis);
    void Settings();
    void CloseSettings();
    void SetMode(bool Editing);
    void SelectFromList(FGuid Id, bool Additive);
    static FVector MovementDirection(int32 Axis, double ViewYaw);
    bool UpdatingNativeMode = false;
    bool Allowed() const;
    void Send(FBSPlanCommand Command);
    void QueueMenu(FBSPlanCommand Command);
    UPROPERTY(Transient) TArray<FBSPlanCommand> MenuCommands;
    void Receive(TArray<FBSRailPlan> Plans, FGuid Preferred, const FString& Message);
    FBSRailPlan* Current();
    const FBSRailPlan* Current() const;
    void Load(FGuid Id);
    void Refresh();
    FText Status() const;
    TArray<TPair<FText,FText>> Hints() const;
    bool Active = false, Editing = false, Waiting = false;
    UPROPERTY(Transient) TArray<FBSRailPlan> Plans;
    UPROPERTY(Transient) FGuid CurrentId;
    UPROPERTY(Transient) TArray<FGuid> Selection;
    UPROPERTY(Transient) TObjectPtr<class UBSPlanWidget> Window;
    FString Message;
private:
    friend class FBSPlanSmoke;
    UPROPERTY(Transient) TObjectPtr<class UInputMappingContext> Context;
    UPROPERTY(Transient) TArray<TObjectPtr<class UInstancedStaticMeshComponent>> Visuals;
    UPROPERTY(Transient) FBSPlanNode Aim;
    UPROPERTY(Transient) TArray<FSplinePointData> AimRoute;
    FGuid LastNode, HoverNode, HoverEdge;
    FVector HoverNodePosition = FVector::ZeroVector;
    TArray<FBSPlanNode> ParallelHoverNodes;
    int32 HoverSegment = -1;
    double HoverTime = 0;
    bool HoverOrientation = false, DragOrientation = false;
    int32 HoverControl = -1, DragControl = -1;
    FGuid DragNode, DragEdge;
    FVector DragOrigin;
    UPROPERTY(Transient) FBSRailPlan DragBefore;
    UPROPERTY(Transient) FBSPlanCommand DragCommand;
    bool DragChanged = false;
    FVector LastPlacedPosition = FVector::ZeroVector;
    double DragDistance = 0, NextPoll = 0, NextDraw = 0, NextMove = 0, EnteredAt = 0;
    bool HaveAim = false;
    bool PendingCreate = false;
    bool MultiHeld = false, RemovingSelection = false;
    TSet<FGuid> Swept;
    uint32 DrawHash = 0;
    void Draw();
    void AimAndHit();
    void ResetStart();
    void FinishDrag();
    void SelectHovered();
};
struct FBSPlanListItem { FGuid Id; FString Label; };
template<typename ItemType> class SListView;
UCLASS()
class BUILDINGSKILLS_API UBSPlanWidget : public UFGInteractWidget
{
    GENERATED_BODY()
public:
    UBSPlanWidget(const FObjectInitializer& Initializer);
    virtual void OnEscapePressed_Implementation() override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override;
private:
    friend class FBSPlanSmoke;
    TSharedPtr<class SVerticalBox> SavedPlanList;
    TArray<FGuid> ListedSavedPlans;
    FGuid PendingPlanSelection;
    FVector NodePosition = FVector::ZeroVector;
    FRotator NodeRotation = FRotator::ZeroRotator;
    FBSPlanEdge EdgeDraft;
    TArray<FVector> ParallelDraft;
    TArray<double> ParallelNodeOffsetsDraft, ParallelStartOffsetsDraft;
    double SegmentLengthDraft = 0;
    EBSPlanRemainder RemainderDraft = EBSPlanRemainder::Reduce;
    bool PlaceBlockSignalDraft = false, LeftSideSignalDraft = false;
    int32 SignalEveryDraft = 1;
    TArray<bool> ParallelReversedDraft;
    TSharedPtr<class SVerticalBox> Details;
    TSharedPtr<class SVerticalBox> ItemList;
    TArray<TSharedPtr<FBSPlanListItem>> ListItems;
    TSharedPtr<SListView<TSharedPtr<FBSPlanListItem>>> ItemView;
    bool SyncingList = false;
    FGuid ListedPlan;
    int32 ListedRevision = -1;
    bool DetailsRefreshPending = false;
    TMap<FName,TSharedPtr<class SExpandableArea>> Sections;
    TSharedRef<SWidget> Section(FName Id, const TCHAR* Title, TSharedRef<SWidget> Body);
    void RebuildDetails();
    void SaveNode(FGuid Id);
    void SaveEdge();
    void SaveParallels();
    void SaveSpecification();
    void RebuildList();
    void RebuildSavedPlans();
    virtual void NativeTick(const FGeometry&, float) override;
};
