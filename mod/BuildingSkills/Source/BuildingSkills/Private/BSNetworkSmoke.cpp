#include "BSGroups.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Buildables/FGBuildableWire.h"
#include "FGCharacterPlayer.h"
#include "FGBuildableSubsystem.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGCircuitConnectionComponent.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "Resources/FGResourceNode.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodySetup.h"

namespace { TWeakObjectPtr<AFGBuildableConveyorBelt> SimulationBelt; }

// Called only by the explicit fresh-world, save-blocked runtime harness.
bool FBSTransformNative::RunNetworkChecks(AFGCharacterPlayer* Player)
{
    auto* World = Player->GetWorld();
    auto* Buildables = AFGBuildableSubsystem::Get(World);
    auto* Groups = ABSGroupSubsystem::Get(World);
    const FVector Base = Player->GetActorLocation() + FVector(1800, 1800, 0);
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
        const bool Success = Groups->TransformSelection(Player, Selection, Groups->Revision, Mode, Amount, Delta, Result, Error);
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
                Found|=Hit.GetActor()==Target;
                UE_LOG(LogBuildingSkills,Display,TEXT("BELT TARGET TRACE complex=%d actor=%s component=%s target=%s"),Complex,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*GetNameSafe(Target));
            }
        }
        auto* Collision=Target->FindComponentByClass<UFGSplineCollisionComponent>();
        UE_LOG(LogBuildingSkills,Display,TEXT("SPLINE TARGET STATE actor=%s hit=%d registered=%d physics=%d collision=%d response=%d"),*GetNameSafe(Target),Found,Collision && Collision->IsRegistered(),Collision && Collision->IsPhysicsStateCreated(),Collision?int32(Collision->GetCollisionEnabled()):-1,Collision?int32(Collision->GetCollisionResponseToChannel(ECC_GameTraceChannel5)):-1);
        return Found && Collision && Collision->IsRegistered() && Collision->IsPhysicsStateCreated();
    };
    auto SplineSpawn = [&](const TCHAR* Path, const FVector& Offset) -> AFGBuildable*
    {
        auto* A = Spawn(Path, Offset, false);
        if (auto* S = Cast<IFGSplineBuildableInterface>(A))
        {
            *S->GetMutableSplinePointData() = { FSplinePointData(FVector::ZeroVector, FVector(1000,0,0)), FSplinePointData(FVector(1000,0,0), FVector(1000,0,0)) };
            A->FinishSpawning(A->GetActorTransform());
            return A;
        }
        return nullptr;
    };
    auto* Belt = Cast<AFGBuildableConveyorBelt>(SplineSpawn(TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorBeltMk1/Build_ConveyorBeltMk1.Build_ConveyorBeltMk1_C"), FVector::ZeroVector));
    auto* Pipe = Cast<AFGBuildablePipeline>(SplineSpawn(TEXT("/Game/FactoryGame/Buildable/Factory/Pipeline/Build_Pipeline.Build_Pipeline_C"), FVector(0,700,0)));
    auto* Rail = Cast<AFGBuildableRailroadTrack>(SplineSpawn(TEXT("/Game/FactoryGame/Buildable/Factory/Train/Track/Build_RailroadTrack.Build_RailroadTrack_C"), FVector(0,1500,0)));
    Check(TEXT("Native belt, pipe and rail fixtures spawn"), Belt && Pipe && Rail);
    if (!Belt || !Pipe || !Rail) return false;
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
        Check(*FString::Printf(TEXT("%s collision rebuild does not duplicate physics boxes"), *A->GetName()), Collision && Collision->mBodySetup &&
            Collision->mBodySetup->AggGeom.BoxElems.Num()==Collision->mCachedCollisionInfo.Num());
        const FTransform RestoredPose(FRotator(15,25,10),A->GetActorLocation()+FVector(0,0,1500),FVector(1.2,.9,1.1));
        auto* Restored=Buildables->BeginSpawnBuildable(A->GetClass(),RestoredPose);
        auto* RestoredSpline=Cast<IFGSplineBuildableInterface>(Restored);
        if(RestoredSpline) *RestoredSpline->GetMutableSplinePointData()=S->GetSplinePointData();
        if(Restored) Restored->FinishSpawning(RestoredPose);
        Check(*FString::Printf(TEXT("%s reconstructs transformed native spline data and connection frames"),*A->GetName()),RestoredSpline &&
            RestoredSpline->GetSplineConnection1()->GetComponentLocation().Equals(RestoredPose.TransformPosition(S->GetSplinePointData().Last().Location),.1) &&
            RestoredSpline->GetSplineConnection1()->GetForwardVector().Equals(RestoredPose.TransformVector(S->GetSplinePointData().Last().ArriveTangent).GetSafeNormal(),.001));
    }
    auto* Storage = Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C"), FVector(1000,0,0));
    TInlineComponentArray<UFGFactoryConnectionComponent*> Ports(Storage);
    Check(TEXT("Connected storage fixture has conveyor ports"), Storage && !Ports.IsEmpty());
    if (Storage && !Ports.IsEmpty())
    {
        auto* Port = Ports[0];
        for (auto* P : Ports) if (P->GetDirection() != Belt->GetConnection1()->GetDirection()) { Port=P; break; }
        Belt->GetConnection1()->SetConnection(Port);
        const FVector Fixed = Belt->GetConnection0()->GetComponentLocation();
        const FVector End = Port->GetComponentLocation();
        const FVector Delta(0,100,100);
        const bool Applied = Transform({Storage}, EBSTransformMode::RotateZ, 0, Delta);
        UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST belt: applied=%d link=%d start=%s fixed=%s end=%s expected=%s"),Applied,Belt->GetConnection1()->GetConnection()==Port,*Belt->GetConnection0()->GetComponentLocation().ToString(),*Fixed.ToString(),*Belt->GetConnection1()->GetComponentLocation().ToString(),*(End+Delta).ToString());
        Check(TEXT("Moving a connected machine stretches its unselected belt and preserves the link"), Applied && Belt->GetConnection1()->GetConnection()==Port &&
            Belt->GetConnection1()->GetComponentLocation().Equals(End+Delta,.1) && Belt->GetConnection0()->GetComponentLocation().Equals(Fixed,.1));
        Check(TEXT("Belt remains targetable after its connected machine moves"),Targetable(Belt));
        // Exercise the geometry application used by receiving clients, without
        // server network planning. This is not a remote-client replication test.
        const auto ReplicatedChange=Capture(Belt);
        Check(TEXT("Replaying belt geometry preserves Build Gun targeting"),Apply(World,ReplicatedChange) && Targetable(Belt));
        auto* OtherStorage=Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C"),FVector(-1000,0,0));
        TInlineComponentArray<UFGFactoryConnectionComponent*> OtherPorts(OtherStorage);
        for(auto* OtherPort:OtherPorts)
            if(OtherPort->GetDirection()!=Belt->GetConnection0()->GetDirection())
            {
                Belt->GetConnection0()->SetConnection(OtherPort);
                Transform({OtherStorage},EBSTransformMode::RotateX,0,FVector::ZeroVector);
                const FVector Middle=Belt->GetSplineComponent()->GetLocationAtSplineInputKey(.5,ESplineCoordinateSpace::World);
                const bool Moved=Transform({Belt},EBSTransformMode::RotateX,0,FVector(0,0,100));
                Check(TEXT("A selected belt between fixed machines moves its middle while retaining both endpoints"),Moved && Belt->GetSplinePointData().Num()==3 &&
                    Belt->GetConnection0()->GetComponentLocation().Equals(OtherPort->GetComponentLocation(),.1) &&
                    Belt->GetConnection1()->GetComponentLocation().Equals(Port->GetComponentLocation(),.1) &&
                    Belt->GetSplineComponent()->GetLocationAtSplinePoint(1,ESplineCoordinateSpace::World).Equals(Middle+FVector(0,0,100),.1));
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
    auto* Rail2 = Cast<AFGBuildableRailroadTrack>(SplineSpawn(TEXT("/Game/FactoryGame/Buildable/Factory/Train/Track/Build_RailroadTrack.Build_RailroadTrack_C"), FVector(1000,1500,0)));
    if (Rail2)
    {
        Rail->GetConnection(1)->AddConnection(Rail2->GetConnection(0));
        const FVector Fixed=Rail2->GetConnection(1)->GetComponentLocation();
        const bool Applied=Transform({Rail},EBSTransformMode::RotateX,0,FVector(0,100,0));
        Check(TEXT("Rail boundary stretches and preserves its connection graph"),Applied && Rail->GetConnection(1)->GetConnections().Contains(Rail2->GetConnection(0)) &&
            Rail->GetConnection(1)->GetComponentLocation().Equals(Rail2->GetConnection(0)->GetComponentLocation(),.1) &&
            Rail2->GetConnection(1)->GetComponentLocation().Equals(Fixed,.1));
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
        Check(TEXT("Miner rotates and scales while retaining its resource node"),Node && Applied && Miner->GetExtractableResource().GetObject()==Node);
    }
    auto* Lift=Cast<AFGBuildableConveyorLift>(Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorLiftMk1/Build_ConveyorLiftMk1.Build_ConveyorLiftMk1_C"),FVector(-1000,0,0),false));
    Check(TEXT("Conveyor lift fixture spawns"), Lift!=nullptr);
    if(Lift)
    {
        Lift->mTopTransform=FTransform(FVector(0,0,800));
        Lift->FinishSpawning(Lift->GetActorTransform());
        const FVector Before=Lift->GetConnection1()->GetComponentLocation();
        const bool Applied=Transform({Lift},EBSTransformMode::RotateY,15,FVector::ZeroVector);
        Check(TEXT("Conveyor lift rotates with its native endpoints and visual instances"),Applied && !Lift->GetConnection1()->GetComponentLocation().Equals(Before,.1) && !Lift->mInstanceHandles.IsEmpty());
        auto* LiftStorage=Spawn(TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C"),FVector(-1000,0,1000));
        TInlineComponentArray<UFGFactoryConnectionComponent*> LiftPorts(LiftStorage);
        for(auto* Port:LiftPorts)
            if(Port->GetDirection()!=Lift->GetConnection1()->GetDirection())
            {
                Lift->GetConnection1()->SetConnection(Port);
                const FVector Bottom=Lift->GetConnection0()->GetComponentLocation();
                const bool Moved=Transform({LiftStorage},EBSTransformMode::RotateX,0,FVector(0,0,100));
                Check(TEXT("A connected lift stretches to a moved machine and retains its opposite end"),Moved && Lift->GetConnection1()->GetComponentLocation().Equals(Port->GetComponentLocation(),.1) && Lift->GetConnection0()->GetComponentLocation().Equals(Bottom,.1));
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
            SimulationBelt->mItems.Add(Item);
        }
    }
    return Passed;
}

bool FBSTransformNative::RunSettledNetworkChecks(AFGCharacterPlayer* Player, int32 Phase)
{
    bool Passed=true;
    auto Check=[&](const TCHAR* Name,bool Result){Passed &= Result; UE_LOG(LogBuildingSkills,Display,TEXT("RUNTIME ASSERT: %s: %s"),Name,Result?TEXT("PASS"):TEXT("FAIL"));};
    auto* Belt=SimulationBelt.Get();
    auto* Chain=Belt?Belt->GetConveyorChainActor():nullptr;
    Check(TEXT("Conveyor simulation has a live native chain"),IsValid(Chain));
    if(!IsValid(Chain))return false;
    TArray<FConveyorBeltItem*> Items;
    Belt->GetConveyorBeltItems(Items);
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
        const FVector Expected=Belt->GetSplineComponent()->GetLocationAtDistanceAlongSpline(Belt->GetLength()*.5,ESplineCoordinateSpace::World);
        const FVector Actual=Segment?Chain->GetLocationAtDistanceAlongSpline(Segment->GetItemStartsAtLength()+Belt->GetLength()*.5,ESplineCoordinateSpace::World):FVector::ZeroVector;
        UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST chain midpoint expected=%s actual=%s length=%.2f"),*Expected.ToString(),*Actual.ToString(),Belt->GetLength());
        UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST chain pose=%s belt=%s spline=%s start=%.2f offset=%.2f length=%.2f"),*Chain->GetActorTransform().ToString(),*Belt->GetActorTransform().ToString(),*Belt->GetSplineComponent()->GetComponentTransform().ToString(),Segment?Segment->StartsAtLength:0,Segment?Segment->OffsetAtStart:0,Chain->GetSplineComponent()->GetSplineLength());
        if(Segment)for(const auto& P:Segment->SplinePointData) UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST segment point=%s arrive=%s leave=%s"),*P.Location.ToString(),*P.ArriveTangent.ToString(),*P.LeaveTangent.ToString());
        for(int32 I=0;I<Chain->GetSplineComponent()->GetNumberOfSplinePoints();++I)
            UE_LOG(LogBuildingSkills,Display,TEXT("NETWORK TEST chain point=%s tangent=%s"),*Chain->GetSplineComponent()->GetLocationAtSplinePoint(I,ESplineCoordinateSpace::World).ToString(),*Chain->GetSplineComponent()->GetTangentAtSplinePoint(I,ESplineCoordinateSpace::World).ToString());
        Check(TEXT("Rebuilt conveyor simulation follows rotated and scaled belt geometry"),Segment && Actual.Equals(Expected,1));
    }
    return Passed;
}
