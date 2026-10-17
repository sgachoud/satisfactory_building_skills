#pragma once
#include "CoreMinimal.h"
#include "BSConnectedSettings.generated.h"

UENUM()
enum class EBSConnectedScope : uint8 { All, Category, Subcategory, Strict };

/** Shared by selection and paint. Distances are user-facing meters. */
USTRUCT()
struct BUILDINGSKILLS_API FBSConnectedSettings
{
    GENERATED_BODY()
    UPROPERTY() double Gap = 0;
    UPROPERTY() bool IgnoreSwatch = false;
    UPROPERTY() bool IgnoreMaterial = false;
    UPROPERTY() EBSConnectedScope Scope = EBSConnectedScope::Strict;
    UPROPERTY() int32 Limit = 64;
    UPROPERTY() FString Whitelist;
    UPROPERTY() FString Blacklist;
    bool Validate(FString& Error) const;
};
