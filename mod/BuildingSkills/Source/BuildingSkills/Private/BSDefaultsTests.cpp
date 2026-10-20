#if WITH_DEV_AUTOMATION_TESTS
#include "BSTransform.h"
#include "BSStick.h"
#include "BSVolume.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Reflection/BlueprintReflectionLibrary.h"
#include "Misc/AutomationTest.h"
#include "UObject/StructOnScope.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSDefaultsReflectionTest,
    "BuildingSkills.Configuration.SavedDefaultsReflection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBSDefaultsReflectionTest::RunTest(const FString&)
{
    // A private configuration instance: never register it, mark it dirty or
    // touch the player's config file. Exercise the same reflection bridge used
    // by ConfigManager, including refreshing an already-created cached wrapper.
    auto* Config=NewObject<UBSTransformConfiguration>();
    auto* Root=Config->RootSection.Get();
    if (!TestNotNull(TEXT("Private configuration root exists"),Root)) return false;
    if (!TestTrue(TEXT("Test does not edit the default object"),Root!=GetDefault<UBSTransformConfiguration>()->RootSection.Get())) return false;
    TSet<FString> Covered;
    for (UScriptStruct* Struct:{FBSStickSettings::StaticStruct(),FBSVolumeSettings::StaticStruct(),FBSTransformSettings::StaticStruct()})
    {
        FStructOnScope Value(Struct);
        const auto Reflected=UBlueprintReflectionLibrary::ReflectStruct(FDynamicStructInfo{Struct,Value.GetStructMemory()});
        for (int32 Pass=0;Pass<2;++Pass)
        {
            for (const auto& Pair:Root->SectionProperties)
            {
                if (auto* Number=Cast<UConfigPropertyFloat>(Pair.Value))
                    Number->Value=Pair.Key==TEXT("StickStep") ? (Pass==0 ? 10.f : 3.5f) : Number->DefaultValue+(Pass==0 ? .25f : .75f);
                else if (auto* Boolean=Cast<UConfigPropertyBool>(Pair.Value))
                    Boolean->Value=Pass==0 ? !Boolean->DefaultValue : Boolean->DefaultValue;
                else if (auto* String=Cast<UConfigPropertyString>(Pair.Value))
                    String->Value=Pass==0 ? TEXT("^(Belt|Pipe)$") : TEXT("Foundation");
            }
            Root->FillConfigStructSelf(Reflected);
            Reflected.CopyWrappedStruct(Struct,Value.GetStructMemory());
            for (TFieldIterator<FProperty> It(Struct);It;++It)
            {
                const FString Name=It->GetName();
                const auto* Entry=Root->SectionProperties.Find(Name);
                if (!TestTrue(*FString::Printf(TEXT("%s.%s has a config source"),*Struct->GetName(),*Name),Entry!=nullptr)) continue;
                Covered.Add(Name);
                const FString Label=FString::Printf(TEXT("%s.%s receives saved value (pass %d)"),*Struct->GetName(),*Name,Pass);
                const void* Memory=It->ContainerPtrToValuePtr<void>(Value.GetStructMemory());
                if (const auto* Number=Cast<UConfigPropertyFloat>(Entry->Get()))
                {
                    const auto* Property=CastField<FFloatProperty>(*It);
                    if (TestNotNull(*Label,Property)) TestEqual(*Label,Property->GetPropertyValue(Memory),Number->Value);
                }
                else if (const auto* Boolean=Cast<UConfigPropertyBool>(Entry->Get()))
                {
                    const auto* Property=CastField<FBoolProperty>(*It);
                    if (TestNotNull(*Label,Property)) TestEqual(*Label,Property->GetPropertyValue(Memory),Boolean->Value);
                }
                else if (const auto* String=Cast<UConfigPropertyString>(Entry->Get()))
                {
                    const auto* Property=CastField<FStrProperty>(*It);
                    if (TestNotNull(*Label,Property)) TestEqual(*Label,Property->GetPropertyValue(Memory),String->Value);
                }
            }
        }
    }
    for (const auto& Pair:Root->SectionProperties)
        TestTrue(*FString::Printf(TEXT("Config option %s is consumed by a tested settings struct"),*Pair.Key),Covered.Contains(Pair.Key));
    return true;
}
#endif
