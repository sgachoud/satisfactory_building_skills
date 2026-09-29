#include "BSEditModeComponent.h"
#include "BSEditModeWidget.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildable.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "FGBuildableSubsystem.h"
#include "FGRecipe.h"
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
    static bool Tick(float Delta)
    {
        static double Started = FPlatformTime::Seconds();
        static int32 Stage = 0;
        static double Next = 0;
        static int32 Samples = 0;
        static TWeakObjectPtr<AFGBuildable> TestBuilding;
        static TArray<TWeakObjectPtr<AFGBuildable>> Buildings;
        static bool ChecksPassed = true;
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
            if (auto* Subsystem = AFGBuildableSubsystem::Get(World); Subsystem && Class)
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
            Stage = 2;
            Next = Now + 1;
            return true;
        }
        if (TestBuilding.IsValid())
        {
            FVector Origin, Extent;
            TestBuilding->GetActorBounds(false, Origin, Extent);
            Player->GetController()->SetControlRotation((Origin - Player->GetPawnViewLocation()).Rotation());
        }
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
        auto* Groups = ABSGroupSubsystem::Get(World);
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
        case 11: Key(EKeys::LeftControl, true); break;
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
        }
        UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME TARGET: expected=%s hit=%s selected=%s pending=%d hover=%d multi=%d paused=%d interact=%d geometry=%s"),
            *GetNameSafe(TestBuilding.Get()), *GetNameSafe(Gun->GetHitResult().GetActor()), *GetNameSafe(Dismantle ? Dismantle->GetSelectedActor() : nullptr),
            Dismantle ? Dismantle->GetPendingDismantleActors().Num() : -1, Edit->Hover.Buildings.Num(), Dismantle && Dismantle->IsMultiDismantleActive(),
            UGameplayStatics::IsGamePaused(World), Player->IsInteractWidgetOpen(),
            Edit->Indicator ? *Edit->Indicator->GetCachedGeometry().GetLocalSize().ToString() : TEXT("no widget"));
        if (Samples == 3 && !FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("BuildingSkillsRuntimeTest.png"), true, false);
        if (++Samples < 25) return true;
        for (TObjectIterator<UUserWidget> It; It; ++It)
        {
            if (It->GetWorld() != World || !It->WidgetTree) continue;
            const FString Name = It->GetClass()->GetName();
            if (!Name.Contains(TEXT("Dismantle")) && !Name.Contains(TEXT("Shortcuts")) && !Name.Contains(TEXT("Edit")) && !Name.Contains(TEXT("BuildMenu"))) continue;
            UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME UI: %s visible=%d viewport=%d"), *Name, It->IsVisible(), It->IsInViewport());
            It->WidgetTree->ForEachWidget([](UWidget* Widget)
            {
                auto* Text = Cast<UTextBlock>(Widget);
                UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME UI CHILD: %s %s visible=%d text=%s"), *Widget->GetName(), *Widget->GetClass()->GetName(), Widget->IsVisible(), Text ? *Text->GetText().ToString() : TEXT(""));
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
    SUBSCRIBE_METHOD(UFGSaveSession::SaveGame,
        [](auto& Scope, UFGSaveSession*, const FString&, FOnSaveGameComplete, void*)
        { Scope.Cancel(); UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME TEST: blocked save request.")); });
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&FBSRuntimeSmoke::Tick), 0.2f);
}
