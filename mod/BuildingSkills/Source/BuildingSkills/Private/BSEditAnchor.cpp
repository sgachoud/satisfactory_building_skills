#include "BSEditModeComponent.h"
#include "FGOutlineComponent.h"
#include "Buildables/FGBuildable.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

void UBSEditModeComponent::OnBlueprintAnchor()
{
    if ((!IsEditing() && !IsVolumeSelection()) || bTransforming || !InputAllowed() || bRequestPending || bCopyPending || bOpeningBlueprint || BlueprintWindow) return;
    auto* Target = HoverActor.Get();
    if (!IsValid(Target)) return;
    const auto Ref = FBSBuildingRef::FromActor(Target);
    if (!Ref.IsAlive(GetWorld())) return;
    const bool Clearing = BlueprintAnchor == Ref;
    ReleaseBlueprintAnchor();
    BlueprintAnchor = Clearing ? FBSBuildingRef{} : Ref;
    if (HoverOutline) HoverOutline->HideOutline(Target);
    OutlinedRevision = INDEX_NONE;
    bSelectionDirty = true;
    UpdateBlueprintAnchor();
    if (IsVolumeSelection())
    {
        NextVolumeQuery = 0;
        Feedback = Clearing ? TEXT("Volume anchor cleared. The volume follows you.")
            : TEXT("Volume anchor set. Aim at it and press the anchor shortcut again to clear it.");
    }
    else Feedback = Clearing ? TEXT("Blueprint alignment: automatic.") : TEXT("Blueprint anchor set. Ctrl-select it to include it in Save / Copy.");
}

void UBSEditModeComponent::ReleaseBlueprintAnchor()
{
    if (IsValid(BlueprintAnchorActor))
    {
        if (AnchorOutline) AnchorOutline->HideOutline(BlueprintAnchorActor);
        if (BlueprintAnchorActor->GetIsLightweightTemporary() && !Highlighted.Contains(BlueprintAnchorActor) && !HoverHighlighted.Contains(BlueprintAnchorActor))
            BlueprintAnchorActor->SetBlockCleanupOfTemporary(false);
    }
    BlueprintAnchorActor = nullptr;
    if (AnchorPostProcess) AnchorPostProcess->bEnabled = false;
}

void UBSEditModeComponent::UpdateBlueprintAnchor()
{
    if ((!IsEditing() && !IsVolumeSelection()) || !(BlueprintAnchor.Actor || BlueprintAnchor.LightweightClass)) { ReleaseBlueprintAnchor(); return; }
    if (!BlueprintAnchor.IsAlive(GetWorld()))
    {
        ReleaseBlueprintAnchor(); BlueprintAnchor = {}; bSelectionDirty = true;
        Feedback = TEXT("Blueprint anchor is no longer available; alignment is automatic.");
        return;
    }
    auto* Actor = BlueprintAnchor.ResolveActor(GetWorld());
    if (!Actor) return;
    // SyncDismantle already resolves the explicit selection once when it changes.
    // Avoid expanding large nested groups again on every rendered frame.
    const bool Included = !Selection.IsEmpty() && Highlighted.Contains(Actor);
    if (Actor == BlueprintAnchorActor && Included == bAnchorIncluded && Actor->GetActorTransform().Equals(OutlinedAnchorTransform, .001)) return;
    ReleaseBlueprintAnchor();
    BlueprintAnchorActor = Actor;
    bAnchorIncluded = Included;
    OutlinedAnchorTransform = Actor->GetActorTransform();
    if (Actor->GetIsLightweightTemporary()) Actor->SetBlockCleanupOfTemporary(true);
    if (!AnchorOutline)
    {
        AnchorOutline = NewObject<UFGOutlineComponent>(GetOwner());
        AnchorOutline->RegisterComponent();
    }
    if (!AnchorPostProcess)
    {
        auto* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FactoryGame/PostProcess/Materials/PP_OutlineColored.PP_OutlineColored"));
        if (!Parent) return;
        AnchorMaterial = UMaterialInstanceDynamic::Create(Parent, this);
        AnchorMaterial->SetScalarParameterValue(TEXT("StencilStartIndex"), 240);
        AnchorPostProcess = NewObject<UPostProcessComponent>(GetOwner());
        AnchorPostProcess->bUnbound = true;
        AnchorPostProcess->Priority = 100;
        AnchorPostProcess->Settings.AddBlendable(AnchorMaterial, 1);
        AnchorPostProcess->RegisterComponent();
    }
    const FLinearColor Color = Included ? FLinearColor(.6f, .05f, 1.f) : FLinearColor(.02f, .2f, 1.f);
    for (const TCHAR* Name : {TEXT("Color1"), TEXT("Color2"), TEXT("Color3"), TEXT("Color4")}) AnchorMaterial->SetVectorParameterValue(Name, Color);
    AnchorPostProcess->bEnabled = true;
    // Use native geometry proxies, but a separate stencil/post-process material
    // so the Edit palette and the game's normal outline colors stay untouched.
    AnchorOutline->ShowOutline(Actor, EOutlineColor::OC_USABLE, true, true);
    if (const auto* State = AnchorOutline->GetImmutableOutlineStateForActor(Actor))
    {
        for (const auto& Pair : State->OutlineProxies) if (Pair.Value) Pair.Value->SetCustomDepthStencilValue(240);
        for (const auto& Pair : State->InstancedOutlineProxies) if (Pair.Value) Pair.Value->SetCustomDepthStencilValue(240);
    }
}
