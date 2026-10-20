#include "BSActionHistory.h"
#include "BSBlueprintScale.h"
#include "BSBlueprintSave.h"
#include "BSClipboard.h"
#include "FGBlueprintSubsystem.h"
#include "FGActorSaveHeaderTypes.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGBlueprintProxy.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "FGFactoryConnectionComponent.h"
#include "FGInventoryComponent.h"
#include "EnhancedPlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Hologram/FGHologram.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Components/SceneComponent.h"
#include "FGPowerConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "Buildables/FGBuildableWire.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Buildables/FGBuildableRailroadSignal.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "UObject/UnrealType.h"

class FBSPolishSmoke
{
public:
    static bool Run(AFGCharacterPlayer* Player,int32 Phase)
    {
        auto* World=Player->GetWorld(); auto* Edit=Player->FindComponentByClass<UBSEditModeComponent>();
        auto* PC=Cast<AFGPlayerController>(Player->GetController()); auto* RCO=PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>();
        auto* Gun=Player->GetBuildGun(); auto* Groups=ABSGroupSubsystem::Get(World);
        auto* LW=AFGLightweightBuildableSubsystem::Get(World); auto* Input=Cast<UEnhancedPlayerInput>(PC->PlayerInput);
        static TArray<FBSBuildingRef> Refs; static FGuid GroupId; static TWeakObjectPtr<AFGBuildableStorage> Storage;
        static FVector StorageLocation; static int32 PlateBefore=0; static TSubclassOf<UFGRecipe> FoundationRecipe;
        static TSubclassOf<UFGItemDescriptor> Plate; static int32 BuildCount=0;
        static TArray<FBSBuildingRef> BlueprintRefs; static TArray<TWeakObjectPtr<AFGBuildable>> Network;
        static TWeakObjectPtr<UFGPipeConnectionComponentBase> OutsidePipe;
        static TWeakObjectPtr<UFGCircuitConnectionComponent> OutsidePower;
        static TWeakObjectPtr<AFGBuildableRailroadTrack> RailA,RailB; static TWeakObjectPtr<AFGBuildableRailroadSignal> Signal;
        static FVector RailLocation;
        static FTransform BlueprintPose; static TWeakObjectPtr<AFGBlueprintProxy> OldProxy;
        static TWeakObjectPtr<AFGBuildableConveyorLift> Lift;
        static TWeakObjectPtr<AFGBuildableConveyorBelt> LowerBelt, UpperBelt;
        static FVector LiftLocation, LowerLocation;
        static TArray<FBSBuildingRef> LargeSelection;
        bool Passed=true;
        auto Check=[&](const TCHAR* Name,bool OK){ Passed &= OK; UE_LOG(LogBuildingSkills,Display,TEXT("RUNTIME %s: Polish: %s"),OK?TEXT("PASS"):TEXT("FAIL"),Name); };
        auto Key=[&](FKey K,bool Down){ Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,K,Down?IE_Pressed:IE_Released,Down?1.f:0.f,false,0)); };
        if(Phase==0)
        {
            Key(EKeys::E,false); Key(EKeys::LeftAlt,false);
            auto* Class=LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x1_01.Build_Foundation_8x1_01_C"));
            FoundationRecipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Foundations/Recipe_Foundation_8x1_01.Recipe_Foundation_8x1_01_C"));
            Check(TEXT("fixture loads"),Class && FoundationRecipe && LW && RCO); if(!Class || !FoundationRecipe || !LW || !RCO) return false;
            AFGRecipeManager::Get(World)->AddAvailableRecipe(FoundationRecipe); Refs.Reset();
            for(int32 I=0; I<3; ++I)
            {
                FRuntimeBuildableInstanceData Data; Data.Transform=FTransform(FRotator(0,I==2?70:37,0),Player->GetActorLocation()+FVector(1600,I*1000,100)); Data.BuiltWithRecipe=FoundationRecipe;
                Data.CustomizationData.SwatchDesc=Class->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(World);
                Refs.Add(FBSBuildingRef::FromLightweight(World,Class,LW->AddFromBuildableInstanceData(Class,Data)));
            }
            Edit->Selection={}; Edit->Selection.Buildings={Refs[0]}; Edit->Hover={}; Edit->OnTransform();
            Check(TEXT("Transform starts with local axes"),Edit->bTransforming && Edit->TransformOptions.Space==EBSTransformSpace::Local);
            Check(TEXT("Single-object world pose is displayed"),Edit->TransformPoseText().Contains(TEXT("world")) && Edit->TransformPoseText().Contains(TEXT("Position (cm)")));
            PC->SetControlRotation(FRotator(0,37,0)); Edit->OnMove(0);
            Check(TEXT("Arrow movement follows a diagonal local X"),!Edit->TransformCommands.IsEmpty() && Edit->TransformCommands.Last().Translation.GetSafeNormal().Equals(Refs[0].Transform.GetRotation().GetAxisX(),.001));
            Edit->TransformCommands.Reset(); Edit->BlueprintAnchor=Refs[2]; Edit->OnAxisSpace();
            Check(TEXT("Anchor axes are offered"),Edit->TransformOptions.Space==EBSTransformSpace::Anchor && Edit->GetTransformFrame().Equals(Refs[2].Transform.GetRotation(),.001));
            Check(TEXT("Pose switches to anchor-relative coordinates"),Edit->TransformPoseText().Contains(TEXT("relative to anchor")));
            Check(TEXT("Transform labels use reference"),Edit->GetStatusText().ToString().Contains(TEXT("Reference 1")));
            Edit->EndTransform(); Edit->LeaveEditMode(false); Gun->GotoDismantleState();
            auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState()); if(State) Gun->SetCurrentBuildGunMode(State->mDefaultDismantleMode);
        }
        if(Phase==1)
        {
            Check(TEXT("Dismantle exposes a scoped undo shortcut and hint"),Edit->ActionHistoryContext && Edit->ActionHistoryHint);
            FBSGroup G; G.Id=FGuid::NewGuid(); G.Buildings={Refs[0],Refs[1]}; GroupId=G.Id; Groups->Groups.Add(G); Groups->PublishGroups();
            auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState());
            FDismantleLightweightBundle Bundle(Refs[0].LightweightClass); Bundle.RemovalIndices={Refs[0].ResolveIndex(World),Refs[1].ResolveIndex(World)};
            if(State) State->Server_DismantleActors_Implementation({}, {Bundle});
            Check(TEXT("Native batch deletes both foundations"),!Refs[0].IsAlive(World) && !Refs[1].IsAlive(World));
            Check(TEXT("Deleting final members prunes group immediately"),!FBSGroupTree::Find(Groups->Groups,GroupId));
            Check(TEXT("One deletion creates one history entry"),RCO->DeleteHistory.Num()==1);
            if(!RCO->DeleteHistory.IsEmpty()) UE_LOG(LogBuildingSkills,Display,TEXT("POLISH deletion archive: %s"),*RCO->DeleteHistory.Last()->Error);
        }
        if(Phase==2) { Key(EKeys::LeftControl,true); Key(EKeys::Z,true); }
        if(Phase==3)
        {
            Key(EKeys::Z,false); Key(EKeys::LeftControl,false);
            Check(TEXT("Physical Ctrl+Z restores both foundations"),Refs[0].IsAlive(World) && Refs[1].IsAlive(World));
            bool Instanced=true;
            for(int32 I=0;I<2;++I)
            {
                auto* Data=LW->GetRuntimeDataForBuildableClassAndIndex(Refs[I].LightweightClass,Refs[I].ResolveIndex(World));
                Instanced &= Data && !Data->Handles.IsEmpty() && Data->Handles[0]->IsInstanced();
                if(Data) for(const auto& Handle:Data->Handles)
                {
                    FTransform Visual,Collision;
                    const auto* Mesh=Handle->GetInstanceComponent();
                    const auto* Physics=Handle->GetCollisionInstanceComponent();
                    if(Mesh) Instanced &= Mesh->GetInstanceTransform(Handle->GetHandleID(),Visual,true) &&
                        Visual.GetLocation().Equals(Data->Transform.GetLocation(),1000) && Visual.GetScale3D().GetAbsMin()>0;
                    if(Physics) Instanced &= Physics->GetInstanceTransform(Handle->GetCollisionHandleId(),Collision,true) &&
                        Collision.GetLocation().Equals(Data->Transform.GetLocation(),1000) && Collision.GetScale3D().GetAbsMin()>0;
                }
            }
            Check(TEXT("restored foundations have visual and collision instances without build-effect delay"),Instanced);
            const auto* G=FBSGroupTree::Find(Groups->Groups,GroupId);
            Check(TEXT("Deletion undo restores group identity and members"),G && G->Buildings.Num()==2);
            Check(TEXT("Successful undo consumes one action"),RCO->DeleteHistory.IsEmpty());
            auto* Class=LoadClass<AFGBuildableStorage>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C"));
            auto* Recipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_StorageContainerMk1.Recipe_StorageContainerMk1_C"));
            Plate=LoadClass<UFGItemDescriptor>(nullptr,TEXT("/Game/FactoryGame/Resource/Parts/IronPlate/Desc_IronPlate.Desc_IronPlate_C"));
            StorageLocation=Player->GetActorLocation()+FVector(1600,-1400,200);
            auto* B=Class?Cast<AFGBuildableStorage>(AFGBuildableSubsystem::Get(World)->BeginSpawnBuildable(Class,FTransform(StorageLocation))):nullptr;
            Check(TEXT("stocked storage fixture"),B && Recipe && Plate); if(!B || !Recipe || !Plate) return false;
            B->SetBuiltWithRecipe(Recipe); AFGRecipeManager::Get(World)->AddAvailableRecipe(Recipe); B->FinishSpawning(B->GetActorTransform());
            B->GetStorageInventory()->AddStack(FInventoryStack(24,Plate)); Storage=B;
        }
        if(Phase==4)
        {
            PlateBefore=Player->GetInventory()->GetNumItems(Plate);
            auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState()); if(State && Storage.IsValid()) State->Server_DismantleActors_Implementation({Storage.Get()},{});
            Check(TEXT("Native dismantle archives stocked storage"),(!Storage.IsValid() || Storage->GetIsDismantled()) && !RCO->DeleteHistory.IsEmpty());
        }
        if(Phase==5)
        {
            RCO->Server_UndoGunAction_Implementation(true);
            AFGBuildableStorage* Restored=nullptr;
            for(TActorIterator<AFGBuildableStorage> It(World); It; ++It) if(!It->GetIsDismantled() && It->GetActorLocation().Equals(StorageLocation,.1)) Restored=*It;
            Check(TEXT("Inventory contents are restored exactly"),Restored && Restored->GetStorageInventory()->GetNumItems(Plate)==24);
            Check(TEXT("restored actor is immediately visible and collidable without build effect"),Restored && !Restored->IsHidden() && Restored->GetActorEnableCollision() && !Restored->IsPlayingBuildEffect() && Restored->GetBlueprintBuildEffectID()==INDEX_NONE);
            Check(TEXT("Restoration repays native refunds without duplication"),Player->GetInventory()->GetNumItems(Plate)==PlateBefore);
            Gun->GotoBuildState(FoundationRecipe);
        }
        if(Phase==6)
        {
            auto* State=Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState()); auto* H=State?State->GetHologram():nullptr;
            Check(TEXT("Build exposes independent undo hint"),State && H && Edit->ActionHistoryHint); if(!State || !H) return false;
            H->SetActorLocation(Player->GetActorLocation()+FVector(2200,2200,500));
            BuildCount=RCO->BuildHistory.Num(); State->InternalConstructHologram(FNetConstructionID());
            Check(TEXT("Native construction creates one history entry"),RCO->BuildHistory.Num()==BuildCount+1);
        }
        if(Phase==7) { Key(EKeys::LeftControl,true); Key(EKeys::Z,true); }
        if(Phase==8)
        {
            Key(EKeys::Z,false); Key(EKeys::LeftControl,false);
            Check(TEXT("Physical Build undo removes the last construction action"),RCO->BuildHistory.Num()==BuildCount);
            FString RedoMessage;
            Check(TEXT("Build redo restores the complete undone construction"),FBSActionHistory::Undo(RCO,false,RedoMessage,true) && RCO->BuildHistory.Num()==BuildCount+1);
            Check(TEXT("Redone construction can be undone again"),FBSActionHistory::Undo(RCO,false,RedoMessage) && RCO->BuildHistory.Num()==BuildCount);
            Gun->GotoNoneState();
        }
        if(Phase==9) Check(TEXT("Leaving the gun releases undo input"),!Edit->ActionHistoryContext && !Edit->ActionHistoryHint);
        if(Phase==10)
        {
            Gun->GotoDismantleState(); BlueprintRefs.Reset();
            BlueprintPose=FTransform(FRotator(0,23,0),Player->GetActorLocation()+FVector(-1500,-1500,100));
            auto* Proxy=World->SpawnActorDeferred<AFGBlueprintProxy>(AFGBlueprintProxy::StaticClass(),BlueprintPose);
            if(!Proxy) return false;
            if(auto* P=FindFProperty<FTextProperty>(Proxy->GetClass(),TEXT("mBlueprintName"))) P->SetPropertyValue_InContainer(Proxy,FText::FromString(TEXT("Polish test")));
            if(auto* P=FindFProperty<FStructProperty>(Proxy->GetClass(),TEXT("mLocalBounds"))) *P->ContainerPtrToValuePtr<FBox>(Proxy)=FBox(FVector(-400,-400,-100),FVector(1200,400,0));
            for(int32 I=0; I<2; ++I)
            {
                FRuntimeBuildableInstanceData Data; Data.Transform=FTransform(FVector(I*800,0,0))*BlueprintPose;
                Data.BuiltWithRecipe=FoundationRecipe; Data.BlueprintProxy=Proxy;
                Data.CustomizationData.SwatchDesc=Refs[0].LightweightClass->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(World);
                const int32 Index=LW->AddFromBuildableInstanceData(Refs[0].LightweightClass,Data);
                Proxy->RegisterLightweightInstance(Refs[0].LightweightClass,Index);
                BlueprintRefs.Add(FBSBuildingRef::FromLightweight(World,Refs[0].LightweightClass,Index));
            }
            Proxy->FinishSpawning(BlueprintPose); OldProxy=Proxy;
            Check(TEXT("Blueprint fixture owns both lightweight foundations"),BlueprintRefs[0].Blueprint(World)==Proxy && BlueprintRefs[1].Blueprint(World)==Proxy);
        }
        if(Phase==11)
        {
            auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState());
            FDismantleLightweightBundle Bundle(BlueprintRefs[0].LightweightClass);
            for(const auto& Ref:BlueprintRefs) Bundle.RemovalIndices.Add(Ref.ResolveIndex(World));
            if(State && OldProxy.IsValid()) State->Server_DismantleActors_Implementation({OldProxy.Get()},{Bundle});
            Check(TEXT("Blueprint deletion removes both members"),!BlueprintRefs[0].IsAlive(World) && !BlueprintRefs[1].IsAlive(World));
        }
        if(Phase==12)
        {
            Check(TEXT("Empty native blueprint proxy is removed"),!OldProxy.IsValid());
            RCO->Server_UndoGunAction_Implementation(true);
            auto* Proxy=BlueprintRefs[0].Blueprint(World);
            Check(TEXT("Undo restores native blueprint selection identity"),Proxy && BlueprintRefs[1].Blueprint(World)==Proxy && Proxy->GetActorTransform().Equals(BlueprintPose,.01));
            FString RedoMessage;
            Check(TEXT("Deletion redo removes the restored blueprint as one action"),FBSActionHistory::Undo(RCO,true,RedoMessage,true) && !BlueprintRefs[0].IsAlive(World) && !BlueprintRefs[1].IsAlive(World));
            Check(TEXT("Redone blueprint deletion can be undone again"),FBSActionHistory::Undo(RCO,true,RedoMessage) && BlueprintRefs[0].IsAlive(World) && BlueprintRefs[1].IsAlive(World));
            Edit->TransformMembers=BlueprintRefs; Edit->Selection={}; Edit->BlueprintAnchor={};
            Check(TEXT("Complete blueprint exposes its global transform"),Edit->TransformPoseText().Contains(TEXT("Blueprint")) && Edit->TransformPoseText().Contains(TEXT("world")));
            Edit->TransformMembers.Reset();
        }
        if(Phase==13)
        {
            Network.Reset(); const FVector Base=Player->GetActorLocation()+FVector(1000,1500,200);
            auto Spawn=[&](const TCHAR* Directory,const TCHAR* Name,const TCHAR* Recipe,FVector Offset,bool Deferred=false)
            {
                auto* Class=LoadClass<AFGBuildable>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/%s/%s.%s_C"),Directory,Name,Name));
                auto* RC=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/%s.%s_C"),Recipe,Recipe));
                auto* B=Class && RC ? AFGBuildableSubsystem::Get(World)->BeginSpawnBuildable(Class,FTransform(Base+Offset)) : nullptr;
                if(B) { B->SetBuiltWithRecipe(RC); AFGRecipeManager::Get(World)->AddAvailableRecipe(RC); if(!Deferred) B->FinishSpawning(B->GetActorTransform()); }
                return B;
            };
            auto* Machine=Spawn(TEXT("ConstructorMk1"),TEXT("Build_ConstructorMk1"),TEXT("Recipe_ConstructorMk1"),{});
            auto* External=Spawn(TEXT("ConstructorMk1"),TEXT("Build_ConstructorMk1"),TEXT("Recipe_ConstructorMk1"),FVector(0,1200,0));
            auto* A=Machine?Machine->FindComponentByClass<UFGPowerConnectionComponent>():nullptr;
            auto* B=External?External->FindComponentByClass<UFGPowerConnectionComponent>():nullptr;
            auto* Wire=Cast<AFGBuildableWire>(Spawn(TEXT("PowerLine"),TEXT("Build_PowerLine"),TEXT("Recipe_PowerLine"),{},true));
            Check(TEXT("External power restoration fixture"),Wire && A && B); if(!Wire || !A || !B) return false;
            Wire->Connect(A,B); Wire->FinishSpawning(Wire->GetActorTransform()); Network={Machine,Wire}; OutsidePower=B;
            auto* Pump=Spawn(TEXT("PipePump"),TEXT("Build_PipelinePump"),TEXT("Recipe_PipelinePump"),FVector(-1200,0,0));
            TInlineComponentArray<UFGPipeConnectionComponent*> Ports(Pump);
            auto* Pipe=Cast<AFGBuildablePipeline>(Spawn(TEXT("Pipeline"),TEXT("Build_Pipeline"),TEXT("Recipe_Pipeline"),{},true));
            Check(TEXT("External pipe restoration fixture"),Pump && Ports.Num()==2 && Pipe); if(!Pump || Ports.Num()!=2 || !Pipe) return false;
            Pipe->SetActorLocation(Ports[0]->GetComponentLocation()); const FVector Direction=Ports[0]->GetForwardVector();
            *Pipe->GetMutableSplinePointData()={FSplinePointData(FVector::ZeroVector,Direction*1000),FSplinePointData(Direction*1000,Direction*1000)};
            Pipe->FinishSpawning(Pipe->GetActorTransform()); Pipe->GetPipeConnection0()->SetConnection(Ports[0]);
            OutsidePipe=Ports[0]; Network.Add(Pipe);
        }
        if(Phase==14)
        {
            auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState()); TArray<AActor*> Actors;
            for(const auto& Weak:Network) if(auto* B=Weak.Get()) Actors.Add(B);
            if(State) State->Server_DismantleActors_Implementation(Actors,{});
        }
        if(Phase==15)
        {
            Check(TEXT("Network deletion frees external connections"),OutsidePower.IsValid() && OutsidePower->GetNumConnections()==0 && OutsidePipe.IsValid() && !OutsidePipe->GetConnection());
            if(OutsidePipe.IsValid())
            {
                auto* Owner=OutsidePipe->GetOwner(); auto* Root=Owner->GetRootComponent();
                const FVector Position=Owner->GetActorLocation(); const auto Mobility=Root->Mobility;
                Root->SetMobility(EComponentMobility::Movable); Owner->SetActorLocation(Position+FVector(5,0,0));
                const int32 Count=RCO->DeleteHistory.Num(); FString Error;
                Check(TEXT("Moved outside connections reject undo without consuming history"),!FBSActionHistory::Undo(RCO,true,Error) && Error.Contains(TEXT("outside connection")) && RCO->DeleteHistory.Num()==Count);
                Owner->SetActorLocation(Position); Root->SetMobility(Mobility);
            }
            RCO->Server_UndoGunAction_Implementation(true);
            Check(TEXT("Undo reconnects the external power wire"),OutsidePower.IsValid() && OutsidePower->GetNumConnections()==1);
            Check(TEXT("Undo reconnects the external pipeline"),OutsidePipe.IsValid() && OutsidePipe->GetConnection() && OutsidePipe->GetConnection()->GetConnection()==OutsidePipe.Get());
        }
        if(Phase==16)
        {
            auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState()); AFGBuildableStorage* Target=nullptr;
            for(TActorIterator<AFGBuildableStorage> It(World); It; ++It) if(!It->GetIsDismantled() && It->GetActorLocation().Equals(StorageLocation,.1)) Target=*It;
            if(State && Target) State->Server_DismantleActors_Implementation({Target},{});
            Player->GetInventory()->Remove(Plate,Player->GetInventory()->GetNumItems(Plate));
            const int32 Count=RCO->DeleteHistory.Num(); FString Message;
            Check(TEXT("Spent refunds block undo before restoring any contents"),!FBSActionHistory::Undo(RCO,true,Message) && Message.Contains(TEXT("refund")) && RCO->DeleteHistory.Num()==Count);
        }
        if(Phase==17) { Gun->GotoNoneState(); Key(EKeys::LeftAlt,true); Key(EKeys::E,true); }
        if(Phase==18)
        {
            Key(EKeys::E,false); Key(EKeys::LeftAlt,false);
            Edit->Selection={}; Edit->Selection.Buildings={Refs[2]}; Edit->Hover={}; Edit->BlueprintAnchor={}; Edit->OnTransform();
            Check(TEXT("Transform HUD is visible with world coordinates"),Edit->Indicator && Edit->IsEditing() && Edit->GetStatusText().ToString().Contains(TEXT("Position (cm)")));
        }
        if(Phase==19) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsPolishPose.png"),true,false);
        if(Phase==20) { Edit->EndTransform(); Edit->LeaveEditMode(false); }
        if(Phase==21)
        {
            Gun->GotoDismantleState();
            if(auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState())) Gun->SetCurrentBuildGunMode(State->mDefaultDismantleMode);
            auto* RailClass=LoadClass<AFGBuildableRailroadTrack>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/Train/Track/Build_RailroadTrack.Build_RailroadTrack_C"));
            auto* RailRecipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_RailroadTrack.Recipe_RailroadTrack_C"));
            auto* SignalClass=LoadClass<AFGBuildableRailroadSignal>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/Train/Signal/Build_RailroadBlockSignal.Build_RailroadBlockSignal_C"));
            auto* SignalRecipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/Train/Signal/Recipe_RailroadBlockSignal.Recipe_RailroadBlockSignal_C"));
            Check(TEXT("Rail and signal fixture classes"),RailClass && RailRecipe && SignalClass && SignalRecipe);
            if(!RailClass || !RailRecipe || !SignalClass || !SignalRecipe) return false;
            AFGRecipeManager::Get(World)->AddAvailableRecipe(RailRecipe); AFGRecipeManager::Get(World)->AddAvailableRecipe(SignalRecipe);
            RailLocation=Player->GetActorLocation()+FVector(-2500,500,200);
            for(int32 I=0; I<2; ++I)
            {
                auto* Rail=Cast<AFGBuildableRailroadTrack>(AFGBuildableSubsystem::Get(World)->BeginSpawnBuildable(RailClass,FTransform(RailLocation+FVector(I*1000,0,0))));
                if(!Rail) return false;
                Rail->SetBuiltWithRecipe(RailRecipe);
                *Rail->GetMutableSplinePointData()={FSplinePointData(FVector::ZeroVector,FVector(1000,0,0)),FSplinePointData(FVector(1000,0,0),FVector(1000,0,0))};
                Rail->FinishSpawning(Rail->GetActorTransform()); if(I==0) RailA=Rail; else RailB=Rail;
            }
            RailA->GetConnection(1)->AddConnection(RailB->GetConnection(0));
            auto* S=Cast<AFGBuildableRailroadSignal>(AFGBuildableSubsystem::Get(World)->BeginSpawnBuildable(SignalClass,FTransform(RailLocation+FVector(1000,0,0))));
            if(!S) return false;
            S->SetBuiltWithRecipe(SignalRecipe); S->AddGuardedConnection(RailA->GetConnection(1)); S->AddObservedConnection(RailB->GetConnection(0));
            S->FinishSpawning(S->GetActorTransform()); S->UpdateConnections(); Signal=S;
            Check(TEXT("Signal owns an existing rail junction"),S->HasValidConnections() && RailA->GetConnection(1)->GetFacingSignal()==S);
        }
        if(Phase==22)
        {
            auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState());
            const int32 Count=RCO->DeleteHistory.Num();
            if(State && Signal.IsValid()) State->Server_DismantleActors_Implementation({Signal.Get()},{});
            Check(TEXT("Signal deletion is captured"),RCO->DeleteHistory.Num()==Count+1 && RCO->DeleteHistory.Last()->Error.IsEmpty() && (!Signal.IsValid() || Signal->GetIsDismantled()));
        }
        if(Phase==23)
        {
            if(!RailA.IsValid() || !RailB.IsValid()) return false;
            Check(TEXT("Signal dismantling releases the junction"),!RailA->GetConnection(1)->GetFacingSignal());
            RCO->Server_UndoGunAction_Implementation(true);
            Signal=RailA->GetConnection(1)->GetFacingSignal();
            Check(TEXT("Undo restores signal orientation and reciprocal rail ownership"),Signal.IsValid() && Signal->GetGuardedConnections().Contains(RailA->GetConnection(1)) && Signal->GetObservedConnections().Contains(RailB->GetConnection(0)) && RailB->GetConnection(0)->GetTrailingSignal()==Signal.Get());
        }
        if(Phase==24)
        {
            auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState());
            const int32 Count=RCO->DeleteHistory.Num();
            if(State && RailA.IsValid() && Signal.IsValid()) State->Server_DismantleActors_Implementation({Signal.Get(),RailA.Get()},{});
            Check(TEXT("Rail and signal deletion creates one action"),RCO->DeleteHistory.Num()==Count+1 && (!RailA.IsValid() || RailA->GetIsDismantled()));
        }
        if(Phase==25)
        {
            RCO->Server_UndoGunAction_Implementation(true);
            AFGBuildableRailroadTrack* Restored=nullptr;
            for(TActorIterator<AFGBuildableRailroadTrack> It(World); It; ++It) if(!It->GetIsDismantled() && It->GetActorLocation().Equals(RailLocation,.1)) Restored=*It;
            Check(TEXT("Undo reconnects a rail segment to the unselected track"),Restored && RailB.IsValid() && Restored->GetConnection(1)->GetConnections().Contains(RailB->GetConnection(0)));
            Check(TEXT("A rail and its signal restore in the same action"),Restored && IsValid(Restored->GetConnection(1)->GetFacingSignal()));
        }
        if(Phase==26)
        {
            LiftLocation=Player->GetActorLocation()+FVector(4000,-3000,1000);
            auto Spawn=[&](const TCHAR* Name,const FTransform& Pose)
            {
                const FString Build=FString(TEXT("Build_"))+Name, Recipe=FString(TEXT("Recipe_"))+Name;
                auto* Class=LoadClass<AFGBuildable>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/%s/%s.%s_C"),Name,*Build,*Build));
                auto* RC=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/%s.%s_C"),*Recipe,*Recipe));
                auto* B=Class && RC ? AFGBuildableSubsystem::Get(World)->BeginSpawnBuildable(Class,Pose) : nullptr;
                if(B) {B->SetBuiltWithRecipe(RC); AFGRecipeManager::Get(World)->AddAvailableRecipe(RC);}
                return B;
            };
            auto* L=Cast<AFGBuildableConveyorLift>(Spawn(TEXT("ConveyorLiftMk1"),FTransform(LiftLocation)));
            Check(TEXT("Lift deletion fixture spawns"),L!=nullptr); if(!L)return false;
            auto* Top=FindFProperty<FStructProperty>(L->GetClass(),TEXT("mTopTransform"));
            if(!Top)return false;
            *Top->ContainerPtrToValuePtr<FTransform>(L)=FTransform(FVector(0,0,800)); L->FinishSpawning(L->GetActorTransform()); Lift=L;
            for(int32 I=0;I<2;++I)
            {
                auto* Port=I==0?L->GetConnection0():L->GetConnection1();
                const FVector End=Port->GetComponentLocation(), Direction=Port->GetForwardVector()*1200;
                const FVector Start=I==0?End+Direction:End, Delta=I==0?-Direction:Direction;
                auto* Belt=Cast<AFGBuildableConveyorBelt>(Spawn(TEXT("ConveyorBeltMk1"),FTransform(Start)));
                if(!Belt)return false;
                *Belt->GetMutableSplinePointData()={FSplinePointData(FVector::ZeroVector,Delta),FSplinePointData(Delta,Delta)};
                Belt->FinishSpawning(Belt->GetActorTransform()); Port->SetConnection(I==0?Belt->GetConnection1():Belt->GetConnection0());
                if(I==0){LowerBelt=Belt;LowerLocation=Start;}else UpperBelt=Belt;
            }
            Check(TEXT("Lift connects to two unselected belts"),L->GetConnection0()->GetConnection()==LowerBelt->GetConnection1() && L->GetConnection1()->GetConnection()==UpperBelt->GetConnection0());
            Check(TEXT("Bulk archive preserves foreign-owned dependency data and rejects null entries safely"),TestBuildingSkillsArchiveObjects(L,LowerBelt->GetConnection1()));
        }
        if(Phase==27 || Phase==29)
        {
            auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState());
            if(!State || !Lift.IsValid() || !LowerBelt.IsValid() || !UpperBelt.IsValid())return false;
            const int32 Count=RCO->DeleteHistory.Num(); TArray<AActor*> Targets{Lift.Get()};
            if(Phase==29)Targets.Add(LowerBelt.Get());
            State->Server_DismantleActors_Implementation(Targets,{});
            Check(Phase==27?TEXT("Single connected lift deletes with a valid undo archive"):TEXT("Lift and belt batch deletes with one valid undo archive"),
                RCO->DeleteHistory.Num()==Count+1 && RCO->DeleteHistory.Last()->Error.IsEmpty() && (!Lift.IsValid() || Lift->GetIsDismantled()));
            Check(TEXT("Unselected upper belt survives lift deletion"),UpperBelt.IsValid() && !UpperBelt->GetIsDismantled());
            if(Lift.IsValid())
            {
                auto* Stale=NewObject<UBSActionRecord>(RCO);
                Check(TEXT("Dismantled lift cannot be captured again during its effect"),!Stale->Capture(Player,{FBSBuildingRef::FromActor(Lift.Get())}));
            }
        }
        if(Phase==28 || Phase==30)
        {
            const int32 Count=RCO->DeleteHistory.Num(); RCO->Server_UndoGunAction_Implementation(true);
            Lift.Reset();
            for(TActorIterator<AFGBuildableConveyorLift> It(World);It;++It)
                if(!It->GetIsDismantled() && It->GetActorLocation().Equals(LiftLocation,.1))Lift=*It;
            if(Phase==30)
                for(TActorIterator<AFGBuildableConveyorBelt> It(World);It;++It)
                    if(!It->GetIsDismantled() && It->GetActorLocation().Equals(LowerLocation,.1))LowerBelt=*It;
            const bool Linked=Lift.IsValid() && LowerBelt.IsValid() && UpperBelt.IsValid() &&
                Lift->GetConnection0()->GetConnection()==LowerBelt->GetConnection1() && LowerBelt->GetConnection1()->GetConnection()==Lift->GetConnection0() &&
                Lift->GetConnection1()->GetConnection()==UpperBelt->GetConnection0() && UpperBelt->GetConnection0()->GetConnection()==Lift->GetConnection1();
            Check(Phase==28?TEXT("Undo restores the lift and both external belt connections"):TEXT("Batch undo restores internal and external conveyor connections"),Linked && RCO->DeleteHistory.Num()==Count-1);
            auto* Top=Lift.IsValid()?FindFProperty<FStructProperty>(Lift->GetClass(),TEXT("mTopTransform")):nullptr;
            Check(TEXT("Restored lift keeps its height and is immediately visible and collidable"),Top && Top->ContainerPtrToValuePtr<FTransform>(Lift.Get())->GetLocation().Equals(FVector(0,0,800),.01) && !Lift->IsHidden() && Lift->GetActorEnableCollision() && !Lift->IsPlayingBuildEffect());
        }
        if(Phase==31) {Gun->GotoNoneState();Key(EKeys::LeftAlt,true);Key(EKeys::E,true);}
        if(Phase==32)
        {
            Key(EKeys::E,false);Key(EKeys::LeftAlt,false);LargeSelection.Reset();
            Check(TEXT("Large selection fixture enters Edit"),Edit->IsEditing());
            auto* Class=Refs[0].LightweightClass.Get();
            for(int32 I=0;I<900;++I)
            {
                FRuntimeBuildableInstanceData Data;
                Data.Transform=FTransform(Player->GetActorLocation()+FVector(1600+(I%30)*800,1000+(I/30)*800,100));
                Data.BuiltWithRecipe=FoundationRecipe;
                Data.CustomizationData.SwatchDesc=Class->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(World);
                LargeSelection.Add(FBSBuildingRef::FromLightweight(World,Class,LW->AddFromBuildableInstanceData(Class,Data)));
            }
            Edit->Selection={};Edit->Selection.Buildings=LargeSelection;Edit->Hover={};Edit->BlueprintAnchor={};
            Edit->bSelectionDirty=true;Edit->OnGroup();
        }
        if(Phase==33)
        {
            TArray<FBSBuildingRef> Members;
            if(Edit->Selection.Groups.Num()==1)FBSGroupTree::Collect(Groups->Groups,Edit->Selection.Groups[0],Members);
            Check(TEXT("Acknowledged Group request groups all 900 foundations"),Members.Num()==900 && !Edit->bRequestPending && !RCO->SelectionUpload.Request.IsValid());
            Edit->OnUngroup();
        }
        if(Phase==34)
        {
            Check(TEXT("Chunked Ungroup response restores all 900 selected foundations"),Edit->Selection.Groups.IsEmpty() && Edit->Selection.Buildings.Num()==900 && !Edit->bRequestPending && !RCO->SelectionReply.Request.IsValid());
            Edit->OnSave();
        }
        if(Phase==35)
        {
            Check(TEXT("Acknowledged Save request opens native blueprint window for 900 objects"),Edit->BlueprintWindow && Edit->BlueprintSession && Edit->BlueprintSession->Members.Num()==900 && !Edit->bOpeningBlueprint);
            if(auto* Session=Edit->BlueprintSession.Get())
            {
                FString Error;const bool Ready=Session->Prepare(Error);
                FBlueprintRecord Record;Record.BlueprintName=TEXT("BSLargeSelectionRegression");
                FBlueprintSaveData Data;TArray<FActorSaveHeader> Headers;
                const bool Saved=Ready && WriteBuildingSkillsClipboard(AFGBlueprintSubsystem::Get(World),Record,Session->BlueprintOrigin,Session->GetSelectionBuildables(),Session->GetSelectionDimensions(),Data);
                Check(TEXT("All 900 selected objects survive native blueprint serialization"),Saved && ReadBuildingSkillsBlueprintHeaders(Data,Headers) && Headers.Num()==900);
                Session->ShowSummaryOnly();
            }
            Edit->CloseBlueprintSave();
        }
        if(Phase==36)Edit->OnCopy();
        if(Phase==37)
        {
            Check(TEXT("Acknowledged Copy request captures all 900 objects"),!RCO->Clipboards.IsEmpty() && RCO->Clipboards.Last()->Members.Num()==900 && !Edit->bCopyPending && Gun->GetCurrentState() && Gun->GetCurrentState()->IsA<UFGBuildGunStateBuild>());
            Gun->GotoNoneState();Key(EKeys::LeftAlt,true);Key(EKeys::E,true);
        }
        if(Phase==38)
        {
            Key(EKeys::E,false);Key(EKeys::LeftAlt,false);Edit->Selection={};Edit->Selection.Buildings=LargeSelection;
            Edit->bRequestPending=true;Edit->PendingRevision=INDEX_NONE;RCO->ExpectedSelectionResult.Invalidate();
            FBSSelectionPacket Stale;Stale.Request=FGuid::NewGuid();Stale.Action=5;Stale.Success=true;Stale.Revision=Groups->Revision;
            RCO->Client_SelectionResultPacket_Implementation(Stale);
            Check(TEXT("Late group reply cannot replace a timed-out selection"),Edit->PendingRevision==INDEX_NONE && Edit->Selection.Buildings.Num()==900);
            Edit->GroupRequestDeadline=FPlatformTime::Seconds()-1;
        }
        if(Phase==39)Check(TEXT("Group timeout releases controls without losing the 900-object selection"),!Edit->bRequestPending && Edit->Selection.Buildings.Num()==900);
        return Passed;
    }
};
bool RunBuildingSkillsPolishChecks(AFGCharacterPlayer* Player,int32 Phase) { return FBSPolishSmoke::Run(Player,Phase); }
