#include "BSTransformers.h"
#include "Math/RotationMatrix.h"

bool FBSTransformer::Valid() const
{
    return Id.IsValid() && !Frame.ContainsNaN() && Frame.GetRotation().IsNormalized() &&
        Frame.GetScale3D().Equals(FVector::OneVector,.0001) && Frame.GetLocation().GetAbsMax()<1.e8 &&
        !OriginalAnchor.ContainsNaN() && OriginalAnchor.GetRotation().IsNormalized() &&
        OriginalAnchor.GetScale3D().Equals(FVector::OneVector,.0001) && OriginalAnchor.GetLocation().GetAbsMax()<1.e8 &&
        !(Mirror && Rotation) && FMath::IsFinite(Angle) && FMath::Abs(Angle)<=360 &&
        FMath::IsFinite(Radius) && Radius>=0 && Radius<=1000 &&
        FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) && FMath::IsFinite(Color.B) &&
        Color.R>=0 && Color.R<=1 && Color.G>=0 && Color.G<=1 && Color.B>=0 && Color.B<=1;
}
FTransform FBSTransformer::DisplayFrame() const
{ return RelativeToOriginalAnchor ? Frame.GetRelativeTransform(OriginalAnchor) : Frame; }
void FBSTransformer::SetDisplayFrame(const FTransform& Value)
{
    Frame = RelativeToOriginalAnchor ? Value * OriginalAnchor : Value;
    Frame.SetScale3D(FVector::OneVector);
    Frame.NormalizeRotation();
}
bool FBSTransformer::Contains(const FVector& Center) const
{ return Valid() && (Mirror || Rotation) && FVector::DistSquared(Center,Frame.GetLocation())<=FMath::Square(Radius*100+.1); }
FVector FBSTransformer::Vector(const FVector& World) const
{
    FVector Local=Frame.InverseTransformVectorNoScale(World);
    if(Mirror) Local.Y=-Local.Y;
    else if(Rotation) Local=FQuat(FVector::UpVector,FMath::DegreesToRadians(Angle)).RotateVector(Local);
    return Frame.TransformVectorNoScale(Local);
}
FVector FBSTransformer::Point(const FVector& World) const
{ return Frame.GetLocation()+Vector(World-Frame.GetLocation()); }
FTransform FBSTransformer::Pose(const FTransform& World,const FVector& LocalForward) const
{
    // Preserve a local vertical symmetry plane chosen from the port layout.
    // Two reflections retain positive scale and the mesh's original handedness.
    const double Heading=Mirror?FMath::Atan2(LocalForward.Y,LocalForward.X):0;
    const FQuat Basis(FVector::UpVector,Heading);
    const FVector X=Vector((World.GetRotation()*Basis).GetAxisX());
    const FVector Z=Vector(World.GetRotation().GetAxisZ());
    return FTransform(FRotationMatrix::MakeFromXZ(X,Z).ToQuat()*Basis.Inverse(),Point(World.GetLocation()),World.GetScale3D());
}
TArray<FBSTransformerNode> FBSTransformers::Plan(const TArray<FBSTransformer>& List,const FVector& Center,bool& Limited)
{
    Limited=false;
    TArray<FBSTransformerNode> Nodes; Nodes.Add({INDEX_NONE,INDEX_NONE,Center});
    for(int32 I=0; I<List.Num(); ++I)
    {
        const auto& Rule=List[I]; const int32 End=Nodes.Num();
        for(int32 Parent=0; Parent<End; ++Parent)
        {
            if(!(Parent==0?Rule.RepeatPlayer:Rule.RepeatTransformers) || !Rule.Contains(Nodes[Parent].Center)) continue;
            const FVector Target=Rule.Point(Nodes[Parent].Center);
            if(Nodes.Num()>MaxCopies) { Limited=true; return Nodes; }
            Nodes.Add({Parent,I,Target});
        }
    }
    return Nodes;
}
