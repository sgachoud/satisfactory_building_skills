#include "BSVolume.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGCharacterPlayer.h"
#include "FGGameUserSettings.h"
#include "Settings/FGUserSettingApplyType.h"
#include "Settings/FGUserSetting.h"
#include "UI/FGOptionsValueController.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "FGRecipe.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Buildables/FGBuildable.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "EnhancedPlayerInput.h"
#include "InputKeyEventArgs.h"
#include "UI/FGGameUI.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/SWindow.h"

class FBSVolumeSmoke
{
public:
    static bool ClickWidget(TSharedRef<SWidget> Widget)
    {
        auto& App = FSlateApplication::Get();
        const auto Window = App.FindWidgetWindow(Widget);
        if (!Window) return false;
        const FVector2D Point = Widget->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(.5, .5));
        const TSet<FKey> Up, Down = {EKeys::LeftMouseButton};
        App.ProcessMouseMoveEvent(FPointerEvent(0, Point, App.GetCursorPos(), Up, EKeys::Invalid, 0, FModifierKeysState()));
        const bool Pressed = App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),
            FPointerEvent(0, Point, Point, Down, EKeys::LeftMouseButton, 0, FModifierKeysState()));
        const bool Released = App.ProcessMouseButtonUpEvent(FPointerEvent(0, Point, Point, Up, EKeys::LeftMouseButton, 0, FModifierKeysState()));
        return Pressed && Released;
    }
    static bool Run(AFGCharacterPlayer* Player, int32 Phase)
    {
        auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>();
        auto* PC = Cast<APlayerController>(Player->GetController());
        auto* Input = Cast<UEnhancedPlayerInput>(PC->PlayerInput);
        auto* Gun = Player->GetBuildGun(); auto* World = Player->GetWorld();
        static TArray<FBSBuildingRef> Refs;
        static FVector AnchorCenter;
        static bool AimAnchor = false, WasAnchored = false;
        if (Phase == -1)
        {
            if (AimAnchor && Refs.Num() == 2)
                PC->SetControlRotation((FBSTransformNative::Bounds(World,Refs[0]).GetCenter() - Player->GetPawnViewLocation()).Rotation());
            return true;
        }
        bool Passed = true;
        auto Check = [&](const TCHAR* Name, bool OK) { UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME %s: Volume: %s"), OK ? TEXT("PASS") : TEXT("FAIL"), Name); Passed &= OK; };
        auto Key = [&](FKey K, bool Down) { Input->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, K, Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f, false, 0)); };
        if (Phase == 0)
        {
            Key(EKeys::E,false); Key(EKeys::LeftAlt,false);
            auto* LW = AFGLightweightBuildableSubsystem::Get(World);
            auto* Class = LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x1_01.Build_Foundation_8x1_01_C"));
            auto* Recipe = LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Foundations/Recipe_Foundation_8x1_01.Recipe_Foundation_8x1_01_C"));
            if (!LW || !Class || !Recipe) { Check(TEXT("foundation fixture assets"),false); return false; }
            Refs.Reset();
            for (const double X : {100.0,1800.0})
            {
                FRuntimeBuildableInstanceData Data;
                Data.Transform = FTransform(Player->GetActorLocation()+FVector(X,0,0)); Data.BuiltWithRecipe = Recipe;
                Data.CustomizationData.SwatchDesc = Class->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(World);
                const int32 Index = LW->AddFromBuildableInstanceData(Class,Data);
                Refs.Add(FBSBuildingRef::FromLightweight(World,Class,Index));
            }
            Edit->ClearSelection();
            Gun->SetCurrentBuildGunMode(UBSEditVolumeModeDescriptor::StaticClass());
        }
        if (Refs.Num()!=2) return false;
        if (Phase == 1)
        {
            Check(TEXT("Edit Volume is active with scoped context"),Edit->IsEditing() && Edit->IsVolumeSelection() && Edit->VolumeContext);
            Check(TEXT("starts with eight meter radius and sixteen meter cube sides"), Edit->Volume.Radius == 800 && Edit->Volume.Min == FVector(-800) && Edit->Volume.Max == FVector(800));
            UE_LOG(LogBuildingSkills, Display, TEXT("Volume initial: frame=%s radius=%g min=%s max=%s hover=%d pending=%d"), *Edit->Volume.Frame.ToString(), Edit->Volume.Radius, *Edit->Volume.Min.ToString(), *Edit->Volume.Max.ToString(), Edit->Hover.Buildings.Num(), Edit->ExpectedPendingCount);
            FString Error; auto Found=FBSVolumeShape::Find(World,Edit->Volume,Error);
            Check(TEXT("sphere uses center inclusion for lightweight foundations"),Found.Contains(Refs[0])&&!Found.Contains(Refs[1]));
            Check(TEXT("translucent boundary is visible"),Edit->VolumeMesh && Edit->VolumeMesh->IsVisible() && Edit->VolumeMaterial);
            Check(TEXT("sphere boundary is a two-sided translucent surface"), Edit->VolumeMesh->GetStaticMesh()->GetName() == TEXT("Sphere") && Edit->VolumeMaterial->IsTwoSided() && Edit->VolumeMaterial->GetBlendMode() == BLEND_Translucent);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsVolumeSphere.png"),true,false);
            const FBox RenderBounds = Edit->VolumeMesh->Bounds.GetBox();
            Check(TEXT("rendered sphere bounds are sixteen meters across"), RenderBounds.GetCenter().Equals(Edit->Volume.Frame.GetLocation(), 1) && RenderBounds.GetSize().Equals(FVector(1600), 5));
            Check(TEXT("volume automatically highlights its contents before Ctrl"),Edit->Hover.Buildings.Contains(Refs[0]) && Edit->ExpectedPendingCount > 0);
            auto* Settings=UFGGameUserSettings::GetFGGameUserSettings();
            Check(TEXT("native color option is registered"), Settings && Settings->FindUserSetting(TEXT("BuildingSkills.VolumeColor")));
            auto* ColorOption = Settings ? Settings->FindUserSetting(TEXT("BuildingSkills.VolumeColor")) : nullptr;
            Check(TEXT("color option uses the native color picker"), ColorOption && Cast<UFGUserSetting_LinearColorPicker>(ColorOption->GetUserSetting()->ValueSelector) && ColorOption->GetUserSetting()->GetValueSelectorWidgetClass());
            if (ColorOption)
            {
                const FLinearColor Original = Settings->GetLinearColorOptionValue(TEXT("BuildingSkills.VolumeColor"));
                ColorOption->ForceSetValue(FVariant(FLinearColor(.2f,.7f,.4f,.1f)));
                const FLinearColor Preview = FBSVolumeSettings::Color();
                ColorOption->ForceSetValue(FVariant(Original));
                Check(TEXT("boundary follows native RGB option with fixed opacity"), Preview.Equals(FLinearColor(.2f,.7f,.4f,1)));
            }
            Key(EKeys::MouseScrollUp,true);
        }
        if (Phase == 2)
        {
            Key(EKeys::MouseScrollUp,false);
            Check(TEXT("physical wheel grows sphere by eight meters"),Edit->Volume.Radius==1600);
            Check(TEXT("expanded volume also previews ordinary actor buildings"), Edit->Hover.Buildings.ContainsByPredicate([](const FBSBuildingRef& Ref) { return IsValid(Ref.Actor) && !Ref.Actor->GetIsLightweightTemporary(); }));
            Key(EKeys::LeftAlt,true); Key(EKeys::V,true);
            PC->SetControlRotation(FRotator::ZeroRotator);
        }
        if (Phase == 3)
        {
            Key(EKeys::V,false); Key(EKeys::LeftAlt,false);
            Check(TEXT("Alt V switches to cube"),Edit->Volume.Cube);
            Key(EKeys::MouseScrollUp,true);
        }
        if (Phase == 4)
        {
            Key(EKeys::MouseScrollUp,false);
            Check(TEXT("physical wheel chooses positive X face only"),Edit->Volume.Max.X==1600 && Edit->Volume.Min.X==-800);
            Check(TEXT("cube surface matches the asymmetrically resized volume"), Edit->VolumeMesh->GetStaticMesh()->GetName() == TEXT("Cube") && Edit->VolumeMesh->Bounds.GetBox().GetCenter().Equals(Edit->Volume.Bounds().GetCenter(), 1) && Edit->VolumeMesh->Bounds.GetBox().GetSize().Equals(Edit->Volume.Bounds().GetSize(), 1));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsVolumeCube.png"),true,false);
            Key(EKeys::LeftAlt,true);
        }
        if (Phase == 5)
        {
            Key(EKeys::MouseScrollUp,true);
        }
        if (Phase == 6)
        {
            Key(EKeys::MouseScrollUp,false);
            Check(TEXT("Alt wheel changes negative X face only"),Edit->Volume.Min.X==-1600 && Edit->Volume.Max.X==1600);
            Key(EKeys::LeftAlt,false); Key(EKeys::LeftShift,true);
        }
        if (Phase == 7) Key(EKeys::MouseScrollUp,true);
        if (Phase == 8)
        {
            Key(EKeys::MouseScrollUp,false); Key(EKeys::LeftShift,false);
            Check(TEXT("Shift wheel rotates cube fifteen degrees"),FMath::IsNearlyEqual(Edit->Volume.Frame.Rotator().Yaw,15.0,.01));
            Edit->BlueprintAnchor=Refs[0]; Edit->TickVolume(); AnchorCenter=Edit->Volume.Frame.GetLocation();
            Player->AddActorWorldOffset(FVector(0,100,0)); Edit->TickVolume();
            Check(TEXT("anchor holds volume in place while player moves"),Edit->Volume.Frame.GetLocation().Equals(AnchorCenter,.01));
            Edit->Volume.Cube=false; Edit->Volume.Radius=800; Edit->NextVolumeQuery=0;
            Key(EKeys::LeftControl,true);
        }
        if (Phase == 9)
        {
            Key(EKeys::LeftControl,false);
            Check(TEXT("native Ctrl pins volume members"),Edit->Selection.Buildings.Contains(Refs[0]));
            Gun->SetCurrentBuildGunMode(UBSEditModeDescriptor::StaticClass());
        }
        if (Phase == 10)
        {
            Check(TEXT("mode switch retains pinned members and removes volume context"),Edit->Selection.Buildings.Contains(Refs[0])&&!Edit->VolumeContext);
            Gun->SetCurrentBuildGunMode(UBSEditVolumeModeDescriptor::StaticClass());
        }
        if (Phase == 11) Key(EKeys::K,true);
        if (Phase == 12)
        {
            Key(EKeys::K,false);
            Check(TEXT("K opens mouse-enabled Volume settings"),Edit->VolumeWindow && PC->bShowMouseCursor && !Edit->InputAllowed());
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsVolumeSettings.png"),true,false);
            TSharedPtr<SEditableText> Field;
            TFunction<void(TSharedRef<SWidget>)> FindField = [&](TSharedRef<SWidget> Widget)
            {
                if (Field) return;
                if (Widget->GetTypeAsString()==TEXT("SEditableText")) { Field=StaticCastSharedRef<SEditableText>(Widget); return; }
                auto* Children=Widget->GetChildren();
                for (int32 I=0; I<Children->Num(); ++I) FindField(Children->GetChildAt(I));
            };
            if (Edit->VolumeWindow) FindField(Edit->VolumeWindow->TakeWidget());
            Check(TEXT("numeric field accepts mouse focus"),Field && ClickWidget(Field.ToSharedRef()) && Field->HasKeyboardFocus());
            if (Field)
            {
                Field->SelectAllText();
                for (TCHAR Ch : FString(TEXT("4.5"))) FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Ch,FModifierKeysState(),0,false));
                FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
            }
        }
        if (Phase == 13)
        {
            Check(TEXT("typed value reaches settings draft"),Edit->VolumeWindow && FMath::IsNearlyEqual(Edit->VolumeWindow->Draft.VolumeStep,4.5f));
            Player->GetGameUI()->Native_HandlePauseGamePressed();
        }
        if (Phase == 14)
        {
            Check(TEXT("Escape closes Volume settings and restores input"),!Edit->VolumeWindow && Edit->InputAllowed());
            Edit->LeaveEditMode(false); Gun->SetCurrentBuildGunMode(UBSVolumeModeDescriptor::StaticClass());
        }
        if (Phase == 15)
        {
            Check(TEXT("Dismantle Volume is available without Edit"),!Edit->IsEditing() && Edit->IsVolumeSelection() && Edit->VolumeContext);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsVolumePreview.png"),true,false);
        }
        if (Phase == 16)
        {
            TArray<TSubclassOf<UFGBuildGunModeDescriptor>> Modes; Gun->GetCurrentState()->GetSupportedBuildModes(Modes);
            if (!Modes.IsEmpty()) Gun->SetCurrentBuildGunMode(Modes[0]);
        }
        if (Phase == 17) Check(TEXT("leaving Volume removes mesh and scoped controls"),!Edit->VolumeContext && (!Edit->VolumeMesh || !Edit->VolumeMesh->IsVisible()));
        if (Phase == 18) Gun->SetCurrentBuildGunMode(UBSVolumeModeDescriptor::StaticClass());
        if (Phase == 19)
        {
            Check(TEXT("reentering Volume resets the initial dimensions"), Edit->Volume.Radius == 800 && Edit->Volume.Min == FVector(-800) && Edit->Volume.Max == FVector(800));
            Edit->VolumeSettings.VolumeStep=4;
            Key(EKeys::MouseScrollDown,true);
        }
        if (Phase == 20)
        {
            Key(EKeys::MouseScrollDown,false);
            Check(TEXT("wheel shrinks sphere in Dismantle"), Edit->Volume.Radius==400);
            Key(EKeys::MouseScrollUp,true);
        }
        if (Phase == 21)
        {
            Key(EKeys::MouseScrollUp,false);
            Check(TEXT("wheel grows sphere in Dismantle"), Edit->Volume.Radius==800);
            Edit->ClearSelection(); Edit->BlueprintAnchor=Refs[0];
            Edit->Volume.Radius=2400; Edit->NextVolumeQuery=0;
        }
        if (Phase == 22)
        {
            Check(TEXT("anchored volume previews unpinned contents"), Edit->Hover.Buildings.Contains(Refs[1]) && !Edit->HoverHighlighted.IsEmpty());
            Check(TEXT("anchor preview does not implicitly pin objects"), Edit->Selection.IsEmpty());
            Key(EKeys::LeftControl,true);
        }
        if (Phase == 23)
        {
            Key(EKeys::LeftControl,false);
            Check(TEXT("Ctrl pins multiple anchored Volume contents"), Edit->Selection.Buildings.Contains(Refs[0]) && Edit->Selection.Buildings.Contains(Refs[1]));
        }
        if (Phase == 24)
        {
            Edit->ClearSelection();
            AimAnchor = true;
            auto* Target = Refs[0].ResolveActor(World);
            Check(TEXT("Dismantle anchor target exists"), IsValid(Target));
            if (Target)
            {
                const FVector Center = FBSTransformNative::Bounds(World,Refs[0]).GetCenter();
                Check(TEXT("anchor fixture positions player beside foundation"), Player->SetActorLocation(Center + FVector(-650,0,300), false, nullptr, ETeleportType::TeleportPhysics));
                PC->SetControlRotation((FBSTransformNative::Bounds(World,Refs[0]).GetCenter() - Player->GetPawnViewLocation()).Rotation());
            }
            Key(EKeys::LeftAlt,true);
        }
        if (Phase == 25)
        {
            FVector ViewLocation; FRotator ViewRotation; PC->GetPlayerViewPoint(ViewLocation,ViewRotation);
            UE_LOG(LogBuildingSkills,Display,TEXT("ANCHOR TARGET: player=%s view=%s rotation=%s control=%s bounds=%s hover=%s hit=%s"),
                *Player->GetActorLocation().ToString(), *ViewLocation.ToString(), *ViewRotation.ToString(), *PC->GetControlRotation().ToString(),
                *FBSTransformNative::Bounds(World,Refs[0]).ToString(), *GetNameSafe(Edit->HoverActor.Get()),
                *GetNameSafe(Gun->GetHitResult().GetActor()));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsDismantleAnchor.png"),true,false);
            Check(TEXT("Dismantle Volume traces the aimed foundation for anchoring"), Edit->HoverActor.IsValid() && FBSBuildingRef::FromActor(Edit->HoverActor.Get()) == Refs[0]);
            Key(EKeys::A,true);
        }
        if (Phase == 26)
        {
            Key(EKeys::A,false);
            WasAnchored = Edit->BlueprintAnchor == Refs[0];
            Check(TEXT("Alt A sets an anchor in Dismantle Volume"), !Edit->IsEditing() && Edit->BlueprintAnchor == Refs[0]);
            Check(TEXT("Dismantle Volume exposes the configured anchor hint"), Edit->GetControlsText().ToString().Contains(TEXT("Set / clear anchor")));
            Check(TEXT("Volume anchor has contextual feedback"), Edit->Feedback.StartsWith(TEXT("Volume anchor set")));
            AnchorCenter = Edit->Volume.Frame.GetLocation();
            Player->AddActorWorldOffset(FVector(0,100,0));
            PC->SetControlRotation((AnchorCenter - Player->GetPawnViewLocation()).Rotation());
        }
        if (Phase == 27)
        {
            Check(TEXT("Dismantle volume stays at placed anchor when player moves"), Edit->BlueprintAnchor == Refs[0] && Edit->Volume.Frame.GetLocation().Equals(AnchorCenter,.1));
            Key(EKeys::A,true);
        }
        if (Phase == 28)
        {
            Key(EKeys::A,false); Key(EKeys::LeftAlt,false);
            Check(TEXT("Alt A clears the aimed Dismantle anchor"), WasAnchored && !Edit->BlueprintAnchor.IsAlive(World));
            Check(TEXT("cleared Dismantle volume follows the player again"), Edit->Volume.Frame.GetLocation().Equals(Player->GetActorLocation(),.1));
        }
        return Passed;
    }
};
bool RunBuildingSkillsVolumeChecks(AFGCharacterPlayer* Player,int32 Phase) { return FBSVolumeSmoke::Run(Player,Phase); }
