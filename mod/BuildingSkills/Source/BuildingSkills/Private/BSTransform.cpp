#include "BSTransform.h"
#include "Net/Core/Serialization/QuantizedVectorSerialization.h"

bool FBSTransformMath::MatchesInitialReplication(const FTransform& Authority, const FTransform& Observed)
{
    if (!Authority.IsValid() || !Observed.IsValid()) return false;
    // UPackageMapClient::SerializeNewActor sends location and scale as
    // FVector_NetQuantize10 and rotation as compressed shorts. Match the actual
    // spawn representation, not an arbitrary distance tolerance. Once edited,
    // our full-precision transport and per-building version must match instead.
    // Use the engine helper: packed vectors round negative ties away from zero,
    // unlike RoundToDouble. Reimplementing this rejects valid half-step spawns.
    auto Quantize = [](FVector Value) { return UE::Net::QuantizeVector(10, Value); };
    const FVector Scale = Authority.GetScale3D().Equals(FVector::OneVector, .01) ? FVector::OneVector : Quantize(Authority.GetScale3D());
    const auto Rotation = Authority.Rotator();
    const FRotator QuantizedRotation(
        FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(Rotation.Pitch)),
        FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(Rotation.Yaw)),
        FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(Rotation.Roll)));
    return Quantize(Authority.GetLocation()).Equals(Observed.GetLocation(), .01) &&
        Scale.Equals(Observed.GetScale3D(), .0001) &&
        FMath::RadiansToDegrees(QuantizedRotation.Quaternion().AngularDistance(Observed.GetRotation())) <= .002;
}
#include "Configuration/ConfigManager.h"
#include "BSDefaultsWidget.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

UBSTransformConfiguration::UBSTransformConfiguration()
{
    ConfigId = FConfigId{TEXT("BuildingSkills"), TEXT("")};
    DisplayName = FText::FromString(TEXT("Building Skills"));
    Description = FText::FromString(TEXT("Held step modifiers use the key bindings in Options > Keybindings > Building Skills. Movement is in centimetres, rotation in degrees, and scale is a multiplier. Scroll down uses the inverse multiplier."));
    RootSection = CreateDefaultSubobject<UBSDefaultsSection>(TEXT("TransformSettings"));
    auto Add = [&](const TCHAR* Name, const TCHAR* Label, float Default)
    {
        auto* Property = CreateDefaultSubobject<UConfigPropertyFloat>(FName(Name));
        Property->DisplayName = FText::FromString(Label);
        Property->DefaultValue = Property->Value = Default;
        RootSection->SectionProperties.Add(Name, Property);
    };
    Add(TEXT("ActionHistorySize"), TEXT("Undo history (actions per gun, 0 disables; server setting in multiplayer)"), 32);
    Add(TEXT("Move"), TEXT("Normal movement (cm)"), 100);
    Add(TEXT("TinyMove"), TEXT("Tiny movement (cm)"), 20);
    Add(TEXT("SmallMove"), TEXT("Small movement (cm)"), 50);
    Add(TEXT("LargeMove"), TEXT("Large movement (cm)"), 250);
    Add(TEXT("Rotate"), TEXT("Normal rotation (degrees)"), 15);
    Add(TEXT("TinyRotate"), TEXT("Tiny rotation (degrees)"), 1);
    Add(TEXT("SmallRotate"), TEXT("Small rotation (degrees)"), 5);
    Add(TEXT("LargeRotate"), TEXT("Large rotation (degrees)"), 45);
    Add(TEXT("Scale"), TEXT("Normal scale multiplier"), 1.1f);
    Add(TEXT("TinyScale"), TEXT("Tiny scale multiplier"), 1.01f);
    Add(TEXT("SmallScale"), TEXT("Small scale multiplier"), 2);
    Add(TEXT("LargeScale"), TEXT("Large scale multiplier"), 11);
    Add(TEXT("RepeatDelay"), TEXT("Held movement repeat delay (seconds)"), 0.15f);
    Add(TEXT("VolumeStep"), TEXT("Volume size step (meters)"), 8);
    Add(TEXT("VolumeRotation"), TEXT("Volume rotation step (degrees)"), 15);
    Add(TEXT("StickDistance"), TEXT("Stick initial distance (meters)"), 64);
    Add(TEXT("StickStep"), TEXT("Stick distance scroll step (meters)"), 1);
    Add(TEXT("StickTiltStep"), TEXT("Stick endpoint tilt step (degrees)"), 15);
    Add(TEXT("StickSlope"), TEXT("Stick upward slope (degrees)"), 0);
    Add(TEXT("StickRadius"), TEXT("Stick horizontal radius (meters; negative left, positive right, zero straight)"), 0);
    Add(TEXT("ConnectedGap"),TEXT("Connected gap (m)"),0);
    Add(TEXT("ConnectedLimit"),TEXT("Connected object limit"),64);
    Add(TEXT("ConnectedScope"),TEXT("Connected matching scope"),3);
    Add(TEXT("PlanSegmentLength"),TEXT("Rail segment length (m)"),80);
    Add(TEXT("PlanRemainder"),TEXT("Rail remainder policy"),1);
    Add(TEXT("PlanSignalEvery"),TEXT("Block signal every N segments"),1);
    { auto* V=CreateDefaultSubobject<UConfigPropertyBool>(TEXT("ConnectedIgnoreSwatch")); V->DisplayName=FText::FromString(TEXT("Connected: ignore swatch")); V->DefaultValue=V->Value=false; RootSection->SectionProperties.Add(TEXT("ConnectedIgnoreSwatch"),V); }
    { auto* V=CreateDefaultSubobject<UConfigPropertyBool>(TEXT("ConnectedIgnoreMaterial")); V->DisplayName=FText::FromString(TEXT("Connected: ignore material")); V->DefaultValue=V->Value=false; RootSection->SectionProperties.Add(TEXT("ConnectedIgnoreMaterial"),V); }
    { auto* V=CreateDefaultSubobject<UConfigPropertyBool>(TEXT("VerticalConstraint")); V->DisplayName=FText::FromString(TEXT("Stick vertical constraint")); V->DefaultValue=V->Value=true; RootSection->SectionProperties.Add(TEXT("VerticalConstraint"),V); }
    { auto* V=CreateDefaultSubobject<UConfigPropertyBool>(TEXT("HorizontalConstraint")); V->DisplayName=FText::FromString(TEXT("Stick horizontal constraint")); V->DefaultValue=V->Value=true; RootSection->SectionProperties.Add(TEXT("HorizontalConstraint"),V); }
    { auto* V=CreateDefaultSubobject<UConfigPropertyBool>(TEXT("PlanPlaceBlockSignal")); V->DisplayName=FText::FromString(TEXT("Place block signals")); V->DefaultValue=V->Value=false; RootSection->SectionProperties.Add(TEXT("PlanPlaceBlockSignal"),V); }
    { auto* V=CreateDefaultSubobject<UConfigPropertyBool>(TEXT("PlanLeftSideSignal")); V->DisplayName=FText::FromString(TEXT("Left-side block signals")); V->DefaultValue=V->Value=false; RootSection->SectionProperties.Add(TEXT("PlanLeftSideSignal"),V); }
    { auto* V=CreateDefaultSubobject<UConfigPropertyString>(TEXT("ConnectedWhitelist")); V->DisplayName=FText::FromString(TEXT("Connected name whitelist regex")); RootSection->SectionProperties.Add(TEXT("ConnectedWhitelist"),V); }
    { auto* V=CreateDefaultSubobject<UConfigPropertyString>(TEXT("ConnectedBlacklist")); V->DisplayName=FText::FromString(TEXT("Connected name blacklist regex")); RootSection->SectionProperties.Add(TEXT("ConnectedBlacklist"),V); }

}

FBSTransformSettings FBSTransformSettings::Get(UWorld* World)
{
    FBSTransformSettings Result;
    if (World && World->GetGameInstance())
        if (auto* Config = World->GetGameInstance()->GetSubsystem<UConfigManager>())
            Config->FillConfigurationStruct(FConfigId{TEXT("BuildingSkills"), TEXT("")}, FDynamicStructInfo{StaticStruct(), &Result});
    return Result;
}

FVector FBSTransformMath::Axes(EBSTransformMode Mode)
{
    const FVector Values[] = {{1,0,0}, {0,1,0}, {0,0,1}, {1,1,1}, {1,1,0}, {1,0,1}, {0,1,1}, {1,0,0}, {0,1,0}, {0,0,1}};
    return uint8(Mode) < UE_ARRAY_COUNT(Values) ? Values[uint8(Mode)] : FVector::ZeroVector;
}
FString FBSTransformMath::Name(EBSTransformMode Mode)
{
    const TCHAR* Names[] = {TEXT("Rotate X"), TEXT("Rotate Y"), TEXT("Rotate Z"), TEXT("Scale XYZ"), TEXT("Scale XY"), TEXT("Scale XZ"), TEXT("Scale YZ"), TEXT("Scale X"), TEXT("Scale Y"), TEXT("Scale Z")};
    return uint8(Mode) < UE_ARRAY_COUNT(Names) ? Names[uint8(Mode)] : TEXT("");
}
FTransform FBSTransformMath::Apply(const FTransform& Original, const FVector& Pivot, EBSTransformMode Mode, double Amount, const FVector& Translation)
{
    return ApplyInFrame(Original, Pivot, FQuat::Identity, Mode, Amount, Translation);
}

FVector FBSTransformMath::ViewTranslation(float Yaw, int32 Direction)
{
    // Use the nearest horizontal world axis, matching nudge controls while
    // keeping foundations aligned to the world grid as the camera turns.
    const float Snapped = FMath::RoundToFloat(Yaw / 90.0f) * 90.0f;
    const FVector Forward = FRotator(0, Snapped, 0).Vector();
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    const FVector Directions[] = {Forward, -Forward, -Right, Right, FVector::UpVector, -FVector::UpVector};
    return Direction >= 0 && Direction < 6 ? Directions[Direction] : FVector::ZeroVector;
}

FVector FBSTransformMath::FrameTranslation(const FQuat& Frame, const FVector& ViewDirection, int32 Direction)
{
    const FVector LocalView = Frame.UnrotateVector(ViewDirection);
    return Frame.RotateVector(ViewTranslation(LocalView.Rotation().Yaw, Direction));
}

bool FBSTransformMath::CombineSteps(EBSTransformMode Mode, double& Amount, FVector& Translation,
    double NextAmount, const FVector& NextTranslation, bool& Cancelled)
{
    Cancelled = false;
    const bool Moving = !Translation.IsNearlyZero();
    if (Moving != !NextTranslation.IsNearlyZero()) return false;
    if (Moving)
    {
        const FVector Sum = Translation + NextTranslation;
        if (Sum.ContainsNaN() || Sum.Size() > 10000) return false;
        Cancelled = Sum.IsNearlyZero();
        Translation = Sum;
    }
    else
    {
        const bool Rotating = uint8(Mode) < 3;
        const double Combined = Rotating ? Amount + NextAmount : Amount * NextAmount;
        if (!FMath::IsFinite(Combined) || (Rotating ? FMath::Abs(Combined) > 360 : Combined < .001 || Combined > 100)) return false;
        Cancelled = FMath::IsNearlyEqual(Combined, Rotating ? 0.0 : 1.0, 1.e-10);
        Amount = Combined;
    }
    return true;
}

FTransform FBSTransformMath::ApplyInFrame(const FTransform& Original, const FVector& Pivot, const FQuat& Frame,
    EBSTransformMode Mode, double Amount, const FVector& Translation)
{
    FTransform Result = Original;
    if (!Translation.IsNearlyZero()) { Result.AddToTranslation(Translation); return Result; }
    const FVector Axes = FBSTransformMath::Axes(Mode);
    if (uint8(Mode) < 3)
    {
        const FQuat Delta(Frame.RotateVector(Axes), FMath::DegreesToRadians(Amount));
        Result.SetLocation(Pivot + Delta.RotateVector(Original.GetLocation() - Pivot));
        Result.SetRotation((Delta * Original.GetRotation()).GetNormalized());
    }
    else
    {
        const FVector Scale = FVector::OneVector + Axes * (Amount - 1);
        Result.SetLocation(Pivot + Frame.RotateVector(Frame.UnrotateVector(Original.GetLocation() - Pivot) * Scale));
        // Project logarithmic stretch onto each local basis. This avoids shear,
        // maps a world axis correctly on quarter-turned buildings, preserves volume,
        // and makes reciprocal steps exact even for obliquely rotated buildings.
        FVector LocalScale = Original.GetScale3D();
        const FVector LogScale(FMath::Loge(Scale.X), FMath::Loge(Scale.Y), FMath::Loge(Scale.Z));
        for (int32 I = 0; I < 3; ++I)
        {
            FVector Basis = FVector::ZeroVector; Basis[I] = 1;
            const FVector V = Frame.UnrotateVector(Original.GetRotation().RotateVector(Basis));
            LocalScale[I] *= FMath::Exp(FVector::DotProduct(V * V, LogScale));
        }
        Result.SetScale3D(LocalScale);
    }
    return Result;
}
