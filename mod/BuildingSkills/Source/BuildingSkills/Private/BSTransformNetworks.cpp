#include "BSGroups.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePipeBase.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Buildables/FGBuildableRailroadSignal.h"
#include "Buildables/FGBuildableRailroadSwitchControl.h"
#include "Buildables/FGBuildableTrainPlatform.h"
#include "Buildables/FGBuildableWire.h"
#include "FGBuildablePipelineFlowIndicator.h"
#include "FGBuildableSubsystem.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGCircuitConnectionComponent.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "FGSplineMeshGenerationLibrary.h"
#include "FGPipeSubsystem.h"
#include "FGPipeNetwork.h"
#include "FGRailroadSubsystem.h"
#include "FGRailroadVehicle.h"
#include "Patching/NativeHookManager.h"

namespace
{
    bool IsFlexible(AFGBuildable* Actor)
    {
        return Actor && (Cast<IFGSplineBuildableInterface>(Actor) || Actor->IsA<AFGBuildableConveyorLift>() || Actor->IsA<AFGBuildableWire>());
    }
    TArray<UFGConnectionComponent*> Opposite(UFGConnectionComponent* Connection)
    {
        TArray<UFGConnectionComponent*> Out;
        if (auto* C = Cast<UFGFactoryConnectionComponent>(Connection)) { if (C->GetConnection()) Out.Add(C->GetConnection()); }
        if (auto* C = Cast<UFGPipeConnectionComponentBase>(Connection)) { if (C->GetConnection()) Out.Add(C->GetConnection()); }
        if (auto* C = Cast<UFGRailroadTrackConnectionComponent>(Connection)) for (auto* Other : C->GetConnections()) Out.Add(Other);
        return Out;
    }
    FBSNativeTransform* Find(TArray<FBSNativeTransform>& Changes, AActor* Actor)
    { return Changes.FindByPredicate([Actor](const FBSNativeTransform& C) { return C.Building.Actor == Actor; }); }
    FVector MovedPoint(const FBSNativeTransform& C, const FVector& World)
    { return C.Target.TransformPosition(C.Building.Transform.InverseTransformPosition(World)); }
    FVector MovedVector(const FBSNativeTransform& C, const FVector& World)
    { return C.Target.TransformVector(C.Building.Transform.InverseTransformVector(World)); }
    void OwnedAttachments(AFGBuildable* Actor,TArray<AActor*>& Children)
    {
        Actor->GetChildDismantleActors_Implementation(Children);
        if(auto* Track=Cast<AFGBuildableRailroadTrack>(Actor))
            for(int32 I=0;I<2;++I)
                if(auto* End=Track->GetConnection(I))
                {
                    if(End->GetFacingSignal()) Children.AddUnique(End->GetFacingSignal());
                    if(End->GetTrailingSignal()) Children.AddUnique(End->GetTrailingSignal());
                    if(End->GetSwitchControl()) Children.AddUnique(End->GetSwitchControl());
                }
    }
}

void InstallBuildingSkillsNetworkHooks()
{
    // Editor SDK functions are stubs; native hooks require the actual game.
    if (WITH_EDITOR) return;
    auto RestoreFrames=[](AFGBuildable* Actor)
    {
        // Native save/load restores actor transforms and spline data separately.
        // Reapply connection frames after construction of transformed splines.
        if(Actor->GetActorRotation().IsNearlyZero(.001) && Actor->GetActorScale3D().Equals(FVector::OneVector,.001)) return;
        if(auto* Spline=Cast<IFGSplineBuildableInterface>(Actor); Spline && Spline->GetSplinePointData().Num()>=2)
            FBSTransformNative::RebuildSpline(Actor);
    };
    if(!WITH_EDITOR)
    {
        SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildableConveyorBelt,BeginPlay,[RestoreFrames](AFGBuildableConveyorBelt* Actor){RestoreFrames(Actor);});
        SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildablePipeBase,BeginPlay,[RestoreFrames](AFGBuildablePipeBase* Actor){RestoreFrames(Actor);});
        SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildableRailroadTrack,BeginPlay,[RestoreFrames](AFGBuildableRailroadTrack* Actor){RestoreFrames(Actor);});
    }
    SUBSCRIBE_METHOD(FConveyorChainSplineSegment::Initialize,
        [](auto&, FConveyorChainSplineSegment* Segment, USplineComponent*, float)
        {
            auto* Chain=Segment->ChainActor.Get();
            if(!Chain || Chain->GetActorScale3D().Equals(FVector::OneVector,.0001)) return;
            // A native chain inherits the lead belt's actor scale, but its spline
            // uses absolute unit scale. Initialize converts world points through
            // the chain transform, which would otherwise scale them twice.
            // Canonicalize the owner BEFORE native segment coordinates are built.
            if(auto* Root=Chain->GetRootComponent())
            {
                TGuardValue<TEnumAsByte<EComponentMobility::Type>> Mobility(Root->Mobility,EComponentMobility::Movable);
                Chain->SetActorScale3D(FVector::OneVector);
            }
        });
}

FBSNativeTransform FBSTransformNative::Capture(AFGBuildable* Actor)
{
    FBSNativeTransform C;
    C.Building = FBSBuildingRef::FromActor(Actor);
    C.Building.Transform = C.Target = Actor->GetActorTransform();
    if (auto* Spline = Cast<IFGSplineBuildableInterface>(Actor)) C.Spline = Spline->GetSplinePointData();
    if (auto* Lift = Cast<AFGBuildableConveyorLift>(Actor)) { C.HasLift = true; C.LiftTop = Lift->GetTopTransform(); }
    if (auto* Wire = Cast<AFGBuildableWire>(Actor))
    {
        C.HasWire = true;
        C.WireStart = Wire->GetConnectionLocation(0);
        C.WireEnd = Wire->GetConnectionLocation(1);
        for(const auto& Instance:Wire->GetWireInstances()) { C.WirePoints.Add(Instance.Locations[0]); C.WirePoints.Add(Instance.Locations[1]); }
    }
    return C;
}

bool FBSTransformNative::PlanNetworks(TArray<FBSNativeTransform>& Changes, FString& Error)
{
    TSet<AActor*> ExplicitSelection;
    for(const auto& C:Changes) if(C.Building.Actor) ExplicitSelection.Add(C.Building.Actor);
    // Own child buildables travel with their parent, even when the gun exposes
    // only the platform/pipe as a selectable object. Do this before boundaries.
    for (int32 I = 0; I < Changes.Num(); ++I)
    {
        const auto Parent = Changes[I];
        auto* Actor = Parent.Building.Actor.Get();
        TArray<AFGBuildable*> Children;
        if (Actor)
        {
            TArray<AActor*> Owned;
            OwnedAttachments(Actor,Owned);
            for (auto* Child : Owned)
                if (auto* Building = Cast<AFGBuildable>(Child); Building && !IsFlexible(Building)) Children.AddUnique(Building);
        }
        if (auto* Platform = Cast<AFGBuildableTrainPlatform>(Actor)) Children.Add(Platform->GetTrackPosition().Track.Get());
        if (auto* Pipe = Cast<AFGBuildablePipeline>(Actor)) Children.Add(Pipe->GetFlowIndicator());
        for (auto* Child : Children)
            if (IsValid(Child) && !Find(Changes, Child))
            {
                auto C = Capture(Child);
                C.Target = C.Target.GetRelativeTransform(Parent.Building.Transform) * Parent.Target;
                Changes.Add(C);
            }
    }
    const int32 MovingCount = Changes.Num();
    TSet<AActor*> Moving;
    for (const auto& C : Changes) if (C.Building.Actor) Moving.Add(C.Building.Actor);
    // Expand one edge at the boundary, never the whole connected factory.
    for (int32 I = 0; I < MovingCount; ++I)
    {
        auto* Actor = Changes[I].Building.Actor.Get();
        if (!Actor) continue;
        TInlineComponentArray<UFGConnectionComponent*> Connections(Actor);
        for (auto* Connection : Connections)
        {
            for (auto* Other : Opposite(Connection))
                if (auto* Edge = Cast<AFGBuildable>(Other->GetOwner()); IsFlexible(Edge) && !Find(Changes, Edge)) Changes.Add(Capture(Edge));
            if (auto* Circuit = Cast<UFGCircuitConnectionComponent>(Connection))
            {
                TArray<AFGBuildableWire*> Wires;
                Circuit->GetWires(Wires);
                for (auto* Wire : Wires) if (IsValid(Wire) && !Find(Changes, Wire)) Changes.Add(Capture(Wire));
            }
        }
    }
    // Resolve endpoint targets against the immutable rigid-motion plan. A
    // selected edge drives an unselected edge; a fixed machine anchors an end.
    const auto RigidPlan = Changes;
    auto Endpoint = [&](UFGConnectionComponent* C, const FBSNativeTransform& Owner, FVector& Position, FVector& Normal)
    {
        Position = MovedPoint(Owner, C->GetComponentLocation());
        Normal = MovedVector(Owner, C->GetForwardVector()).GetSafeNormal();
        TArray<UFGConnectionComponent*> Junction{C};
        TArray<int32> Depth{0};
        for(int32 I=0;I<Junction.Num() && I<64;++I)
            for(auto* Other:Opposite(Junction[I]))
                if(!Junction.Contains(Other)) { Junction.Add(Other); Depth.Add(Depth[I]+1); }
        int32 BestPriority=MAX_int32;
        uint32 BestId=MAX_uint32;
        for(int32 I=0;I<Junction.Num();++I)
        {
            auto* Other=Junction[I];
            auto* OtherOwner = Cast<AFGBuildable>(Other->GetOwner());
            const auto* Target = RigidPlan.FindByPredicate([&](const FBSNativeTransform& X) { return X.Building.Actor == OtherOwner; });
            const bool Moves=Moving.Contains(OtherOwner) && Target;
            const int32 Priority=IsFlexible(OtherOwner) ? (Moves?2:3) : (Moves?1:0);
            // Every branch resolves the SAME junction anchor, including selected
            // splines with different rotations under nonuniform scaling.
            if(Priority>BestPriority || (Priority==BestPriority && Other->GetUniqueID()>=BestId)) continue;
            BestPriority=Priority; BestId=Other->GetUniqueID();
            Position=Moves?MovedPoint(*Target,Other->GetComponentLocation()):Other->GetComponentLocation();
            Normal=(Moves?MovedVector(*Target,Other->GetForwardVector()):Other->GetForwardVector()).GetSafeNormal()*(Depth[I]%2?-1:1);
        }
    };
    for (auto& C : Changes)
    {
        auto* Actor = C.Building.Actor.Get();
        if (!Actor) continue;
        if (auto* Spline = Cast<IFGSplineBuildableInterface>(Actor))
        {
            C.Spline = Spline->GetSplinePointData();
            if (C.Spline.Num() < 2 || C.Spline.Num() > 128)
            { Error = TEXT("A connection has invalid spline data. No buildings changed."); return false; }
            auto* Start = Spline->GetSplineConnection0();
            auto* End = Spline->GetSplineConnection1();
            if (!Start || !End) { Error = TEXT("A spline is missing its endpoints."); return false; }
            FVector P0, P1, N0, N1;
            Endpoint(Start, C, P0, N0); Endpoint(End, C, P1, N1);
            const FVector Original0 = C.Target.TransformPosition(C.Spline[0].Location);
            const FVector Original1 = C.Target.TransformPosition(C.Spline.Last().Location);
            const FVector Delta0 = P0 - Original0, Delta1 = P1 - Original1;
            const bool PinnedBody = Moving.Contains(Actor) && !Delta0.IsNearlyZero(.01) && !Delta1.IsNearlyZero(.01);
            if(PinnedBody && C.Spline.Num()==2)
            {
                // Split the original Hermite span exactly, providing an editable
                // middle point when both end buildings must remain stationary.
                auto A=C.Spline[0], B=C.Spline[1];
                const FSplinePointData Mid(FMath::CubicInterp(A.Location,A.LeaveTangent,B.Location,B.ArriveTangent,.5),
                    FMath::CubicInterpDerivative(A.Location,A.LeaveTangent,B.Location,B.ArriveTangent,.5)*.5);
                A.ArriveTangent*=.5; A.LeaveTangent*=.5; B.ArriveTangent*=.5; B.LeaveTangent*=.5;
                C.Spline={A,Mid,B};
            }
            for (int32 I = 0; I < C.Spline.Num(); ++I)
            {
                auto& Point = C.Spline[I];
                const double Alpha = double(I) / (C.Spline.Num() - 1);
                const FVector Correction = PinnedBody ? (I==0 ? Delta0 : I==C.Spline.Num()-1 ? Delta1 : FVector::ZeroVector) : FMath::Lerp(Delta0, Delta1, Alpha);
                Point.Location += C.Target.InverseTransformVector(Correction);
            }
            // Connections face outward: spline traversal is opposite the first
            // connector and along the last. Keep tangent magnitude/curvature.
            auto SetEnd = [&](FSplinePointData& P, const FVector& Direction)
            {
                const double A = C.Target.TransformVector(P.ArriveTangent).Size();
                const double L = C.Target.TransformVector(P.LeaveTangent).Size();
                P.ArriveTangent = C.Target.InverseTransformVector(Direction * A);
                P.LeaveTangent = C.Target.InverseTransformVector(Direction * L);
            };
            SetEnd(C.Spline[0], -N0); SetEnd(C.Spline.Last(), N1);
            for (const auto& P : C.Spline)
                if (P.Location.ContainsNaN() || P.ArriveTangent.ContainsNaN() || P.LeaveTangent.ContainsNaN() ||
                    C.Target.TransformPosition(P.Location).GetAbsMax() > 2000000)
                { Error = TEXT("A stretched connection exceeds world limits."); return false; }
        }
        if (auto* Lift = Cast<AFGBuildableConveyorLift>(Actor))
        {
            C.HasLift = true; C.LiftTop = Lift->GetTopTransform();
            FVector P0, P1, N0, N1;
            Endpoint(Lift->GetConnection0(), C, P0, N0); Endpoint(Lift->GetConnection1(), C, P1, N1);
            auto* Bottom = Lift->GetConnection0(); auto* Top = Lift->GetConnection1();
            if (FMath::Abs(Bottom->GetRelativeLocation().Z) > FMath::Abs(Top->GetRelativeLocation().Z)) { Swap(Bottom, Top); Swap(P0, P1); }
            const FVector OldDelta = C.Target.TransformVector(Top->GetRelativeLocation() - Bottom->GetRelativeLocation());
            const FQuat Align = FQuat::FindBetweenNormals(OldDelta.GetSafeNormal(), (P1 - P0).GetSafeNormal());
            C.Target.SetRotation((Align * C.Target.GetRotation()).GetNormalized());
            C.Target.SetLocation(P0 - C.Target.TransformVector(Bottom->GetRelativeLocation()));
            const double Ratio = FVector::Distance(P0, P1) / FMath::Max(OldDelta.Size(), 1.0);
            C.LiftTop.SetTranslation(C.LiftTop.GetTranslation() * Ratio);
        }
        if (auto* Wire = Cast<AFGBuildableWire>(Actor))
        {
            C.HasWire = true;
            C.WirePoints.Reset();
            FVector* Ends[] = {&C.WireStart, &C.WireEnd};
            for (int32 I = 0; I < 2; ++I)
            {
                auto* Connection = Wire->GetConnection(I);
                *Ends[I] = Connection ? Connection->GetComponentLocation() : MovedPoint(C, Wire->GetConnectionLocation(I));
                if (Connection)
                    if (const auto* Target = RigidPlan.FindByPredicate([&](const FBSNativeTransform& X) { return X.Building.Actor == Connection->GetOwner(); }))
                        *Ends[I] = MovedPoint(*Target, *Ends[I]);
            }
            for(const auto& Instance:Wire->GetWireInstances())
                for(int32 I=0;I<2;++I)
                {
                    FVector Position=Instance.Locations[I];
                    if(auto* Connection=Wire->GetConnection(I))
                        if(const auto* Target=RigidPlan.FindByPredicate([&](const FBSNativeTransform& X){return X.Building.Actor==Connection->GetOwner();}))
                            Position=MovedPoint(*Target,Position);
                    C.WirePoints.Add(Position);
                }
        }
    }
    // Indicators, monitors and signals on an unselected boundary spline follow
    // the deformed curve at their original spline parameter, not its old root.
    const int32 EdgeCount = Changes.Num();
    for (int32 I=0; I<EdgeCount; ++I)
    {
        const auto Parent = Changes[I];
        auto* Actor = Parent.Building.Actor.Get();
        auto* Spline = Cast<IFGSplineBuildableInterface>(Actor);
        if (!Spline || Parent.Spline.Num()<2) continue;
        TArray<AActor*> Children;
        OwnedAttachments(Actor,Children);
        for (auto* Child : Children)
        {
            auto* Building = Cast<AFGBuildable>(Child);
            if (!Building || IsFlexible(Building) || ExplicitSelection.Contains(Building)) continue;
            const double Key=FMath::Clamp(double(Spline->GetSplineComponent()->FindInputKeyClosestToWorldLocation(Building->GetActorLocation())),0.0,double(Parent.Spline.Num()-1));
            const int32 Segment=FMath::Min(FMath::FloorToInt(Key),Parent.Spline.Num()-2);
            const double Alpha=Key-Segment;
            const auto& A=Parent.Spline[Segment]; const auto& B=Parent.Spline[Segment+1];
            const FVector Position=FMath::CubicInterp(A.Location,A.LeaveTangent,B.Location,B.ArriveTangent,Alpha);
            const FVector Direction=FMath::CubicInterpDerivative(A.Location,A.LeaveTangent,B.Location,B.ArriveTangent,Alpha).GetSafeNormal();
            const FTransform OldFrame=Spline->GetSplineComponent()->GetTransformAtSplineInputKey(Key,ESplineCoordinateSpace::World,true);
            const FTransform NewFrame(FRotationMatrix::MakeFromXZ(Parent.Target.TransformVector(Direction),Parent.Target.GetUnitAxis(EAxis::Z)).ToQuat(),Parent.Target.TransformPosition(Position),Parent.Target.GetScale3D());
            auto C=Capture(Building);
            C.Target=C.Target.GetRelativeTransform(OldFrame)*NewFrame;
            if(auto* Existing=Find(Changes,Building)) *Existing=C; else Changes.Add(C);
        }
    }
    if (Changes.Num()>8192) { Error=TEXT("This transform would affect more than 8192 buildings and connections."); return false; }
    for(const auto& C:Changes)
        if(!C.Target.IsValid() || C.Target.GetLocation().GetAbsMax()>2000000 || C.Target.GetScale3D().GetMin()<.01 || C.Target.GetScale3D().GetMax()>100)
        { Error=TEXT("A connected building would exceed world or scale limits. No buildings changed."); return false; }
    return true;
}

void FBSTransformNative::PrepareNetworks(UWorld* World, const TArray<FBSNativeTransform>& Changes)
{
    auto* Buildables = AFGBuildableSubsystem::Get(World);
    if (!Buildables || !Buildables->HasAuthority()) return;
    TSet<AFGConveyorChainActor*> Chains;
    for (const auto& C : Changes)
        if (auto* Belt = Cast<AFGBuildableConveyorBase>(C.Building.Actor))
            if (auto* Chain = Belt->GetConveyorChainActor()) Chains.Add(Chain);
    // Native teardown returns each item to its owning belt before geometry
    // changes. The subsystem then rebuilds chains with new lengths and paths.
    for (auto* Chain : Chains) Buildables->ForceDestroyChainActor(Chain);
    for (const auto& C : Changes)
        if (auto* Belt = Cast<AFGBuildableConveyorBase>(C.Building.Actor)) Buildables->RemoveConveyor(Belt);
}

void FBSTransformNative::RebuildSpline(AFGBuildable* Actor)
{
    auto* Interface = Cast<IFGSplineBuildableInterface>(Actor);
    if (!Interface) return;
    auto* Spline = Interface->GetSplineComponent();
    TInlineComponentArray<UFGConnectionComponent*> Connections(Actor);
    TArray<TEnumAsByte<EComponentMobility::Type>> Mobilities;
    for(auto* Connection:Connections) { Mobilities.Add(Connection->Mobility); Connection->Mobility=EComponentMobility::Movable; }
    UFGSplineCollisionComponent* Collision = nullptr;
    FVector Extent, Offset = FVector::ZeroVector;
    float Spacing = 100;
    if (auto* Belt = Cast<AFGBuildableConveyorBelt>(Actor))
    {
        Belt->PopulateSplineComponentFromSplinePointsData();
        Belt->mLength = Spline->GetSplineLength();
        Belt->SetupConnections(); Belt->UpdateVisibilityMesh();
        Belt->mCachedClearanceData.Reset();
        Collision = Belt->mCollisionComponent; Extent = Belt->COLLISION_EXTENT; Spacing = Belt->COLLISION_SPACING;
    }
    if (auto* Pipe = Cast<AFGBuildablePipeBase>(Actor))
    {
        Pipe->PopulateSplineComponentFromSplinePointsData();
        Pipe->mLength = Spline->GetSplineLength(); Pipe->SetupConnections(); Pipe->mCachedClearanceData.Reset();
        Collision = Pipe->mCollisionComponent; Extent = Pipe->COLLISION_EXTENT; Spacing = Pipe->COLLISION_SPACING;
    }
    if (auto* Track = Cast<AFGBuildableRailroadTrack>(Actor))
    {
        Track->PopulateSplineComponentFromSplinePointsData();
        Track->mLength = Spline->GetSplineLength();
        // Native track-position assignment is only legal while the connection
        // component is unregistered. Unregistering preserves graph edges.
        TArray<UFGRailroadTrackConnectionComponent*> Registered;
        for(int32 I=0;I<2;++I)
            if(auto* Connection=Track->GetConnection(I); Connection && Connection->IsRegistered())
            { Registered.Add(Connection); Connection->UnregisterComponent(); }
        Track->SetupConnections();
        for(auto* Connection:Registered) Connection->RegisterComponent();
        Track->mCachedClearanceData.Reset();
        Collision = Track->mCollisionComponent; Extent = Track->COLLISION_EXTENT; Spacing = Track->COLLISION_SPACING; Offset = Track->COLLISION_OFFSET;
    }
    // Native constructors historically assumed an unrotated owner. Make the
    // component frames explicit after their setup, otherwise repeated rotations
    // can feed an already-rotated connector normal back into saved tangents.
    const int32 Last=Spline->GetNumberOfSplinePoints()-1;
    if(Last>=1)
    {
        Interface->GetSplineConnection0()->SetWorldLocationAndRotation(Spline->GetLocationAtSplinePoint(0,ESplineCoordinateSpace::World),(-Spline->GetTangentAtSplinePoint(0,ESplineCoordinateSpace::World)).Rotation());
        Interface->GetSplineConnection1()->SetWorldLocationAndRotation(Spline->GetLocationAtSplinePoint(Last,ESplineCoordinateSpace::World),Spline->GetTangentAtSplinePoint(Last,ESplineCoordinateSpace::World).Rotation());
    }
    if (Collision)
    {
        Collision->DestroyPhysics();
        Collision->mCachedCollisionInfo.Reset();
        TArray<FCachedCollisionBoxInfo> Boxes;
        UFGSplineMeshGenerationLibrary::BuildSplineCollisionInfoBoxesWithVariableSteps(Spline, Extent, Spacing, Boxes, Offset);
        Collision->AddBoxes(Boxes); Collision->CreatePhysics(); Collision->UpdateBounds();
    }
    Actor->Internal_CallRemoveInstances();
    Actor->CallSetupInstances();
    for(int32 I=0;I<Connections.Num();++I) Connections[I]->Mobility=Mobilities[I];
}

void FBSTransformNative::ApplyGeometry(AFGBuildable* Actor, const FBSNativeTransform& C, const FTransform& Before)
{
    auto* Conveyor = Cast<AFGBuildableConveyorBase>(Actor);
    const float OldLength = Conveyor ? Conveyor->GetLength() : 0;
    auto* Track = Cast<AFGBuildableRailroadTrack>(Actor);
    const float OldTrackLength = Track ? Track->GetLength() : 0;
    if (!C.Spline.IsEmpty())
    {
        if (auto* Spline = Cast<IFGSplineBuildableInterface>(Actor))
        {
            *Spline->GetMutableSplinePointData() = C.Spline;
            RebuildSpline(Actor);
            if(Track && Track->HasAuthority())
            {
                // Vehicle positions are offsets on tracks, not actor transforms.
                // Keep their relative place when a track changes length.
                const auto Vehicles=Track->GetVehicles();
                for(AFGRailroadVehicle* Vehicle:Vehicles)
                    if(IsValid(Vehicle) && Vehicle->GetTrackPosition().Track==Track)
                    {
                        auto Position=Vehicle->GetTrackPosition();
                        Position.Offset *= Track->GetLength()/FMath::Max(OldTrackLength,.01f);
                        Vehicle->SetTrackPosition(Position);
                    }
            }
        }
    }
    if (auto* Lift = Cast<AFGBuildableConveyorLift>(Actor); Lift && C.HasLift)
    {
        TGuardValue<TEnumAsByte<EComponentMobility::Type>> Mobility0(Lift->GetConnection0()->Mobility,EComponentMobility::Movable);
        TGuardValue<TEnumAsByte<EComponentMobility::Type>> Mobility1(Lift->GetConnection1()->Mobility,EComponentMobility::Movable);
        Lift->mTopTransform = C.LiftTop;
        Lift->OnRep_TopTransform(); Lift->SetupConnections();
        Lift->mLength = Lift->GetHeight();
        Lift->Internal_CallRemoveInstances(); Lift->CallSetupInstances();
    }
    if (Conveyor)
    {
        const float Ratio = Conveyor->GetLength() / FMath::Max(OldLength, 0.01f);
        for (int32 I = 0; I < Conveyor->mItems.Num(); ++I)
        { Conveyor->mItems[I].Offset *= Ratio; Conveyor->mItems[I].LocalOffset *= Ratio; }
        Conveyor->mItems.ConveyorLength = Conveyor->GetLength();
        Conveyor->MarkItemTransformsDirty();
    }
    if (auto* Wire = Cast<AFGBuildableWire>(Actor); Wire && C.HasWire)
    {
        const FVector Delta0 = C.WireStart - Wire->mConnectionLocations[0];
        const FVector Delta1 = C.WireEnd - Wire->mConnectionLocations[1];
        Wire->mConnectionLocations[0] = C.WireStart; Wire->mConnectionLocations[1] = C.WireEnd;
        Wire->mCachedLength = FVector::Distance(C.WireStart, C.WireEnd);
        if(!C.WirePoints.IsEmpty() && Wire->mWireInstances.Num()!=C.WirePoints.Num()/2) Wire->DestroyWireInstances();
        // The RPC can precede endpoint-owner replication. Use its explicit end
        // positions and retain every strand/socket offset on power towers.
        for (int32 N=0;N<Wire->mWireInstances.Num();++N)
        {
            auto& Instance=Wire->mWireInstances[N];
            Instance.Locations[0] = C.WirePoints.IsValidIndex(N*2) ? C.WirePoints[N*2] : Instance.Locations[0]+Delta0;
            Instance.Locations[1] = C.WirePoints.IsValidIndex(N*2+1) ? C.WirePoints[N*2+1] : Instance.Locations[1]+Delta1;
            for (int32 I=0; I<2; ++I) Instance.CachedRelativeLocations[I] = C.Target.InverseTransformPosition(Instance.Locations[I]);
        }
        if (Wire->mWireInstances.IsEmpty())
        {
            for(int32 I=0;I+1<C.WirePoints.Num();I+=2) Wire->mWireInstances.Add(Wire->CreateWireInstance(C.WirePoints[I],C.WirePoints[I+1],Wire->GetActorTransform()));
            if(Wire->mWireInstances.IsEmpty()) Wire->mWireInstances.Add(Wire->CreateWireInstance(C.WireStart, C.WireEnd, Wire->GetActorTransform()));
        }
        TArray<TPair<TWeakObjectPtr<UStaticMeshComponent>,TEnumAsByte<EComponentMobility::Type>>> WireMobilities;
        for(const auto& Instance:Wire->mWireInstances)
            if(auto* Mesh=Instance.WireMesh.Get()) { WireMobilities.Emplace(Mesh,Mesh->Mobility); Mesh->Mobility=EComponentMobility::Movable; }
        Wire->UpdateWireMeshes();
        for(const auto& Entry:WireMobilities) if(Entry.Key.IsValid()) Entry.Key->Mobility=Entry.Value;
    }
    if (auto* Fluid = Cast<IFGFluidIntegrantInterface>(Actor))
        if (auto* Box = Fluid->GetFluidBox())
        {
            // Preserve contents, capacity and producer pressure. Only elevation
            // belongs to the edit; rebuilding a network must never flush it.
            const double Dz = (Actor->GetActorLocation().Z - Before.GetLocation().Z) * .01;
            Box->Z += Dz; Box->LowZ += Dz; Box->HighZ += Dz;
            if (auto* Spline = Cast<IFGSplineBuildableInterface>(Actor))
            {
                const auto Bounds = Spline->GetSplineComponent()->Bounds;
                Box->LowZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * .01;
                Box->HighZ = (Bounds.Origin.Z + Bounds.BoxExtent.Z) * .01;
                Box->Z = (Box->LowZ + Box->HighZ) * .5;
                Box->Height = FMath::Max(Box->HighZ - Box->LowZ, .01f);
                Box->LaminarHeight = FMath::Min(Box->LaminarHeight, Box->Height);
            }
        }
    Actor->mCachedBounds = Actor->CalculateBounds();
}

void FBSTransformNative::RefreshNetworks(UWorld* World, const TArray<FBSNativeTransform>& Changes)
{
    auto* Pipes = AFGPipeSubsystem::Get(World);
    auto* Buildables = AFGBuildableSubsystem::Get(World);
    for (const auto& C : Changes)
    {
        auto* Actor = C.Building.Actor.Get();
        if (!Actor) continue;
        if(Actor->HasAuthority() && Buildables)
            if(auto* Belt=Cast<AFGBuildableConveyorBase>(Actor)) Buildables->AddConveyor(Belt);
        if (Actor->HasAuthority() && Pipes)
        {
            TInlineComponentArray<UFGPipeConnectionComponent*> Connections(Actor);
            for (auto* Connection : Connections)
                if (auto* Network = Pipes->FindPipeNetwork(Connection->GetPipeNetworkID())) Network->MarkForFullRebuild();
        }
        if (auto* Track = Cast<AFGBuildableRailroadTrack>(Actor))
        {
            Track->UpdateOverlappingTracks();
            if (Track->HasAuthority())
                if (auto* Rails = AFGRailroadSubsystem::Get(World)) Rails->MarkGraphAsChanged(Track->GetTrackGraphID());
        }
        Actor->FlushNetDormancy(); Actor->ForceNetUpdate();
    }
}
