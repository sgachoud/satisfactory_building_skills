#include "BSGroups.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildable.h"
#include "FGBackgroundThread.h"
#include "FGBuildableSubsystem.h"
#include "FGCharacterPlayer.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectIterator.h"

// Runs only in the opt-in fresh-world harness, with actual rendering enabled.
// Allow the native background pool to settle between phases; actor-transform
// assertions alone cannot detect fog covers stranded in its shared mesh pool.
bool FBSTransformNative::RunPooledVisualChecks(AFGCharacterPlayer* Player, int32 Phase)
{
    static TWeakObjectPtr<AFGBuildable> Fixture;
    static FTransform Original;
    static TArray<FTransform> LocalCovers;
    static TWeakObjectPtr<UStaticMesh> CoverMesh;
    auto* World = Player->GetWorld();
    bool Passed = true;
    auto Check = [&](const TCHAR* Name, bool Result)
    {
        Passed &= Result;
        UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME ASSERT: %s: %s"), Name, Result ? TEXT("PASS") : TEXT("FAIL"));
    };
    auto Aim = [&](const FTransform& Pose)
    {
        if (auto* PC = Cast<APlayerController>(Player->GetController()))
            PC->SetControlRotation((Pose.GetLocation() + FVector(0,0,170) - Player->GetPawnViewLocation()).Rotation());
    };
    if (Phase == 0)
    {
        auto* Class = LoadClass<AFGBuildable>(nullptr, TEXT("/Game/FactoryGame/Buildable/Factory/ConstructorMk1/Build_ConstructorMk1.Build_ConstructorMk1_C"));
        // Keep every pose inside the fog-plane pool's distance relevance limit.
        Original = FTransform(Player->GetActorLocation() + FVector(1000,1000,0));
        auto* Buildables = AFGBuildableSubsystem::Get(World);
        auto* Actor = Class && Buildables ? Buildables->BeginSpawnBuildable(Class, Original) : nullptr;
        if (Actor) Actor->FinishSpawning(Original);
        Fixture = Actor;
        // Use the actual decorator/pool settings. The older sparse fog-plane
        // locations differ from the constructor's current pooled decorations.
        auto* FogClass = LoadClass<UFGPoolableProxyComponentBase>(nullptr, TEXT("/Game/FactoryGame/-Shared/PooledComponents/BPC_PoolableFogPlane.BPC_PoolableFogPlane_C"));
        LocalCovers.Reset();
        CoverMesh.Reset();
        if (Actor && FogClass)
        {
            for (auto* Proxy : AFGDecorationTemplate::GetPoolAbleComponentsFromSubclass(Actor->GetDecorationTemplate()))
                if (Proxy && Proxy->IsA(FogClass)) LocalCovers.Add(Proxy->GetRelativeTransform());
            for (const auto& Type : UFGPoolSettings::GetPoolTypes())
                if (Type.mProxyComponent.LoadSynchronous() == FogClass) CoverMesh = Type.mVisual_Mesh.LoadSynchronous();
        }
        Check(TEXT("Pooled-cover constructor fixture has four native fog planes"), Actor && CoverMesh.IsValid() && LocalCovers.Num() == 4);
        Aim(Original);
        return Passed;
    }
    auto* Actor = Fixture.Get();
    Check(TEXT("Pooled-cover fixture remains available"), IsValid(Actor) && CoverMesh.IsValid());
    if (!Actor || !CoverMesh.IsValid()) return false;
    const FTransform Current = Actor->GetActorTransform();
    int32 Visible = 0, Misplaced = 0;
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
    {
        if (It->GetWorld() != World || It->GetStaticMesh() != CoverMesh.Get() ||
            !It->IsRegistered() || !It->IsVisible() || It->bHiddenInGame ||
            It->GetComponentScale().GetAbsMax() < .001) continue;
        // Constructor is the only fog-plane machine in this isolated fixture.
        ++Visible;
        bool Aligned = false;
        for (const auto& Local : LocalCovers)
        {
            const FTransform Expected = Local * Current;
            const FTransform Actual = It->GetComponentTransform();
            Aligned |= Actual.GetLocation().Equals(Expected.GetLocation(), 1) &&
                Actual.GetRotation().Equals(Expected.GetRotation(), .001) &&
                Actual.GetScale3D().Equals(Expected.GetScale3D(), .01);
        }
        if (!Aligned)
        {
            ++Misplaced;
            UE_LOG(LogBuildingSkills, Display, TEXT("POOLED COVER misplaced: %s pose=%s"), *It->GetPathName(), *It->GetComponentTransform().ToString());
        }
    }
    UE_LOG(LogBuildingSkills, Display, TEXT("POOLED COVER phase=%d visible=%d misplaced=%d handles=%d pose=%s"), Phase, Visible, Misplaced, Actor->mPoolHandles.Num(), *Current.ToString());
    Check(*FString::Printf(TEXT("Pooled covers follow settled constructor pose (phase %d)"), Phase), Visible > 0 && Misplaced == 0);
    Check(TEXT("Constructor retains exactly one set of pooled cover handles"), Actor->mPoolHandles.Num() == LocalCovers.Num());
    auto Change = Capture(Actor);
    switch (Phase)
    {
    case 1: Change.Target.AddToTranslation(FVector(300,100,200)); break;
    case 2: Change.Target.SetRotation(FRotator(0,30,0).Quaternion()); break;
    case 3: Change.Target.SetScale3D(FVector(1.2,.9,1.1)); break;
    case 4: Change.Target = Original; break; // Same Apply path used by undo/client replay.
    case 5:
        // Several changes before the background worker consumes re-registration.
        for (int32 Step = 0; Step < 20; ++Step)
        {
            auto Burst = Capture(Actor);
            Burst.Target.AddToTranslation(FVector(0,0,Step % 2 ? -10 : 10));
            Check(TEXT("Rapid constructor transform applies"), Apply(World, Burst));
        }
        break;
    case 6:
        Check(TEXT("Replayed constructor pose is idempotent"), Apply(World, Change));
        break;
    default: return Passed;
    }
    if (Phase < 5) Check(TEXT("Pooled-cover transform applies"), Apply(World, Change));
    Aim(Actor->GetActorTransform());
    return Passed;
}
