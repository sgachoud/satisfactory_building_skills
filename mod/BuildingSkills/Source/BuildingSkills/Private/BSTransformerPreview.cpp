#include "BSTransformers.h"
#include "BSEditModeComponent.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Hologram/FGHologram.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Buildables/FGBuildable.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"

namespace
{
    UStaticMeshComponent* Mesh(UBSEditModeComponent* Edit,bool Spline=false)
    {
        UStaticMeshComponent* C=Spline?NewObject<USplineMeshComponent>(Edit->GetOwner()):NewObject<UStaticMeshComponent>(Edit->GetOwner());
        C->SetMobility(EComponentMobility::Movable); C->SetAbsolute(true,true,true); C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetGenerateOverlapEvents(false); C->SetCastShadow(false); C->SetCanEverAffectNavigation(false); C->RegisterComponent(); return C;
    }
    struct FPreviewPart { UStaticMeshComponent* Mesh; FTransform Pose; FTransform Frame; bool Instance=false; FVector MirrorForward=FVector::ForwardVector; };
}
void UBSEditModeComponent::ClearTransformerVisuals()
{
    for(auto& C:TransformerMarkers) if(C) C->DestroyComponent(); TransformerMarkers.Reset();
    for(auto& C:TransformerPreview) if(C) C->DestroyComponent(); TransformerPreview.Reset();
}
void UBSEditModeComponent::TickTransformers()
{
    auto* P=Cast<AFGCharacterPlayer>(GetOwner()); auto* PC=P?Cast<AFGPlayerController>(P->GetController()):nullptr;
    auto* RCO=PC?PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>():nullptr;
    if(!P || !P->IsLocallyControlled() || !RCO) return;
    const bool Editing=IsEditing();
    if(!Editing && TransformersWindow) CloseTransformerSettings();
    auto Context=[&](bool Active,TObjectPtr<UInputMappingContext>& Current,const TCHAR* Path)
    {
        if(!InputSubsystem.IsValid()) return;
        if(Active && !Current) if(auto* Source=LoadObject<UInputMappingContext>(nullptr,Path)) {Current=DuplicateObject<UInputMappingContext>(Source,this);InputSubsystem->AddMappingContext(Current,1004);}
        if(!Active && Current){InputSubsystem->RemoveMappingContext(Current);Current=nullptr;}
    };
    Context(Editing && !bTransforming,TransformersContext,TEXT("/BuildingSkills/Inputs/MC_Transformers.MC_Transformers"));
    Context(Editing && !bTransforming && BlueprintAnchor.IsAlive(GetWorld()),AddTransformerContext,TEXT("/BuildingSkills/Inputs/MC_AddTransformer.MC_AddTransformer"));
    auto* Gun=P->IsBuildGunEquipped()?P->GetBuildGun():nullptr;
    auto* Build=Gun?Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState()):nullptr;
    auto* H=Build?Build->GetHologram():nullptr;
    const bool Show=(Editing || H) && InputAllowed();
    const auto& Rules=RCO->Transformers;
    // Markers are owned only by this local player, with no collision or ticking actors.
    for(int32 I=0;I<Rules.Num();++I)
    {
        if(!TransformerMarkers.IsValidIndex(I))
        {
            auto* C=Mesh(this); C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            if(auto* M=LoadObject<UMaterialInterface>(nullptr,TEXT("/BuildingSkills/Materials/M_Volume.M_Volume"))) C->SetMaterial(0,UMaterialInstanceDynamic::Create(M,this));
            TransformerMarkers.Add(C);
        }
        auto* C=TransformerMarkers[I].Get(); const auto& R=Rules[I];
        C->SetVisibility(Show && (R.Mirror || R.Rotation));
        C->SetWorldTransform(FTransform(R.Frame.GetRotation(),R.Frame.GetLocation(),R.Mirror?FVector(R.Radius*2,.025,R.Radius*2):FVector(.06,.06,R.Radius*2)));
        if(auto* M=Cast<UMaterialInstanceDynamic>(C->GetMaterial(0))){M->SetVectorParameterValue(TEXT("Color"),R.Color);M->SetScalarParameterValue(TEXT("Opacity"),R.Mirror?.14f:.8f);}
    }
    while(TransformerMarkers.Num()>Rules.Num()){TransformerMarkers.Last()->DestroyComponent();TransformerMarkers.Pop();}
    if(!H || !Show || Rules.IsEmpty() || H->IsHidden()) {for(auto& C:TransformerPreview)C->SetVisibility(false);return;}
    if(GetWorld()->GetRealTimeSeconds()<NextTransformerPreview) return;
    NextTransformerPreview=GetWorld()->GetRealTimeSeconds()+.033;
    TArray<AFGHologram*> Holos{H};
    for(int32 I=0;I<Holos.Num() && I<256;++I) for(auto* Child:Holos[I]->GetHologramChildren()) if(IsValid(Child) && !Child->IsDisabled()) Holos.AddUnique(Child);
    TArray<FPreviewPart> Parts; FBox Bounds(ForceInit);TMap<AActor*,FVector> MirrorAxes;
    for(auto* Actor:Holos)
        for(auto* C:TInlineComponentArray<UStaticMeshComponent*>(Actor))
        {
            if(!C->GetStaticMesh() || !C->IsVisible() || C->bHiddenInGame)continue;
            FTransform Frame=Actor->GetActorTransform();AActor* PortOwner=Actor;
            if(auto* Blueprint=Cast<AFGBlueprintHologram>(Actor))
                for(const auto& Pair:Blueprint->mBuildableToNewRoot) if(Pair.Value && (C==Pair.Value || C->IsAttachedTo(Pair.Value))) {Frame=Pair.Value->GetComponentTransform();PortOwner=Pair.Key;break;}
            if(!MirrorAxes.Contains(PortOwner))MirrorAxes.Add(PortOwner,FBSTransformers::MirrorForward(PortOwner));
            Bounds+=C->Bounds.GetBox();
            if(auto* ISM=Cast<UInstancedStaticMeshComponent>(C))
            {for(int32 N=0;N<ISM->GetInstanceCount() && Parts.Num()<2048;++N){FTransform T;ISM->GetInstanceTransform(N,T,true);Parts.Add({C,T,T,true});}}
            else Parts.Add({C,C->GetComponentTransform(),Frame,false,MirrorAxes[PortOwner]});
        }
    if(!Bounds.IsValid || Parts.IsEmpty()){for(auto& C:TransformerPreview)C->SetVisibility(false);return;}
    bool Limited=false; const auto Nodes=FBSTransformers::Plan(Rules,Bounds.GetCenter(),Limited);
    int32 Used=0;
    for(int32 N=1;N<Nodes.Num();++N)
    {
        TArray<int32> Chain; for(int32 I=N;I>0;I=Nodes[I].Parent)Chain.Insert(Nodes[I].Transformer,0);
        auto Pose=[&](FTransform T,const FVector& Axis){for(int32 I:Chain)T=Rules[I].Pose(T,Axis);return T;};
        auto Point=[&](FVector V){for(int32 I:Chain)V=Rules[I].Point(V);return V;};
        auto Vector=[&](FVector V){for(int32 I:Chain)V=Rules[I].Vector(V);return V;};
        for(const auto& Part:Parts)
        {
            if(Used==4096)break;
            auto* Spline=Cast<USplineMeshComponent>(Part.Mesh);
            if(TransformerPreview.IsValidIndex(Used) && bool(Cast<USplineMeshComponent>(TransformerPreview[Used]))!=bool(Spline))
            {TransformerPreview[Used]->DestroyComponent();TransformerPreview[Used]=Mesh(this,Spline!=nullptr);}
            if(!TransformerPreview.IsValidIndex(Used))TransformerPreview.Add(Mesh(this,Spline!=nullptr));
            auto* C=TransformerPreview[Used++].Get(); C->SetStaticMesh(Part.Mesh->GetStaticMesh());
            for(int32 Slot=0;Slot<Part.Mesh->GetNumMaterials();++Slot) C->SetMaterial(Slot,Part.Mesh->GetMaterial(Slot));
            // Native holograms use stencil/post-process rendering, not just
            // material overrides. Preserve that state on the generated meshes.
            C->SetRenderCustomDepth(Part.Mesh->bRenderCustomDepth);
            C->SetCustomDepthStencilValue(Part.Mesh->CustomDepthStencilValue);
            C->SetCustomDepthStencilWriteMask(Part.Mesh->CustomDepthStencilWriteMask);
            C->SetRenderInMainPass(Part.Mesh->bRenderInMainPass);
            C->SetRenderInDepthPass(Part.Mesh->bRenderInDepthPass);
            C->SetCustomPrimitiveDataFloatArray(0,Part.Mesh->GetCustomPrimitiveData().Data);
            const FTransform Target=Part.Pose.GetRelativeTransform(Part.Frame)*Pose(Part.Frame,Part.MirrorForward);
            C->SetWorldTransform(Target); C->SetVisibility(true);
            if(auto* Out=Cast<USplineMeshComponent>(C))
            {
                Out->SetForwardAxis(Spline->GetForwardAxis(),false);
                Out->SetStartScale(Spline->GetStartScale(),false);Out->SetEndScale(Spline->GetEndScale(),false);
                Out->SetStartRoll(Spline->GetStartRoll(),false);Out->SetEndRoll(Spline->GetEndRoll(),false);
                Out->SetSplineUpDir(Target.InverseTransformVectorNoScale(Vector(Part.Pose.TransformVectorNoScale(Spline->GetSplineUpDir()))),false);
                Out->SetStartAndEnd(Target.InverseTransformPosition(Point(Part.Pose.TransformPosition(Spline->GetStartPosition()))),Target.InverseTransformVector(Vector(Part.Pose.TransformVector(Spline->GetStartTangent()))),
                    Target.InverseTransformPosition(Point(Part.Pose.TransformPosition(Spline->GetEndPosition()))),Target.InverseTransformVector(Vector(Part.Pose.TransformVector(Spline->GetEndTangent()))),true);
            }
        }
    }
    while(TransformerPreview.Num()>Used){TransformerPreview.Last()->DestroyComponent();TransformerPreview.Pop();}
}
