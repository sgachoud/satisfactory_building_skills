#include "BSTransformers.h"
#include "BSGroups.h"
#include "BSDeduplication.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "FGBuildableSubsystem.h"
#include "FGCircuitConnectionComponent.h"
#include "FGSplineBuildableInterface.h"
#include "UObject/UnrealType.h"

namespace
{
    // A matching origin alone is unsafe for splines: two different routes can
    // start at the same point. Compare their complete native geometry as well.
    bool ShapeMatches(const FBSBuildingRef& Source,const FBSBuildingRef& Candidate,const FBSTransformer& Rule)
    {
        auto* A=Source.Actor.Get(); auto* B=Candidate.Actor.Get();
        if(!A || !B) return true; // Lightweight structural variants use the same pose.
        if(auto* SA=Cast<IFGSplineBuildableInterface>(A))
        {
            auto* SB=Cast<IFGSplineBuildableInterface>(B); if(!SB) return false;
            const auto& AP=SA->GetSplinePointData(); const auto& BP=SB->GetSplinePointData();
            if(AP.Num()!=BP.Num()) return false;
            for(int32 I=0;I<AP.Num();++I)
                if(!Rule.Point(A->GetActorTransform().TransformPosition(AP[I].Location)).Equals(B->GetActorTransform().TransformPosition(BP[I].Location),.1) ||
                   !Rule.Vector(A->GetActorTransform().TransformVector(AP[I].ArriveTangent)).Equals(B->GetActorTransform().TransformVector(BP[I].ArriveTangent),.1) ||
                   !Rule.Vector(A->GetActorTransform().TransformVector(AP[I].LeaveTangent)).Equals(B->GetActorTransform().TransformVector(BP[I].LeaveTangent),.1)) return false;
        }
        if(auto* WA=Cast<AFGBuildableWire>(A))
        {
            auto* WB=Cast<AFGBuildableWire>(B); if(!WB) return false;
            if(WA->GetConnection(0) && WA->GetConnection(1) && WB->GetConnection(0) && WB->GetConnection(1))
            {
                // Mirrored machines retain their mesh handedness. A socket off
                // the local mirror plane therefore need not be at the reflected
                // world point. Match the corresponding named sockets on the
                // transformed owners, exactly as construction attaches cables.
                auto PortMatches=[&](int32 X,int32 Y)
                {
                    auto* AP=WA->GetConnection(X);auto* BP=WB->GetConnection(Y);
                    auto* AO=Cast<AFGBuildable>(AP->GetOwner());auto* BO=Cast<AFGBuildable>(BP->GetOwner());
                    if(!AO || !BO)return false;
                    const auto Expected=FBSTransformers::BuildingPose(Rule,AO->GetActorTransform(),AO);const auto Actual=BO->GetActorTransform();
                    return AP->GetFName()==BP->GetFName() &&
                        FBSDeduplication::SameType(A->GetWorld(),AO->GetClass(),BO->GetClass()) &&
                        Expected.GetLocation().Equals(Actual.GetLocation(),.1) &&
                        Expected.GetRotation().AngularDistance(Actual.GetRotation())<=FMath::DegreesToRadians(.01) &&
                        Expected.GetScale3D().Equals(Actual.GetScale3D(),.0001);
                };
                return (PortMatches(0,0) && PortMatches(1,1)) || (PortMatches(0,1) && PortMatches(1,0));
            }
            const auto& AP=WA->GetWireInstances(); const auto& BP=WB->GetWireInstances();
            if(AP.Num()!=BP.Num()) return false;
            for(int32 I=0;I<AP.Num();++I)
            {
                const FVector X=Rule.Point(AP[I].Locations[0]),Y=Rule.Point(AP[I].Locations[1]);
                if(!((X.Equals(BP[I].Locations[0],.1) && Y.Equals(BP[I].Locations[1],.1)) ||
                     (X.Equals(BP[I].Locations[1],.1) && Y.Equals(BP[I].Locations[0],.1)))) return false;
            }
        }
        // Lift height is independent of its actor pose. Its native top frame is
        // checked below through reflection so no additional SDK access is needed.
        if(A->IsA<AFGBuildableConveyorLift>())
        {
            const auto* Field=FindFProperty<FStructProperty>(A->GetClass(),TEXT("mTopTransform"));
            if(!Field || !B->IsA<AFGBuildableConveyorLift>()) return false;
            const auto& AT=*Field->ContainerPtrToValuePtr<FTransform>(A);
            const auto& BT=*Field->ContainerPtrToValuePtr<FTransform>(B);
            FBSTransformer Local; Local.Id=Rule.Id; Local.Mirror=Rule.Mirror;
            const auto Expected=Rule.Mirror?Local.Pose(AT):AT;
            if(!Expected.Equals(BT,.001)) return false;
        }
        return true;
    }
}

static TArray<FBSBuildingRef> VisitMatching(UWorld* World,const TArray<FBSTransformer>& Rules,
    const TArray<FBSBuildingRef>& Original,FString& Message,
    const TFunction<bool(const FBSBuildingRef&,const FBSBuildingRef&,const FBSTransformer&)>& Visit={})
{
    TArray<FBSBuildingRef> Result;
    auto* System=World?AFGBuildableSubsystem::Get(World):nullptr;
    if(!System || Rules.IsEmpty() || Original.IsEmpty()) return Result;
    FBox Bounds(ForceInit); for(const auto& Ref:Original) Bounds+=FBSTransformNative::Bounds(World,Ref);
    if(!Bounds.IsValid) return Result;
    bool PlanLimited=false; const auto Nodes=FBSTransformers::Plan(Rules,Bounds.GetCenter(),PlanLimited);
    bool Limited=false;
    TArray<TArray<FBSBuildingRef>> Batches; Batches.SetNum(Nodes.Num()); Batches[0]=Original;
    int32 ExaminedSources=0;
    for(int32 I=1;I<Nodes.Num();++I)
    {
        const auto& Node=Nodes[I]; const auto& Rule=Rules[Node.Transformer];
        for(const auto& Source:Batches[Node.Parent])
        {
            if(++ExaminedSources>FBSTransformers::MaxBuildings) { Limited=true; break; }
            if(!Source.IsAlive(World)) continue;
            const auto Pose=FBSTransformers::BuildingPose(Rule,Source.Transform,Source.Actor);
            FBox Query(ForceInit); const auto Box=FBSTransformNative::Bounds(World,Source);
            for(int32 C=0;C<8;++C) Query+=Rule.Point(FVector(C&1?Box.Max.X:Box.Min.X,C&2?Box.Max.Y:Box.Min.Y,C&4?Box.Max.Z:Box.Min.Z));
            auto* SourceWire=Cast<AFGBuildableWire>(Source.Actor);
            if(SourceWire) for(int32 End=0;End<2;++End) if(auto* Port=SourceWire->GetConnection(End))
                if(auto* Owner=Cast<AFGBuildable>(Port->GetOwner()))
                {
                    const auto OwnerBox=FBSTransformNative::Bounds(World,FBSBuildingRef::FromActor(Owner));
                    for(int32 C=0;C<8;++C)Query+=Rule.Point(FVector(C&1?OwnerBox.Max.X:OwnerBox.Min.X,C&2?OwnerBox.Max.Y:OwnerBox.Min.Y,C&4?OwnerBox.Max.Z:OwnerBox.Min.Z));
                }
            Query=Query.ExpandBy(1);
            TArray<FBSBuildingRef> Candidates; int32 Examined=0;
            FBSTransformNative::QueryLightweightBounds(World,Query,Candidates,Examined);
            TArray<AFGBuildable*> Actors; System->GetCollidingBuildablesInBoundingBox(Actors,Query);
            for(auto* B:Actors) if(IsValid(B) && !B->GetIsDismantled() && !B->GetIsLightweightTemporary())
            {
                Candidates.AddUnique(FBSBuildingRef::FromActor(B));
                if(Source.Actor && Source.Actor->IsA<AFGBuildableWire>())
                    for(auto* Port:TInlineComponentArray<UFGCircuitConnectionComponent*>(B))
                    {
                        TArray<AFGBuildableWire*> Wires; Port->GetWires(Wires);
                        for(auto* Wire:Wires) if(IsValid(Wire) && !Wire->GetIsDismantled()) Candidates.AddUnique(FBSBuildingRef::FromActor(Wire));
                    }
            }
            UClass* SourceClass=Source.LightweightClass?Source.LightweightClass.Get():Source.Actor->GetClass();
            TArray<FBSBuildingRef> Matches;
            for(const auto& Candidate:Candidates)
            {
                UClass* Class=Candidate.LightweightClass?Candidate.LightweightClass.Get():Candidate.Actor->GetClass();
                if((!SourceWire && (!Candidate.Transform.GetLocation().Equals(Pose.GetLocation(),.1) ||
                   Candidate.Transform.GetRotation().AngularDistance(Pose.GetRotation())>FMath::DegreesToRadians(.01))) ||
                   !Candidate.Transform.GetScale3D().Equals(Pose.GetScale3D(),.0001) ||
                   !FBSDeduplication::SameType(World,SourceClass,Class) || !ShapeMatches(Source,Candidate,Rule)) continue;
                // Do not collapse identical overlapping instances into one match.
                // Ambiguous destinations must never choose an arbitrary building.
                Matches.Add(Candidate);
            }
            if(Matches.Num()>1) Message=TEXT("Some transformer targets overlap ambiguously and were skipped.");
            if(Matches.Num()!=1) continue;
            if(Visit && !Visit(Source,Matches[0],Rule)) return Result;
            Batches[I].AddUnique(Matches[0]);
            if(!Original.Contains(Matches[0])) Result.AddUnique(Matches[0]);
        }
        if(Limited) break;
    }
    if(Limited || PlanLimited) Message=TEXT("Transformer operation limit reached; remaining targets were skipped.");
    return Result;
}

TArray<FBSBuildingRef> FBSTransformers::Matching(UWorld* World,const TArray<FBSTransformer>& Rules,
    const TArray<FBSBuildingRef>& Original,FString& Message)
{ return VisitMatching(World,Rules,Original,Message); }

bool FBSTransformers::ExpandTransforms(UWorld* World,const TArray<FBSTransformer>& Rules,
    TArray<FBSNativeTransform>& Changes,FString& Message)
{
    if(Rules.IsEmpty() || Changes.IsEmpty())return true;
    TArray<FBSBuildingRef> Original;
    TSet<uint64> Direct;
    TMap<uint64,FTransform> Targets;
    for(const auto& C:Changes)
    {
        auto Ref=C.Building;if(Ref.LightweightClass)Ref.RuntimeIndex=C.InstanceIndex;
        Original.Add(Ref);const uint64 Key=FBSTransformNative::Key(Ref);
        Direct.Add(Key);Targets.Add(Key,C.Target);
    }
    TArray<FBSNativeTransform> Extra;
    bool Valid=true;
    VisitMatching(World,Rules,Original,Message,[&](const FBSBuildingRef& Source,const FBSBuildingRef& Match,const FBSTransformer& Rule)
    {
        const uint64 Key=FBSTransformNative::Key(Match);
        // Explicit selection always owns its requested motion. Repeated matches
        // cannot apply a second movement to an already-selected building.
        if(Direct.Contains(Key))return true;
        const auto* SourceTarget=Targets.Find(FBSTransformNative::Key(Source));
        if(!SourceTarget)return true;
        const FTransform ExpectedBefore=BuildingPose(Rule,Source.Transform,Source.Actor);
        // Preserve native rounding and derived cable frames. The conjugated
        // movement maps the expected counterpart's before-pose to its after-pose.
        const FTransform Target=Match.Transform.GetRelativeTransform(ExpectedBefore)*BuildingPose(Rule,*SourceTarget,Source.Actor);
        if(const auto* Previous=Targets.Find(Key))
        {
            if(!Previous->Equals(Target,.001))
            {Valid=false;Message=TEXT("Transformers request conflicting moves for one building. No buildings changed.");}
            return Valid;
        }
        if(!Target.IsValid() || Target.GetLocation().GetAbsMax()>2000000 || Target.GetScale3D().GetMin()<.01 || Target.GetScale3D().GetMax()>100)
        {Valid=false;Message=TEXT("A transformer counterpart would exceed world or scale limits. No buildings changed.");return false;}
        auto* Actor=Match.LightweightClass?Match.LightweightClass->GetDefaultObject<AFGBuildable>():Match.Actor.Get();
        if(!FBSTransformNative::Supports(Actor,Message)){Valid=false;return false;}
        FBSNativeTransform C;C.Building=Match;C.InstanceIndex=Match.LightweightClass?Match.ResolveIndex(World):INDEX_NONE;C.Target=Target;
        Extra.Add(C);Targets.Add(Key,Target);
        return true;
    });
    if(!Valid)return false;
    Changes.Append(Extra);
    return true;
}
