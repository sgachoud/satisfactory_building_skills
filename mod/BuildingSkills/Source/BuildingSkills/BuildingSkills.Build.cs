using UnrealBuildTool;

public class BuildingSkills : ModuleRules
{
    public BuildingSkills(ReadOnlyTargetRules Target) : base(Target)
    {
        // Match the existing small-mod workflow without the engine's large shared PCH.
        PCHUsage = PCHUsageMode.NoPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        AddEngineThirdPartyPrivateStaticDependencies(Target, "ICU");
        PrivateDependencyModuleNames.Add("CoreOnline"); // Local account identity for per-player menu preferences.

        // SML / FactoryGame public headers expose these transitive dependencies.
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "DeveloperSettings", "PhysicsCore", "InputCore",
            "GeometryCollectionEngine", "AnimGraphRuntime", "AssetRegistry", "NavigationSystem",
            "AIModule", "GameplayTasks", "SlateCore", "Slate", "UMG", "RenderCore",
            "CinematicCamera", "Foliage", "NetCore", "GameplayTags", "Json", "JsonUtilities",
            "DummyHeaders", "FactoryGame", "SML", "EnhancedInput", "AbstractInstance"
        });

        if (Target.Type == TargetRules.TargetType.Editor)
        {
            PublicDependencyModuleNames.Add("AnimGraph");
        }
    }
}
