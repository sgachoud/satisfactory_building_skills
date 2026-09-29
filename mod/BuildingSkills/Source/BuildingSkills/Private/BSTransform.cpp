#include "BSTransform.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

UBSTransformConfiguration::UBSTransformConfiguration()
{
    ConfigId = FConfigId{TEXT("BuildingSkills"), TEXT("")};
    DisplayName = FText::FromString(TEXT("Building Skills — Transform"));
    Description = FText::FromString(TEXT("Held step modifiers use the key bindings in Options > Keybindings > Building Skills. Movement is in centimetres, rotation in degrees, and scale is a multiplier. Scroll down uses the inverse multiplier."));
    RootSection = CreateDefaultSubobject<UConfigPropertySection>(TEXT("TransformSettings"));
    auto Add = [&](const TCHAR* Name, const TCHAR* Label, float Default)
    {
        auto* Property = CreateDefaultSubobject<UConfigPropertyFloat>(FName(Name));
        Property->DisplayName = FText::FromString(Label);
        Property->DefaultValue = Property->Value = Default;
        RootSection->SectionProperties.Add(Name, Property);
    };
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
    FTransform Result = Original;
    if (!Translation.IsNearlyZero()) { Result.AddToTranslation(Translation); return Result; }
    const FVector Axes = FBSTransformMath::Axes(Mode);
    if (uint8(Mode) < 3)
    {
        // Inverse scrolling exactly reverses the previous rotation step.
        const FQuat Delta(Axes.GetSafeNormal(), FMath::DegreesToRadians(Amount) * Axes.Size());
        Result.SetLocation(Pivot + Delta.RotateVector(Original.GetLocation() - Pivot));
        Result.SetRotation((Delta * Original.GetRotation()).GetNormalized());
    }
    else
    {
        const FVector Scale = FVector::OneVector + Axes * (Amount - 1);
        Result.SetLocation(Pivot + (Original.GetLocation() - Pivot) * Scale);
        // Actor local scale axes are retained; FTransform cannot encode shear.
        Result.SetScale3D(Original.GetScale3D() * Scale);
    }
    return Result;
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
