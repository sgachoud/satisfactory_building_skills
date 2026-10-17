#pragma once
#include "CoreMinimal.h"
class AFGBuildable;
class AFGBlueprintHologram;

class FBSBlueprintPlacement
{
public:
    static void Install();
    static FBox GeometryBounds(const AFGBlueprintHologram* Hologram);
    static FBox PlacementBounds(const AFGBlueprintHologram* Hologram);
};

namespace BSBlueprintBounds
{
    bool FitFrame(const TArray<FVector>& Points, const FQuat& Rotation, FTransform& Origin, FVector& Size,
        const TArray<FVector>& FloorPoints = {});
    void AppendBox(const FBox& LocalBounds, const FTransform& Transform, TArray<FVector>& Points);
    void AppendActorGeometry(AFGBuildable* Building, TArray<FVector>& Points);
    // One vote per building, with perpendicular directions sharing an alignment.
    // Maximize aligned buildings first, then minimize the enclosing XY rectangle.
    bool FitUpright(const TArray<FVector>& Points, const TArray<double>& BuildingYaws,
        FTransform& Origin, FVector& Size);
}
