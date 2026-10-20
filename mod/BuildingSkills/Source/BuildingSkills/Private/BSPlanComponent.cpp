#include "BSPlan.h"
#include "BSGroups.h"
#include "BSStick.h"
#include "BSEditModeComponent.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGInputLibrary.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Hologram/FGRailroadTrackHologram.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Buildables/FGBuildableFoundation.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "UI/FGGameUI.h"
#include "Engine/OverlapResult.h"
#include "AbstractInstanceManager.h"
#include "FGLightweightBuildableSubsystem.h"
#include "BSTransform.h"

namespace
{
    AFGCharacterPlayer* Player(const UBSPlanComponent* Plan) { return Cast<AFGCharacterPlayer>(Plan->GetOwner()); }
    UBSEditModeComponent* Edit(const UBSPlanComponent* Plan) { auto* P=Player(Plan); return P ? P->FindComponentByClass<UBSEditModeComponent>() : nullptr; }
    UBSGroupRemoteCallObject* Remote(const UBSPlanComponent* Plan)
    { auto* P=Player(Plan); auto* PC=P ? Cast<AFGPlayerController>(P->GetController()) : nullptr; return PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr; }
    AFGBuildableFoundation* HitFoundation(const FHitResult& Hit)
    {
        if (auto* Floor=Cast<AFGBuildableFoundation>(Hit.GetActor())) return Floor;
        auto* Manager=Cast<AAbstractInstanceManager>(Hit.GetActor());
        // TraceForBuilding can already replace the manager actor with the
        // instance owner (the lightweight subsystem). The hit component still
        // belongs to the manager and identifies the actual foundation.
        if (!Manager && Hit.GetComponent()) Manager=Cast<AAbstractInstanceManager>(Hit.GetComponent()->GetOwner());
        FInstanceHandle Handle;
        if (!Manager || !Manager->ResolveHit(Hit,Handle)) return nullptr;
        if (auto* Floor=Cast<AFGBuildableFoundation>(Manager->GetOwnerByHandle(Handle))) return Floor;
        FLightweightBuildableInstanceRef Instance;
        if (!AFGLightweightBuildableSubsystem::ResolveLightweightInstance(Handle,Instance) ||
            !Instance.GetBuildableClass() || !Instance.GetBuildableClass()->IsChildOf(AFGBuildableFoundation::StaticClass())) return nullptr;
        return Cast<AFGBuildableFoundation>(Instance.SpawnTemporaryBuildable());
    }
    bool Multi(const UBSPlanComponent* Plan)
    {
        auto* PC=Player(Plan) ? Cast<APlayerController>(Player(Plan)->GetController()) : nullptr;
        auto* A=LoadObject<UInputAction>(nullptr,TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/Dismantle/IA_DismantleMultiSelect.IA_DismantleMultiSelect"));
        auto* Source=LoadObject<UInputMappingContext>(nullptr,TEXT("/Game/FactoryGame/Inputs/Equipment/Buildgun/Dismantle/MC_BuildGunDismantle.MC_BuildGunDismantle"));
        if (!PC || !A || !Source) return false;
        // The native action's trigger value is not a persistent held state in
        // Build. Read its configured key/chord, just as transform modifiers do.
        for (const auto& Mapping:Source->GetMappings()) if (Mapping.Action==A && !Mapping.Key.IsGamepadKey())
        {
            FKey Key; TArray<FKey> Modifiers;
            UFGInputLibrary::GetCurrentMappingForAction(PC,Mapping.GetMappingName(),Key,Modifiers);
            bool Down=Key.IsValid() && PC->IsInputKeyDown(Key);
            for (const auto& Modifier:Modifiers) Down &= PC->IsInputKeyDown(Modifier);
            if (Down) return true;
        }
        return false;
    }
}
UBSPlanPlaceMode::UBSPlanPlaceMode() { mDisplayName=NSLOCTEXT("BuildingSkills","PlanPlace","Place"); }
UBSPlanEditMode::UBSPlanEditMode() { mDisplayName=NSLOCTEXT("BuildingSkills","PlanEdit","Edit"); }
UBSPlanComponent::UBSPlanComponent()
{ PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.bStartWithTickEnabled=true; PrimaryComponentTick.TickGroup=TG_PostUpdateWork; }
UBSPlanComponent* UBSPlanComponent::Find(AFGCharacterPlayer* P) { return P ? P->FindComponentByClass<UBSPlanComponent>() : nullptr; }
UBSPlanComponent* UBSPlanComponent::For(AFGBuildGun* G)
{ auto* Plan=Find(G ? G->GetInstigatorCharacter() : nullptr); return Plan && Plan->Active ? Plan : nullptr; }
void UBSPlanComponent::Initialize(UEnhancedInputComponent* Input)
{
    auto Bind=[&](const TCHAR* Name,auto Method) {
        if (auto* A=LoadObject<UInputAction>(nullptr,*FString::Printf(TEXT("/BuildingSkills/Inputs/IA_%s.IA_%s"),Name,Name))) Input->BindAction(A,ETriggerEvent::Started,this,Method);
    };
    Bind(TEXT("TogglePlan"),&ThisClass::Toggle);
    Bind(TEXT("PlanDelete"),&ThisClass::DeleteSelected);
    for (int32 D:{1,-1})
        if (auto* A=LoadObject<UInputAction>(nullptr,D>0 ? TEXT("/BuildingSkills/Inputs/IA_PlanScrollUp.IA_PlanScrollUp") : TEXT("/BuildingSkills/Inputs/IA_PlanScrollDown.IA_PlanScrollDown"))) Input->BindAction(A,ETriggerEvent::Started,this,&ThisClass::Scroll,D);
    const TCHAR* Names[]={TEXT("Forward"),TEXT("Backward"),TEXT("Left"),TEXT("Right"),TEXT("Up"),TEXT("Down")};
    for (int32 I=0;I<6;++I) if (auto* A=LoadObject<UInputAction>(nullptr,*FString::Printf(TEXT("/BuildingSkills/Inputs/IA_Move%s.IA_Move%s"),Names[I],Names[I]))) Input->BindAction(A,ETriggerEvent::Triggered,this,&ThisClass::Move,I);
}
bool UBSPlanComponent::Allowed() const { auto* E=Edit(this); return Active && E && E->InputAllowed(); }
FBSRailPlan* UBSPlanComponent::Current() { return Plans.FindByPredicate([&](const auto& P) { return P.Id==CurrentId; }); }
const FBSRailPlan* UBSPlanComponent::Current() const { return Plans.FindByPredicate([&](const auto& P) { return P.Id==CurrentId; }); }
void UBSPlanComponent::Toggle()
{
    auto* E=Edit(this); if (!E || !E->InputAllowed()) return;
    if (Active) { Close(true); return; }
    auto* P=Player(this); auto* PC=Cast<APlayerController>(P->GetController());
    if (!PC || !PC->GetLocalPlayer() || !FBSPlanBuild::Recipe()) return;
    E->LeaveEditMode(false); Active=true; Editing=false; EnteredAt=GetWorld()->GetRealTimeSeconds();
    if (!P->IsBuildGunEquipped()) P->ToggleBuildGun();
    // The Plan-scoped transition clears the recipe and enters the native state
    // directly; the shipped GotoBuildState wrapper rejects a null recipe.
    P->GetBuildGun()->GotoBuildState(nullptr);
    SetMode(false);
    if (auto* Source=LoadObject<UInputMappingContext>(nullptr,TEXT("/BuildingSkills/Inputs/Runtime/MC_Plan.MC_Plan")))
    {
        Context=DuplicateObject<UInputMappingContext>(Source,this);
        PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->AddMappingContext(Context,1010);
    }
    LastNode.Invalidate(); Selection.Reset(); Swept.Reset(); MultiHeld=false; NextPoll=0;
    if (auto* R=Remote(this)) R->Server_PlanSnapshot();
}
void UBSPlanComponent::Close(bool Unequip)
{
    if (!Active) return;
    FinishDrag(); CloseSettings(); Active=false; HaveAim=false; LastNode.Invalidate(); MultiHeld=false; Swept.Reset();
    auto* P=Player(this); auto* PC=P ? Cast<APlayerController>(P->GetController()) : nullptr;
    if (Context && PC && PC->GetLocalPlayer()) PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->RemoveMappingContext(Context);
    Context=nullptr;
    for (auto& C:Visuals) if (C) C->ClearInstances(); DrawHash=0;
    if (Unequip && P) P->UnequipBuildGun();
}
void UBSPlanComponent::EndPlay(EEndPlayReason::Type R)
{ Close(false); for (auto& C:Visuals) if (C) C->DestroyComponent(); Visuals.Reset(); Super::EndPlay(R); }
void UBSPlanComponent::QueueMenu(FBSPlanCommand C)
{
    C.Plan=CurrentId;
    if (Waiting || !MenuCommands.IsEmpty())
    {
        if (auto* Existing=MenuCommands.FindByPredicate([&](const auto& Q) {
            return Q.Plan==C.Plan && Q.Action==C.Action && Q.Node.Id==C.Node.Id && Q.Edge.Id==C.Edge.Id;
        })) *Existing=C;
        else MenuCommands.Add(C);
    }
    else Send(C);
}
void UBSPlanComponent::Send(FBSPlanCommand C)
{
    if (Waiting) return;
    if (auto* R=Remote(this))
    {
        if (C.Action==EBSPlanAction::New)
        {
            // Personal defaults belong to the creating player, including when
            // the plan itself is stored by a dedicated server.
            const auto Defaults=FBSTransformSettings::Get(GetWorld());
            const double Length=FMath::IsFinite(Defaults.PlanSegmentLength) ? double(Defaults.PlanSegmentLength)*100 : 8000;
            C.SegmentLength=FMath::Clamp(Length,FBSPlanBuild::MinimumLength(),FBSPlanBuild::MaximumLength());
            C.Remainder=EBSPlanRemainder(FMath::IsFinite(Defaults.PlanRemainder) ? FMath::RoundToInt(FMath::Clamp(Defaults.PlanRemainder,0.f,2.f)) : 1);
            C.SignalEvery=FMath::IsFinite(Defaults.PlanSignalEvery) ? FMath::RoundToInt(FMath::Clamp(Defaults.PlanSignalEvery,1.f,2048.f)) : 1;
            C.PlaceBlockSignal=Defaults.PlanPlaceBlockSignal;
            C.LeftSideSignal=Defaults.PlanLeftSideSignal;
        }
        if (!C.Plan.IsValid()) C.Plan=CurrentId;
        if (const auto* P=Plans.FindByPredicate([&](const auto& V) { return V.Id==C.Plan; }); C.Action!=EBSPlanAction::New && P) C.Revision=P->Revision;
        Waiting=true; NextPoll=GetWorld()->GetRealTimeSeconds()+15; R->Server_PlanCommand(C);
    }
}
void UBSPlanComponent::Receive(TArray<FBSRailPlan> Incoming,FGuid Preferred,const FString& Feedback)
{
    // Never replace a locally dragged preview until its gesture is submitted.
    if (DragNode.IsValid() || DragEdge.IsValid()) return;
    for (const auto& Command:MenuCommands)
    {
        const auto* Before=Plans.FindByPredicate([&](const auto& V) { return V.Id==Command.Plan; });
        const auto* After=Incoming.FindByPredicate([&](const auto& V) { return V.Id==Command.Plan; });
        if (!Before || !After || After->Revision!=Before->Revision+1)
        { MenuCommands.Reset(); Message=TEXT("This plan changed. Review the latest settings before editing again."); break; }
    }
    Plans=MoveTemp(Incoming); Waiting=false;
    if (Preferred.IsValid()) CurrentId=Preferred;
    if (!Feedback.IsEmpty()) Message=Feedback;
    if (!Current())
    {
        if (auto* Personal=Plans.FindByPredicate([](const auto& P) { return !P.Shared; })) CurrentId=Personal->Id;
        else if (Active) PendingCreate=true;
    }
    Selection.RemoveAll([&](FGuid Id) { const auto* P=Current(); return !P || (!FBSPlanMath::Node(*P,Id) && !P->Edges.ContainsByPredicate([&](const auto& E) { return E.Id==Id; })); });
    if (auto* P=Current(); P && LastNode.IsValid() && !FBSPlanMath::Node(*P,LastNode))
    { const auto* N=P->Nodes.FindByPredicate([&](const auto& V) { return V.Position.Equals(LastPlacedPosition,1); }); LastNode=N ? N->Id : FGuid(); }
    NextPoll=GetWorld()->GetRealTimeSeconds()+1; NextDraw=0; ResetStart();
    if (!MenuCommands.IsEmpty()) { auto Next=MenuCommands[0]; MenuCommands.RemoveAt(0); Send(Next); }
}
void UBSPlanComponent::Load(FGuid Id) { FinishDrag(); CurrentId=Id; LastNode.Invalidate(); Selection.Reset(); Message.Reset(); ResetStart(); Refresh(); }
void UBSPlanComponent::Refresh() { NextDraw=0; DrawHash=0; }
void UBSPlanComponent::ResetStart()
{ AimRoute.Reset(); }
void UBSPlanComponent::SetMode(bool Mode)
{
    FinishDrag(); Editing=Mode; LastNode.Invalidate(); ResetStart(); NextDraw=0;
    if (auto* Gun=Player(this)->GetBuildGun())
    {
        TSubclassOf<UFGBuildGunModeDescriptor> Descriptor=Mode ? UBSPlanEditMode::StaticClass() : UBSPlanPlaceMode::StaticClass();
        if (Gun->GetCurrentBuildGunMode()!=Descriptor)
        {
            TGuardValue<bool> Updating(UpdatingNativeMode,true);
            Gun->SetCurrentBuildGunMode(Descriptor);
        }
    }
}
void UBSPlanComponent::Primary(bool Release)
{
    if (Release) { FinishDrag(); return; }
    if (!Allowed() || Waiting || !Current()) return;
    AimAndHit();
    if (Multi(this) && (Editing || HoverNode.IsValid() || HoverEdge.IsValid())) { SelectHovered(); return; }
    if (Editing)
    {
        if (!HoverNode.IsValid() && (!HoverEdge.IsValid() || HoverControl<0)) return;
        DragNode=HoverNode; DragEdge=HoverEdge; DragControl=HoverControl; DragOrientation=HoverOrientation;
        DragBefore=*Current(); DragChanged=false;
        DragOrigin=DragNode.IsValid() ? HoverNodePosition : Current()->Edges.FindByPredicate([&](const auto& E) { return E.Id==DragEdge; })->Controls[DragControl];
        if (DragOrientation) DragOrigin+=250*(DragNode.IsValid() ? FBSPlanMath::Node(*Current(),DragNode)->Direction.GetSafeNormal() : FBSPlanMath::ControlDirection(*Current(),*Current()->Edges.FindByPredicate([&](const auto& E) { return E.Id==DragEdge; }),DragControl));
        FVector Eye; FRotator View; Cast<APlayerController>(Player(this)->GetController())->GetPlayerViewPoint(Eye,View); DragDistance=(DragOrigin-Eye).Size();
        Selection={DragNode.IsValid() ? DragNode : DragEdge}; Refresh(); return;
    }
    if (!HaveAim) return;
    if (AimRoute.Num()>64 && !HoverNode.IsValid()) { Message=TEXT("This curve needs more than 64 points. Add an intermediate plan node."); return; }
    FBSPlanCommand C; C.Action=EBSPlanAction::Place; C.Node=Aim;
    if (HoverNode.IsValid()) C.Node=*FBSPlanMath::Node(*Current(),HoverNode);
    else
    {
        C.Node.Id=FGuid::NewGuid();
        if (HoverEdge.IsValid() && HoverSegment>=0)
        { C.SplitEdge=HoverEdge; C.SplitSegment=HoverSegment; C.SplitTime=HoverTime; }
    }
    if (LastNode.IsValid() && LastNode!=C.Node.Id)
    {
        C.Edge.Id=FGuid::NewGuid(); C.Edge.Start=LastNode; C.Edge.End=C.Node.Id;
        if (AimRoute.Num()<=64 && AimRoute.Num()>1 && AimRoute.Last().Location.Equals(C.Node.Position,1)) for (const auto& Point:AimRoute) C.Edge.Route.Emplace(Point);
    }
    LastNode=C.Node.Id; LastPlacedPosition=C.Node.Position; Send(C);
}
void UBSPlanComponent::Secondary()
{ if (!Allowed()) return; FinishDrag(); LastNode.Invalidate(); Selection.Reset(); ResetStart(); Refresh(); }
void UBSPlanComponent::SelectHovered()
{
    if (!Allowed() || !Multi(this) || DragNode.IsValid() || DragEdge.IsValid()) return;
    const FGuid Id=HoverNode.IsValid() ? HoverNode : HoverEdge;
    if (!Id.IsValid() || Swept.Contains(Id)) return;
    if (Swept.IsEmpty()) RemovingSelection=Selection.Contains(Id);
    Swept.Add(Id);
    if (RemovingSelection) Selection.Remove(Id); else Selection.AddUnique(Id);
    Refresh();
}
void UBSPlanComponent::DeleteSelected()
{
    if (!Allowed() || Waiting || Selection.IsEmpty()) return;
    FinishDrag(); FBSPlanCommand C; C.Action=EBSPlanAction::Delete; C.Selection=Selection; Send(C);
}
void UBSPlanComponent::Undo()
{
    if (!Allowed() || Waiting) return;
    FinishDrag(); LastNode.Invalidate(); FBSPlanCommand C; C.Action=EBSPlanAction::Undo; Send(C);
}
void UBSPlanComponent::Redo()
{
    if (!Allowed() || Waiting) return;
    FinishDrag(); LastNode.Invalidate(); FBSPlanCommand C; C.Action=EBSPlanAction::Redo; Send(C);
}
void UBSPlanComponent::FinishDrag()
{
    const bool HadDrag=DragNode.IsValid() || DragEdge.IsValid();
    if (!HadDrag) return;
    if (auto* P=Current(); P && P->Id==DragBefore.Id) *P=DragBefore;
    DragNode.Invalidate(); DragEdge.Invalidate(); DragControl=-1; DragOrientation=false;
    const bool Changed=DragChanged; DragChanged=false; DragBefore={};
    if (Changed) Send(DragCommand);
    Refresh();
}
void UBSPlanComponent::SelectFromList(FGuid Id,bool Additive)
{
    const auto* P=Current();
    if (!P || (!FBSPlanMath::Node(*P,Id) && !P->Edges.ContainsByPredicate([&](const auto& E) { return E.Id==Id; }))) return;
    if (!Additive) Selection={Id};
    else if (Selection.Contains(Id)) Selection.Remove(Id); else Selection.Add(Id);
    Refresh();
}
void UBSPlanComponent::Scroll(int32 Delta)
{
    if (!Allowed()) return;
    if (DragNode.IsValid() || DragEdge.IsValid()) DragDistance=FMath::Max(0.0,DragDistance+Delta*GetWorld()->GetSubsystem<UBSStickSession>()->GetSettings().StickStep*100);
    else if (auto* E=Edit(this)) E->ScrollStick(Delta);
}
FVector UBSPlanComponent::MovementDirection(int32 Axis,double ViewYaw)
{
    if (Axis==4) return FVector::UpVector;
    if (Axis==5) return -FVector::UpVector;
    if (Axis<0 || Axis>3) return FVector::ZeroVector;
    // Yaw remains meaningful even when looking straight up/down. Snap to one
    // horizontal world axis rather than introducing diagonal or vertical drift.
    const FVector View=FRotator(0,ViewYaw,0).Vector();
    const FVector Forward=FMath::Abs(View.X)>=FMath::Abs(View.Y)
        ? FVector(View.X>=0 ? 1 : -1,0,0) : FVector(0,View.Y>=0 ? 1 : -1,0);
    const FVector Right=FVector::CrossProduct(FVector::UpVector,Forward);
    return Axis==0 ? Forward : Axis==1 ? -Forward : Axis==2 ? -Right : Right;
}
void UBSPlanComponent::Move(int32 Axis)
{
    if (!Allowed() || Waiting || Selection.IsEmpty() || GetWorld()->GetRealTimeSeconds()<NextMove) return;
    if (Axis<0 || Axis>=6) return;
    auto* Controller=Cast<APlayerController>(Player(this)->GetController());
    if (!Controller) return;
    const FRotator View=Controller->GetControlRotation();
    NextMove=GetWorld()->GetRealTimeSeconds()+.15;
    FBSPlanCommand C; C.Action=EBSPlanAction::Move; C.Selection=Selection; C.Delta=MovementDirection(Axis,View.Yaw)*GetWorld()->GetSubsystem<UBSStickSession>()->GetSettings().StickStep*100; Send(C);
}
void UBSPlanComponent::Settings()
{
    if (!Allowed() || Window) return; auto* UI=Player(this)->GetGameUI(); if (!UI) return;
    FinishDrag(); Window=CreateWidget<UBSPlanWidget>(UI->GetOwningPlayer()); Window->mInteractObject=this;
    Window->SetDefaultFocusWidget(Window); UI->PushWidget(Window); Window->SetInputMode();
}
void UBSPlanComponent::CloseSettings()
{ auto* W=Window.Get(); Window=nullptr; auto* P=Player(this); if (W && P && P->GetGameUI()) P->GetGameUI()->PopWidget(W); }
void UBSPlanComponent::AimAndHit()
{
    HoverNode.Invalidate(); HoverEdge.Invalidate(); HoverControl=-1; HoverSegment=-1; HoverOrientation=false; HaveAim=false;
    auto* P=Current(); auto* Gun=Player(this)->GetBuildGun(); if (!P || !Gun) return;
    FVector Eye; FRotator View; Cast<APlayerController>(Player(this)->GetController())->GetPlayerViewPoint(Eye,View);
    const FVector Ray=View.Vector(); double Best=1.e20;
    FVector EdgeHit=FVector::ZeroVector;
    const auto* EditState=Edit(this);
    const bool Stick=EditState && EditState->StickMode!=EBSStickMode::Off;
    const FHitResult& HitResult=Gun->GetHitResult();
    Aim={}; AimRoute.Reset(); HaveAim=Stick || HitResult.bBlockingHit;
    Aim.Position=Stick ? Eye+Ray*(EditState->StickDistance*100) : FVector(HitResult.ImpactPoint);
    Aim.Direction=FVector(Ray.X,Ray.Y,0).GetSafeNormal();
    if (Aim.Direction.IsNearlyZero()) Aim.Direction=FVector::ForwardVector;
    if (HitResult.bBlockingHit && !Multi(this) && (!Stick || FVector::DistSquared(Aim.Position,HitResult.ImpactPoint)<=FMath::Square(200.0)))
        if (auto* Floor=HitFoundation(HitResult))
            FBSPlanBuild::SnapFoundation(Floor,Aim.Position,Aim.Direction);
    const auto* Start=FBSPlanMath::Node(*P,LastNode);
    FVector StartDirection=Start ? FBSPlanMath::ConnectionDirection(*P,Start->Id) : Aim.Direction;
    if (Start)
    {
        int32 Incident=0; FVector Outward=StartDirection;
        for (const auto& Edge:P->Edges) if (Edge.Start==Start->Id || Edge.End==Start->Id)
        {
            const auto Points=FBSPlanMath::Curve(*P,Edge); if (Points.Num()<2) continue;
            ++Incident; Outward=(Edge.Start==Start->Id ? -Points[0].LeaveTangent : Points.Last().ArriveTangent).GetSafeNormal();
        }
        if (Incident==1) StartDirection=Outward;
    }
    if (Start && Stick && EditState->StickMode==EBSStickMode::Constrained)
    {
        FBSStickPath Path; Path.Start=Start->Position; Path.Forward=StartDirection;
        const double Distance=Path.ResolveConstraints(GetWorld()->GetSubsystem<UBSStickSession>()->GetSettings(),Aim.Position);
        TArray<TArray<FSplinePointData>> Parts;
        if (Path.Segments(Distance,10000,Parts))
        {
            const FVector End=FBSStickPath::EndpointDirection(Path.Derivative(Distance),EditState->StickTiltSet,EditState->StickTilt,EditState->StickYaw);
            if (FBSStickPath::FitEndDirections(Parts,StartDirection,End,10000))
                for (const auto& Part:Parts)
                {
                    if (Part.IsEmpty()) continue;
                    const int32 First=AimRoute.IsEmpty() ? 0 : 1;
                    if (First) AimRoute.Last().LeaveTangent=Part[0].LeaveTangent;
                    for (int32 I=First;I<Part.Num();++I) AimRoute.Add(Part[I]);
                }
            if (!AimRoute.IsEmpty()) { Aim.Position=AimRoute.Last().Location; Aim.Direction=End; }
        }
    }
    else if (Start) Aim.Direction=FBSStickPath::EndpointDirection(StartDirection,EditState && EditState->StickTiltSet,EditState ? EditState->StickTilt : 0,EditState ? EditState->StickYaw : 0);
    // Snap to existing native rail connection components near the aim point,
    // without constructing or updating any rail hologram.
    if (HaveAim && !Multi(this))
    {
        TSet<AFGBuildableRailroadTrack*> Rails;
        if (auto* Rail=Cast<AFGBuildableRailroadTrack>(HitResult.GetActor())) Rails.Add(Rail);
        TArray<FOverlapResult> Nearby;
        GetWorld()->OverlapMultiByObjectType(Nearby,Aim.Position,FQuat::Identity,FCollisionObjectQueryParams::AllObjects,FCollisionShape::MakeSphere(200),FCollisionQueryParams(SCENE_QUERY_STAT(BSPlanSnap),false,Player(this)));
        for (const auto& Result:Nearby) if (auto* Rail=Cast<AFGBuildableRailroadTrack>(Result.GetActor())) Rails.Add(Rail);
        double Nearest=200*200;
        const FVector SnapTarget=Aim.Position;
        for (auto* Rail:Rails) for (int32 End=0;End<2;++End) if (auto* Port=Rail->GetConnection(End))
        {
            const double D=FVector::DistSquared(SnapTarget,Port->GetConnectorLocation());
            if (D>=Nearest) continue;
            Nearest=D; Aim.Position=Port->GetConnectorLocation(); Aim.Direction=Port->GetConnectorNormal(); AimRoute.Reset();
        }
    }
    const bool Placing=!Editing && !Multi(this);
    const bool StickPlacing=Placing && EditState && EditState->StickMode!=EBSStickMode::Off;
    auto Hit=[&](FVector Pos,double Radius) {
        const double Depth=FVector::DotProduct(Pos-Eye,Ray);
        const double Distance=(Pos-Eye-Ray*Depth).Size();
        const double Threshold=FMath::Clamp(Depth*.008,Radius,150.0);
        if (Depth<=0 || Distance>Threshold || Depth>=Best) return false; Best=Depth; return true;
    };
    // A node between the camera and the stick tip must not capture every
    // subsequent click on a straight chain. Stick snapping is local to its tip.
    for (const auto& N:P->Nodes)
        if ((!Placing || N.Id!=LastNode) && (!StickPlacing || N.Position.Equals(Aim.Position,150)) && Hit(N.Position,55))
        { HoverNode=N.Id; HoverNodePosition=N.Position; HoverEdge.Invalidate(); HoverControl=-1; HoverOrientation=false; }
    // Replicas select and move their source node; they never create a second,
    // independently editable graph. Place mode continues to snap main nodes.
    if (Editing || Multi(this)) for (const auto& N:ParallelHoverNodes)
        if (FBSPlanMath::Node(*P,N.Id) && Hit(N.Position,55))
        { HoverNode=N.Id; HoverNodePosition=N.Position; HoverEdge.Invalidate(); HoverControl=-1; HoverOrientation=false; }
    if (Editing) for (const auto& N:P->Nodes)
        if (Hit(N.Position+N.Direction.GetSafeNormal()*250,25))
        { HoverNode=N.Id; HoverNodePosition=N.Position; HoverEdge.Invalidate(); HoverControl=-1; HoverOrientation=true; }
    if (!HoverNode.IsValid()) for (const auto& E:P->Edges)
    {
        if (Editing && (E.Trace==EBSPlanTrace::Free || E.Trace==EBSPlanTrace::Bezier)) for (int32 I=0;I<E.Controls.Num();++I) {
            if (Hit(E.Controls[I],45)) { HoverEdge=E.Id; HoverControl=I; HoverOrientation=false; }
            if (E.Trace==EBSPlanTrace::Free && Hit(E.Controls[I]+FBSPlanMath::ControlDirection(*P,E,I)*250,22)) { HoverEdge=E.Id; HoverControl=I; HoverOrientation=true; }
        }
        if (HoverControl>=0) continue;
        const auto Points=FBSPlanMath::Curve(*P,E);
        for (int32 I=1;I<Points.Num();++I) for (int32 S=0;S<32;++S)
        {
            FVector OnRay,OnCurve;
            FMath::SegmentDistToSegmentSafe(Eye,Eye+Ray*1.e8,
                FBSPlanMath::Point(Points[I-1],Points[I],S/32.0),FBSPlanMath::Point(Points[I-1],Points[I],(S+1)/32.0),OnRay,OnCurve);
            if ((!StickPlacing || OnCurve.Equals(Aim.Position,150)) && Hit(OnCurve,25))
            { HoverEdge=E.Id; EdgeHit=OnCurve; HoverOrientation=false; }
        }
    }
    if (DragNode.IsValid() || DragEdge.IsValid())
    {
        if (GetWorld()->GetRealTimeSeconds()<NextDraw) return;
        const FVector Position=Eye+Ray*DragDistance;
        FBSPlanCommand Command; Command.Revision=DragBefore.Revision;
        if (const auto* N=FBSPlanMath::Node(DragBefore,DragNode))
        {
            if (DragOrientation) { Command.Action=EBSPlanAction::Node; Command.Node=*N; Command.Node.Direction=(Position-N->Position).GetSafeNormal(); }
            else { Command.Action=EBSPlanAction::Move; Command.Selection={DragNode}; Command.Delta=Position-DragOrigin; }
        }
        else if (const auto* E=DragBefore.Edges.FindByPredicate([&](const auto& V) { return V.Id==DragEdge; }); E && E->Controls.IsValidIndex(DragControl))
        {
            Command.Action=EBSPlanAction::Edge; Command.Edge=*E;
            if (!DragOrientation) Command.Edge.Controls[DragControl]=Position;
            else
            {
                const FVector Direction=(Position-E->Controls[DragControl]).GetSafeNormal();
                if (Direction.IsNearlyZero()) return;
                Command.Edge.ControlDirections.SetNumZeroed(E->Controls.Num()); Command.Edge.ControlDirections[DragControl]=Direction;
            }
        }
        auto Preview=DragBefore; FString Error;
        if (FBSPlanMath::Apply(Preview,Command,Error))
        { Preview.Revision=DragBefore.Revision; *P=MoveTemp(Preview); DragCommand=MoveTemp(Command); DragChanged=true; }
        else if (!Error.IsEmpty()) Message=Error;
        NextDraw=0; return;
    }
    if (HoverNode.IsValid()) { Aim=*FBSPlanMath::Node(*P,HoverNode); Aim.Direction=FBSPlanMath::ConnectionDirection(*P,HoverNode); HaveAim=true; AimRoute.Reset(); }
    else if (Placing && HoverEdge.IsValid())
    {
        const auto* E=P->Edges.FindByPredicate([&](const auto& V) { return V.Id==HoverEdge; });
        const auto Points=FBSPlanMath::Curve(*P,*E); FVector At;
        if (FBSPlanMath::Closest(Points,StickPlacing ? Aim.Position : EdgeHit,HoverSegment,HoverTime,At))
        {
            if (At.Equals(Points[0].Location,55)) { HoverNode=E->Start; Aim=*FBSPlanMath::Node(*P,E->Start); }
            else if (At.Equals(Points.Last().Location,55)) { HoverNode=E->End; Aim=*FBSPlanMath::Node(*P,E->End); }
            else { Aim.Position=At; Aim.Direction=FBSPlanMath::Tangent(Points[HoverSegment],Points[HoverSegment+1],HoverTime).GetSafeNormal(); }
            HaveAim=true; AimRoute.Reset();
        }
    }
    HaveAim &= !Aim.Position.ContainsNaN();
}
void UBSPlanComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,Type,Function); if (!Active) return;
    auto* P=Player(this); auto* Gun=P ? P->GetBuildGun() : nullptr;
    auto* Build=Gun ? Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState()) : nullptr;
    if ((!P || !P->IsBuildGunEquipped() || !Build || Build->GetActiveRecipe()) && GetWorld()->GetRealTimeSeconds()-EnteredAt>2) { Close(false); return; }
    if (!Build) return;
    if (PendingCreate && !Waiting) { PendingCreate=false; FBSPlanCommand C; C.Action=EBSPlanAction::New; C.Name=TEXT("Untitled plan"); Send(C); }
    if (GetWorld()->GetRealTimeSeconds()>NextPoll && !Window && !DragNode.IsValid() && !DragEdge.IsValid())
    { Waiting=false; NextPoll=GetWorld()->GetRealTimeSeconds()+2; if (auto* R=Remote(this)) R->Server_PlanSnapshot(); }
    if (Allowed())
    {
        const bool Held=Multi(this);
        if (!Held || !MultiHeld) Swept.Reset();
        MultiHeld=Held; AimAndHit(); if (Held) SelectHovered();
    }
    else { MultiHeld=false; Swept.Reset(); }
    if (GetWorld()->GetRealTimeSeconds()>=NextDraw) { Draw(); NextDraw=GetWorld()->GetRealTimeSeconds()+.05; }
}
FText UBSPlanComponent::Status() const
{
    const auto* P=Current(); const auto* E=Edit(this);
    return FText::FromString(FString::Printf(TEXT("%s%s\nStick: %s | Distance: %.2f m\n%d nodes | %d splines | %d selected%s%s"),
        P ? *P->Name : TEXT("Choose or create a plan with K"),P && P->Shared ? TEXT(" (shared)") : TEXT(""),
        E && E->StickMode==EBSStickMode::Default ? TEXT("Default") : E && E->StickMode==EBSStickMode::Constrained ? TEXT("Constrained") : TEXT("Off"),E ? E->StickDistance : 0,
        P ? P->Nodes.Num() : 0,P ? P->Edges.Num() : 0,Selection.Num(),Message.IsEmpty() ? TEXT("") : TEXT("\n"),*Message));
}
TArray<TPair<FText,FText>> UBSPlanComponent::Hints() const
{
    auto* PC=Cast<APlayerController>(Player(this)->GetController());
    TArray<TPair<FText,FText>> Out;
    auto Hint=[&](const TCHAR* Label,FName Name) { Out.Emplace(FText::FromString(Label),UFGInputLibrary::GetInputActionNameAsText_Keyboard(PC,Name)); };
    Hint(TEXT("Plans / selection settings"),TEXT("BuildingSkills.Settings")); Hint(TEXT("Toggle stick"),TEXT("BuildingSkills.StickToggle"));
    if (!Selection.IsEmpty()) Hint(TEXT("Delete selection"),TEXT("BuildingSkills.PlanDelete"));
    Hint(TEXT("Undo plan edit"),TEXT("BuildingSkills.Undo")); Hint(TEXT("Redo plan edit"),TEXT("BuildingSkills.Redo"));
    if (!Selection.IsEmpty())
    {
        auto Keys=[&](std::initializer_list<const TCHAR*> Names) { FString Text; for (const auto* Name:Names) { if (!Text.IsEmpty()) Text+=TEXT(" / "); Text+=UFGInputLibrary::GetInputActionNameAsText_Keyboard(PC,Name).ToString(); } return FText::FromString(Text); };
        Out.Emplace(FText::FromString(TEXT("Move X / Y")),Keys({TEXT("BuildingSkills.MoveForward"),TEXT("BuildingSkills.MoveBackward"),TEXT("BuildingSkills.MoveLeft"),TEXT("BuildingSkills.MoveRight")}));
        Out.Emplace(FText::FromString(TEXT("Move Z")),Keys({TEXT("BuildingSkills.MoveUp"),TEXT("BuildingSkills.MoveDown")}));
    }
    Hint(TEXT("Distance / drag depth"),TEXT("BuildingSkills.PlanScrollUp"));
    Out.Emplace(FText::FromString(Editing ? TEXT("Drag node / control") : TEXT("Place / connect node")),FText::FromString(TEXT("LMB")));
    Out.Emplace(FText::FromString(TEXT("End chain / clear selection")),FText::FromString(TEXT("RMB")));
    return Out;
}
