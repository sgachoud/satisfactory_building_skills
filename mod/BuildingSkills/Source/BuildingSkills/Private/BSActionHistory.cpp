#include "BSActionHistory.h"
#include "BSTransformers.h"
#include "BSBlueprintScale.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGActorSaveHeaderTypes.h"
#include "FGBlueprintSubsystem.h"
#include "FGBlueprintProxy.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGHealthComponent.h"
#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGDismantleInterface.h"
#include "FGInventoryLibrary.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "FGCircuitConnectionComponent.h"
#include "FGFluidIntegrantInterface.h"
#include "FGChatManager.h"
#include "Components/SceneComponent.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "Hologram/FGHologram.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"
#include "Buildables/FGBuildableRailroadSignal.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Patching/NativeHookManager.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectIterator.h"

namespace
{
    thread_local UBSActionRecord* Capturing = nullptr;
    thread_local AFGCharacterPlayer* CapturingPlayer = nullptr;
    thread_local bool Restoring = false;
    thread_local UBSActionRecord* RestoringRecord = nullptr;
    thread_local bool DroppingRefund = false;
    thread_local TArray<FBSBuildingRef>* RestoredInstances = nullptr;
    UBSGroupRemoteCallObject* RCOFor(AFGCharacterPlayer* Player)
    {
        auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
        return PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    }
    bool SameItem(const FInventoryItem& A, const FInventoryItem& B)
    { return FInventoryItem::StaticStruct()->CompareScriptStruct(&A, &B, 0); }
    void AppendRefund(TArray<FInventoryStack>& Out, FInventoryStack Stack)
    {
        if (!Stack.HasItems()) return;
        for (auto& Existing : Out) if (SameItem(Existing.Item, Stack.Item)) { Existing.NumItems += Stack.NumItems; return; }
        Out.Add(Stack);
    }
    void Expand(UWorld* World, AActor* Actor, TArray<FBSBuildingRef>& Out, TSet<AActor*>& Visited)
    {
        if (!IsValid(Actor) || Visited.Contains(Actor)) return;
        Visited.Add(Actor);
        if (auto* Proxy = Cast<AFGBlueprintProxy>(Actor))
        {
            for (auto* B : Proxy->GetBuildables()) Expand(World, B, Out, Visited);
            for (const auto& Bundle : Proxy->GetLightweightClassAndIndices())
                for (int32 I : Bundle.Indices) Out.AddUnique(FBSBuildingRef::FromLightweight(World, Bundle.BuildableClass, I));
        }
        else if (auto* B = Cast<AFGBuildable>(Actor))
        {
            // A repeated RPC can arrive while the dismantle effect still owns
            // the actor. Its save dependencies have already been torn down.
            if (B->GetIsDismantled()) return;
            Out.AddUnique(FBSBuildingRef::FromActor(B));
        }
        if (Actor->Implements<UFGDismantleInterface>())
        {
            TArray<AActor*> Children;
            IFGDismantleInterface::Execute_GetChildDismantleActors(Actor, Children);
            IFGDismantleInterface::Execute_GetDismantleDependencies(Actor, Children);
            for (auto* Child : Children) Expand(World, Child, Out, Visited);
        }
    }
    bool Alive(UWorld* World, const FBSBuildingRef& Ref) { return Ref.IsAlive(World); }
    bool PortFree(UActorComponent* Port)
    {
        if (!IsValid(Port)) return false;
        if (auto* Factory = Cast<UFGFactoryConnectionComponent>(Port)) return !Factory->GetConnection();
        if (auto* Pipe = Cast<UFGPipeConnectionComponentBase>(Port)) return !Pipe->GetConnection();
        return true; // Rails permit branches; wire capacity is checked by native Connect.
    }
}

int32 FBSActionHistory::Limit(UWorld* World)
{
    const float Value = FBSTransformSettings::Get(World).ActionHistorySize;
    return FMath::IsFinite(Value) ? FMath::Clamp(FMath::RoundToInt(Value), 0, 256) : 32;
}
void FBSActionHistory::Record(UBSGroupRemoteCallObject* RCO, UBSActionRecord* Entry)
{
    if (!RCO || !Entry || Entry->Members.IsEmpty()) return;
    auto& History = Entry->Deletion ? RCO->DeleteHistory : RCO->BuildHistory;
    History.Add(Entry);
    const int32 Max = Limit(RCO->GetWorld());
    if (History.Num() > Max) History.RemoveAt(0, History.Num() - Max);
}

bool UBSActionRecord::Capture(AFGCharacterPlayer* Player, const TArray<FBSBuildingRef>& Buildings)
{
    Members = Buildings;
    if (Members.IsEmpty() || Members.Num() > 8192) { Error = TEXT("This deletion is too large for undo."); return false; }
    auto* System = AFGBlueprintSubsystem::Get(GetWorld());
    if (!System || !Player) return false;
    TArray<AFGBuildable*> Actors;
    TArray<bool> Blocked;
    ON_SCOPE_EXIT { for (int32 I=0; I<Actors.Num(); ++I) if (IsValid(Actors[I]) && Actors[I]->GetIsLightweightTemporary()) Actors[I]->SetBlockCleanupOfTemporary(Blocked[I]); };
    for (const auto& Ref : Members)
    {
        auto* B = Ref.ResolveActor(GetWorld());
        if (!IsValid(B) || B->GetIsDismantled()) { Error = TEXT("A deleted object could not be archived for undo."); return false; }
        Actors.Add(B); Blocked.Add(B->GetBlockCleanupOfTemporary());
        if (B->GetIsLightweightTemporary()) B->SetBlockCleanupOfTemporary(true);
        Paths.Add(FObjectReferenceDisc(B).ToString()); ActorNames.Add(B->GetPathName());
        auto& Saved = Contents.AddDefaulted_GetRef(); Saved.Path = Paths.Last();
        for (auto* Inventory : TInlineComponentArray<UFGInventoryComponent*>(B))
        {
            auto& I = Saved.Inventories.AddDefaulted_GetRef(); I.Path = Inventory->GetPathName(B);
            Inventory->GetInventoryStacks(I.Slots, true);
        }
        if (auto* Fluid = Cast<IFGFluidIntegrantInterface>(B); Fluid && Fluid->GetFluidBox()) Saved.FluidContent = Fluid->GetFluidBox()->Content;
        for(auto* Pipe:TInlineComponentArray<UFGPipeConnectionComponent*>(B))
            if(Pipe->GetFluidDescriptor()) { Saved.FluidDescriptor=Pipe->GetFluidDescriptor(); break; }
        if (auto* Belt = Cast<AFGBuildableConveyorBase>(B))
        {
            TArray<FConveyorBeltItem*> Items; Belt->GetConveyorBeltItems(Items);
            for (const auto* Item : Items) if (Item)
            {
                auto& Copy=Saved.Conveyor.Add_GetRef(*Item);
                if(Belt->GetConveyorChainActor()) Copy.Offset=Item->LocalOffset;
                Copy.LocalOffset=Copy.Offset; Copy.TimeDebt=0; Copy.DistanceMoved=0;
            }
        }
    }
    for(int32 I=0; I<Members.Num(); ++I)
        if(auto* Proxy=Members[I].Blueprint(GetWorld()))
        {
            auto* Saved=Blueprints.FindByPredicate([&](const auto& B){ return B.Existing==Proxy; });
            if(!Saved)
            {
                Saved=&Blueprints.AddDefaulted_GetRef(); Saved->Existing=Proxy;
                Saved->Transform=Proxy->GetActorTransform(); Saved->Bounds=Proxy->mLocalBounds; Saved->Name=Proxy->GetBlueprintName();
            }
            Saved->Members.Add(I);
        }
    auto SavePort = [&](UActorComponent* A, UActorComponent* B, int32 WireEnd = INDEX_NONE)
    {
        if (!A || !B) return;
        auto& P = Ports.AddDefaulted_GetRef(); P.Path = A->GetPathName(); P.WireEnd = WireEnd;
        if (Actors.Contains(Cast<AFGBuildable>(B->GetOwner()))) P.Internal = B->GetPathName();
        else { P.External = B; if(auto* Scene=Cast<USceneComponent>(B)) P.ExternalTransform=Scene->GetComponentTransform(); }
    };
    for (auto* B : Actors)
    {
        for (auto* P : TInlineComponentArray<UFGConnectionComponent*>(B))
        {
            if (auto* C = Cast<UFGFactoryConnectionComponent>(P)) SavePort(P, C->GetConnection());
            if (auto* C = Cast<UFGPipeConnectionComponentBase>(P)) SavePort(P, C->GetConnection());
            if (auto* C = Cast<UFGRailroadTrackConnectionComponent>(P)) for (auto* Other : C->GetConnections()) SavePort(P, Other);
        }
        if (auto* Wire = Cast<AFGBuildableWire>(B))
            for (int32 I=0; I<2; ++I)
                if (auto* Other = Wire->GetConnection(I))
                {
                    auto& P = Ports.AddDefaulted_GetRef(); P.Path = B->GetPathName(); P.WireEnd = I;
                    if (Actors.Contains(Cast<AFGBuildable>(Other->GetOwner()))) P.Internal = Other->GetPathName(); else { P.External = Other; P.ExternalTransform=Other->GetComponentTransform(); }
                }
    }
    // Signal ownership is separate from spline connections. It must be restored
    // before BeginPlay, with the original guarded/observed orientation.
    for(auto* B:Actors) if(auto* Signal=Cast<AFGBuildableRailroadSignal>(B))
        for(int32 Kind:{2,3})
            for(auto* Port:Kind==2?Signal->GetGuardedConnections():Signal->GetObservedConnections())
            {
                if(!IsValid(Port)) continue;
                auto& Saved=Ports.AddDefaulted_GetRef(); Saved.Path=Signal->GetPathName(); Saved.WireEnd=Kind;
                if(Actors.Contains(Cast<AFGBuildable>(Port->GetOwner()))) Saved.Internal=Port->GetPathName();
                else { Saved.External=Port; Saved.ExternalTransform=Port->GetComponentTransform(); }
            }
    if (auto* SystemGroups = ABSGroupSubsystem::Get(GetWorld()))
    {
        TSet<FGuid> Included;
        for (const auto& G : SystemGroups->Groups)
            for (const auto& B : G.Buildings) if (Members.Contains(B)) { Included.Add(G.Id); break; }
        for (bool Added=true; Added;)
        {
            Added=false;
            for (const auto& G : SystemGroups->Groups)
                if (Included.Contains(G.Id) && G.Parent.IsValid() && !Included.Contains(G.Parent)) { Included.Add(G.Parent); Added=true; }
        }
        for (const auto& G : SystemGroups->Groups) if (Included.Contains(G.Id)) Groups.Add(G);
    }
    FBlueprintRecord Record; Record.BlueprintName = TEXT("BSUndo_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!WriteBuildingSkillsClipboard(System, Record, FTransform::Identity, Actors, FIntVector(8,8,8), Data))
    { Error = TEXT("The native archive could not capture this deletion."); return false; }
    TMap<int32,FString> SavedPaths;
    if (!ReadBuildingSkillsBlueprintObjectPaths(Data,SavedPaths)) return false;
    for (const auto& Path : Paths) if (!SavedPaths.FindKey(Path)) { Error = TEXT("Undo archive is missing a selected object."); return false; }
    Descriptor = NewObject<UFGBlueprintDescriptor>(this);
    Descriptor->InitializeData(Record); Descriptor->InitializeHeaderInfo(Data.BlueprintHeader);
    Descriptor->SetRecipeRequirementsAreMet(true);
    return true;
}

bool UBSActionRecord::Restore(AFGCharacterPlayer* Player, FString& Message)
{
    if (!Error.IsEmpty()) { Message = Error; return false; }
    auto* System = AFGBlueprintSubsystem::Get(GetWorld()); auto* Inventory = Player->GetInventory();
    if (!System || !Inventory || !Descriptor) { Message=TEXT("The deletion archive is unavailable."); return false; }
    for (const auto& Ref : Members) if (Alive(GetWorld(),Ref)) { Message=TEXT("Part of this deletion still exists. Undo was not applied."); return false; }
    TMap<UFGCircuitConnectionComponent*,int32> RequiredWires;
    for (const auto& Port : Ports) if(Port.Internal.IsEmpty())
    {
        auto* Scene=Cast<USceneComponent>(Port.External.Get());
        if (!PortFree(Port.External.Get()) || !Scene || !Scene->GetComponentTransform().Equals(Port.ExternalTransform,.01))
        { Message=TEXT("An outside connection changed or disappeared. Undo was not applied."); return false; }
        if(auto* Rail=Cast<UFGRailroadTrackConnectionComponent>(Scene))
            if((Port.WireEnd==2 && IsValid(Rail->GetFacingSignal())) || (Port.WireEnd==3 && IsValid(Rail->GetTrailingSignal())))
            { Message=TEXT("Another signal now occupies the restored signal's connection."); return false; }
        if(Port.WireEnd>=0 && Port.WireEnd<2) if(auto* Circuit=Cast<UFGCircuitConnectionComponent>(Scene)) ++RequiredWires.FindOrAdd(Circuit);
    }
    for(const auto& Pair:RequiredWires) if(Pair.Key->GetNumFreeConnections()<Pair.Value)
    { Message=TEXT("An outside power connection has no room for the restored wires."); return false; }
    // Take refunds back from the player and the original overflow containers.
    // Reserve exact slots and item state before spawning anything.
    TArray<UFGInventoryComponent*> Sources{Inventory};
    for (const auto& Weak : RefundInventories) if (auto* I=Weak.Get(); IsValid(I)) Sources.AddUnique(I);
    struct FDebit { UFGInventoryComponent* Inventory; int32 Slot; int32 Count; };
    TArray<FDebit> Debits;
    for (const auto& Need : Refund)
    {
        int32 Left=Need.NumItems;
        for (auto* Source : Sources)
        {
            TArray<FInventoryStack> Slots; Source->GetInventoryStacks(Slots,true);
            for (int32 I=0; I<Slots.Num() && Left>0; ++I)
                if (Slots[I].HasItems() && SameItem(Slots[I].Item,Need.Item))
                {
                    int32 Reserved=0; for(const auto& D:Debits) if(D.Inventory==Source && D.Slot==I) Reserved+=D.Count;
                    const int32 Count=FMath::Min(Left,FMath::Max(0,Slots[I].NumItems-Reserved));
                    if(Count) { Debits.Add({Source,I,Count}); Left-=Count; }
                }
        }
        if(Left) { Message=TEXT("Return the dismantle refunds to your inventory before undoing this deletion."); return false; }
    }
    TMap<int32,FString> SourcesByIndex;
    if(!ReadBuildingSkillsBlueprintObjectPaths(Data,SourcesByIndex)) { Message=TEXT("Undo archive could not be read."); return false; }
    TMap<FString,AFGBuildable*> Spawned;
    TMap<FString,FBSBuildingRef> SpawnedRefs;
    TArray<AFGBuildable*> NewBuildings;
    TGuardValue<UBSActionRecord*> RecordGuard(RestoringRecord,this);
    TGuardValue<FBlueprintSaveData> Stored(System->mStoredBlueprintData,Data);
    auto Headers=System->mCachedHeaders; Headers.Add(Data.BlueprintHeader);
    TGuardValue<TArray<FBlueprintHeader>> HeaderGuard(System->mCachedHeaders,MoveTemp(Headers));
    TArray<FBSBuildingRef> NewInstances;
    TGuardValue<TArray<FBSBuildingRef>*> InstanceGuard(RestoredInstances,&NewInstances);
    TArray<AFGBlueprintProxy*> Proxies;
    TArray<AFGBlueprintProxy*> NewProxies;
    for(const auto& Saved:Blueprints)
    {
        auto* Proxy=Saved.Existing.Get();
        if(!IsValid(Proxy))
        {
            Proxy=GetWorld()->SpawnActorDeferred<AFGBlueprintProxy>(AFGBlueprintProxy::StaticClass(),Saved.Transform,nullptr,Player,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Proxy) { for(auto* P:NewProxies) P->Destroy(); Message=TEXT("Unable to restore blueprint identity."); return false; }
            Proxy->mBlueprintName=Saved.Name; Proxy->mLocalBounds=Saved.Bounds;
            NewProxies.Add(Proxy);
        }
        Proxies.Add(Proxy);
    }
    bool Committed=false;
    ON_SCOPE_EXIT
    {
        if(!Committed)
        {
            for(const auto& Ref:NewInstances)
                if(Ref.IsAlive(GetWorld())) AFGLightweightBuildableSubsystem::Get(GetWorld())->RemoveByInstanceIndex(Ref.LightweightClass,Ref.RuntimeIndex);
            for(const auto& Pair:SpawnedRefs)
                if(!Pair.Value.LightweightClass && IsValid(Pair.Value.Actor)) IFGDismantleInterface::Execute_Dismantle(Pair.Value.Actor);
            for(auto* P:NewProxies) if(IsValid(P)) P->Destroy();
        }
    };
    bool ContentsValid=true;
    const TFunction<void(AFGBuildable*,int32)> Capture = [&](AFGBuildable* B,int32 I)
    {
        if(const auto* Path=SourcesByIndex.Find(I))
        {
            Spawned.Add(*Path,B); FBSBuildingRef Ref;
            if(B->ShouldConvertToLightweight()) { Ref.LightweightClass=B->GetClass(); Ref.Recipe=B->GetBuiltWithRecipe(); Ref.Transform=B->GetActorTransform(); }
            else Ref=FBSBuildingRef::FromActor(B);
            SpawnedRefs.Add(*Path,Ref);
        }
    };
    const TFunction<void()> RestoreContents = [&]()
    {
        // Undo restores existing state immediately. A blueprint construction
        // effect otherwise hides meshes/collision until its delayed animation ends.
        // Clear the replicated effect metadata before actor BeginPlay as well.
        for(const auto& Pair:Spawned)
        {
            Pair.Value->SetBuildEffectInstigator(nullptr);
            Pair.Value->SetBlueprintBuildEffectID(INDEX_NONE);
        }
        for(int32 I=0; I<Blueprints.Num(); ++I)
            for(int32 Member:Blueprints[I].Members)
                if(auto* B=Spawned.FindRef(Paths[Member]))
                { B->SetBlueprintProxy(Proxies[I]); if(!B->ShouldConvertToLightweight()) Proxies[I]->RegisterBuildable(B); }
        for(const auto& Link:Ports) if(Link.WireEnd==2 || Link.WireEnd==3)
        {
            const int32 Index=ActorNames.IndexOfByKey(Link.Path);
            auto* Signal=Paths.IsValidIndex(Index)?Cast<AFGBuildableRailroadSignal>(Spawned.FindRef(Paths[Index])):nullptr;
            auto* Port=Cast<UFGRailroadTrackConnectionComponent>(Link.External.Get());
            if(!Link.Internal.IsEmpty())
                for(int32 I=0; I<ActorNames.Num(); ++I)
                    if(Link.Internal.StartsWith(ActorNames[I]+TEXT(".")))
                        if(auto* B=Spawned.FindRef(Paths[I])) Port=FindObject<UFGRailroadTrackConnectionComponent>(B,*Link.Internal.Mid(ActorNames[I].Len()+1));
            if(!Signal || !Port) { ContentsValid=false; continue; }
            if(Link.WireEnd==2 && !Signal->GetGuardedConnections().Contains(Port)) Signal->AddGuardedConnection(Port);
            if(Link.WireEnd==3 && !Signal->GetObservedConnections().Contains(Port)) Signal->AddObservedConnection(Port);
        }
        for(const auto& Saved:Contents)
        {
            auto* B=Spawned.FindRef(Saved.Path); if(!B) { ContentsValid=false; continue; }
            for(const auto& SavedInventory:Saved.Inventories)
            {
                auto* I=FindObject<UFGInventoryComponent>(B,*SavedInventory.Path);
                if(!I) { ContentsValid=false; continue; }
                I->Resize(SavedInventory.Slots.Num()); I->Empty();
                for(int32 Slot=0; Slot<SavedInventory.Slots.Num(); ++Slot)
                    if(SavedInventory.Slots[Slot].HasItems())
                        ContentsValid &= I->IsValidIndex(Slot) && I->AddStackToIndex_Unsafe(Slot,SavedInventory.Slots[Slot],false)==SavedInventory.Slots[Slot].NumItems;
            }
            if(Saved.FluidDescriptor)
                for(auto* Pipe:TInlineComponentArray<UFGPipeConnectionComponent*>(B)) Pipe->SetFluidDescriptor(Saved.FluidDescriptor);
            if(Saved.FluidContent>=0)
                if(auto* Fluid=Cast<IFGFluidIntegrantInterface>(B); Fluid && Fluid->GetFluidBox()) Fluid->GetFluidBox()->Content=Saved.FluidContent;
            if(auto* Belt=Cast<AFGBuildableConveyorBase>(B))
            { Belt->mItems.Empty(); for(const auto& Item:Saved.Conveyor) Belt->mItems.Add(Item); }
        }
    };
    System->LoadStoredBlueprint(Descriptor,FTransform::Identity,NewBuildings,false,nullptr,nullptr,nullptr,Capture,RestoreContents);
    if(!ContentsValid || Spawned.Num()<Members.Num())
    {
        Message=TEXT("Restoring this deletion failed validation; new buildings were rolled back."); return false;
    }
    TMap<FString,UObject*> Remap; TArray<FBSBuildingRef> Restored;
    for(int32 I=0; I<Paths.Num(); ++I)
    {
        auto* B=Spawned.FindRef(Paths[I]);
        if(!B) { Message=TEXT("A restored building is missing."); return false; }
        Remap.Add(ActorNames[I],B);
        for(auto* C:TInlineComponentArray<UActorComponent*>(B)) Remap.Add(ActorNames[I]+TEXT(".")+C->GetPathName(B),C);
        auto Ref=SpawnedRefs.FindRef(Paths[I]);
        if(Ref.LightweightClass)
        {
            const auto* Actual=NewInstances.FindByPredicate([&](const auto& X){ return X.LightweightClass==Ref.LightweightClass && X.Transform.Equals(Ref.Transform,.01); });
            if(!Actual) { Message=TEXT("A restored lightweight instance is missing."); return false; }
            Ref=*Actual;
        }
        Restored.Add(Ref);
    }
    for(const auto& P:Ports)
    {
        auto* A=Cast<UActorComponent>(Remap.FindRef(P.Path));
        auto* B=P.Internal.IsEmpty()?P.External.Get():Cast<UActorComponent>(Remap.FindRef(P.Internal));
        if(P.WireEnd!=INDEX_NONE) continue;
        if(!A || !B) { Message=TEXT("A restored connection is missing; undo was rolled back."); return false; }
        if(auto* C=Cast<UFGFactoryConnectionComponent>(A); C && !C->GetConnection()) C->SetConnection(Cast<UFGFactoryConnectionComponent>(B));
        if(auto* C=Cast<UFGPipeConnectionComponentBase>(A); C && !C->GetConnection()) C->SetConnection(Cast<UFGPipeConnectionComponentBase>(B));
        if(auto* C=Cast<UFGRailroadTrackConnectionComponent>(A); C && !C->GetConnections().Contains(Cast<UFGRailroadTrackConnectionComponent>(B))) C->AddConnection(Cast<UFGRailroadTrackConnectionComponent>(B));
    }
    for(const auto& Pair:Remap)
        if(auto* Wire=Cast<AFGBuildableWire>(Pair.Value))
        {
            UFGCircuitConnectionComponent* Ends[2]{};
            for(const auto& P:Ports) if(P.Path==Pair.Key && P.WireEnd>=0 && P.WireEnd<2)
                Ends[P.WireEnd]=Cast<UFGCircuitConnectionComponent>(P.Internal.IsEmpty()?P.External.Get():Remap.FindRef(P.Internal));
            if(Ends[0] && Ends[1] && (Wire->GetConnection(0)!=Ends[0] || Wire->GetConnection(1)!=Ends[1]))
            { Wire->Disconnect(); Wire->Connect(Ends[0],Ends[1]); }
        }
    for(const auto& Pair:Remap) if(auto* Signal=Cast<AFGBuildableRailroadSignal>(Pair.Value)) Signal->UpdateConnections();
    for(const auto& P:Ports)
    {
        auto* A=Remap.FindRef(P.Path);
        auto* B=P.Internal.IsEmpty()?P.External.Get():Cast<UActorComponent>(Remap.FindRef(P.Internal));
        bool Connected=false;
        if(auto* Signal=Cast<AFGBuildableRailroadSignal>(A))
        {
            auto* Port=Cast<UFGRailroadTrackConnectionComponent>(B);
            if(P.WireEnd==2) Connected=Signal->GetGuardedConnections().Contains(Port) && Port && Port->GetFacingSignal()==Signal;
            if(P.WireEnd==3) Connected=Signal->GetObservedConnections().Contains(Port) && Port && Port->GetTrailingSignal()==Signal;
        }
        if(auto* Wire=Cast<AFGBuildableWire>(A); Wire && P.WireEnd>=0 && P.WireEnd<2) Connected=Wire->GetConnection(P.WireEnd)==B;
        if(auto* C=Cast<UFGFactoryConnectionComponent>(A)) Connected=C->GetConnection()==B;
        if(auto* C=Cast<UFGPipeConnectionComponentBase>(A)) Connected=C->GetConnection()==B;
        if(auto* C=Cast<UFGRailroadTrackConnectionComponent>(A)) Connected=C->GetConnections().Contains(Cast<UFGRailroadTrackConnectionComponent>(B));
        if(!Connected) { Message=TEXT("A restored connection failed validation; undo was rolled back."); return false; }
    }
    for(auto* Proxy:NewProxies) { Proxy->FinishSpawning(Proxy->GetActorTransform()); Proxy->OnRep_LocalBounds(); }
    if(auto* GroupSystem=ABSGroupSubsystem::Get(GetWorld()))
    {
        for(const auto& Saved:Groups)
        {
            auto* G=GroupSystem->Groups.FindByPredicate([&](const auto& X){ return X.Id==Saved.Id; });
            if(!G) { FBSGroup New=Saved; New.Buildings.Reset(); GroupSystem->Groups.Add(New); G=GroupSystem->Groups.FindByPredicate([&](const auto& X){ return X.Id==Saved.Id; }); }
            for(const auto& Ref:Saved.Buildings)
            { const int32 I=Members.IndexOfByKey(Ref); if(Restored.IsValidIndex(I)) G->Buildings.AddUnique(Restored[I]); }
        }
        FBSGroupTree::PruneEmpty(GroupSystem->Groups); GroupSystem->PublishGroups();
    }
    for(const auto& D:Debits) D.Inventory->RemoveFromIndex(D.Slot,D.Count);
    Committed=true; Message=FString::Printf(TEXT("Restored %d buildings."),Restored.Num()); return true;
}

void FBSActionHistory::Install()
{
    if (WITH_EDITOR) return;
    SUBSCRIBE_METHOD(AFGBlueprintSubsystem::ReadBlueprintFromDisc,
        [](auto& Scope,AFGBlueprintSubsystem* System,const FString& Name)
        {
            if(!RestoringRecord || RestoringRecord->GetWorld()!=System->GetWorld() || Name!=RestoringRecord->Data.BlueprintHeader.BlueprintName) return;
            System->mStoredBlueprintData=RestoringRecord->Data; Scope.Override(true);
        });
    SUBSCRIBE_METHOD(AFGBuildableSubsystem::AddBuildable, [](auto&,AFGBuildableSubsystem* System,AFGBuildable* B)
    {
        if(Capturing && !Capturing->Deletion && Capturing->GetWorld()==System->GetWorld() && IsValid(B) && !B->ShouldConvertToLightweight())
            Capturing->Members.AddUnique(FBSBuildingRef::FromActor(B));
    });
    SUBSCRIBE_METHOD(AFGLightweightBuildableSubsystem::AddFromBuildableInstanceData,
        [](auto& Scope,AFGLightweightBuildableSubsystem* System,TSubclassOf<AFGBuildable> Class,FRuntimeBuildableInstanceData& Data,bool FromSave,int32 SavedIndex,uint16 Id,AActor* Instigator,int32 BlueprintIndex)
        {
            if(RestoringRecord && RestoringRecord->GetWorld()==System->GetWorld())
            { Instigator=nullptr; BlueprintIndex=INDEX_NONE; }
            const int32 Index=Scope(System,Class,Data,FromSave,SavedIndex,Id,Instigator,BlueprintIndex);
            if(RestoredInstances && Index!=INDEX_NONE) RestoredInstances->Add(FBSBuildingRef::FromLightweight(System->GetWorld(),Class,Index));
            if(Capturing && !Capturing->Deletion && Capturing->GetWorld()==System->GetWorld() && Index!=INDEX_NONE)
                Capturing->Members.AddUnique(FBSBuildingRef::FromLightweight(System->GetWorld(),Class,Index));
        });
    SUBSCRIBE_METHOD(UFGBuildGunStateBuild::InternalConstructHologram,
        [](auto& Scope,UFGBuildGunStateBuild* State,FNetConstructionID Id)
        {
            auto* Gun=State->GetBuildGun(); auto* Player=Gun?Gun->GetInstigatorCharacter():nullptr; auto* RCO=RCOFor(Player);
            if(Restoring || Capturing || !Player || !Player->HasAuthority() || !RCO || (!Limit(State->GetWorld()) && RCO->Transformers.IsEmpty() && !RCO->TransformersBuildAsBlueprint)) return;
            auto* Entry=NewObject<UBSActionRecord>(RCO);
            if(State->GetHologram() && State->GetHologram()->IsUpgrade()) Entry->Error=TEXT("Upgrade replacement cannot be undone as a new construction.");
            TGuardValue<UBSActionRecord*> CaptureGuard(Capturing,Entry); TGuardValue<AFGCharacterPlayer*> PlayerGuard(CapturingPlayer,Player);
            Scope(State,Id);
            Entry->Members.RemoveAll([&](const auto& Ref){ return !Ref.IsAlive(State->GetWorld()); });
            for(auto& Ref:Entry->Members) if(IsValid(Ref.Actor)) Ref=FBSBuildingRef::FromActor(Ref.Actor);
            const auto Original=Entry->Members;
            FBSTransformers::Replicate(RCO,Original);
            Entry->Members.RemoveAll([&](const auto& Ref){ return !Ref.IsAlive(State->GetWorld()); });
            for(auto& Ref:Entry->Members) if(IsValid(Ref.Actor)) Ref=FBSBuildingRef::FromActor(Ref.Actor);
            if(RCO->TransformersBuildAsBlueprint && !Entry->Members.IsEmpty() && !FBSTransformers::BuildAsBlueprint(RCO,Entry->Members))
                UE_LOG(LogBuildingSkills,Warning,TEXT("Could not assign transformer placement blueprint identity."));
            Record(RCO,Entry);
        });
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateDismantle,Server_DismantleActors_Implementation,
        [](auto& Scope,UFGBuildGunStateDismantle* State,const TArray<AActor*>& Actors,const TArray<FDismantleLightweightBundle>& Bundles)
        {
            auto* Gun=State->GetBuildGun(); auto* Player=Gun?Gun->GetInstigatorCharacter():nullptr; auto* RCO=RCOFor(Player);
            if(Restoring || Capturing || !Player || !Player->HasAuthority() || !RCO ||
                (!Limit(State->GetWorld()) && RCO->Transformers.IsEmpty()) || UBSEditModeComponent::IsEditDescriptor(Gun->GetCurrentBuildGunMode())) return;
            TArray<FBSBuildingRef> Members; TSet<AActor*> Visited;
            for(auto* Actor:Actors) Expand(State->GetWorld(),Actor,Members,Visited);
            for(const auto& Bundle:Bundles) for(int32 Index:Bundle.RemovalIndices)
            { const auto Ref=FBSBuildingRef::FromLightweight(State->GetWorld(),Bundle.BuildableClass,Index); if(Ref.IsAlive(State->GetWorld())) Members.AddUnique(Ref); }
            if(Members.IsEmpty()) return;
            FString MatchingMessage;
            const auto Matching=FBSTransformers::Matching(State->GetWorld(),RCO->Transformers,Members,MatchingMessage);
            TArray<AActor*> Targets=Actors; TArray<FDismantleLightweightBundle> TargetBundles=Bundles;
            for(const auto& Ref:Matching)
            {
                if(Ref.LightweightClass)
                {
                    auto* Bundle=TargetBundles.FindByPredicate([&](const auto& B){return B.BuildableClass==Ref.LightweightClass;});
                    if(!Bundle) {TargetBundles.Add(FDismantleLightweightBundle(Ref.LightweightClass));Bundle=&TargetBundles.Last();}
                    Bundle->RemovalIndices.AddUnique(Ref.ResolveIndex(State->GetWorld())); Members.AddUnique(Ref);
                }
                else if(auto* B=Ref.Actor.Get()) {Targets.AddUnique(B);Expand(State->GetWorld(),B,Members,Visited);}
            }
            if(!MatchingMessage.IsEmpty()) RCO->Client_ConnectedMessage(MatchingMessage);
            auto* Entry=NewObject<UBSActionRecord>(RCO); Entry->Deletion=true;
            if(Limit(State->GetWorld()) && !Entry->Capture(Player,Members) && Entry->Error.IsEmpty()) Entry->Error=TEXT("This deletion could not be captured for undo.");
            auto* Inventory=Player->GetInventory(); TArray<FInventoryStack> Before; TArray<FInventoryStack> After;
            if(Inventory) Inventory->GetInventoryStacks(Before);
            TArray<TWeakObjectPtr<AActor>> Spawned;
            const auto SpawnHandle=State->GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor){ if(DroppingRefund) Spawned.Add(Actor); }));
            ON_SCOPE_EXIT { State->GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle); };
            TGuardValue<UBSActionRecord*> CaptureGuard(Capturing,Entry); TGuardValue<AFGCharacterPlayer*> PlayerGuard(CapturingPlayer,Player);
            Scope(State,Targets,TargetBundles);
            if(Inventory) Inventory->GetInventoryStacks(After);
            TArray<FInventoryStack> Totals;
            for(const auto& Stack:After) AppendRefund(Totals,Stack);
            for(const auto& Stack:Before) for(auto& Total:Totals) if(SameItem(Stack.Item,Total.Item)) { Total.NumItems-=Stack.NumItems; break; }
            for(const auto& Stack:Totals) AppendRefund(Entry->Refund,Stack);
            for(const auto& Weak:Spawned) if(auto* Actor=Weak.Get())
                for(auto* I:TInlineComponentArray<UFGInventoryComponent*>(Actor)) Entry->RefundInventories.AddUnique(I);
            int32 Removed=0; for(const auto& Ref:Members) if(!Ref.IsAlive(State->GetWorld())) ++Removed;
            if(!Removed) return;
            if(Removed!=Members.Num()) Entry->Error=TEXT("This deletion only partially succeeded and cannot be restored as one action.");
            Record(RCO,Entry);
        });
    auto GroundRefund=[](auto& Scope,UWorld* World,const FVector& Location,AActor* Ignore,const TArray<FInventoryStack>& Refund,AFGCharacterPlayer* Player)
    {
        if(!Capturing || !Capturing->Deletion || CapturingPlayer!=Player || DroppingRefund) return;
        for(const auto& Stack:Refund) AppendRefund(Capturing->Refund,Stack);
        TGuardValue<bool> Guard(DroppingRefund,true);
        Scope(World,Location,Ignore,Refund,Player);
    };
    SUBSCRIBE_METHOD(FDismantleHelpers::DropRefundOnGroundNoActor,GroundRefund);
    SUBSCRIBE_METHOD(FDismantleHelpers::DropRefundOnGround,
        [](auto& Scope,AActor* Actor,const FVector& Location,const TArray<FInventoryStack>& Refund,AFGCharacterPlayer* Player)
        {
            if(!Capturing || !Capturing->Deletion || CapturingPlayer!=Player || DroppingRefund) return;
            for(const auto& Stack:Refund) AppendRefund(Capturing->Refund,Stack);
            TGuardValue<bool> Guard(DroppingRefund,true);
            Scope(Actor,Location,Refund,Player);
        });
}

bool FBSActionHistory::Undo(UBSGroupRemoteCallObject* RCO,bool Deletion,FString& Message)
{
    auto* Player=RCO?RCO->GetOwnerPlayerCharacter():nullptr; auto* Gun=Player?Player->GetBuildGun():nullptr;
    if(!Player || !Player->HasAuthority() || !Player->IsBuildGunEquipped() || !Gun ||
        !Gun->IsInState(Deletion?EBuildGunState::BGS_DISMANTLE:EBuildGunState::BGS_BUILD) ||
        UBSEditModeComponent::IsEditDescriptor(Gun->GetCurrentBuildGunMode()) || Player->IsDrivingVehicle() || Player->IsInPortal() ||
        !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead())
    { Message=TEXT("Open the matching Build or Dismantle gun to undo."); return false; }
    auto& History=Deletion?RCO->DeleteHistory:RCO->BuildHistory;
    const int32 Max=Limit(RCO->GetWorld()); if(History.Num()>Max) History.RemoveAt(0,History.Num()-Max);
    if(History.IsEmpty()) { Message=Deletion?TEXT("No deletion to undo."):TEXT("No construction to undo."); return false; }
    auto* Entry=History.Last().Get(); if(!Entry || !Entry->Error.IsEmpty()) { Message=Entry?Entry->Error:TEXT("History is unavailable."); return false; }
    bool InReach=false; for(const auto& Ref:Entry->Members) InReach |= FVector::DistSquared(Ref.Transform.GetLocation(),Player->GetActorLocation())<=FMath::Square(Gun->GetBuildGunRange()+1000.0f);
    if(!InReach) { Message=TEXT("Move within Build Gun range of the affected buildings to undo."); return false; }
    TGuardValue<bool> RestoreGuard(Restoring,true);
    if(Deletion)
    { if(!Entry->Restore(Player,Message)) return false; History.Pop(); return true; }
    auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetBuildGunStateFor(EBuildGunState::BGS_DISMANTLE));
    if(!State) return false;
    TArray<AActor*> Actors; TArray<FDismantleLightweightBundle> Bundles; TSet<AActor*> Allowed;
    for(const auto& Ref:Entry->Members)
    {
        if(!Ref.IsAlive(RCO->GetWorld())) { Message=TEXT("An object from this construction was removed. Undo was not applied."); return false; }
        auto* B=Ref.ResolveActor(RCO->GetWorld());
        if(!B || !B->GetActorTransform().Equals(Ref.Transform,.01) || !IFGDismantleInterface::Execute_CanDismantle(B))
        { Message=TEXT("An object from this construction changed or cannot be dismantled. Undo was not applied."); return false; }
        Allowed.Add(B);
        if(Ref.LightweightClass)
        {
            auto* Bundle=Bundles.FindByPredicate([&](const auto& X){ return X.BuildableClass==Ref.LightweightClass; });
            if(!Bundle) Bundle=&Bundles.Emplace_GetRef(Ref.LightweightClass);
            Bundle->RemovalIndices.Add(Ref.ResolveIndex(RCO->GetWorld()));
        }
        else Actors.Add(B);
    }
    for(auto* B:Allowed)
    {
        TArray<AActor*> Related; IFGDismantleInterface::Execute_GetDismantleDependencies(B,Related);
        IFGDismantleInterface::Execute_GetChildDismantleActors(B,Related);
        for(auto* Other:Related) if(IsValid(Other) && !Allowed.Contains(Other))
        { Message=TEXT("Other buildings now depend on this construction. Disconnect them before undoing it."); return false; }
    }
    State->Server_DismantleActors_Implementation(Actors,Bundles);
    Entry->Members.RemoveAll([&](const auto& Ref){ return !Ref.IsAlive(RCO->GetWorld()); });
    if(!Entry->Members.IsEmpty()) { Message=TEXT("Some construction objects could not be dismantled; remaining objects stay in history."); return false; }
    History.Pop(); Message=TEXT("Construction undone."); return true;
}
void UBSGroupRemoteCallObject::Server_UndoGunAction_Implementation(bool Deletion)
{
    const double Now=GetWorld()->GetRealTimeSeconds(); if(Now-LastActionUndo<.25) return; LastActionUndo=Now;
    FString Message; FBSActionHistory::Undo(this,Deletion,Message);
    UE_LOG(LogBuildingSkills,Display,TEXT("Action undo: %s"),*Message);
    if(auto* Player=GetOwnerPlayerCharacter()) if(auto* PC=Cast<AFGPlayerController>(Player->GetController()))
    {
        FChatMessageStruct Notice; Notice.MessageText=FText::FromString(Message);
        Notice.MessageType=EFGChatMessageType::CMT_SystemMessage; PC->Client_SendChatMessage(Notice);
    }
}
