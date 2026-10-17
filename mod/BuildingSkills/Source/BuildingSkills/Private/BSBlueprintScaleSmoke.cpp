#include "BSBlueprintScale.h"
#include "BSBlueprintBounds.h"
#include "BSGroups.h"
#include "BuildingSkills.h"
#include "FGBlueprintSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGRecipe.h"
#include "FGBlueprintProxy.h"
#include "Components/BoxComponent.h"
#include "Hologram/FGBlueprintHologram.h"
#include "FGActorSaveHeaderTypes.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

bool RunBuildingSkillsBlueprintScaleChecks(AFGCharacterPlayer* Player)
{
    auto* World = Player->GetWorld();
    auto* System = AFGBlueprintSubsystem::Get(World);
    auto* Buildables = AFGBuildableSubsystem::Get(World);
    auto* LW = AFGLightweightBuildableSubsystem::Get(World);
    bool Passed = true;
    auto Check = [&](const TCHAR* Name, bool Value)
    {
        Passed &= Value;
        UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME ASSERT: %s: %s"), Name, Value ? TEXT("PASS") : TEXT("FAIL"));
    };
    if (!System || !Buildables || !LW) return false;

    const FVector Scales[] = { FVector(.5), FVector(1.5, .75, 2), FVector::OneVector };
    const FTransform Origin(FRotator(0, 30, 0), Player->GetActorLocation() + FVector(2500, 0, 1800));
    auto* StorageClass = LoadClass<AFGBuildable>(nullptr, TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C"));
    auto* StorageRecipe = LoadClass<UFGRecipe>(nullptr, TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_StorageContainerMk1.Recipe_StorageContainerMk1_C"));
    auto* FoundationClass = LoadClass<AFGBuildable>(nullptr, TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x4_01.Build_Foundation_8x4_01_C"));
    auto* FoundationRecipe = LoadClass<UFGRecipe>(nullptr, TEXT("/Game/FactoryGame/Recipes/Buildings/Foundations/Recipe_Foundation_8x4_01.Recipe_Foundation_8x4_01_C"));
    if (!StorageClass || !StorageRecipe || !FoundationClass || !FoundationRecipe) return false;
    TArray<AFGBuildable*> Source;
    TArray<FTransform> Before;
    for (int32 I = 0; I < 3; ++I)
    {
        const FTransform Transform = FTransform(FRotator(0, I * 45, 0), FVector(0, I * 1800, 0), Scales[I]) * Origin;
        AFGBuildable* Actor = nullptr;
        if (I == 1)
        {
            FRuntimeBuildableInstanceData Data;
            Data.Transform = Transform; Data.BuiltWithRecipe = FoundationRecipe;
            // Real construction supplies a swatch. A bare synthetic instance has
            // none and invokes legacy color-slot migration when placed from disk.
            Data.CustomizationData.SwatchDesc = FoundationClass->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(World);
            Check(TEXT("Lightweight blueprint fixture has native construction customization"), Data.CustomizationData.SwatchDesc != nullptr);
            if (!Data.CustomizationData.SwatchDesc) return false;
            const int32 Index = LW->AddFromBuildableInstanceData(FoundationClass, Data);
            Actor = FBSBuildingRef::FromLightweight(World, FoundationClass, Index).ResolveActor(World);
        }
        else
        {
            Actor = Buildables->BeginSpawnBuildable(StorageClass, Transform);
            if (Actor) { Actor->SetBuiltWithRecipe(StorageRecipe); Actor->FinishSpawning(Transform); }
        }
        if (Actor) { Source.Add(Actor); Before.Add(Actor->GetActorTransform()); }
    }
    Check(TEXT("Scale fixture contains uniform, nonuniform lightweight and unit-scale buildings"), Source.Num() == 3);
    if (Source.Num() != 3) return false;

    FBlueprintRecord Record;
    Record.BlueprintName = TEXT("BuildingSkills_Scale_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Record.Category = System->GetCategoryForUndefined();
    Record.SubCategory = System->GetSubCategoryForUndefined();
    auto Header = WriteBuildingSkillsBlueprintForTest(System, Record, Origin, Source);
    const bool Written = System->WriteBlueprintToDisk(Record);
    Check(TEXT("Scaled blueprint uses the native disk writer"), Written);
    // The ordinary designer Save also registers its header. This direct native
    // archive fixture must enumerate the written file before attempting a load.
    if (Written) System->EnumerateBlueprints();
    auto* Descriptor = Written ? System->CreateBlueprintDescriptor(Record, Header) : nullptr;
    if (!Descriptor) return false;
    bool Unchanged = true;
    for (int32 I = 0; I < Source.Num(); ++I) Unchanged &= Source[I]->GetActorTransform().Equals(Before[I], .001);
    Check(TEXT("Saving custom scales leaves source transforms unchanged"), Unchanged);
    const bool Read = System->ReadBlueprintFromDisc(Record.BlueprintName);
    Check(TEXT("Scaled blueprint reads back from disk in native format"), Read);
    if (!Read) { System->DeleteBlueprintDescriptor(Descriptor); return false; }

    auto Verify = [&](const TArray<AFGBuildable*>& Loaded, const FTransform& Placement, bool Vanilla, bool InWorld = false)
    {
        if (Loaded.Num() != Source.Num()) return false;
        bool Result = true;
        for (int32 I = 0; I < Source.Num(); ++I)
        {
            FTransform Expected = Before[I].GetRelativeTransform(Origin) * Placement;
            if (Vanilla) Expected.SetScale3D(FVector::OneVector);
            if (InWorld && I == 1)
            {
                // BeginPlay converts the foundation into a native lightweight
                // record and destroys its actor. Inspect the actual placed data.
                const auto* Instances = LW->GetAllLightweightBuildableInstances().Find(FoundationClass);
                Result &= Instances && Instances->ContainsByPredicate([&](const FRuntimeBuildableInstanceData& Data)
                { return Data.IsValid() && Data.BuiltWithRecipe == FoundationRecipe && Data.Transform.Equals(Expected, .02); });
                continue;
            }
            const auto* Match = Loaded.FindByPredicate([&](AFGBuildable* Actor)
            { return IsValid(Actor) && Actor->GetClass() == Source[I]->GetClass() && Actor->GetActorTransform().Equals(Expected, .02); });
            Result &= Match != nullptr;
        }
        return Result;
    };
    TArray<FActorSaveHeader> Stored;
    const bool HeadersRead = ReadBuildingSkillsBlueprintHeaders(System->mStoredBlueprintData, Stored);
    TArray<AFGBuildable*> Preview;
    System->LoadStoredBlueprint(Descriptor, FTransform::Identity, Preview, true);
    bool HeaderScales = HeadersRead && Stored.Num() == 3;
    for (const FVector& Scale : Scales) HeaderScales &= Stored.ContainsByPredicate([&](const FActorSaveHeader& Item)
    { return FVector(Item.Transform.GetScale3D()).Equals(Scale, .0001); });
    Check(TEXT("Native blueprint actor headers retain all three original scales"), HeaderScales);
    auto InvalidTOC = System->mStoredBlueprintData;
    InvalidTOC.TOCData.Add(0);
    TArray<FActorSaveHeader> InvalidHeaders;
    Check(TEXT("Blueprint TOC validation rejects trailing unparsed data"), !ReadBuildingSkillsBlueprintHeaders(InvalidTOC, InvalidHeaders));
    Check(TEXT("Blueprint preview world retains uniform and nonuniform scales"), Verify(Preview, FTransform::Identity, false));

    auto* Hologram = World->SpawnActorDeferred<AFGBlueprintHologram>(AFGBlueprintHologram::StaticClass(), FTransform::Identity, Player, Player);
    bool HologramScales = Hologram != nullptr;
    if (Hologram)
    {
        // Native holograms require recipe/build-class initialization before BeginPlay.
        Hologram->SetRecipe(StorageRecipe);
        Hologram->SetBuildClass(StorageClass);
        Hologram->SetConstructionInstigator(Player);
        Hologram->mBlueprintDescriptor = Descriptor;
        Hologram->mBlueprintDescName = Record.BlueprintName;
        Hologram->FinishSpawning(FTransform::Identity);
        HologramScales &= Hologram->mBuildableToNewRoot.Num() == 3;
        for (const auto& Entry : Hologram->mBuildableToNewRoot)
        {
            HologramScales &= Entry.Key && Entry.Value && Entry.Value->GetRelativeScale3D().Equals(Entry.Key->GetActorScale3D(), .001);
        }
        Hologram->Destroy();
    }
    Check(TEXT("Native blueprint hologram meshes inherit restored scales"), HologramScales);

    TArray<AFGBuildable*> Vanilla;
    LoadBuildingSkillsBlueprintVanillaForTest(System, Descriptor, Vanilla);
    Check(TEXT("Unmodified native loader accepts the same blueprint at normal size"), Verify(Vanilla, FTransform::Identity, true));

    const FTransform Placement(FRotator(0, 90, 0), Player->GetActorLocation() + FVector(-4000, 0, 1800));
    TArray<AFGBuildable*> Placed;
    System->LoadStoredBlueprint(Descriptor, Placement, Placed, false);
    Check(TEXT("World blueprint placement preserves sizes, relative positions and rotations"), Verify(Placed, Placement, false, true));
    if (auto* Groups = ABSGroupSubsystem::Get(World))
    {
        bool Published = false;
        for (auto* Actor : Placed)
            if (IsValid(Actor) && Actor->GetClass() == StorageClass && Actor->GetActorScale3D().Equals(Scales[0], .001))
                Published |= Groups->TransformSnapshot.ContainsByPredicate([&](const FBSNativeTransform& Change)
                { return Change.Building.Actor == Actor && Change.Target.GetScale3D().Equals(Scales[0], .001); });
        Check(TEXT("Placed actor scales enter multiplayer and late-join transport"), Published);
    }
    // Serialization outside the blueprint scope must keep ordinary world-save
    // transforms untouched, including scales that differ from the live actor.
    FActorSaveHeader Ordinary;
    Ordinary.Set(Source[0]);
    Ordinary.Transform.SetScale3D(FVector3f(.7f, .8f, .9f));
    FBufferArchive Buffer;
    Ordinary.Serialize(Buffer, Header.SaveVersion);
    FMemoryReader Reader(Buffer);
    FActorSaveHeader Restored;
    Restored.Serialize(Reader, Header.SaveVersion);
    Check(TEXT("World-save actor-header serialization is unaffected by blueprint hooks"), Restored.Transform.Equals(Ordinary.Transform, .0001f));
    System->DeleteBlueprintDescriptor(Descriptor);
    return Passed;
}

AFGBlueprintProxy* RunBuildingSkillsFoundationPlacementChecks(AFGCharacterPlayer* Player, bool& Passed)
{
    auto* World = Player->GetWorld();
    auto* System = AFGBlueprintSubsystem::Get(World);
    auto* LW = AFGLightweightBuildableSubsystem::Get(World);
    auto Check = [&](const TCHAR* Name, bool Value)
    {
        Passed &= Value;
        UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME ASSERT: %s: %s"), Name, Value ? TEXT("PASS") : TEXT("FAIL"));
    };
    auto* Class = LoadClass<AFGBuildable>(nullptr, TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x4_01.Build_Foundation_8x4_01_C"));
    auto* Recipe = LoadClass<UFGRecipe>(nullptr, TEXT("/Game/FactoryGame/Recipes/Buildings/Foundations/Recipe_Foundation_8x4_01.Recipe_Foundation_8x4_01_C"));
    if (!System || !LW || !Class || !Recipe) { Check(TEXT("Foundation placement fixture available"), false); return nullptr; }
    TArray<AFGBuildable*> Source;
    TArray<FVector> Points;
    for (int32 I = 0; I < 2; ++I)
    {
        FRuntimeBuildableInstanceData Data;
        Data.Transform = FTransform(FRotator(0, 30, 0), Player->GetActorLocation() + FVector(2000, 0, 1000));
        Data.Transform.AddToTranslation(Data.Transform.GetRotation().RotateVector(FVector(0, I * 800, 0)));
        Data.BuiltWithRecipe = Recipe;
        Data.CustomizationData.SwatchDesc = Class->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(World);
        const int32 Index = LW->AddFromBuildableInstanceData(Class, Data);
        auto* Actor = FBSBuildingRef::FromLightweight(World, Class, Index).ResolveActor(World);
        if (Actor)
        {
            Source.Add(Actor);
            const auto* Stored = LW->GetRuntimeDataForBuildableClassAndIndex(Class, Index);
            BSBlueprintBounds::AppendBox(Stored->BoundingBox, Stored->Transform, Points);
        }
    }
    FTransform Origin; FVector Size;
    if (Source.Num() != 2 || !BSBlueprintBounds::FitUpright(Points, {30, 30}, Origin, Size))
    { Check(TEXT("Two-foundation fixture has full native geometry"), false); return nullptr; }
    FBlueprintRecord Record;
    Record.BlueprintName = TEXT("BuildingSkills_Foundations_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Record.Category = System->GetCategoryForUndefined(); Record.SubCategory = System->GetSubCategoryForUndefined();
    auto Header = WriteBuildingSkillsBlueprintForTest(System, Record, Origin, Source);
    if (!System->WriteBlueprintToDisk(Record)) { Check(TEXT("Two-foundation blueprint saves"), false); return nullptr; }
    System->EnumerateBlueprints();
    auto* Descriptor = System->CreateBlueprintDescriptor(Record, Header);
    System->ReadBlueprintFromDisc(Record.BlueprintName);
    auto* Hologram = World->SpawnActorDeferred<AFGBlueprintHologram>(AFGBlueprintHologram::StaticClass(), FTransform::Identity, Player, Player);
    Hologram->SetRecipe(Recipe); Hologram->SetBuildClass(Class); Hologram->SetConstructionInstigator(Player);
    Hologram->mBlueprintDescriptor = Descriptor; Hologram->mBlueprintDescName = Record.BlueprintName;
    Hologram->FinishSpawning(FTransform::Identity);
    const FBox Geometry = FBSBlueprintPlacement::GeometryBounds(Hologram);
    const FBox Bounds = FBSBlueprintPlacement::PlacementBounds(Hologram);
    UE_LOG(LogBuildingSkills, Display, TEXT("Foundation placement: geometry=%s bounds=%s saveSize=%s"), *Geometry.ToString(), *Bounds.ToString(), *Size.ToString());
    Check(TEXT("Two-foundation hologram bounds enclose the full geometry above the placement floor"),
        Geometry.IsValid && Bounds.IsValid && Bounds.ExpandBy(.1).IsInsideOrOn(Geometry.Min) && Bounds.ExpandBy(.1).IsInsideOrOn(Geometry.Max));
    Check(TEXT("Two-foundation hologram is centered horizontally at its lowest Z"),
        Bounds.IsValid && FMath::IsNearlyZero(Bounds.GetCenter().X, .1) && FMath::IsNearlyZero(Bounds.GetCenter().Y, .1) && FMath::IsNearlyZero(Bounds.Min.Z, .1));
    Check(TEXT("Two-foundation placement box tightly fits its saved mesh dimensions"), Bounds.GetSize().Equals(Size, .1));
    const FTransform Support = Source[0]->GetActorTransform();
    const FVector HitLocation = Support.TransformPosition(FVector(400, 0, 0));
    const FVector HitNormal = Support.GetRotation().GetAxisX();
    FHitResult Hit(Source[0], Cast<UPrimitiveComponent>(Source[0]->GetRootComponent()), HitLocation, HitNormal);
    Hit.bBlockingHit = true; Hit.Location = HitLocation; Hit.ImpactPoint = HitLocation; Hit.Normal = HitNormal; Hit.ImpactNormal = HitNormal;
    Hologram->SetActorRotation(Support.Rotator());
    Hologram->SetActorLocation(HitLocation);
    const bool Snapped = Hologram->TrySnapToActor(Hit);
    bool GridAligned = Snapped;
    for (const auto& Entry : Hologram->mBuildableToNewRoot)
        if (Entry.Value)
        {
            const FVector Local = Support.InverseTransformPosition(Entry.Value->GetComponentLocation());
            GridAligned &= FMath::IsNearlyEqual(Local.X / 800, FMath::RoundToDouble(Local.X / 800), .001) &&
                FMath::IsNearlyEqual(Local.Y / 800, FMath::RoundToDouble(Local.Y / 800), .001) && FMath::IsNearlyZero(Local.Z, .1);
            UE_LOG(LogBuildingSkills, Display, TEXT("Foundation snap: success=%d local=%s"), Snapped, *Local.ToString());
        }
    Check(TEXT("Foundation-side snapping keeps both blueprint foundations on the support grid"), GridAligned);
    Hit.Location = Hit.ImpactPoint = Support.TransformPosition(FVector(0, 0, 200));
    Hit.Normal = Hit.ImpactNormal = Support.GetRotation().GetAxisZ();
    Hologram->SetActorLocation(Hit.Location);
    bool TopAligned = Hologram->TrySnapToActor(Hit);
    for (const auto& Entry : Hologram->mBuildableToNewRoot)
        if (Entry.Value)
        {
            const FVector Local = Support.InverseTransformPosition(Entry.Value->GetComponentLocation());
            TopAligned &= FMath::IsNearlyEqual(Local.X / 800, FMath::RoundToDouble(Local.X / 800), .001) &&
                FMath::IsNearlyEqual(Local.Y / 800, FMath::RoundToDouble(Local.Y / 800), .001) && FMath::IsNearlyEqual(Local.Z, 400.0, .1);
        }
    Check(TEXT("Foundation-top snapping aligns the grid and stacks at the correct height"), TopAligned);
    Hologram->SetActorLocation(Player->GetActorLocation() + FVector(-3000, 0, 1500));
    TArray<AActor*> Children;
    auto* Proxy = Cast<AFGBlueprintProxy>(Hologram->Construct(Children, FNetConstructionID()));
    int32 Members = 0; bool Linked = true;
    if (Proxy)
        for (const auto& Bundle : Proxy->GetLightweightClassAndIndices())
            for (int32 Index : Bundle.Indices)
            {
                ++Members;
                Linked &= FBSBuildingRef::FromLightweight(World, Bundle.BuildableClass, Index).Blueprint(World) == Proxy;
            }
    Check(TEXT("Actual blueprint construction registers both foundations with the native proxy"), IsValid(Proxy) && Members == 2 && Linked);
    Check(TEXT("Actual blueprint proxy is ready for blueprint selection"), IsValid(Proxy) && Proxy->AreProxyBuildingsRegisteredAndValid());
    if (Proxy && Proxy->GetBoundingBox())
        UE_LOG(LogBuildingSkills, Display, TEXT("Foundation proxy bounds: %s"), *Proxy->GetBoundingBox()->GetUnscaledBoxExtent().ToString());
    Hologram->Destroy();
    System->DeleteBlueprintDescriptor(Descriptor);
    return Proxy;
}
