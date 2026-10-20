#include "BSPlan.h"
#include "BSStick.h"
#include "BSEditModeComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Hologram/FGSplineHologram.h"
#include "UObject/UnrealType.h"

void UBSPlanComponent::Draw()
{
    const auto* CurrentPlan=Current();
    auto* E=GetOwner()->FindComponentByClass<UBSEditModeComponent>();
    const FVector Limits=FBSStick::PlanLimits(FBSStick::Active(E));
    const FLinearColor Colors[]={FLinearColor(.05,.7,1),FLinearColor(1,.03,.03),FLinearColor(1,.85,.05),FLinearColor(.3,.5,.6),FLinearColor(.05,.7,1),FLinearColor(1,.85,.05),FLinearColor(.9,.2,1),FLinearColor(.15,1,.5),FLinearColor(1,.03,.03),FLinearColor(.15,1,.5),FLinearColor(.2,1,.8),FLinearColor(.2,1,.8)};
    while (Visuals.Num()<12)
    {
        const int32 Index=Visuals.Num(); auto* C=NewObject<UInstancedStaticMeshComponent>(GetOwner());
        C->SetMobility(EComponentMobility::Movable); C->SetAbsolute(true,true,true); C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetGenerateOverlapEvents(false); C->SetCastShadow(false); C->SetCanEverAffectNavigation(false);
        C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,(Index>=4 && Index<=6) || Index==9 || Index==10 ? TEXT("/Engine/BasicShapes/Sphere.Sphere") : TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
        if (auto* M=LoadObject<UMaterialInterface>(nullptr,TEXT("/BuildingSkills/Materials/M_Volume.M_Volume")))
        {
            auto* MID=UMaterialInstanceDynamic::Create(M,this); MID->SetVectorParameterValue(TEXT("Color"),Colors[Index]); MID->SetScalarParameterValue(TEXT("Opacity"),.85); C->SetMaterial(0,MID);
        }
        C->RegisterComponent(); Visuals.Add(C);
    }
    auto Line=[&](int32 Kind,FVector A,FVector B,float Width=5) {
        const FVector D=B-A; if (D.IsNearlyZero()) return;
        Visuals[Kind]->AddInstance(FTransform(FQuat::FindBetweenNormals(FVector::UpVector,D.GetSafeNormal()),(A+B)*.5,FVector(Width/100,Width/100,D.Size()/100)),true);
    };
    auto Sphere=[&](int32 Kind,FVector At,double Diameter) { Visuals[Kind]->AddInstance(FTransform(FQuat::Identity,At,FVector(Diameter/100)),true); };
    uint32 Hash=GetTypeHash(CurrentId); Hash=HashCombine(Hash,Editing);
    for (const auto& P:Plans) { Hash=HashCombine(Hash,GetTypeHash(P.Id)); Hash=HashCombine(Hash,P.Revision); }
    for (FGuid Id:Selection) Hash=HashCombine(Hash,GetTypeHash(Id));
    Hash=HashCombine(Hash,GetTypeHash(HoverNode)); Hash=HashCombine(Hash,GetTypeHash(HoverEdge));
    Hash=HashCombine(Hash,HoverOrientation); Hash=HashCombine(Hash,GetTypeHash(HoverControl));
    const bool Rebuild=Hash!=DrawHash || DragNode.IsValid() || DragEdge.IsValid();
    if (Rebuild) ParallelHoverNodes.Reset();
    if (Rebuild) for (int32 I=0;I<Visuals.Num();++I) if (I<7 || I>=10) Visuals[I]->ClearInstances();
    for (int32 I=7;I<10;++I) Visuals[I]->ClearInstances();
    auto Curve=[&](const TArray<FSplinePointData>& Points,int32 Color,bool Preview,const FBSPlanNode* A=nullptr,const FBSPlanNode* B=nullptr) {
        double Length=0;
        for (int32 I=1;I<Points.Num();++I) for (int32 S=0;S<16;++S) Length+=(FBSPlanMath::Point(Points[I-1],Points[I],(S+1)/16.0)-FBSPlanMath::Point(Points[I-1],Points[I],S/16.0)).Size();
        for (int32 I=1;I<Points.Num();++I)
        {
            const int32 Steps=FMath::Clamp(FMath::CeilToInt((Points[I].Location-Points[I-1].Location).Size()/200),8,128);
            for (int32 S=0;S<Steps;++S)
            {
                const double T=double(S)/Steps, U=double(S+1)/Steps;
                bool Valid=Length>=Limits.Z && FBSPlanMath::ShapeValid(Points[I-1],Points[I],T,Limits.X,Limits.Y) && FBSPlanMath::ShapeValid(Points[I-1],Points[I],(T+U)*.5,Limits.X,Limits.Y) && FBSPlanMath::ShapeValid(Points[I-1],Points[I],U,Limits.X,Limits.Y);
                if (A && I==1 && S==0) Valid &= FMath::Abs(FVector::DotProduct(Points[0].LeaveTangent.GetSafeNormal(),A->Direction.GetSafeNormal()))>=.999;
                if (B && I==Points.Num()-1 && S==Steps-1) Valid &= FMath::Abs(FVector::DotProduct(Points.Last().ArriveTangent.GetSafeNormal(),B->Direction.GetSafeNormal()))>=.999;
                if (Color==2 && !Valid) Line(2,FBSPlanMath::Point(Points[I-1],Points[I],T),FBSPlanMath::Point(Points[I-1],Points[I],U),12);
                Line(Valid ? Color : Preview ? 8 : 1,FBSPlanMath::Point(Points[I-1],Points[I],T),FBSPlanMath::Point(Points[I-1],Points[I],U));
            }
        }
    };
    if (Rebuild) for (const auto& P:Plans)
    {
        const bool IsCurrent=P.Id==CurrentId; if (!IsCurrent && !P.Shared) continue;
        TArray<FBSPlanPath> Paths; FString PathError;
        TMap<int32,TSet<FGuid>> DrawnParallelNodes;
        if (FBSPlanSegments::Paths(P,Paths,PathError,false)) for (const auto& Path:Paths)
        {
            if (Path.Parallel>0)
            {
                bool Selected=false; for (FGuid Id:Path.Edges) Selected |= Selection.Contains(Id) || HoverEdge==Id;
                Curve(Path.Points,!Path.PlacementValid ? 1 : !IsCurrent ? 3 : Selected ? 2 : 0,false);
                for (const auto& Node:Path.Nodes)
                {
                    if (DrawnParallelNodes.FindOrAdd(Path.Parallel).Contains(Node.Id)) continue;
                    DrawnParallelNodes[Path.Parallel].Add(Node.Id);
                    const bool NodeSelected=IsCurrent && (Selection.Contains(Node.Id) || HoverNode==Node.Id);
                    Sphere(NodeSelected ? 5 : 4,Node.Position,Editing ? 60 : 35);
                    if (IsCurrent) ParallelHoverNodes.Add(Node);
                }
            }
            for (int32 I=1;I<Path.Points.Num();++I)
            {
                const auto& A=Path.Points[I-1]; const auto& B=Path.Points[I];
                const FVector At=FBSPlanMath::Point(A,B,.5),D=FBSPlanMath::Tangent(A,B,.5).GetSafeNormal()*(Path.SignalReverse ? -1 : 1);
                const FVector Right=FVector::CrossProduct(FVector::UpVector,D).GetSafeNormal();
                const int32 Color=IsCurrent ? 0 : 3;
                Line(Color,At-D*100,At+D*100,9);
                Line(Color,At+D*100,At+D*20+Right*60,9); Line(Color,At+D*100,At+D*20-Right*60,9);
            }
        }
        for (const auto& N:P.Nodes)
        {
            const bool Selected=IsCurrent && (Selection.Contains(N.Id) || HoverNode==N.Id);
            Sphere(Selected ? 5 : 4,N.Position,Editing ? 60 : 35);
            if (Editing && IsCurrent)
            { const FVector Handle=N.Position+N.Direction.GetSafeNormal()*250; Line(11,N.Position,Handle,3); Sphere(Selected ? 5 : 10,Handle,25); }
        }
        for (const auto& Edge:P.Edges)
        {
            const int32 Color=!IsCurrent ? 3 : Selection.Contains(Edge.Id) || HoverEdge==Edge.Id ? 2 : 0;
            const auto Points=FBSPlanMath::Curve(P,Edge);
            Curve(Points,Color,false,FBSPlanMath::Node(P,Edge.Start),FBSPlanMath::Node(P,Edge.End));
            if (Editing && IsCurrent && (Edge.Trace==EBSPlanTrace::Free || Edge.Trace==EBSPlanTrace::Bezier))
            {
                FVector Prev=FBSPlanMath::Node(P,Edge.Start)->Position;
                for (int32 I=0;I<Edge.Controls.Num();++I)
                {
                    const FVector Control=Edge.Controls[I]; const bool Selected=Selection.Contains(Edge.Id) || (HoverEdge==Edge.Id && HoverControl==I);
                    Sphere(Selected ? 5 : 6,Control,45); Line(3,Prev,Control,2); Prev=Control;
                    if (Edge.Trace==EBSPlanTrace::Free)
                    { const FVector Handle=Control+FBSPlanMath::ControlDirection(P,Edge,I)*250; Line(11,Control,Handle,3); Sphere(Selected ? 5 : 10,Handle,22); }
                }
                Line(3,Prev,FBSPlanMath::Node(P,Edge.End)->Position,2);
            }
        }
    }
    if (!Editing && HaveAim && Allowed())
    {
        Sphere(9,Aim.Position,35);
        if (CurrentPlan && LastNode.IsValid())
        {
            auto Draft=*CurrentPlan; auto N=Aim; if (!N.Id.IsValid()) N.Id=FGuid::NewGuid();
            if (!FBSPlanMath::Node(Draft,N.Id)) Draft.Nodes.Add(N);
            FBSPlanEdge Edge; Edge.Id=FGuid::NewGuid(); Edge.Start=LastNode; Edge.End=N.Id; for (const auto& Point:AimRoute) Edge.Route.Emplace(Point);
            FBSPlanMath::InheritTangents(Draft,Edge);
            auto Points=FBSPlanMath::Curve(Draft,Edge); Curve(Points,7,true);
            if (Edge.Start!=Edge.End)
            {
                Draft.Edges.Add(Edge);
                TArray<FBSPlanPath> PreviewPaths; FString Error;
                if (FBSPlanSegments::Paths(Draft,PreviewPaths,Error,false))
                    for (const auto& Path:PreviewPaths) if (Path.Parallel>0 && Path.Edges.Contains(Edge.Id))
                    {
                        Curve(Path.Points,Path.PlacementValid ? 7 : 8,true);
                        for (const auto& Node:Path.Nodes) if (Node.Id==N.Id) Sphere(9,Node.Position,35);
                    }
            }
        }
    }
    DrawHash=Hash;
}
