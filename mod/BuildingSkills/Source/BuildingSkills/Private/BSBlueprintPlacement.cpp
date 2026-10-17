#include "BSBlueprintBounds.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFoundation.h"
#include "Patching/NativeHookManager.h"

FBox FBSBlueprintPlacement::GeometryBounds(const AFGBlueprintHologram* Hologram)
{
    FBox Bounds(ForceInit);
    if (!Hologram) return Bounds;
    for (const auto& Entry : Hologram->mBuildableToNewRoot)
    {
        if (!IsValid(Entry.Key)) continue;
        TArray<FVector> Points;
        BSBlueprintBounds::AppendActorGeometry(Entry.Key, Points);
        for (const auto& Point : Points) Bounds += Point;
    }
    return Bounds;
}

FBox FBSBlueprintPlacement::PlacementBounds(const AFGBlueprintHologram* Hologram)
{
    return Hologram ? Hologram->mLocalBounds : FBox(ForceInit);
}

void FBSBlueprintPlacement::Install()
{
    if (WITH_EDITOR) return;
    SUBSCRIBE_METHOD(AFGBlueprintHologram::AlignBuildableRootWithBounds,
        [](auto&, AFGBlueprintHologram* Hologram)
        {
            const FBox Geometry = GeometryBounds(Hologram);
            if (!Geometry.IsValid || !Hologram->mLocalBounds.IsValid) return;
            // Native placement sizes its proxy from clearance, which can omit
            // visible foundation lips and scaled meshes. Enclose those meshes
            // while retaining the native XY snap center and any clearance padding.
            auto& Bounds = Hologram->mLocalBounds;
            const FVector Center = Bounds.GetCenter();
            for (int32 Axis = 0; Axis < 2; ++Axis)
            {
                const double Extent = FMath::Max(Bounds.GetExtent()[Axis],
                    FMath::Max(FMath::Abs(Geometry.Min[Axis] - Center[Axis]), FMath::Abs(Geometry.Max[Axis] - Center[Axis])));
                Bounds.Min[Axis] = Center[Axis] - Extent;
                Bounds.Max[Axis] = Center[Axis] + Extent;
            }
            Bounds.Min.Z = FMath::Min(Bounds.Min.Z, Geometry.Min.Z);
            Bounds.Max.Z = FMath::Max(Bounds.Max.Z, Geometry.Max.Z);
        });
    SUBSCRIBE_UOBJECT_METHOD(AFGBlueprintHologram, TrySnapToActor,
        [](auto& Scope, AFGBlueprintHologram* Hologram, const FHitResult& Hit)
        {
            if (!Scope(Hologram, Hit)) return;
            auto* Support = Cast<AFGBuildableFoundation>(Hit.GetActor());
            if (!Support || !FMath::IsNearlyZero(Support->mElevation) || Support->mWidth <= 0 || Support->mDepth <= 0) return;
            const FTransform Frame = Support->GetActorTransform();
            const FVector Normal = Frame.InverseTransformVectorNoScale(Hit.ImpactNormal);
            bool Found = false;
            double BestDistance = TNumericLimits<double>::Max();
            double HalfHeight = 0;
            FVector Anchor = FVector::ZeroVector;
            for (const auto& Entry : Hologram->mBuildableToNewRoot)
            {
                auto* Foundation = Cast<AFGBuildableFoundation>(Entry.Key);
                if (!Foundation || !Entry.Value || !FMath::IsNearlyZero(Foundation->mElevation)) continue;
                const FTransform Relative = Entry.Value->GetComponentTransform().GetRelativeTransform(Frame);
                const FVector X = Relative.GetRotation().GetAxisX();
                const FVector Z = Relative.GetRotation().GetAxisZ();
                if (Z.Z < .9999 || FMath::Max(FMath::Abs(X.X), FMath::Abs(X.Y)) < .9999) continue;
                FVector Dimensions(Foundation->mWidth * Relative.GetScale3D().X, Foundation->mDepth * Relative.GetScale3D().Y, 0);
                if (FMath::Abs(X.Y) > .9999) Swap(Dimensions.X, Dimensions.Y);
                // Only use a common foundation grid. Custom sizes and diagonal
                // pieces keep the native snapping result instead of being forced
                // onto an incompatible grid.
                if (!FMath::IsNearlyEqual(Dimensions.X, double(Support->mWidth), .1) ||
                    !FMath::IsNearlyEqual(Dimensions.Y, double(Support->mDepth), .1)) continue;
                const FVector Candidate = Relative.GetLocation();
                const double Distance = FVector::DistSquared(Entry.Value->GetComponentLocation(), Hit.ImpactPoint);
                const bool Earlier = Candidate.X < Anchor.X || (FMath::IsNearlyEqual(Candidate.X, Anchor.X, .01) && Candidate.Y < Anchor.Y);
                if (Found && Distance > BestDistance - .01 && !(FMath::IsNearlyEqual(Distance, BestDistance, .01) && Earlier)) continue;
                Found = true; BestDistance = Distance; Anchor = Candidate;
                HalfHeight = Foundation->mHeight * Relative.GetScale3D().Z / 2;
            }
            if (!Found) return;
            FVector Aligned = Anchor;
            Aligned.X = FMath::RoundToDouble(Anchor.X / Support->mWidth) * Support->mWidth;
            Aligned.Y = FMath::RoundToDouble(Anchor.Y / Support->mDepth) * Support->mDepth;
            const FBox Geometry = GeometryBounds(Hologram);
            // A lower excluded anchor deliberately leaves space under the
            // contents. Keep native box-to-support height in that case: aligning
            // a contained foundation directly would erase the chosen floor gap.
            if (!Geometry.IsValid || Geometry.Min.Z <= .1)
            {
                if (Normal.Z > .5) Aligned.Z = Support->mHeight / 2 + HalfHeight;
                else if (Normal.Z < -.5) Aligned.Z = -Support->mHeight / 2 - HalfHeight;
                else Aligned.Z = Support->mHeight / 2 - HalfHeight;
            }
            Hologram->AddActorWorldOffset(Frame.TransformVector(Aligned - Anchor), false, nullptr, ETeleportType::TeleportPhysics);
        });
}
