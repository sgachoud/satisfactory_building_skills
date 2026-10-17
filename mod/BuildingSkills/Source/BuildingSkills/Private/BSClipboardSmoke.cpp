#include "BSClipboard.h"
#include "BSDeduplication.h"
#include "BSEditModeComponent.h"
#include "HAL/FileManager.h"
#include "BSBlueprintSave.h"
#include "BSBlueprintBounds.h"
#include "BuildingSkills.h"
#include "FGBlueprintSubsystem.h"
#include "FGBlueprintSettings.h"
#include "FGBuildableSubsystem.h"
#include "FGInventoryComponent.h"
#include "Buildables/FGBuildableStorage.h"
#include "FGDismantleInterface.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGBlueprintProxy.h"
#include "Hologram/FGBlueprintHologram.h"
#include "FGCharacterPlayer.h"
#include "FGRecipeManager.h"
#include "FGRecipe.h"
#include "FGPlayerController.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFoundation.h"

bool RunBuildingSkillsClipboardChecks(AFGCharacterPlayer* Player, const FBSSelection& Selection)
{
    check(FParse::Param(FCommandLine::Get(), TEXT("BuildingSkillsRuntimeTest")));
    auto* PC = Cast<AFGPlayerController>(Player->GetController());
    auto* RCO = PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    auto* Groups = ABSGroupSubsystem::Get(Player->GetWorld());
    auto* System = AFGBlueprintSubsystem::Get(Player->GetWorld());
    if (!RCO || !Groups || !System) return false;
    bool Passed = true;
    auto Check = [&](const TCHAR* Label, bool OK)
    { UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME ASSERT: %s: %s"), Label, OK ? TEXT("PASS") : TEXT("FAIL")); Passed &= OK; };
    TArray<FBSBuildingRef> Members = Selection.Buildings;
    for (auto Id : Selection.Groups) FBSGroupTree::Collect(Groups->Groups, Id, Members);
    if (Members.Num() < 3) { Check(TEXT("Copy fixture has three members"), false); return false; }

    // General Save must accept individual buildings and deduplicate mixed selections.
    FActorSpawnParameters Params; Params.Owner = PC; Params.ObjectFlags |= RF_Transient;
    auto* Session = Player->GetWorld()->SpawnActor<ABSBlueprintSaveSession>(ABSBlueprintSaveSession::StaticClass(), Player->GetActorTransform(), Params);
    FString Error;
    if (!Session) return false;
    Session->SourceSelection.Buildings = Members;
    Check(TEXT("Save accepts an ungrouped selection"), Session->Prepare(Error) && Session->Members.Num() == Members.Num());
    Session->SourceSelection = Selection;
    Session->SourceSelection.Buildings.Append(Members);
    Check(TEXT("Save deduplicates mixed individual and group selections"), Session->Prepare(Error) && Session->Members.Num() == Members.Num());
    Session->SourceSelection = {};
    Session->SourceSelection.Buildings = {Members[0], Members[1]};
    Session->SourceAnchor = Members[2];
    const FQuat AnchorRotation = Members[2].ResolveActor(Player->GetWorld())->GetActorQuat();
    Check(TEXT("Save uses an external anchor's axes without including it"), Session->Prepare(Error) &&
        Session->Members.Num() == 2 && !Session->Members.Contains(Members[2]) && Session->BlueprintOrigin.GetRotation().Equals(AnchorRotation, .0001));
    Session->SourceSelection.Buildings.Add(Members[2]);
    Check(TEXT("Save includes the anchor only when selected"), Session->Prepare(Error) && Session->Members.Num() == 3);
    Session->Destroy();

    // Use a nested group plus one loose object. Restore the fixture hierarchy on exit.
    TGuardValue<TArray<FBSGroup>> RestoreTree(Groups->Groups, {});
    FBSGroup Root, Child;
    Root.Id = FGuid::NewGuid(); Root.Name = TEXT("Copy parent"); Root.Buildings.Add(Members[0]);
    Child.Id = FGuid::NewGuid(); Child.Name = TEXT("Copy child"); Child.Parent = Root.Id; Child.Buildings.Add(Members[1]);
    Groups->Groups = {Root, Child};
    FBSSelection Source; Source.Groups.Add(Root.Id); Source.Buildings.Add(Members[2]);
    if (auto* Recipes = AFGRecipeManager::Get(Player->GetWorld()))
        for (const auto& Member : Members)
            if (auto* Actor = Member.ResolveActor(Player->GetWorld())) Recipes->AddAvailableRecipe(Actor->GetBuiltWithRecipe());
    auto* Anchored = NewObject<UBSClipboard>(RCO); Anchored->RequestId = FGuid::NewGuid();
    FBSSelection AnchoredSelection; AnchoredSelection.Buildings = {Members[0], Members[1]};
    Check(TEXT("Copy validates archive poses against an excluded anchor's frame"), Anchored->Capture(AnchoredSelection, Error, Members[2]) &&
        Anchored->Members.Num() == 2 && !Anchored->Members.Contains(Members[2]) && Anchored->SourceOrigin.GetRotation().Equals(AnchorRotation, .0001));
    // An excluded low anchor must survive native hologram loading, including
    // the intentional empty vertical space underneath the copied geometry.
    auto* AnchorSource = Members[0].ResolveActor(Player->GetWorld());
    const FTransform LowPose(AnchorSource->GetActorQuat(), AnchorSource->GetActorLocation() - AnchorSource->GetActorUpVector() * 2000);
    auto* AnchorClass = LoadClass<AFGBuildable>(nullptr, TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C"));
    auto* LowAnchor = Player->GetWorld()->SpawnActor<AFGBuildable>(AnchorClass, LowPose, Params);
    auto* Floating = NewObject<UBSClipboard>(RCO); Floating->RequestId = FGuid::NewGuid();
    if (LowAnchor && Floating->Capture(AnchoredSelection, Error, FBSBuildingRef::FromActor(LowAnchor)))
    {
        RCO->Clipboards.Add(Floating);
        auto* FloatHolo = Player->GetWorld()->SpawnActorDeferred<AFGBlueprintHologram>(AFGBlueprintHologram::StaticClass(), FTransform::Identity, Player, Player);
        FloatHolo->SetRecipe(AnchorSource->GetBuiltWithRecipe());
        FloatHolo->SetBuildClass(AnchorSource->GetClass());
        FloatHolo->SetConstructionInstigator(Player);
        FloatHolo->SetBlueprintDescriptor(Floating->Descriptor);
        FloatHolo->FinishSpawning(FTransform::Identity);
        const FBox Geometry = FBSBlueprintPlacement::GeometryBounds(FloatHolo);
        const FBox Bounds = FBSBlueprintPlacement::PlacementBounds(FloatHolo);
        Check(TEXT("Excluded lower anchor preserves a gap below native hologram contents"), Geometry.IsValid && Geometry.Min.Z > 1000);
        Check(TEXT("Native hologram bounding box includes the excluded anchor floor"), Bounds.IsValid && FMath::IsNearlyZero(Bounds.Min.Z, .1) && Bounds.Max.Z >= Geometry.Max.Z - .1);
        if (auto* Support = Cast<AFGBuildableFoundation>(AnchorSource))
        {
            const FTransform Frame = Support->GetActorTransform();
            const FVector Point = Frame.TransformPosition(FVector(0, 0, Support->mHeight / 2));
            const FVector Normal = Frame.GetRotation().GetAxisZ();
            FHitResult Hit(Support, Cast<UPrimitiveComponent>(Support->GetRootComponent()), Point, Normal);
            Hit.bBlockingHit = true; Hit.Location = Hit.ImpactPoint = Point; Hit.Normal = Hit.ImpactNormal = Normal;
            FloatHolo->SetActorRotation(Frame.Rotator()); FloatHolo->SetActorLocation(Point);
            const bool Snapped = FloatHolo->TrySnapToActor(Hit);
            Check(TEXT("Foundation snapping retains the floor gap chosen by the excluded anchor"), Snapped &&
                FMath::IsNearlyEqual(Frame.InverseTransformPosition(FloatHolo->GetActorLocation()).Z, double(Support->mHeight / 2), .1));
        }
        FloatHolo->Destroy();
        RCO->Clipboards.Remove(Floating);
    }
    else { Check(TEXT("Lower excluded anchor captures a floating selection"), false); UE_LOG(LogBuildingSkills, Error, TEXT("Lower anchor fixture: %s"), *Error); }
    if (LowAnchor) LowAnchor->Destroy();
    auto* Clipboard = NewObject<UBSClipboard>(RCO); Clipboard->RequestId = FGuid::NewGuid();
    const bool Captured = Clipboard->Capture(Source, Error);
    Check(TEXT("Server captures a nested group and loose building"), Captured && Clipboard->Hierarchy.Num() == 2);
    if (!Captured) { UE_LOG(LogBuildingSkills, Error, TEXT("Copy capture: %s"), *Error); return false; }
    const FString ClipboardFile = System->GetSessionBlueprintPath() / Clipboard->Name();
    Check(TEXT("Copy creates no blueprint or configuration file"),
        !IFileManager::Get().FileExists(*(ClipboardFile + TEXT(".sbp"))) &&
        !IFileManager::Get().FileExists(*(ClipboardFile + TEXT(".sbpcfg"))));
    RCO->Clipboards.Add(Clipboard);
    auto* Decoded = NewObject<UBSClipboard>(RCO);
    Decoded->RequestId = Clipboard->RequestId; Decoded->Bytes = Clipboard->Bytes;
    Check(TEXT("Clipboard transport preserves native archive bytes"), Decoded->Decode(Error) &&
        Decoded->Data.TOCData == Clipboard->Data.TOCData && Decoded->Data.BlobData == Clipboard->Data.BlobData);
    TArray<UFGBlueprintDescriptor*> Library;
    System->GetBlueprintDescriptors_Internal(Library);
    Check(TEXT("Copy descriptor is absent from native blueprint library"), !Library.Contains(Clipboard->Descriptor));
    TArray<AFGBuildable*> Preview;
    System->LoadStoredBlueprint(Clipboard->Descriptor, FTransform::Identity, Preview, true);
    Check(TEXT("Clipboard loads three preview buildings without writing a blueprint"), Preview.Num() == 3);
    const FTransform Placement(FRotator(0, 90, 0), Player->GetActorLocation() + FVector(6000, 6000, 3000));
    auto* Hologram = Player->GetWorld()->SpawnActorDeferred<AFGBlueprintHologram>(AFGBlueprintHologram::StaticClass(), FTransform::Identity, Player, Player);
    if (!Hologram || Preview.IsEmpty()) { RCO->Clipboards.Remove(Clipboard); return false; }
    Hologram->SetRecipe(Preview[0]->GetBuiltWithRecipe());
    Hologram->SetBuildClass(Preview[0]->GetClass());
    Hologram->SetConstructionInstigator(Player);
    Hologram->SetBlueprintDescriptor(Clipboard->Descriptor);
    Hologram->FinishSpawning(FTransform::Identity);
    Check(TEXT("Copy produces a native hologram with all three members"), Hologram->mBuildableToNewRoot.Num() == 3);
    auto* LiveWorld = Player->GetWorld();
    auto* LiveState = LiveWorld->GetGameState();
    auto* LiveRecipes = AFGRecipeManager::Get(LiveWorld);
    bool CostsPresent = !Hologram->mBuildableToNewRoot.IsEmpty();
    bool ContextRestored = true;
    for (const auto& Pair : Hologram->mBuildableToNewRoot)
    {
        auto* PreviewWorld = Pair.Key->GetWorld();
        auto* OriginalState = PreviewWorld->GetGameState();
        auto* OriginalRecipes = AFGRecipeManager::Get(PreviewWorld);
        TArray<FItemAmount> MemberCost;
        UBSClipboard::PreviewMemberCost(System, Pair.Key, MemberCost);
        CostsPresent &= !MemberCost.IsEmpty();
        ContextRestored &= PreviewWorld->GetGameState() == OriginalState &&
            AFGRecipeManager::Get(PreviewWorld) == OriginalRecipes &&
            LiveWorld->GetGameState() == LiveState && AFGRecipeManager::Get(LiveWorld) == LiveRecipes;
    }
    Check(TEXT("Native recipe lookup prices every preview member without a global hook"), CostsPresent);
    Check(TEXT("Pricing restores preview context and leaves live recipe lookup unchanged"), ContextRestored);
    Hologram->SetActorTransform(Placement);
    // Construction ownership uses the native construction pawn, not Actor.Instigator.
    Hologram->SetInstigator(nullptr);
    TArray<AActor*> Children;
    auto* Proxy = Cast<AFGBlueprintProxy>(Hologram->Construct(Children, FNetConstructionID()));
    int32 Count = Proxy ? Proxy->GetBuildables().Num() : 0;
    if (Proxy) for (const auto& Bundle : Proxy->GetLightweightClassAndIndices()) Count += Bundle.Indices.Num();
    Check(TEXT("Clipboard hologram constructs exactly three buildings"), Proxy && Count == 3);
    Hologram->Destroy();
    Check(TEXT("Copy restores nested groups with fresh identities"), Groups->Groups.Num() == 4 &&
        Groups->Groups[2].Id != Root.Id && Groups->Groups[3].Id != Child.Id &&
        Groups->Groups[3].Parent == Groups->Groups[2].Id && FBSGroupTree::Validate(Groups->Groups));
    TArray<FBSBuildingRef> CopiedMembers;
    if (Groups->Groups.Num() == 4) FBSGroupTree::Collect(Groups->Groups, Groups->Groups[2].Id, CopiedMembers);
    bool Independent = CopiedMembers.Num() == 2;
    for (const auto& Member : CopiedMembers) Independent &= Member.IsAlive(Player->GetWorld()) && !Members.Contains(Member);
    Check(TEXT("Copied group references new buildings only; loose object stays ungrouped"), Independent);
    bool Unchanged = true;
    for (const auto& Member : Members) Unchanged &= Member.IsAlive(Player->GetWorld()) &&
        (!Member.Actor || Member.Actor->GetActorTransform().Equals(Member.Transform, .01));
    Check(TEXT("Copy leaves original buildings unchanged"), Unchanged);
    if (auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>())
    {
        auto SpawnCopy = [&]()
        {
            auto* Next = Player->GetWorld()->SpawnActorDeferred<AFGBlueprintHologram>(AFGBlueprintHologram::StaticClass(), FTransform::Identity, Player, Player);
            Next->SetRecipe(UFGBlueprintSettings::Get()->mBlueprintRecipeClass.LoadSynchronous()); Next->SetBuildClass(Preview[0]->GetClass());
            Next->SetConstructionInstigator(Player); Next->SetBlueprintDescriptor(Clipboard->Descriptor);
            Next->FinishSpawning(FTransform::Identity); Next->SetActorTransform(Placement);
            return Next;
        };
        auto CountProxy = [](AFGBlueprintProxy* P)
        { int32 N = P ? P->GetBuildables().Num() : 0; if (P) for (const auto& Bundle : P->GetLightweightClassAndIndices()) N += Bundle.Indices.Num(); return N; };
        TGuardValue<EBSDeduplication> ModeGuard(Edit->Deduplication, EBSDeduplication::Skip);
        auto* SkipHolo = SpawnCopy();
        TMap<AFGBuildable*,bool> PreviewVisibility;
        for (const auto& Pair : SkipHolo->mBuildableToNewRoot) PreviewVisibility.Add(Pair.Key,Pair.Key->IsHidden());
        Check(TEXT("Skip charges no ingredients when all three objects already exist"), SkipHolo->GetBaseCost().IsEmpty());
        bool SameVisibility=true; for (const auto& Pair : PreviewVisibility) SameVisibility &= Pair.Key->IsHidden()==Pair.Value;
        Check(TEXT("Cost calculation leaves preview visibility unchanged"),SameVisibility);
        TArray<AActor*> SkippedChildren;
        auto* SkippedProxy = Cast<AFGBlueprintProxy>(SkipHolo->Construct(SkippedChildren, FNetConstructionID()));
        Check(TEXT("Skip constructs no duplicate members"), CountProxy(SkippedProxy) == 0);
        bool OriginalsSurvive = true;
        for (const auto& Member : CopiedMembers) OriginalsSurvive &= Member.IsAlive(Player->GetWorld());
        Check(TEXT("Skip preserves existing copied members"), OriginalsSurvive);
        SkipHolo->Destroy();
        Edit->Deduplication = EBSDeduplication::Replace;
        auto* ReplaceHolo = SpawnCopy();
        TArray<AActor*> ReplacedChildren;
        auto* ReplacedProxy = Cast<AFGBlueprintProxy>(ReplaceHolo->Construct(ReplacedChildren, FNetConstructionID()));
        Check(TEXT("Replace constructs three blueprint members"), CountProxy(ReplacedProxy) == 3);
        Check(TEXT("Replace clears the previous proxy members"), CountProxy(Proxy) == 0);
        ReplaceHolo->Destroy();
        Check(TEXT("Replacement restores a valid copied hierarchy after removing old members"),FBSGroupTree::Validate(Groups->Groups));
        // Remove one member, leaving a partial overlap for the next placement.
        AFGBuildable* Removed=nullptr;
        if (ReplacedProxy && !ReplacedProxy->GetBuildables().IsEmpty()) Removed=ReplacedProxy->GetBuildables()[0];
        if (!Removed && ReplacedProxy)
            for (const auto& Bundle : ReplacedProxy->GetLightweightClassAndIndices())
                if (!Bundle.Indices.IsEmpty())
                {
                    FLightweightBuildableInstanceRef Ref; Ref.Initialize(AFGLightweightBuildableSubsystem::Get(Player->GetWorld()),Bundle.BuildableClass,Bundle.Indices[0]);
                    Removed=Ref.SpawnTemporaryBuildable(); break;
                }
        Check(TEXT("Partial overlap fixture resolves one replacement"),Removed!=nullptr);
        if (Removed)
        {
            const auto ExpectedCost=UFGRecipe::GetIngredients(Player,Removed->GetBuiltWithRecipe());
            IFGDismantleInterface::Execute_Dismantle(Removed);
            Edit->Deduplication=EBSDeduplication::Skip;
            auto* PartialHolo=SpawnCopy();
            const auto PartialCost=PartialHolo->GetBaseCost();
            bool CorrectCost=!PartialCost.IsEmpty() && PartialCost.Num()==ExpectedCost.Num();
            for (const auto& Expected : ExpectedCost)
            { int32 Amount=0; for (const auto& Actual : PartialCost) if (Actual.ItemClass==Expected.ItemClass) Amount+=Actual.Amount; CorrectCost &= Amount==Expected.Amount; }
            Check(TEXT("Partial Skip charges exactly the one missing member"),CorrectCost);
            TArray<AActor*> PartialChildren;
            auto* PartialProxy=Cast<AFGBlueprintProxy>(PartialHolo->Construct(PartialChildren,FNetConstructionID()));
            Check(TEXT("Partial Skip constructs only the one missing member"),CountProxy(PartialProxy)==1 && CountProxy(ReplacedProxy)==2);
            Check(TEXT("Partial Skip prunes empty copied groups and retains a valid hierarchy"),FBSGroupTree::Validate(Groups->Groups));
            PartialHolo->Destroy();
        }
        // Actor geometry and stored inventory costs must work as well as LW foundations.
        auto* Factory=AFGBuildableSubsystem::Get(Player->GetWorld());
        auto SpawnMachine=[&](const TCHAR* Directory,const TCHAR* Name,const TCHAR* RecipeName,const FVector& Offset)
        {
            auto* Class=LoadClass<AFGBuildable>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/%s/%s.%s_C"),Directory,Name,Name));
            auto* Recipe=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/%s.%s_C"),RecipeName,RecipeName));
            auto* Actor=Factory && Class && Recipe ? Factory->BeginSpawnBuildable(Class,FTransform(Player->GetActorLocation()+FVector(-6000,4000,2000)+Offset)) : nullptr;
            if (Actor)
            { Actor->SetBuiltWithRecipe(Recipe); AFGRecipeManager::Get(Player->GetWorld())->AddAvailableRecipe(Recipe); Actor->FinishSpawning(Actor->GetActorTransform()); }
            return Actor;
        };
        auto* Constructor=SpawnMachine(TEXT("ConstructorMk1"),TEXT("Build_ConstructorMk1"),TEXT("Recipe_ConstructorMk1"),{});
        auto* Storage=Cast<AFGBuildableStorage>(SpawnMachine(TEXT("StorageContainerMk1"),TEXT("Build_StorageContainerMk1"),TEXT("Recipe_StorageContainerMk1"),FVector(1600,0,0)));
        auto* Plate=LoadClass<UFGItemDescriptor>(nullptr,TEXT("/Game/FactoryGame/Resource/Parts/IronPlate/Desc_IronPlate.Desc_IronPlate_C"));
        Check(TEXT("Actor deduplication fixture assets load"),Constructor && Storage && Plate && Storage->GetStorageInventory());
        if (Constructor && Storage && Plate && Storage->GetStorageInventory())
        {
            Storage->GetStorageInventory()->AddStack(FInventoryStack(7,Plate));
            FBSSelection ActorSelection; ActorSelection.Buildings={FBSBuildingRef::FromActor(Constructor),FBSBuildingRef::FromActor(Storage)};
            auto* ActorClipboard=NewObject<UBSClipboard>(RCO); ActorClipboard->RequestId=FGuid::NewGuid();
            const bool ActorCaptured=ActorClipboard->Capture(ActorSelection,Error);
            Check(TEXT("Actor deduplication captures constructor and stocked storage"),ActorCaptured);
            if (ActorCaptured)
            {
                RCO->Clipboards.Add(ActorClipboard);
                AFGBlueprintProxy* ActorProxy=nullptr;
                for (int32 Pass=0; Pass<2; ++Pass)
                {
                    Edit->Deduplication=Pass==0 ? EBSDeduplication::Off : EBSDeduplication::Skip;
                    auto* ActorHolo=Player->GetWorld()->SpawnActorDeferred<AFGBlueprintHologram>(AFGBlueprintHologram::StaticClass(),FTransform::Identity,Player,Player);
                    ActorHolo->SetRecipe(UFGBlueprintSettings::Get()->mBlueprintRecipeClass.LoadSynchronous()); ActorHolo->SetBuildClass(Constructor->GetClass());
                    ActorHolo->SetConstructionInstigator(Player); ActorHolo->SetBlueprintDescriptor(ActorClipboard->Descriptor); ActorHolo->FinishSpawning(FTransform::Identity);
                    ActorHolo->SetActorTransform(FTransform(Player->GetActorLocation()+FVector(-6000,9000,2000)));
                    if (Pass==1) Check(TEXT("Skip charges no machine, storage or stored-item ingredients for actor duplicates"),ActorHolo->GetBaseCost().IsEmpty());
                    TArray<AActor*> ActorChildren;
                    auto* Built=Cast<AFGBlueprintProxy>(ActorHolo->Construct(ActorChildren,FNetConstructionID()));
                    if (Pass==0) { ActorProxy=Built; Check(TEXT("Actor fixture constructs constructor and storage"),CountProxy(Built)==2); }
                    else Check(TEXT("Skip preserves two existing actor members without duplicating them"),CountProxy(Built)==0 && CountProxy(ActorProxy)==2);
                    ActorHolo->Destroy();
                }
                Check(TEXT("Actor Skip leaves source storage contents untouched"),Storage->GetStorageInventory()->GetNumItems(Plate)==7);
                RCO->Clipboards.Remove(ActorClipboard);
            }
        }
        auto* Standard=LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x1_01.Build_Foundation_8x1_01_C"));
        auto* Concrete=LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Building/Foundation/ConcreteSet/Build_Foundation_Concrete_8x1.Build_Foundation_Concrete_8x1_C"));
        auto* Tall=LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Building/Foundation/ConcreteSet/Build_Foundation_Concrete_8x4.Build_Foundation_Concrete_8x4_C"));
        Check(TEXT("Material variants match symmetrically"),Standard && Concrete && FBSDeduplication::SameType(Player->GetWorld(),Standard,Concrete) && FBSDeduplication::SameType(Player->GetWorld(),Concrete,Standard));
        Check(TEXT("Different foundation heights do not match"),Tall && !FBSDeduplication::SameType(Player->GetWorld(),Standard,Tall));

    }
    RCO->Clipboards.Remove(Clipboard);
    return Passed;
}
