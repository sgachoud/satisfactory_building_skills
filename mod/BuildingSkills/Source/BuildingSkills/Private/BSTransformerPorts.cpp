#include "BSTransformers.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildableSplitterSmart.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGSplineBuildableInterface.h"
#include "UObject/UnrealType.h"

namespace
{
    // Obtain the native output ordering using a protected member pointer. No
    // actor is cast to this helper type and no SDK declaration is modified.
    struct FNativeOutputOrder : AFGBuildableConveyorAttachment
    {
        static const TArray<UFGFactoryConnectionComponent*>& Get(const AFGBuildableConveyorAttachment* Owner)
        {return Owner->*&FNativeOutputOrder::mOutputs;}
    };
    struct FPort
    {
        USceneComponent* Component;
        FVector Position,Normal;
        int32 Role;
    };
    TArray<FPort> Ports(const AActor* Actor)
    {
        TArray<FPort> Out;
        if(!Actor)return Out;
        const auto Frame=Actor->GetActorTransform();
        for(auto* C:TInlineComponentArray<USceneComponent*>(Actor))
        {
            int32 Role=INDEX_NONE;
            if(auto* Belt=Cast<UFGFactoryConnectionComponent>(C))
            {
                if(Belt->GetDirection()!=EFactoryConnectionDirection::FCD_SNAP_ONLY)Role=int32(Belt->GetDirection());
            }
            if(auto* Pipe=Cast<UFGPipeConnectionComponent>(C))Role=16+int32(Pipe->GetPipeConnectionType());
            if(Role!=INDEX_NONE)Out.Add({C,Frame.InverseTransformPosition(C->GetComponentLocation()),Frame.InverseTransformVectorNoScale(C->GetForwardVector()).GetSafeNormal(),Role});
        }
        // Stable ordering makes equally good ports deterministic on server/client.
        Out.Sort([](const auto& A,const auto& B){return A.Component->GetFName().LexicalLess(B.Component->GetFName());});
        return Out;
    }
    FVector Reflect(const FVector& V,const FVector& Forward)
    {const FVector Normal(-Forward.Y,Forward.X,0);return V-2*FVector::DotProduct(V,Normal)*Normal;}

    // Minimum-cost one-to-one assignment, never input-to-output or belt-to-pipe.
    double Assign(const TArray<FPort>& From,const TArray<FPort>& To,const TFunction<double(const FPort&,const FPort&)>& Cost,TArray<int32>& Map)
    {
        const int32 N=From.Num();Map.Init(INDEX_NONE,N);
        if(N!=To.Num() || N>64)return 1.e30;
        TArray<double> U,V;U.Init(0,N+1);V.Init(0,N+1);
        TArray<int32> P,Way;P.Init(0,N+1);Way.Init(0,N+1);
        for(int32 I=1;I<=N;++I)
        {
            P[0]=I;int32 J0=0;TArray<double> Min;Min.Init(1.e30,N+1);TArray<bool> Used;Used.Init(false,N+1);
            do
            {
                Used[J0]=true;const int32 I0=P[J0];double Delta=1.e30;int32 J1=0;
                for(int32 J=1;J<=N;++J)if(!Used[J])
                {
                    const double C=(From[I0-1].Role==To[J-1].Role?Cost(From[I0-1],To[J-1]):1.e20)-U[I0]-V[J];
                    if(C<Min[J]){Min[J]=C;Way[J]=J0;}
                    if(Min[J]<Delta){Delta=Min[J];J1=J;}
                }
                for(int32 J=0;J<=N;++J)if(Used[J]){U[P[J]]+=Delta;V[J]-=Delta;}else Min[J]-=Delta;
                J0=J1;
            }while(P[J0]!=0);
            do {const int32 J1=Way[J0];P[J0]=P[J1];J0=J1;}while(J0);
        }
        double Total=0;
        for(int32 J=1;J<=N;++J)
        {
            const int32 I=P[J]-1;
            if(I<0 || From[I].Role!=To[J-1].Role)return 1.e30;
            Map[I]=J-1;Total+=Cost(From[I],To[J-1]);
        }
        return Total;
    }
}

FVector FBSTransformers::MirrorForward(const AActor* Building)
{
    // Flexible geometry is reflected explicitly; its local-Y convention must
    // remain the same as the spline/lift geometry conversion.
    if(!Building || Cast<IFGSplineBuildableInterface>(Building) || Building->IsA<AFGBuildableConveyorLift>())return FVector::ForwardVector;
    const auto Layout=Ports(Building);
    if(Layout.IsEmpty() || Layout.Num()>64)return FVector::ForwardVector;
    TArray<FVector> Candidates{FVector::ForwardVector,FVector::RightVector};
    for(const auto& Port:Layout)
    {
        FVector Axis(Port.Normal.X,Port.Normal.Y,0);if(!Axis.Normalize())continue;
        bool Found=false;for(const auto& C:Candidates)Found |= FMath::Abs(FVector::DotProduct(C,Axis))>.99999;
        if(!Found)Candidates.Add(Axis);
    }
    FVector Best=FVector::ForwardVector;double BestCost=1.e30;
    for(const auto& Axis:Candidates)
    {
        TArray<int32> Map;
        const double Cost=Assign(Layout,Layout,[&](const auto& A,const auto& B)
        {
            // Port direction has priority over small lateral offsets on an
            // asymmetric machine. Distances are normalized in meters.
            return FVector::DistSquared(Reflect(A.Position,Axis),B.Position)/10000.0+
                100*FVector::DistSquared(Reflect(A.Normal,Axis),B.Normal);
        },Map);
        if(Cost<BestCost-.000001){BestCost=Cost;Best=Axis;}
    }
    return Best;
}
FTransform FBSTransformers::BuildingPose(const FBSTransformer& Rule,const FTransform& Pose,const AActor* Building)
{return Rule.Pose(Pose,Rule.Mirror?MirrorForward(Building):FVector::ForwardVector);}

bool FBSTransformers::ReconnectMirrorPorts(const FBSTransformer& Rule,const TMap<AFGBuildable*,AFGBuildable*>& Placed,FString& Error)
{
    TMap<USceneComponent*,USceneComponent*> Remap;
    for(const auto& Pair:Placed)
    {
        const auto Source=Ports(Pair.Key),Target=Ports(Pair.Value);TArray<int32> Map;
        if(Cast<IFGSplineBuildableInterface>(Pair.Key) || Pair.Key->IsA<AFGBuildableConveyorLift>())
        {
            for(const auto& Port:Source)
            {
                const auto* Found=Target.FindByPredicate([&](const auto& T){return T.Role==Port.Role && T.Component->GetFName()==Port.Component->GetFName();});
                if(!Found){Error=TEXT("Transformer copy lost a spline connection.");return false;}
                Remap.Add(Port.Component,Found->Component);
            }
            continue;
        }
        const double Score=Assign(Source,Target,[&](const auto& A,const auto& B)
        {
            return FVector::DistSquared(Rule.Point(A.Component->GetComponentLocation()),B.Component->GetComponentLocation())/10000.0+
                100*FVector::DistSquared(Rule.Vector(A.Component->GetForwardVector()),B.Component->GetForwardVector());
        },Map);
        if(Score>=1.e20){Error=TEXT("Transformer copy could not match its input/output ports.");return false;}
        for(int32 I=0;I<Source.Num();++I)Remap.Add(Source[I].Component,Target[Map[I]].Component);
    }
    TArray<TPair<USceneComponent*,USceneComponent*>> Links;
    for(const auto& Pair:Placed)if(auto* Smart=Cast<AFGBuildableSplitterSmart>(Pair.Key))
    {
        const auto& Outputs=FNativeOutputOrder::Get(Smart);auto Rules=Smart->GetSortRules();
        for(auto& Sort:Rules)if(Outputs.IsValidIndex(Sort.OutputIndex))
        {
            auto* Target=Remap.FindRef(Outputs[Sort.OutputIndex]);
            const int32 Index=Outputs.IndexOfByPredicate([&](auto* Port){return Target && Port->GetFName()==Target->GetFName();});
            if(Index==INDEX_NONE){Error=TEXT("Transformer copy could not map a splitter output rule.");return false;}
            Sort.OutputIndex=Index;
        }
        // The callback precedes BeginPlay, which builds the native sort caches.
        // Preserve the source filters on their corresponding mirrored outlets.
        auto* Field=FindFProperty<FArrayProperty>(Pair.Value->GetClass(),TEXT("mSortRules"));
        if(!Field){Error=TEXT("Transformer copy could not restore splitter output rules.");return false;}
        *Field->ContainerPtrToValuePtr<TArray<FSplitterSortRule>>(Pair.Value)=MoveTemp(Rules);
    }
    for(const auto& Pair:Remap)
    {
        USceneComponent* Other=nullptr;
        if(auto* Belt=Cast<UFGFactoryConnectionComponent>(Pair.Key))Other=Belt->GetConnection();
        if(auto* Pipe=Cast<UFGPipeConnectionComponent>(Pair.Key))Other=Pipe->GetConnection();
        if(auto** Target=Remap.Find(Other))Links.Add({Pair.Value,*Target});
    }
    // All mappings are validated before modifying only the newly created ports.
    for(const auto& Pair:Remap)
    {
        if(auto* Belt=Cast<UFGFactoryConnectionComponent>(Pair.Value))Belt->ClearConnection();
        if(auto* Pipe=Cast<UFGPipeConnectionComponent>(Pair.Value))Pipe->ClearConnection();
    }
    for(const auto& Link:Links)
    {
        if(auto* Belt=Cast<UFGFactoryConnectionComponent>(Link.Key);Belt && !Belt->GetConnection())Belt->SetConnection(CastChecked<UFGFactoryConnectionComponent>(Link.Value));
        if(auto* Pipe=Cast<UFGPipeConnectionComponent>(Link.Key);Pipe && !Pipe->GetConnection())Pipe->SetConnection(CastChecked<UFGPipeConnectionComponent>(Link.Value));
    }
    return true;
}
