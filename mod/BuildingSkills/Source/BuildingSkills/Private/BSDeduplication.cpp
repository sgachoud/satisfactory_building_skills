#include "BSDeduplication.h"
#include "BSClipboard.h"
#include "BSBlueprintBounds.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFoundation.h"
#include "Buildables/FGBuildableWire.h"
#include "FGCircuitConnectionComponent.h"
#include "FGSplineBuildableInterface.h"
#include "Engine/StaticMesh.h"
#include "FGBlueprintFunctionLibrary.h"
#include "FGBlueprintSubsystem.h"
#include "FGBlueprintProxy.h"
#include "FGRecipeManager.h"
#include "FGCustomizationRecipe.h"
#include "FGFactoryColoringTypes.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "FGInventoryLibrary.h"
#include "FGDismantleInterface.h"
#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Patching/NativeHookManager.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
    using FBlueprintCostInvoker = HookInvoker<decltype(&AFGBlueprintHologram::GetBaseCost), &AFGBlueprintHologram::GetBaseCost>;
#if PLATFORM_LINUX
    // SML 3.12's member aggregate-return helper puts `this` before the hidden
    // return buffer (the Windows ABI). SysV puts the buffer first. A typed free
    // function with an explicit object argument lets Clang emit the native ABI
    // and construct the returned TArray, including when a handler overrides it.
    using FBlueprintCostFunction = TArray<FItemAmount> (*)(const AFGBlueprintHologram*);
    FBlueprintCostFunction NativeBlueprintCost = nullptr;
    THandlerLists<FBlueprintCostInvoker::Handler, FBlueprintCostInvoker::HandlerAfter>* BlueprintCostHandlers = nullptr;

    TArray<FItemAmount> DispatchBlueprintCost(const AFGBlueprintHologram* Hologram)
    {
        FBlueprintCostInvoker::ScopeType Scope(&BlueprintCostHandlers->HandlersBefore, NativeBlueprintCost);
        Scope(Hologram);
        for (const auto& Handler : BlueprintCostHandlers->HandlersAfter)
            (*Handler)(Scope.GetResult(), Hologram);
        return Scope.GetResult();
    }
#endif

    void SubscribeBlueprintCost(FBlueprintCostInvoker::Handler Handler)
    {
#if PLATFORM_LINUX
        // Keep the standard SML handler list so other subscribers still run.
        void* Address = FNativeHookManagerInternal::RegisterHookFunction(
            TEXT("AFGBlueprintHologram::GetBaseCost"),
            ConvertFunctionPointer(&AFGBlueprintHologram::GetBaseCost), GetDefault<AFGBlueprintHologram>(),
            reinterpret_cast<void*>(&DispatchBlueprintCost), reinterpret_cast<void**>(&NativeBlueprintCost));
        BlueprintCostHandlers = CreateHandlerLists<FBlueprintCostInvoker::Handler, FBlueprintCostInvoker::HandlerAfter>(Address);
        BlueprintCostHandlers->HandlersBefore.Add(MakeShared<FBlueprintCostInvoker::Handler>(MoveTemp(Handler)));
        UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint cost hook installed with Linux aggregate-return ABI."));
#else
        SUBSCRIBE_UOBJECT_METHOD(AFGBlueprintHologram, GetBaseCost, MoveTemp(Handler));
#endif
    }

    struct FPlan
    {
        EBSDeduplication Mode = EBSDeduplication::Off;
        TSet<FString> Skipped;
        TArray<FBSBuildingRef> Replace;
        TMap<TWeakObjectPtr<AFGBuildable>, FString> Sources;
        TArray<TFunction<void()>> DeferredGroups;
    };
    thread_local FPlan* ActivePlan = nullptr;
    struct FPreviewData { FString Source; FBox Local{ForceInit}; };
    TMap<TWeakObjectPtr<const AFGBlueprintHologram>,TMap<TWeakObjectPtr<AFGBuildable>,FPreviewData>> PreviewData;
    struct FCostCache { TSet<FString> Skipped; TArray<FItemAmount> Cost; };
    TMap<TWeakObjectPtr<const AFGBlueprintHologram>,FCostCache> CostCache;
    const FPreviewData* TemplateData(const AFGBlueprintHologram* Hologram,AFGBuildable* Building)
    { const auto* Records=PreviewData.Find(Hologram); return Records ? Records->Find(Building) : nullptr; }
    TMap<TWeakObjectPtr<const AFGBlueprintHologram>, EBSDeduplication> ReceivedModes;
    TMap<TWeakObjectPtr<AFGBuildable>, FString> SourcePaths;

    AFGBuildable* ReplacementActor(UWorld* World, const FBSBuildingRef& Ref)
    {
        if (!Ref.LightweightClass) return Ref.ResolveActor(World);
        auto* LW = AFGLightweightBuildableSubsystem::Get(World);
        const auto* Data = LW ? LW->GetRuntimeDataForBuildableClassAndIndex(Ref.LightweightClass, Ref.RuntimeIndex) : nullptr;
        if (!Data || !Data->IsValid() || Data->BuiltWithRecipe != Ref.Recipe || !Data->Transform.Equals(Ref.Transform,.01)) return nullptr;
        FLightweightBuildableInstanceRef Native; Native.Initialize(LW,Ref.LightweightClass,Ref.RuntimeIndex);
        return Native.SpawnTemporaryBuildable();
    }

    bool Geometry(AFGBuildable* Actor, FBox& Local)
    {
        if (!IsValid(Actor)) return false;
        if (auto* Wire = Cast<AFGBuildableWire>(Actor))
        {
            // Cached world endpoints are not saved and are unset before BeginPlay.
            // The native hologram uses the saved relative instance endpoints.
            const FTransform Frame = Wire->GetActorTransform();
            for (const auto& Instance : Wire->GetWireInstances())
                for (int32 End=0; End<2; ++End)
                    Local += Wire->HasActorBegunPlay() ? Frame.InverseTransformPosition(Instance.Locations[End]) : Instance.CachedRelativeLocations[End];
            return Local.IsValid != 0;
        }
        if (auto* Spline = Cast<IFGSplineBuildableInterface>(Actor))
        {
            // Raw spline points are valid in both preview and gameplay worlds;
            // the component and its generated meshes only initialize at BeginPlay.
            const auto& Points=Spline->GetSplinePointData();
            if (Points.Num()<2) return false;
            FInterpCurveVector Curve;
            for (int32 I=0; I<Points.Num(); ++I)
                Curve.Points.Emplace(float(I),Points[I].Location,Points[I].ArriveTangent,Points[I].LeaveTangent,CIM_CurveUser);
            FVector Min,Max; Curve.CalcBounds(Min,Max,FVector::ZeroVector);
            Local=FBox(Min,Max);
            if (const auto* Mesh=Spline->GetSplineMesh())
            {
                const FVector Extent=Mesh->GetBoundingBox().GetExtent();
                Local=Local.ExpandBy(FMath::Max(Extent.Y,Extent.Z));
            }
            return true;
        }
        if (auto* Foundation = Cast<AFGBuildableFoundation>(Actor); Foundation && FMath::IsNearlyZero(Foundation->mElevation))
        {
            const FVector Half(Foundation->mWidth / 2, Foundation->mDepth / 2, Foundation->mHeight / 2);
            Local = FBox(-Half, Half); return true;
        }
        TArray<FVector> Points; BSBlueprintBounds::AppendActorGeometry(Actor, Points);
        const FTransform Frame = Actor->GetActorTransform();
        for (const FVector& Point : Points) Local += Frame.InverseTransformPosition(Point);
        return Local.IsValid != 0;
    }
    FPlan Plan(AFGBlueprintHologram* Hologram, EBSDeduplication Mode)
    {
        FPlan Result; Result.Mode = Mode;
        if (Mode == EBSDeduplication::Off) return Result;
        UWorld* World = Hologram->GetWorld();
        auto* LW = AFGLightweightBuildableSubsystem::Get(World);
        auto* System = AFGBuildableSubsystem::Get(World);
        TSet<uint64> Replaced;
        for (const auto& Pair : Hologram->mBuildableToNewRoot)
        {
            if (!Pair.Value || !IsValid(Pair.Key)) continue;
            const auto* Template=TemplateData(Hologram,Pair.Key);
            if (!Template || !Template->Local.IsValid) continue;
            const FBox Local=Template->Local;
            const FTransform Transform = Pair.Value->GetComponentTransform();
            const FBox Query = Local.TransformBy(Transform).ExpandBy(.2);
            TArray<FBSBuildingRef> Candidates; int32 Examined = 0;
            FBSTransformNative::QueryLightweightBounds(World, Query, Candidates, Examined);
            if (System)
            {
                TArray<AFGBuildable*> Actors; System->GetCollidingBuildablesInBoundingBox(Actors, Query);
                for (auto* Actor : Actors)
                    if (IsValid(Actor) && !Actor->GetIsDismantled() && !Actor->GetIsLightweightTemporary())
                    {
                        Candidates.Add(FBSBuildingRef::FromActor(Actor));
                        // Cables need not occupy the buildable collision octree.
                        // Their endpoint owners do; inspect only these nearby ports.
                        if (Pair.Key->IsA<AFGBuildableWire>())
                            for (auto* Port : TInlineComponentArray<UFGCircuitConnectionComponent*>(Actor))
                            {
                                TArray<AFGBuildableWire*> Wires; Port->GetWires(Wires);
                                for (auto* Wire : Wires)
                                    if (IsValid(Wire) && !Wire->GetIsDismantled()) Candidates.Add(FBSBuildingRef::FromActor(Wire));
                            }
                    }
            }
            for (const auto& Candidate : Candidates)
            {
                UClass* Class = Candidate.LightweightClass ? Candidate.LightweightClass.Get() : Candidate.Actor->GetClass();
                if (!FBSDeduplication::SameType(World, Pair.Key->GetClass(), Class)) continue;
                FBox Other(ForceInit); FTransform OtherTransform = Candidate.Transform;
                if (Candidate.LightweightClass)
                {
                    const auto* Data = LW ? LW->GetRuntimeDataForBuildableClassAndIndex(Class, Candidate.RuntimeIndex) : nullptr;
                    if (!Data || !Data->IsValid()) continue;
                    Other = Data->BoundingBox; OtherTransform = Data->Transform;
                }
                else if (!Geometry(Candidate.Actor, Other)) continue;
                // Ignore cosmetic mesh lips between material variants. A
                // structural type uses the same canonical local box on both sides.
                if (Class != Pair.Key->GetClass() || Class->IsChildOf(AFGBuildableFoundation::StaticClass())) Other = Local;
                if (!FBSDeduplication::SameBounds(Local, Transform, Other, OtherTransform)) continue;
                // TOC references use the original preview-world paths. The
                // loader rebases those paths, so retain them before constructing.
                if (Mode == EBSDeduplication::Skip)
                {
                    const FString& Path = Template->Source;
                    if (!Path.IsEmpty()) Result.Skipped.Add(Path);
                }
                else if (!Replaced.Contains(FBSTransformNative::Key(Candidate)))
                { Replaced.Add(FBSTransformNative::Key(Candidate)); Result.Replace.Add(Candidate); }
            }
        }
        return Result;
    }
}
bool FBSDeduplication::SameType(UWorld* World, UClass* A, UClass* B)
{
    if (!A || !B) return false;
    if (A == B) return true;
    // The native material lookup only examines unlocked recipes. Deduplication
    // must also recognize an existing material variant before its recipe unlocks.
    static TWeakObjectPtr<AFGRecipeManager> CachedManager;
    static int32 CachedCount=INDEX_NONE;
    static TMap<TWeakObjectPtr<UClass>,TWeakObjectPtr<UClass>> Parents;
    auto Root = [&](UClass* Class)
    {
        UClass* Current=Class;
        while (const auto* Parent=Parents.Find(Current))
        { if (!Parent->IsValid() || Parent->Get()==Current) break; Current=Parent->Get(); }
        return Current;
    };
    auto* Manager=AFGRecipeManager::Get(World);
    if (!Manager) return false;
    const auto& Recipes=Manager->GetAllRecipes();
    if (CachedManager!=Manager || CachedCount!=Recipes.Num())
    {
        CachedManager=Manager; CachedCount=Recipes.Num(); Parents.Reset();
        for (const auto& Recipe : Recipes)
            if (Recipe && Recipe->IsChildOf(UFGCustomizationRecipe::StaticClass()))
            {
                const auto Descriptor=UFGCustomizationRecipe::GetCustomizationDescriptor(Recipe.Get());
                auto* Material=Descriptor ? Cast<UFGFactoryCustomizationDescriptor_Material>(Descriptor.GetDefaultObject()) : nullptr;
                if (!Material) continue;
                for (const auto& Pair : Material->GetBuildableMap())
                {
                    UClass* Source=Pair.Key.Get(); UClass* Target=AFGBuildable::GetBuildableClassFromRecipe(Pair.Value);
                    if (Source && Target)
                    { UClass* From=Root(Source); UClass* To=Root(Target); if (From!=To) Parents.Add(From,To); }
                }
            }
    }
    return Root(A)==Root(B);
}
bool FBSDeduplication::SameBounds(const FBox& A, const FTransform& AT, const FBox& B, const FTransform& BT)
{
    if (!A.IsValid || !B.IsValid || !AT.IsValid() || !BT.IsValid()) return false;
    constexpr double Tolerance = .1; // one millimeter; independent of building size
    if (!AT.TransformPosition(A.GetCenter()).Equals(BT.TransformPosition(B.GetCenter()), Tolerance)) return false;
    TArray<FVector> ACorners, BCorners;
    BSBlueprintBounds::AppendBox(A, AT, ACorners); BSBlueprintBounds::AppendBox(B, BT, BCorners);
    for (const FVector& Corner : ACorners)
        if (!BCorners.ContainsByPredicate([&](const FVector& Other) { return Corner.Equals(Other, Tolerance); })) return false;
    return true;
}
bool FBSDeduplication::SkipObject(const FString& Path)
{
    if (!ActivePlan || ActivePlan->Mode != EBSDeduplication::Skip) return false;
    for (const FString& Root : ActivePlan->Skipped)
        if (Path == Root || Path.StartsWith(Root + TEXT("."))) return true;
    return false;
}
void FBSDeduplication::RecordSource(AFGBuildable* Building, const FString& Path, bool Preview)
{
    if (!Building) return;
    if (Preview)
    {
        static uint64 LastCleanup = 0;
        if (LastCleanup != GFrameCounter)
        { LastCleanup = GFrameCounter; for (auto It = SourcePaths.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent(); }
        SourcePaths.Add(Building, Path);
    }
    if (ActivePlan) ActivePlan->Sources.Add(Building, Path);
}
FString FBSDeduplication::SourcePath(AFGBuildable* Building)
{
    if (ActivePlan) if (const FString* Path = ActivePlan->Sources.Find(Building)) return *Path;
    return SourcePaths.FindRef(Building);
}
EBSDeduplication FBSDeduplication::Mode(const AFGBlueprintHologram* Hologram)
{
    if (const auto* Received = ReceivedModes.Find(Hologram)) return *Received;
    auto* Player = Hologram ? Hologram->GetConstructionInstigator() : nullptr;
    auto* Edit = Player ? Player->FindComponentByClass<UBSEditModeComponent>() : nullptr;
    return Edit ? Edit->Deduplication : EBSDeduplication::Off;
}
bool FBSDeduplication::DeferGroups(TFunction<void()> Restore)
{
    if (!ActivePlan || ActivePlan->Mode != EBSDeduplication::Replace) return false;
    ActivePlan->DeferredGroups.Add(MoveTemp(Restore)); return true;
}
void FBSDeduplication::Install()
{
    if (WITH_EDITOR) return;
    SUBSCRIBE_METHOD(AFGBlueprintHologram::DuplicateMeshComponentsFromBuildableArray,
        [](auto&, AFGBlueprintHologram* Hologram, const TArray<AFGBuildable*>& Buildings)
        {
            for (auto It=PreviewData.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
            for (auto It=CostCache.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
            CostCache.Remove(Hologram);
            auto& Records=PreviewData.FindOrAdd(Hologram); Records.Reset();
            for (auto* Building : Buildings)
            {
                if (!IsValid(Building)) continue;
                auto& Record=Records.FindOrAdd(Building);
                Record.Source=SourcePath(Building); Geometry(Building,Record.Local);
            }
        });
    SubscribeBlueprintCost(
        [](auto& Scope, const AFGBlueprintHologram* Hologram)
        {
            if (Mode(Hologram) != EBSDeduplication::Skip) return;
            FPlan Work = Plan(const_cast<AFGBlueprintHologram*>(Hologram), EBSDeduplication::Skip);
            if (Work.Skipped.IsEmpty()) return;
            auto* System=AFGBlueprintSubsystem::Get(Hologram->GetWorld());
            if (!System) return;
            // SML's Windows struct-return trampoline assigns into the native
            // return buffer. The original call must construct its TArray first.
            (void)Scope(Hologram);
            // A header can contain refunds from source connections outside the
            // archive. Price the remaining archive members, not header-minus-refunds.
            auto& Cached=CostCache.FindOrAdd(Hologram);
            bool SameSet=Cached.Skipped.Num()==Work.Skipped.Num();
            for (const FString& Path : Work.Skipped) SameSet &= Cached.Skipped.Contains(Path);
            if (!SameSet)
            {
                Cached.Skipped=Work.Skipped; Cached.Cost.Reset();
                TGuardValue<FPlan*> Guard(ActivePlan,&Work);
                for (const auto& Pair : Hologram->mBuildableToNewRoot)
                    if (const auto* Template=TemplateData(Hologram,Pair.Key); Template && !Work.Skipped.Contains(Template->Source))
                    {
                        TArray<FItemAmount> MemberCost;
                        UBSClipboard::PreviewMemberCost(System,Pair.Key,MemberCost); Cached.Cost.Append(MemberCost);
                    }
                UFGInventoryLibrary::ConsolidateItemsAmount(Cached.Cost);
            }
            Scope.Override(Cached.Cost);
        });
    SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBlueprintHologram, SerializeConstructMessage,
        [](AFGBlueprintHologram* Hologram, FArchive& Ar, FNetConstructionID)
        {
            uint8 Value = uint8(Mode(Hologram)); Ar << Value;
            if (Ar.IsLoading())
            {
                for (auto It = ReceivedModes.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
                ReceivedModes.Add(Hologram, Value <= uint8(EBSDeduplication::Skip) ? EBSDeduplication(Value) : EBSDeduplication::Off);
            }
        });
    SUBSCRIBE_UOBJECT_METHOD(AFGBlueprintHologram, Construct,
        [](auto& Scope, AFGBlueprintHologram* Hologram, TArray<AActor*>& Children, FNetConstructionID Id)
        {
            const auto Choice = Mode(Hologram);
            if (!Hologram->HasAuthority() || Choice == EBSDeduplication::Off) return;
            FPlan Work = Plan(Hologram, Choice);
            if (Choice==EBSDeduplication::Skip && !Work.Skipped.IsEmpty() && Work.Skipped.Num()==Hologram->mBuildableToNewRoot.Num())
            { Scope.Override(static_cast<AActor*>(nullptr)); return; } // No empty blueprint proxy for an entirely skipped placement.
            auto* Player = Cast<AFGCharacterPlayer>(Hologram->GetConstructionInstigator());
            auto* Inventory = Player ? Player->GetInventory() : nullptr;
            if (!Player || !Inventory) { Scope.Override(static_cast<AActor*>(nullptr)); return; }
            Work.Replace.Sort([](const FBSBuildingRef& A, const FBSBuildingRef& B)
            {
                if (A.LightweightClass != B.LightweightClass) return GetNameSafe(A.LightweightClass.Get()) < GetNameSafe(B.LightweightClass.Get());
                return A.RuntimeIndex > B.RuntimeIndex;
            });
            TArray<AFGBuildable*> Targets;
            TArray<bool> CleanupBlocks;
            ON_SCOPE_EXIT
            {
                for (int32 I=0; I<Targets.Num(); ++I)
                    if (IsValid(Targets[I]) && Targets[I]->GetIsLightweightTemporary()) Targets[I]->SetBlockCleanupOfTemporary(CleanupBlocks[I]);
            };
            // Validate all replacements before the first mutation. Occupied
            // tracks or protected actors must not be removed as a side effect.
            for (const auto& Ref : Work.Replace)
            {
                auto* Actor = ReplacementActor(Hologram->GetWorld(), Ref);
                TArray<AActor*> Dependencies;
                if (Actor) IFGDismantleInterface::Execute_GetDismantleDependencies(Actor, Dependencies);
                if (!Actor || !IFGDismantleInterface::Execute_CanDismantle(Actor) || !Dependencies.IsEmpty())
                { UE_LOG(LogBuildingSkills, Warning, TEXT("Deduplication replacement rejected: target cannot be dismantled independently.")); Scope.Override(static_cast<AActor*>(nullptr)); return; }
                Targets.Add(Actor); CleanupBlocks.Add(Actor->GetBlockCleanupOfTemporary());
                if (Actor->GetIsLightweightTemporary()) Actor->SetBlockCleanupOfTemporary(true);
            }
            TGuardValue<FPlan*> Guard(ActivePlan, &Work);
            AActor* Result = Scope(Hologram, Children, Id);
            if (!Result) return;
            if (Choice==EBSDeduplication::Replace)
            {
                auto* Proxy=Cast<AFGBlueprintProxy>(Result);
                int32 Count=Proxy ? Proxy->GetBuildables().Num() : 0;
                if (Proxy) for (const auto& Bundle : Proxy->GetLightweightClassAndIndices()) Count+=Bundle.Indices.Num();
                if (Count<Hologram->mBuildableToNewRoot.Num())
                {
                    UE_LOG(LogBuildingSkills,Warning,TEXT("Deduplication kept existing buildings because blueprint construction was incomplete."));
                    for (auto& Restore : Work.DeferredGroups) Restore();
                    return;
                }
            }
            for (auto* Actor : Targets)
            {
                if (!IsValid(Actor)) continue;
                const FVector Position = Actor->GetActorLocation();
                TArray<FInventoryStack> Refund;
                IFGDismantleInterface::Execute_GetDismantleRefund(Actor, Refund, Inventory->GetNoBuildCost());
                IFGDismantleInterface::Execute_Dismantle(Actor);
                TArray<FInventoryStack> Overflow;
                for (auto Stack : Refund)
                {
                    const int32 Added = Inventory->AddStack(Stack, true);
                    Stack.NumItems -= Added; if (Stack.NumItems > 0) Overflow.Add(Stack);
                }
                if (!Overflow.IsEmpty()) FDismantleHelpers::DropRefundOnGroundNoActor(Hologram->GetWorld(), Position, nullptr, Overflow, Player);
            }
            for (auto& Restore : Work.DeferredGroups) Restore();
            UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint deduplication: mode=%d replaced=%d skipped=%d"), int32(Choice), Work.Replace.Num(), Work.Skipped.Num());
        });
}
bool UBSEditModeComponent::IsBlueprintBuilding() const
{
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* Gun = Player && Player->IsBuildGunEquipped() ? Player->GetBuildGun() : nullptr;
    auto* Build = Gun ? Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState()) : nullptr;
    return Build && Cast<AFGBlueprintHologram>(Build->GetHologram());
}
void UBSEditModeComponent::OnDeduplication()
{ if (IsBlueprintBuilding() && InputAllowed()) Deduplication = EBSDeduplication((uint8(Deduplication) + 1) % 3); }
void UBSEditModeComponent::TickDeduplication()
{
    if (!InputSubsystem.IsValid()) return;
    const bool Active = IsBlueprintBuilding();
    if (Active && !DeduplicationContext)
        if (auto* Source = LoadObject<UInputMappingContext>(nullptr, TEXT("/BuildingSkills/Inputs/MC_Deduplication.MC_Deduplication")))
        { DeduplicationContext = DuplicateObject<UInputMappingContext>(Source, this); InputSubsystem->AddMappingContext(DeduplicationContext, 1002); }
    if (!Active && DeduplicationContext)
    { InputSubsystem->RemoveMappingContext(DeduplicationContext); DeduplicationContext = nullptr; Deduplication = EBSDeduplication::Off; }
}
