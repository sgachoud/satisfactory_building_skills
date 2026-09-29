#include "BuildingSkills.h"

DEFINE_LOG_CATEGORY(LogBuildingSkills);

void FBuildingSkillsModule::StartupModule()
{
    UE_LOG(LogBuildingSkills, Display, TEXT("Building Skills module loaded."));
}

void FBuildingSkillsModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FBuildingSkillsModule, BuildingSkills)
