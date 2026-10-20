#include "BSEditModeComponent.h"
#include "BSEditModeWidget.h"
#include "BSTransformSettingsWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/SWindow.h"
#include "BSBlueprintSave.h"
#include "BSBlueprintScale.h"
bool RunBuildingSkillsTransformerChecks(AFGCharacterPlayer* Player, int32 Phase);
bool RunBuildingSkillsPlanChecks(AFGCharacterPlayer* Player, int32 Phase);
bool RunBuildingSkillsPolishChecks(AFGCharacterPlayer* Player, int32 Phase);
bool RunBuildingSkillsNetworkCopyChecks(AFGCharacterPlayer* Player, int32 Phase);
bool RunBuildingSkillsBlueprintNetworkChecks(AFGCharacterPlayer* Player, int32 Phase);
void InstallBuildingSkillsBlueprintNetworkSmoke();
bool RunBuildingSkillsVolumeChecks(AFGCharacterPlayer* Player, int32 Phase);
bool RunBuildingSkillsConnectedChecks(AFGCharacterPlayer* Player, int32 Phase);
bool RunBuildingSkillsClipboardChecks(AFGCharacterPlayer* Player, const FBSSelection& Selection);
#include "FGBlueprintProxy.h"
#include "FGBlueprintSubsystem.h"
#include "FGBlueprintSettings.h"
#include "FGOutlineComponent.h"
#include "UObject/StructOnScope.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildable.h"
#include "Components/TextBlock.h"
#include "Components/EditableText.h"
#include "Components/MultiLineEditableText.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"
#include "UObject/UObjectArray.h"
#include "Components/UniformGridSlot.h"
#include "Components/BackgroundBlur.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "FGInputLibrary.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGSaveSession.h"
#include "FGPlayerInput.h"
#include "FGUnlockSubsystem.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
AFGBlueprintProxy* RunBuildingSkillsFoundationPlacementChecks(AFGCharacterPlayer* Player, bool& Passed);
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Patching/NativeHookManager.h"
#include "UObject/UObjectIterator.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"

// Opt-in packaged-game integration check. It opens a NEW session, blocks every
// save request, and exits automatically. Normal play never registers this ticker.
class FBSRuntimeSmoke
{
public:
    static inline bool ProbeClientForwarding = false;
    static inline bool ObservedClientForwarding = false;
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
    static bool Tick(float Delta)
    {
        static double Started = FPlatformTime::Seconds();
        static int32 Stage = 0;
        static double Next = 0;
        static int32 Samples = 0;
        static TWeakObjectPtr<AFGBuildable> TestBuilding;
        static TArray<TWeakObjectPtr<AFGBuildable>> Buildings;
        static bool ChecksPassed = true;
        static uint64 RebuildBaseline = 0, RefundBaseline = 0;
        static int32 ObjectBaseline = 0, WidgetBaseline = 0;
        static TArray<FBSBuildingRef> StressBuildings;
        static TArray<FBSBuildingRef> TransformBuildings;
        static FVector MoveStart;
        static FVector WheelPivot;
        static FString BlueprintTestName;
        static TArray<FBSBuildingRef> BlueprintMembers;
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 240)
        {
            UE_LOG(LogBuildingSkills, Error, TEXT("RUNTIME TEST FAILED: timed out waiting for a playable fresh world."));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return false;
        }
        UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        if (!World || !World->HasBegunPlay()) return true;
        if (auto* Save = UFGSaveSession::Get(World)) Save->SetAutoSaveEnabled(false);
        if (Stage == 0)
        {
            const FString Options = TEXT("sessionName=BuildingSkillsRuntimeTest?skipOnboarding=true");
            UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME TEST: opening fresh session with options %s"), *Options);
            UGameplayStatics::OpenLevel(World, TEXT("/Game/FactoryGame/Map/GameLevel01/Persistent_Level"), true, Options);
            Stage = 1;
            return true;
        }
        if (!World->GetMapName().Contains(TEXT("Persistent_Level"))) return true;
        auto* Player = Cast<AFGCharacterPlayer>(UGameplayStatics::GetPlayerCharacter(World, 0));
        auto* Edit = Player ? Player->FindComponentByClass<UBSEditModeComponent>() : nullptr;
        if (!Edit) return true;
        auto* PC = Cast<APlayerController>(Player->GetController());
        auto* Input = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
        auto Key = [Input](FKey Key, bool Down)
        {
            if (Input) Input->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Key,
                Down ? IE_Pressed : IE_Released, Down ? 1.0f : 0.0f, false, 0));
        };
        if (Stage == 1)
        {
            if (!Edit->InputAllowed() || !Player->CanEquipBuildGunForDismantle()) return true;
            if (auto* Unlocks = AFGUnlockSubsystem::Get(World)) Unlocks->UnlockBlueprints();
            Player->GetCharacterMovement()->DisableMovement();
            Player->SetActorLocation(Player->GetActorLocation() + FVector(0, 0, 4000), false, nullptr, ETeleportType::TeleportPhysics);
            auto* Class = LoadClass<AFGBuildable>(nullptr, TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C"));
            if (auto* Subsystem = AFGBuildableSubsystem::Get(World); Subsystem && Class && !FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsPooledVisualTest")) && !FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsConnectedTest")))
            {
                for (int32 Index = 0; Index < 3; ++Index)
                {
                    const FVector Offsets[] = {FVector(1400, 0, 0), FVector(0, 1400, 0), FVector(-1400, 0, 0)};
                    const FTransform Transform(FRotator::ZeroRotator, Player->GetActorLocation() + Offsets[Index]);
                    auto* Building = Subsystem->BeginSpawnBuildable(Class, Transform);
                    Building->SetBuiltWithRecipe(LoadClass<UFGRecipe>(nullptr, TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_StorageContainerMk1.Recipe_StorageContainerMk1_C")));
                    Building->FinishSpawning(Transform);
                    Buildings.Add(Building);
                }
                TestBuilding = Buildings[0];
            }
            Key(EKeys::LeftAlt, true);
            Key(EKeys::E, true);
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsNetworkTest"))) Samples=71;
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsBlueprintTest"))) Samples=79;
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsPooledVisualTest"))) { Samples=130; TestBuilding.Reset(); }
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsConnectedTest"))) { Samples=150; TestBuilding.Reset(); }
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsVolumeTest"))) { Samples=250; TestBuilding.Reset(); }
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsStickTest"))) { Samples=300; TestBuilding.Reset(); }
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsPlanTest"))) { Samples=550; TestBuilding.Reset(); }
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsPolishTest"))) { Samples=400; TestBuilding.Reset(); }
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsTransformersTest"))) { Samples=450; TestBuilding.Reset(); }
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsBlueprintNetworkTest"))) { Samples=500; TestBuilding.Reset(); }
            Stage = 2;
            Next = Now + (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsTransformersTest")) ? 18 : 1);
            return true;
        }
        if (TestBuilding.IsValid())
        {
            FVector Origin, Extent;
            TestBuilding->GetActorBounds(false, Origin, Extent);
            // Lightweight temporary actors do not own their instanced mesh bounds.
            if (TestBuilding->GetIsLightweightTemporary()) Origin = TestBuilding->GetActorLocation();
            Player->GetController()->SetControlRotation((Origin - Player->GetPawnViewLocation()).Rotation());
        }
        if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsVolumeTest"))) RunBuildingSkillsVolumeChecks(Player,-1);
        if (Now < Next) return true;
        Next = Now + 1;
        auto* Gun = Player->GetBuildGun();
        UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME TEST sample %d: equipped=%d state=%s mode=%s editing=%d intent=%d entering=%d allowed=%d indicator=%d viewport=%d visible=%d status=%s"),
            Samples, Player->IsBuildGunEquipped(), *GetNameSafe(Gun->GetCurrentState()), *GetNameSafe(Gun->GetCurrentBuildGunMode().Get()),
            Edit->IsEditing(), Edit->EditGun.IsValid(), Edit->bEnteringEdit, Edit->InputAllowed(), !!Edit->Indicator,
            Edit->Indicator && Edit->Indicator->IsInViewport(), Edit->Indicator && Edit->Indicator->IsVisible(), *Edit->GetStatusText().ToString());
        auto* Dismantle = Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState());
        auto Check = [&](const TCHAR* Description, bool Passed)
        {
            ChecksPassed &= Passed;
            UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME ASSERT: %s: %s"), Description, Passed ? TEXT("PASS") : TEXT("FAIL"));
        };
        static bool CheckedQueueCancellation = false;
        if (!CheckedQueueCancellation)
        {
            CheckedQueueCancellation = true;
            // Exercise the actual component queue before any command can be sent.
            Edit->QueueTransform(EBSTransformMode::ScaleXYZ, 0, FVector(100,0,0));
            Edit->QueueTransform(EBSTransformMode::ScaleXYZ, 0, FVector(-100,0,0));
            Check(TEXT("Opposite queued movement in scale mode leaves no zero-scale command"), Edit->TransformCommands.IsEmpty());
            Edit->TransformCommands.Reset();
            Edit->QueueTransform(EBSTransformMode::ScaleXYZ, 1.1, FVector::ZeroVector);
            Edit->QueueTransform(EBSTransformMode::ScaleXYZ, 1/1.1, FVector::ZeroVector);
            Check(TEXT("Reciprocal queued scaling leaves no redundant server command"), Edit->TransformCommands.IsEmpty());
            Edit->TransformCommands.Reset();
            Edit->TransformOptions.Gesture.Invalidate();
            Edit->RefreshTransformPreview();
        }
        auto CountWidgets = [World]()
        {
            int32 Count = 0;
            for (TObjectIterator<UUserWidget> It; It; ++It) if (It->GetWorld() == World) ++Count;
            return Count;
        };
        auto* Groups = ABSGroupSubsystem::Get(World);
        auto NativeDedupHintVisible = [&]()
        {
            bool Found = false;
            TSet<UUserWidget*> Visited;
            TFunction<void(UUserWidget*)> Visit = [&](UUserWidget* Widget)
            {
                if (!Widget || !Widget->IsVisible() || !Widget->WidgetTree || Visited.Contains(Widget)) return;
                Visited.Add(Widget);
                Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
                {
                    if (auto* Text = Cast<UTextBlock>(Child); Text && Text->IsVisible() && Text->GetText().ToString().Contains(TEXT("deduplication")))
                    {
                        const FVector2D Size = Text->GetCachedGeometry().GetLocalSize();
                        float Opacity = Widget->GetRenderOpacity();
                        for (UWidget* Parent = Text; Parent; Parent = Parent->GetParent())
                            Opacity *= Parent->IsVisible() ? Parent->GetRenderOpacity() : 0;
                        Found |= Size.X > 1 && Size.Y > 1 && Opacity > .5f;
                        UE_LOG(LogBuildingSkills, Display, TEXT("NATIVE DEDUP HINT: %s size=%s position=%s opacity=%g"), *Text->GetText().ToString(), *Size.ToString(), *Text->GetCachedGeometry().GetAbsolutePosition().ToString(), Opacity);
                    }
                    if (auto* Nested = Cast<UUserWidget>(Child)) Visit(Nested);
                });
            };
            if (auto* UI = Player->GetGameUI()) for (auto* Widget : UI->GetInteractWidgetStack()) Visit(Widget);
            return Found;
        };
        switch (Samples)
        {
        case 0: Key(EKeys::E, false); Key(EKeys::LeftAlt, false); break;
        case 5:
            UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME INPUT: class=%s mappings=%d controls=%s"), *GetNameSafe(Input ? Input->GetClass() : nullptr), Input ? Input->GetEnhancedActionMappings().Num() : -1, *Edit->GetControlsText().ToString());
            if (PC && PC->GetLocalPlayer())
                if (auto* Subsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
                {
                    const auto* Context = Edit->OperationsContext.Get();
                    UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME INPUT: operations applied=%d"), Context && Subsystem->HasMappingContext(Context));
                }
            if (Input)
                for (const auto& Mapping : Input->GetEnhancedActionMappings())
                    if (Mapping.Action && Mapping.Action->GetName().Contains(TEXT("Group")))
                        UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME MAPPING: action=%s name=%s key=%s"), *Mapping.Action->GetName(), *Mapping.GetMappingName().ToString(), *Mapping.Key.ToString());
            Check(TEXT("Edit is visible and input is allowed"), Edit->IsEditing() && Edit->InputAllowed() && Edit->Indicator && Edit->Indicator->IsVisible());
            Check(TEXT("Edit targets and highlights a real building"), Edit->Hover.Buildings.Num() == 1 && Dismantle && Dismantle->GetPendingDismantleActors().Num() == 1);
            Key(EKeys::LeftControl, true); break;
        case 6:
            Check(TEXT("Configured multi-select pins first building"), Edit->Selection.Buildings.Num() == 1);
            TestBuilding = Buildings[1]; break;
        case 7:
            Check(TEXT("Multi-select adds second building"), Edit->Selection.Buildings.Num() == 2);
            Key(EKeys::LeftControl, false); Gun->CycleBuildMode(1); break;
        case 8:
            Check(TEXT("Blueprint mode preserves selection"), Gun->GetCurrentBuildGunMode() == UBSEditBlueprintModeDescriptor::StaticClass() && Edit->Selection.Buildings.Num() == 2);
            Gun->CycleBuildMode(1); break;
        case 9:
            Check(TEXT("Group mode preserves selection"), Gun->GetCurrentBuildGunMode() == UBSEditGroupModeDescriptor::StaticClass() && Edit->Selection.Buildings.Num() == 2);
            Gun->CycleBuildMode(1); Key(EKeys::G, true); break;
        case 10:
            Key(EKeys::G, false);
            UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME GROUP: system=%d count=%d requestPending=%d feedback=%s"), !!Groups, Groups ? Groups->Groups.Num() : -1, Edit->bRequestPending, *Edit->Feedback);
            Check(TEXT("G creates a group through the input binding and server request"), Groups && Groups->Groups.Num() == 1 && Edit->Selection.Groups.Num() == 1 && Edit->ResolveSelection(Edit->Selection).Num() == 2);
            TestBuilding = Buildings[2]; break;
        case 11:
            Check(TEXT("Unselected hover remains outlined after pinning a group"), Edit->HoverHighlighted.Contains(Buildings[2].Get()) && Edit->HoverOutline && Edit->HoverOutline->GetOutlineStateColorForActor(Buildings[2].Get()) != EOutlineColor::OC_NONE);
            Check(TEXT("Hover does not enter the pinned selection"), Edit->Selection.Buildings.IsEmpty() && Edit->Selection.Groups.Num() == 1);
            Check(TEXT("Edit pinned and hover outlines share the green edit channel"),
                Edit->bEditColorsApplied && Edit->HoverOutline &&
                Edit->HoverOutline->GetOutlineStateColorForActor(Buildings[2].Get()) == EOutlineColor::OC_DISMANTLE &&
                Player->GetOutline()->GetOutlineStateColorForActor(Buildings[0].Get()) == EOutlineColor::OC_DISMANTLE);
            Key(EKeys::LeftControl, true); break;
        case 12:
            Key(EKeys::LeftControl, false);
            Check(TEXT("Group and single can be selected together"), Edit->Selection.Groups.Num() == 1 && Edit->Selection.Buildings.Num() == 1);
            Key(EKeys::M, true); break;
        case 13:
            Key(EKeys::M, false);
            Check(TEXT("M merges the single into the existing group"), Groups && Groups->Groups.Num() == 1 && Edit->Selection.Groups.Num() == 1 && Edit->ResolveSelection(Edit->Selection).Num() == 3);
            Key(EKeys::U, true); break;
        case 14:
            Key(EKeys::U, false);
            Check(TEXT("U ungroups the selected group"), Groups && Groups->Groups.IsEmpty() && Edit->Selection.Buildings.Num() == 3);
            if (Dismantle) Dismantle->PrimaryFire(); break;
        case 15:
            Check(TEXT("Edit cannot dismantle selected buildings"), Buildings.ContainsByPredicate([](const auto& B) { return !B.IsValid() || B->GetIsDismantled(); }) == false);
            Key(EKeys::LeftAlt, true); Key(EKeys::E, true); break;
        case 16:
            Key(EKeys::E, false); Key(EKeys::LeftAlt, false);
            Check(TEXT("Alt+E closes Edit"), !Edit->IsEditing() && !Player->IsBuildGunEquipped());
            Key(EKeys::F, true); break;
        case 17:
            Key(EKeys::F, false);
            Check(TEXT("Vanilla dismantle targets buildings again"), !Edit->IsEditing() && Edit->InputAllowed() && Dismantle && Dismantle->GetSelectedActor() == TestBuilding.Get());
            Key(EKeys::LeftControl, true); break;
        case 18:
            Check(TEXT("Vanilla dismantle pins a building"), Edit->Selection.Buildings.Num() == 1);
            TestBuilding = Buildings[0]; break;
        case 19:
            Check(TEXT("Vanilla dismantle selects multiple buildings"), Edit->Selection.Buildings.Num() == 2);
            Key(EKeys::LeftControl, false); Gun->CycleBuildMode(1); break;
        case 20:
            Check(TEXT("Vanilla mode change preserves selection"), Dismantle && Edit->Selection.Buildings.Num() == 2);
            Check(TEXT("Vanilla offers group selection"), Gun->GetCurrentBuildGunMode() == UBSGroupModeDescriptor::StaticClass());
            Gun->CycleBuildMode(1); break;
        case 21:
            Check(TEXT("Returning to vanilla normal preserves selection"), Edit->GetStatusText().ToString().Contains(TEXT("Normal")) && Edit->Selection.Buildings.Num() == 2);
            Key(EKeys::LeftMouseButton, true);
            break;
        case 24:
            Key(EKeys::LeftMouseButton, false);
            Check(TEXT("Vanilla dismantles both selected buildings"),
                (!Buildings[0].IsValid() || Buildings[0]->GetIsDismantled()) && (!Buildings[2].IsValid() || Buildings[2]->GetIsDismantled()));
            Check(TEXT("Vanilla leaves the unselected building intact"), Buildings[1].IsValid() && !Buildings[1]->GetIsDismantled());
            break;
        case 25:
            Key(EKeys::LeftAlt, true); Key(EKeys::E, true); break;
        case 26:
        {
            Key(EKeys::E, false); Key(EKeys::LeftAlt, false);
            TestBuilding = Buildings[1];
            auto* LW = AFGLightweightBuildableSubsystem::Get(World);
            auto* Class = LoadClass<AFGBuildable>(nullptr, TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x4_01.Build_Foundation_8x4_01_C"));
            auto* Recipe = LoadClass<UFGRecipe>(nullptr, TEXT("/Game/FactoryGame/Recipes/Buildings/Foundations/Recipe_Foundation_8x4_01.Recipe_Foundation_8x4_01_C"));
            if (LW && Class && Recipe)
            {
                for (int32 Index = 0; Index < 128; ++Index)
                {
                    FRuntimeBuildableInstanceData Data;
                    Data.Transform = FTransform(Player->GetActorLocation() + FVector(2000 + (Index % 16) * 800, (Index / 16) * 800, -600));
                    Data.BuiltWithRecipe = Recipe;
                    Data.CustomizationData.SwatchDesc = Class->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(World);
                    const int32 RuntimeIndex = LW->AddFromBuildableInstanceData(Class, Data);
                    StressBuildings.Add(FBSBuildingRef::FromLightweight(World, Class, RuntimeIndex));
                }
            }
            Check(TEXT("Stress fixture has 128 valid lightweight foundations"), StressBuildings.Num() == 128 &&
                !StressBuildings.ContainsByPredicate([&](const auto& Ref) { return !Ref.IsAlive(World); }));
            Edit->Selection.Buildings = StressBuildings;
            Edit->Selection.Buildings.Add(FBSBuildingRef::FromActor(Buildings[1].Get()));
            Edit->bSelectionDirty = true;
            Key(EKeys::G, true);
            break;
        }
        case 27:
            Key(EKeys::G, false);
            Check(TEXT("Mixed actor/lightweight group created"), Edit->Selection.Groups.Num() == 1 && Edit->ResolvedCount == 129);
            Edit->ClearSelection();
            Gun->SetCurrentBuildGunMode(UBSEditGroupModeDescriptor::StaticClass());
            break;
        case 28:
            Check(TEXT("Hover alone expands a 129-building group"), Edit->Selection.IsEmpty() && Edit->Hover.Groups.Num() == 1 &&
                Dismantle && Dismantle->GetPendingDismantleActors().Num() == 129);
            RebuildBaseline = Edit->SelectionRebuilds;
            RefundBaseline = Edit->RefundRefreshes;
            ObjectBaseline = GUObjectArray.GetObjectArrayNum();
            WidgetBaseline = CountWidgets();
            Check(TEXT("Edit has native bottom hints"), Edit->ControlHints && Edit->Indicator && Edit->Indicator->IsInViewport() &&
                !Edit->LastControlHints.IsEmpty() && (FParse::Param(FCommandLine::Get(), TEXT("nullrhi")) ||
                Edit->ControlHints->GetCachedGeometry().GetAbsolutePosition().Y > 500));
            break;
        case 38:
            Check(TEXT("Stable actor group hover does not rebuild selection or refunds"),
                Edit->SelectionRebuilds == RebuildBaseline && Edit->RefundRefreshes == RefundBaseline);
            TestBuilding = StressBuildings[0].ResolveActor(World);
            break;
        case 40:
            Check(TEXT("Hovering a lightweight member selects the same complete group"),
                Edit->Hover.Groups.Num() == 1 && Dismantle && Dismantle->GetPendingDismantleActors().Num() == 129);
            RebuildBaseline = Edit->SelectionRebuilds;
            RefundBaseline = Edit->RefundRefreshes;
            ObjectBaseline = GUObjectArray.GetObjectArrayNum();
            WidgetBaseline = CountWidgets();
            break;
        case 48:
            UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME STRESS: rebuilds=%llu refunds=%llu newObjectSlots=%d widgetDelta=%d"),
                Edit->SelectionRebuilds - RebuildBaseline, Edit->RefundRefreshes - RefundBaseline, GUObjectArray.GetObjectArrayNum() - ObjectBaseline, CountWidgets() - WidgetBaseline);
            Check(TEXT("Stable lightweight group hover does not rebuild selection or refunds"),
                Edit->SelectionRebuilds == RebuildBaseline && Edit->RefundRefreshes == RefundBaseline);
            Check(TEXT("Group hover has no accumulating UI widgets"), CountWidgets() - WidgetBaseline <= 32);
            Check(TEXT("All 129 group members remain highlighted"), Dismantle && Dismantle->GetPendingDismantleActors().Num() == 129);
            TestBuilding = Buildings[1];
            Key(EKeys::LeftAlt, true); Key(EKeys::E, true); break;
        case 49:
            Key(EKeys::E, false); Key(EKeys::LeftAlt, false);
            Key(EKeys::F, true); break;
        case 50:
            Key(EKeys::F, false);
            Gun->SetCurrentBuildGunMode(UBSGroupModeDescriptor::StaticClass()); break;
        case 51:
            Check(TEXT("Vanilla group hover expands actor and lightweight members"), Dismantle && Dismantle->GetPendingDismantleActors().Num() == 129);
            Check(TEXT("Dismantle restores the native outline style and palette"),
                !Edit->bEditColorsApplied && Player->GetOutline()->GetOutlineStateColorForActor(Buildings[1].Get()) == EOutlineColor::OC_DISMANTLE);
            RebuildBaseline = Edit->SelectionRebuilds;
            RefundBaseline = Edit->RefundRefreshes;
            ObjectBaseline = GUObjectArray.GetObjectArrayNum();
            WidgetBaseline = CountWidgets();
            break;
        case 56:
            UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME DISMANTLE STRESS: rebuilds=%llu refunds=%llu newObjectSlots=%d widgetDelta=%d"),
                Edit->SelectionRebuilds - RebuildBaseline, Edit->RefundRefreshes - RefundBaseline, GUObjectArray.GetObjectArrayNum() - ObjectBaseline, CountWidgets() - WidgetBaseline);
            Check(TEXT("Vanilla group hover avoids repeated refund/selection rebuilds"),
                Edit->SelectionRebuilds == RebuildBaseline && Edit->RefundRefreshes == RefundBaseline);
            Check(TEXT("Vanilla group hover has no accumulating UI widgets"), CountWidgets() - WidgetBaseline <= 32);
            break;
        case 57:
        {
            auto* LW = AFGLightweightBuildableSubsystem::Get(World);
            const auto Original = StressBuildings[0];
            const int32 OriginalIndex = Original.ResolveIndex(World);
            FRuntimeBuildableInstanceData Data;
            Data.Transform = Original.Transform;
            Data.BuiltWithRecipe = Original.Recipe;
            Data.CustomizationData.SwatchDesc = Original.LightweightClass->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(World);
            const int32 OverlapIndex = LW->AddFromBuildableInstanceData(Original.LightweightClass, Data);
            Check(TEXT("Cached lightweight lookup rejects an added coincident instance"), !Original.IsAlive(World));
            LW->RemoveByInstanceIndex(Original.LightweightClass, OverlapIndex);
            Check(TEXT("Cached lightweight lookup recovers after ambiguity is removed"), Original.ResolveIndex(World) == OriginalIndex);
            LW->RemoveByInstanceIndex(Original.LightweightClass, OriginalIndex);
            Data = FRuntimeBuildableInstanceData();
            Data.Transform = FTransform(Original.Transform.GetLocation() + FVector(0, 0, -1200));
            Data.BuiltWithRecipe = Original.Recipe;
            Data.CustomizationData.SwatchDesc = Original.LightweightClass->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(World);
            LW->AddFromBuildableInstanceData(Original.LightweightClass, Data);
            Check(TEXT("Cached lightweight lookup never adopts a reused slot elsewhere"), !Original.IsAlive(World));
            break;
        }
        case 58:
            Key(EKeys::LeftAlt, true); Key(EKeys::E, true); break;
        case 59:
            Key(EKeys::E, false); Key(EKeys::LeftAlt, false);
            Edit->ClearSelection();
            TransformBuildings = {FBSBuildingRef::FromActor(Buildings[1].Get()), StressBuildings[1], StressBuildings[2]};
            Edit->Selection.Buildings = TransformBuildings;
            Edit->OnGroup();
            break;
        case 60: Key(EKeys::T, true); break;
        case 61:
            Key(EKeys::T, false);
            Check(TEXT("T enters Transform with all three group members"), Edit->bTransforming && Edit->ResolvedCount == 3 && Edit->TransformMode == EBSTransformMode::RotateX);
            Check(TEXT("Transform replaces normal edit hints"), Edit->GetControlsText().ToString().Contains(TEXT("Return to Edit")) && !Edit->GetControlsText().ToString().Contains(TEXT("Multi-select")) && !Edit->GetControlsText().ToString().Contains(TEXT("Merge")));
            for (const TCHAR* Name : {TEXT("InfiniteNudge.TinyNudge"), TEXT("InfiniteNudge.SmallNudge"), TEXT("InfiniteNudge.LargeNudge")})
                UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME NUDGE REFERENCE: %s=%s"), Name, *UFGInputLibrary::GetInputActionNameAsText_Keyboard(PC,FName(Name)).ToString());
            {
                FBox Bounds(ForceInit);
                for (const auto& Ref : TransformBuildings) Bounds += FBSTransformNative::Bounds(World, Ref);
                WheelPivot = Bounds.GetCenter();
                Check(TEXT("Default transform pivot is the combined selection bounds center"), Edit->GetTransformPivot().Equals(WheelPivot, .01));
            }
            Key(EKeys::R, true); break;
        case 62:
            Key(EKeys::R, false);
            Check(TEXT("Native R changes transform mode without changing selection mode"), Edit->TransformMode == EBSTransformMode::RotateY && Edit->Selection.Groups.Num() == 1);
            Key(EKeys::MouseScrollUp, true);
            break;
        case 63:
        {
            Key(EKeys::MouseScrollUp, false);
            const auto Members = Edit->ResolveSelection(Edit->Selection);
            bool Moved = Members.Num() == 3;
            const FVector Pivot = WheelPivot;
            for (int32 I=0; I<Members.Num() && I<3; ++I)
            {
                const auto Expected = FBSTransformMath::Apply(TransformBuildings[I].Transform, Pivot, EBSTransformMode::RotateY, 15, FVector::ZeroVector);
                const auto Actual = Members[I].LightweightClass ? Members[I].Transform : Members[I].Actor->GetActorTransform();
                Moved &= Actual.Equals(Expected, .01);
            }
            Check(TEXT("Mouse wheel rotates actor and native lightweight records around shared pivot"), Moved);
            Edit->ScrollTransform(-1);
            break;
        }
        case 64:
        {
            bool AllModes = true;
            FBSSelection Selected = Edit->Selection, Result;
            FString Error;
            for (int32 I=0; I<10; ++I)
            {
                const auto Mode = EBSTransformMode(I);
                if (!Groups->TransformSelection(Player, Selected, Groups->Revision, Mode, I<3 ? 15 : 1.1, FVector::ZeroVector, Result, Error)) { AllModes = false; break; }
                Selected = Result;
                if (!Groups->TransformSelection(Player, Selected, Groups->Revision, Mode, I<3 ? -15 : 1.0/1.1, FVector::ZeroVector, Result, Error)) { AllModes = false; break; }
                Selected = Result;
            }
            Edit->Selection = Selected;
            for (const auto& Ref : TransformBuildings)
                AllModes &= Ref.LightweightClass ? Ref.IsAlive(World) : Ref.Actor->GetActorTransform().Equals(Ref.Transform,.01);
            Check(TEXT("All ten modes transform and inverse correctly in native game data"), AllModes);
            auto* LW = AFGLightweightBuildableSubsystem::Get(World);
            const auto& Ref = TransformBuildings[1];
            auto* Data = LW->GetRuntimeDataForBuildableClassAndIndex(Ref.LightweightClass, Ref.ResolveIndex(World));
            Check(TEXT("Transformed lightweight still has collision and visual handles"), Data && Data->IsValid() && Data->Handles[0]->IsInstanced());
            const int32 OldRevision = Groups->Revision;
            const bool Invalid = Groups->TransformSelection(Player, Selected, OldRevision, EBSTransformMode::ScaleX, -2, FVector::ZeroVector, Result, Error);
            Check(TEXT("Invalid transform is rejected without partial mutation"), !Invalid && Groups->Revision == OldRevision);
            Edit->TransformMode = EBSTransformMode::RotateX;
            bool Cycle = true;
            for (int32 I=1; I<=10; ++I) { Gun->CycleBuildMode(1); Cycle &= int32(Edit->TransformMode) == I%10; }
            Check(TEXT("Mode cycles in requested ten-mode order and wraps"), Cycle);
            break;
        }
        case 65:
            MoveStart = TransformBuildings[0].Actor->GetActorLocation();
            TestBuilding.Reset();
            PC->SetControlRotation(FRotator(0,90,0));
            Key(EKeys::LeftControl,true); Key(EKeys::Up,true);
            break;
        case 66:
        {
            Key(EKeys::Up,false); Key(EKeys::LeftControl,false);
            const FVector MovementDelta = TransformBuildings[0].Actor->GetActorLocation() - MoveStart;
            Check(TEXT("Arrow movement follows facing, uses held Tiny step and keeps group selection"), MovementDelta.Y >= 20 && FMath::Abs(MovementDelta.X)<.01 && FMath::Abs(MovementDelta.Z)<.01 && Edit->Selection.Groups.Num()==1);
            MoveStart = TransformBuildings[0].Actor->GetActorLocation();
            Key(EKeys::PageUp,true); break;
        }
        case 67:
            Key(EKeys::PageUp,false);
            Check(TEXT("Page Up moves selected buildings along Z"), TransformBuildings[0].Actor->GetActorLocation().Z > MoveStart.Z);
            TestBuilding.Reset();
            PC->SetControlRotation((Edit->TransformPivot - Player->GetPawnViewLocation()).Rotation());
            break;
        case 68:
            if (!FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsTransformTest.png"),true,false);
            break;
        case 69:
            // Exercise the native Escape route used by the pause input.
            Player->GetGameUI()->Native_HandlePauseGamePressed();
            break;
        case 70:
            Check(TEXT("Escape returns to normal Edit and keeps transformed selection"), Edit->IsEditing() && !Edit->bTransforming && Edit->Selection.Groups.Num()==1 && !UGameplayStatics::IsGamePaused(World));
            Check(TEXT("Normal edit controls return after Escape"), Edit->GetControlsText().ToString().Contains(TEXT("Multi-select")) && Edit->GetControlsText().ToString().Contains(TEXT("Transform")));
            break;
        case 71:
            Key(EKeys::E,false); Key(EKeys::LeftAlt,false);
            ChecksPassed &= FBSTransformNative::RunNetworkChecks(Player);
            ChecksPassed &= RunBuildingSkillsNetworkCopyChecks(Player,0);
            break;
        case 72:
            TestBuilding.Reset();
            PC->SetControlRotation((Player->GetActorLocation()+FVector(1800,1600,100)-Player->GetPawnViewLocation()).Rotation());
            break;
        case 73:
            if (!FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsNetworkTest.png"),true,false);
            break;
        case 74:
            ChecksPassed &= FBSTransformNative::RunSettledNetworkChecks(Player,0);
            ChecksPassed &= RunBuildingSkillsNetworkCopyChecks(Player,1);
            break;
        case 78:
            ChecksPassed &= FBSTransformNative::RunSettledNetworkChecks(Player,1);
            ChecksPassed &= RunBuildingSkillsNetworkCopyChecks(Player,2);
            if (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsNetworkTest"))) Samples=209;
            break;
        case 79:
            if(FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsBlueprintTest")))
            {
                Key(EKeys::E,false); Key(EKeys::LeftAlt,false);
                auto* LW=AFGLightweightBuildableSubsystem::Get(World);
                auto* Class=LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x4_01.Build_Foundation_8x4_01_C"));
                auto* Recipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Foundations/Recipe_Foundation_8x4_01.Recipe_Foundation_8x4_01_C"));
                // Focused regression: no actor-backed machine may hide missing
                // bounds on an entirely lightweight group.
                Edit->Selection.Buildings.Reset();
                if(LW && Class && Recipe) for(int32 I=0;I<3;++I)
                {
                    FRuntimeBuildableInstanceData Data;
                    const FTransform GroupFrame(FRotator(0,30,0), Player->GetActorLocation()+FVector(1600,0,-600));
                    Data.Transform=FTransform(FQuat::Identity, FVector(0,I*800,0),
                        I == 1 ? FVector(.5,.75,.5) : FVector::OneVector) * GroupFrame;
                    Data.BuiltWithRecipe=Recipe;
                    Data.CustomizationData.SwatchDesc = Class->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(World);
                    const int32 Index=LW->AddFromBuildableInstanceData(Class,Data);
                    Edit->Selection.Buildings.Add(FBSBuildingRef::FromLightweight(World,Class,Index));
                }
                Edit->OnGroup();
            }
            break;
        case 80:
            Check(TEXT("Single group offers Save in native gun hints"), Edit->GetControlsText().ToString().Contains(TEXT("Save")));
            Key(EKeys::LeftAlt,true); Key(EKeys::S,true);
            break;
        case 81:
        {
            Key(EKeys::S,false); Key(EKeys::LeftAlt,false);
            Check(TEXT("Alt+S opens the native blueprint designer window"), IsValid(Edit->BlueprintSession) && IsValid(Edit->BlueprintWindow));
            auto* Window=Edit->BlueprintWindow.Get();
            if(!Window) break;
            if(FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsBlueprintTest")))
            {
                Check(TEXT("Foundation-only blueprint bounds cover all three foundations"),
                    Edit->BlueprintSession->GetBlueprintDimensions()==FIntVector(1,3,1));
                Check(TEXT("Group blueprint aligns to the rotated foundations while remaining upright"),
                    Edit->BlueprintSession->BlueprintOrigin.GetRotation().Equals(FRotator(0,30,0).Quaternion(), .001));
                FBox LocalBounds(ForceInit);
                const auto Origin = Edit->BlueprintSession->BlueprintOrigin;
                auto* LW = AFGLightweightBuildableSubsystem::Get(World);
                for (const auto& Member : Edit->BlueprintSession->Members)
                    if (const auto* Data = LW->GetRuntimeDataForBuildableClassAndIndex(Member.LightweightClass, Member.ResolveIndex(World)))
                        LocalBounds += Data->BoundingBox.TransformBy(Data->Transform.GetRelativeTransform(Origin));
                Check(TEXT("Group blueprint origin centers X/Y with its lowest geometry at Z zero"),
                    LocalBounds.IsValid && FMath::IsNearlyZero(LocalBounds.GetCenter().X, .02) && FMath::IsNearlyZero(LocalBounds.GetCenter().Y, .02) && FMath::IsNearlyZero(LocalBounds.Min.Z, .02));
            }
            bool SaveOnly=true;
            for(const auto* Name:{TEXT("mClearDesingerButton"),TEXT("mLoadButton")})
            {
                auto* Button=Window->WidgetTree->FindWidget(FName(Name));
                SaveOnly &= Button && Button->GetVisibility()==ESlateVisibility::Collapsed && !Button->GetIsEnabled();
            }
            Check(TEXT("Blueprint group window hides and disables Load and Clear"),SaveOnly);
            BlueprintTestName=TEXT("BuildingSkills_Automation_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
            BlueprintMembers=Edit->BlueprintSession->Members;
            Edit->BlueprintSession->DismantleCurrentBuildables(Cast<AFGPlayerController>(PC));
            bool Intact=true;
            for(const auto& Member:BlueprintMembers) Intact &= Member.IsAlive(World);
            Check(TEXT("Native Clear cannot dismantle a save-session group"),Intact && BlueprintMembers.Num()==3);
            auto* NameInput=Cast<UEditableText>(Window->WidgetTree->FindWidget(TEXT("mNameInput")));
            Check(TEXT("Native blueprint name field is editable"),NameInput!=nullptr);
            if(NameInput)
            {
                NameInput->SetText(FText::FromString(BlueprintTestName));
                NameInput->OnTextCommitted.Broadcast(FText::FromString(BlueprintTestName),ETextCommit::OnEnter);
            }
            if(auto* Description=Cast<UMultiLineEditableText>(Window->WidgetTree->FindWidget(TEXT("mDescriptionInput"))))
            {
                Description->SetText(FText::FromString(TEXT("Selected group integration test")));
                Description->OnTextCommitted.Broadcast(Description->GetText(),ETextCommit::OnEnter);
            }
            break;
        }
        case 82:
            if(!FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsBlueprintTest.png"),true,false);
            break;
        case 83:
            if(auto* Controller=Cast<AFGPlayerController>(PC))
                if(auto* RCO=Controller->GetRemoteCallObjectOfClass<UFGBlueprintRemoteCallObject>(); RCO && Edit->BlueprintSession)
                {
                    // The unattended account has no online UGC name-filter response.
                    // Submit through the same native RPC as the window after its
                    // filter, rather than depending on that external service.
                    auto* Blueprints=AFGBlueprintSubsystem::Get(World);
                    FBlueprintRecord Record;
                    Record.BlueprintName=BlueprintTestName;
                    Record.BlueprintDescription=TEXT("Selected group integration test");
                    Record.Category=Blueprints->GetCategoryForUndefined();
                    Record.SubCategory=Blueprints->GetSubCategoryForUndefined();
                    FBlueprintCategoryRecord Category;
                    Blueprints->GetGeneratedUndefinedCategoryRecord(Category);
                    // Exercise the previously blocked non-authority entry point.
                    // Intercept transport, then restore authority and run the actual writer.
                    ProbeClientForwarding = true; ObservedClientForwarding = false;
                    Edit->BlueprintSession->SetRole(ROLE_SimulatedProxy);
                    Edit->BlueprintSession->SaveBlueprint(Record, Controller);
                    Edit->BlueprintSession->SetRole(ROLE_Authority);
                    ProbeClientForwarding = false;
                    Check(TEXT("Client Save reaches the native server RPC before authority validation"), ObservedClientForwarding);
                    Edit->BlueprintSession->SaveBlueprint(Record, Controller);
                    Check(TEXT("Native Save reports the actual disk-write result"), Edit->Feedback.Contains(TEXT("Blueprint saved:")));
                }
            break;
        case 86:
        {
            auto* Blueprints=AFGBlueprintSubsystem::Get(World);
            auto* Descriptor=Blueprints?Blueprints->GetBlueprintDescriptorByNameString(BlueprintTestName):nullptr;
            Check(TEXT("Native Save creates a blueprint descriptor for the selected group"),Descriptor!=nullptr);
            if(Descriptor)
            {
                const bool Read=Blueprints->ReadBlueprintFromDisc(BlueprintTestName);
                Check(TEXT("Saved group blueprint reads back from disk with native archive data"),Read && Blueprints->mStoredBlueprintData.IsValid());
                TArray<AFGBuildable*> Loaded;
                if(Read) Blueprints->LoadStoredBlueprint(Descriptor,FTransform::Identity,Loaded,true);
                Check(TEXT("Blueprint loads exactly the three selected members"),Loaded.Num()==BlueprintMembers.Num() && Loaded.Num()==3);
                bool CorrectOrigin = Loaded.Num() == BlueprintMembers.Num();
                const FTransform Origin = Edit->BlueprintSession->BlueprintOrigin;
                for (const auto& Member : BlueprintMembers)
                {
                    const FTransform Expected = Member.Transform.GetRelativeTransform(Origin);
                    CorrectOrigin &= Loaded.ContainsByPredicate([&](AFGBuildable* Actor)
                    { return IsValid(Actor) && Actor->GetActorTransform().Equals(Expected, .02); });
                }
                Check(TEXT("Group blueprint uses selection bounds instead of player position and facing"), CorrectOrigin);
                bool Unchanged=true;
                for(const auto& Member:BlueprintMembers)
                {
                    Unchanged &= Member.IsAlive(World);
                    if(auto* Actor=Member.ResolveActor(World)) Unchanged &= Actor->GetActorTransform().Equals(Member.Transform,.01) && Actor->GetBlueprintDesigner() != Edit->BlueprintSession;
                }
                Check(TEXT("Saving a blueprint leaves original buildings and transforms intact"),Unchanged);
                // Retain only this generated fixture for the saved-blueprint HUD
                // check below; remove it after leaving placement.
            }
            Edit->CloseBlueprintSave();
            break;
        }
        case 87:
            Check(TEXT("Closing blueprint save returns to Edit with the selected group"),Edit->IsEditing() && Player->IsBuildGunEquipped() && Edit->Selection.Groups.Num()==1 && !Edit->BlueprintWindow);
            break;
        case 88:
            // Delay discovery as if the actor channel arrived after the RPC.
            // This is a deterministic handoff regression, not a remote-client test.
            Edit->OnSave();
            Edit->BlueprintNextSessionCheck=Now+2.5;
            Check(TEXT("Save waits for session discovery instead of consuming an RPC actor pointer"),Edit->bOpeningBlueprint && !Edit->BlueprintWindow);
            Key(EKeys::LeftAlt,true); Key(EKeys::S,true);
            break;
        case 89:
        {
            bool SaveActive=false, MovementActive=false;
            if(Input) for(const auto& Mapping:Input->GetEnhancedActionMappings())
                if(Mapping.Key==EKeys::S && Input->FindActionInstanceData(Mapping.Action) && Input->FindActionInstanceData(Mapping.Action)->GetTriggerEvent()==ETriggerEvent::Triggered)
                {
                    if(Mapping.GetMappingName()==TEXT("BuildingSkills.Save")) SaveActive=true;
                    else MovementActive=true;
                }
            Check(TEXT("Held Save remains consumed while the replicated session is pending"),Edit->bOpeningBlueprint && SaveActive && !MovementActive);
            Edit->OnBlueprintSaveReply(FGuid::NewGuid(),TEXT("stale response"));
            Check(TEXT("Unrelated save replies cannot cancel the pending request"),Edit->bOpeningBlueprint);
            break;
        }
        case 92:
        {
            Key(EKeys::S,false); Key(EKeys::LeftAlt,false);
            Check(TEXT("Delayed session discovery opens the native window"),Edit->BlueprintWindow && Edit->BlueprintSession && !Edit->bOpeningBlueprint);
            Check(TEXT("Dynamic save session is awake and identified for replication"),Edit->BlueprintSession && Edit->BlueprintSession->NetDormancy==DORM_Awake && Edit->BlueprintSession->RequestId==Edit->BlueprintRequestId);
            const FGuid ClosedRequest=Edit->BlueprintRequestId;
            Edit->CloseBlueprintSave();
            Edit->OnBlueprintSaveReply(ClosedRequest,TEXT("stale response"));
            Check(TEXT("Late replies cannot reopen a closed blueprint request"),!Edit->bOpeningBlueprint && !Edit->BlueprintWindow && !Edit->BlueprintRequestId.IsValid());
            break;
        }
        case 93:
        {
            Edit->OnSave();
            const FGuid TimedOut=Edit->BlueprintRequestId;
            Edit->BlueprintRequestDeadline=Now-1;
            Edit->TickBlueprintSave();
            bool SessionLeft=false;
            for(TActorIterator<ABSBlueprintSaveSession> It(World);It;++It) SessionLeft|=It->RequestId==TimedOut;
            Check(TEXT("Timed-out save requests release their server session and preserve selection"),TimedOut.IsValid() && !SessionLeft && !Edit->bOpeningBlueprint && Edit->Selection.Groups.Num()==1);
            ChecksPassed &= RunBuildingSkillsClipboardChecks(Player, Edit->Selection);
            ChecksPassed &= RunBuildingSkillsBlueprintScaleChecks(Player);
            auto KeptMembers = Edit->ResolveSelection(Edit->Selection);
            for (auto& Ref : KeptMembers) if (Ref.Actor) Ref.Transform = Ref.Actor->GetActorTransform();
            Edit->OnTransform();
            Edit->OnTransformResult(true, Edit->Selection, TEXT("test delayed transform"), Groups->Revision);
            Edit->TransformReplicationDeadline = World->GetRealTimeSeconds() - 1;
            if (Dismantle) Edit->UpdateSelection(Dismantle);
            bool WorldUnchanged = true;
            for (const auto& Ref : KeptMembers)
                WorldUnchanged &= Ref.IsAlive(World) && (!Ref.Actor || Ref.Actor->GetActorTransform().Equals(Ref.Transform, .01));
            Check(TEXT("Superseded transform replies release selection without reverting world changes"),
                !Edit->bRequestPending && !Edit->IsTransforming() && Edit->Selection.IsEmpty() && WorldUnchanged);

            break;
        }
        case 94:
            if (auto* Proxy = RunBuildingSkillsFoundationPlacementChecks(Player, ChecksPassed))
            {
                Edit->ClearSelection();
                Gun->SetCurrentBuildGunMode(UBSEditBlueprintModeDescriptor::StaticClass());
                {
                    TGuardValue<TObjectPtr<AActor>> Target(Dismantle->mCurrentlySelectedActor, Proxy);
                    Check(TEXT("Blueprint selection also accepts the native proxy target"), Edit->HoverSelection(Dismantle).Buildings.Num() == 2);
                }
                const auto& Bundles = Proxy->GetLightweightClassAndIndices();
                if (!Bundles.IsEmpty() && !Bundles[0].Indices.IsEmpty())
                {
                    auto Ref = FBSBuildingRef::FromLightweight(World, Bundles[0].BuildableClass, Bundles[0].Indices[0]);
                    TestBuilding = Ref.ResolveActor(World);
                    if (TestBuilding.IsValid()) TestBuilding->SetBlockCleanupOfTemporary(true);
                }
            }
            break;
        case 95:
            Check(TEXT("Blueprint selection expands a newly placed foundation to both pieces"), Edit->Hover.Buildings.Num() == 2);
            if (TestBuilding.IsValid()) TestBuilding->SetBlockCleanupOfTemporary(false);
            break;
        case 96:
        {
            // The earlier dismantle check destroyed fixtures 0 and 2. Create
            // fresh actors before testing native Ctrl-hover highlighting.
            auto* Subsystem = AFGBuildableSubsystem::Get(World);
            auto* Class = LoadClass<AFGBuildable>(nullptr, TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C"));
            if (Subsystem && Class)
                for (int32 Index : {0, 2})
                {
                    const FTransform Pose(Player->GetActorLocation() + FVector(Index == 0 ? 1400 : -1400, 0, 1000));
                    auto* Building = Subsystem->BeginSpawnBuildable(Class, Pose);
                    if (!Building) continue;
                    Building->SetBuiltWithRecipe(LoadClass<UFGRecipe>(nullptr, TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_StorageContainerMk1.Recipe_StorageContainerMk1_C")));
                    Building->FinishSpawning(Pose);
                    Buildings[Index] = Building;
                }
            Check(TEXT("Fresh dismantle highlight fixtures exist"), Buildings[0].IsValid() && Buildings[2].IsValid());
            TestBuilding = Buildings[0];
            Key(EKeys::LeftAlt, true); Key(EKeys::E, true); break;
        }
        case 97:
            Key(EKeys::E, false); Key(EKeys::LeftAlt, false);
            Key(EKeys::F, true); break;
        case 98:
            Key(EKeys::F, false);
            Edit->ClearSelection();
            if (Dismantle) Gun->SetCurrentBuildGunMode(Dismantle->mDefaultDismantleMode);
            break;
        case 99:
            Check(TEXT("Dismantle first target retains the native selection style"),
                !Edit->IsEditing() && !Edit->bEditColorsApplied &&
                Player->GetOutline()->GetOutlineStateColorForActor(Buildings[0].Get()) == EOutlineColor::OC_DISMANTLE);
            Key(EKeys::LeftControl, true); break;
        case 100:
            Key(EKeys::LeftControl, false);
            Check(TEXT("Dismantle Ctrl pins the first target"), Edit->Selection.Buildings.Num() == 1);
            TestBuilding = Buildings[2]; break;
        case 101:
            Check(TEXT("Dismantle subsequent hover uses the same native style as the pinned selection"),
                Edit->Selection.Buildings.Num() == 1 && Edit->HoverHighlighted.Contains(Buildings[2]) &&
                !Edit->Highlighted.Contains(Buildings[2]) && Edit->HoverOutline &&
                Edit->HoverOutline->GetOutlineStateColorForActor(Buildings[2].Get()) == EOutlineColor::OC_DISMANTLE &&
                Player->GetOutline()->GetOutlineStateColorForActor(Buildings[0].Get()) == EOutlineColor::OC_DISMANTLE &&
                !Edit->bEditColorsApplied);
            if (!FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("BuildingSkillsDismantleTest.png"), true, false);
            break;
        case 102:
            Key(EKeys::LeftAlt, true); Key(EKeys::E, true); break;
        case 103:
            Key(EKeys::E, false); Key(EKeys::LeftAlt, false);
            Edit->Selection = {};
            Edit->Selection.Buildings.Add(FBSBuildingRef::FromActor(Buildings[0].Get()));
            if (auto* Recipes = AFGRecipeManager::Get(World)) Recipes->AddAvailableRecipe(Buildings[0]->GetBuiltWithRecipe());
            Edit->bSelectionDirty = true;
            Edit->TickBlueprintSave();
            Key(EKeys::LeftAlt, true); Key(EKeys::A, true);
            break;
        case 104:
            Key(EKeys::A, false); Key(EKeys::LeftAlt, false);
            Check(TEXT("Alt+A anchors the pointed object without selecting it"), Edit->BlueprintAnchorActor.Get() == Buildings[2].Get() && !Edit->bAnchorIncluded && Edit->Selection.Buildings.Num() == 1);
            if (!FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("BuildingSkillsAnchorBlueTest.png"), true, false);
            Key(EKeys::LeftControl, true);
            break;
        case 105:
            Key(EKeys::LeftControl, false);
            Check(TEXT("Ctrl includes the anchor and changes it to the selected-anchor palette"), Edit->bAnchorIncluded && Edit->Selection.Buildings.Num() == 2);
            if (!FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("BuildingSkillsAnchorTest.png"), true, false);
            break;
        case 106:
            Key(EKeys::LeftAlt, true); Key(EKeys::A, true);
            break;
        case 107:
            Key(EKeys::A, false); Key(EKeys::LeftAlt, false);
            Check(TEXT("Alt+A on the anchor clears it without changing selection"), !Edit->BlueprintAnchorActor && !(Edit->BlueprintAnchor.Actor || Edit->BlueprintAnchor.LightweightClass) && Edit->Selection.Buildings.Num() == 2);
            Edit->Selection.Buildings.SetNum(1); Edit->bSelectionDirty = true;
            break;
        case 108:
            Check(TEXT("Copy key is available for a single selected building"), Edit->IsEditing() && Edit->SaveContext);
            Key(EKeys::LeftAlt,true); Key(EKeys::C, true); break;
        case 109:
        {
            Key(EKeys::C, false); Key(EKeys::LeftAlt,false);
            auto* Build = Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState());
            auto* Hologram = Build ? Cast<AFGBlueprintHologram>(Build->GetHologram()) : nullptr;
            Check(TEXT("Configured Alt+C opens Build Gun with the copied selection hologram"),
                !Edit->IsEditing() && !Edit->bCopyPending && Hologram && Hologram->mBuildableToNewRoot.Num() == 1);
            Check(TEXT("Copy key leaves the source building in place"), Buildings[0].IsValid());
            Check(TEXT("Blueprint deduplication starts Off with a scoped input context"),Edit->Deduplication==EBSDeduplication::Off && Edit->DeduplicationContext);
            Check(TEXT("Copy placement displays Toggle deduplication in native hints"), NativeDedupHintVisible());
            if (!FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsCopyHints.png"),true,false);
            Key(EKeys::LeftAlt,true); Key(EKeys::D,true);
            break;
        }
        case 110:
            Key(EKeys::D,false);
            Check(TEXT("Configured Alt+D toggles blueprint deduplication to Replace"),Edit->Deduplication==EBSDeduplication::Replace);
            Edit->OnDeduplication();
            Check(TEXT("Deduplication next mode is Skip"),Edit->Deduplication==EBSDeduplication::Skip);
            Edit->OnDeduplication();
            Check(TEXT("Deduplication cycles back to Off"),Edit->Deduplication==EBSDeduplication::Off);
            Key(EKeys::LeftAlt, true); Key(EKeys::E, true); break;
        case 111:
            Key(EKeys::E, false); Key(EKeys::LeftAlt, false);
            Check(TEXT("Leaving blueprint placement removes its deduplication context"),!Edit->DeduplicationContext);
            Edit->Selection = {};
            Edit->Selection.Buildings.Add(FBSBuildingRef::FromActor(Buildings[0].Get()));
            Edit->bSelectionDirty = true;
            Key(EKeys::T, true); break;
        case 112:
            Key(EKeys::T, false); Key(EKeys::K, true); break;
        case 113:
        {
            Key(EKeys::K, false);
            Check(TEXT("K opens the temporary step menu while retaining Transform"), Edit->TransformSettingsWindow && Edit->IsTransforming() && !Edit->InputAllowed());
            Check(TEXT("Step menu shows the cursor and releases permanent game mouse capture"), PC->bShowMouseCursor &&
                GEngine->GameViewport->GetMouseCaptureMode() == EMouseCaptureMode::CaptureDuringMouseDown && !FSlateApplication::Get().HasAnyMouseCaptor());
            Check(TEXT("Step menu suspends camera look and movement"), PC->IsLookInputIgnored() && PC->IsMoveInputIgnored());
            auto* Window = Edit->TransformSettingsWindow.Get();
            TSharedPtr<SEditableText> Field;
            TFunction<void(TSharedRef<SWidget>)> FindField = [&](TSharedRef<SWidget> Widget)
            {
                if (Field) return;
                if (Widget->GetTypeAsString() == TEXT("SEditableText")) { Field = StaticCastSharedRef<SEditableText>(Widget); return; }
                auto* Children = Widget->GetChildren();
                for (int32 I=0; I<Children->Num(); ++I) FindField(Children->GetChildAt(I));
            };
            if (Window) FindField(Window->TakeWidget());
            Check(TEXT("Temporary step menu contains a typed numeric field"), Field.IsValid());
            if (Field)
            {
                const bool Clicked = ClickWidget(Field.ToSharedRef());
                Check(TEXT("Mouse click focuses a step field without forced keyboard focus"), Clicked && Field->HasKeyboardFocus());
                Field->SelectAllText();
                for (TCHAR Ch : FString(TEXT("37.5")))
                    FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Ch, FModifierKeysState(), 0, false));
                FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
            }
            break;
        }
        case 114:
            Check(TEXT("Typed centimeters become the active temporary movement step"), FMath::IsNearlyEqual(Edit->TransformSteps().X, 37.5, .0001));
            if (!FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("BuildingSkillsStepSettingsTest.png"), true, false);
            break;
        case 115:
            if (Edit->TransformSettingsWindow)
            {
                TSharedPtr<SWidget> Button;
                TFunction<void(TSharedRef<SWidget>)> FindButton = [&](TSharedRef<SWidget> Widget)
                {
                    if (Button) return;
                    if (Widget->GetTypeAsString() == TEXT("SButton")) { Button = Widget; return; }
                    auto* Children = Widget->GetChildren();
                    for (int32 I=0; I<Children->Num(); ++I) FindButton(Children->GetChildAt(I));
                };
                FindButton(Edit->TransformSettingsWindow->TakeWidget());
                Check(TEXT("Mouse click activates the step window Close button"), Button && ClickWidget(Button.ToSharedRef()));
            }
            break;
        case 116:
            Check(TEXT("Closing step menu restores gun input and retains temporary values"), !Edit->TransformSettingsWindow && Edit->IsTransforming() && Edit->InputAllowed() && FMath::IsNearlyEqual(Edit->TransformSteps().X, 37.5, .0001));
            Check(TEXT("Closing step menu restores camera input and hides its cursor"), !PC->bShowMouseCursor && !PC->IsLookInputIgnored() && !PC->IsMoveInputIgnored());
            MoveStart = Buildings[0]->GetActorLocation();
            Edit->OnMove(4);
            break;
        case 117:
            Check(TEXT("Transform applies the typed movement distance to a placed building"), FMath::IsNearlyEqual(Buildings[0]->GetActorLocation().Z - MoveStart.Z, 37.5, .01));
            Key(EKeys::K, true); break;
        case 118:
            Key(EKeys::K, false);
            Check(TEXT("Reopening step settings restores a usable mouse cursor"), Edit->TransformSettingsWindow && PC->bShowMouseCursor);
            if (Edit->TransformSettingsWindow)
                FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
            break;
        case 119:
            Check(TEXT("Escape closes the step menu and restores camera input without ending Transform"), !Edit->TransformSettingsWindow && Edit->IsTransforming() && !PC->bShowMouseCursor && !PC->IsLookInputIgnored());
            Edit->EndTransform();
            Check(TEXT("Leaving Transform for Edit retains temporary step values"), Edit->IsEditing() && FMath::IsNearlyEqual(Edit->GetTransformStepValues()[0].X, 37.5, .0001));
            Key(EKeys::LeftAlt, true); Key(EKeys::E, true); break;
        case 120:
            Key(EKeys::E, false); Key(EKeys::LeftAlt, false);
            Check(TEXT("Closing Edit discards temporary steps and restores configuration"), !Edit->IsEditing() && Edit->TemporaryTransformSteps.IsEmpty() && FMath::IsNearlyEqual(Edit->GetTransformStepValues()[0].X, double(FBSTransformSettings::Get(World).Move), .0001));
            break;
        case 121:
        {
            auto* System = AFGBlueprintSubsystem::Get(World);
            auto* Descriptor = System ? System->GetBlueprintDescriptorByNameString(BlueprintTestName) : nullptr;
            auto* BuildState = Cast<UFGBuildGunStateBuild>(Gun->GetBuildGunStateFor(EBuildGunState::BGS_BUILD));
            Check(TEXT("Saved blueprint fixture remains available for HUD check"), Descriptor && BuildState);
            if (Descriptor && BuildState)
            {
                BuildState->SetActiveBlueprintDescriptor(Descriptor);
                System->SetActiveBlueprintHologramDescriptor(Descriptor);
                Player->HotKeyRecipe(UFGBlueprintSettings::Get()->mBlueprintRecipeClass.LoadSynchronous());
            }
            break;
        }
        case 122:
            Check(TEXT("Saved blueprint placement displays Toggle deduplication in native hints"), Edit->IsBlueprintBuilding() && NativeDedupHintVisible());
            if (!FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsBlueprintHints.png"),true,false);
            break;
        case 123:
            Player->UnequipBuildGun();
            if (auto* System = AFGBlueprintSubsystem::Get(World))
                if (auto* Descriptor = System->GetBlueprintDescriptorByNameString(BlueprintTestName)) System->DeleteBlueprintDescriptor(Descriptor);
            break;
        }
        UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME TARGET: expected=%s hit=%s selected=%s pending=%d hover=%d multi=%d paused=%d interact=%d geometry=%s"),
            *GetNameSafe(TestBuilding.Get()), *GetNameSafe(Gun->GetHitResult().GetActor()), *GetNameSafe(Dismantle ? Dismantle->GetSelectedActor() : nullptr),
            Dismantle ? Dismantle->GetPendingDismantleActors().Num() : -1, Edit->Hover.Buildings.Num(), Dismantle && Dismantle->IsMultiDismantleActive(),
            UGameplayStatics::IsGamePaused(World), Player->IsInteractWidgetOpen(),
            Edit->Indicator ? *Edit->Indicator->GetCachedGeometry().GetLocalSize().ToString() : TEXT("no widget"));
        if (Samples == 28 && !FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
        {
            for (TObjectIterator<UBackgroundBlur> It; It; ++It)
                if (It->GetWorld() == World && It->IsVisible())
                    UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME BLUR: %s parent=%s position=%s size=%s strength=%f"),
                        *It->GetPathName(), *GetNameSafe(It->GetParent()), *It->GetCachedGeometry().GetAbsolutePosition().ToString(),
                        *It->GetCachedGeometry().GetLocalSize().ToString(), It->GetBlurStrength());
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("BuildingSkillsRuntimeTest.png"), true, false);
        }
        if (Samples >= 130 && Samples <= 144 && Samples % 2 == 0)
            ChecksPassed &= FBSTransformNative::RunPooledVisualChecks(Player, (Samples-130)/2);
        if (Samples >= 150 && Samples <= 203) ChecksPassed &= RunBuildingSkillsConnectedChecks(Player, Samples - 150);
        if (Samples >= 250 && Samples <= 278) ChecksPassed &= RunBuildingSkillsVolumeChecks(Player, Samples - 250);
        if (Samples >= 450 && Samples <= 476) ChecksPassed &= RunBuildingSkillsTransformerChecks(Player, Samples - 450);
        if (Samples >= 400 && Samples <= 439) ChecksPassed &= RunBuildingSkillsPolishChecks(Player, Samples - 400);
        if (Samples >= 300 && Samples <= 374) ChecksPassed &= FBSStick::RunChecks(Player, Samples - 300);
        if (Samples >= 550 && Samples <= 604) ChecksPassed &= RunBuildingSkillsPlanChecks(Player, Samples - 550);
        if (Samples >= 500 && Samples <= 535) ChecksPassed &= RunBuildingSkillsBlueprintNetworkChecks(Player, Samples - 500);
        if (Samples == 239 && FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsNetworkTest"))) ChecksPassed &= RunBuildingSkillsNetworkCopyChecks(Player,3);
        if (Samples == 153) Next += 3; // Allow modal/HUD animations to settle for visual inspection.
        if (++Samples < (FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsPlanTest")) ? 605 : FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsBlueprintNetworkTest")) ? 537 : FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsTransformersTest")) ? 478 : FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsPolishTest")) ? 441 : FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsStickTest")) ? 376 : FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsVolumeTest")) ? 280 : FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsConnectedTest")) ? 205 : FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsPooledVisualTest")) ? 146 : FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsNetworkTest")) ? 240 : 125)) return true;
        for (TObjectIterator<UUserWidget> It; It; ++It)
        {
            if (It->GetWorld() != World || !It->WidgetTree) continue;
            const FString Name = It->GetClass()->GetName();
            if (!Name.Contains(TEXT("Dismantle")) && !Name.Contains(TEXT("Shortcuts")) && !Name.Contains(TEXT("Edit")) && !Name.Contains(TEXT("BuildMenu")) && !Name.Contains(TEXT("HintContainer")) && !Name.Contains(TEXT("BuildMode"))) continue;
            UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME UI: %s visible=%d viewport=%d"), *Name, It->IsVisible(), It->IsInViewport());
            It->WidgetTree->ForEachWidget([](UWidget* Widget)
            {
                auto* Text = Cast<UTextBlock>(Widget);
                UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME UI CHILD: %s %s visible=%d text=%s"), *Widget->GetName(), *Widget->GetClass()->GetName(), Widget->IsVisible(), Text ? *Text->GetText().ToString() : TEXT(""));
                if (auto* GridSlot = Cast<UUniformGridSlot>(Widget->Slot))
                    UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME GRID: %s row=%d col=%d pos=%s size=%s"), *Widget->GetName(), GridSlot->GetRow(), GridSlot->GetColumn(), *Widget->GetCachedGeometry().GetAbsolutePosition().ToString(), *Widget->GetCachedGeometry().GetLocalSize().ToString());
            });
        }
        if (Input)
        {
            for (const auto& Mapping : Input->GetEnhancedActionMappings())
                if (Mapping.GetMappingName().ToString().Contains(TEXT("BuildingSkills")))
                    UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME MAPPING: %s key=%s hint=%d"), *Mapping.GetMappingName().ToString(), *Mapping.Key.ToString(), Mapping.bDisplayButtonHint);
        }
        if (auto* UI = Player->GetGameUI())
            for (auto* Widget : UI->GetInteractWidgetStack())
                UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME UI STACK: %s object=%s gun=%s keyboard=%d mouse=%d"),
                    *GetNameSafe(Widget), *GetNameSafe(Widget->mInteractObject), *GetNameSafe(Gun), Widget->mUseKeyboard, Widget->mUseMouse);
        const bool Passed = ChecksPassed;
        UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME EDIT CHECK: %s"), Passed ? TEXT("PASS") : TEXT("FAIL"));
        FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
        return false;
    }
};

void InstallBuildingSkillsRuntimeSmoke()
{
    if (WITH_EDITOR || !FParse::Param(FCommandLine::Get(), TEXT("BuildingSkillsRuntimeTest"))) return;
    InstallBuildingSkillsBlueprintNetworkSmoke();
    SUBSCRIBE_METHOD(UFGBlueprintRemoteCallObject::Server_SaveBlueprintInDesigner,
        [](auto& Scope, UFGBlueprintRemoteCallObject*, AFGBuildableBlueprintDesigner* Designer, AFGPlayerController* Controller,
            FBlueprintRecord Record, FBlueprintCategoryRecord, FBlueprintSubCategoryRecord)
        {
            if (!FBSRuntimeSmoke::ProbeClientForwarding) return;
            FBSRuntimeSmoke::ObservedClientForwarding = Cast<ABSBlueprintSaveSession>(Designer) && Controller == Designer->GetOwner() && !Record.BlueprintName.IsEmpty();
            Scope.Cancel();
        });
    SUBSCRIBE_METHOD(UFGSaveSession::SaveGame,
        [](auto& Scope, UFGSaveSession*, const FString&, FOnSaveGameComplete, void*)
        { Scope.Cancel(); UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME TEST: blocked save request.")); });
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&FBSRuntimeSmoke::Tick), 0.2f);
}
