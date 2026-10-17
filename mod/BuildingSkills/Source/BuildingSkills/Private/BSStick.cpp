#include "BSStick.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "Hologram/FGSplineHologram.h"
#include "Hologram/FGConveyorBeltHologram.h"
#include "Hologram/FGPipelineHologram.h"
#include "Hologram/FGRailroadTrackHologram.h"
#include "Hologram/FGPoleHologram.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGCharacterPlayer.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "FGConstructDisqualifier.h"
#include "FGDismantleInterface.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UI/FGGameUI.h"
#include "UObject/UnrealType.h"
#include "Patching/NativeHookManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "FGBuildableSubsystem.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "FGBlueprintSubsystem.h"
#include "FGBlueprintProxy.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Camera/PlayerCameraManager.h"

namespace BSStickPrivate
{
    struct FState
    {
        TArray<FSplinePointData> Unsplit;
        TArray<FSplinePointData> Preview;
        TWeakObjectPtr<UObject> StartSnap;
        TWeakObjectPtr<UObject> EndSnap;
        int32 ActivePoint=0;
        int32 Parts=1;
        bool Invalid=false;
    };
    TMap<TWeakObjectPtr<AFGSplineHologram>,FState> States;
    TMap<TWeakObjectPtr<AFGSplineHologram>,FBSStickPath> Starts;
    TSet<TWeakObjectPtr<AFGHologram>> ReceivedStickRoots;
    TArray<TWeakObjectPtr<AFGCharacterPlayer>> TestConstructionPayers;
    thread_local TMap<AFGSplineHologram*,AActor*>* Constructed=nullptr;
    thread_local APawn* ConstructionPlayer=nullptr;
    thread_local bool ServerConstructionRequest=false;
    bool TestNativeEndpointClick=false;
    struct FTestStamp { FString Name; FVector Location; int32 Objects=0; };
    TArray<FTestStamp> TestStamps;
    bool ObserveTestStamps=false;
    TArray<TWeakObjectPtr<UFGBlueprintDescriptor>> TestPresetDescriptors;
    TArray<FString> TestPresetNames;
    int32 TestRailPieces=5;
    float TestTileLength=800;

    bool Supported(const AFGHologram* H)
    { return H && (H->IsA<AFGConveyorBeltHologram>() || H->IsA<AFGPipelineHologram>() || H->IsA<AFGRailroadTrackHologram>()); }
    bool Part(const AFGHologram* H) { return H && H->GetNameWithinParentHologram().ToString().StartsWith(TEXT("BuildingSkillsStick_")); }
    UBSEditModeComponent* EditFor(const AFGHologram* H)
    {
        auto* P=H ? Cast<AFGCharacterPlayer>(H->GetConstructionInstigator()) : nullptr;
        auto* E=P && P->IsLocallyControlled() ? P->FindComponentByClass<UBSEditModeComponent>() : nullptr;
        return E && FBSStick::Active(E)==H ? E : nullptr;
    }
    bool StickRoot(const AFGHologram* H)
    {
        if (!Supported(H) || H->GetParentHologram()) return false;
        if (ReceivedStickRoots.Contains(const_cast<AFGHologram*>(H))) return true;
        const auto* E=EditFor(H);
        return E && E->StickMode!=EBSStickMode::Off;
    }
    UObject* Snap(AFGSplineHologram* H,int32 Index)
    {
        auto* P=FindFProperty<FObjectPropertyBase>(H->GetClass(),TEXT("mSnappedConnectionComponents"));
        return P && P->ArrayDim==2 ? P->GetObjectPropertyValue_InContainer(H,Index) : nullptr;
    }
    void Snap(AFGSplineHologram* H,int32 Index,UObject* Value)
    {
        auto* P=FindFProperty<FObjectPropertyBase>(H->GetClass(),TEXT("mSnappedConnectionComponents"));
        if (P && P->ArrayDim==2) P->SetObjectPropertyValue_InContainer(H,Value,Index);
    }
}
using namespace BSStickPrivate;

AFGSplineHologram* FBSStick::Active(UBSEditModeComponent* E)
{
    auto* P=E ? Cast<AFGCharacterPlayer>(E->GetOwner()) : nullptr;
    auto* G=P && P->IsBuildGunEquipped() ? P->GetBuildGun() : nullptr;
    auto* B=G ? Cast<UFGBuildGunStateBuild>(G->GetCurrentState()) : nullptr;
    auto* H=B ? Cast<AFGSplineHologram>(B->GetHologram()) : nullptr;
    return Supported(H) && !H->GetParentHologram() ? H : nullptr;
}
double FBSStick::MaxLength(AFGSplineHologram* H)
{
    if (auto* B=Cast<AFGConveyorBeltHologram>(H)) return B->mMaxSplineLength;
    if (auto* P=Cast<AFGPipelineHologram>(H)) return P->mMaxSplineLength;
    if (auto* R=Cast<AFGRailroadTrackHologram>(H)) return R->mMaxLength;
    return 0;
}
void FBSStick::SetPoints(AFGSplineHologram* H,const TArray<FSplinePointData>& Points)
{
    H->mSplineData=Points;
    H->UpdateSplineComponent();
    H->UpdateClearanceData();
    if (auto* P=Cast<AFGPipelineHologram>(H)) P->UpdateConnectionComponentsFromSplineData();
    // Native point refresh is also used after multiplayer deserialization.
}
void FBSStick::ResizeChildren(AFGSplineHologram* H,int32 Count)
{
    for (int32 I=H->mChildren.Num()-1; I>=0; --I)
    {
        auto* C=H->mChildren[I].Get();
        if (!Part(C)) continue;
        const FString Name=C->GetNameWithinParentHologram().ToString();
        if (FCString::Atoi(*Name.RightChop(20)) <= Count) continue;
        H->mChildrenNameLookupMap.Remove(C->GetNameWithinParentHologram());
        H->mChildren.RemoveAt(I); C->Destroy();
    }
    for (int32 I=1; I<=Count; ++I)
    {
        const FName Name(*FString::Printf(TEXT("BuildingSkillsStick_%d"),I));
        if (H->FindChildHologramByName(Name)) continue;
        auto* Child=AFGHologram::SpawnChildHologramFromRecipe(H,Name,H->GetRecipe(),H->GetOwner(),H->GetActorLocation(),
            [](AFGHologram* C) { C->mShouldSpawnChildHolograms=false; });
        // The parent has already begun this frame's placement cycle. This new
        // child misses its initial reset, but receives all geometry below.
        if (Child) Child->ResetConstructDisqualifiers();
    }
}
void FBSStick::Restore(AFGSplineHologram* H,bool RemoveChildren)
{
    if (auto* S=States.Find(H); S && !S->Unsplit.IsEmpty())
    {
        SetPoints(H,S->Unsplit); H->mActivePointIdx=S->ActivePoint;
        Snap(H,1,S->EndSnap.Get()); S->Unsplit.Reset();
    }
    if (RemoveChildren) ResizeChildren(H,0);
    States.Remove(H);
}
bool FBSStick::ApplySegments(AFGSplineHologram* H,const TArray<TArray<FSplinePointData>>& Segments)
{
    if (Segments.IsEmpty() || Segments.Num()>256) return false;
    auto& State=States.FindOrAdd(H);
    State.Unsplit=H->mSplineData; State.StartSnap=Snap(H,0); State.EndSnap=Snap(H,1); State.ActivePoint=H->mActivePointIdx;
    State.Parts=Segments.Num(); State.Invalid=false;
    State.Preview.Reset();
    for (const auto& Segment:Segments)
    {
        if (!State.Preview.IsEmpty()) State.Preview.Last().LeaveTangent=Segment[0].LeaveTangent;
        for (int32 J=State.Preview.IsEmpty() ? 0 : 1; J<Segment.Num(); ++J) State.Preview.Add(Segment[J]);
    }
    ResizeChildren(H,Segments.Num()-1);
    for (int32 I=0; I<Segments.Num(); ++I)
    {
        auto* PartH=I==0 ? H : Cast<AFGSplineHologram>(H->FindChildHologramByName(FName(*FString::Printf(TEXT("BuildingSkillsStick_%d"),I))));
        if (!PartH) { State.Invalid=true; return false; }
        // Native placement unhides the directly aimed hologram. Generated
        // segments have no independent aim update, so initialize that state here.
        if (I>0) PartH->SetActorHiddenInGame(false);
        PartH->SetActorTransform(H->GetActorTransform());
        PartH->SetConstructionInstigator(H->GetConstructionInstigator());
        PartH->mBuildStep=H->mBuildStep;
        PartH->mActivePointIdx=Segments[I].Num()-1;
        PartH->SetBuildModeOverride(H->mBuildModeOverride);
        if (I>0)
        {
            Snap(PartH,0,nullptr);
            if (auto* Belt=Cast<AFGConveyorBeltHologram>(PartH))
            {
                auto* Root=CastChecked<AFGConveyorBeltHologram>(H);
                Belt->mConnectionArrowComponentDirection=Root->mConnectionArrowComponentDirection;
                for (int32 J=0; J<2; ++J) Belt->mConnectionComponents[J]->SetDirection(Root->mConnectionComponents[J]->GetDirection());
            }
        }
        Snap(PartH,1,I==Segments.Num()-1 ? State.EndSnap.Get() : nullptr);
        SetPoints(PartH,Segments[I]);
        if (auto* Rail=Cast<AFGRailroadTrackHologram>(PartH))
        {
            Rail->mFloorData.Reset();
            for (const auto& P:Segments[I]) Rail->mFloorData.Add(P.Location);
        }
    }
    return true;
}
void FBSStick::Update(AFGSplineHologram* H,UBSEditModeComponent* E,const FVector& Target)
{
    if (H->mBuildStep==ESplineHologramBuildStep::SHBS_FindStart || H->mBuildStep==ESplineHologramBuildStep::SHBS_AdjustStartingPole || H->mSplineData.Num()<2) return;
    TArray<TArray<FSplinePointData>> Segments;
    bool Valid=false;
    FVector StartDirection=FVector::ZeroVector;
    const auto Transform=H->GetActorTransform();
    const auto& Settings=E->GetWorld()->GetSubsystem<UBSStickSession>()->Settings;
    // An accepted native endpoint owns its position, tangent and route.
    if (E->StickMode==EBSStickMode::Constrained && (Settings.VerticalConstraint || Settings.HorizontalConstraint) && !Snap(H,1))
    {
        FBSStickPath Path;
        Path.Start=Transform.TransformPosition(H->mSplineData[0].Location);
        Path.Forward=Transform.TransformVectorNoScale(H->mSplineData[0].LeaveTangent);
        if (const auto* Saved=Starts.Find(H); Saved && Saved->Start.Equals(Path.Start,.1)) Path.Forward=Saved->Forward;
        StartDirection=Transform.InverseTransformVectorNoScale(Path.Forward);
        const double Distance=Path.ResolveConstraints(Settings,Target);
        if (!Settings.VerticalConstraint && Distance<=.01)
        {
            // A free height above the starting XY needs a vertical route, not
            // an artificial near-90-degree grade or a forced zero-height target.
            const FVector End(Path.Start.X,Path.Start.Y,Target.Z);
            const FVector Delta=End-Path.Start;
            Valid=Delta.Size()>.01 && FBSStickPath::Split({FSplinePointData(Path.Start,Delta),FSplinePointData(End,Delta)},MaxLength(H),Segments);
        }
        else Valid=Path.Segments(Distance,MaxLength(H),Segments);
        for (auto& Segment:Segments) for (auto& P:Segment)
        { P.Location=Transform.InverseTransformPosition(P.Location); P.ArriveTangent=Transform.InverseTransformVector(P.ArriveTangent); P.LeaveTangent=Transform.InverseTransformVector(P.LeaveTangent); }
    }
    else Valid=FBSStickPath::Split(H->mSplineData,MaxLength(H),Segments);
    if (Valid && !Snap(H,1))
    {
        FVector EndDirection=FVector::ZeroVector;
        if (E->StickTiltSet || E->StickYawSet)
        {
            const FVector Tangent=Transform.TransformVectorNoScale(Segments.Last().Last().ArriveTangent);
            EndDirection=Transform.InverseTransformVectorNoScale(FBSStickPath::EndpointDirection(
                Tangent,E->StickTiltSet,E->StickTilt,E->StickYaw));
        }
        Valid=FBSStickPath::FitEndDirections(Segments,StartDirection,EndDirection,MaxLength(H));
    }
    if (Valid) Valid=ApplySegments(H,Segments);
    States.FindOrAdd(H).Invalid=!Valid;
    E->StickStatus=Valid ? FString::Printf(TEXT("%d segment%s"),Segments.Num(),Segments.Num()==1 ? TEXT("") : TEXT("s")) : TEXT("No valid path (maximum 256 segments)");
}
void FBSStick::Validate(AFGSplineHologram* H)
{
    auto* State=States.Find(H);
    bool HasParts=false;
    for (auto* C:H->GetHologramChildren()) HasParts |= Part(C);
    if (!State && !HasParts) return;
    bool Valid=!State || !State->Invalid;
    for (auto* C:H->GetHologramChildren()) if (Part(C))
    {
        auto* P=Cast<AFGCharacterPlayer>(H->GetConstructionInstigator());
        if (!P) { Valid=false; continue; }
        // Native ValidatePlacementAndCost already traverses enabled children.
        Valid &= C->CanConstruct();
    }
    if (!Valid)
    {
        H->AddConstructDisqualifier(UFGCDInvalidAimLocation::StaticClass());
        H->SetPlacementMaterialState(EHologramMaterialState::HMS_ERROR);
    }
}
bool FBSStick::Connect(AActor* A,AActor* B,const FVector& At)
{
    auto Find=[&]<typename T>(AActor* Actor) -> T* {
        TInlineComponentArray<T*> Ports(Actor);
        for (auto* P:Ports) if (P->GetComponentLocation().Equals(At,.5)) return P;
        return nullptr;
    };
    if (auto* X=Find.operator()<UFGFactoryConnectionComponent>(A))
    {
        auto* Y=Find.operator()<UFGFactoryConnectionComponent>(B);
        if (Y && X->GetConnection()==Y && Y->GetConnection()==X) return true;
        if (!Y || X->IsConnected() || Y->IsConnected() || X->GetDirection()==Y->GetDirection()) return false;
        X->SetConnection(Y); return X->GetConnection()==Y && Y->GetConnection()==X;
    }
    if (auto* X=Find.operator()<UFGPipeConnectionComponentBase>(A))
    {
        auto* Y=Find.operator()<UFGPipeConnectionComponentBase>(B);
        if (Y && X->GetConnection()==Y && Y->GetConnection()==X) return true;
        if (!Y || X->GetConnection() || Y->GetConnection() || !X->CanConnectTo(Y)) return false;
        X->SetConnection(Y); return X->GetConnection()==Y && Y->GetConnection()==X;
    }
    if (auto* X=Find.operator()<UFGRailroadTrackConnectionComponent>(A))
    {
        auto* Y=Find.operator()<UFGRailroadTrackConnectionComponent>(B);
        if (!Y) return false;
        if (!X->GetConnections().Contains(Y)) X->AddConnection(Y);
        return X->GetConnections().Contains(Y) && Y->GetConnections().Contains(X);
    }
    return false;
}
void FBSStick::Install()
{
    if (WITH_EDITOR) return;
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateBuild,Server_ConstructHologram_Implementation,
        [](auto& Scope,UFGBuildGunStateBuild* State,FNetConstructionID Id,FConstructHologramMessage Message) {
            TGuardValue<bool> RequestGuard(ServerConstructionRequest,true);
            Scope(State,Id,Message);
        });
    if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsStickTest")))
        SUBSCRIBE_METHOD(AFGBlueprintSubsystem::LoadStoredBlueprint,
            [](auto& Scope,AFGBlueprintSubsystem* System,UFGBlueprintDescriptor* Descriptor,const FTransform& Origin,TArray<AFGBuildable*>& Buildings,bool BlueprintWorld,
               AFGBuildableBlueprintDesigner* Designer,APawn* Instigator,AFGBlueprintProxy* Proxy,const TFunction<void(AFGBuildable*,int32)>& PostSerialize,const TFunction<void()>& PreBeginPlay) {
                auto Count=[](AFGBlueprintProxy* P) { int32 N=0; if (P) for (const auto& Entry:P->GetLightweightClassAndIndices()) N+=Entry.Indices.Num(); return N; };
                const int32 Before=Count(Proxy);
                Scope(System,Descriptor,Origin,Buildings,BlueprintWorld,Designer,Instigator,Proxy,PostSerialize,PreBeginPlay);
                if (ObserveTestStamps && !BlueprintWorld && Descriptor)
                    TestStamps.Add({Descriptor->GetBlueprintNameAsString(),Origin.GetLocation(),Buildings.Num()+Count(Proxy)-Before});
            });
    // This method belongs to a secondary C++ interface. Hook through that
    // interface, so both Windows and Linux pass the correctly adjusted pointer.
    SUBSCRIBE_METHOD_VIRTUAL(IFGConstructionMessageInterface::SerializeConstructMessage,
        static_cast<const IFGConstructionMessageInterface*>(GetDefault<AFGBuildableHologram>()),
        [](auto& Scope,IFGConstructionMessageInterface* Self,FArchive& Ar,FNetConstructionID Id) {
            auto* H=static_cast<AFGHologram*>(Self);
            // Snapshot accepted rail endpoints before native serialization. Send
            // the replicated track actor and connector index, not just a scene
            // component reference held by a generated child hologram.
            auto* Rail=Cast<AFGRailroadTrackHologram>(H);
            auto LastPart=[](AFGSplineHologram* Root) {
                AFGSplineHologram* Last=Root; int32 Index=0;
                for (auto* Child:Root->GetHologramChildren()) if (Part(Child))
                {
                    const int32 Next=FCString::Atoi(*Child->GetNameWithinParentHologram().ToString().RightChop(20));
                    if (Next>Index) { Last=CastChecked<AFGSplineHologram>(Child); Index=Next; }
                }
                return Last;
            };
            AFGBuildableRailroadTrack* Targets[2]{};
            uint8 TargetEnds[2];
            TargetEnds[0]=TargetEnds[1]=2; // 2 means deliberately unsnapped.
            if (Ar.IsSaving() && Rail && !Rail->GetParentHologram())
            {
                const auto* State=States.Find(Rail);
                for (int32 End=0; End<2; ++End)
                {
                    UObject* Saved=State ? (End==0 ? State->StartSnap.Get() : State->EndSnap.Get()) : Snap(End==0 ? Rail : LastPart(Rail),End);
                    auto* Port=Cast<UFGRailroadTrackConnectionComponent>(Saved);
                    auto* Track=Port ? Cast<AFGBuildableRailroadTrack>(Port->GetOwner()) : nullptr;
                    if (!IsValid(Track)) continue;
                    for (uint8 Index=0; Index<2; ++Index) if (Track->GetConnection(Index)==Port)
                    { Targets[End]=Track; TargetEnds[End]=Index; }
                }
            }
            Scope(Self,Ar,Id);
            if (!Supported(H) || H->GetParentHologram()) return;
            bool HasParts=false;
            for (auto* Child:H->GetHologramChildren()) HasParts |= Part(Child);
            uint8 Stick=Ar.IsSaving() && (StickRoot(H) || HasParts) ? 1 : 0;
            Ar.SerializeBits(&Stick,1);
            if (Stick && Rail) for (int32 End=0; End<2; ++End)
            {
                Ar.SerializeBits(&TargetEnds[End],2);
                if (TargetEnds[End]<2) Ar << Targets[End];
                if (!Ar.IsLoading() || Ar.IsError()) continue;
                auto* Piece=End==0 ? Rail : LastPart(Rail);
                auto* Target=IsValid(Targets[End]) && TargetEnds[End]<2 ? Targets[End]->GetConnection(TargetEnds[End]) : nullptr;
                if (TargetEnds[End]>2 || (TargetEnds[End]<2 && !Target))
                {
                    UE_LOG(LogBuildingSkills,Warning,TEXT("Stick construction rejected an unresolved rail endpoint."));
                    Ar.SetError(); continue;
                }
                Snap(Piece,End,Target);
            }
            if (Ar.IsLoading() && !Ar.IsError())
            {
                for (auto It=ReceivedStickRoots.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
                if (Stick) ReceivedStickRoots.Add(H);
                else ReceivedStickRoots.Remove(H);
            }
            if (ServerConstructionRequest && Ar.IsLoading())
                UE_LOG(LogBuildingSkills,Display,TEXT("Spline construction received: root=%s stick=%d children=%d archiveError=%d"),
                    *H->GetClass()->GetName(),Stick,H->GetHologramChildren().Num(),Ar.IsError());
        });
    SUBSCRIBE_UOBJECT_METHOD(AFGBuildableHologram,CheckValidFloor,[](auto& Scope,AFGBuildableHologram* H) {
        // Stick deliberately permits floating support poles. Keep floor checks
        // for ordinary placement and every unrelated building unchanged.
        if (H->IsA<AFGPoleHologram>() && StickRoot(H->GetParentHologram())) Scope.Cancel();
    });
    auto CompleteSnappedRoute=[](auto& Scope,auto* H,bool Released) {
        const auto* State=States.Find(H);
        if (TestNativeEndpointClick || !State || State->Parts<2 || !State->EndSnap.IsValid() ||
            H->mBuildStep!=ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding) return;
        // Construction needs the external end on the last piece, but the
        // native click state machine runs on the root. Expose the accepted
        // endpoint only while it decides whether the whole route is complete.
        // Restore before construction/serialization so the first piece cannot
        // connect directly to the far support or consume its connection.
        UObject* SegmentEnd=Snap(H,1);
        Snap(H,1,State->EndSnap.Get());
        ON_SCOPE_EXIT { Snap(H,1,SegmentEnd); };
        Scope(H,Released);
    };
    SUBSCRIBE_UOBJECT_METHOD(AFGPipelineHologram,DoMultiStepPlacement,CompleteSnappedRoute);
    SUBSCRIBE_UOBJECT_METHOD(AFGConveyorBeltHologram,DoMultiStepPlacement,CompleteSnappedRoute);
    SUBSCRIBE_UOBJECT_METHOD(AFGRailroadTrackHologram,TrySnapToActor,[](auto& Scope,AFGRailroadTrackHologram* H,const FHitResult&) {
        if (auto* E=EditFor(H); E && E->StickMode!=EBSStickMode::Off && H->mSnapToGuideLines)
        {
            // Rails do not apply the same modifier gate as native pipes/belts.
            // Use the configured native modifier state, never a physical key.
            const int32 End=H->mBuildStep==ESplineHologramBuildStep::SHBS_FindStart ? 0 : 1;
            Snap(H,End,nullptr);
            if (End==0) H->mSnappedRailroadTrack=nullptr;
            Scope.Override(false);
        }
    });
    SUBSCRIBE_UOBJECT_METHOD(AFGHologram,ValidatePlacementAndCost,[](auto& Scope,AFGHologram* H,UFGInventoryComponent* Inventory) {
        for (auto* C:H->GetHologramChildren()) if (Part(C)) C->SetActorHiddenInGame(false);
        auto* S=Cast<AFGSplineHologram>(H);
        auto* Parent=Cast<AFGSplineHologram>(H->GetParentHologram());
        if (!S || !Parent || !Part(H)) return;
        const int32 Index=FCString::Atoi(*H->GetNameWithinParentHologram().ToString().RightChop(20));
        auto* Previous=Index==1 ? Parent : Cast<AFGSplineHologram>(Parent->FindChildHologramByName(FName(*FString::Printf(TEXT("BuildingSkillsStick_%d"),Index-1))));
        if (!Previous || Previous->mSplineData.IsEmpty() || S->mSplineData.IsEmpty()) return;
        if (!Previous->GetActorTransform().TransformPosition(Previous->mSplineData.Last().Location).Equals(S->GetActorTransform().TransformPosition(S->mSplineData[0].Location),.5)) return;
        // The server resets before deserialization, which creates these children
        // afterwards. Their geometry is now ready; retain every real rejection.
        H->mConstructDisqualifiers.Remove(UFGCDInitializing::StaticClass());
        UObject* Port=nullptr;
        if (auto* Pipe=Cast<AFGPipelineHologram>(Previous)) Port=Pipe->mConnectionComponents[1];
        if (auto* Belt=Cast<AFGConveyorBeltHologram>(Previous)) Port=Belt->mConnectionComponents[1];
        if (!Port || Snap(S,0)) return;
        // Native pipe/belt checks require a supported or snapped start. During
        // validation only, the preceding hologram supplies that start. Restore
        // the pointer before serialization/construction, where real actors join.
        Snap(S,0,Port); Scope(H,Inventory); Snap(S,0,nullptr);
    });
    // BeginSpawnBuildable broadcasts before the hologram assigns its player.
    // Expose that player during this early event only, so optional listeners can
    // retain the payer without changing notification order or build effects.
    SUBSCRIBE_METHOD(AFGBuildableSubsystem::BeginSpawnBuildable,[](auto& Scope,AFGBuildableSubsystem* System,TSubclassOf<AFGBuildable> Class,const FTransform& Transform) {
        if (!IsValid(ConstructionPlayer) || !Class || !Class->IsChildOf(AFGBuildableRailroadTrack::StaticClass()) || System->GetWorld()!=ConstructionPlayer->GetWorld()) return;
        auto* World=System->GetWorld(); auto* Player=ConstructionPlayer;
        TWeakObjectPtr<AActor> Captured;
        FObjectPropertyBase* Property=nullptr;
        const auto Handle=World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor) {
            if (Captured.IsValid() || Actor->GetClass()!=Class.Get() || Actor->GetOwner()!=System) return;
            auto* P=FindFProperty<FObjectPropertyBase>(Actor->GetClass(),TEXT("mBuildEffectInstignator"));
            if (!P || P->GetObjectPropertyValue_InContainer(Actor)) return;
            Captured=Actor; Property=P; P->SetObjectPropertyValue_InContainer(Actor,Player);
        }));
        ON_SCOPE_EXIT {
            World->RemoveOnActorSpawnedHandler(Handle);
            if (Captured.IsValid() && Property && Property->GetObjectPropertyValue_InContainer(Captured.Get())==Player)
                Property->SetObjectPropertyValue_InContainer(Captured.Get(),nullptr);
        };
        Scope(System,Class,Transform);
    });
    SUBSCRIBE_METHOD(AFGBuildGun::Scroll,[](auto& Scope,AFGBuildGun* G,int32) {
        auto* P=Cast<AFGCharacterPlayer>(G->GetInstigator());
        auto* E=P ? P->FindComponentByClass<UBSEditModeComponent>() : nullptr;
        // Mouse wheel also drives a separate native axis action. Our mappable
        // wheel buttons own distance; do not let the axis rotate the endpoint.
        if (E && Active(E) && E->StickMode!=EBSStickMode::Off) Scope.Cancel();
    });
    SUBSCRIBE_METHOD(UFGGameUI::Native_HandlePauseGamePressed,[](auto& Scope,UFGGameUI* UI) {
        auto* P=UI->GetOwningPlayerPawn(); auto* E=P ? P->FindComponentByClass<UBSEditModeComponent>() : nullptr;
        if (!E || !E->StickWindow) return;
        const auto& Stack=UI->GetInteractWidgetStack();
        if (!Stack.IsEmpty() && Stack.Last()==E->StickWindow) { E->CloseStickSettings(); Scope.Cancel(); }
    });
    SUBSCRIBE_UOBJECT_METHOD(AFGHologram,UpdateHologramPlacement,[](auto& Scope,AFGHologram* Base,const FHitResult& Hit) {
        auto* H=Cast<AFGSplineHologram>(Base);
        auto* E=EditFor(H);
        if (!E) return;
        if (!E->InputAllowed()) { if (E->StickMode!=EBSStickMode::Off) Scope.Cancel(); return; }
        Restore(H,E->StickMode==EBSStickMode::Off);
        if (E->StickMode==EBSStickMode::Off) return;
        FVector Eye; FRotator View;
        auto* P=CastChecked<AFGCharacterPlayer>(E->GetOwner());
        if (auto* PC=Cast<APlayerController>(P->GetController())) PC->GetPlayerViewPoint(Eye,View);
        else P->GetActorEyesViewPoint(Eye,View);
        const FVector Target=Eye+View.Vector()*(E->StickDistance*100);
        FVector Aim=Target;
        if (H->mBuildStep==ESplineHologramBuildStep::SHBS_FindStart) Starts.Remove(H);
        else if (H->mSplineData.Num()>=2)
        {
            const FVector Start=H->GetActorTransform().TransformPosition(H->mSplineData[0].Location);
            const bool New=!Starts.Contains(H);
            auto& Path=Starts.FindOrAdd(H);
            if (New || !Path.Start.Equals(Start,.1) || Path.Forward.IsNearlyZero())
            { Path.Start=Start; Path.Forward=H->GetActorTransform().TransformVectorNoScale(H->mSplineData[0].LeaveTangent); }
            if (E->StickMode==EBSStickMode::Constrained)
            {
                const auto& Settings=E->GetWorld()->GetSubsystem<UBSStickSession>()->Settings;
                if (Settings.VerticalConstraint || Settings.HorizontalConstraint)
                {
                    auto ConstrainedPath=Path;
                    const double Distance=ConstrainedPath.ResolveConstraints(Settings,Target);
                    Aim=ConstrainedPath.Position(Distance);
                    if (!Settings.VerticalConstraint) Aim.Z=Target.Z;
                }
            }
        }
        FHitResult StickHit=Hit;
        const bool VirtualSurface=!Hit.bBlockingHit || !Hit.ImpactPoint.Equals(Aim,1);
        if (VirtualSurface) StickHit=FHitResult();
        StickHit.bBlockingHit=true; StickHit.Location=StickHit.ImpactPoint=Aim;
        // The free-space target acts as a horizontal virtual support surface.
        // A view-facing normal would turn poles sideways and fail floor checks.
        if (VirtualSurface) StickHit.Normal=StickHit.ImpactNormal=FVector::UpVector;
        StickHit.TraceStart=Eye; StickHit.TraceEnd=Aim;
        StickHit.Distance=E->StickDistance*100;
        Scope(Base,StickHit);
        // Native poles add their own connector height to a surface hit. Feed
        // that offset back into native routing so the connector, rather than
        // the pole base, reaches the stick tip. This retains the chosen route
        // mode and moves the actual support together with its connection.
        const int32 MovingEnd=H->mBuildStep==ESplineHologramBuildStep::SHBS_FindStart ? 0 : 1;
        if (!Snap(H,MovingEnd) && (H->mBuildStep==ESplineHologramBuildStep::SHBS_FindStart || H->mBuildStep==ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding))
        {
            for (int32 Pass=0; Pass<2 && !H->mSplineData.IsEmpty(); ++Pass)
            {
                if (Snap(H,MovingEnd)) break;
                const int32 Index=H->mBuildStep==ESplineHologramBuildStep::SHBS_FindStart ? 0 : H->mSplineData.Num()-1;
                const FVector Actual=H->GetActorTransform().TransformPosition(H->mSplineData[Index].Location);
                const FVector Correction=Aim-Actual;
                if (Correction.IsNearlyZero(.1)) break;
                StickHit.Location+=Correction; StickHit.ImpactPoint=StickHit.Location;
                StickHit.TraceEnd=StickHit.Location;
                Scope(Base,StickHit);
            }
        }
        Update(H,E,Target);
    });
    auto ValidHit=[](auto& Scope,const auto* H,const FHitResult&) {
        if (auto* E=EditFor(H); E && E->StickMode!=EBSStickMode::Off) Scope.Override(true);
    };
    SUBSCRIBE_UOBJECT_METHOD(AFGBuildableHologram,IsValidHitResult,ValidHit);
    SUBSCRIBE_UOBJECT_METHOD(AFGPipelineHologram,IsValidHitResult,ValidHit);
    SUBSCRIBE_UOBJECT_METHOD(AFGConveyorBeltHologram,IsValidHitResult,ValidHit);
    SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGHologram,ValidatePlacementAndCost,[](AFGHologram* Base,UFGInventoryComponent*) {
        if (auto* H=Cast<AFGSplineHologram>(Base); H && !Part(H)) Validate(H);
        // Remote construction can reject a valid-looking client preview. Keep
        // the server's actual reasons, including child supports, in its log.
        if (!ServerConstructionRequest || !Supported(Base) || Base->GetParentHologram() || Base->CanConstruct()) return;
        TFunction<void(AFGHologram*)> Report=[&](AFGHologram* H) {
            if (!H || H->IsDisabled()) return;
            TArray<TSubclassOf<UFGConstructDisqualifier>> Reasons; H->GetConstructDisqualifiers(Reasons);
            for (const auto& Reason:Reasons)
                UE_LOG(LogBuildingSkills,Warning,TEXT("Spline construction rejected: stick=%d root=%s part=%s reason=%s hidden=%d"),
                    ReceivedStickRoots.Contains(Base),*Base->GetClass()->GetName(),*H->GetNameWithinParentHologram().ToString(),*GetNameSafe(Reason.Get()),H->IsHidden());
            for (auto* Child:H->GetHologramChildren()) Report(Child);
        };
        Report(Base);
    });
    // Native construction messages already transport dynamic children's names,
    // recipes and geometry. Identify the chain from those names on the server;
    // no additional packet fields or preview-only state are needed there.
    SUBSCRIBE_UOBJECT_METHOD(AFGBuildableHologram,Construct,([](auto& Scope,AFGBuildableHologram* Base,TArray<AActor*>& Children,FNetConstructionID Id) {
        auto* H=Cast<AFGSplineHologram>(Base);
        if (!Supported(H)) return;
        // Dynamic construction-message children are created after the server
        // assigns the root's player. Preserve that owner for native events and
        // consumers which charge the player for additional construction.
        if (Part(H) && H->GetParentHologram()) H->SetConstructionInstigator(H->GetParentHologram()->GetConstructionInstigator());
        if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsStickTest")))
            UE_LOG(LogBuildingSkills,Display,TEXT("STICK CONSTRUCT: %s role=%d net=%d parts=%d nested=%d player=%s"),*H->GetName(),int32(H->GetLocalRole()),int32(H->GetNetMode()),States.Contains(H) ? States.FindChecked(H).Parts : 0,Constructed!=nullptr,*GetNameSafe(H->GetConstructionInstigator()));
        // Holograms are local previews and need not have an authoritative actor
        // role. The construction world, rather than that role, owns this decision.
        if (H->GetNetMode()==NM_Client) return;
        TGuardValue<APawn*> PlayerGuard(ConstructionPlayer,(StickRoot(H) || Part(H)) ? H->GetConstructionInstigator() : nullptr);
        if (Constructed) { AActor* A=Scope(Base,Children,Id); Constructed->Add(H,A); return; }
        auto* State=States.Find(H);
        TArray<AFGSplineHologram*> Extra;
        for (auto* C:H->GetHologramChildren()) if (Part(C))
            if (auto* S=Cast<AFGSplineHologram>(C)) Extra.Add(S);
        if (Extra.IsEmpty()) return;
        if ((State && State->Invalid) || Extra.Num()>255) { Scope.Override(static_cast<AActor*>(nullptr)); return; }
        Extra.Sort([](const AFGSplineHologram& A,const AFGSplineHologram& B) {
            return FCString::Atoi(*A.GetNameWithinParentHologram().ToString().RightChop(20)) < FCString::Atoi(*B.GetNameWithinParentHologram().ToString().RightChop(20));
        });
        TArray<AFGSplineHologram*> Parts{H};
        Parts.Append(Extra);
        // Keep the exact native snap targets before construction consumes the
        // holograms. The outer ends belong to different actors after splitting;
        // native construction does not reliably connect these generated rails.
        TWeakObjectPtr<UFGRailroadTrackConnectionComponent> RailTargets[2]{
            Cast<UFGRailroadTrackConnectionComponent>(Snap(Parts[0],0)),
            Cast<UFGRailroadTrackConnectionComponent>(Snap(Parts.Last(),1))};
        // PostConstructMessageDeserialization has now restored each actor's
        // construction transform. Checking positions inside archive loading
        // would compare against the receiving hologram's temporary spawn pose.
        for (int32 End=0; End<2; ++End) if (auto* Target=RailTargets[End].Get())
        {
            auto* Piece=End==0 ? Parts[0] : Parts.Last();
            if (Piece->mSplineData.IsEmpty() || !Target->GetConnectorLocation().Equals(
                Piece->GetActorTransform().TransformPosition(End==0 ? Piece->mSplineData[0].Location : Piece->mSplineData.Last().Location),1))
            {
                UE_LOG(LogBuildingSkills,Warning,TEXT("Stick construction rejected a displaced rail endpoint."));
                Scope.Override(static_cast<AActor*>(nullptr)); return;
            }
        }
        TMap<AFGSplineHologram*,AActor*> Actors;
        TGuardValue<TMap<AFGSplineHologram*,AActor*>*> Guard(Constructed,&Actors);
        AActor* Root=Scope(Base,Children,Id); Actors.Add(H,Root);
        bool Linked=Root!=nullptr;
        for (int32 I=1; I<Parts.Num(); ++I)
        {
            auto* A=Actors.FindRef(Parts[I-1]); auto* B=Actors.FindRef(Parts[I]);
            Linked &= A && B && Connect(A,B,Parts[I]->GetActorTransform().TransformPosition(Parts[I]->mSplineData[0].Location));
        }
        if (H->IsA<AFGRailroadTrackHologram>()) for (int32 End=0; End<2; ++End)
        {
            // No proximity search here: Ctrl and deliberately unsnapped ends
            // must remain disconnected, even when another rail is nearby.
            if (RailTargets[End].IsExplicitlyNull()) continue;
            auto* Target=RailTargets[End].Get();
            auto* Track=Cast<AFGBuildableRailroadTrack>(Actors.FindRef(End==0 ? Parts[0] : Parts.Last()));
            auto* Port=Track ? Track->GetConnection(End) : nullptr;
            const bool AlreadyLinked=Port && Target && Port->GetConnections().Contains(Target) && Target->GetConnections().Contains(Port);
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsStickTest")))
                UE_LOG(LogBuildingSkills,Display,TEXT("STICK EXTERNAL: end=%d nativeLinked=%d target=%s"),End,AlreadyLinked,*GetNameSafe(Target));
            if (!AlreadyLinked && IsValid(Port) && IsValid(Target) &&
                Port->GetConnectorLocation().Equals(Target->GetConnectorLocation(),.5) && Port->CanConnectTo(Target))
                Port->AddConnection(Target);
            Linked &= Port && Target && Port->GetConnections().Contains(Target) && Target->GetConnections().Contains(Port);
        }
        if (!Linked)
        {
            UE_LOG(LogBuildingSkills,Error,TEXT("Stick construction could not connect every segment; removing incomplete placement."));
            for (auto* A:Children) if (IsValid(A) && A->Implements<UFGDismantleInterface>()) IFGDismantleInterface::Execute_Dismantle(A);
            if (IsValid(Root) && Root->Implements<UFGDismantleInterface>()) IFGDismantleInterface::Execute_Dismantle(Root);
            Children.Reset(); Scope.Override(static_cast<AActor*>(nullptr));
        }
    }));
}

void FBSStick::InstallRailCompatibility()
{
    static bool Installed=false;
    if (Installed) return;
    auto* Class=FindObject<UClass>(nullptr,TEXT("/Script/AutoRailSupports.ARSClientManager"));
    if (!Class || !Class->IsChildOf(AActor::StaticClass())) return;
    Installed=true;
    // ARS reads the active rail's spline during its own Tick. Expose the full
    // route only for that read, keeping the native construction pieces intact.
    SUBSCRIBE_METHOD_VIRTUAL(AActor::Tick,Class->GetDefaultObject<AActor>(),
        [](auto& Scope,AActor* Manager,float Delta) {
            AFGSplineHologram* H=nullptr;
            for (const auto& Entry:States) if (Entry.Key.IsValid() && Entry.Key->GetWorld()==Manager->GetWorld() && Entry.Value.Parts>1 && Entry.Key->IsA<AFGRailroadTrackHologram>() && EditFor(Entry.Key.Get()))
            { H=Entry.Key.Get(); break; }
            auto* State=H ? States.Find(H) : nullptr;
            auto* Spline=H ? H->FindComponentByClass<USplineComponent>() : nullptr;
            if (!State || !Spline || State->Preview.Num()<2) return;
            FSplineCurves Original=Spline->SplineCurves;
            TWeakObjectPtr<USplineComponent> Weak=Spline;
            ON_SCOPE_EXIT { if (Weak.IsValid()) { Weak->SplineCurves=MoveTemp(Original); Weak->UpdateSpline(); } };
            Spline->ClearSplinePoints(false);
            for (int32 I=0; I<State->Preview.Num(); ++I)
            {
                const auto& P=State->Preview[I];
                Spline->AddPoint(FSplinePoint(float(I),P.Location,P.ArriveTangent,P.LeaveTangent,FRotator::ZeroRotator,FVector::OneVector,ESplinePointType::CurveCustomTangent),false);
            }
            Spline->UpdateSpline();
            Scope(Manager,Delta);
        });
    UE_LOG(LogBuildingSkills,Display,TEXT("Auto Rail Supports: full Stick preview integration active"));
}

void UBSEditModeComponent::OnStick()
{
    if (!FBSStick::Active(this) || !InputAllowed()) return;
    FBSStick::Restore(FBSStick::Active(this));
    StickMode=EBSStickMode((uint8(StickMode)+1)%3); StickStatus.Reset(); TickStick();
}
void UBSEditModeComponent::ScrollStick(int32 Delta)
{
    if (!FBSStick::Active(this) || StickMode==EBSStickMode::Off || !InputAllowed()) return;
    const auto& Settings=GetWorld()->GetSubsystem<UBSStickSession>()->Settings;
    auto* PC=Cast<APlayerController>(Cast<APawn>(GetOwner())->GetController());
    auto* Input=PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
    auto* Modifier=LoadObject<UInputAction>(nullptr,TEXT("/BuildingSkills/Inputs/IA_StickTilt.IA_StickTilt"));
    auto* Horizontal=LoadObject<UInputAction>(nullptr,TEXT("/BuildingSkills/Inputs/IA_StickHorizontalTilt.IA_StickHorizontalTilt"));
    const bool VerticalHeld=Input && Modifier && Input->GetActionValue(Modifier).Get<bool>();
    const bool HorizontalHeld=Input && Horizontal && Input->GetActionValue(Horizontal).Get<bool>();
    if (VerticalHeld)
    {
        StickTilt=FMath::Clamp(StickTilt+Delta*Settings.StickTiltStep,-90.0,90.0); StickTiltSet=true;
    }
    if (HorizontalHeld)
    {
        StickYaw=FMath::UnwindDegrees(StickYaw+Delta*Settings.StickTiltStep); StickYawSet=true;
    }
    if (!VerticalHeld && !HorizontalHeld) StickDistance=FMath::Clamp(StickDistance+Delta*Settings.StickStep,0.0,100000.0);
}
void UBSEditModeComponent::TickStick()
{
    FBSStick::InstallRailCompatibility();
    const bool Spline=FBSStick::Active(this)!=nullptr;
    if (!StickInitialized && Spline) { StickDistance=GetWorld()->GetSubsystem<UBSStickSession>()->Settings.StickDistance; StickInitialized=true; }
    if (!Spline) { StickTilt=0; StickTiltSet=false; StickYaw=0; StickYawSet=false; StickMode=EBSStickMode::Off; StickStatus.Reset(); if (StickWindow) CloseStickSettings(); }
    if (!InputSubsystem.IsValid()) return;
    auto Context=[&](TObjectPtr<UInputMappingContext>& Owned,const TCHAR* Name,bool Enable) {
        if (Enable && !Owned) if (auto* Source=LoadObject<UInputMappingContext>(nullptr,*FString::Printf(TEXT("/BuildingSkills/Inputs/MC_%s.MC_%s"),Name,Name)))
        { Owned=DuplicateObject<UInputMappingContext>(Source,this); InputSubsystem->AddMappingContext(Owned,1003); }
        if (!Enable && Owned) { InputSubsystem->RemoveMappingContext(Owned); Owned=nullptr; }
    };
    Context(StickToggleContext,TEXT("StickToggle"),Spline);
    Context(StickContext,TEXT("Stick"),Spline && StickMode!=EBSStickMode::Off);
    for (auto It=States.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    for (auto It=Starts.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    for (auto It=ReceivedStickRoots.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
}
void UBSEditModeComponent::OnStickSettings()
{
    if (!FBSStick::Active(this) || StickMode==EBSStickMode::Off || !InputAllowed()) return;
    auto* P=Cast<AFGCharacterPlayer>(GetOwner()); auto* UI=P ? P->GetGameUI() : nullptr;
    if (!UI) return;
    StickWindow=CreateWidget<UBSStickSettingsWidget>(UI->GetOwningPlayer());
    StickWindow->Draft=GetWorld()->GetSubsystem<UBSStickSession>()->Settings; StickWindow->mInteractObject=this;
    StickWindow->SetDefaultFocusWidget(StickWindow); UI->PushWidget(StickWindow); StickWindow->SetInputMode();
}
void UBSEditModeComponent::CloseStickSettings()
{
    auto* W=StickWindow.Get(); StickWindow=nullptr;
    auto* P=Cast<AFGCharacterPlayer>(GetOwner());
    if (W && P && P->GetGameUI()) P->GetGameUI()->PopWidget(W);
}

#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "FGInventoryLibrary.h"
#include "FGInventoryComponent.h"
#include "FGSplineBuildableInterface.h"
#include "EnhancedPlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Framework/Application/SlateApplication.h"
#include "FGBuildableSubsystem.h"
#include "FGPlayerController.h"
#include "BSClipboard.h"
#include "BSBlueprintScale.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/StructOnScope.h"
#include "EngineUtils.h"

void UBSStickSession::ObserveConstruction(AFGBuildable* Building)
{
    ObservedConstruction.Add(Building);
    auto* Property=FindFProperty<FObjectPropertyBase>(Building->GetClass(),TEXT("mBuildEffectInstignator"));
    auto* Payer=Property ? Property->GetObjectPropertyValue_InContainer(Building) : nullptr;
    TestConstructionPayers.Add(Cast<AFGCharacterPlayer>(Payer));
    UE_LOG(LogBuildingSkills,Display,TEXT("STICK EVENT: building=%s payer=%s property=%d"),*Building->GetName(),*GetNameSafe(Payer),Property!=nullptr);
}
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

bool FBSStick::RunChecks(AFGCharacterPlayer* Player,int32 Phase)
{
    auto* E=Player->FindComponentByClass<UBSEditModeComponent>();
    auto* PC=Cast<APlayerController>(Player->GetController());
    auto* Input=PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
    if (!E || !Input) return false;
    bool Passed=true;
    auto Check=[&](const TCHAR* Name,bool OK) { UE_LOG(LogBuildingSkills,Display,TEXT("RUNTIME %s: Stick: %s"),OK ? TEXT("PASS") : TEXT("FAIL"),Name); Passed &= OK; };
    auto Supply=[&](AFGHologram* H) {
        auto* Inventory=Player->GetInventory(); Inventory->Resize(FMath::Max(150,Inventory->GetSizeLinear()));
        for (const auto& Item:H->GetCost(true)) Inventory->AddStack(FInventoryStack(Item.Amount+100,Item.ItemClass));
    };
    TFunction<void(AFGHologram*)> Explain=[&](AFGHologram* H) {
        TArray<TSubclassOf<UFGConstructDisqualifier>> Reasons; H->GetConstructDisqualifiers(Reasons);
        for (auto Reason:Reasons) UE_LOG(LogBuildingSkills,Display,TEXT("STICK VALIDATION: %s hidden=%d reason=%s"),*H->GetName(),H->IsHidden(),*GetNameSafe(Reason.Get()));
        for (auto* C:H->GetHologramChildren()) if (!C->IsDisabled()) Explain(C);
    };
    auto Key=[&](FKey K,bool Down) { Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,K,Down ? IE_Pressed : IE_Released,Down ? 1.f : 0.f,false,0)); };
    auto Click=[](TSharedRef<SWidget> W) {
        auto& App=FSlateApplication::Get(); const auto Window=App.FindWidgetWindow(W);
        if (!Window) return false;
        const auto Point=W->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(.5,.5));
        const TSet<FKey> Up,Down={EKeys::LeftMouseButton};
        App.ProcessMouseMoveEvent(FPointerEvent(0,Point,App.GetCursorPos(),Up,EKeys::Invalid,0,FModifierKeysState()));
        const bool Pressed=App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),FPointerEvent(0,Point,Point,Down,EKeys::LeftMouseButton,0,FModifierKeysState()));
        return App.ProcessMouseButtonUpEvent(FPointerEvent(0,Point,Point,Up,EKeys::LeftMouseButton,0,FModifierKeysState())) && Pressed;
    };
    static TWeakObjectPtr<AActor> PendingEndpoint;
    static TWeakObjectPtr<UFGBlueprintDescriptor> SupportDescriptor;
    static FBox SupportRouteBounds(ForceInit);
    static TArray<TWeakObjectPtr<AActor>> ExistingSupports[3];
    static FVector ExistingSupportEnds[3][2];
    if (PendingEndpoint.IsValid())
    {
        auto* H=Active(E); auto* Existing=PendingEndpoint.Get();
        if (H)
        {
            E->StickMode=EBSStickMode::Default;
            H->SetSnapToGuideLines(false); H->UpdateHologramPlacement(FHitResult());
            auto* State=States.Find(H);
            auto* Snapped=State ? Cast<USceneComponent>(State->EndSnap.Get()) : nullptr;
            FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View);
            const FVector Tip=Eye+View.Vector()*(E->StickDistance*100);
            UE_LOG(LogBuildingSkills,Display,TEXT("STICK SNAP: %s tip=%s existing=%s collision=%d snapped=%s modifier=%d"),*H->GetClass()->GetName(),*Tip.ToString(),*Existing->GetActorLocation().ToString(),Existing->GetActorEnableCollision(),*GetNameSafe(Snapped),H->mSnapToGuideLines);
            for (auto* Port:TInlineComponentArray<UFGPipeConnectionComponentBase*>(Existing))
                UE_LOG(LogBuildingSkills,Display,TEXT("STICK SNAP PORT: pos=%s connected=%d allowed=%d"),*Port->GetComponentLocation().ToString(),Port->IsConnected(),!Port->IsSnappingToDisallowed());
            if (auto* Pipe=Cast<AFGPipelineHologram>(H))
            {
                auto* Native=UFGPipeConnectionComponentBase::FindCompatibleOverlappingConnection(Pipe->mConnectionComponents[1],Tip,nullptr,350);
                auto* Priority=UFGPipeConnectionComponentBase::FindCompatibleOverlappingConnection(Pipe->mConnectionComponents[1],Tip,Existing,350);
                UE_LOG(LogBuildingSkills,Display,TEXT("STICK QUERY: native=%s priority=%s"),*GetNameSafe(Native),*GetNameSafe(Priority));
            }
            if (auto* Belt=Cast<AFGConveyorBeltHologram>(H))
            {
                auto* Native=UFGFactoryConnectionComponent::FindCompatibleOverlappingConnections(Belt->mConnectionComponents[1],Tip,nullptr,300);
                auto* Priority=UFGFactoryConnectionComponent::FindCompatibleOverlappingConnections(Belt->mConnectionComponents[1],Tip,Existing,300);
                UE_LOG(LogBuildingSkills,Display,TEXT("STICK QUERY: native=%s priority=%s"),*GetNameSafe(Native),*GetNameSafe(Priority));
            }
            Check(TEXT("Stick preserves nearby vanilla endpoint snap"),Snapped && Snapped->GetOwner()==Existing);
            auto* EndPart=H;
            for (auto* C:H->GetHologramChildren()) if (Part(C)) EndPart=CastChecked<AFGSplineHologram>(C);
            Check(TEXT("Snapped geometry ends on the accepted native connection"),Snapped && EndPart->GetActorTransform().TransformPosition(EndPart->mSplineData.Last().Location).Equals(Snapped->GetComponentLocation(),1));
            H->SetSnapToGuideLines(true); H->UpdateHologramPlacement(FHitResult());
            Check(TEXT("Native Ctrl modifier suppresses endpoint snapping"),!States.FindChecked(H).EndSnap.IsValid());
            H->SetSnapToGuideLines(false);
            if (H->IsA<AFGRailroadTrackHologram>())
            {
                H->UpdateHologramPlacement(FHitResult());
                auto* Accepted=Cast<UFGRailroadTrackConnectionComponent>(States.FindChecked(H).EndSnap.Get());
                bool Ready=false;
                for (int32 ClickIndex=0; ClickIndex<4 && !Ready; ++ClickIndex) Ready=H->DoMultiStepPlacement(ClickIndex%2!=0);
                Check(TEXT("Snapped split rail completes native placement clicks"),Ready && Accepted);
                TArray<uint8> Bytes;
                FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Save(Writer,false);
                H->PreConstructMessageSerialization(); H->SerializeConstructMessage(Save,FNetConstructionID());
                auto* Received=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(H->GetRecipe(),Player->GetBuildGun(),H->GetActorLocation(),Player,
                    [](AFGHologram* A) { A->mShouldSpawnChildHolograms=false; }));
                FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Load(Reader,true);
                Received->SerializeConstructMessage(Load,FNetConstructionID()); Received->PostConstructMessageDeserialization();
                TArray<AActor*> Built; auto* Root=Received->Construct(Built,FNetConstructionID());
                if (Root) Built.AddUnique(Root);
                auto* Other=Accepted ? Accepted->GetConnection(0) : nullptr;
                Check(TEXT("Native snapped placement connects to existing rail after message transport"),
                    !Save.IsError() && !Load.IsError() && Root && Other && Built.Contains(Other->GetOwner()) && Other->GetConnections().Contains(Accepted));
                for (auto* A:Built) if (IsValid(A) && A->Implements<UFGDismantleInterface>()) IFGDismantleInterface::Execute_Dismantle(A);
                Received->Destroy();
            }
        }
        Existing->Destroy(); PendingEndpoint.Reset(); Player->UnequipBuildGun(); E->StickMode=EBSStickMode::Off;
    }
    static TWeakObjectPtr<AActor> TiltSource;
    if (Phase==0)
    {
        Check(TEXT("Tilt step defaults to fifteen degrees"),FBSStickSettings{}.StickTiltStep==15);
        FBSStickPath P; P.Forward=FRotator(20,0,0).Vector(); P.Radius=5000; P.Slope=FMath::Atan(.1);
        TArray<TArray<FSplinePointData>> Parts;
        Check(TEXT("Boundary transition fixture spans multiple turns"),P.Segments(35000,5600,Parts));
        const FVector First=Parts[0][0].Location, Last=Parts.Last().Last().Location;
        const FVector End=FRotator(-30,P.Derivative(35000).Rotation().Yaw,0).Vector();
        Check(TEXT("Boundary transitions split within length limits"),FBSStickPath::FitEndDirections(Parts,P.Forward,End,5600));
        Check(TEXT("Boundary transitions retain endpoint positions"),Parts[0][0].Location.Equals(First,.001) && Parts.Last().Last().Location.Equals(Last,.001));
        Check(TEXT("Boundary transitions retain starting direction"),Parts[0][0].LeaveTangent.GetSafeNormal().Equals(P.Forward,.0001));
        Check(TEXT("Boundary transitions match final tilted direction"),Parts.Last().Last().ArriveTangent.GetSafeNormal().Equals(End,.0001));
        bool Continuous=true, Lengths=true;
        for (int32 I=0; I<Parts.Num(); ++I)
        {
            auto* Curve=NewObject<USplineComponent>(); Curve->ClearSplinePoints(false);
            for (int32 J=0; J<Parts[I].Num(); ++J) { const auto& V=Parts[I][J]; Curve->AddPoint(FSplinePoint(J,V.Location,V.ArriveTangent,V.LeaveTangent,FRotator::ZeroRotator,FVector::OneVector,ESplinePointType::CurveCustomTangent),false); }
            Curve->UpdateSpline(); Lengths &= Curve->GetSplineLength()<=5600.5;
            if (I) Continuous &= Parts[I-1].Last().Location.Equals(Parts[I][0].Location,.001) && Parts[I-1].Last().ArriveTangent.GetSafeNormal().Equals(Parts[I][0].LeaveTangent.GetSafeNormal(),.0001);
        }
        Check(TEXT("Tilted pieces remain length-limited and continuous"),Continuous && Lengths);

        Key(EKeys::E,false); Key(EKeys::LeftAlt,false);
        auto R=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_Pipeline.Recipe_Pipeline_C"));
        AFGRecipeManager::Get(Player->GetWorld())->AddAvailableRecipe(R);
        Player->HotKeyRecipe(R);
    }
    if (Phase==1)
    {
        Check(TEXT("Spline exposes toggle while initially off"),Active(E) && E->StickToggleContext && !E->StickContext && E->StickMode==EBSStickMode::Off);
        Check(TEXT("Initial distance defaults to sixty-four meters"),FBSStickSettings{}.StickDistance==64 && E->StickDistance==64);
        Key(EKeys::LeftAlt,true); Key(EKeys::S,true);
    }
    if (Phase==2)
    {
        Key(EKeys::S,false); Key(EKeys::LeftAlt,false);
        Check(TEXT("Configured Alt+S enters Default"),E->StickMode==EBSStickMode::Default && E->StickContext);
        E->StickDistance=8; E->ScrollStick(1);
        Check(TEXT("One scroll adds configured step"),FMath::IsNearlyEqual(E->StickDistance,8.0+Player->GetWorld()->GetSubsystem<UBSStickSession>()->Settings.StickStep));
        E->StickDistance=0; E->ScrollStick(-1); Check(TEXT("Distance stops at zero"),E->StickDistance==0);
        E->StickDistance=8;
        Key(EKeys::K,true);
    }
    if (Phase==3)
    {
        Key(EKeys::K,false);
        Check(TEXT("K opens mouse-enabled Stick settings"),E->StickWindow && E->StickWindow->mUseMouse && !E->InputAllowed());
        E->CloseStickSettings();
        Check(TEXT("Closing settings releases input"),E->InputAllowed());
        E->OnStick(); Check(TEXT("Toggle cycles to Constrained"),E->StickMode==EBSStickMode::Constrained);
        E->OnStick(); Check(TEXT("Off releases scroll and K mapping"),E->StickMode==EBSStickMode::Off && !E->StickContext);
    }
    if ((Phase>=4 && Phase<=7) || (Phase>=16 && Phase<=19))
    {
        const bool Curved=Phase>=16;
        const TCHAR* Recipes[]={TEXT("Recipe_Pipeline"),TEXT("Recipe_PipeHyper"),TEXT("Recipe_ConveyorBeltMk1"),TEXT("Recipe_RailroadTrack")};
        const TCHAR* Name=Recipes[Phase-(Curved ? 16 : 4)];
        auto R=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/%s.%s_C"),Name,Name));
        auto* H=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(R,Player->GetBuildGun(),Player->GetActorLocation()+FVector(30000,(Phase-4)*10000,10000),Player,
            [](AFGHologram* A) { A->mShouldSpawnChildHolograms=false; }));
        Check(TEXT("Native spline recipe spawns"),H!=nullptr); if (!H) return false;
        const double Length=MaxLength(H)*2.5;
        TArray<FSplinePointData> Line{FSplinePointData(FVector::ZeroVector,FVector(Length,0,0)),FSplinePointData(FVector(Length,0,0),FVector(Length,0,0))};
        H->mBuildStep=ESplineHologramBuildStep::SHBS_AdjustPole;
        if (auto* Belt=Cast<AFGConveyorBeltHologram>(H))
        { Belt->mConnectionComponents[0]->SetDirection(EFactoryConnectionDirection::FCD_INPUT); Belt->mConnectionComponents[1]->SetDirection(EFactoryConnectionDirection::FCD_OUTPUT); }
        SetPoints(H,Line);
        if (!Curved)
        {
            auto& Settings=Player->GetWorld()->GetSubsystem<UBSStickSession>()->Settings;
            const auto SavedSettings=Settings; const auto SavedMode=E->StickMode;
            E->StickMode=EBSStickMode::Constrained;
            Settings.StickSlope=0; Settings.StickRadius=10;
            H->SetActorRotation(FRotator::ZeroRotator);
            const FVector Target=H->GetActorTransform().TransformPosition(FVector(1000,1000,500));
            for (int32 Mask=0; Mask<4; ++Mask)
            {
                Settings.VerticalConstraint=(Mask&1)!=0; Settings.HorizontalConstraint=(Mask&2)!=0;
                SetPoints(H,Line); Update(H,E,Target);
                const auto* State=States.Find(H);
                const FVector Expected=Mask==0 ? Line.Last().Location : FVector(1000,1000,Settings.VerticalConstraint ? 0 : 500);
                Check(TEXT("Constraint combination preserves its native hologram endpoint"),State && !State->Invalid && !State->Preview.IsEmpty() && State->Preview.Last().Location.Equals(Expected,.05));
                Check(TEXT("Constraint combination preserves start continuity"),State && !State->Preview.IsEmpty() && State->Preview[0].LeaveTangent.GetSafeNormal().Equals(FVector::ForwardVector,.001));
                Restore(H,true);
            }
            Settings=SavedSettings; E->StickMode=SavedMode; SetPoints(H,Line);
        }
        TArray<TArray<FSplinePointData>> Segments;
        if (Curved)
        {
            FBSStickPath Path; Path.Radius=MaxLength(H)*.6; Path.Slope=FMath::Atan(.1);
            Check(TEXT("Multi-turn constrained helix creates segments"),Path.Segments(Path.Radius*4.5*UE_DOUBLE_PI,MaxLength(H),Segments) && Segments.Num()>4);
            const FVector StartDirection=FRotator(5,Segments[0][0].LeaveTangent.Rotation().Yaw,0).Vector();
            const FVector EndDirection=FRotator(-5,Segments.Last().Last().ArriveTangent.Rotation().Yaw,0).Vector();
            Check(TEXT("Curved transport fixture fits boundary directions"),FBSStickPath::FitEndDirections(Segments,StartDirection,EndDirection,MaxLength(H)));
        }
        else Check(TEXT("Long native route divides into three pieces"),FBSStickPath::Split(Line,MaxLength(H),Segments) && Segments.Num()==3);
        TArray<AFGBuildableRailroadTrack*> ExternalRails;
        UFGRailroadTrackConnectionComponent* ExternalPorts[2]{};
        if (Curved && H->IsA<AFGRailroadTrackHologram>()) H->SetActorRotation(FRotator(0,37,0));
        if (H->IsA<AFGRailroadTrackHologram>()) for (int32 End=0; End<2; ++End)
        {
            const auto& Boundary=End==0 ? Segments[0][0] : Segments.Last().Last();
            const FVector Direction=(End==0 ? Boundary.LeaveTangent : Boundary.ArriveTangent).GetSafeNormal();
            const FVector Delta=Direction*(MaxLength(H)*.4);
            const FVector Start=End==0 ? Boundary.Location-Delta : Boundary.Location;
            auto* Existing=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(R,Player->GetBuildGun(),H->GetActorLocation(),Player,
                [](AFGHologram* A) { A->mShouldSpawnChildHolograms=false; }));
            Existing->SetActorTransform(H->GetActorTransform());
            Existing->SetSuppressBuildEffect(true);
            Existing->mBuildStep=ESplineHologramBuildStep::SHBS_AdjustPole;
            SetPoints(Existing,{FSplinePointData(Start,Delta),FSplinePointData(Start+Delta,Delta)});
            TArray<AActor*> Ignored;
            auto* Track=Cast<AFGBuildableRailroadTrack>(Existing->Construct(Ignored,FNetConstructionID()));
            Check(TEXT("Existing boundary rail fixture constructs"),Track!=nullptr);
            if (Track)
            {
                ExternalRails.Add(Track); ExternalPorts[End]=Track->GetConnection(1-End);
                Snap(H,End,ExternalPorts[End]);
            }
            Existing->Destroy();
        }
        Check(TEXT("Split has native child holograms"),ApplySegments(H,Segments) && H->GetHologramChildren().Num()==Segments.Num()-1);
        auto Cost=H->GetCost(true);
        auto Sum=H->GetCost(false);
        for (auto* Child:H->GetHologramChildren()) Sum.Append(Child->GetCost(true));
        UFGInventoryLibrary::ConsolidateItemsAmount(Cost); UFGInventoryLibrary::ConsolidateItemsAmount(Sum);
        bool CorrectCost=Cost.Num()==Sum.Num() && !Cost.IsEmpty();
        for (const auto& Item:Sum) CorrectCost &= Cost.ContainsByPredicate([&](const FItemAmount& Other) { return Item.ItemClass==Other.ItemClass && Item.Amount==Other.Amount; });
        Check(TEXT("Native total cost includes every segment"),CorrectCost);
        H->SetActorHiddenInGame(false); Supply(H); H->ResetConstructDisqualifiers(); H->ValidatePlacementAndCost(Player->GetInventory());
        bool PartsValid=true; for (auto* C:H->GetHologramChildren()) if (Part(C)) PartsValid &= C->CanConstruct();
        if (!PartsValid) Explain(H);
        Check(TEXT("Generated segments pass native placement validation"),PartsValid);
        Check(TEXT("Bare root retains native start-support requirement"),H->CanConstruct()==H->IsA<AFGRailroadTrackHologram>());
        // Roundtrip into another native hologram before constructing, as on the
        // receiving server. Object references use names in this local fixture.
        TArray<uint8> Bytes;
        FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Save(Writer,false);
        if (!ExternalRails.IsEmpty())
        {
            // Exercise the independent actor/index transport with missing
            // native component references, as opposed to merely round-tripping
            // references which already work in a standalone world.
            Snap(H,0,nullptr);
            auto* Last=Cast<AFGSplineHologram>(H->FindChildHologramByName(FName(*FString::Printf(TEXT("BuildingSkillsStick_%d"),Segments.Num()-1))));
            Snap(Last,1,nullptr);
        }
        H->PreConstructMessageSerialization(); H->SerializeConstructMessage(Save,FNetConstructionID());
        auto* Received=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(R,Player->GetBuildGun(),FVector::ZeroVector,Player,
            [](AFGHologram* A) { A->mShouldSpawnChildHolograms=false; }));
        FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Load(Reader,true);
        Received->ResetConstructDisqualifiers();
        Received->SerializeConstructMessage(Load,FNetConstructionID()); Received->PostConstructMessageDeserialization();
        Check(TEXT("Construction message restores split children"),!Save.IsError() && !Load.IsError() && Reader.Tell()==Bytes.Num() && Received->GetHologramChildren().Num()==Segments.Num()-1);
        if (!ExternalRails.IsEmpty())
        {
            auto* Last=Cast<AFGSplineHologram>(Received->FindChildHologramByName(FName(*FString::Printf(TEXT("BuildingSkillsStick_%d"),Segments.Num()-1))));
            Check(TEXT("Construction message preserves both external rail snap targets"),Last && Snap(Received,0)==ExternalPorts[0] && Snap(Last,1)==ExternalPorts[1]);
        }
        if (Curved)
        {
            AFGSplineHologram* Last=Received;
            for (auto* Child:Received->GetHologramChildren()) if (auto* Spline=Cast<AFGSplineHologram>(Child); Spline && Part(Spline)) Last=Spline;
            Check(TEXT("Construction message preserves boundary tilts"),
                Received->mSplineData[0].LeaveTangent.GetSafeNormal().Equals(Segments[0][0].LeaveTangent.GetSafeNormal(),.001) &&
                Last->mSplineData.Last().ArriveTangent.GetSafeNormal().Equals(Segments.Last().Last().ArriveTangent.GetSafeNormal(),.001));
        }
        Received->SetActorHiddenInGame(false); Received->ValidatePlacementAndCost(Player->GetInventory());
        PartsValid=true; for (auto* C:Received->GetHologramChildren()) if (Part(C)) PartsValid &= C->CanConstruct();
        if (!PartsValid) Explain(Received);
        Check(TEXT("Received segments pass native placement validation"),PartsValid);
        TArray<AActor*> Children;
        auto* Built=Received->Construct(Children,FNetConstructionID());
        Check(TEXT("Native construction returns complete split path"),Built && Children.Num()==Segments.Num()-1);
        TArray<AActor*> Actors=Children; if (Built) Actors.AddUnique(Built);
        int32 Connections=0;
        for (auto* A:Actors) if (auto* S=Cast<IFGSplineBuildableInterface>(A))
        {
            Check(TEXT("Each constructed segment respects length limit"),S->GetSplineComponent()->GetSplineLength()<=MaxLength(H)+1);
            for (auto* P:TInlineComponentArray<UFGPipeConnectionComponentBase*>(A)) if (P->GetConnection()) ++Connections;
            for (auto* P:TInlineComponentArray<UFGFactoryConnectionComponent*>(A)) if (P->GetConnection()) ++Connections;
            for (auto* P:TInlineComponentArray<UFGRailroadTrackConnectionComponent*>(A)) Connections+=P->GetConnections().Num();
        }
        UE_LOG(LogBuildingSkills,Display,TEXT("STICK CONNECTIONS: recipe=%s actors=%d links=%d parts=%d"),Name,Actors.Num(),Connections,States.Contains(Received) ? States.FindChecked(Received).Parts : 0);
        Check(TEXT("All internal joins connect reciprocally"),Connections==2*(Segments.Num()-1)+ExternalRails.Num());
        for (int32 End=0; End<2; ++End) if (ExternalPorts[End])
        {
            auto* Port=ExternalPorts[End];
            auto* Other=Port->GetConnection(0);
            Check(TEXT("Split route connects reciprocally to existing boundary rail"),
                Port->GetConnections().Num()==1 && Other && Actors.Contains(Other->GetOwner()) && Other->GetConnections().Contains(Port));
        }
        for (auto* A:Actors) if (IsValid(A) && A->Implements<UFGDismantleInterface>()) IFGDismantleInterface::Execute_Dismantle(A);
        if (!ExternalRails.IsEmpty())
        {
            States.FindChecked(H).StartSnap.Reset(); States.FindChecked(H).EndSnap.Reset();
            TArray<uint8> FreeBytes;
            FMemoryWriter FreeWriter(FreeBytes); FObjectAndNameAsStringProxyArchive FreeSave(FreeWriter,false);
            H->SerializeConstructMessage(FreeSave,FNetConstructionID());
            auto* Free=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(R,Player->GetBuildGun(),H->GetActorLocation(),Player,
                [](AFGHologram* A) { A->mShouldSpawnChildHolograms=false; }));
            FMemoryReader FreeReader(FreeBytes); FObjectAndNameAsStringProxyArchive FreeLoad(FreeReader,true);
            Free->SerializeConstructMessage(FreeLoad,FNetConstructionID()); Free->PostConstructMessageDeserialization();
            auto* FreeLast=Cast<AFGSplineHologram>(Free->FindChildHologramByName(FName(*FString::Printf(TEXT("BuildingSkillsStick_%d"),Segments.Num()-1))));
            Check(TEXT("Construction message preserves deliberately unsnapped rail endpoints"),
                !FreeSave.IsError() && !FreeLoad.IsError() && FreeLast && !Snap(Free,0) && !Snap(FreeLast,1));
            Free->Destroy();
        }
        for (auto* Rail:ExternalRails)
        {
            Check(TEXT("Removing split route disconnects existing rail cleanly"),!Rail->GetConnection(0)->IsConnected() && !Rail->GetConnection(1)->IsConnected());
            IFGDismantleInterface::Execute_Dismantle(Rail);
        }
        Restore(H); Check(TEXT("Leaving Stick restores the unsplit endpoint"),H->mSplineData.Num()==2 && H->mSplineData.Last().Location.Equals(Line.Last().Location,.01) && H->GetHologramChildren().IsEmpty());
        H->Destroy(); Received->Destroy();
    }
    if (Phase==8)
    {
        auto* H=Active(E);
        Check(TEXT("Active spline retained after settings and fixture construction"),H!=nullptr);
        if (H)
        {
            const FVector Start(123,456,789);
            const double L=MaxLength(H)*2;
            TArray<FSplinePointData> Line{FSplinePointData(Start,FVector(L,0,0)),FSplinePointData(Start+FVector(L,0,0),FVector(L,0,0))};
            H->mBuildStep=ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding;
            H->mActivePointIdx=1; SetPoints(H,Line);
            E->OnStick(); Update(H,E,H->GetActorTransform().TransformPosition(Line.Last().Location));
            E->OnStick(); E->OnStick();
            Check(TEXT("Cycling Stick after the start preserves the start and native build step"),E->StickMode==EBSStickMode::Off && H->GetCurrentBuildStep()==ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding && H->mSplineData[0].Location.Equals(Start,.001));
        }
        Player->UnequipBuildGun();
    }
    if (Phase==9)
    {
        Player->HotKeyRecipe(LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_Pipeline.Recipe_Pipeline_C")));
    }
    if (Phase==10) { Key(EKeys::LeftAlt,true); Key(EKeys::S,true); }
    if (Phase==11)
    {
        Key(EKeys::S,false); Key(EKeys::LeftAlt,false);
        E->StickDistance=8; Key(EKeys::MouseScrollUp,true);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsStickHints.png"),true,false);
    }
    if (Phase==12)
    {
        Key(EKeys::MouseScrollUp,false);
        Check(TEXT("Physical wheel drives the configured distance action"),FMath::IsNearlyEqual(E->StickDistance,8.0+Player->GetWorld()->GetSubsystem<UBSStickSession>()->Settings.StickStep));
        Check(TEXT("Bottom hints expose toggle, wheel, both tilt axes and K"),E->GetControlHints().Num()==6);
        Key(EKeys::K,true);
    }
    if (Phase==13)
    {
        Key(EKeys::K,false);
        Check(TEXT("Settings show native mouse cursor"),E->StickWindow && PC->bShowMouseCursor);
        if (E->StickWindow && !FParse::Param(FCommandLine::Get(),TEXT("nullrhi")))
        {
            TArray<TSharedRef<SEditableText>> Fields;
            TArray<TSharedRef<SCheckBox>> Toggles;
            TFunction<void(TSharedRef<SWidget>)> Find=[&](TSharedRef<SWidget> W) {
                if (W->GetTypeAsString()==TEXT("SEditableText")) Fields.Add(StaticCastSharedRef<SEditableText>(W));
                if (W->GetTypeAsString()==TEXT("SCheckBox")) Toggles.Add(StaticCastSharedRef<SCheckBox>(W));
                auto* C=W->GetChildren(); for (int32 I=0; I<C->Num(); ++I) Find(C->GetChildAt(I));
            };
            Find(E->StickWindow->TakeWidget()); Check(TEXT("All five numeric controls are present"),Fields.Num()==5);
            if (Fields.Num()==5)
            {
                const auto A=Fields[1]->GetCachedGeometry().GetAbsolutePosition();
                const auto B=Fields[2]->GetCachedGeometry().GetAbsolutePosition();
                Check(TEXT("Equivalent slope fields share a row"),FMath::Abs(A.Y-B.Y)<2 && B.X>A.X);
                for (const int32 Index:{0,2,3,4})
                {
                    Check(TEXT("Numeric control accepts mouse focus"),Click(Fields[Index]) && Fields[Index]->HasKeyboardFocus());
                    Fields[Index]->SelectAllText();
                    for (TCHAR Ch:FString(Index==0 ? TEXT("2.5") : Index==2 ? TEXT("10") : Index==3 ? TEXT("60") : TEXT("7.5")))
                        FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Ch,FModifierKeysState(),0,false));
                    FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
                }
            }
            Check(TEXT("Typed percent updates linked degrees and radius stays independent"),FMath::IsNearlyEqual(E->StickWindow->Draft.StickSlope,float(FMath::RadiansToDegrees(FMath::Atan(.1))),.001f) && E->StickWindow->Draft.StickRadius==60 && E->StickWindow->Draft.StickStep==2.5f);
            Check(TEXT("K offers separate vertical and horizontal switches"),Toggles.Num()==2);
            if (Toggles.Num()==2)
            {
                const float Slope=E->StickWindow->Draft.StickSlope;
                Check(TEXT("Mouse disables vertical constraint independently"),Click(Toggles[0]) && !E->StickWindow->Draft.VerticalConstraint && E->StickWindow->Draft.HorizontalConstraint);
                Check(TEXT("Mouse disables horizontal constraint independently"),Click(Toggles[1]) && !E->StickWindow->Draft.HorizontalConstraint);
                Check(TEXT("Disabling constraints retains their numeric values"),E->StickWindow->Draft.StickSlope==Slope && E->StickWindow->Draft.StickRadius==60);
            }
        }
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsStickSettings.png"),true,false);
    }
    if (Phase==14)
    {
        if (E->StickWindow && !FParse::Param(FCommandLine::Get(),TEXT("nullrhi")))
        {
            TArray<TSharedRef<SWidget>> Buttons;
            TFunction<void(TSharedRef<SWidget>)> Find=[&](TSharedRef<SWidget> W) {
                if (W->GetTypeAsString()==TEXT("SButton")) Buttons.Add(W);
                auto* C=W->GetChildren(); for (int32 I=0; I<C->Num(); ++I) Find(C->GetChildAt(I));
            };
            Find(E->StickWindow->TakeWidget());
            Check(TEXT("Mouse Apply closes settings"),Buttons.Num()==2 && Click(Buttons.Last()) && !E->StickWindow);
            const auto& Settings=Player->GetWorld()->GetSubsystem<UBSStickSession>()->Settings;
            Check(TEXT("Apply stores temporary session values"),Settings.StickStep==2.5f && Settings.StickRadius==60 && Settings.StickTiltStep==7.5f && !Settings.VerticalConstraint && !Settings.HorizontalConstraint);
        }
        if (E->StickWindow) E->CloseStickSettings();
        E->OnStickSettings();
    }
    if (Phase==15)
    {
        auto& Settings=Player->GetWorld()->GetSubsystem<UBSStickSession>()->Settings;
        const bool VerticalBefore=Settings.VerticalConstraint, HorizontalBefore=Settings.HorizontalConstraint;
        Check(TEXT("Reopening K retains applied constraint switches"),E->StickWindow && E->StickWindow->Draft.VerticalConstraint==VerticalBefore && E->StickWindow->Draft.HorizontalConstraint==HorizontalBefore);
        if (E->StickWindow) E->StickWindow->Draft.VerticalConstraint=!VerticalBefore;
        Player->GetGameUI()->Native_HandlePauseGamePressed();
        Check(TEXT("Cancel discards constraint switch changes"),Settings.VerticalConstraint==VerticalBefore && Settings.HorizontalConstraint==HorizontalBefore);
        Settings.VerticalConstraint=true; Settings.HorizontalConstraint=true;
        Check(TEXT("Escape closes Stick settings and restores input"),!E->StickWindow && E->InputAllowed());
        Player->UnequipBuildGun();
    }
    if (Phase==16)
    {
        FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View);
        const FVector Tip=Eye+View.Vector()*800, Forward=FRotator(20,View.Yaw,0).Vector();
        auto Recipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_Pipeline.Recipe_Pipeline_C"));
        auto* Source=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(Recipe,Player->GetBuildGun(),Tip-Forward*2000,Player,
            [](AFGHologram* H) { H->mShouldSpawnChildHolograms=false; }));
        Source->mBuildStep=ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding;
        SetPoints(Source,{FSplinePointData(FVector::ZeroVector,Forward*2000),FSplinePointData(Forward*2000,Forward*2000)});
        Source->SetSuppressBuildEffect(true); TArray<AActor*> Children;
        TiltSource=Source->Construct(Children,FNetConstructionID()); Source->Destroy();
        Player->HotKeyRecipe(Recipe);
        E->StickMode=EBSStickMode::Default; E->TickStick();
    }
    if (Phase==17)
    {
        E->StickMode=EBSStickMode::Default; E->StickDistance=8;
        auto* H=Active(E); H->UpdateHologramPlacement(FHitResult());
        Check(TEXT("Tilt fixture snaps its start to an inclined real pipe"),Snap(H,0) && Cast<USceneComponent>(Snap(H,0))->GetOwner()==TiltSource.Get());
        for (int32 I=0; I<6 && H->mBuildStep!=ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding; ++I) H->DoMultiStepPlacement(I%2!=0);
        E->StickMode=EBSStickMode::Constrained; E->StickDistance=64;
        H->UpdateHologramPlacement(FHitResult());
        Key(EKeys::LeftAlt,true);
    }
    if (Phase==18) Key(EKeys::MouseScrollUp,true);
    if (Phase==19)
    {
        Key(EKeys::MouseScrollUp,false); Key(EKeys::LeftAlt,false);
        UE_LOG(LogBuildingSkills,Display,TEXT("STICK TILT INPUT: set=%d tilt=%g distance=%g"),E->StickTiltSet,E->StickTilt,E->StickDistance);
        Check(TEXT("Alt wheel changes tilt by the temporary step without changing distance"),E->StickTiltSet && FMath::IsNearlyEqual(E->StickTilt,7.5) && E->StickDistance==64);
        auto* H=Active(E); H->UpdateHologramPlacement(FHitResult());
        auto* Last=H; for (auto* C:H->GetHologramChildren()) if (Part(C)) Last=CastChecked<AFGSplineHologram>(C);
        auto* Port=Cast<USceneComponent>(Snap(H,0));
        const FVector StartDirection=H->GetActorTransform().TransformVectorNoScale(H->mSplineData[0].LeaveTangent).GetSafeNormal();
        const FVector EndDirection=Last->GetActorTransform().TransformVectorNoScale(Last->mSplineData.Last().ArriveTangent).GetSafeNormal();
        Check(TEXT("Constrained first piece continues the real starting connector"),Port && StartDirection.Equals(Port->GetForwardVector(),.001));
        Check(TEXT("Constrained final piece reaches the selected endpoint pitch"),FMath::IsNearlyEqual(EndDirection.Rotation().Pitch,7.5,.01));
        Player->UnequipBuildGun(); E->TickStick();
        Check(TEXT("Leaving the spline tool resets its endpoint tilt"),!E->StickTiltSet && E->StickTilt==0);
        if (TiltSource.IsValid()) TiltSource->Destroy(); TiltSource.Reset();
    }
    if (Phase>=20 && Phase<=27)
    {
        const TCHAR* Names[]={TEXT("Recipe_Pipeline"),TEXT("Recipe_PipeHyper"),TEXT("Recipe_ConveyorBeltMk1"),TEXT("Recipe_RailroadTrack")};
        if (Phase%2==0)
        {
            if (Phase==20) Player->AddActorWorldOffset(FVector(0,0,30000));
            PC->SetControlRotation(FRotator::ZeroRotator);
            const TCHAR* Name=Names[(Phase-20)/2];
            auto R=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/%s.%s_C"),Name,Name));
            AFGRecipeManager::Get(Player->GetWorld())->AddAvailableRecipe(R); Player->HotKeyRecipe(R);
        }
        else if (auto* H=Active(E))
        {
            E->OnStick(); E->StickDistance=8;
            PC->SetControlRotation(FRotator::ZeroRotator);
            H->UpdateHologramPlacement(FHitResult());
            for (int32 I=0; I<6 && (H->mBuildStep==ESplineHologramBuildStep::SHBS_FindStart || H->mBuildStep==ESplineHologramBuildStep::SHBS_AdjustStartingPole); ++I)
                H->DoMultiStepPlacement(I%2!=0);
            Check(TEXT("Native build steps place the starting endpoint"),H->mBuildStep==ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding);
            const FVector Start=H->GetActorTransform().TransformPosition(H->mSplineData[0].Location);
            E->StickDistance=8+MaxLength(H)*2.5/100;
            FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View);
            const FVector Target=Eye+View.Vector()*(E->StickDistance*100);
            Starts.Remove(H);
            H->UpdateHologramPlacement(FHitResult());
            auto* State=States.Find(H);
            FVector End=State && !State->Unsplit.IsEmpty() ? H->GetActorTransform().TransformPosition(State->Unsplit.Last().Location) : FVector::ZeroVector;
            UE_LOG(LogBuildingSkills,Display,TEXT("STICK TIP: %s default error=%g"),*H->GetClass()->GetName(),FVector::Distance(End,Target));
            Check(TEXT("Native Default route reaches camera stick tip"),End.Equals(Target,1));
            Check(TEXT("Native Default route retains placed start"),State && !State->Unsplit.IsEmpty() && H->GetActorTransform().TransformPosition(State->Unsplit[0].Location).Equals(Start,1));
            Supply(H); H->ResetConstructDisqualifiers(); H->ValidatePlacementAndCost(Player->GetInventory());
            if (!H->CanConstruct()) Explain(H);
            Check(TEXT("Long native path with floating supports is buildable"),H->CanConstruct());
            TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Save(Writer,false);
            H->PreConstructMessageSerialization(); H->SerializeConstructMessage(Save,FNetConstructionID());
            auto* Received=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(H->GetRecipe(),Player->GetBuildGun(),H->GetActorLocation(),Player));
            FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Load(Reader,true);
            Received->ResetConstructDisqualifiers();
            Received->SerializeConstructMessage(Load,FNetConstructionID()); Received->PostConstructMessageDeserialization();
            Received->SetActorHiddenInGame(false); Received->ValidatePlacementAndCost(Player->GetInventory());
            Check(TEXT("Construction message carries Stick support permission"),!Save.IsError() && !Load.IsError() && Reader.Tell()==Bytes.Num() && ReceivedStickRoots.Contains(Received));
            if (!Received->CanConstruct()) Explain(Received);
            Check(TEXT("Received native path with supports is buildable"),Received->CanConstruct());
            Received->Destroy();
            E->OnStick();
            auto& Settings=Player->GetWorld()->GetSubsystem<UBSStickSession>()->Settings;
            // Free supports may face back toward the player. A finite radius
            // gives those starts a valid forward-running turn toward the aim.
            Settings.StickSlope=FMath::RadiansToDegrees(FMath::Atan(.1)); Settings.StickRadius=120;
            const FVector Forward=H->GetActorTransform().TransformVectorNoScale(H->mSplineData[0].LeaveTangent);
            Starts.Remove(H);
            H->UpdateHologramPlacement(FHitResult());
            auto* Last=H;
            for (int32 I=1; I<256; ++I)
            {
                auto* C=Cast<AFGSplineHologram>(H->FindChildHologramByName(FName(*FString::Printf(TEXT("BuildingSkillsStick_%d"),I))));
                if (!C) break; Last=C;
            }
            Check(TEXT("Constrained fixture has a valid nonzero split path"),States.Contains(H) && !States.FindChecked(H).Invalid && States.FindChecked(H).Parts>1);
            FBSStickPath Expected; Expected.Start=Start; Expected.Forward=Forward; Expected.Slope=FMath::Atan(.1); Expected.Radius=12000;
            End=Last->GetActorTransform().TransformPosition(Last->mSplineData.Last().Location);
            Check(TEXT("Native Constrained preview reaches projected tip"),End.Equals(Expected.Position(Expected.Closest(Target)),1));
            E->StickTiltSet=true; E->StickTilt=-15;
            H->UpdateHologramPlacement(FHitResult());
            auto* TiltedEnd=H; for (auto* C:H->GetHologramChildren()) if (Part(C)) TiltedEnd=CastChecked<AFGSplineHologram>(C);
            UE_LOG(LogBuildingSkills,Display,TEXT("STICK TILT END: type=%s pitch=%g snap=%s last=%s parts=%d invalid=%d"),*H->GetClass()->GetName(),TiltedEnd->GetActorTransform().TransformVectorNoScale(TiltedEnd->mSplineData.Last().ArriveTangent).Rotation().Pitch,*GetPathNameSafe(Snap(H,1)),*TiltedEnd->GetNameWithinParentHologram().ToString(),States.FindChecked(H).Parts,States.FindChecked(H).Invalid);
            Check(TEXT("Each constrained spline family honors endpoint tilt"),FMath::IsNearlyEqual(TiltedEnd->GetActorTransform().TransformVectorNoScale(TiltedEnd->mSplineData.Last().ArriveTangent).Rotation().Pitch,-15.0,.01));
            const double NativeYaw=TiltedEnd->GetActorTransform().TransformVectorNoScale(TiltedEnd->mSplineData.Last().ArriveTangent).Rotation().Yaw;
            E->StickYawSet=true; E->StickYaw=30; H->UpdateHologramPlacement(FHitResult());
            TiltedEnd=H; for (auto* C:H->GetHologramChildren()) if (Part(C)) TiltedEnd=CastChecked<AFGSplineHologram>(C);
            const auto TurnedDirection=TiltedEnd->GetActorTransform().TransformVectorNoScale(TiltedEnd->mSplineData.Last().ArriveTangent).Rotation();
            Check(TEXT("Each constrained spline family combines horizontal and vertical endpoint tilt"),
                FMath::Abs(FMath::FindDeltaAngleDegrees(NativeYaw,TurnedDirection.Yaw)-30)<.01 && FMath::IsNearlyEqual(TurnedDirection.Pitch,-15.0,.01));
            E->StickYawSet=false; E->StickYaw=0;
            E->StickMode=EBSStickMode::Default; H->UpdateHologramPlacement(FHitResult());
            TiltedEnd=H; for (auto* C:H->GetHologramChildren()) if (Part(C)) TiltedEnd=CastChecked<AFGSplineHologram>(C);
            Check(TEXT("Each default spline family honors endpoint tilt"),FMath::IsNearlyEqual(TiltedEnd->GetActorTransform().TransformVectorNoScale(TiltedEnd->mSplineData.Last().ArriveTangent).Rotation().Pitch,-15.0,.01));
            E->StickTiltSet=false; E->StickTilt=0; E->StickMode=EBSStickMode::Constrained;
            E->OnStick();
            H->ResetConstructDisqualifiers(); H->ValidatePlacementAndCost(Player->GetInventory());
            bool HasPoles=false,RejectsFloating=false;
            for (auto* Child:H->GetHologramChildren()) if (Child->IsA<AFGPoleHologram>() && !Child->IsDisabled())
            {
                HasPoles=true; TArray<TSubclassOf<UFGConstructDisqualifier>> Reasons;
                Child->GetConstructDisqualifiers(Reasons); RejectsFloating |= Reasons.Contains(UFGCDInvalidFloor::StaticClass());
            }
            if (HasPoles) Check(TEXT("Turning Stick off restores native ground checks"),RejectsFloating);
            auto* ExistingH=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(H->GetRecipe(),Player->GetBuildGun(),Target+FVector(0,20,0),Player,
                [](AFGHologram* A) { A->mShouldSpawnChildHolograms=false; }));
            TArray<FSplinePointData> External; External.SetNum(2);
            External[1].Location=FVector(2000,0,0);
            for (auto& Point:External) Point.ArriveTangent=Point.LeaveTangent=FVector(2000,0,0);
            ExistingH->SetSuppressBuildEffect(true);
            ExistingH->mBuildStep=ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding; SetPoints(ExistingH,External);
            TArray<AActor*> ExternalChildren; auto* Existing=ExistingH->Construct(ExternalChildren,FNetConstructionID());
            PendingEndpoint=Existing; ExistingH->Destroy();
            // Keep the established long route, but walk near its destination so
            // native significance enables endpoint collision before the next tick.
            Player->AddActorWorldOffset(View.Vector()*(E->StickDistance*100-800));
            E->StickDistance=8;
        }
        else Check(TEXT("Native endpoint fixture equips spline"),false);
    }
    const bool DoublePillars=FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsARSDoubleTest"));
    FString PresetDirectory; const bool ExactPreset=FParse::Value(FCommandLine::Get(),TEXT("BuildingSkillsARSPreset="),PresetDirectory);
    auto* Session=Player->GetWorld()->GetSubsystem<UBSStickSession>();
    if (Phase==28)
    {
        auto* ARSClass=FindObject<UClass>(nullptr,TEXT("/Script/AutoRailSupports.ARSRemoteCallObject"));
        auto* ARS=ARSClass ? Cast<AFGPlayerController>(PC)->GetRemoteCallObjectOfClass(ARSClass) : nullptr;
        Check(TEXT("Optional ARS integration fixture is available"),ARS!=nullptr);
        if (ARS)
        {
            auto* RCO=Cast<AFGPlayerController>(PC)->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>();
            auto Recipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_StorageContainerMk1.Recipe_StorageContainerMk1_C"));
            AFGRecipeManager::Get(Player->GetWorld())->AddAvailableRecipe(Recipe);
            auto* SourceH=AFGHologram::SpawnHologramFromRecipe(Recipe,Player->GetBuildGun(),Player->GetActorLocation()+FVector(0,0,-5000),Player);
            TArray<AActor*> Children; auto* Source=Cast<AFGBuildable>(SourceH->Construct(Children,FNetConstructionID()));
            static FBlueprintSaveData SupportData;
            static FString SupportName;
            SupportName=TEXT("BuildingSkillsRuntimeSupport_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
            FBlueprintRecord Record; Record.BlueprintName=SupportName;
            const bool Captured=Source && WriteBuildingSkillsClipboard(AFGBlueprintSubsystem::Get(Player->GetWorld()),Record,Source->GetActorTransform(),{Source},FIntVector(8,8,8),SupportData);
            Supply(SourceH);
            // Provision both the fifty deck tiles and the sparse pillar stamps.
            for (const auto& Item:SourceH->GetCost(true)) Player->GetInventory()->AddStack(FInventoryStack(Item.Amount*80+100,Item.ItemClass));
            Check(TEXT("ARS test support blueprint captured in memory"),Captured);
            if (Captured)
            {
                auto* System=AFGBlueprintSubsystem::Get(Player->GetWorld());
                System->mStoredBlueprintData=SupportData;
                const bool Written=System->WriteBlueprintToDisk(Record);
                Check(TEXT("Temporary support blueprint uses native disk format"),Written);
                System->EnumerateBlueprints();
                SupportDescriptor=System->CreateBlueprintDescriptor(Record,SupportData.BlueprintHeader,false);
                auto* Function=ARS->FindFunction(TEXT("Server_SetSettings"));
                auto* Parameter=Function ? FindFProperty<FStructProperty>(Function,TEXT("NewSettings")) : nullptr;
                Check(TEXT("ARS exposes its settings RPC"),Parameter!=nullptr);
                if (Parameter)
                {
                    FStructOnScope Args(Function); void* Data=Parameter->ContainerPtrToValuePtr<void>(Args.GetStructMemory());
                    FindFProperty<FStrProperty>(Parameter->Struct,TEXT("UnderRailBlueprintName"))->SetPropertyValue_InContainer(Data,SupportName);
                    FindFProperty<FFloatProperty>(Parameter->Struct,TEXT("UnderRailLength"))->SetPropertyValue_InContainer(Data,800.f);
                    FindFProperty<FIntProperty>(Parameter->Struct,TEXT("Version"))->SetPropertyValue_InContainer(Data,1);
                    FindFProperty<FBoolProperty>(Parameter->Struct,TEXT("bDoubleRails"))->SetPropertyValue_InContainer(Data,DoublePillars);
                    FindFProperty<FBoolProperty>(Parameter->Struct,TEXT("bCombineIntoOneBlueprint"))->SetPropertyValue_InContainer(Data,DoublePillars);
                    auto* Side=FindFProperty<FEnumProperty>(Parameter->Struct,TEXT("UnderRailSideMode"));
                    if (Side) Side->GetUnderlyingProperty()->SetIntPropertyValue(Side->ContainerPtrToValuePtr<void>(Data),int64(0));
                    auto* Slots=FindFProperty<FArrayProperty>(Parameter->Struct,TEXT("PillarSlots"));
                    auto* SlotType=Slots ? CastField<FStructProperty>(Slots->Inner) : nullptr;
                    Check(TEXT("ARS exposes its every-N pillar settings"),SlotType!=nullptr);
                    if (SlotType)
                    {
                        FScriptArrayHelper List(Slots,Slots->ContainerPtrToValuePtr<void>(Data));
                        for (int32 I=0; I<(DoublePillars ? 2 : 1); ++I)
                        {
                            List.AddValue(); void* Slot=List.GetRawPtr(I);
                            FindFProperty<FStrProperty>(SlotType->Struct,TEXT("BlueprintName"))->SetPropertyValue_InContainer(Slot,SupportName);
                            FindFProperty<FIntProperty>(SlotType->Struct,TEXT("EveryN"))->SetPropertyValue_InContainer(Slot,8);
                            FindFProperty<FBoolProperty>(SlotType->Struct,TEXT("bPreventOverlap"))->SetPropertyValue_InContainer(Slot,false);
                            FindFProperty<FBoolProperty>(SlotType->Struct,TEXT("bAlignToRail"))->SetPropertyValue_InContainer(Slot,I==1);
                            FindFProperty<FBoolProperty>(SlotType->Struct,TEXT("bAutoRoll"))->SetPropertyValue_InContainer(Slot,I==1);
                            // Separate slots by height, using one temporary asset.
                            FindFProperty<FFloatProperty>(SlotType->Struct,TEXT("ZOffset"))->SetPropertyValue_InContainer(Slot,-1000.f*(I+1));
                        }
                    }
                    if (ExactPreset)
                    {
                        FString Json; TSharedPtr<FJsonObject> Object;
                        const bool Loaded=FFileHelper::LoadFileToString(Json,*(PresetDirectory/TEXT("settings.json"))) &&
                            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Object) && Object.IsValid() &&
                            FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(),Parameter->Struct,Data);
                        Check(TEXT("Exact ARS preset loads into native settings"),Loaded);
                        const FString Target=System->GetSessionBlueprintPath();
                        Check(TEXT("Preset fixtures use only the isolated blueprint directory"),Target.Contains(TEXT("BuildingSkillsRuntimeTest")));
                        if (Loaded && Target.Contains(TEXT("BuildingSkillsRuntimeTest")))
                        {
                            TestPresetDescriptors.Reset(); TestPresetNames.Reset();
                            auto Stage=[&](FStrProperty* Field,void* Container) {
                                const FString Original=Field->GetPropertyValue_InContainer(Container);
                                const FString Alias=TEXT("BuildingSkillsRuntimeRing_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
                                bool Copied=true;
                                for (const TCHAR* Ext:{TEXT(".sbp"),TEXT(".sbpcfg")})
                                    Copied &= IFileManager::Get().Copy(*(Target/(Alias+Ext)),*(PresetDirectory/(Original+Ext)))==COPY_OK;
                                Check(TEXT("Preset blueprint copied to isolated directory"),Copied);
                                if (Copied) System->EnumerateBlueprints();
                                if (Copied && System->ReadBlueprintFromDisc(Alias))
                                {
                                    FBlueprintRecord Record; Record.BlueprintName=Alias;
                                    auto* Descriptor=System->CreateBlueprintDescriptor(Record,System->mStoredBlueprintData.BlueprintHeader,false);
                                    Check(TEXT("Preset blueprint descriptor is ready"),Descriptor!=nullptr && System->DoesHeaderExistForDescriptor(Descriptor));
                                    TestPresetDescriptors.Add(Descriptor); TestPresetNames.Add(Alias);
                                    Field->SetPropertyValue_InContainer(Container,Alias);
                                    TArray<FItemAmount> Cost; if (Descriptor) Descriptor->GetBlueprintCost(Cost);
                                    for (const auto& Item:Cost) Player->GetInventory()->AddStack(FInventoryStack(Item.Amount*80+100,Item.ItemClass));
                                }
                            };
                            Player->GetInventory()->Resize(1000);
                            Stage(FindFProperty<FStrProperty>(Parameter->Struct,TEXT("UnderRailBlueprintName")),Data);
                            FScriptArrayHelper ExactSlots(Slots,Slots->ContainerPtrToValuePtr<void>(Data));
                            for (int32 I=0; I<ExactSlots.Num(); ++I) Stage(FindFProperty<FStrProperty>(SlotType->Struct,TEXT("BlueprintName")),ExactSlots.GetRawPtr(I));
                            TestTileLength=FindFProperty<FFloatProperty>(Parameter->Struct,TEXT("UnderRailLength"))->GetPropertyValue_InContainer(Data);
                        }
                    }
                    ARS->ProcessEvent(Function,Args.GetStructMemory());
                }
            }
            if (Source) Source->Destroy(); SourceH->Destroy();
        }
        Session->ObservedConstruction.Reset(); TestConstructionPayers.Reset();
        AFGBuildableSubsystem::Get(Player->GetWorld())->BuildableConstructedGlobalDelegate.AddUniqueDynamic(Session,&UBSStickSession::ObserveConstruction);
        auto Rail=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_RailroadTrack.Recipe_RailroadTrack_C"));
        Player->HotKeyRecipe(Rail);
    }
    if (Phase==29)
    {
        auto* H=Active(E); Check(TEXT("ARS long rail fixture equipped"),H!=nullptr);
        if (H)
        {
            E->StickMode=EBSStickMode::Default; E->StickDistance=8;
            PC->SetControlRotation(FRotator::ZeroRotator); H->UpdateHologramPlacement(FHitResult());
            for (int32 I=0; I<6 && H->mBuildStep!=ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding; ++I) H->DoMultiStepPlacement(I%2!=0);
            E->StickDistance=8+50*TestTileLength/100; H->UpdateHologramPlacement(FHitResult());
            Supply(H);
            TestRailPieces=States.FindChecked(H).Parts;
            SupportRouteBounds=FBox(ForceInit);
            for (const auto& Point:States.FindChecked(H).Preview) SupportRouteBounds+=H->GetActorTransform().TransformPosition(Point.Location);
            if (ExactPreset) Player->GetInventory()->Resize(1000);
        }
    }
    if (Phase==31)
    {
        auto* H=Active(E); int32 Instances=0; FBox PreviewBounds(ForceInit);
        for (TActorIterator<AActor> It(Player->GetWorld()); It; ++It) if (It->GetClass()->GetPathName()==TEXT("/Script/AutoRailSupports.ARSPreviewActor"))
            for (auto* ISM:TInlineComponentArray<UInstancedStaticMeshComponent*>(*It))
            {
                Instances+=ISM->GetInstanceCount();
                for (int32 I=0; I<ISM->GetInstanceCount(); ++I) { FTransform T; ISM->GetInstanceTransform(I,T,true); PreviewBounds+=T.GetLocation(); }
            }
        UE_LOG(LogBuildingSkills,Display,TEXT("STICK ARS: preview instances=%d"),Instances);
        Check(TEXT("ARS previews supports on the entire split Stick route"),H && Instances>=3 && PreviewBounds.IsValid && PreviewBounds.GetSize().GetMax()>MaxLength(H)*1.5);
        if (H)
        {
            Check(TEXT("ARS preview restores native first-piece length"),H->FindComponentByClass<USplineComponent>()->GetSplineLength()<=MaxLength(H)+1);
            Session->ObservedConstruction.Reset(); TestConstructionPayers.Reset(); TestStamps.Reset(); ObserveTestStamps=true;
            if (ExactPreset)
            {
                // Also exercise the receiving-side hologram: its dynamic children
                // do not exist when the server assigns the construction player.
                TArray<uint8> Bytes;
                FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Save(Writer,false);
                H->PreConstructMessageSerialization(); H->SerializeConstructMessage(Save,FNetConstructionID());
                auto* Received=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(H->GetRecipe(),Player->GetBuildGun(),H->GetActorLocation(),Player,
                    [](AFGHologram* A) { A->mShouldSpawnChildHolograms=false; }));
                Received->SetConstructionInstigator(Player);
                Received->ResetConstructDisqualifiers();
                FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Load(Reader,true);
                Received->SerializeConstructMessage(Load,FNetConstructionID()); Received->PostConstructMessageDeserialization();
                Check(TEXT("ARS route survives receiving-side construction message"),!Save.IsError() && !Load.IsError() && Reader.Tell()==Bytes.Num());
                Received->SetActorHiddenInGame(false); Received->ValidatePlacementAndCost(Player->GetInventory());
                Check(TEXT("Received ARS route passes native validation"),Received->CanConstruct());
                TArray<AActor*> Children;
                auto* Root=Received->Construct(Children,FNetConstructionID());
                Check(TEXT("Received ARS route constructs every piece"),Root && Children.Num()==TestRailPieces-1);
                Received->Destroy();
            }
            else Cast<UFGBuildGunStateBuild>(Player->GetBuildGun()->GetCurrentState())->InternalConstructHologram(FNetConstructionID());
        }
        Player->UnequipBuildGun();
    }
    if (Phase==39)
    {
        int32 Rails=0; bool Owners=true;
        for (int32 I=0; I<Session->ObservedConstruction.Num(); ++I) if (auto* B=Session->ObservedConstruction[I].Get(); B && B->IsA<AFGBuildableRailroadTrack>())
        {
            ++Rails;
            Owners &= TestConstructionPayers.IsValidIndex(I) && TestConstructionPayers[I].Get()==Player;
        }
        UE_LOG(LogBuildingSkills,Display,TEXT("STICK ARS: constructed rails=%d owners=%d"),Rails,Owners);
        Check(TEXT("Native construction reports all Stick rail pieces"),Rails==TestRailPieces);
        Check(TEXT("Every rail retains the construction player"),Owners);
        if (!ExactPreset)
        {
        TArray<FVector> Stamps; TSet<AActor*> Proxies;
        for (TActorIterator<AFGBuildable> It(Player->GetWorld()); It; ++It)
            // Earlier endpoint placements may still have asynchronous support
            // work in flight. Count this fixture's straight +X route only.
            if (It->GetBlueprintProxy() && It->GetBuiltWithRecipe() && It->GetBuiltWithRecipe()->GetName()==TEXT("Recipe_StorageContainerMk1_C") &&
                SupportRouteBounds.IsValid && It->GetActorLocation().X>SupportRouteBounds.Min.X+1 && It->GetActorLocation().X<SupportRouteBounds.Max.X-1)
                { Stamps.Add(It->GetActorLocation()); Proxies.Add(It->GetBlueprintProxy()); }
        double DeckZ=-DBL_MAX;
        for (const auto& At:Stamps) DeckZ=FMath::Max(DeckZ,At.Z);
        TArray<double> Pillars,Complements,DeckPositions; int32 Decks=0;
        for (const auto& At:Stamps)
        {
            if (FMath::IsNearlyEqual(At.Z,DeckZ,1.0)) { ++Decks; DeckPositions.Add(At.X); }
            else if (FMath::IsNearlyEqual(At.Z,DeckZ-1000,1.0)) Pillars.Add(At.X);
            else if (FMath::IsNearlyEqual(At.Z,DeckZ-2000,1.0)) Complements.Add(At.X);
        }
        for (double X:Pillars) UE_LOG(LogBuildingSkills,Display,TEXT("STICK ARS PILLAR X: %.3f"),X);
        Pillars.Sort(); DeckPositions.Sort(); bool Spacing=Pillars.Num()==7;
        // ARS specifies an interval in tiles. Check the actual every-eighth
        // deck position, independent of native spline distance approximation.
        for (int32 I=0; I<Pillars.Num(); ++I)
            Spacing &= DeckPositions.IsValidIndex(I*8) && FMath::IsNearlyEqual(Pillars[I],DeckPositions[I*8],1.0);
        UE_LOG(LogBuildingSkills,Display,TEXT("STICK ARS: deck tiles=%d pillar tiles=%d total=%d spacing=%d"),Decks,Pillars.Num(),Stamps.Num(),Spacing);
        Check(TEXT("ARS stamps fifty under-rail tiles across split rails"),Decks==50);
        Check(TEXT("ARS stamps all seven every-eighth-tile pillars"),Pillars.Num()==7);
        Check(TEXT("ARS preserves pillar interval across native rail joins"),Spacing);
        if (DoublePillars)
        {
            Complements.Sort(); bool Matches=Complements.Num()==Pillars.Num();
            for (int32 I=0; I<Complements.Num() && Matches; ++I) Matches &= FMath::IsNearlyEqual(Complements[I],Pillars[I],1.0);
            Check(TEXT("ARS repeats both overlapping-permitted pillar slots"),Matches && Complements.Num()==7);
            Check(TEXT("ARS combines all deck and pillar stamps in one blueprint"),Proxies.Num()==1 && Stamps.Num()==64);
        }
        }
        else
        {
            for (int32 I=0; I<TestPresetNames.Num(); ++I)
            {
                int32 Count=0,Empty=0;
                for (const auto& Stamp:TestStamps) if (Stamp.Name==TestPresetNames[I])
                {
                    ++Count; Empty+=Stamp.Objects<=0;
                    UE_LOG(LogBuildingSkills,Display,TEXT("STICK ARS PRESET: slot=%d stamp=%d at=%s objects=%d"),I,Count,*Stamp.Location.ToString(),Stamp.Objects);
                }
                UE_LOG(LogBuildingSkills,Display,TEXT("STICK ARS PRESET TOTAL: slot=%d stamps=%d empty=%d"),I,Count,Empty);
                Check(TEXT("Exact preset stamps every tile and every-eighth slot"),Count==(I==0 ? 50 : 7) && Empty==0);
            }
            for (const auto& Descriptor:TestPresetDescriptors) if (Descriptor.IsValid()) AFGBlueprintSubsystem::Get(Player->GetWorld())->DeleteBlueprintDescriptor(Descriptor.Get());
            TestPresetDescriptors.Reset();
        }
        ObserveTestStamps=false;
        AFGBuildableSubsystem::Get(Player->GetWorld())->BuildableConstructedGlobalDelegate.RemoveDynamic(Session,&UBSStickSession::ObserveConstruction);
        if (SupportDescriptor.IsValid()) AFGBlueprintSubsystem::Get(Player->GetWorld())->DeleteBlueprintDescriptor(SupportDescriptor.Get());
        AFGBlueprintSubsystem::Get(Player->GetWorld())->mBlueprintWorldBuildables.Add(nullptr);
        Check(TEXT("Shutdown fixture includes an expired preview entry"),true);
    }
    if (Phase==40)
    {
        Player->UnequipBuildGun(); E->TickStick();
        auto Rail=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_RailroadTrack.Recipe_RailroadTrack_C"));
        Player->HotKeyRecipe(Rail);
    }
    if (Phase==41)
    {
        E->StickMode=EBSStickMode::Default; E->TickStick(); E->StickDistance=64;
        E->StickTiltSet=true; E->StickTilt=15;
    }
    // Let the new context finish rebuilding before holding its modifier;
    // Enhanced Input deliberately ignores keys held when a context activates.
    if (Phase==42) Key(EKeys::LeftControl,true);
    if (Phase==43) Key(EKeys::MouseScrollUp,true);
    if (Phase==44)
    {
        Key(EKeys::MouseScrollUp,false); Key(EKeys::LeftControl,false);
        const auto& Settings=Player->GetWorld()->GetSubsystem<UBSStickSession>()->Settings;
        Check(TEXT("Ctrl wheel uses the configured horizontal tilt step without changing pitch or distance"),
            E->StickYawSet && FMath::IsNearlyEqual(E->StickYaw,double(Settings.StickTiltStep)) && E->StickTilt==15 && E->StickDistance==64);
        Player->UnequipBuildGun(); E->TickStick();
        Check(TEXT("Leaving the spline tool resets both endpoint tilt axes"),!E->StickTiltSet && !E->StickYawSet && E->StickTilt==0 && E->StickYaw==0);
    }
    if (Phase>=45 && Phase<=62)
    {
        const int32 Case=(Phase-45)/2;
        const TCHAR* Names[]={TEXT("Recipe_Pipeline"),TEXT("Recipe_PipeHyper"),TEXT("Recipe_ConveyorBeltMk1")};
        if ((Phase-45)%2==0)
        {
            Player->UnequipBuildGun(); E->TickStick();
            Player->AddActorWorldOffset(FVector(0,10000,1000));
            PC->SetControlRotation(FRotator::ZeroRotator);
            const TCHAR* Name=Names[Case/3];
            auto Recipe=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/%s.%s_C"),Name,Name));
            AFGRecipeManager::Get(Player->GetWorld())->AddAvailableRecipe(Recipe); Player->HotKeyRecipe(Recipe);
        }
        else if (auto* H=Active(E))
        {
            E->StickMode=EBSStickMode::Default;
            E->TickStick(); E->StickDistance=8;
            auto& Settings=Player->GetWorld()->GetSubsystem<UBSStickSession>()->Settings;
            Settings.VerticalConstraint=true; Settings.HorizontalConstraint=true;
            Settings.StickSlope=FMath::RadiansToDegrees(FMath::Atan(.1)); Settings.StickRadius=120;
            auto Refresh=[&] {
                H->ResetConstructDisqualifiers(); H->UpdateHologramPlacement(FHitResult());
                Supply(H); H->ValidatePlacementAndCost(Player->GetInventory());
            };
            Refresh();
            for (int32 ClickIndex=0; ClickIndex<6 && (H->mBuildStep==ESplineHologramBuildStep::SHBS_FindStart || H->mBuildStep==ESplineHologramBuildStep::SHBS_AdjustStartingPole); ++ClickIndex)
            { H->DoMultiStepPlacement(ClickIndex%2!=0); Refresh(); }
            Check(TEXT("Support placement clicks establish the first endpoint"),H->mBuildStep==ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding);
            E->StickDistance=8+MaxLength(H)*(Case%3==0 && Case/3!=2 ? .8 : Case%3==2 ? 1.5 : 2.5)/100;
            Refresh();
            bool Ready=false;
            for (int32 ClickIndex=0; ClickIndex<6 && !Ready; ++ClickIndex)
            { Ready=H->DoMultiStepPlacement(ClickIndex%2!=0); if (!Ready) Refresh(); }
            UE_LOG(LogBuildingSkills,Display,TEXT("STICK FINAL CLICK: family=%s case=%d ready=%d step=%d parts=%d valid=%d"),
                Names[Case/3],Case%3,Ready,int32(H->mBuildStep),States.Contains(H) ? States.FindChecked(H).Parts : 1,H->CanConstruct());
            Check(TEXT("Support endpoint completes the native final click sequence"),Ready && H->CanConstruct());
            if (!H->CanConstruct()) Explain(H);
            const int32 Expected=States.Contains(H) ? States.FindChecked(H).Parts : 1;
            auto* Received=Cast<AFGSplineHologram>(AFGHologram::SpawnHologramFromRecipe(H->GetRecipe(),Player->GetBuildGun(),Player->GetActorLocation(),Player));
            Received->ResetConstructDisqualifiers();
            // Exercise the native relative-reference archive as well. A plain
            // string archive alone resolves support pointers back to the client
            // preview and misses child-reference failures on the receiver.
            AFGHologram* Pending=nullptr;
            if (Case%3==2)
            {
                Pending=AFGHologram::SpawnHologramFromRecipe(H->GetRecipe(),Player->GetBuildGun(),Player->GetActorLocation(),Player);
                Pending->SetupPendingConstructionHologram(H);
            }
            Received->SetupPendingConstructionHologram(Pending ? Pending : H);
            Received->SetIsPendingToBeConstructed(false);
            Received->SetActorHiddenInGame(false); Received->ValidatePlacementAndCost(Player->GetInventory());
            Check(TEXT("Completed supported placement survives server message validation"),ReceivedStickRoots.Contains(Received) && Received->CanConstruct());
            if (!Received->CanConstruct()) Explain(Received);
            TArray<AActor*> Children;
            Received->SetSuppressBuildEffect(true);
            AActor* Built=Ready && Received->CanConstruct() ? Received->Construct(Children,FNetConstructionID()) : nullptr;
            int32 Pieces=IsValid(Built) ? 1 : 0;
            for (auto* Child:Children) if (IsValid(Child) && Child->IsA(H->GetBuildClass())) ++Pieces;
            Check(TEXT("Final supported placement constructs every connected spline segment"),IsValid(Built) && Pieces==Expected);
            if (Case%3==2 && States.Contains(H))
            {
                const auto& Preview=States.FindChecked(H).Preview;
                ExistingSupportEnds[Case/3][0]=H->GetActorTransform().TransformPosition(Preview[0].Location);
                ExistingSupportEnds[Case/3][1]=H->GetActorTransform().TransformPosition(Preview.Last().Location);
            }
            for (auto* Child:Children) if (IsValid(Child) && Child->Implements<UFGDismantleInterface>())
            {
                if (Case%3==2 && !Cast<IFGSplineBuildableInterface>(Child)) ExistingSupports[Case/3].Add(Child);
                else IFGDismantleInterface::Execute_Dismantle(Child);
            }
            if (IsValid(Built) && Built->Implements<UFGDismantleInterface>()) IFGDismantleInterface::Execute_Dismantle(Built);
            Received->Destroy();
            if (Pending) Pending->Destroy();
        }
        else Check(TEXT("Supported placement fixture equips its spline"),false);
    }
    if (Phase>=63 && Phase<=74)
    {
        const int32 Family=(Phase-63)/4, Step=(Phase-63)%4;
        const TCHAR* Names[]={TEXT("Recipe_Pipeline"),TEXT("Recipe_PipeHyper"),TEXT("Recipe_ConveyorBeltMk1")};
        if (Step==0)
        {
            Player->UnequipBuildGun(); E->TickStick();
            Player->SetActorLocation(ExistingSupportEnds[Family][0]-FVector(800,0,100));
            PC->SetControlRotation(FRotator::ZeroRotator);
            auto Recipe=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/%s.%s_C"),Names[Family],Names[Family]));
            Player->HotKeyRecipe(Recipe);
            Check(TEXT("Existing-support fixture retains both native poles"),ExistingSupports[Family].Num()==2);
        }
        if (Step==1 || Step==2) if (auto* H=Active(E))
        {
            E->StickMode=EBSStickMode::Default; E->TickStick();
            FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View);
            const FVector Delta=ExistingSupportEnds[Family][Step-1]-Eye;
            PC->SetControlRotation(Delta.Rotation()); E->StickDistance=Delta.Size()/100;
            if (PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0);
            H->SetSnapToGuideLines(false); H->ResetConstructDisqualifiers(); H->UpdateHologramPlacement(FHitResult());
            Supply(H); H->ValidatePlacementAndCost(Player->GetInventory());
            auto* Target=Cast<USceneComponent>(Step==1 ? Snap(H,0) : States.Contains(H) ? States.FindChecked(H).EndSnap.Get() : nullptr);
            UE_LOG(LogBuildingSkills,Display,TEXT("STICK SUPPORT AIM: family=%s end=%d distance=%g snapped=%s target=%s"),
                Names[Family],Step-1,E->StickDistance,*GetPathNameSafe(Target),*ExistingSupportEnds[Family][Step-1].ToString());
            Check(TEXT("Native aim snaps to the existing support"),Target && ExistingSupports[Family].Contains(Target->GetOwner()));
            if (Step==2)
            {
                Check(TEXT("Existing-support route is valid before its final click"),H->CanConstruct() && H->CanTakeNextBuildStep());
                if (!H->CanConstruct()) Explain(H);
                const auto Before=H->mBuildStep;
                bool NativeReady=false;
                { TGuardValue<bool> NativeGuard(TestNativeEndpointClick,true); NativeReady=H->DoMultiStepPlacement(false); }
                H->mBuildStep=Before;
                Check(TEXT("Unadapted native final click reproduces the split endpoint stall"),!NativeReady);
                Session->ObservedConstruction.Reset();
                AFGBuildableSubsystem::Get(Player->GetWorld())->BuildableConstructedGlobalDelegate.AddDynamic(Session,&UBSStickSession::ObserveConstruction);
            }
            auto* Build=CastChecked<UFGBuildGunStateBuild>(Player->GetBuildGun()->GetCurrentState());
            Build->PrimaryFire_Implementation(); Build->PrimaryFireRelease_Implementation();
            if (Step==1)
            {
                Check(TEXT("First existing-support click establishes the route"),H->mBuildStep==ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding);
                // Distant native supports can have their collision disabled by
                // significance. Walk to the destination before the next frame,
                // just as a player aiming at an existing support would do.
                if (Family==1)
                {
                    Player->SetActorLocation(ExistingSupportEnds[Family][1]-FVector(800,0,100));
                    PC->SetControlRotation(FRotator::ZeroRotator);
                }
            }
        }
        if (Step==3)
        {
            AFGBuildableSubsystem::Get(Player->GetWorld())->BuildableConstructedGlobalDelegate.RemoveDynamic(Session,&UBSStickSession::ObserveConstruction);
            int32 Pieces=0,Links=0; bool Endpoints[2]{};
            for (const auto& Weak:Session->ObservedConstruction) if (auto* B=Weak.Get(); B && Cast<IFGSplineBuildableInterface>(B))
            {
                ++Pieces;
                for (auto* Port:TInlineComponentArray<UFGPipeConnectionComponentBase*>(B))
                {
                    Links+=Port->GetConnection()!=nullptr;
                    for (int32 End=0; End<2; ++End) Endpoints[End] |= Port->GetComponentLocation().Equals(ExistingSupportEnds[Family][End],1);
                }
                for (auto* Port:TInlineComponentArray<UFGFactoryConnectionComponent*>(B))
                {
                    Links+=Port->GetConnection()!=nullptr;
                    for (int32 End=0; End<2; ++End) Endpoints[End] |= Port->GetComponentLocation().Equals(ExistingSupportEnds[Family][End],1);
                }
            }
            UE_LOG(LogBuildingSkills,Display,TEXT("STICK EXISTING SUPPORTS: family=%s pieces=%d links=%d ends=%d,%d"),Names[Family],Pieces,Links,Endpoints[0],Endpoints[1]);
            Check(TEXT("Build-gun click constructs the connected route between existing supports"),Pieces==2 && Links==2 && Endpoints[0] && Endpoints[1]);
            for (const auto& Weak:Session->ObservedConstruction) if (auto* B=Weak.Get()) IFGDismantleInterface::Execute_Dismantle(B);
            for (const auto& Weak:ExistingSupports[Family]) if (auto* B=Weak.Get()) IFGDismantleInterface::Execute_Dismantle(B);
            ExistingSupports[Family].Reset(); Session->ObservedConstruction.Reset();
            Player->UnequipBuildGun(); E->TickStick();
        }
    }
    return Passed;
}
