#include "BSVolume.h"
#include "BSEditModeComponent.h"
#include "BSBlueprintBounds.h"
#include "FGCharacterPlayer.h"
#include "FGGameUserSettings.h"
#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Buildables/FGBuildable.h"
#include "Equipment/FGBuildGun.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Configuration/ConfigManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "UI/FGGameUI.h"

FBSVolumeSettings FBSVolumeSettings::Get(UWorld* World)
{
    FBSVolumeSettings S;
    if (World && World->GetGameInstance())
        if (auto* Config = World->GetGameInstance()->GetSubsystem<UConfigManager>())
            Config->FillConfigurationStruct(FConfigId{TEXT("BuildingSkills"), TEXT("")}, FDynamicStructInfo{StaticStruct(), &S});
    return S.Valid() ? S : FBSVolumeSettings{};
}
bool FBSVolumeSettings::Valid() const
{
    return FMath::IsFinite(VolumeStep) && VolumeStep >= .01f && VolumeStep <= 1000 &&
        FMath::IsFinite(VolumeRotation) && VolumeRotation >= .01f && VolumeRotation <= 180;
}
FLinearColor FBSVolumeSettings::Color()
{
    if (auto* Settings = UFGGameUserSettings::GetFGGameUserSettings())
        if (Settings->FindUserSetting(TEXT("BuildingSkills.VolumeColor")))
        {
            FLinearColor Color = Settings->GetLinearColorOptionValue(TEXT("BuildingSkills.VolumeColor"));
            Color.A = 1;
            return Color;
        }
    return FLinearColor(.6f, .12f, 1, 1);
}
bool FBSVolumeShape::Contains(const FVector& Point) const
{
    const FVector Local = Frame.InverseTransformPosition(Point);
    return Cube ? FBox(Min, Max).ExpandBy(.1).IsInsideOrOn(Local) : Local.SizeSquared() <= FMath::Square(Radius + .1);
}
FBox FBSVolumeShape::Bounds() const
{ return (Cube ? FBox(Min, Max) : FBox(FVector(-Radius), FVector(Radius))).TransformBy(Frame); }
void FBSVolumeShape::Resize(const FVector& View, int32 Delta, double Step, bool Opposite)
{
    if (!FMath::IsFinite(Step) || Step <= 0 || !Delta) return;
    const double Amount = FMath::Clamp(Delta, -10, 10) * Step;
    if (!Cube) { Radius = FMath::Clamp(Radius + Amount, 1.0, 100000.0); return; }
    const FVector Direction = Frame.InverseTransformVectorNoScale(View);
    const FVector Abs = Direction.GetAbs();
    const int32 Axis = Abs.X >= Abs.Y && Abs.X >= Abs.Z ? 0 : Abs.Y >= Abs.Z ? 1 : 2;
    const bool Positive = (Direction[Axis] >= 0) != Opposite;
    if (Positive) Max[Axis] = FMath::Clamp(Max[Axis] + Amount, 1.0, 100000.0);
    else Min[Axis] = -FMath::Clamp(-Min[Axis] + Amount, 1.0, 100000.0);
}
TArray<FBSBuildingRef> FBSVolumeShape::Find(UWorld* World, const FBSVolumeShape& Shape, FString& Error)
{
    Error.Reset();
    TArray<FBSBuildingRef> Candidates, Result;
    int32 Examined = 0;
    FBSTransformNative::QueryLightweightBounds(World, Shape.Bounds(), Candidates, Examined);
    if (auto* System = AFGBuildableSubsystem::Get(World))
    {
        TArray<AFGBuildable*> Actors;
        System->GetCollidingBuildablesInBoundingBox(Actors, Shape.Bounds());
        for (auto* Actor : Actors)
            if (IsValid(Actor) && !Actor->GetIsDismantled() && !Actor->GetIsLightweightTemporary()) Candidates.Add(FBSBuildingRef::FromActor(Actor));
    }
    auto* LW = AFGLightweightBuildableSubsystem::Get(World);
    TSet<uint64> Seen;
    for (const auto& Ref : Candidates)
    {
        const uint64 Key = FBSTransformNative::Key(Ref);
        if (Seen.Contains(Key)) continue;
        Seen.Add(Key);
        FBox Box(ForceInit);
        if (Ref.LightweightClass)
        {
            const auto* Data = LW ? LW->GetRuntimeDataForBuildableClassAndIndex(Ref.LightweightClass, Ref.RuntimeIndex) : nullptr;
            if (Data && Data->IsValid()) Box = Data->BoundingBox.TransformBy(Data->Transform);
        }
        else if (IsValid(Ref.Actor))
        { TArray<FVector> Points; BSBlueprintBounds::AppendActorGeometry(Ref.Actor, Points); for (const FVector& Point : Points) Box += Point; }
        if (!Box.IsValid || !Shape.Contains(Box.GetCenter())) continue;
        if (Result.Num() == 2048) { Error = TEXT("Volume exceeds 2048 buildings. Shrink it before selecting."); return {}; }
        Result.Add(Ref);
    }
    Result.Sort([](const FBSBuildingRef& A, const FBSBuildingRef& B) { return FBSTransformNative::Key(A) < FBSTransformNative::Key(B); });
    return Result;
}
bool UBSEditModeComponent::IsVolumeSelection() const
{
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* Gun = Player && Player->IsBuildGunEquipped() ? Player->GetBuildGun() : nullptr;
    if (!Gun || !Gun->IsInState(EBuildGunState::BGS_DISMANTLE) || bTransforming) return false;
    return Gun->GetCurrentBuildGunMode() == UBSVolumeModeDescriptor::StaticClass() || Gun->GetCurrentBuildGunMode() == UBSEditVolumeModeDescriptor::StaticClass();
}
void UBSEditModeComponent::TickVolume()
{
    const bool Active = IsVolumeSelection();
    if (!Active && VolumeWindow) CloseVolumeSettings();
    if (InputSubsystem.IsValid())
    {
        if (Active && !VolumeContext)
            if (auto* Source = LoadObject<UInputMappingContext>(nullptr, TEXT("/BuildingSkills/Inputs/MC_Volume.MC_Volume")))
            { VolumeContext = DuplicateObject<UInputMappingContext>(Source, this); InputSubsystem->AddMappingContext(VolumeContext, 1002); }
        if (!Active && VolumeContext) { InputSubsystem->RemoveMappingContext(VolumeContext); VolumeContext = nullptr; }
    }
    if (!Active)
    {
        if (VolumeMesh) VolumeMesh->SetVisibility(false);
        bVolumeInitialized = false;
        VolumeMembers = {};
        NextVolumeQuery = 0;
        return;
    }
    if (!bVolumeInitialized)
    {
        Volume = FBSVolumeShape{};
        VolumeSettings = FBSVolumeSettings::Get(GetWorld());
        NextVolumeQuery = 0;
        bVolumeInitialized = true;
    }
    FVector Center = GetOwner()->GetActorLocation();
    if (BlueprintAnchor.IsAlive(GetWorld()))
    {
        if (auto* Anchor = BlueprintAnchor.ResolveActor(GetWorld()))
        { TArray<FVector> Points; BSBlueprintBounds::AppendActorGeometry(Anchor, Points); FBox Box(ForceInit); for (const auto& P : Points) Box += P; Center = Box.IsValid ? Box.GetCenter() : Anchor->GetActorLocation(); }
    }
    Volume.Frame.SetLocation(Center);
    if (!VolumeMesh)
    {
        VolumeMesh = NewObject<UStaticMeshComponent>(GetOwner());
        VolumeMesh->SetAbsolute(true, true, true);
        VolumeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        VolumeMesh->SetGenerateOverlapEvents(false); VolumeMesh->SetCastShadow(false);
        VolumeMesh->SetCanEverAffectNavigation(false); VolumeMesh->RegisterComponent();
        if (auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/BuildingSkills/Materials/M_Volume.M_Volume")))
        { VolumeMaterial = UMaterialInstanceDynamic::Create(Material, this); VolumeMesh->SetMaterial(0, VolumeMaterial); }
    }
    // Closed, two-sided translucent surfaces remain visible from inside and outside.
    // These engine primitives are centered and measure 100 cm along each axis.
    auto* ShapeMesh = LoadObject<UStaticMesh>(nullptr, Volume.Cube
        ? TEXT("/Engine/BasicShapes/Cube.Cube") : TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (VolumeMesh->GetStaticMesh() != ShapeMesh) VolumeMesh->SetStaticMesh(ShapeMesh);
    const FVector LocalCenter = Volume.Cube ? (Volume.Min + Volume.Max) / 2 : FVector::ZeroVector;
    const FVector Size = Volume.Cube ? Volume.Max - Volume.Min : FVector(Volume.Radius * 2);
    VolumeMesh->SetWorldTransform(FTransform(Volume.Frame.GetRotation(),
        Volume.Frame.TransformPosition(LocalCenter), Size / 100));
    if (VolumeMaterial) { VolumeMaterial->SetVectorParameterValue(TEXT("Color"), FBSVolumeSettings::Color()); VolumeMaterial->SetScalarParameterValue(TEXT("Opacity"), .18f); }
    VolumeMesh->SetVisibility(InputAllowed());
}
FBSSelection UBSEditModeComponent::VolumeSelection()
{
    if (GetWorld()->GetRealTimeSeconds() >= NextVolumeQuery)
    {
        FString Error;
        VolumeMembers.Buildings = FBSVolumeShape::Find(GetWorld(), Volume, Error);
        // Successful background scans must not erase anchor interaction feedback.
        if (!Error.IsEmpty() || Feedback.StartsWith(TEXT("Volume exceeds"))) Feedback = MoveTemp(Error);
        NextVolumeQuery = GetWorld()->GetRealTimeSeconds() + .15;
    }
    return VolumeMembers;
}
void UBSEditModeComponent::OnVolumeShape()
{
    if (!IsVolumeSelection() || !InputAllowed()) return;
    Volume.Cube = !Volume.Cube; NextVolumeQuery = 0;
}
void UBSEditModeComponent::ScrollVolume(int32 Delta)
{
    if (!IsVolumeSelection() || !InputAllowed() || !Delta) return;
    auto* PC = Cast<APlayerController>(Cast<APawn>(GetOwner())->GetController());
    auto* Input = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
    auto Held = [&](const TCHAR* Path) { auto* Action = LoadObject<UInputAction>(nullptr, Path); return Input && Action && Input->GetActionValue(Action).Get<bool>(); };
    if (Volume.Cube && Held(TEXT("/BuildingSkills/Inputs/IA_VolumeRotate.IA_VolumeRotate")))
        Volume.Frame.SetRotation((FQuat(FVector::UpVector, FMath::DegreesToRadians(double(VolumeSettings.VolumeRotation) * Delta)) * Volume.Frame.GetRotation()).GetNormalized());
    else Volume.Resize(PC ? PC->GetControlRotation().Vector() : FVector::ForwardVector, Delta, VolumeSettings.VolumeStep * 100,
        Held(TEXT("/BuildingSkills/Inputs/IA_VolumeOpposite.IA_VolumeOpposite")));
    NextVolumeQuery = 0;
}
void UBSEditModeComponent::OnVolumeSettings()
{
    if(IsEditing()) { OnTransformerSettings(); return; }
    OpenVolumeSettings();
}
void UBSEditModeComponent::OpenVolumeSettings()
{
    if (!IsVolumeSelection() || !InputAllowed()) return;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner()); auto* UI = Player ? Player->GetGameUI() : nullptr;
    if (!UI) return;
    VolumeWindow = CreateWidget<UBSVolumeSettingsWidget>(UI->GetOwningPlayer());
    VolumeWindow->Draft = VolumeSettings; VolumeWindow->mInteractObject = this;
    VolumeWindow->SetDefaultFocusWidget(VolumeWindow); UI->PushWidget(VolumeWindow); VolumeWindow->SetInputMode();
}
void UBSEditModeComponent::CloseVolumeSettings()
{
    auto* Window = VolumeWindow.Get(); VolumeWindow = nullptr;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    if (Window && Player && Player->GetGameUI()) Player->GetGameUI()->PopWidget(Window);
    NextVolumeQuery = 0;
}
