#include "BSClipboard.h"
#include "BSDeduplication.h"
#include "BSBlueprintSave.h"
#include "BSBlueprintScale.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGActorSaveHeaderTypes.h"
#include "FGBlueprintSubsystem.h"
#include "FGBlueprintSettings.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "FGSplineBuildableInterface.h"
#include "Buildables/FGBuildablePipeBase.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "FGDismantleInterface.h"
#include "FGInventoryComponent.h"
#include "FGInventoryLibrary.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/ScopeExit.h"
#include "Patching/NativeHookManager.h"

namespace
{
    constexpr int32 ChunkSize = 24 * 1024;
    constexpr int32 MaxBytes = 32 * 1024 * 1024;
    const TCHAR* ClipboardPrefix = TEXT("BSClipboard_");
    thread_local UWorld* CostPreviewWorld = nullptr;

    int32 PreviewSplineMultiplier(const AFGBuildable* Building, const IFGSplineBuildableInterface* Spline, float SegmentLength)
    {
        FSplineCurves Curves;
        const auto& Points=Spline->GetSplinePointData();
        for (int32 I=0; I<Points.Num(); ++I)
        {
            Curves.Position.Points.Emplace(float(I),Points[I].Location,Points[I].ArriveTangent,Points[I].LeaveTangent,CIM_CurveUser);
            Curves.Rotation.AddPoint(float(I),FQuat::Identity);
            Curves.Scale.AddPoint(float(I),FVector::OneVector);
        }
        const auto* Component=Spline->GetSplineComponent();
        Curves.UpdateSpline(false,false,Component ? Component->ReparamStepsPerSegment : 10,false,0,Building->GetActorScale3D());
        return AFGBuildable::GetCostMultiplierForLength(Curves.GetSplineLength(),SegmentLength);
    }

    UBSEditModeComponent* EditFor(UBSGroupRemoteCallObject* RCO)
    {
        auto* Player = RCO->GetOwnerPlayerCharacter();
        return Player ? Player->FindComponentByClass<UBSEditModeComponent>() : nullptr;
    }
    UBSClipboard* ByRequest(UBSGroupRemoteCallObject* RCO, FGuid Id)
    {
        for (UBSClipboard* Clipboard : RCO->Clipboards) if (Clipboard && Clipboard->RequestId == Id) return Clipboard;
        return nullptr;
    }
    void Retain(UBSGroupRemoteCallObject* RCO, UBSClipboard* Clipboard)
    {
        // Keep the previous snapshot for construction messages already in flight.
        while (RCO->Clipboards.Num() >= 2) RCO->Clipboards.RemoveAt(0);
        RCO->Clipboards.Add(Clipboard);
    }
}

FString UBSClipboard::Name() const { return FString(ClipboardPrefix) + RequestId.ToString(EGuidFormats::Digits); }

UBSClipboard* UBSClipboard::Find(UWorld* World, const FString& Name)
{
    if (!Name.StartsWith(ClipboardPrefix)) return nullptr;
    for (TObjectIterator<UBSGroupRemoteCallObject> It; It; ++It)
        if (It->GetWorld() == World)
            for (UBSClipboard* Clipboard : It->Clipboards)
                if (Clipboard && Clipboard->Name() == Name && Clipboard->Data.IsValid()) return Clipboard;
    return nullptr;
}

void UBSClipboard::MakeDescriptor()
{
    FBlueprintRecord Record;
    Record.BlueprintName = Name();
    Record.FilteredBlueprintName = TEXT("Selection Copy");
    Record.BlueprintDescription = TEXT("Copy of the Edit selection");
    Descriptor = NewObject<UFGBlueprintDescriptor>(this, NAME_None, RF_Transient);
    Descriptor->InitializeData(Record);
    Descriptor->InitializeHeaderInfo(Data.BlueprintHeader);
    if (auto* System = AFGBlueprintSubsystem::Get(GetWorld()))
        Descriptor->SetRecipeRequirementsAreMet(System->AreRecipeRequirementsMetForBlueprint(Data.BlueprintHeader));
}

bool UBSClipboard::Capture(const FBSSelection& Selection, FString& Error, const FBSBuildingRef& Anchor, bool RequireReach)
{
    auto* RCO = Cast<UBSGroupRemoteCallObject>(GetOuter());
    auto* Player = RCO ? RCO->GetOwnerPlayerCharacter() : nullptr;
    auto* System = AFGBlueprintSubsystem::Get(GetWorld());
    if (!Player || !System || !System->HasAuthority()) return false;
    FActorSpawnParameters Params;
    Params.Owner = Player->GetController(); Params.ObjectFlags |= RF_Transient;
    auto* Session = GetWorld()->SpawnActor<ABSBlueprintSaveSession>(ABSBlueprintSaveSession::StaticClass(), Player->GetActorTransform(), Params);
    if (!Session) return false;
    Session->RequireReach = RequireReach;
    Session->SourceSelection = Selection;
    Session->SourceAnchor = Anchor;
    const bool Prepared = Session->Prepare(Error);
    if (!Prepared) { Session->Destroy(); return false; }
    Members = Session->Members;
    SourceOrigin = Session->BlueprintOrigin;
    FBlueprintRecord Record;
    Record.BlueprintName = Name();
    const auto Buildings = Session->GetSelectionBuildables();
    const bool Written = WriteBuildingSkillsClipboard(System, Record, SourceOrigin, Buildings, Session->GetSelectionDimensions(), Data);
    Session->Destroy();
    if (!Written) { Error = TEXT("The selection could not be copied."); return false; }

    // Require every selected object at its expected pose; never show an empty or partial copy.
    TArray<FActorSaveHeader> Headers;
    if (!ReadBuildingSkillsBlueprintHeaders(Data, Headers)) return false;
    for (const auto& Member : Members)
    {
        auto* Actor = Member.ResolveActor(GetWorld());
        if (!Actor) return false;
        const FString Path = FObjectReferenceDisc(Actor).ToString();
        const auto* Header = Headers.FindByPredicate([&](const auto& H) { return H.ObjectHeader.Reference.ToString() == Path; });
        if (!Header || !FTransform(Header->Transform).Equals(Actor->GetActorTransform().GetRelativeTransform(SourceOrigin), .1))
        { Error = TEXT("Copy validation failed: a selected building is missing or misplaced."); return false; }
        MemberPaths.Add(Path);
    }
    if (auto* Groups = ABSGroupSubsystem::Get(GetWorld()))
        for (const auto& Group : Groups->Groups)
            for (const auto& Root : Selection.Groups)
                if (FBSGroupTree::IsWithin(Groups->Groups, Group.Id, Root))
                { Hierarchy.Add(Group); break; }
    // Parents outside the selected hierarchy are deliberately not copied.
    for (auto& Group : Hierarchy) if (!FBSGroupTree::Find(Hierarchy, Group.Parent)) Group.Parent.Invalidate();
    FMemoryWriter Writer(Bytes, true);
    AFGBlueprintSubsystem::SerializeBlueprintHeader(Writer, Data.BlueprintHeader);
    Writer << Data.TOCData << Data.BlobData;
    if (Writer.IsError() || Bytes.Num() > MaxBytes)
    { Error = TEXT("This selection is too large to copy. Select fewer buildings."); return false; }
    MakeDescriptor();
    if (!Descriptor->GetRecipeRequirementsAreMet())
    { Error = TEXT("Unlock all selected building recipes before copying."); return false; }
    return true;
}

bool UBSClipboard::Decode(FString& Error)
{
    FMemoryReader Reader(Bytes, true);
    Reader.ArMaxSerializeSize = MaxBytes;
    if (!AFGBlueprintSubsystem::SerializeBlueprintHeader(Reader, Data.BlueprintHeader)) return false;
    Reader << Data.TOCData << Data.BlobData;
    Data.BlueprintHeader.BlueprintName = Name();
    TArray<FActorSaveHeader> Headers;
    if (Reader.IsError() || Reader.Tell() != Bytes.Num() || !Data.IsValid() || !ReadBuildingSkillsBlueprintHeaders(Data, Headers))
    { Error = TEXT("The server's copy could not be read. Try Copy again."); return false; }
    MakeDescriptor();
    Bytes.Reset();
    return true;
}

void UBSClipboard::RestoreGroups(const TMap<FString, FBSBuildingRef>& Placed, const FTransform& Origin)
{
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    if (!Groups || !Groups->HasAuthority() || Hierarchy.IsEmpty()) return;
    TArray<FBSGroup> Added;
    TMap<FGuid, FGuid> Ids;
    for (const auto& Group : Hierarchy) Ids.Add(Group.Id, FGuid::NewGuid());
    for (const auto& Original : Hierarchy)
    {
        auto& Group = Added.AddDefaulted_GetRef();
        Group.Id = Ids[Original.Id]; Group.Name = Original.Name; Group.Parent = Ids.FindRef(Original.Parent);
        Group.Pivot = Origin.TransformPosition(SourceOrigin.InverseTransformPosition(Original.Pivot));
        for (const auto& Member : Original.Buildings)
        {
            const int32 Index = Members.IndexOfByKey(Member);
            const auto* Ref = MemberPaths.IsValidIndex(Index) ? Placed.Find(MemberPaths[Index]) : nullptr;
            if (!Ref || !Ref->IsAlive(GetWorld()))
            {
                if (MemberPaths.IsValidIndex(Index) && FBSDeduplication::SkipObject(MemberPaths[Index])) continue;
                UE_LOG(LogBuildingSkills, Error, TEXT("Copy constructed but group restoration failed: missing member"));
                if (auto* RCO = Cast<UBSGroupRemoteCallObject>(GetOuter()))
                    RCO->Client_BlueprintMessage(TEXT("Copy placed, but its groups could not be restored. Regroup the copy."));
                return;
            }
            Group.Buildings.Add(*Ref);
        }
    }
    FBSGroupTree::PruneEmpty(Added);
    if (Added.IsEmpty() || !FBSGroupTree::Validate(Added)) return;
    Groups->Groups.Append(Added);
    Groups->PublishGroups();
}

void UBSGroupRemoteCallObject::Server_CopySelection_Implementation(const FBSSelection& Selection, FGuid RequestId, const FBSBuildingRef& Anchor)
{
    if (!RequestId.IsValid() || ByRequest(this, RequestId)) return;
    auto* Clipboard = NewObject<UBSClipboard>(this);
    Clipboard->RequestId = RequestId;
    FString Error;
    if (!Clipboard->Capture(Selection, Error, Anchor))
    { Client_CopyChunk(RequestId, 0, 0, {}, Error.IsEmpty() ? TEXT("Could not copy the selection.") : Error); return; }
    Retain(this, Clipboard);
    Server_CopyChunk_Implementation(RequestId, 0);
}

void UBSGroupRemoteCallObject::Server_CopyChunk_Implementation(FGuid RequestId, int32 Offset)
{
    auto* Clipboard = ByRequest(this, RequestId);
    if (!Clipboard || Offset != Clipboard->NextOffset || Offset < 0 || Offset >= Clipboard->Bytes.Num()) return;
    TArray<uint8> Chunk;
    Chunk.Append(Clipboard->Bytes.GetData() + Offset, FMath::Min(ChunkSize, Clipboard->Bytes.Num() - Offset));
    Clipboard->NextOffset += Chunk.Num();
    Client_CopyChunk(RequestId, Offset, Clipboard->Bytes.Num(), Chunk, TEXT(""));
}

void UBSGroupRemoteCallObject::Client_CopyChunk_Implementation(FGuid RequestId, int32 Offset, int32 Total, const TArray<uint8>& Bytes, const FString& Error)
{
    auto* Edit = EditFor(this);
    if (!Edit || !Edit->IsCopyPending(RequestId)) return;
    if (!Error.IsEmpty()) { Edit->OnCopyReady(RequestId, nullptr, Error); return; }
    auto* Clipboard = ByRequest(this, RequestId);
    if (GetOwnerPlayerCharacter() && GetOwnerPlayerCharacter()->HasAuthority() && Clipboard)
    { Edit->OnCopyReady(RequestId, Clipboard->Descriptor, TEXT("")); return; }
    if (Total <= 0 || Total > MaxBytes || Offset < 0 || Bytes.IsEmpty() || Bytes.Num() > ChunkSize || Offset > Total - Bytes.Num())
    { Edit->OnCopyReady(RequestId, nullptr, TEXT("Invalid copy transfer.")); return; }
    if (!Clipboard && Offset == 0)
    {
        Clipboard = NewObject<UBSClipboard>(this); Clipboard->RequestId = RequestId;
        Retain(this, Clipboard);
    }
    if (!Clipboard || Clipboard->Bytes.Num() != Offset)
    { Edit->OnCopyReady(RequestId, nullptr, TEXT("The copy transfer was interrupted.")); return; }
    Clipboard->Bytes.Append(Bytes);
    if (Clipboard->Bytes.Num() < Total) { Server_CopyChunk(RequestId, Clipboard->Bytes.Num()); return; }
    FString DecodeError;
    const bool Success = Clipboard->Decode(DecodeError);
    Edit->OnCopyReady(RequestId, Success ? Clipboard->Descriptor.Get() : nullptr,
        Success ? TEXT("") : (DecodeError.IsEmpty() ? TEXT("Could not decode the copy.") : DecodeError));
}

void UBSEditModeComponent::OnCopy()
{
    if (!IsEditing() || bTransforming || !InputAllowed() || bRequestPending || bCopyPending || bOpeningBlueprint || BlueprintWindow) return;
    const auto Current = EffectiveSelection();
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    auto* RCO = PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    if (!RCO || Current.IsEmpty()) return;
    bCopyPending = true; CopyRequestId = FGuid::NewGuid(); CopyDeadline = FPlatformTime::Seconds() + 180;
    Feedback = TEXT("Preparing copy...");
    RCO->RequestSelection(4, Current, CopyRequestId, 0, BlueprintAnchor);
}

void UBSEditModeComponent::OnCopyReady(FGuid RequestId, UFGBlueprintDescriptor* Descriptor, const FString& Error)
{
    if (!IsCopyPending(RequestId)) return;
    bCopyPending = false;
    if (!Error.IsEmpty() || !Descriptor) { Feedback = Error; return; }
    if (!Descriptor->GetRecipeRequirementsAreMet())
    { Feedback = TEXT("Unlock all selected building recipes before copying."); return; }
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* System = AFGBlueprintSubsystem::Get(GetWorld());
    auto Recipe = UFGBlueprintSettings::Get()->mBlueprintRecipeClass.LoadSynchronous();
    if (!IsEditing() || !InputAllowed() || !Player || !Player->GetBuildGun() || !System || !Recipe) return;
    // Authority GotoBuildState copies the build state's descriptor back into
    // the subsystem. Prime both, just as native blueprint sampling does.
    auto* BuildState = Cast<UFGBuildGunStateBuild>(Player->GetBuildGun()->GetBuildGunStateFor(EBuildGunState::BGS_BUILD));
    if (!BuildState) return;
    LeaveEditMode(false);
    BuildState->SetActiveBlueprintDescriptor(Descriptor);
    System->SetActiveBlueprintHologramDescriptor(Descriptor);
    Player->GetBuildGun()->GotoBuildState(Recipe);
}

void UBSClipboard::InstallHooks()
{
    SUBSCRIBE_UOBJECT_METHOD(AFGBlueprintSubsystem, EndPlay,
        [](auto&, AFGBlueprintSubsystem* System, EEndPlayReason::Type)
        {
            // Preview consumers can destroy measurement actors before this
            // subsystem. GC nulls the reflected references; native EndPlay
            // calls Destroy on each entry without checking for null.
            const int32 Removed=System->mBlueprintWorldBuildables.RemoveAll(
                [](const auto& Building) { return !IsValid(Building.Get()); });
            if (Removed) UE_LOG(LogBuildingSkills, Display,
                TEXT("Blueprint shutdown: removed %d expired preview entries"), Removed);
        });
    if (WITH_EDITOR) return;
    // Recipe lookup remains entirely native. PreviewMemberCost supplies its
    // transient preview-world context without intercepting gameplay lookups.
    // Base refunds ask connected components for nested refunds through interface
    // events too. Only preview cost evaluation bypasses suppressed ProcessEvent.
    SUBSCRIBE_METHOD(IFGDismantleInterface::Execute_GetDismantleRefund,
        [](auto& Scope, const UObject* Object, TArray<FInventoryStack>& Refund, bool Free)
        {
            if (CostPreviewWorld && Object && Object->GetWorld()==CostPreviewWorld)
            {
                if (auto* Building=Cast<AFGBuildable>(const_cast<UObject*>(Object)); Building && FBSDeduplication::SkipObject(FBSDeduplication::SourcePath(Building)))
                { Scope.Cancel(); return; }
                if (const auto* Interface=Cast<IFGDismantleInterface>(Object))
                { Interface->GetDismantleRefund_Implementation(Refund,Free); Scope.Cancel(); }
            }
        });
    // Length caches initialize at BeginPlay, which preview actors never receive.
    // Evaluate native cubic arc lengths on a stack curve without touching actors.
    SUBSCRIBE_UOBJECT_METHOD(AFGBuildablePipeBase, GetDismantleRefundReturnsMultiplier,
        [](auto& Scope, const AFGBuildablePipeBase* Pipe)
        { if (CostPreviewWorld && Pipe->GetWorld()==CostPreviewWorld) Scope.Override(PreviewSplineMultiplier(Pipe,Pipe,Pipe->GetMeshLength()*AFGBuildablePipeBase::PIPE_COST_LENGTH_MULTIPLIER)); });
    SUBSCRIBE_UOBJECT_METHOD(AFGBuildableConveyorBelt, GetDismantleRefundReturnsMultiplier,
        [](auto& Scope, const AFGBuildableConveyorBelt* Belt)
        { if (CostPreviewWorld && Belt->GetWorld()==CostPreviewWorld) Scope.Override(PreviewSplineMultiplier(Belt,Belt,Belt->GetMeshLength())); });
    SUBSCRIBE_UOBJECT_METHOD(AFGBuildableRailroadTrack, GetDismantleRefundReturnsMultiplier,
        [](auto& Scope, const AFGBuildableRailroadTrack* Track)
        { if (CostPreviewWorld && Track->GetWorld()==CostPreviewWorld) Scope.Override(PreviewSplineMultiplier(Track,Track,Track->GetMeshLength())); });
    SUBSCRIBE_METHOD(AFGBlueprintSubsystem::GetBlueprintDescriptorByNameString,
        [](auto& Scope, AFGBlueprintSubsystem* System, const FString& Name)
        { if (auto* Clipboard = Find(System->GetWorld(), Name)) Scope.Override(Clipboard->Descriptor.Get()); });
    SUBSCRIBE_METHOD(AFGBlueprintSubsystem::ReadBlueprintFromDisc,
        [](auto& Scope, AFGBlueprintSubsystem* System, const FString& Name)
        {
            if (!Name.StartsWith(ClipboardPrefix)) return;
            auto* Clipboard = Find(System->GetWorld(), Name);
            if (Clipboard) System->mStoredBlueprintData = Clipboard->Data;
            Scope.Override(Clipboard != nullptr);
        });
    SUBSCRIBE_METHOD(AFGBuildGun::SetDesiredBlueprint,
        [](auto& Scope, AFGBuildGun* Gun, const FString& Name)
        {
            if (!Name.StartsWith(ClipboardPrefix)) return;
            auto* Clipboard = Find(Gun->GetWorld(), Name);
            auto* RCO = Clipboard ? Cast<UBSGroupRemoteCallObject>(Clipboard->GetOuter()) : nullptr;
            if (RCO && RCO->GetOwnerPlayerCharacter() == Gun->GetInstigator())
                if (auto* State = Cast<UFGBuildGunStateBuild>(Gun->GetBuildGunStateFor(EBuildGunState::BGS_BUILD)))
                    State->SetActiveBlueprintDescriptor(Clipboard->Descriptor);
            Scope.Cancel();
        });
    SUBSCRIBE_UOBJECT_METHOD(AFGBlueprintHologram, PostConstructMessageDeserialization,
        [](auto& Scope, AFGBlueprintHologram* Hologram)
        {
            auto* Clipboard = Find(Hologram->GetWorld(), Hologram->mBlueprintDescName);
            auto* System = AFGBlueprintSubsystem::Get(Hologram->GetWorld());
            if (!Clipboard || !System) return;
            // The shipping binary inlines descriptor lookup here. Expose the
            // descriptor only while the native message decoder uses that array.
            auto Descriptors = System->mAllBlueprintDescriptors;
            Descriptors.AddUnique(Clipboard->Descriptor);
            TGuardValue<decltype(Descriptors)> Guard(System->mAllBlueprintDescriptors, MoveTemp(Descriptors));
            Scope(Hologram);
        });
    SUBSCRIBE_METHOD(AFGBlueprintHologram::OnRep_BlueprintDescName,
        [](auto& Scope, AFGBlueprintHologram* Hologram)
        {
            auto* Clipboard = Find(Hologram->GetWorld(), Hologram->mBlueprintDescName);
            auto* System = AFGBlueprintSubsystem::Get(Hologram->GetWorld());
            if (!Clipboard || !System) return;
            auto Descriptors = System->mAllBlueprintDescriptors;
            Descriptors.AddUnique(Clipboard->Descriptor);
            TGuardValue<decltype(Descriptors)> Guard(System->mAllBlueprintDescriptors, MoveTemp(Descriptors));
            Scope(Hologram);
        });
    SUBSCRIBE_UOBJECT_METHOD(AFGBlueprintHologram, Construct,
        [](auto& Scope, AFGBlueprintHologram* Hologram, TArray<AActor*>& Children, FNetConstructionID Id)
        {
            const FString Name = Hologram->mBlueprintDescriptor ? Hologram->mBlueprintDescriptor->GetBlueprintNameAsString() : Hologram->mBlueprintDescName;
            if (!Name.StartsWith(ClipboardPrefix)) return;
            auto* Clipboard = Find(Hologram->GetWorld(), Name);
            auto* RCO = Clipboard ? Cast<UBSGroupRemoteCallObject>(Clipboard->GetOuter()) : nullptr;
            if (!RCO || RCO->GetOwnerPlayerCharacter() != Hologram->GetConstructionInstigator()) Scope.Override(static_cast<AActor*>(nullptr));
        });
    SUBSCRIBE_METHOD(AFGBlueprintSubsystem::LoadStoredBlueprint,
        ([](auto& Scope, AFGBlueprintSubsystem* System, UFGBlueprintDescriptor* Descriptor,
            const FTransform& Origin, TArray<AFGBuildable*>& Buildings, bool BlueprintWorld,
            AFGBuildableBlueprintDesigner* Designer, APawn* Instigator, AFGBlueprintProxy* Proxy,
            const TFunction<void(AFGBuildable*, int32)>& PostSerialize, const TFunction<void()>& PreBeginPlay)
        {
            if (!Descriptor) return;
            auto* Clipboard = Find(System->GetWorld(), Descriptor->GetBlueprintNameAsString());
            if (!Clipboard) return;
            auto* RCO = Cast<UBSGroupRemoteCallObject>(Clipboard->GetOuter());
            if (!BlueprintWorld && (!RCO || RCO->GetOwnerPlayerCharacter() != Instigator)) { Scope.Cancel(); return; }
            System->mStoredBlueprintData = Clipboard->Data;
            // Native loading requires a cached header as well as the archive.
            // Scope it to this call so clipboard entries never enter library enumeration.
            auto Headers = System->mCachedHeaders;
            Headers.RemoveAll([&](const auto& Header) { return Header.BlueprintName == Clipboard->Name(); });
            Headers.Add(Clipboard->Data.BlueprintHeader);
            TGuardValue<TArray<FBlueprintHeader>> HeaderGuard(System->mCachedHeaders, MoveTemp(Headers));
            if (BlueprintWorld || !System->HasAuthority())
            {
                Scope(System, Descriptor, Origin, Buildings, BlueprintWorld, Designer, Instigator, Proxy, PostSerialize, PreBeginPlay);
                return;
            }
            TMap<int32, FString> Paths;
            if (!ReadBuildingSkillsBlueprintObjectPaths(Clipboard->Data, Paths)) { Scope.Cancel(); return; }
            TMap<FString, FBSBuildingRef> Placed;
            const TFunction<void(AFGBuildable*, int32)> Capture = [&](AFGBuildable* Building, int32 Index)
            {
                if (PostSerialize) PostSerialize(Building, Index);
                const FString Source = FBSDeduplication::SourcePath(Building);
                const FString* Path = Source.IsEmpty() ? Paths.Find(Index) : &Source;
                if (Path && Clipboard->MemberPaths.Contains(*Path))
                {
                    FBSBuildingRef Ref;
                    Ref.Transform = Building->GetActorTransform();
                    if (Building->ShouldConvertToLightweight())
                    { Ref.LightweightClass = Building->GetClass(); Ref.Recipe = Building->GetBuiltWithRecipe(); }
                    else Ref.Actor = Building;
                    Placed.Add(*Path, Ref);
                }
            };
            Scope(System, Descriptor, Origin, Buildings, BlueprintWorld, Designer, Instigator, Proxy, Capture, PreBeginPlay);
              if (!FBSDeduplication::DeferGroups([Clipboard, Placed, Origin]() { Clipboard->RestoreGroups(Placed, Origin); }))
                  Clipboard->RestoreGroups(Placed, Origin);
        }));
}

// Uses the native blueprint cost components without its world-dismantle eligibility
// filter. Preview actors are never offered to the dismantle gun or modified here.
void UBSClipboard::PreviewMemberCost(AFGBlueprintSubsystem* System,AFGBuildable* Building,TArray<FItemAmount>& Cost)
{
    Cost.Reset();
    if (!IsInGameThread() || !System || !IsValid(Building)) return;
    auto* LiveWorld = System->GetWorld();
    auto* PreviewWorld = Building->GetWorld();
    auto* Recipes = AFGRecipeManager::Get(LiveWorld);
    if (!Recipes || !PreviewWorld || (PreviewWorld != LiveWorld && PreviewWorld != System->mBlueprintWorld)) return;

    // Native recipe lookup reads AFGGameState from UWorld. Only the native
    // blueprint preview world borrows this pointer during synchronous pricing.
    // Do not call SetGameState: that broadcasts events and changes level
    // collections. Restore the exact prior field on every exit, including nested
    // evaluations. Neither the live world nor the GameState actor is modified.
    FObjectPropertyBase* StateProperty = nullptr;
    UObject* PreviousState = nullptr;
    ON_SCOPE_EXIT
    {
        if (StateProperty) StateProperty->SetObjectPropertyValue_InContainer(PreviewWorld, PreviousState);
    };
    if (PreviewWorld != LiveWorld)
    {
        StateProperty = FindFProperty<FObjectPropertyBase>(UWorld::StaticClass(), TEXT("GameState"));
        if (!StateProperty) return;
        PreviousState = StateProperty->GetObjectPropertyValue_InContainer(PreviewWorld);
        StateProperty->SetObjectPropertyValue_InContainer(PreviewWorld, LiveWorld->GetGameState());
    }
    if (AFGRecipeManager::Get(PreviewWorld) != Recipes) return;
    TGuardValue<UWorld*> WorldGuard(CostPreviewWorld, PreviewWorld);
    TArray<FInventoryStack> Refund;
    // Blueprint preview worlds suppress ProcessEvent; the interface Execute_
    // wrapper consequently returns an empty default. Dispatch the native virtual
    // implementation so storage/factory subclasses still contribute their items.
    Building->GetDismantleRefund_Implementation(Refund,false);
    for (const auto& Stack : Refund) if (Stack.HasItems()) Cost.Emplace(Stack.Item.GetItemClass(),Stack.NumItems);
    System->CalculateBlueprintCustomizationCost({Building},Cost);
    UFGInventoryLibrary::ConsolidateItemsAmount(Cost);
}
