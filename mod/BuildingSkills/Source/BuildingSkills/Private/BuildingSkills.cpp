#include "BuildingSkills.h"
#include "BSEditModeComponent.h"
#include "EnhancedInputComponent.h"
#include "FGCharacterPlayer.h"
#include "BSBlueprintSave.h"
#include "BSClipboard.h"
#include "BSBlueprintScale.h"
#include "BSBlueprintBounds.h"
#include "BSConnected.h"
#include "BSDeduplication.h"
#include "BSActionHistory.h"
#include "BSTransformers.h"

DEFINE_LOG_CATEGORY(LogBuildingSkills);
void InstallBuildingSkillsRuntimeSmoke();
void InstallBuildingSkillsNetworkHooks();

void FBuildingSkillsModule::StartupModule()
{
    UE_LOG(LogBuildingSkills, Display, TEXT("Building Skills module loaded."));
    UBSEditModeComponent::InstallHooks();
    FBSConnectedPaint::Install();
    InstallBuildingSkillsNetworkHooks();
    ABSBlueprintSaveSession::InstallHooks();
    InstallBuildingSkillsBlueprintScale();
    FBSBlueprintPlacement::Install();
    UBSClipboard::InstallHooks();
    FBSDeduplication::Install();
    FBSStick::Install();
    FBSActionHistory::Install();
    FBSTransformers::Install();
    InstallBuildingSkillsRuntimeSmoke();
#if !UE_SERVER
    InputInitializedHandle = AFGCharacterPlayer::OnPlayerInputInitialized.AddLambda(
        [](AFGCharacterPlayer* Player, UInputComponent* Input)
        {
            UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(Input);
            if (!IsValid(Player) || !Player->IsLocallyControlled() || !EnhancedInput) return;

            UBSEditModeComponent* Edit = Player->FindComponentByClass<UBSEditModeComponent>();
            if (!Edit)
            {
                Edit = NewObject<UBSEditModeComponent>(Player);
                Player->AddInstanceComponent(Edit);
                Edit->RegisterComponent();
            }
            Edit->InitializeInput(EnhancedInput);
        });
#endif
}

void FBuildingSkillsModule::ShutdownModule()
{
#if !UE_SERVER
    AFGCharacterPlayer::OnPlayerInputInitialized.Remove(InputInitializedHandle);
#endif
}

IMPLEMENT_MODULE(FBuildingSkillsModule, BuildingSkills)
