#include "BSConnected.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "Equipment/FGBuildGunPaint.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGPlayerState.h"
#include "FGHealthComponent.h"
#include "FGRecipe.h"
#include "FGCustomizationRecipe.h"
#include "FGRecipeManager.h"
#include "FGInventoryComponent.h"
#include "FGCentralStorageSubsystem.h"
#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGGameState.h"
#include "FGOutlineComponent.h"
#include "BSTransform.h"
#include "Buildables/FGBuildable.h"
#include "Patching/NativeHookManager.h"

namespace
{
    UBSGroupRemoteCallObject* PaintOwner(AFGCharacterPlayer* Player)
    {
        auto* PC=Player?Cast<AFGPlayerController>(Player->GetController()):nullptr;
        return PC?PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>():nullptr;
    }
    bool HasPaintTransformers(AFGCharacterPlayer* Player)
    { auto* RCO=PaintOwner(Player); return RCO && !RCO->Transformers.IsEmpty(); }
    bool SameSettings(const FBSConnectedSettings& A, const FBSConnectedSettings& B)
    {
        return A.Gap == B.Gap && A.IgnoreSwatch == B.IgnoreSwatch && A.IgnoreMaterial == B.IgnoreMaterial &&
            A.Scope == B.Scope && A.Limit == B.Limit && A.Whitelist == B.Whitelist && A.Blacklist == B.Blacklist;
    }

    bool EligibleForPaint(AFGBuildable* Actor, TSubclassOf<UFGFactoryCustomizationDescriptor> Desc, bool Material)
    {
        if (!IsValid(Actor) || Actor->GetIsDismantled() || !Desc) return false;
        bool Eligible = !Material && Actor->GetCanBeColored_Native() && !Actor->IsColorApplicationDeferred();
        if (Material)
        {
            auto* Mat = Cast<UFGFactoryCustomizationDescriptor_Material>(Desc.GetDefaultObject());
            Eligible = Mat && Mat->GetBuildableMap().Contains(Actor->GetClass());
        }
        const auto* Descriptor = Desc.GetDefaultObject();
        if (Eligible && Descriptor->mValidBuildables.ContainsByPredicate([](auto Class) { return Class != nullptr; }))
            Eligible = Descriptor->mValidBuildables.ContainsByPredicate([&](auto Class) { return Class && Actor->IsA(Class); });
        return Eligible;
    }
}

void FBSConnectedPaint::Install()
{
    if (WITH_EDITOR) return;
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStatePaint, TickState_Implementation,
        [](auto& Scope, UFGBuildGunStatePaint* State, float Delta)
        {
            auto* Player = State->GetBuildGun()->GetInstigatorCharacter();
            auto* Edit = Player && Player->IsLocallyControlled() ? Player->FindComponentByClass<UBSEditModeComponent>() : nullptr;
            if (!Edit) return;
            // An old extra can become the native aimed target. Restore it BEFORE
            // vanilla computes that target's new preview, never afterwards.
            auto* Hit = Cast<AFGBuildable>(State->GetBuildGun()->GetHitResult().GetActor());
            if (!Edit->IsSpreadPainting() || (!Edit->bConnectedPaint && !HasPaintTransformers(Player)) || !Edit->InputAllowed() ||
                (Edit->ConnectedPaintPreviewRecipe.IsValid() &&
                    Edit->ConnectedPaintPreviewRecipe.Get() != State->GetActiveRecipe().Get()))
                ClearPreview(Edit);
            else if (!(FBSBuildingRef::FromActor(Hit) == Edit->ConnectedPaintPreviewSeed))
                for (int32 I = Edit->ConnectedPaintPreviewActors.Num() - 1; I >= 0; --I)
                    if (Edit->ConnectedPaintPreviewActors[I].Get() == Hit) RemovePreview(Edit, I);
            Scope(State, Delta);
            TickPreview(Edit, State);
        });
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStatePaint, EndState_Implementation,
        [](auto&, UFGBuildGunStatePaint* State)
        {
            auto* Player = State->GetBuildGun()->GetInstigatorCharacter();
            if (auto* Edit = Player ? Player->FindComponentByClass<UBSEditModeComponent>() : nullptr)
            {
                ClearPreview(Edit);
                // Restore the shared native mode row before the next gun state
                // updates its text/icon, rather than after that state has begun.
                Edit->UpdateControlHints(false);
            }
        });
    SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStatePaint, PrimaryFire_Implementation,
        [](auto& Scope, UFGBuildGunStatePaint* State)
        {
            auto* Player = State->GetBuildGun()->GetInstigatorCharacter();
            auto* Edit = Player ? Player->FindComponentByClass<UBSEditModeComponent>() : nullptr;
            if (!Edit || (!Edit->bConnectedPaint && !HasPaintTransformers(Player))) return;
            auto* Recipe = State->GetActiveRecipe().Get();
            auto Desc = Recipe ? UFGCustomizationRecipe::GetCustomizationDescriptor(Recipe) : nullptr;
            // Patterns/skins retain the vanilla behavior; this mode covers the
            // material and swatch operations requested by Connected Components.
            if (!Desc || !(Desc->IsChildOf(UFGFactoryCustomizationDescriptor_Swatch::StaticClass()) || Desc->IsChildOf(UFGFactoryCustomizationDescriptor_Material::StaticClass()))) return;
            Scope.Cancel();
            if (!Edit->InputAllowed()) return;
            ClearPreview(Edit);
            auto* Actor = Cast<AFGBuildable>(State->mCurrentlyAimedAtActor);
            if (!Actor) Actor = Cast<AFGBuildable>(State->GetBuildGun()->GetHitResult().GetActor());
            auto* PC = Cast<AFGPlayerController>(Player->GetController());
            auto* RCO = PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
            if (Actor && RCO)
            {
                if(Edit->bConnectedPaint) RCO->Server_PaintConnected(FBSBuildingRef::FromActor(Actor), Edit->ConnectedSettings, Recipe);
                else RCO->Server_PaintTransformers(FBSBuildingRef::FromActor(Actor),Recipe);
            }
        });
}

void FBSConnectedPaint::RemovePreview(UBSEditModeComponent* Edit, int32 I, AFGBuildable* PreserveNativeTarget)
{
    if (!Edit || !Edit->ConnectedPaintPreviewActors.IsValidIndex(I)) return;
    auto* World = Edit->GetWorld();
    auto* Buildables = World ? AFGBuildableSubsystem::Get(World) : nullptr;
    if (auto* Actor = Edit->ConnectedPaintPreviewActors[I].Get())
        {
            // Native cleanup applies the CURRENT saved customization, so a
            // replicated paint change is never overwritten with an old snapshot.
            if (Actor != PreserveNativeTarget && Buildables) Buildables->ClearCustomizationPreview(Actor);
            if (Edit->ConnectedPaintPreviewOutline) Edit->ConnectedPaintPreviewOutline->HideOutline(Actor);
            if (Actor->GetIsLightweightTemporary())
            {
                const auto Current = FBSBuildingRef::FromActor(Actor);
                const auto& Held = Edit->ConnectedPaintPreviewMembers[I];
                // A remote move preserves this temporary actor and native slot
                // while changing its transform. Release our hold in that case,
                // but never touch a pooled actor reused for a different slot.
                if (Current.LightweightClass == Held.LightweightClass && Current.RuntimeIndex == Held.RuntimeIndex && Current.Recipe == Held.Recipe)
                    Actor->SetBlockCleanupOfTemporary(Edit->ConnectedPaintPreviewOriginalCleanupBlocks[I]);
            }
        }
    if (auto* Replacement = Edit->ConnectedPaintPreviewReplacements[I].Get()) Replacement->Destroy();
    Edit->ConnectedPaintPreviewMembers.RemoveAt(I);
    Edit->ConnectedPaintPreviewActors.RemoveAt(I);
    Edit->ConnectedPaintPreviewReplacements.RemoveAt(I);
    Edit->ConnectedPaintPreviewOriginalCleanupBlocks.RemoveAt(I);
    Edit->ConnectedPaintPreviewOriginalData.RemoveAt(I);
}

void FBSConnectedPaint::ClearPreview(UBSEditModeComponent* Edit, AFGBuildable* PreserveNativeTarget)
{
    if (!Edit) return;
    for (int32 I = Edit->ConnectedPaintPreviewActors.Num() - 1; I >= 0; --I) RemovePreview(Edit, I, PreserveNativeTarget);
    if (Edit->ConnectedPaintPreviewOutline) Edit->ConnectedPaintPreviewOutline->DestroyComponent();
    Edit->ConnectedPaintPreviewOutline = nullptr;
    Edit->ConnectedPaintPreviewMembers.Reset();
    Edit->ConnectedPaintPreviewActors.Reset();
    Edit->ConnectedPaintPreviewReplacements.Reset();
    Edit->ConnectedPaintPreviewOriginalCleanupBlocks.Reset();
    Edit->ConnectedPaintPreviewOriginalData.Reset();
    Edit->ConnectedPaintPreviewSeed = {};
    Edit->ConnectedPaintPreviewRecipe.Reset();
    Edit->ConnectedPaintPreviewCustomization = {};
    Edit->NextConnectedPaintPreviewRefresh = 0;
    Edit->ConnectedPaintPreviewTransformerRevision = -1;
}

void FBSConnectedPaint::TickPreview(UBSEditModeComponent* Edit, UFGBuildGunStatePaint* Paint)
{
    if (!Edit || !Paint) return;
    auto* Player = Cast<AFGCharacterPlayer>(Edit->GetOwner());
    auto* World = Edit->GetWorld();
    auto* Buildables = World ? AFGBuildableSubsystem::Get(World) : nullptr;
    auto* Aimed = Cast<AFGBuildable>(Paint->mCurrentlyAimedAtActor);
    auto* RCO=PaintOwner(Player);
    if (!Player || !Player->IsLocallyControlled() || !Buildables || (!Edit->bConnectedPaint && !HasPaintTransformers(Player)) ||
        !Edit->IsSpreadPainting() || !Edit->InputAllowed() || !IsValid(Aimed))
    { ClearPreview(Edit); return; }
    const auto Recipe = Paint->GetActiveRecipe();
    const auto Desc = Recipe ? UFGCustomizationRecipe::GetCustomizationDescriptor(Recipe) : nullptr;
    if (!Desc) { ClearPreview(Edit); return; }
    const bool Material = Desc->IsChildOf(UFGFactoryCustomizationDescriptor_Material::StaticClass());
    // Native's replacement helper uses a fixed 1.01 scale. Preserve any scale
    // applied by Edit, while retaining vanilla's slight preview inflation.
    if (Material && IsValid(Paint->mPreviewActor)) Paint->mPreviewActor->SetActorScale3D(Aimed->GetActorScale3D() * 1.01);
    FFactoryCustomizationData Change;
    if (Material) Change.MaterialDesc = Desc.Get();
    else
    {
        Change.SwatchDesc = Desc.Get();
        if (const auto* PS = Player->GetPlayerState<AFGPlayerState>()) Change.OverrideColorData = PS->GetCustomColorData();
    }
    const auto Seed = FBSBuildingRef::FromActor(Aimed);
    if (!Seed.IsAlive(World)) { ClearPreview(Edit); return; }
    bool Changed = !(Edit->ConnectedPaintPreviewSeed == Seed) || Edit->ConnectedPaintPreviewRecipe.Get() != Recipe.Get() ||
        Edit->ConnectedPaintPreviewCustomization != Change || !SameSettings(Edit->ConnectedPaintPreviewSettings, Edit->ConnectedSettings) ||
        Edit->ConnectedPaintPreviewTransformerRevision!=(RCO?RCO->TransformerRevision:-1) || Edit->ConnectedPaintPreviewConnected!=Edit->bConnectedPaint;
    for (int32 I = 0; !Changed && I < Edit->ConnectedPaintPreviewMembers.Num(); ++I)
    {
        auto* Actor = Edit->ConnectedPaintPreviewActors[I].Get();
        Changed = !Actor || !(FBSBuildingRef::FromActor(Actor) == Edit->ConnectedPaintPreviewMembers[I]) ||
            !Actor->GetActorTransform().Equals(Edit->ConnectedPaintPreviewMembers[I].Transform, .01) ||
            Actor->GetCustomizationData_Native() != Edit->ConnectedPaintPreviewOriginalData[I];
    }
    const double Now = World->GetRealTimeSeconds();
    if (!Changed && Now < Edit->NextConnectedPaintPreviewRefresh) return;
    FString Message;
    // Search reads persistent customization data. Native previews only change
    // rendered component data; they do not change membership or save data.
    auto Members = Edit->bConnectedPaint?FBSConnected::Find(World, Seed, Edit->ConnectedSettings, Message):TArray<FBSBuildingRef>{Seed};
    if(RCO) Members.Append(FBSTransformers::Matching(World,RCO->Transformers,Members,Message));
    Members.Remove(Seed);
    // Retain previews shared with the new component. Crossing between neighbors
    // changes only the aimed target, not every material replacement actor.
    const bool ChangeAppearance = Edit->ConnectedPaintPreviewRecipe.Get() != Recipe.Get() ||
        Edit->ConnectedPaintPreviewCustomization != Change;
    for (int32 I = Edit->ConnectedPaintPreviewMembers.Num() - 1; I >= 0; --I)
    {
        auto* Actor = Edit->ConnectedPaintPreviewActors[I].Get();
        const auto& Previous = Edit->ConnectedPaintPreviewMembers[I];
        if (ChangeAppearance || !Members.Contains(Previous) || !Actor ||
            !(FBSBuildingRef::FromActor(Actor) == Previous) || !Actor->GetActorTransform().Equals(Previous.Transform, .01) ||
            Actor->GetCustomizationData_Native() != Edit->ConnectedPaintPreviewOriginalData[I]) RemovePreview(Edit, I, Aimed);
    }
    Edit->ConnectedPaintPreviewSeed = Seed;
    Edit->ConnectedPaintPreviewRecipe = Recipe.Get();
    Edit->ConnectedPaintPreviewCustomization = Change;
    Edit->ConnectedPaintPreviewSettings = Edit->ConnectedSettings;
    Edit->ConnectedPaintPreviewTransformerRevision=RCO?RCO->TransformerRevision:-1;
    Edit->ConnectedPaintPreviewConnected=Edit->bConnectedPaint;
    Edit->NextConnectedPaintPreviewRefresh = Now + 1;
    for (const auto& Ref : Members)
    {
        if (Edit->ConnectedPaintPreviewMembers.Contains(Ref)) continue;
        auto* Actor = Ref.ResolveActor(World);
        if (!EligibleForPaint(Actor, Desc, Material)) continue;
        const auto Original = Actor->GetCustomizationData_Native();
        if (!Material && Original.SwatchDesc == Desc &&
            (Desc.GetDefaultObject()->ID != INDEX_CUSTOM_COLOR_SLOT || Original.OverrideColorData == Change.OverrideColorData)) continue;

        const bool BlockedCleanup = Actor->GetBlockCleanupOfTemporary();
        if (Ref.LightweightClass) Actor->SetBlockCleanupOfTemporary(true);
        auto PreviewData = Original;
        PreviewData.InlineCombine(Change);
        AActor* Replacement = nullptr;
        if (Material)
        {
            // Vanilla's material helper creates the replacement meshes and
            // changes MaterialDesc to its invisible descriptor when appropriate.
            // Keep its single native aimed-preview bookkeeping untouched.
            auto NativeMeshes = MoveTemp(Paint->mPendingPreviewMeshes);
            {
                TGuardValue<TObjectPtr<AActor>> NativeActor(Paint->mPreviewActor, nullptr);
                Paint->SpawnPreviewForMaterial(Actor, PreviewData.MaterialDesc);
                Replacement = Paint->mPreviewActor.Get();
            }
            Paint->mPendingPreviewMeshes = MoveTemp(NativeMeshes);
            if (Replacement)
            {
                Replacement->SetFlags(RF_Transient);
                Replacement->SetReplicates(false);
                Replacement->SetActorEnableCollision(false);
                Replacement->SetActorScale3D(Actor->GetActorScale3D() * 1.01);
            }
        }
        PreviewData.Initialize(World->GetGameState<AFGGameState>(), BUILDABLE_CUSTOM_DATA_NUM);
        Buildables->ApplyCustomizationPreview(Actor, PreviewData);
        if (!Edit->ConnectedPaintPreviewOutline)
        {
            Edit->ConnectedPaintPreviewOutline = NewObject<UFGOutlineComponent>(Player);
            Edit->ConnectedPaintPreviewOutline->RegisterComponent();
        }
        Edit->ConnectedPaintPreviewOutline->ShowOutline(Actor, EOutlineColor::OC_HOLOGRAMLINE);
        Edit->ConnectedPaintPreviewMembers.Add(Ref);
        Edit->ConnectedPaintPreviewActors.Add(Actor);
        Edit->ConnectedPaintPreviewOriginalData.Add(Original);
        Edit->ConnectedPaintPreviewReplacements.Add(Replacement);
        Edit->ConnectedPaintPreviewOriginalCleanupBlocks.Add(BlockedCleanup);
    }
    if (Edit->ConnectedPaintPreviewMembers.IsEmpty() && Edit->ConnectedPaintPreviewOutline)
    {
        Edit->ConnectedPaintPreviewOutline->DestroyComponent();
        Edit->ConnectedPaintPreviewOutline = nullptr;
    }
}

void UBSGroupRemoteCallObject::Server_PaintConnected_Implementation(const FBSBuildingRef& Seed,
    const FBSConnectedSettings& Settings, TSubclassOf<UFGCustomizationRecipe> Recipe)
{ FBSConnectedPaint::Execute(this, Seed, Settings, Recipe); }
void UBSGroupRemoteCallObject::Server_PaintTransformers_Implementation(const FBSBuildingRef& Seed,TSubclassOf<UFGCustomizationRecipe> Recipe)
{ FBSConnectedPaint::Execute(this,Seed,FBSConnectedSettings(),Recipe,false); }
void UBSGroupRemoteCallObject::Client_ConnectedMessage_Implementation(const FString& Message)
{
    if (auto* Player = GetOwnerPlayerCharacter())
        if (auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>()) Edit->ConnectedMessage(Message);
}

void FBSConnectedPaint::Execute(UBSGroupRemoteCallObject* RCO, const FBSBuildingRef& Seed,
    const FBSConnectedSettings& Settings, TSubclassOf<UFGCustomizationRecipe> Recipe,bool Connected)
{
    auto* Player = RCO->GetOwnerPlayerCharacter();
    auto* World = Player ? Player->GetWorld() : nullptr;
    auto* Gun = Player ? Player->GetBuildGun() : nullptr;
    auto* Paint = Gun ? Cast<UFGBuildGunStatePaint>(Gun->GetCurrentState()) : nullptr;
    if (!World || !Player->HasAuthority() || !Player->IsBuildGunEquipped() || !Paint || !Player->GetHealthComponent() || Player->GetHealthComponent()->IsDead()) return;
    if (World->GetRealTimeSeconds() - RCO->LastConnectedPaint < .2) return;
    RCO->LastConnectedPaint = World->GetRealTimeSeconds();
    if (!Recipe || Paint->GetActiveRecipe() != Recipe || !Seed.IsAlive(World) || FVector::DistSquared(Player->GetActorLocation(), Seed.Location()) > FMath::Square(20000.0))
    { RCO->Client_ConnectedMessage(TEXT("Paint target or recipe changed. Aim again and retry.")); return; }
    auto* Recipes = AFGRecipeManager::Get(World);
    if (!Recipes || !Recipes->IsRecipeAvailable(Recipe)) return;
    const auto Desc = UFGCustomizationRecipe::GetCustomizationDescriptor(Recipe);
    const bool Material = Desc && Desc->IsChildOf(UFGFactoryCustomizationDescriptor_Material::StaticClass());
    const bool Swatch = Desc && Desc->IsChildOf(UFGFactoryCustomizationDescriptor_Swatch::StaticClass());
    if (!Material && !Swatch) return;
    FString Message;
    // Resolve all membership BEFORE any material replacement or swatch change.
    auto Members = Connected?FBSConnected::Find(World, Seed, Settings, Message):TArray<FBSBuildingRef>{Seed};
    Members.Append(FBSTransformers::Matching(World,RCO->Transformers,Members,Message));
    if (Members.IsEmpty()) { RCO->Client_ConnectedMessage(Message.IsEmpty() ? TEXT("No matching connected buildings.") : Message); return; }
    auto* Inventory = Gun->GetInventory();
    auto* Central = AFGCentralStorageSubsystem::Get(World);
    int32 Applied = 0, Skipped = 0;
    for (const auto& Ref : Members)
    {
        auto* Actor = Ref.ResolveActor(World);
        if (!Actor) { ++Skipped; continue; }
        // These native fields select material replacement vs color application.
        // Restore the hovered target's flags after every operation.
        const auto* Descriptor = Desc.GetDefaultObject();
        // Vanilla swatches can contain a null placeholder in this optional list.
        // Only actual class restrictions should narrow the eligible buildings.
        if (!EligibleForPaint(Actor, Desc, Material)) { ++Skipped; continue; }
        const auto Existing = Actor->GetCustomizationData_Native();
        if (Swatch && Existing.SwatchDesc == Desc &&
            (Descriptor->ID != INDEX_CUSTOM_COLOR_SLOT || (Player->GetPlayerState<AFGPlayerState>() && Existing.OverrideColorData == Player->GetPlayerState<AFGPlayerState>()->GetCustomColorData())))
        { ++Skipped; continue; }
        bool Affordable = Inventory != nullptr;
        if (Inventory && !Inventory->GetNoBuildCost())
            for (const auto& Cost : Paint->GetCustomizationCost())
                if (Inventory->GetNumItems(Cost.ItemClass) + (Central ? Central->GetNumItemsFromCentralStorage(Cost.ItemClass) : 0) < Cost.Amount) Affordable = false;
        if (!Affordable) { Message = TEXT("Stopped: insufficient customization materials."); break; }
        const FBox PreviousBounds=Material?FBSTransformNative::Bounds(World,Ref):FBox(ForceInit);
        TSubclassOf<AFGBuildable> ReplacementClass;
        if(Material) if(auto* Mat=Cast<UFGFactoryCustomizationDescriptor_Material>(Desc.GetDefaultObject()))
            if(const auto* ReplacementRecipe=Mat->GetBuildableMap().Find(Actor->GetClass()))
                ReplacementClass=AFGBuildable::GetBuildableClassFromRecipe(*ReplacementRecipe);
        // Material replacement constructs a new instance. Supply its complete
        // existing appearance so native construction does not use default colors.
        FFactoryCustomizationData Data = Material ? Existing : FFactoryCustomizationData();
        if (Material) Data.MaterialDesc = Desc.Get();
        else
        {
            Data.SwatchDesc = Desc.Get();
            if (auto* PS = Player->GetPlayerState<AFGPlayerState>()) Data.OverrideColorData = PS->GetCustomColorData();
        }
        TGuardValue<bool> MaterialFlag(Paint->mHitCanApplyMaterial, Material);
        TGuardValue<bool> SwatchFlag(Paint->mHitCanApplySwatch, Swatch);
        TGuardValue<bool> PatternFlag(Paint->mHitCanApplyPattern, false);
        TGuardValue<bool> SkinFlag(Paint->mHitCanApplySkin, false);
        // Native single-target execution owns costs/refunds, material replacement,
        // customization persistence and lightweight instance replication.
        Paint->Server_ExecutePaint_Implementation(uint8(EPaintMode::EPM_Single), Data, Actor);
        if(Material && Ref.LightweightClass && ReplacementClass && !Ref.IsAlive(World) && Existing.SwatchDesc)
        {
            // Native material construction forces the new class's default swatch
            // even when full customization is supplied. Restore the old swatch
            // through the native instance API, which also replicates the change.
            TArray<FBSBuildingRef> Replacements;int32 Examined=0;
            FBSTransformNative::QueryLightweightBounds(World,PreviousBounds.ExpandBy(1),Replacements,Examined);
            Replacements.RemoveAll([&](const auto& B){return B.LightweightClass!=ReplacementClass || !B.Transform.Equals(Ref.Transform,.01);});
            if(Replacements.Num()==1)
            {
                const auto& B=Replacements[0];auto* LW=AFGLightweightBuildableSubsystem::Get(World);
                const int32 Index=B.ResolveIndex(World);auto* Current=LW->GetRuntimeDataForBuildableClassAndIndex(B.LightweightClass,Index);
                if(Current)
                {
                    auto Preserved=Current->CustomizationData;Preserved.SwatchDesc=Existing.SwatchDesc;Preserved.OverrideColorData=Existing.OverrideColorData;
                    Preserved.Initialize(World->GetGameState<AFGGameState>(),BUILDABLE_CUSTOM_DATA_NUM);
                    LW->SetCustomizationDataOnInstance(B.LightweightClass,Preserved,Index);
                }
            }
        }
        ++Applied;
    }
    RCO->Client_ConnectedMessage(FString::Printf(TEXT("%s paint: %d applied, %d skipped.%s%s"),Connected?TEXT("Connected"):TEXT("Transformer"), Applied, Skipped, Message.IsEmpty() ? TEXT("") : TEXT("\n"), *Message));
    UE_LOG(LogBuildingSkills, Display, TEXT("Connected paint: members=%d applied=%d skipped=%d"), Members.Num(), Applied, Skipped);
}
