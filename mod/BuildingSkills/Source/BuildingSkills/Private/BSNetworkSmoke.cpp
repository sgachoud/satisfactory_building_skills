#include "BSConveyorAccess.h"
#include "BSFactoryAccess.h"
#include "BSBlueprintBounds.h"
#include "FGNetSignificanceInterface.h"
#include "BSGroups.h"
#include "AbstractInstanceManager.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Buildables/FGBuildableWire.h"
#include "FGCharacterPlayer.h"
#include "FGOutlineComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "FGBuildableSubsystem.h"
#include "FGFactoryConnectionComponent.h"
#include "FGFactoryConnectionStateOwner.h"
#include "FGPipeConnectionComponent.h"
#include "FGCircuitConnectionComponent.h"
#include "FGCustomizationRecipe.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "Resources/FGResourceNode.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/NetSerialization.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "UObject/UnrealType.h"

namespace { TWeakObjectPtr<AFGBuildableConveyorBelt> SimulationBelt; }

// Called only by the explicit fresh-world, save-blocked runtime harness.
bool FBSTransformNative::RunNetworkChecks(AFGCharacterPlayer* Player)
{
    auto* World = Player->GetWorld();
    auto* Buildables = AFGBuildableSubsystem::Get(World);
    auto* Groups = ABSGroupSubsystem::Get(World);
    const FVector Base = Player->GetActorLocation() + FVector(1800, 1800, 1800);
    bool Passed = true;
    auto Check = [&](const TCHAR* Name, bool Result)
    { Passed &= Result; UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME ASSERT: %s: %s"), Name, Result ? TEXT("PASS") : TEXT("FAIL")); };
    auto Spawn = [&](const TCHAR* Path, const FVector& Offset, bool Finish = true) -> AFGBuildable*
    {
        auto* Class = LoadClass<AFGBuildable>(nullptr, Path);
        if (!Class) return nullptr;
        const FTransform T(Base + Offset);
        auto* A = Buildables->BeginSpawnBuildable(Class, T);
        if (A && Finish) A->FinishSpawning(T);
        return A;
    };
    auto Transform = [&](TArray<AFGBuildable*> Actors, EBSTransformMode Mode, double Amount, FVector Delta)
    {
        FBSSelection Selection, Result; FString Error;
        for (auto* A : Actors) if (A) Selection.Buildings.Add(FBSBuildingRef::FromActor(A));
        const int32 TopologyRevision=Groups->Revision, PublishedRevision=Groups->PublishedGroupRevision;
        const uint64 TransformVersion=Groups->TransformSerial;
        const bool Success = Groups->TransformSelection(Player, Selection, Groups->Revision, Mode, Amount, Delta, Result, Error);
        if (Success) Check(TEXT("Transform advances building versions without publishing group topology"),
            Groups->Revision==TopologyRevision && Groups->PublishedGroupRevision==PublishedRevision && Groups->TransformSerial>TransformVersion);
        if (!Success) UE_LOG(LogBuildingSkills, Warning, TEXT("NETWORK TEST transform: %s"), *Error);
        return Success;
    };
    auto Targetable = [&](AFGBuildable* Target)
    {
        auto* Spline=Cast<IFGSplineBuildableInterface>(Target)->GetSplineComponent();
        const FVector Middle=Spline->GetLocationAtDistanceAlongSpline(Spline->GetSplineLength()*.5,ESplineCoordinateSpace::World);
        const FVector Up=Spline->GetUpVectorAtDistanceAlongSpline(Spline->GetSplineLength()*.5,ESplineCoordinateSpace::World);
        bool Found=false;
        for(bool Complex:{false,true})
        {
            TArray<FHitResult> Hits;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildingSkillsBeltTargeting),Complex);
            World->LineTraceMultiByChannel(Hits,Middle+Up*500,Middle-Up*500,ECC_GameTraceChannel5,Params);
            for(const auto& Hit:Hits)
            {
                AActor* Resolved = Hit.GetActor();
                if (auto* Manager = Cast<AAbstractInstanceManager>(Resolved))
                {
                    FInstanceHandle Handle;
                    if (Manager->ResolveHit(Hit, Handle)) Resolved = Manager->GetOwnerByHandle(Handle);
                }
                Found |= Resolved == Target;
                UE_LOG(LogBuildingSkills,Display,TEXT("BELT TARGET TRACE complex=%d actor=%s resolved=%s component=%s target=%s"),Complex,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Resolved),*GetNameSafe(Hit.GetComponent()),*GetNameSafe(Target));
            }
        }
        auto* Collision=Target->FindComponentByClass<UFGSplineCollisionComponent>();
        UE_LOG(LogBuildingSkills,Display,TEXT("SPLINE TARGET STATE actor=%s hit=%d registered=%d physics=%d collision=%d response=%d"),*GetNameSafe(Target),Found,Collision && Collision->IsRegistered(),Collision && Collision->IsPhysicsStateCreated(),Collision?int32(Collision->GetCollisionEnabled()):-1,Collision?int32(Collision->GetCollisionResponseToChannel(ECC_GameTraceChannel5)):-1);
        // Belts and pipes use AbstractInstance collision, while rails use their
        // registered spline component. The actual world trace must resolve the owner.
        return Found;
    };
    auto SplineSpawn = [&](const TCHAR* Path, const FVector& Offset) -> AFGBuildable*
    {
        auto* A = Spawn(Path, Offset, false);
        if (auto* S = Cast<IFGSplineBuildableInterface>(A))
        {
            *S->GetMutableSplinePointData() = { FSplinePointData(FVector::ZeroVector, FVector(1000,0,0)), FSplinePointData(FVector(1000,0,0), FVector(1000,0,0)) };
            A->FinishSpawning(A->GetActorTransform());
            // Native network relevance activates spline collision. Fixtures run
            // synchronously before the relevance manager's next update, so take
            // the same native activation path before asserting the baseline.
            if (auto* Belt = Cast<AFGBuildableConveyorBelt>(A)) IFGNetSignificanceInterface::Execute_GainedNetSignificance(Belt);
            if (auto* Pipe = Cast<AFGBuildablePipeBase>(A)) IFGNetSignificanceInterface::Execute_GainedNetSignificance(Pipe);
            return A;
        }
        return nullptr;
    };
    auto* Belt = Cast<AFGBuildableConveyorBelt>(SplineSpawn(TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorBeltMk1/Build_ConveyorBeltMk1.Build_ConveyorBeltMk1_C"), FVector::ZeroVector));
    auto* Pipe = Cast<AFGBuildablePipeline>(SplineSpawn(TEXT("/Game/FactoryGame/Buildable/Factory/Pipeline/Build_Pipeline.Build_Pipeline_C"), FVector(0,700,0)));
    auto* Rail = Cast<AFGBuildableRailroadTrack>(SplineSpawn(TEXT("/Game/FactoryGame/Buildable/Factory/Train/Track/Build_RailroadTrack.Build_RailroadTrack_C"), FVector(0,1500,0)));
    Check(TEXT("Native belt, pipe and rail fixtures spawn"), Belt && Pipe && Rail);
    if (!Belt || !Pipe || !Rail) return false;
    Check(TEXT("CL 520140 conveyor connection getters match runtime-reflected identities"),
        Belt->GetConnection0()==BSConveyorAccess::Connection(Belt, 0) &&
        Belt->GetConnection1()==BSConveyorAccess::Connection(Belt, 1));
    {
        const TCHAR* RailPath=TEXT("/Game/FactoryGame/Buildable/Factory/Train/Track/Build_RailroadTrack.Build_RailroadTrack_C");
        auto* Retired=SplineSpawn(RailPath,FVector(-1000,2500,0));
        Check(TEXT("Rail lifetime fixture spawns"),Retired!=nullptr);
        if(Retired)
        {
            const auto RetiredRef=FBSBuildingRef::FromActor(Retired);
            const uint64 RetiredKey=Key(RetiredRef);
            FBSSelection Published;Published.Buildings={RetiredRef};
            Groups->PublishTransforms({Capture(Retired)},Published);
            Groups->ReceivedVersions.Add(RetiredKey,Groups->BuildingVersions.FindRef(RetiredKey));
            Check(TEXT("Retired rail has a real published edit version"),Groups->BuildingVersions.FindRef(RetiredKey)>0);
            Retired->Destroy();
            Check(TEXT("Destroyed rail releases server and client versions before object ID reuse"),
                !Groups->BuildingVersions.Contains(RetiredKey) && !Groups->ReceivedVersions.Contains(RetiredKey)
                && !Groups->SnapshotIndices.Contains(RetiredKey) && !RetiredRef.IsAlive(World));
            auto* Fresh=SplineSpawn(RailPath,FVector(-1000,2500,0));
            FBSSelection Selected,Result;FString Error;FBSTransformOptions Options;
            if(Fresh)Selected.Buildings={FBSBuildingRef::FromActor(Fresh)};
            auto Initial=Selected.Buildings;
            if(!Initial.IsEmpty())Initial[0].TransformVersion=0; // First client observation.
            const bool FirstMove=Fresh && Groups->TransformSelection(Player,Selected,0,EBSTransformMode::RotateX,0,FVector(0,0,10),Result,Error,&Options,&Initial);
            Check(TEXT("Fresh rail accepts its first move with client version zero"),FirstMove);
            if(FirstMove)
            {
                Selected.Buildings={FBSBuildingRef::FromActor(Fresh)};auto Current=Selected.Buildings;
                Check(TEXT("Rail can return to its original pose with a current edit version"),
                    Groups->TransformSelection(Player,Selected,0,EBSTransformMode::RotateX,0,FVector(0,0,-10),Result,Error,&Options,&Current));
                Check(TEXT("Matching pose does not bypass a genuinely stale rail edit version"),
                    Fresh->GetActorTransform().Equals(Initial[0].Transform,.01) &&
                    !Groups->TransformSelection(Player,Selected,0,EBSTransformMode::RotateX,0,FVector(0,0,10),Result,Error,&Options,&Initial));
            }
            if(Fresh)Fresh->Destroy();
        }
    }
    Check(TEXT("Fresh belt can be targeted on the Build Gun trace channel"),Targetable(Belt));
    for (auto* A : TArray<AFGBuildable*>{Belt, Pipe, Rail})
    {
        auto* S = Cast<IFGSplineBuildableInterface>(A);
        const auto OriginalPoints=S->GetSplinePointData();
        const FVector Before = S->GetSplineComponent()->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World);
        const FVector Delta(30,40,50);
        const bool Applied = Transform({A}, EBSTransformMode::RotateX, 0, Delta);
        Check(*FString::Printf(TEXT("%s translation updates native spline and endpoint"), *A->GetName()), Applied &&
            S->GetSplineComponent()->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World).Equals(Before + Delta, .1) &&
            S->GetSplineConnection1()->GetComponentLocation().Equals(Before + Delta, .1));
        bool Modes = true;
        for (int32 I=0; I<10; ++I)
        {
            Modes &= Transform({A}, EBSTransformMode(I), I<3 ? 15 : 1.1, FVector::ZeroVector);
            Modes &= Transform({A}, EBSTransformMode(I), I<3 ? -15 : 1/1.1, FVector::ZeroVector);
        }
        Check(*FString::Printf(TEXT("%s all rotation and scale modes keep reversible native geometry"), *A->GetName()), Modes &&
            S->GetSplineComponent()->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World).Equals(Before + Delta, .5));
        bool Tangents=S->GetSplinePointData().Num()==OriginalPoints.Num();
        if(Tangents)for(int32 I=0;I<OriginalPoints.Num();++I) Tangents &= S->GetSplinePointData()[I].ArriveTangent.Equals(OriginalPoints[I].ArriveTangent,.01) && S->GetSplinePointData()[I].LeaveTangent.Equals(OriginalPoints[I].LeaveTangent,.01);
        Check(*FString::Printf(TEXT("%s inverse steps restore spline tangents"),*A->GetName()),Tangents);
        Check(*FString::Printf(TEXT("%s rebuilt visual and collision data exists"), *A->GetName()), !A->mInstanceHandles.IsEmpty() && A->FindComponentByClass<UFGSplineCollisionComponent>());
        Check(*FString::Printf(TEXT("%s remains targetable after repeated transforms"), *A->GetName()), Targetable(A));
        auto* Collision=A->FindComponentByClass<UFGSplineCollisionComponent>();
        bool UniqueCollision = true;
        int32 ActiveCollision = 0;
        if (Collision && Collision->IsRegistered())
        {
            UniqueCollision = Collision->IsPhysicsStateCreated() && Collision->mBodySetup &&
                Collision->mBodySetup->AggGeom.BoxElems.Num() == Collision->mCachedCollisionInfo.Num();
            ActiveCollision = UniqueCollision ? 1 : 0;
        }
        else
        {
            TSet<uint64> Handles;
            for (const auto& Handle : A->mInstanceHandles)
                if (Handle && Handle->IsInstanced())
                    if (const auto* Component = Handle->GetCollisionInstanceComponent())
                    {
                        const uint64 Key = (uint64(Component->GetUniqueID()) << 32) | Handle->GetCollisionHandleId();
                        UniqueCollision &= !Handles.Contains(Key) && Component->IsRegistered() && Component->IsPhysicsStateCreated();
                        Handles.Add(Key); ++ActiveCollision;
                    }
        }
        Check(*FString::Printf(TEXT("%s native collision backend is active without duplicate handles or boxes"), *A->GetName()), UniqueCollision && ActiveCollision > 0);
        const FTransform RestoredPose(FRotator(15,25,10),A->GetActorLocation()+FVector(0,0,1500),FVector(1.2,.9,1.1));
        auto* Restored=Buildables->BeginSpawnBuildable(A->GetClass(),RestoredPose);
        auto* RestoredSpline=Cast<IFGSplineBuildableInterface>(Restored);
        if(RestoredSpline) *RestoredSpline->GetMutableSplinePointData()=S->GetSplinePointData();
        if(Restored) Restored->FinishSpawning(RestoredPose);
        Check(*FString::Printf(TEXT("%s reconstructs transformed native spline data and connection frames"),*A->GetName()),RestoredSpline &&
            RestoredSpline->GetSplineConnection1()->GetComponentLocation().Equals(RestoredPose.TransformPosition(S->GetSplinePointData().Last().Location),.1) &&
            RestoredSpline->GetSplineConnection1()->GetForwardVector().Equals(RestoredPose.TransformVector(S->GetSplinePointData().Last().ArriveTangent).GetSafeNormal(),.001));
    }
    auto* Storage = Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/ConstructorMk1/Build_ConstructorMk1.Build_ConstructorMk1_C"), FVector(1000.049,0.049,0.049));
    TInlineComponentArray<UFGFactoryConnectionComponent*> Ports(Storage);
    Check(TEXT("Constructor fixture has conveyor ports"), Storage && !Ports.IsEmpty());
    if (Storage && !Ports.IsEmpty())
    {
        {
        FBSSelection Selection, Result; FString Error;
        // Exercise a half-step spawn using the actual network serializer, not
        // a second copy of the production rounding calculation.
        FVector Location = Storage->GetActorLocation();
        Location.X = FMath::FloorToDouble(Location.X) + .25;
        auto MidpointFixture = Capture(Storage);
        MidpointFixture.Target.SetLocation(Location);
        Check(TEXT("Constructor fixture reaches the exact network midpoint"),
            Apply(World, MidpointFixture) && Storage->GetActorLocation().Equals(Location, .000001));
        Selection.Buildings = {FBSBuildingRef::FromActor(Storage)};
        auto Expected = Selection.Buildings;
        FVector_NetQuantize10 ObservedLocation(Location);
        FBitWriter Writer(0, true); bool Serialized = true;
        ObservedLocation.NetSerialize(Writer, nullptr, Serialized);
        FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
        ObservedLocation.NetSerialize(Reader, nullptr, Serialized);
        Expected[0].Transform.SetLocation(ObservedLocation);
        FBSTransformOptions Options;
        Check(TEXT("Unconnected constructor accepts the client spawn-rounded position"),
            Serialized && Groups->TransformSelection(Player, Selection, Groups->Revision, EBSTransformMode::RotateX, 0, FVector(0,0,.01), Result, Error, &Options, &Expected));
        Check(TEXT("Building version rejects an old selection even after a sub-rounding edit"),
            !Groups->TransformSelection(Player, Selection, Groups->Revision, EBSTransformMode::RotateX, 0, FVector(0,0,.01), Result, Error, &Options, &Expected));
        }
        auto* Port = Ports[0];
        for (auto* P : Ports) if (P->GetDirection() != BSConveyorAccess::Connection(Belt, 1)->GetDirection()) { Port=P; break; }
        BSConveyorAccess::Connection(Belt, 1)->SetConnection(Port);
        const FVector Fixed = BSConveyorAccess::Connection(Belt, 0)->GetComponentLocation();
        const FVector End = Port->GetComponentLocation();
        const FVector Delta(0,100,100);
        const auto OriginalMachine = Capture(Storage);
        const auto OriginalBelt = Capture(Belt);
        auto* Outline=UFGOutlineComponent::Get(World);
        TArray<TPair<USceneComponent*,FVector>> ProxyLocations;
        if (Outline)
        {
            Outline->ShowOutline(Storage,EOutlineColor::OC_DISMANTLE);
            if (const auto* State=Outline->GetImmutableOutlineStateForActor(Storage))
            {
                for (const auto& Pair:State->OutlineProxies) if (IsValid(Pair.Value)) ProxyLocations.Emplace(Pair.Value,Pair.Value->GetComponentLocation());
                for (const auto& Pair:State->InstancedOutlineProxies) if (IsValid(Pair.Value)) ProxyLocations.Emplace(Pair.Value,Pair.Value->GetComponentLocation());
            }
        }
        const bool Applied = Transform({Storage}, EBSTransformMode::RotateZ, 0, Delta);
        bool ProxiesFollow=!ProxyLocations.IsEmpty();
        for (const auto& Pair:ProxyLocations) ProxiesFollow &= Pair.Key->GetComponentLocation().Equals(Pair.Value+Delta,.01);
        Check(TEXT("Existing outline proxies follow the transform immediately"),ProxiesFollow);
        if (Outline) Outline->HideOutline(Storage);
        UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST belt: applied=%d link=%d start=%s fixed=%s end=%s expected=%s"),Applied,BSConveyorAccess::Connection(Belt, 1)->GetConnection()==Port,*BSConveyorAccess::Connection(Belt, 0)->GetComponentLocation().ToString(),*Fixed.ToString(),*BSConveyorAccess::Connection(Belt, 1)->GetComponentLocation().ToString(),*(End+Delta).ToString());
        Check(TEXT("Moving a connected machine stretches its unselected belt and preserves the link"), Applied && BSConveyorAccess::Connection(Belt, 1)->GetConnection()==Port &&
            BSConveyorAccess::Connection(Belt, 1)->GetComponentLocation().Equals(End+Delta,.1) && BSConveyorAccess::Connection(Belt, 0)->GetComponentLocation().Equals(Fixed,.1));
        Check(TEXT("Native owner-replicated factory connection state follows a moved machine"),
            Cast<IFGFactoryConnectionStateOwner>(Storage) && Cast<IFGFactoryConnectionStateOwner>(Belt) &&
            Port && Port->IsConnected() && BSConveyorAccess::Connection(Belt, 1)->IsConnected());
        Check(TEXT("Belt remains targetable after its connected machine moves"),Targetable(Belt));
        const auto MovedMachine = Capture(Storage), MovedBelt = Capture(Belt);
        FBSSelection HistoryResult; FString HistoryError;
        Check(TEXT("Undo restores connected machine and full belt geometry"), Groups->RestoreTransform(Player, false, HistoryResult, HistoryError) && Matches(World, OriginalMachine) && Matches(World, OriginalBelt) && Targetable(Belt));
        Check(TEXT("Redo restores connected machine and full belt geometry"), Groups->RestoreTransform(Player, true, HistoryResult, HistoryError) && Matches(World, MovedMachine) && Matches(World, MovedBelt) && Targetable(Belt));
        FBSSelection Selection, Result;
        Selection.Buildings.Add(FBSBuildingRef::FromActor(Storage));
        auto Expected = Selection.Buildings;
        FBSTransformOptions Options; Options.Gesture = FGuid::NewGuid();
        const int32 OldRevision = Groups->Revision;
        Transform({Rail}, EBSTransformMode::RotateX, 0, FVector(0,0,10));
        Check(TEXT("Unrelated edits do not invalidate a selection fingerprint"), Groups->Revision == OldRevision && Groups->TransformSelection(Player, Selection, OldRevision, EBSTransformMode::RotateX, 0, FVector(0,0,10), Result, HistoryError, &Options, &Expected));
        Check(TEXT("Concurrent stale edits to the same building are rejected"), !Groups->TransformSelection(Player, Selection, OldRevision, EBSTransformMode::RotateX, 0, FVector(0,0,10), Result, HistoryError, &Options, &Expected));
        // Exercise the geometry application used by receiving clients, without
        // server network planning. This is not a remote-client replication test.
        const auto ReplicatedChange=Capture(Belt);
        Check(TEXT("Replaying belt geometry preserves Build Gun targeting"),Apply(World,ReplicatedChange) && Targetable(Belt));
        auto* OtherStorage=Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C"),FVector(-1000,0,0));
        TInlineComponentArray<UFGFactoryConnectionComponent*> OtherPorts(OtherStorage);
        for(auto* OtherPort:OtherPorts)
            if(OtherPort->GetDirection()!=BSConveyorAccess::Connection(Belt, 0)->GetDirection())
            {
                BSConveyorAccess::Connection(Belt, 0)->SetConnection(OtherPort);
                Transform({OtherStorage},EBSTransformMode::RotateX,0,FVector::ZeroVector);
                const FVector Middle=Cast<IFGSplineBuildableInterface>(Belt)->GetSplineComponent()->GetLocationAtSplineInputKey(.5,ESplineCoordinateSpace::World);
                auto ClearanceBounds = [&]()
                {
                    TArray<FFGClearanceData> Entries; Belt->GetClearanceData_Implementation(Entries);
                    TArray<FVector> Points;
                    // Conveyor clearances are excluded from snapping, but their
                    // cached physical bounds still need to follow deformation.
                    for (const auto& Entry : Entries) if (Entry.IsValid())
                        BSBlueprintBounds::AppendBox(Entry.ClearanceBox,Entry.RelativeTransform*Belt->GetActorTransform(),Points);
                    FBox Bounds(ForceInit); for (const auto& Point : Points) Bounds += Point;
                    return Bounds;
                };
                const FBox BeforeBounds = ClearanceBounds();
                const bool Moved=Transform({Belt},EBSTransformMode::RotateX,0,FVector(0,0,100));
                const FBox AfterBounds = ClearanceBounds();
                Check(TEXT("Belt deformation refreshes cached native clearance"),Moved && BeforeBounds.IsValid && AfterBounds.IsValid && AfterBounds.Max.Z > BeforeBounds.Max.Z + 25);
                Check(TEXT("A selected belt between fixed machines moves its middle while retaining both endpoints"),Moved && Cast<IFGSplineBuildableInterface>(Belt)->GetSplinePointData().Num()==3 &&
                    BSConveyorAccess::Connection(Belt, 0)->GetComponentLocation().Equals(OtherPort->GetComponentLocation(),.1) &&
                    BSConveyorAccess::Connection(Belt, 1)->GetComponentLocation().Equals(Port->GetComponentLocation(),.1) &&
                    Cast<IFGSplineBuildableInterface>(Belt)->GetSplineComponent()->GetLocationAtSplinePoint(1,ESplineCoordinateSpace::World).Equals(Middle+FVector(0,0,100),.1));
                break;
            }
    }
    auto* Pipe2 = Cast<AFGBuildablePipeline>(SplineSpawn(TEXT("/Game/FactoryGame/Buildable/Factory/Pipeline/Build_Pipeline.Build_Pipeline_C"), FVector(1000,700,0)));
    if (Pipe2)
    {
        Pipe->GetPipeConnection1()->SetConnection(Pipe2->GetPipeConnection0());
        Pipe->GetFluidBox()->Content=2; Pipe2->GetFluidBox()->Content=3;
        const FVector Fixed = Pipe2->GetPipeConnection1()->GetComponentLocation();
        const bool Applied = Transform({Pipe},EBSTransformMode::RotateX,0,FVector(0,0,100));
        UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST pipe: applied=%d end=%s start2=%s end2=%s fixed=%s content=%.2f/%.2f"),Applied,*Pipe->GetPipeConnection1()->GetComponentLocation().ToString(),*Pipe2->GetPipeConnection0()->GetComponentLocation().ToString(),*Pipe2->GetPipeConnection1()->GetComponentLocation().ToString(),*Fixed.ToString(),Pipe->GetFluidBox()->Content,Pipe2->GetFluidBox()->Content);
        Check(TEXT("Pipe boundary stretches while preserving links and fluid contents"),Applied &&
            Pipe->GetPipeConnection1()->GetComponentLocation().Equals(Pipe2->GetPipeConnection0()->GetComponentLocation(),.1) &&
            Pipe2->GetPipeConnection1()->GetComponentLocation().Equals(Fixed,.1) &&
            FMath::IsNearlyEqual(Pipe->GetFluidBox()->Content,2.f) && FMath::IsNearlyEqual(Pipe2->GetFluidBox()->Content,3.f));
        const bool Rotated=Transform({Pipe},EBSTransformMode::RotateZ,15,FVector::ZeroVector);
        const bool Scaled=Transform({Pipe,Pipe2},EBSTransformMode::ScaleX,1.1,FVector::ZeroVector);
        Check(TEXT("Internal pipe junction stays coincident when differently rotated pipes scale together"),Rotated && Scaled &&
            Pipe->GetPipeConnection1()->GetComponentLocation().Equals(Pipe2->GetPipeConnection0()->GetComponentLocation(),.1));
    }
    auto* Rail2 = Cast<AFGBuildableRailroadTrack>(SplineSpawn(TEXT("/Game/FactoryGame/Buildable/Factory/Train/Track/Build_RailroadTrack.Build_RailroadTrack_C"), Rail->GetConnection(1)->GetComponentLocation()-Base));
    if (Rail2)
    {
        Rail->GetConnection(1)->AddConnection(Rail2->GetConnection(0));
        const FVector Fixed=Rail2->GetConnection(1)->GetComponentLocation();
        const bool Applied=Transform({Rail},EBSTransformMode::RotateX,0,FVector(0,100,0));
        Check(TEXT("Rail boundary stretches and preserves its connection graph"),Applied && Rail->GetConnection(1)->GetConnections().Contains(Rail2->GetConnection(0)) &&
            Rail->GetConnection(1)->GetComponentLocation().Equals(Rail2->GetConnection(0)->GetComponentLocation(),.1) &&
            Rail2->GetConnection(1)->GetComponentLocation().Equals(Fixed,.1));
    }
    {
        TArray<AFGBuildable*> Curves;
        TArray<FBSNativeTransform> Original;
        TArray<TArray<FVector>> WorldSamples;
        TArray<TArray<TArray<float>>> Buffers;
        TArray<TArray<FInstanceOwnerHandlePtr>> Handles;
        auto ReadBuffers=[](AFGBuildable* B)
        {
            TArray<TArray<float>> Out;
            for(const auto& Handle:B->mInstanceHandles)if(Handle && Handle->IsInstanced())
                if(auto* Mesh=Handle->GetInstanceComponent())
                {
                    const int32 Count=Mesh->NumCustomDataFloats,Start=int32(Handle->GetHandleID())*Count;
                    TArray<float> Values;
                    if(Count>0 && Mesh->PerInstanceSMCustomData.IsValidIndex(Start+Count-1))Values.Append(Mesh->PerInstanceSMCustomData.GetData()+Start,Count);
                    Out.Add(MoveTemp(Values));
                }
            return Out;
        };
        auto SwatchRecipe=LoadClass<UFGCustomizationRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/-Shared/Customization/Swatches/Recipe_Swatch_Slot1.Recipe_Swatch_Slot1_C"));
        FFactoryCustomizationData Paint;
        if(SwatchRecipe)Paint.SwatchDesc=UFGCustomizationRecipe::GetCustomizationDescriptor(SwatchRecipe);
        for(int32 I=0;I<2;++I)
        {
            auto* Track=Cast<AFGBuildableRailroadTrack>(Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/Train/Track/Build_RailroadTrack.Build_RailroadTrack_C"),FVector(I*4000,6000+I*2000,2500+I*500),false));
            if(!Track)break;
            const FVector StartTangent=I?FVector(4000,2000,0):FVector(4000,0,0);
            const FVector EndTangent=I?FVector(4000,0,0):FVector(4000,2000,0);
            *Track->GetMutableSplinePointData()={FSplinePointData(FVector::ZeroVector,StartTangent),FSplinePointData(FVector(4000,2000,I?0:500),EndTangent)};
            Track->FinishSpawning(Track->GetActorTransform());
            Track->SetCustomizationData_Native(Paint);Track->ApplyCustomizationData_Native(Track->GetCustomizationData_Native());
            Curves.Add(Track);Original.Add(Capture(Track));Handles.Add(Track->mInstanceHandles);Buffers.Add(ReadBuffers(Track));
            TArray<FVector> Samples;for(int32 K=0;K<=16;++K)Samples.Add(Track->GetSplineComponent()->GetLocationAtSplineInputKey(K/16.f,ESplineCoordinateSpace::World));
            WorldSamples.Add(MoveTemp(Samples));
        }
        Check(TEXT("painted curved-rail batch fixture spawns"),Curves.Num()==2 && SwatchRecipe && !Buffers[0].IsEmpty() && !Buffers[1].IsEmpty());
        if(Curves.Num()==2)
        {
            auto* A=Cast<AFGBuildableRailroadTrack>(Curves[0]);auto* B=Cast<AFGBuildableRailroadTrack>(Curves[1]);
            A->GetConnection(1)->AddConnection(B->GetConnection(0));
            const FVector Up(0,0,100);
            bool Moved=Transform(Curves,EBSTransformMode::RotateX,0,Up),Shape=Moved,Visuals=Moved;
            for(int32 I=0;I<Curves.Num();++I)
            {
                auto* S=Cast<IFGSplineBuildableInterface>(Curves[I]);
                for(int32 K=0;K<=16;++K)Shape &= S->GetSplineComponent()->GetLocationAtSplineInputKey(K/16.f,ESplineCoordinateSpace::World).Equals(WorldSamples[I][K]+Up,.01);
                Visuals &= Curves[I]->mInstanceHandles==Handles[I] && ReadBuffers(Curves[I])==Buffers[I]
                    && Curves[I]->GetCustomizationData_Native().SwatchDesc==Paint.SwatchDesc;
            }
            Check(TEXT("raising a curved rail batch preserves every sampled point and tangent shape"),Shape);
            Check(TEXT("raising rails retains their rendered deformation and swatch buffers"),Visuals);
            Check(TEXT("raised rail junction remains reciprocal and coincident"),A->GetConnection(1)->GetConnections().Contains(B->GetConnection(0))
                && B->GetConnection(0)->GetConnections().Contains(A->GetConnection(1)) && A->GetConnection(1)->GetComponentLocation().Equals(B->GetConnection(0)->GetComponentLocation(),.01));
            FBSSelection Result;FString Error;bool Undone=Groups->RestoreTransform(Player,false,Result,Error);
            for(int32 I=0;I<Curves.Num();++I)Undone &= Matches(World,Original[I]) && ReadBuffers(Curves[I])==Buffers[I];
            Check(TEXT("undo restores curved rail poses without losing their painted appearance"),Undone);
            bool Rebuilt=true;
            for(int32 I=0;I<Curves.Num();++I){RebuildSpline(Curves[I]);Rebuilt &= ReadBuffers(Curves[I])==Buffers[I];}
            Check(TEXT("necessary rail mesh rebuild reapplies swatch without overwriting deformation data"),Rebuilt);
            auto* Port=A->GetConnection(0);
            auto* Property=FindFProperty<FArrayProperty>(Port->GetClass(),TEXT("mConnectedComponents"));
            auto* Inner=Property?CastField<FObjectPropertyBase>(Property->Inner):nullptr;
            bool Rejected=false;
            if(Inner)
            {
                // Reproduce a stale live-copy reference synchronously, without
                // invoking native graph callbacks or leaving it for a tick.
                FScriptArrayHelper Links(Property,Property->ContainerPtrToValuePtr<void>(Port));
                const int32 Index=Links.AddValue();Inner->SetObjectPropertyValue(Links.GetRawPtr(Index),Rail->GetConnection(0));
                TArray<FBSNativeTransform> Proposed{Capture(A),Capture(B)};
                for(auto& C:Proposed)C.Target.AddToTranslation(Up);
                Rejected=!PlanNetworks(Proposed,Error);
                Links.RemoveValues(Index);
            }
            Check(TEXT("distant stale rail link is rejected before any world geometry changes"),Rejected && Matches(World,Original[0]) && Matches(World,Original[1]));
        }
    }
    auto* Pole0=Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/PowerTower/Build_PowerTower.Build_PowerTower_C"),FVector(0,-1000,0));
    auto* Pole1=Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/PowerTower/Build_PowerTower.Build_PowerTower_C"),FVector(1000,-1000,0));
    auto* Wire=Cast<AFGBuildableWire>(Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/PowerLine/Build_PowerLine.Build_PowerLine_C"),FVector(0,-1000,0),false));
    auto TowerConnection=[](AFGBuildable* Tower)->UFGCircuitConnectionComponent*
    {
        UFGCircuitConnectionComponent* Best=nullptr;
        if(!Tower) return Best;
        TInlineComponentArray<UFGCircuitConnectionComponent*> Connections(Tower);
        for(auto* Connection:Connections)
            if(!Connection->IsHidden() && (!Best || Connection->GetWireConnectionLocations().Num()>Best->GetWireConnectionLocations().Num())) Best=Connection;
        return Best;
    };
    auto* C0=TowerConnection(Pole0);
    auto* C1=TowerConnection(Pole1);
    Check(TEXT("Power wire fixtures spawn"),Wire && C0 && C1);
    if(Wire && C0 && C1)
    {
        Wire->Connect(C0,C1); Wire->FinishSpawning(Wire->GetActorTransform());
        UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST tower sockets=%d/%d strands=%d"),C0->GetWireConnectionLocations().Num(),C1->GetWireConnectionLocations().Num(),Wire->GetWireInstances().Num());
        const FVector Fixed=C1->GetComponentLocation();
        const bool Applied=Transform({Pole0},EBSTransformMode::RotateX,0,FVector(0,0,100));
        Check(TEXT("Power wire follows moved pole while its other endpoint remains fixed"),Applied && Wire->GetConnection(0)==C0 && Wire->GetConnection(1)==C1 &&
            Wire->GetConnectionLocation(0).Equals(C0->GetComponentLocation(),.1) && Wire->GetConnectionLocation(1).Equals(Fixed,.1) && !Wire->GetWireInstances().IsEmpty());
        const auto OldInstances=Wire->GetWireInstances();
        TArray<FTransform> OldMeshes;
        for(const auto& Instance:OldInstances) if(Instance.WireMesh) OldMeshes.Add(Instance.WireMesh->GetComponentTransform());
        const FTransform OldPose=Pole0->GetActorTransform();
        bool Strands=Transform({Pole0},EBSTransformMode::RotateZ,25,FVector::ZeroVector) && OldInstances.Num()>1 && Wire->GetWireInstances().Num()==OldInstances.Num();
        if(Strands)for(int32 I=0;I<OldInstances.Num();++I)
            Strands &= Wire->GetWireInstances()[I].Locations[0].Equals(Pole0->GetActorTransform().TransformPosition(OldPose.InverseTransformPosition(OldInstances[I].Locations[0])),.1) && Wire->GetWireInstances()[I].Locations[1].Equals(OldInstances[I].Locations[1],.1);
        Check(TEXT("Every power-tower wire strand follows rotated sockets and retains its far endpoint"),Strands);
        bool MeshMoved=false;
        if(OldMeshes.Num()==Wire->GetWireInstances().Num())for(int32 I=0;I<OldMeshes.Num();++I)
            if(auto* Mesh=Wire->GetWireInstances()[I].WireMesh.Get()) MeshMoved |= !Mesh->GetComponentTransform().Equals(OldMeshes[I],.01);
        Check(TEXT("Power-tower strand meshes update with their saved endpoint data"),MeshMoved);
    }
    auto* Miner=Cast<AFGBuildableResourceExtractor>(Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/MinerMK1/Build_MinerMk1.Build_MinerMk1_C"),FVector(-1000,-1000,0),false));
    AFGResourceNode* Node=nullptr;
    if(Miner)
    {
        for(TActorIterator<AFGResourceNode> It(World);It;++It)
            if(Miner->IsAllowedOnResource(*It)) {Node=*It;break;}
        if(Node) Miner->SetResourceNode(Node);
        Miner->FinishSpawning(Miner->GetActorTransform());
        const bool Applied=Transform({Miner},EBSTransformMode::RotateZ,15,FVector::ZeroVector) && Transform({Miner},EBSTransformMode::ScaleXY,1.1,FVector::ZeroVector);
        Check(TEXT("Miner rotates and scales while retaining its resource node"),Node && Applied && BSFactoryAccess::Object(Miner, TEXT("mExtractableResource"))==Node);
    }
    auto* Lift=Cast<AFGBuildableConveyorLift>(Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorLiftMk1/Build_ConveyorLiftMk1.Build_ConveyorLiftMk1_C"),FVector(-1000,0,0),false));
    Check(TEXT("Conveyor lift fixture spawns"), Lift!=nullptr);
    if(Lift)
    {
        BSConveyorAccess::Top(Lift)=FTransform(FVector(0,0,800));
        Lift->FinishSpawning(Lift->GetActorTransform());
        const FVector Before=BSConveyorAccess::Connection(Lift, 1)->GetComponentLocation();
        const bool Applied=Transform({Lift},EBSTransformMode::RotateY,15,FVector::ZeroVector);
        Check(TEXT("Conveyor lift rotates with its native endpoints and visual instances"),Applied && !BSConveyorAccess::Connection(Lift, 1)->GetComponentLocation().Equals(Before,.1) && !Lift->mInstanceHandles.IsEmpty());
        auto* LiftStorage=Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C"),FVector(-1000,0,1000));
        TInlineComponentArray<UFGFactoryConnectionComponent*> LiftPorts(LiftStorage);
        for(auto* Port:LiftPorts)
            if(Port->GetDirection()!=BSConveyorAccess::Connection(Lift, 1)->GetDirection())
            {
                BSConveyorAccess::Connection(Lift, 1)->SetConnection(Port);
                const FVector Bottom=BSConveyorAccess::Connection(Lift, 0)->GetComponentLocation();
                const bool Moved=Transform({LiftStorage},EBSTransformMode::RotateX,0,FVector(0,0,100));
                Check(TEXT("A connected lift stretches to a moved machine and retains its opposite end"),Moved && BSConveyorAccess::Connection(Lift, 1)->GetComponentLocation().Equals(Port->GetComponentLocation(),.1) && BSConveyorAccess::Connection(Lift, 0)->GetComponentLocation().Equals(Bottom,.1));
                break;
            }
    }
    SimulationBelt=Cast<AFGBuildableConveyorBelt>(SplineSpawn(TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorBeltMk1/Build_ConveyorBeltMk1.Build_ConveyorBeltMk1_C"),FVector(0,-1500,0)));
    if(SimulationBelt.IsValid())
    {
        auto* ItemClass=LoadClass<UFGItemDescriptor>(nullptr,TEXT("/Game/FactoryGame/Resource/Parts/IronPlate/Desc_IronPlate.Desc_IronPlate_C"));
        Check(TEXT("Conveyor item descriptor loads"),ItemClass!=nullptr);
        if(ItemClass) for(int32 I=0;I<3;++I)
        {
            FConveyorBeltItem Item{FInventoryItem(ItemClass)}; Item.Offset=100+I*200;
            BSConveyorAccess::Items(SimulationBelt.Get()).Add(Item);
        }
    }
    return Passed;
}

bool FBSTransformNative::RunSettledNetworkChecks(AFGCharacterPlayer* Player, int32 Phase)
{
    bool Passed=true;
    auto Check=[&](const TCHAR* Name,bool Result){Passed &= Result; UE_LOG(LogBuildingSkills,Display,TEXT("RUNTIME ASSERT: %s: %s"),Name,Result?TEXT("PASS"):TEXT("FAIL"));};
    auto* Belt=SimulationBelt.Get();
    auto* Chain=Belt?BSConveyorAccess::Chain(Belt):nullptr;
    Check(TEXT("Conveyor simulation has a live native chain"),IsValid(Chain));
    if(!IsValid(Chain))return false;
    TArray<FConveyorBeltItem*> Items;
    BSConveyorAccess::ReadItems(Belt, Items);
    Check(TEXT("Conveyor simulation preserves all three items"),Items.Num()==3);
    if(Phase==0)
    {
        auto* Groups=ABSGroupSubsystem::Get(Player->GetWorld());
        FBSSelection Selection, Result; FString Error;
        Selection.Buildings.Add(FBSBuildingRef::FromActor(Belt));
        const bool Rotated=Groups->TransformSelection(Player,Selection,Groups->Revision,EBSTransformMode::RotateY,30,FVector::ZeroVector,Result,Error);
        const bool Scaled=Groups->TransformSelection(Player,Selection,Groups->Revision,EBSTransformMode::ScaleX,1.2,FVector::ZeroVector,Result,Error);
        Check(TEXT("A live conveyor chain can rotate and scale"),Rotated && Scaled);
        Check(TEXT("The old conveyor chain is retired before geometry changes"),!IsValid(Chain));
    }
    else
    {
        auto* Segment=Chain->GetSegmentForConveyorBase(Belt);
        const FVector Expected=Cast<IFGSplineBuildableInterface>(Belt)->GetSplineComponent()->GetLocationAtDistanceAlongSpline(BSConveyorAccess::Length(Belt)*.5,ESplineCoordinateSpace::World);
        const FVector Actual=Segment?Chain->GetLocationAtDistanceAlongSpline(Segment->GetItemStartsAtLength()+BSConveyorAccess::Length(Belt)*.5,ESplineCoordinateSpace::World):FVector::ZeroVector;
        UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST chain midpoint expected=%s actual=%s length=%.2f"),*Expected.ToString(),*Actual.ToString(),BSConveyorAccess::Length(Belt));
        UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST chain pose=%s belt=%s spline=%s start=%.2f offset=%.2f length=%.2f"),*Chain->GetActorTransform().ToString(),*Belt->GetActorTransform().ToString(),*Cast<IFGSplineBuildableInterface>(Belt)->GetSplineComponent()->GetComponentTransform().ToString(),Segment?Segment->StartsAtLength:0,Segment?Segment->OffsetAtStart:0,Chain->GetSplineComponent()->GetSplineLength());
        if(Segment)for(const auto& P:Segment->SplinePointData) UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST segment point=%s arrive=%s leave=%s"),*P.Location.ToString(),*P.ArriveTangent.ToString(),*P.LeaveTangent.ToString());
        for(int32 I=0;I<Chain->GetSplineComponent()->GetNumberOfSplinePoints();++I)
            UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST chain point=%s tangent=%s"),*Chain->GetSplineComponent()->GetLocationAtSplinePoint(I,ESplineCoordinateSpace::World).ToString(),*Chain->GetSplineComponent()->GetTangentAtSplinePoint(I,ESplineCoordinateSpace::World).ToString());
        Check(TEXT("Rebuilt conveyor simulation follows rotated and scaled belt geometry"),Segment && Actual.Equals(Expected,1));
    }
    return Passed;
}
