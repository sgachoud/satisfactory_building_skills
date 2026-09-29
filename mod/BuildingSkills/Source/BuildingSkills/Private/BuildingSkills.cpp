#include "BuildingSkills.h"
#include "BSEditModeComponent.h"
#include "EnhancedInputComponent.h"
#include "FGCharacterPlayer.h"

DEFINE_LOG_CATEGORY(LogBuildingSkills);

void FBuildingSkillsModule::StartupModule()
{
    UE_LOG(LogBuildingSkills, Display, TEXT("Building Skills module loaded."));
    UBSEditModeComponent::InstallHooks();
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
