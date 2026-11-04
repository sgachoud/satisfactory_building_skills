#pragma once

#include "FGBlueprintSubsystem.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

// Native subsystem layouts can change in game hotfixes independently of SML's
// SDK headers. Resolve reflected storage in the running game, including Editor,
// instead of reading arrays at the offsets compiled into the old SDK.
struct FBSBlueprintAccess
{
    static FBlueprintSaveData& Data(AFGBlueprintSubsystem* System)
    {
        static const auto* Property=FindFProperty<FStructProperty>(AFGBlueprintSubsystem::StaticClass(),TEXT("mStoredBlueprintData"));
        checkf(Property,TEXT("Required native blueprint property is unavailable."));
        return *Property->ContainerPtrToValuePtr<FBlueprintSaveData>(System);
    }
    static TArray<FBlueprintHeader>& Headers(AFGBlueprintSubsystem* System)
    {
        static const auto* Property=FindFProperty<FArrayProperty>(AFGBlueprintSubsystem::StaticClass(),TEXT("mCachedHeaders"));
        checkf(Property,TEXT("Required native blueprint property is unavailable."));
        return *Property->ContainerPtrToValuePtr<TArray<FBlueprintHeader>>(System);
    }
    static TArray<TObjectPtr<AFGBuildable>>& Buildables(AFGBlueprintSubsystem* System)
    {
        static const auto* Property=FindFProperty<FArrayProperty>(AFGBlueprintSubsystem::StaticClass(),TEXT("mBlueprintWorldBuildables"));
        checkf(Property,TEXT("Required native blueprint property is unavailable."));
        return *Property->ContainerPtrToValuePtr<TArray<TObjectPtr<AFGBuildable>>>(System);
    }
    static TArray<TObjectPtr<UFGBlueprintDescriptor>>& Descriptors(AFGBlueprintSubsystem* System)
    {
        static const auto* Property=FindFProperty<FArrayProperty>(AFGBlueprintSubsystem::StaticClass(),TEXT("mAllBlueprintDescriptors"));
        checkf(Property,TEXT("Required native blueprint property is unavailable."));
        return *Property->ContainerPtrToValuePtr<TArray<TObjectPtr<UFGBlueprintDescriptor>>>(System);
    }
    static UWorld* PreviewWorld(AFGBlueprintSubsystem* System)
    {
        static const auto* Property=FindFProperty<FObjectProperty>(AFGBlueprintSubsystem::StaticClass(),TEXT("mBlueprintWorld"));
        checkf(Property,TEXT("Required native blueprint world property is unavailable."));
        return Cast<UWorld>(Property->GetObjectPropertyValue_InContainer(System));
    }
    static bool HasHeader(AFGBlueprintSubsystem* System,UFGBlueprintDescriptor* Descriptor)
    {
        return Descriptor && Headers(System).ContainsByPredicate([&](const FBlueprintHeader& Header) {
            return AFGBlueprintSubsystem::AreNamesConsideredEqual(Header.BlueprintName,Descriptor->GetBlueprintNameAsString());
        });
    }
    static void GetDescriptors(AFGBlueprintSubsystem* System,TArray<UFGBlueprintDescriptor*>& Out)
    {
        Out.Reset(); Out.Append(Descriptors(System));
    }
};
