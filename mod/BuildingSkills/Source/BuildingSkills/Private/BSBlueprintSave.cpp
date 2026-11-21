#include "BSFactoryAccess.h"
#include "BSBlueprintAccess.h"
#include "BSBlueprintSave.h"
#include "BSClipboard.h"
#include "BSBlueprintScale.h"
#include "BSBlueprintBounds.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGBlueprintSubsystem.h"
#include "FGActorSaveHeaderTypes.h"
#include "FGCharacterPlayer.h"
#include "FGHealthComponent.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Equipment/FGBuildGun.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "InstanceData.h"
#include "Net/UnrealNetwork.h"
#include "Patching/NativeHookManager.h"
#include "FGInventoryComponent.h"
#include "Buildables/FGBuildableFactory.h"
#include "UObject/UnrealType.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildableStorage.h"
#include "FGInventoryLibrary.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "FGBuildableSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
    thread_local TWeakObjectPtr<ABSBlueprintSaveSession> ActiveBlueprintWriter;
    thread_local bool BlueprintWriteSucceeded = false;
    using FExpectedBlueprintTransforms = TMap<FString, FTransform>;
    thread_local FExpectedBlueprintTransforms ExpectedBlueprintTransforms;
    struct FBlueprintStoredGoodsScope
    {
        struct FEntry { UFGInventoryComponent* Inventory; TArray<FInventoryStack> Stacks; };
        FArrayProperty* Property=nullptr;
        TArray<FEntry> Saved;
        bool Valid=true;
        explicit FBlueprintStoredGoodsScope(const TArray<AFGBuildable*>& Buildings)
        {
            Property=FindFProperty<FArrayProperty>(UFGInventoryComponent::StaticClass(),TEXT("mInventoryStacks"));
            auto* Inner=Property?CastField<FStructProperty>(Property->Inner):nullptr;
            if(!Inner || Inner->Struct!=FInventoryStack::StaticStruct()){Valid=false;return;}
            for(auto* Building:Buildings)if(IsValid(Building))
            {
                auto* Factory=Cast<AFGBuildableFactory>(Building);
                if(Factory && !BSFactoryAccess::Property(Factory, TEXT("mInventoryPotential"))){Valid=false;return;}
                for(auto* Inventory:TInlineComponentArray<UFGInventoryComponent*>(Building))
                {
                    // Shards and Somersloops configure the machine and remain
                    // real blueprint costs. Only stored/processed goods are omitted.
                    if(Factory && Inventory==BSFactoryAccess::Inventory(Factory, TEXT("mInventoryPotential")))continue;
                    auto* Slots=Property->ContainerPtrToValuePtr<TArray<FInventoryStack>>(Inventory);
                    auto& Entry=Saved.AddDefaulted_GetRef();Entry.Inventory=Inventory;Entry.Stacks=MoveTemp(*Slots);
                    Slots->SetNum(Entry.Stacks.Num());
                }
            }
        }
        ~FBlueprintStoredGoodsScope()
        {
            // Native saving is synchronous. Restore the exact arrays without
            // inventory events, replication dirtiness, refunds or production ticks.
            for(auto& Entry:Saved)if(IsValid(Entry.Inventory))
                *Property->ContainerPtrToValuePtr<TArray<FInventoryStack>>(Entry.Inventory)=MoveTemp(Entry.Stacks);
        }
        FBlueprintStoredGoodsScope(const FBlueprintStoredGoodsScope&)=delete;
        FBlueprintStoredGoodsScope& operator=(const FBlueprintStoredGoodsScope&)=delete;
    };
}

bool ABSBlueprintSaveSession::WriteCopy(const FBlueprintRecord& Record, FBlueprintSaveData& Out, FString& Error)
{
    // Construction copies omit goods just like saved selection blueprints.
    // Undo records call the low-level writer directly and retain their contents.
    FBlueprintStoredGoodsScope EmptyGoods(mBuildables);
    if (!EmptyGoods.Valid)
    { Error=TEXT("Cannot safely exclude stored goods from this copy."); return false; }
    return WriteBuildingSkillsClipboard(AFGBlueprintSubsystem::Get(GetWorld()), Record,
        BlueprintOrigin, mBuildables, mDimensions, Out);
}

ABSBlueprintSaveSession::ABSBlueprintSaveSession()
{
    mDisplayName = FText::FromString(TEXT("Save Selection as Blueprint"));
    bReplicates = true;
    bOnlyRelevantToOwner = true;
    // AFGBuildable defaults to DORM_Initial for map buildings. This dynamically
    // spawned interaction session must reach its owning client immediately.
    NetDormancy = DORM_Awake;
    SetReplicateMovement(false);
    SetActorEnableCollision(false);
    SetActorHiddenInGame(true);
    PrimaryActorTick.bCanEverTick = false;
}
void ABSBlueprintSaveSession::BeginPlay()
{
    // Do not register as a buildable/designer or create a platform and storage.
    AActor::BeginPlay();
    if (HasAuthority()) SetLifeSpan(1800);
}
void ABSBlueprintSaveSession::EndPlay(EEndPlayReason::Type Reason) { AActor::EndPlay(Reason); }
void ABSBlueprintSaveSession::Destroyed() { AActor::Destroyed(); }
void ABSBlueprintSaveSession::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ABSBlueprintSaveSession, RequestId);
}

bool ABSBlueprintSaveSession::Prepare(FString& Error)
{
    auto* Controller = Cast<AFGPlayerController>(GetOwner());
    auto* Player = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    if (!HasAuthority() || !Player || !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead() ||
        !Player->GetBuildGun() || Player->IsDrivingVehicle() || Player->IsInPortal() ||
        (!bOpened && RequireReach && (!Player->IsBuildGunEquipped() || !UBSEditModeComponent::IsEditDescriptor(Player->GetBuildGun()->GetCurrentBuildGunMode()))))
    { Error = TEXT("Open Edit to save or copy a selection."); return false; }
    if (!Groups || SourceSelection.Groups.Num() > 2048 || SourceSelection.Buildings.Num() > 2048)
    { Error = TEXT("The selection is unavailable or too large."); return false; }
    TArray<FBSBuildingRef> CurrentMembers;
    for (const auto& Member : SourceSelection.Buildings) CurrentMembers.AddUnique(Member);
    for (const FGuid Id : SourceSelection.Groups)
    {
        if (!FBSGroupTree::Find(Groups->Groups, Id))
        { Error = TEXT("A selected group changed. Select it again."); return false; }
        FBSGroupTree::Collect(Groups->Groups, Id, CurrentMembers);
    }
    for (const auto& Member : CurrentMembers)
        if (!Member.IsAlive(GetWorld()))
        { Error = TEXT("A selected building is no longer available."); return false; }
    if (bOpened)
    {
        bool Unchanged = CurrentMembers.Num() == Members.Num();
        for (const auto& Member : Members)
            Unchanged &= CurrentMembers.Contains(Member) && (!Member.Actor || Member.Actor->GetActorTransform().Equals(Member.Transform, .01));
        if (!Unchanged) { Error = TEXT("The selection changed. Close this window and select it again."); return false; }
    }
    Members = MoveTemp(CurrentMembers);
    for (auto& Member : Members) if (Member.Actor) Member.Transform = Member.Actor->GetActorTransform();
    if (Members.IsEmpty() || Members.Num() > 2048)
    { Error = TEXT("Select between 1 and 2048 buildings."); return false; }
    mBuildables.Reset();
    TArray<FVector> BoundsPoints;
    TArray<double> BuildingYaws;
    bool InReach = false;
    for (const auto& Member : Members)
    {
        if (!Member.IsAlive(GetWorld())) { Error = TEXT("A selected building is no longer available."); return false; }
        auto* Building = Member.ResolveActor(GetWorld());
        if (!IsValid(Building)) { Error = TEXT("A selected building could not be read."); return false; }
        mBuildables.AddUnique(Building);
        const int32 PreviousPoints = BoundsPoints.Num();
        FTransform MemberTransform = Building->GetActorTransform();
        if (Member.LightweightClass)
        {
            auto* Lightweight = AFGLightweightBuildableSubsystem::Get(GetWorld());
            const int32 Index = Member.ResolveIndex(GetWorld());
            const auto* Data = Lightweight && Index != INDEX_NONE
                ? Lightweight->GetRuntimeDataForBuildableClassAndIndex(Member.LightweightClass, Index) : nullptr;
            if (!Data) { Error = TEXT("A selected building is not available yet."); return false; }
            MemberTransform = Data->Transform;
        }
        BSBlueprintBounds::AppendSnappingGeometry(Building, MemberTransform, BoundsPoints);
        // Non-snapping helpers are still serialized, but must not change the
        // footprint or outvote physical buildings in automatic alignment.
        if (BoundsPoints.Num() != PreviousPoints) BuildingYaws.Add(MemberTransform.Rotator().Yaw);
        InReach |= FVector::DistSquared(Member.Location(), Player->GetActorLocation()) <= FMath::Square(Player->GetBuildGun()->GetBuildGunRange() + 1000.f);
    }
    if (RequireReach && !InReach) { Error = TEXT("Move within Build Gun range of the selection."); return false; }
    // A selection containing only non-snapping objects has no native snap box.
    // Keep it copyable without letting its helper geometry enlarge mixed selections.
    if (BoundsPoints.IsEmpty())
    {
        for (AFGBuildable* Building : mBuildables)
        {
            BSBlueprintBounds::AppendActorGeometry(Building, BoundsPoints);
            BuildingYaws.Add(Building->GetActorRotation().Yaw);
        }
    }
    FVector BoundsSize;
    const bool HasAnchor = SourceAnchor.Actor || SourceAnchor.LightweightClass;
    FQuat Rotation = FQuat::Identity;
    TArray<FVector> AnchorPoints;
    if (HasAnchor)
    {
        auto* Anchor = SourceAnchor.IsAlive(GetWorld()) ? SourceAnchor.ResolveActor(GetWorld()) : nullptr;
        if (!Anchor || FVector::DistSquared(Anchor->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(Player->GetBuildGun()->GetBuildGunRange() + 1000.f))
        { Error = TEXT("The blueprint anchor is unavailable or out of range. Choose it again."); return false; }
        Rotation = Anchor->GetActorQuat();
        if (bOpened && !Anchor->GetActorTransform().Equals(AnchorTransform, .01))
        { Error = TEXT("The blueprint anchor changed. Close the window and try again."); return false; }
        AnchorTransform = Anchor->GetActorTransform();
        BSBlueprintBounds::AppendSnappingGeometry(Anchor, AnchorTransform, AnchorPoints);
        if (AnchorPoints.IsEmpty()) AnchorPoints.Add(AnchorTransform.GetLocation());
    }
    if (!(HasAnchor ? BSBlueprintBounds::FitFrame(BoundsPoints, Rotation, BlueprintOrigin, BoundsSize, AnchorPoints)
        : BSBlueprintBounds::FitUpright(BoundsPoints, BuildingYaws, BlueprintOrigin, BoundsSize)))
    { Error = TEXT("The selection's bounds could not be calculated."); return false; }
    UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save bounds: members=%d reachable=%d size=%s center=%s yaw=%.3f"),
        Members.Num(), InReach, *BoundsSize.ToString(), *BlueprintOrigin.GetLocation().ToString(), BlueprintOrigin.Rotator().Yaw);
    // The native designer derives its archive origin from its actor transform
    // plus FLOOR_HEIGHT. Set that source of truth instead of relying on hooking
    // GetOffsetTransform, which can be inlined by the Linux compiler.
    if (auto* Root = GetRootComponent())
    {
        TGuardValue<TEnumAsByte<EComponentMobility::Type>> Mobility(Root->Mobility, EComponentMobility::Movable);
        SetActorTransform(FTransform(BlueprintOrigin.GetRotation(), BlueprintOrigin.GetLocation() - FVector(0, 0, FLOOR_HEIGHT)), false, nullptr, ETeleportType::TeleportPhysics);
    }
    // Ignore sub-micrometre rotation roundoff at exact native tile boundaries.
    const FVector Size = (BoundsSize - FVector(1.e-4)).ComponentMax(FVector::ZeroVector) / TILE_SIZE;
    mDimensions = FIntVector(FMath::Max(1, FMath::CeilToInt(Size.X)), FMath::Max(1, FMath::CeilToInt(Size.Y)), FMath::Max(1, FMath::CeilToInt(Size.Z)));
    mCurrentCost.Reset();
    {
        FBlueprintStoredGoodsScope EmptyGoods(mBuildables);
        if(!EmptyGoods.Valid){Error=TEXT("The inventory layout is unsupported; blueprint saving was cancelled.");return false;}
        if (auto* Blueprints = AFGBlueprintSubsystem::Get(GetWorld())) Blueprints->CalculateBlueprintCost(mBuildables, mCurrentCost);
    }
    return true;
}

void ABSBlueprintSaveSession::Notify(const FString& Message)
{
    if (auto* PC = Cast<AFGPlayerController>(GetOwner()))
        if (auto* RCO = PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>()) RCO->Client_BlueprintMessage(Message);
}

void ABSBlueprintSaveSession::InstallHooks()
{
    if (WITH_EDITOR) return;
    // SML 3.12.1's CL 520140 headers match the native interface layout.
    // The generated interface dispatch now reaches this proxy's overrides.
    SUBSCRIBE_METHOD(AFGBlueprintSubsystem::WriteBlueprintToDisk,
        [](auto& Scope, AFGBlueprintSubsystem* System, const FBlueprintRecord& Record)
        {
            if (!ActiveBlueprintWriter.IsValid()) return;
            // Inspect the completed native TOC. Inline header-serializer hooks
            // are not observable reliably in the Linux shipping executable.
            TArray<FActorSaveHeader> Headers;
            TSet<FString> SerializedBlueprintMembers;
            bool BlueprintContentsValid = RestoreBuildingSkillsBlueprintArchiveScales(System) &&
                ReadBuildingSkillsBlueprintHeaders(FBSBlueprintAccess::Data(System), Headers);
            for (const auto& Header : Headers)
                if (const auto* Expected = ExpectedBlueprintTransforms.Find(Header.ObjectHeader.Reference.ToString()))
                {
                    const FString Key = Header.ObjectHeader.Reference.ToString();
                    BlueprintContentsValid &= !SerializedBlueprintMembers.Contains(Key) && FTransform(Header.Transform).Equals(*Expected, .1);
                    SerializedBlueprintMembers.Add(Key);
                }
            if (!BlueprintContentsValid || SerializedBlueprintMembers.Num() != ExpectedBlueprintTransforms.Num() || ExpectedBlueprintTransforms.IsEmpty())
            {
                UE_LOG(LogBuildingSkills, Error, TEXT("Blueprint Save rejected invalid archive: expected=%d serialized=%d transformsValid=%d"),
                    ExpectedBlueprintTransforms.Num(), SerializedBlueprintMembers.Num(), BlueprintContentsValid);
                Scope.Override(false);
                return;
            }
            UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save archive validated: expected=%d serialized=%d"),
                ExpectedBlueprintTransforms.Num(), SerializedBlueprintMembers.Num());
            const bool Success = Scope(System, Record);
            BlueprintWriteSucceeded = Success;
        });
    SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::SaveBlueprint,
        [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, FBlueprintRecord Record, AFGPlayerController* Controller, bool UpdateAuthor)
        {
            auto* Session = Cast<ABSBlueprintSaveSession>(Designer);
            if (!Session) return;
            UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save dispatch: authority=%d owner=%d"), Session->HasAuthority(), Controller == Session->GetOwner());
            // SaveBlueprint is also the native client entry point: it constructs
            // category records and sends Server_SaveBlueprintInDesigner. Never
            // run authority-only Prepare before that forwarding branch.
            if (!Session->HasAuthority())
            {
                if (Controller != Session->GetOwner()) { Scope.Cancel(); return; }
                if (auto* Player = Cast<AFGCharacterPlayer>(Controller->GetPawn()))
                    if (auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>()) Edit->BlueprintMessage(TEXT("Saving blueprint on server..."));
                return;
            }
            FString Error;
            if (Controller != Session->GetOwner() || Record.BlueprintName.IsEmpty() || Record.BlueprintName.Len() > 200 ||
                AFGBlueprintSubsystem::SanitizeBlueprintFileName(Record.BlueprintName) != Record.BlueprintName || !Session->Prepare(Error))
            {
                Session->ShowSummaryOnly();
                Session->Notify(Error.IsEmpty() ? TEXT("Invalid blueprint save request.") : Error);
                Scope.Cancel(); return;
            }
            // The vanilla writer handles serialization, descriptors, file transfer,
            // categories and overwrite semantics. Only its source set/origin differ.
            struct FMembership
            {
                AFGBuildable* Building;
                AFGBuildableBlueprintDesigner* Designer;
                AFGBlueprintProxy* Proxy;
                bool Inside;
            };
            TArray<FMembership> Membership;
            TGuardValue<FExpectedBlueprintTransforms> ExpectedGuard(ExpectedBlueprintTransforms, {});
            for (AFGBuildable* Building : Session->mBuildables)
            {
                Membership.Add({Building, Building->mBlueprintDesigner, Building->mBlueprintProxy, Building->mReplicatedBuiltInsideBlueprintDesigner});
                ExpectedBlueprintTransforms.Add(FObjectReferenceDisc(Building).ToString(), Building->GetActorTransform().GetRelativeTransform(Session->BlueprintOrigin));
            }
            TGuardValue<TWeakObjectPtr<ABSBlueprintSaveSession>> Writer(ActiveBlueprintWriter, Session);
            BlueprintWriteSucceeded = false;
            FBlueprintStoredGoodsScope EmptyGoods(Session->mBuildables);
            if(!EmptyGoods.Valid){Session->Notify(TEXT("The inventory layout is unsupported; blueprint saving was cancelled."));Scope.Cancel();return;}
            Scope(Designer, Record, Controller, UpdateAuthor);
            for (const auto& Entry : Membership)
                if (IsValid(Entry.Building))
                {
                    Entry.Building->mBlueprintDesigner = Entry.Designer;
                    Entry.Building->mBlueprintProxy = Entry.Proxy;
                    Entry.Building->mReplicatedBuiltInsideBlueprintDesigner = Entry.Inside;
                }
            Session->ShowSummaryOnly();
            Session->ForceNetUpdate();
            Session->Notify(BlueprintWriteSucceeded ? FString::Printf(TEXT("Blueprint saved: %s"), *Record.BlueprintName)
                : TEXT("Blueprint could not be saved correctly. Check the server log; no successful save was confirmed."));
            UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint Save disk result: success=%d members=%d"), BlueprintWriteSucceeded, Membership.Num());
        });
    SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::DismantleCurrentBuildables,
        [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, AFGPlayerController*)
        { if (Cast<ABSBlueprintSaveSession>(Designer)) Scope.Cancel(); });
    SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::LoadBlueprintIntoDesigner,
        [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, UFGBlueprintDescriptor*, AFGPlayerController*)
        { if (Cast<ABSBlueprintSaveSession>(Designer)) Scope.Override(EBlueprintDesignerLoadResult::BDLR_Undefined); });
}

void UBSGroupRemoteCallObject::Server_OpenBlueprintSave_Implementation(const FBSSelection& Selection, FGuid RequestId, const FBSBuildingRef& Anchor)
{
    auto* Player = GetOwnerPlayerCharacter();
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    if (!PC || !RequestId.IsValid()) return;
    for (TActorIterator<ABSBlueprintSaveSession> It(GetWorld()); It; ++It)
        if (It->GetOwner() == PC) It->Destroy();
    FActorSpawnParameters Params;
    Params.Owner = PC; Params.ObjectFlags |= RF_Transient;
    auto* Session = GetWorld()->SpawnActor<ABSBlueprintSaveSession>(ABSBlueprintSaveSession::StaticClass(), Player->GetActorTransform(), Params);
    FString Error;
    if (Session)
    {
        Session->SourceSelection = Selection;
        Session->SourceAnchor = Anchor;
        if (Session->Prepare(Error))
        {
            Session->bOpened = true;
            Session->ShowSummaryOnly();
            Session->RequestId = RequestId;
            Session->ForceNetUpdate();
            // Actor creation and this RCO use different network channels. Send
            // identity, then let the client wait for the replicated actor.
            Client_OpenBlueprintSave(RequestId, TEXT(""));
            return;
        }
        Session->Destroy();
    }
    Client_OpenBlueprintSave(RequestId, Error.IsEmpty() ? TEXT("Could not open blueprint saving.") : Error);
}
void UBSGroupRemoteCallObject::Client_OpenBlueprintSave_Implementation(FGuid RequestId, const FString& Error)
{
    if (auto* Player = GetOwnerPlayerCharacter())
        if (auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>()) Edit->OnBlueprintSaveReply(RequestId, Error);
}
void UBSGroupRemoteCallObject::Server_CloseBlueprintSave_Implementation(FGuid RequestId)
{
    auto* Player = GetOwnerPlayerCharacter();
    if (!Player || !RequestId.IsValid()) return;
    for (TActorIterator<ABSBlueprintSaveSession> It(GetWorld()); It; ++It)
        if (It->RequestId == RequestId && It->GetOwner() == Player->GetController()) It->Destroy();
}
void UBSGroupRemoteCallObject::Client_BlueprintMessage_Implementation(const FString& Message)
{
    if (auto* Player = GetOwnerPlayerCharacter())
        if (auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>()) Edit->BlueprintMessage(Message);
}

bool RunBuildingSkillsBlueprintInventoryChecks(AFGCharacterPlayer* Player)
{
    check(FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsRuntimeTest")));
    auto* World=Player->GetWorld();auto* System=AFGBlueprintSubsystem::Get(World);
    auto* PC=Cast<AFGPlayerController>(Player->GetController());
    bool Passed=true;auto Check=[&](const TCHAR* Label,bool OK){Passed &= OK;UE_LOG(LogBuildingSkills,Display,TEXT("RUNTIME ASSERT: Blueprint inventories: %s: %s"),Label,OK?TEXT("PASS"):TEXT("FAIL"));};
    auto Item=[](const TCHAR* Path){return LoadClass<UFGItemDescriptor>(nullptr,Path);};
    auto Coal=Item(TEXT("/Game/FactoryGame/Resource/RawResources/Coal/Desc_Coal.Desc_Coal_C"));
    auto Ingot=Item(TEXT("/Game/FactoryGame/Resource/Parts/IronIngot/Desc_IronIngot.Desc_IronIngot_C"));
    auto Rod=Item(TEXT("/Game/FactoryGame/Resource/Parts/IronRod/Desc_IronRod.Desc_IronRod_C"));
    auto Shard=Item(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/Desc_CrystalShard.Desc_CrystalShard_C"));
    auto RodRecipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Constructor/Recipe_IronRod.Recipe_IronRod_C"));
    auto Spawn=[&](const TCHAR* Name,FVector Offset)->AFGBuildable*
    {
        auto Class=LoadClass<AFGBuildable>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/%s/Build_%s.Build_%s_C"),Name,Name,Name));
        auto Recipe=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_%s.Recipe_%s_C"),Name,Name));
        if(!Class || !Recipe)return nullptr;
        AFGRecipeManager::Get(World)->AddAvailableRecipe(Recipe);
        auto* B=AFGBuildableSubsystem::Get(World)->BeginSpawnBuildable(Class,FTransform(Player->GetActorLocation()+Offset));
        if(B){B->SetBuiltWithRecipe(Recipe);B->FinishSpawning(B->GetActorTransform());}return B;
    };
    auto* Storage=Cast<AFGBuildableStorage>(Spawn(TEXT("StorageContainerMk1"),FVector(1200,0,1800)));
    auto* Machine=Cast<AFGBuildableManufacturer>(Spawn(TEXT("ConstructorMk1"),FVector(3000,0,1800)));
    const bool Fixture=System && PC && Storage && Machine && Coal && Ingot && Rod && Shard && RodRecipe;
    Check(TEXT("storage, constructor and item fixtures load"),Fixture);if(!Fixture)return false;
    AFGRecipeManager::Get(World)->AddAvailableRecipe(RodRecipe);Machine->SetRecipe(RodRecipe);
    auto* Stored=BSFactoryAccess::Inventory(Storage, TEXT("mStorageInventory"));auto* Input=BSFactoryAccess::Inventory(Machine, TEXT("mInputInventory"));auto* Output=BSFactoryAccess::Inventory(Machine, TEXT("mOutputInventory"));auto* Upgrades=BSFactoryAccess::Inventory(Machine, TEXT("mInventoryPotential"));
    if(!Stored || !Input || !Output || !Upgrades){Check(TEXT("source inventories exist"),false);return false;}
    Check(TEXT("CL 520140 inventory getters match runtime-reflected identities"),
        Storage->GetStorageInventory()==Stored && Machine->GetInputInventory()==Input &&
        Machine->GetOutputInventory()==Output && Machine->GetPotentialInventory()==Upgrades);
    Check(TEXT("installed shard fixture is populated"),Upgrades->AddStackToIndex_Unsafe(0,FInventoryStack(1,Shard))==1);
    TArray<AFGBuildable*> Source{Storage,Machine};TArray<FItemAmount> ExpectedCost;
    System->CalculateBlueprintCost(Source,ExpectedCost);
    Check(TEXT("stored goods fixture is populated"),Stored->AddStack(FInventoryStack(73,Coal))==73
        && Input->AddStackToIndex_Unsafe(0,FInventoryStack(42,Ingot))==42 && Output->AddStackToIndex_Unsafe(0,FInventoryStack(17,Rod))==17);
    const int32 Slots=Stored->GetSizeLinear();
    FActorSpawnParameters Params;Params.Owner=PC;Params.ObjectFlags|=RF_Transient;
    auto* Session=World->SpawnActor<ABSBlueprintSaveSession>(ABSBlueprintSaveSession::StaticClass(),Player->GetActorTransform(),Params);
    if(!Session){Check(TEXT("save session spawns"),false);return false;}
    Session->SourceSelection.Buildings={FBSBuildingRef::FromActor(Storage),FBSBuildingRef::FromActor(Machine)};
    FBlueprintRecord Record;Record.BlueprintName=TEXT("BSRuntime_EmptyGoods_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Record.Category=System->GetCategoryForUndefined();Record.SubCategory=System->GetSubCategoryForUndefined();
    Session->SaveBlueprint(Record,PC,false);
    Check(TEXT("saving leaves original goods and installed upgrades untouched"),Stored->GetNumItems(Coal)==73 && Input->GetNumItems(Ingot)==42 && Output->GetNumItems(Rod)==17 && Upgrades->GetNumItems(Shard)==1 && Stored->GetSizeLinear()==Slots);
    // The test reads the file synchronously in the saving tick, before the
    // normal menu/catalog refresh. Populate native cached headers first.
    System->EnumerateBlueprints();
    auto* Descriptor=System->GetBlueprintDescriptorByNameString(Record.BlueprintName);
    const bool Written=Descriptor && System->ReadBlueprintFromDisc(Record.BlueprintName);
    Check(TEXT("selection save writes a readable native blueprint"),Written);
    if(Written)
    {
        TArray<FItemAmount> Cost;Descriptor->GetBlueprintCost(Cost);
        auto Amount=[](const TArray<FItemAmount>& Items,TSubclassOf<UFGItemDescriptor> Class){int32 Total=0;for(const auto& I:Items)if(I.ItemClass==Class)Total+=I.Amount;return Total;};
        bool SameCost=true;for(const auto& I:ExpectedCost)SameCost &= Amount(Cost,I.ItemClass)==Amount(ExpectedCost,I.ItemClass);
        for(const auto& I:Cost)SameCost &= Amount(Cost,I.ItemClass)==Amount(ExpectedCost,I.ItemClass);
        Check(TEXT("blueprint requires only buildings and installed upgrades, not stored goods"),SameCost && Amount(Cost,Coal)==0 && Amount(Cost,Ingot)==0 && Amount(Cost,Shard)==1);
        TArray<AFGBuildable*> Loaded;System->LoadStoredBlueprint(Descriptor,FTransform::Identity,Loaded,true);
        UE_LOG(LogBuildingSkills,Display,TEXT("BLUEPRINT INVENTORY CHECK: descriptor=%s cachedHeaders=%d loaded=%d"),*Descriptor->GetBlueprintNameAsString(),FBSBlueprintAccess::Headers(System).Num(),Loaded.Num());
        AFGBuildableStorage* NewStorage=nullptr;AFGBuildableManufacturer* NewMachine=nullptr;
        for(auto* B:Loaded){if(auto* S=Cast<AFGBuildableStorage>(B))NewStorage=S;if(auto* M=Cast<AFGBuildableManufacturer>(B))NewMachine=M;}
        Check(TEXT("saved storage and production inventories are empty but retain their slots"),NewStorage && NewMachine && BSFactoryAccess::Inventory(NewStorage, TEXT("mStorageInventory"))->IsEmpty()
            && BSFactoryAccess::Inventory(NewStorage, TEXT("mStorageInventory"))->GetSizeLinear()==Slots && BSFactoryAccess::Inventory(NewMachine, TEXT("mInputInventory"))->IsEmpty() && BSFactoryAccess::Inventory(NewMachine, TEXT("mOutputInventory"))->IsEmpty());
        Check(TEXT("saved machine keeps its recipe and installed upgrade"),NewMachine && BSFactoryAccess::Recipe(NewMachine)==RodRecipe && BSFactoryAccess::Inventory(NewMachine, TEXT("mInventoryPotential"))->GetNumItems(Shard)==1);
        System->DeleteBlueprintDescriptor(Descriptor);
    }
    auto* RCO=PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>();
    auto* Clipboard=RCO?NewObject<UBSClipboard>(RCO):nullptr;
    FString CopyError;
    if(Clipboard)Clipboard->RequestId=FGuid::NewGuid();
    const bool Captured=Clipboard && Clipboard->Capture(Session->SourceSelection,CopyError);
    Check(TEXT("Copy captures occupied storage and production buildings"),Captured);
    Check(TEXT("Copy leaves source goods and installed upgrades unchanged"),Stored->GetNumItems(Coal)==73
        && Input->GetNumItems(Ingot)==42 && Output->GetNumItems(Rod)==17 && Upgrades->GetNumItems(Shard)==1);
    if(Captured)
    {
        auto* Decoded=NewObject<UBSClipboard>(RCO);Decoded->RequestId=Clipboard->RequestId;Decoded->Bytes=Clipboard->Bytes;
        const bool DecodedOK=Decoded->Decode(CopyError);
        Check(TEXT("Copy inventory archive survives client transport"),DecodedOK);
        if(DecodedOK)
        {
            RCO->Clipboards.Add(Decoded);
            auto Amount=[](const TArray<FItemAmount>& Items,TSubclassOf<UFGItemDescriptor> Class){int32 Total=0;for(const auto& I:Items)if(I.ItemClass==Class)Total+=I.Amount;return Total;};
            TArray<FItemAmount> Cost;Decoded->Descriptor->GetBlueprintCost(Cost);
            bool SameCost=true;for(const auto& I:ExpectedCost)SameCost &= Amount(Cost,I.ItemClass)==Amount(ExpectedCost,I.ItemClass);
            for(const auto& I:Cost)SameCost &= Amount(Cost,I.ItemClass)==Amount(ExpectedCost,I.ItemClass);
            Check(TEXT("Copy cost excludes goods and retains installed upgrades"),SameCost && Amount(Cost,Coal)==0 && Amount(Cost,Ingot)==0 && Amount(Cost,Shard)==1);
            for(bool Preview:{true,false})
            {
                // The client previews decoded bytes; authority places its
                // original clipboard with the owning player as instigator.
                if(!Preview){RCO->Clipboards.Remove(Decoded);RCO->Clipboards.Add(Clipboard);}
                TArray<AFGBuildable*> Loaded;
                System->LoadStoredBlueprint((Preview?Decoded:Clipboard)->Descriptor,
                    FTransform(Player->GetActorLocation()+FVector(-6000,4000,1800)),Loaded,Preview,nullptr,Player);
                AFGBuildableStorage* CopyStorage=nullptr;AFGBuildableManufacturer* CopyMachine=nullptr;
                for(auto* B:Loaded){if(auto* S=Cast<AFGBuildableStorage>(B))CopyStorage=S;if(auto* M=Cast<AFGBuildableManufacturer>(B))CopyMachine=M;}
                Check(Preview?TEXT("Copy preview has empty goods, original slots, recipe and installed upgrade"):
                    TEXT("Placed Copy has empty goods, original slots, recipe and installed upgrade"),
                    CopyStorage && CopyMachine && BSFactoryAccess::Inventory(CopyStorage, TEXT("mStorageInventory"))->IsEmpty() && BSFactoryAccess::Inventory(CopyStorage, TEXT("mStorageInventory"))->GetSizeLinear()==Slots
                    && BSFactoryAccess::Inventory(CopyMachine, TEXT("mInputInventory"))->IsEmpty() && BSFactoryAccess::Inventory(CopyMachine, TEXT("mOutputInventory"))->IsEmpty()
                    && BSFactoryAccess::Recipe(CopyMachine)==RodRecipe && BSFactoryAccess::Inventory(CopyMachine, TEXT("mInventoryPotential"))->GetNumItems(Shard)==1);
            }
            RCO->Clipboards.Remove(Decoded);
            RCO->Clipboards.Remove(Clipboard);
        }
    }
    Session->ShowSummaryOnly();Session->Destroy();return Passed;
}
