#include "BSTransformers.h"
#include "BSGroups.h"
#include "BSClipboard.h"
#include "BSBlueprintBounds.h"
#include "BSBlueprintScale.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGBlueprintSubsystem.h"
#include "FGBlueprintProxy.h"
#include "FGInventoryComponent.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGDismantleInterface.h"
#include "FGSplineBuildableInterface.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildableWire.h"
#include "FGBuildableSubsystem.h"
#include "FGCircuitConnectionComponent.h"
#include "Equipment/FGBuildGun.h"
#include "Patching/NativeHookManager.h"
#include "Misc/ScopeExit.h"

namespace
{
    thread_local TArray<FBSBuildingRef>* NewInstances=nullptr;
    void Notice(UBSGroupRemoteCallObject* RCO,const FString& Text)
    {
        if(auto* P=RCO->GetOwnerPlayerCharacter()) if(auto* PC=Cast<AFGPlayerController>(P->GetController()))
        { FChatMessageStruct M; M.MessageType=EFGChatMessageType::CMT_SystemMessage; M.MessageText=FText::FromString(Text); PC->Client_SendChatMessage(M); }
    }
    bool Editing(UBSGroupRemoteCallObject* RCO)
    {
        auto* P=RCO->GetOwnerPlayerCharacter(); auto* Gun=P?P->GetBuildGun():nullptr;
        return P && P->HasAuthority() && P->IsBuildGunEquipped() && Gun && UBSEditModeComponent::IsEditDescriptor(Gun->GetCurrentBuildGunMode());
    }
    FVector ReflectLocal(FVector P) { P.Y=-P.Y; return P; }
}
void UBSGroupRemoteCallObject::Server_AddTransformer_Implementation(const FBSBuildingRef& Anchor)
{
    FString Message;
    auto* P=GetOwnerPlayerCharacter(); auto* B=Editing(this)?Anchor.ResolveActor(GetWorld()):nullptr;
    if(!B || !Anchor.IsAlive(GetWorld())) Message=TEXT("Define an anchor in Edit before adding a transformer.");
    else if(Transformers.Num()>=FBSTransformers::MaxTransformers) Message=TEXT("The session limit is 16 transformers.");
    else if(FVector::DistSquared(B->GetActorLocation(),P->GetActorLocation())>FMath::Square(P->GetBuildGun()->GetBuildGunRange()+1000.f)) Message=TEXT("Move within Build Gun range of the anchor.");
    else
    {
        FBSTransformer Rule; Rule.Id=FGuid::NewGuid(); Rule.Frame=FTransform(B->GetActorQuat(),B->GetActorLocation());
        Rule.OriginalAnchor=Rule.Frame;
        if(Rule.Valid()) { Transformers.Add(Rule); ++TransformerRevision; Message=TEXT("Transformer added. Open K to configure it."); }
    }
    Client_Transformers(Transformers,TransformerRevision,Message,TransformersBuildAsBlueprint);
}
void UBSGroupRemoteCallObject::Server_SetTransformers_Implementation(const TArray<FBSTransformer>& List,int32 ExpectedRevision,bool BuildAsBlueprint)
{
    FString Message;
    bool Valid=Editing(this) && ExpectedRevision==TransformerRevision && List.Num()<=FBSTransformers::MaxTransformers;
    TSet<FGuid> Seen;
    for(const auto& Rule:List)
    {
        const auto* Old=Transformers.FindByPredicate([&](const auto& R){return R.Id==Rule.Id;});
        Valid &= Rule.Valid() && Old && Old->OriginalAnchor.Equals(Rule.OriginalAnchor,.0001) && !Seen.Contains(Rule.Id);
        Seen.Add(Rule.Id);
    }
    if(Valid) { Transformers=List; TransformersBuildAsBlueprint=BuildAsBlueprint; ++TransformerRevision; Message=TEXT("Transformer settings applied for this session."); }
    else Message=TEXT("Transformer list changed or contains invalid settings. Reopen K and retry.");
    Client_Transformers(Transformers,TransformerRevision,Message,TransformersBuildAsBlueprint);
}
void UBSGroupRemoteCallObject::Client_Transformers_Implementation(const TArray<FBSTransformer>& List,int32 NewRevision,const FString& Message,bool BuildAsBlueprint)
{
    Transformers=List; TransformerRevision=NewRevision; TransformersBuildAsBlueprint=BuildAsBlueprint;
    if(auto* P=GetOwnerPlayerCharacter()) if(auto* Edit=P->FindComponentByClass<UBSEditModeComponent>()) Edit->BlueprintMessage(Message);
}
void FBSTransformers::Install()
{
    if(WITH_EDITOR) return;
    SUBSCRIBE_METHOD(AFGLightweightBuildableSubsystem::AddFromBuildableInstanceData,
        [](auto& Scope,AFGLightweightBuildableSubsystem* System,TSubclassOf<AFGBuildable> Class,FRuntimeBuildableInstanceData& Data,bool FromSave,int32 SavedIndex,uint16 Id,AActor* Instigator,int32 BlueprintIndex)
        {
            const int32 Index=Scope(System,Class,Data,FromSave,SavedIndex,Id,Instigator,BlueprintIndex);
            if(NewInstances && Index!=INDEX_NONE) NewInstances->Add(FBSBuildingRef::FromLightweight(System->GetWorld(),Class,Index));
        });
}
bool FBSTransformers::Copy(UBSGroupRemoteCallObject* Owner,const TArray<FBSBuildingRef>& Source,const FBSTransformer& Rule,TArray<FBSBuildingRef>& Result,FString& Error)
{
    auto* P=Owner?Owner->GetOwnerPlayerCharacter():nullptr;
    auto* System=P?AFGBlueprintSubsystem::Get(P->GetWorld()):nullptr;
    auto* Inventory=P?P->GetInventory():nullptr;
    if(!P || !P->HasAuthority() || !System || !Inventory || Source.IsEmpty() || !Rule.Valid()) return false;
    // Identical lightweight instances cannot be addressed unambiguously by
    // selection/undo. This includes a foundation lying on its mirror plane,
    // or two transformers producing the same placement. Reject before spawning
    // so the existing instance and its history remain intact.
    for(const auto& Ref:Source)if(Ref.LightweightClass)
    {
        auto Target=Ref;Target.Transform=Rule.Pose(Ref.Transform);Target.RuntimeIndex=INDEX_NONE;
        if(Target.ResolveIndex(P->GetWorld())!=INDEX_NONE)
        {Error=TEXT("Transformer copy skipped: an identical building already occupies its destination.");return false;}
    }
    auto* Clipboard=NewObject<UBSClipboard>(Owner); Clipboard->RequestId=FGuid::NewGuid();
    FBSSelection Selection; Selection.Buildings=Source;
    if(auto* Groups=ABSGroupSubsystem::Get(P->GetWorld()))
        for(const auto& G:Groups->Groups) if(!G.Parent.IsValid())
        { TArray<FBSBuildingRef> Members; FBSGroupTree::Collect(Groups->Groups,G.Id,Members); bool All=!Members.IsEmpty(); for(const auto& Ref:Members) All &= Source.Contains(Ref); if(All)Selection.Groups.Add(G.Id); }
    if(!Clipboard->Capture(Selection,Error,{},false)) return false;
    TMap<int32,FString> Paths;
    if(!ReadBuildingSkillsBlueprintObjectPaths(Clipboard->Data,Paths)) return false;
    TArray<AFGBuildable*> Originals;
    for(const auto& Ref:Clipboard->Members) Originals.Add(Ref.ResolveActor(P->GetWorld()));
    TMap<AFGBuildable*,AFGBuildable*> External, Placed;
    // A separately built cable must connect to the corresponding repeated
    // machines, never back to the original circuit. Match exact placement and
    // component identity; ambiguous or missing counterparts fail closed.
    auto Counterpart=[&](AFGBuildable* Original)->AFGBuildable*
    {
        if(!Original) return nullptr;
        if(auto* Cached=External.Find(Original))return *Cached;
        const FTransform Target=BuildingPose(Rule,Original->GetActorTransform(),Original);
        const FBox Before=FBSTransformNative::Bounds(P->GetWorld(),FBSBuildingRef::FromActor(Original));
        FBox Query(ForceInit);
        for(int32 I=0;I<8;++I)Query+=Rule.Point(FVector(I&1?Before.Max.X:Before.Min.X,I&2?Before.Max.Y:Before.Min.Y,I&4?Before.Max.Z:Before.Min.Z));
        TArray<AFGBuildable*> Nearby;AFGBuildableSubsystem::Get(P->GetWorld())->GetCollidingBuildablesInBoundingBox(Nearby,Query.ExpandBy(2));
        AFGBuildable* Match=nullptr;
        for(auto* B:Nearby)if(IsValid(B) && B!=Original && !Originals.Contains(B) && B->GetClass()==Original->GetClass()
            && B->GetActorLocation().Equals(Target.GetLocation(),.1) && B->GetActorQuat().Equals(Target.GetRotation(),.0001) && B->GetActorScale3D().Equals(Target.GetScale3D(),.0001))
        {if(Match && Match!=B){Match=nullptr;break;}Match=B;}
        External.Add(Original,Match);return Match;
    };
    auto PortOn=[](UFGCircuitConnectionComponent* SourcePort,AFGBuildable* Actor)->UFGCircuitConnectionComponent*
    {if(!SourcePort || !Actor)return nullptr;return FindObject<UFGCircuitConnectionComponent>(Actor,*SourcePort->GetPathName(SourcePort->GetOwner()));};
    TMap<UFGCircuitConnectionComponent*,int32> Needed;
    for(auto* B:Originals)if(auto* Wire=Cast<AFGBuildableWire>(B))for(int32 End=0;End<2;++End)
    {
        auto* Port=Wire->GetConnection(End);auto* Other=Port?Cast<AFGBuildable>(Port->GetOwner()):nullptr;
        if(Other && Originals.Contains(Other))continue;
        auto* Target=PortOn(Port,Counterpart(Other));
        if(!Target || ++Needed.FindOrAdd(Target)>Target->GetNumFreeConnections())
        {Error=TEXT("Transformer cable skipped: its repeated connection is missing, ambiguous or full.");return false;}
    }
    if(Rule.Mirror) for(auto& G:Clipboard->Hierarchy) G.Pivot=Rule.Point(G.Pivot);
    Owner->Clipboards.Add(Clipboard);
    ON_SCOPE_EXIT { Owner->Clipboards.Remove(Clipboard); };
    TArray<FItemAmount> Cost; Clipboard->Descriptor->GetBlueprintCost(Cost);
    const bool Free=Inventory->GetNoBuildCost();
    if(!Free) for(const auto& Item:Cost) if(!Inventory->HasItems(Item.ItemClass,Item.Amount))
    { Error=TEXT("Not enough materials for a transformer copy."); return false; }
    TArray<FBSBuildingRef> Instances;
    TGuardValue<TArray<FBSBuildingRef>*> Guard(NewInstances,&Instances);
    TArray<AFGBuildable*> Buildings, Serialized;
    const FTransform Origin=Rule.Mirror?Clipboard->SourceOrigin:Rule.Pose(Clipboard->SourceOrigin);
    AFGBlueprintProxy* Proxy=nullptr;
    auto* SourceProxy=Source[0].Blueprint(P->GetWorld());
    bool WholeBlueprint=SourceProxy!=nullptr;
    for(const auto& Ref:Source) WholeBlueprint &= Ref.Blueprint(P->GetWorld())==SourceProxy;
    if(WholeBlueprint)
    {
        int32 Total=SourceProxy->GetBuildables().Num(); for(const auto& Bundle:SourceProxy->GetLightweightClassAndIndices())Total+=Bundle.Indices.Num();
        WholeBlueprint=Total==Source.Num();
    }
    if(WholeBlueprint)
    {
        const FTransform ProxyFrame=Rule.Pose(SourceProxy->GetActorTransform());
        Proxy=P->GetWorld()->SpawnActorDeferred<AFGBlueprintProxy>(AFGBlueprintProxy::StaticClass(),ProxyFrame,nullptr,P,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Proxy){Error=TEXT("Could not create transformer blueprint identity.");return false;}
        Proxy->mBlueprintName=SourceProxy->GetBlueprintName(); Proxy->mLocalBounds=FBox(ForceInit);
        for(const auto& Ref:Source)
        {
            const FBox Box=FBSTransformNative::Bounds(P->GetWorld(),Ref);
            for(int32 Corner=0;Corner<8;++Corner)
                Proxy->mLocalBounds+=ProxyFrame.InverseTransformPosition(Rule.Point(FVector(Corner&1?Box.Max.X:Box.Min.X,Corner&2?Box.Max.Y:Box.Min.Y,Corner&4?Box.Max.Z:Box.Min.Z)));
        }
    }
    const TFunction<void(AFGBuildable*,int32)> Adjust=[&](AFGBuildable* B,int32 Index)
    {
        Serialized.Add(B);
        if(const auto* Path=Paths.Find(Index))
        {const int32 Member=Clipboard->MemberPaths.IndexOfByKey(*Path);if(Originals.IsValidIndex(Member))Placed.Add(Originals[Member],B);}
        if(!Rule.Mirror) return;
        const FTransform Before=B->GetActorTransform();
        if(auto* Root=B->GetRootComponent())
        {
            TGuardValue<TEnumAsByte<EComponentMobility::Type>> Movable(Root->Mobility,EComponentMobility::Movable);
            B->SetActorTransform(BuildingPose(Rule,Before,B));
        }
        // Reflect spline shape in local Y as well as reflecting its actor frame.
        // Meshes themselves retain their original chirality and positive scale.
        if(auto* Spline=Cast<IFGSplineBuildableInterface>(B); Spline && Spline->GetMutableSplinePointData())
            for(auto& Point:*Spline->GetMutableSplinePointData())
            { Point.Location=ReflectLocal(Point.Location); Point.ArriveTangent=ReflectLocal(Point.ArriveTangent); Point.LeaveTangent=ReflectLocal(Point.LeaveTangent); }
        if(auto* Lift=Cast<AFGBuildableConveyorLift>(B))
        {
            FBSTransformer Local; Local.Id=Rule.Id;
            Lift->mTopTransform=Local.Pose(Lift->mTopTransform);
        }
    };
    bool ConnectionsOK=true;
    const TFunction<void()> Register=[&]()
    {
        if(Rule.Mirror)ConnectionsOK=ReconnectMirrorPorts(Rule,Placed,Error);
        if(Proxy)for(auto* B:Serialized){B->SetBlueprintProxy(Proxy);if(!B->ShouldConvertToLightweight())Proxy->RegisterBuildable(B);}
        for(const auto& Pair:Placed)if(auto* SourceWire=Cast<AFGBuildableWire>(Pair.Key))
        {
            auto* Wire=Cast<AFGBuildableWire>(Pair.Value);UFGCircuitConnectionComponent* Ends[2]={};
            for(int32 End=0;End<2;++End)
            {
                auto* Port=SourceWire->GetConnection(End);auto* Other=Port?Cast<AFGBuildable>(Port->GetOwner()):nullptr;
                Ends[End]=PortOn(Port,Placed.Contains(Other)?Placed[Other]:External.FindRef(Other));
            }
            if(!Wire || !Ends[0] || !Ends[1])ConnectionsOK=false;
            else if(Wire->GetConnection(0)!=Ends[0] || Wire->GetConnection(1)!=Ends[1])
            {Wire->Disconnect();ConnectionsOK &= Wire->Connect(Ends[0],Ends[1]);}
        }
    };
    System->LoadStoredBlueprint(Clipboard->Descriptor,Origin,Buildings,false,nullptr,P,Proxy,Adjust,Register);
    for(auto* B:Buildings) if(IsValid(B) && !B->ShouldConvertToLightweight()) Result.Add(FBSBuildingRef::FromActor(B));
    Result.Append(Instances);
    if(Rule.Mirror && ConnectionsOK)
    {
        // Asymmetric meshes cannot put every port at its reflected position.
        // Fit copied flexible connections to the actual ports, within this copy.
        TArray<FBSNativeTransform> Changes;
        for(const auto& Pair:Placed)if(!Pair.Value->ShouldConvertToLightweight())Changes.Add(FBSTransformNative::Capture(Pair.Value));
        ConnectionsOK=FBSTransformNative::PlanNetworks(Changes,Error);
        if(ConnectionsOK)
        {
            FBSTransformNative::PrepareNetworks(P->GetWorld(),Changes);
            for(const auto& Change:Changes)ConnectionsOK &= FBSTransformNative::Apply(P->GetWorld(),Change);
            FBSTransformNative::RefreshNetworks(P->GetWorld(),Changes);
        }
    }
    if(!ConnectionsOK || Serialized.Num()!=Source.Num() || Result.Num()!=Source.Num())
    {
        for(const auto& Ref:Instances) if(Ref.IsAlive(P->GetWorld())) AFGLightweightBuildableSubsystem::Get(P->GetWorld())->RemoveByInstanceIndex(Ref.LightweightClass,Ref.RuntimeIndex);
        for(auto* B:Buildings) if(IsValid(B) && !B->ShouldConvertToLightweight()) IFGDismantleInterface::Execute_Dismantle(B);
        if(Proxy)Proxy->Destroy();
        Result.Reset(); Error=TEXT("A transformer copy was incomplete and has been rolled back."); return false;
    }
    if(Proxy){Proxy->FinishSpawning(Proxy->GetActorTransform());Proxy->OnRep_LocalBounds();}
    if(!Free) for(const auto& Item:Cost) Inventory->Remove(Item.ItemClass,Item.Amount);
    return true;
}
bool FBSTransformers::BuildAsBlueprint(UBSGroupRemoteCallObject* Owner,const TArray<FBSBuildingRef>& Buildings)
{
    auto* Player=Owner?Owner->GetOwnerPlayerCharacter():nullptr;
    if(!Player || !Player->HasAuthority() || Buildings.IsEmpty()) return false;
    auto* World=Player->GetWorld(); auto* LW=AFGLightweightBuildableSubsystem::Get(World);
    FBox Bounds(ForceInit); TArray<int32> Indices; TSet<AFGBlueprintProxy*> Previous;
    // Validate the entire action before changing any existing blueprint membership.
    for(const auto& Ref:Buildings)
    {
        if(!Ref.IsAlive(World)) return false;
        const int32 Index=Ref.LightweightClass?Ref.ResolveIndex(World):INDEX_NONE;
        if(Ref.LightweightClass && (!LW || !LW->GetRuntimeDataForBuildableClassAndIndex(Ref.LightweightClass,Index))) return false;
        Indices.Add(Index); Bounds+=FBSTransformNative::Bounds(World,Ref);
        if(auto* Old=Ref.Blueprint(World)) Previous.Add(Old);
    }
    if(!Bounds.IsValid) return false;
    FVector Origin=Bounds.GetCenter(); Origin.Z=Bounds.Min.Z;
    const FTransform Frame(Origin);
    auto* Proxy=World->SpawnActorDeferred<AFGBlueprintProxy>(AFGBlueprintProxy::StaticClass(),Frame,nullptr,Player,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Proxy) return false;
    Proxy->mBlueprintName=FText::FromString(TEXT("Transformer placement"));
    Proxy->mLocalBounds=FBox(Bounds.Min-Origin,Bounds.Max-Origin);
    for(int32 I=0;I<Buildings.Num();++I)
    {
        const auto& Ref=Buildings[I]; auto* Old=Ref.Blueprint(World);
        if(IsValid(Old)) Old->FlushNetDormancy();
        if(Ref.LightweightClass)
        {
            if(IsValid(Old)) Old->UnregisterLightweightInstance(Ref.LightweightClass,Indices[I]);
            LW->GetRuntimeDataForBuildableClassAndIndex(Ref.LightweightClass,Indices[I])->BlueprintProxy=Proxy;
            if(auto* Temporary=LW->FindTemporaryByBuildableClassAndIndex(Ref.LightweightClass,Indices[I])) Temporary->SetBlueprintProxy(Proxy);
            Proxy->RegisterLightweightInstance(Ref.LightweightClass,Indices[I]);
        }
        else
        {
            auto* B=Ref.Actor.Get(); B->FlushNetDormancy();
            if(IsValid(Old)) Old->UnregisterBuildable(B);
            B->SetBlueprintProxy(Proxy); Proxy->RegisterBuildable(B); B->ForceNetUpdate();
        }
    }
    Proxy->FinishSpawning(Frame); Proxy->OnRep_LocalBounds(); Proxy->ForceNetUpdate();
    for(auto* Old:Previous) if(IsValid(Old)) { Old->ForceNetUpdate(); Old->ValidateExistanceOtherwiseSelfDestruct(); }
    return true;
}
void FBSTransformers::Replicate(UBSGroupRemoteCallObject* Owner,const TArray<FBSBuildingRef>& Original)
{
    if(!Owner || Original.IsEmpty() || Owner->Transformers.IsEmpty()) return;
    FBox Bounds(ForceInit);
    for(const auto& Ref:Original) Bounds+=FBSTransformNative::Bounds(Owner->GetWorld(),Ref);
    if(!Bounds.IsValid) return;
    bool Limited=false; const auto Rules=Owner->Transformers;
    const auto Nodes=Plan(Rules,Bounds.GetCenter(),Limited);
    TArray<TArray<FBSBuildingRef>> Batches; Batches.SetNum(Nodes.Num()); Batches[0]=Original;
    int32 Count=0; FString Error;
    for(int32 I=1; I<Nodes.Num(); ++I)
    {
        const auto& Node=Nodes[I]; const auto& Source=Batches[Node.Parent];
        if(Source.IsEmpty()) continue;
        if(Count+Source.Num()>MaxBuildings) { Limited=true; break; }
        FString Failure;
        if(Copy(Owner,Source,Rules[Node.Transformer],Batches[I],Failure)) Count+=Batches[I].Num();
        else if(Error.IsEmpty()) Error=Failure;
    }
    if(Limited) Error=TEXT("Transformer limit reached (64 copies or 2048 buildings per action).");
    if(!Error.IsEmpty()) Notice(Owner,Error);
    UE_LOG(LogBuildingSkills,Verbose,TEXT("Transformers: generated %d buildings; %s"),Count,*Error);
}
