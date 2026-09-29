#pragma once
#include "Buildables/FGBuildableBlueprintDesigner.h"
#include "BSGroups.h"
#include "BSBlueprintSave.generated.h"

/** Transient, owner-only context for the native blueprint window; never a world designer. */
UCLASS(NotBlueprintable)
class BUILDINGSKILLS_API ABSBlueprintSaveSession : public AFGBuildableBlueprintDesigner
{
    GENERATED_BODY()
public:
    ABSBlueprintSaveSession();
    static void InstallHooks();
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void Destroyed() override;
    virtual bool ShouldSave_Implementation() const override { return false; }
    virtual bool CanDismantle_Implementation() const override { return false; }
    virtual void GetChildDismantleActors_Implementation(TArray<AActor*>& Out) const override {}
    virtual void RegisterInteractingPlayer_Implementation(AFGCharacterPlayer* Player) override {}
    bool Prepare(FString& Error);
    void ShowSummaryOnly() { mBuildables.Reset(); mBuildables.Add(this); }
    FGuid Group;
    int32 GroupRevision = INDEX_NONE;
    bool bOpened = false;
    FTransform BlueprintOrigin;
    UPROPERTY(Transient) TArray<FBSBuildingRef> Members;
    void Notify(const FString& Message);
};
