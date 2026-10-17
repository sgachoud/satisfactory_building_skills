#include "BSBlueprintBounds.h"
#include "Buildables/FGBuildable.h"
#include "Components/MeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "InstanceData.h"


namespace BSBlueprintBounds
{
    bool FitFrame(const TArray<FVector>& Points, const FQuat& Rotation, FTransform& Origin, FVector& Size, const TArray<FVector>& FloorPoints)
    {
        if (Points.IsEmpty() || Rotation.ContainsNaN() || !Rotation.IsNormalized()) return false;
        const FVector Reference = Points[0];
        FBox Bounds(ForceInit);
        for (const FVector& Point : Points)
        {
            if (Point.ContainsNaN()) return false;
            Bounds += Rotation.UnrotateVector(Point - Reference);
        }
        for (const FVector& Point : FloorPoints)
        {
            if (Point.ContainsNaN()) return false;
            Bounds.Min.Z = FMath::Min(Bounds.Min.Z, Rotation.UnrotateVector(Point - Reference).Z);
        }
        Size = Bounds.GetSize();
        Origin = FTransform(Rotation, Reference + Rotation.RotateVector(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z)));
        return true;
    }

    void AppendActorGeometry(AFGBuildable* Building, TArray<FVector>& Points)
    {
        const int32 Before = Points.Num();
        // Native abstract instances need not have individual mesh components.
        for (const auto& Instance : Building->GetActorLightweightInstanceData_Implementation())
            if (Instance.StaticMesh)
                BSBlueprintBounds::AppendBox(Instance.StaticMesh->GetBoundingBox(),
                    Instance.RelativeTransform * Building->GetActorTransform(), Points);
        for (auto* Mesh : TInlineComponentArray<UMeshComponent*>(Building))
        {
            if (!Mesh->IsRegistered()) continue;
            if (auto* Instances = Cast<UInstancedStaticMeshComponent>(Mesh))
            {
                if (!Instances->GetStaticMesh()) continue;
                for (int32 Index = 0; Index < Instances->GetInstanceCount(); ++Index)
                {
                    FTransform Transform;
                    if (Instances->GetInstanceTransform(Index, Transform, true))
                        BSBlueprintBounds::AppendBox(Instances->GetStaticMesh()->GetBoundingBox(), Transform, Points);
                }
            }
            else
                BSBlueprintBounds::AppendBox(Mesh->CalcBounds(FTransform::Identity).GetBox(), Mesh->GetComponentTransform(), Points);
        }
        // Collision-only/custom buildables still need a conservative enclosure.
        if (Points.Num() == Before)
            BSBlueprintBounds::AppendBox(Building->GetComponentsBoundingBox(true), FTransform::Identity, Points);
    }

    void AppendBox(const FBox& Box, const FTransform& Transform, TArray<FVector>& Points)
    {
        if (!Box.IsValid) return;
        for (int32 Corner = 0; Corner < 8; ++Corner)
            Points.Add(Transform.TransformPosition(FVector(
                Corner & 1 ? Box.Max.X : Box.Min.X,
                Corner & 2 ? Box.Max.Y : Box.Min.Y,
                Corner & 4 ? Box.Max.Z : Box.Min.Z)));
    }

    bool FitUpright(const TArray<FVector>& Points, const TArray<double>& BuildingYaws,
        FTransform& Origin, FVector& Size)
    {
        if (Points.IsEmpty() || BuildingYaws.IsEmpty()) return false;
        const FVector Reference = Points[0];
        TArray<FVector2D> Projected;
        double MinZ = TNumericLimits<double>::Max(), MaxZ = -TNumericLimits<double>::Max();
        for (const auto& Point : Points)
        {
            if (Point.ContainsNaN()) return false;
            const auto Relative = Point - Reference;
            Projected.Emplace(Relative.X, Relative.Y);
            MinZ = FMath::Min(MinZ, Relative.Z); MaxZ = FMath::Max(MaxZ, Relative.Z);
        }
        Projected.Sort([](const FVector2D& A, const FVector2D& B)
        { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
        TArray<FVector2D> Unique;
        for (const auto& Point : Projected)
            if (Unique.IsEmpty() || Point != Unique.Last()) Unique.Add(Point);
        // Project only the convex hull for each candidate: interior mesh bounds
        // cannot affect the rectangle, even in groups with many components.
        auto AddHullPoint = [](TArray<FVector2D>& Hull, const FVector2D& Point)
        {
            while (Hull.Num() >= 2)
            {
                const auto A = Hull.Last() - Hull[Hull.Num() - 2];
                const auto B = Point - Hull.Last();
                if (A.X * B.Y - A.Y * B.X > 0) break;
                Hull.Pop(EAllowShrinking::No);
            }
            Hull.Add(Point);
        };
        TArray<FVector2D> Hull, Upper;
        for (const auto& Point : Unique) AddHullPoint(Hull, Point);
        for (int32 I = Unique.Num() - 1; I >= 0; --I) AddHullPoint(Upper, Unique[I]);
        if (Unique.Num() > 1)
        {
            Hull.Pop(EAllowShrinking::No); Upper.Pop(EAllowShrinking::No); Hull.Append(Upper);
        }
        auto CanonicalYaw = [](double Yaw)
        {
            double Result = FMath::Fmod(Yaw, 90.0);
            if (Result < 0) Result += 90;
            return Result >= 90 - 1.e-7 ? 0.0 : Result;
        };
        TArray<double> Yaws, Candidates;
        for (double Yaw : BuildingYaws)
        {
            if (!FMath::IsFinite(Yaw)) return false;
            Yaws.Add(CanonicalYaw(Yaw));
        }
        Candidates = Yaws;
        Candidates.Sort();
        int32 BestVotes = -1;
        double BestArea = TNumericLimits<double>::Max(), PreviousYaw = -1;
        for (double Yaw : Candidates)
        {
            if (FMath::IsNearlyEqual(Yaw, PreviousYaw, 1.e-7)) continue;
            PreviousYaw = Yaw;
            int32 Votes = 0;
            for (double MemberYaw : Yaws)
            {
                const double Difference = FMath::Abs(Yaw - MemberYaw);
                Votes += FMath::Min(Difference, 90 - Difference) <= .1;
            }
            if (Votes < BestVotes) continue;
            const FQuat Rotation(FVector::UpVector, FMath::DegreesToRadians(Yaw));
            FBox Bounds(ForceInit);
            for (const auto& Point : Hull)
            {
                const FVector Local = Rotation.UnrotateVector(FVector(Point.X, Point.Y, 0));
                Bounds += FVector(Local.X, Local.Y, MinZ);
                Bounds += FVector(Local.X, Local.Y, MaxZ);
            }
            const FVector CandidateSize = Bounds.GetSize();
            const double Area = CandidateSize.X * CandidateSize.Y;
            if (Votes == BestVotes && Area >= BestArea - FMath::Max(1.e-6, BestArea * 1.e-9)) continue;
            BestVotes = Votes; BestArea = Area;
            Origin = FTransform(Rotation, Reference + Rotation.RotateVector(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z)));
            Size = CandidateSize;
        }
        return BestVotes > 0;
    }
}
