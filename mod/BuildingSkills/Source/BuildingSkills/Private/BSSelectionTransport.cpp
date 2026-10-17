#include "BSGroups.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGCharacterPlayer.h"
#include "Engine/World.h"

namespace
{
    UBSEditModeComponent* SelectionEdit(UBSGroupRemoteCallObject* RCO)
    {
        auto* Player=RCO->GetOwnerPlayerCharacter();
        return Player?Player->FindComponentByClass<UBSEditModeComponent>():nullptr;
    }
    int32 SelectionCount(const FBSSelection& Items) { return Items.Groups.Num()+Items.Buildings.Num(); }
}

bool FBSSelectionTransfer::Pack(const FBSSelection& Source,int32 Offset,FBSSelection& Batch)
{
    Batch={};
    if(Source.Groups.Num()>2048 || Source.Buildings.Num()>2048 || Offset<0 || Offset>SelectionCount(Source))return false;
    const int32 Groups=FMath::Min(BatchSize,FMath::Max(0,Source.Groups.Num()-Offset));
    if(Groups)Batch.Groups.Append(Source.Groups.GetData()+Offset,Groups);
    const int32 BuildingOffset=FMath::Max(0,Offset-Source.Groups.Num());
    const int32 Buildings=FMath::Min(BatchSize-Groups,Source.Buildings.Num()-BuildingOffset);
    if(Buildings)Batch.Buildings.Append(Source.Buildings.GetData()+BuildingOffset,Buildings);
    return true;
}

bool FBSSelectionTransfer::Append(FBSSelectionPacket& Received,const FBSSelectionPacket& Packet)
{
    if(Packet.TotalGroups<0 || Packet.TotalGroups>2048 || Packet.TotalBuildings<0 || Packet.TotalBuildings>2048)return false;
    const int32 Count=SelectionCount(Packet.Items), Total=Packet.TotalGroups+Packet.TotalBuildings;
    if(!Packet.Request.IsValid() || Packet.Action>5 || Packet.Offset<0 || Packet.Offset>Total ||
        Packet.Message.Len()>2048 || Count>BatchSize || Count>Total-Packet.Offset ||
        (Count==0 && Total!=0) || Packet.Items.Groups.Num()!=FMath::Min(Count,FMath::Max(0,Packet.TotalGroups-Packet.Offset)))return false;
    if(!Received.Request.IsValid())
    {
        if(Packet.Offset!=0)return false;
        Received=Packet; Received.Items={};
    }
    if(Received.Request!=Packet.Request || Received.Action!=Packet.Action || Received.Revision!=Packet.Revision ||
        Received.TotalGroups!=Packet.TotalGroups || Received.TotalBuildings!=Packet.TotalBuildings ||
        Received.Success!=Packet.Success || Received.Message!=Packet.Message ||
        !(Received.Anchor==Packet.Anchor) || !Received.Anchor.Transform.Equals(Packet.Anchor.Transform) ||
        Packet.Offset!=SelectionCount(Received.Items))return false;
    Received.Items.Groups.Append(Packet.Items.Groups); Received.Items.Buildings.Append(Packet.Items.Buildings);
    return true;
}

void UBSGroupRemoteCallObject::RequestSelection(uint8 Action,const FBSSelection& Selection,FGuid Request,int32 Revision,const FBSBuildingRef& Anchor)
{
    SelectionUpload={}; SelectionResult={}; ExpectedSelectionResult.Invalidate();
    SelectionUpload.Request=Request; SelectionUpload.Action=Action; SelectionUpload.Revision=Revision;
    SelectionUpload.Anchor=Anchor; SelectionUpload.TotalGroups=Selection.Groups.Num();
    SelectionUpload.TotalBuildings=Selection.Buildings.Num(); SelectionUpload.Items=Selection;
    SelectionUploadNext=0; SelectionUploadDeadline=FPlatformTime::Seconds()+30;
    if(Action<3)ExpectedSelectionResult=Request;
    if(!Request.IsValid() || Action>4 || Selection.IsEmpty() || Selection.Groups.Num()>2048 || Selection.Buildings.Num()>2048)
    {
        Client_SelectionAck_Implementation(Request,0,TEXT("The selection is empty or exceeds the 2048-item limit.")); return;
    }
    SendSelectionPacket(false);
}

void UBSGroupRemoteCallObject::SendSelectionPacket(bool Result)
{
    const auto& Source=Result?SelectionReply:SelectionUpload;
    auto& Next=Result?SelectionReplyNext:SelectionUploadNext;
    if(!Source.Request.IsValid())return;
    FBSSelectionPacket Packet;
    Packet.Request=Source.Request; Packet.Action=Source.Action; Packet.Revision=Source.Revision;
    Packet.Anchor=Source.Anchor; Packet.TotalGroups=Source.TotalGroups; Packet.TotalBuildings=Source.TotalBuildings;
    Packet.Success=Source.Success; Packet.Message=Source.Message; Packet.Offset=Next;
    if(!FBSSelectionTransfer::Pack(Source.Items,Next,Packet.Items))return;
    Next+=SelectionCount(Packet.Items);
    // One outstanding bounded message, including NetGUID export overhead. Do not
    // enqueue the whole selection into the connection's reliable buffer at once.
    if(Result)Client_SelectionResultPacket(Packet);else Server_SelectionPacket(Packet);
}

void UBSGroupRemoteCallObject::Server_SelectionPacket_Implementation(const FBSSelectionPacket& Packet)
{
    const double Now=FPlatformTime::Seconds();
    if(!GetOwnerPlayerCharacter() || Packet.Action>4 || Packet.Request==CompletedSelectionUpload)return;
    if(Packet.Request!=SelectionAssembly.Request && Packet.Offset==0)SelectionAssembly={};
    else if(SelectionAssembly.Request.IsValid() && Now>SelectionAssemblyDeadline)
    {
        SelectionAssembly={}; Client_SelectionAck(Packet.Request,Packet.Offset,TEXT("Selection upload expired. Please retry.")); return;
    }
    if(!FBSSelectionTransfer::Append(SelectionAssembly,Packet))
    {
        SelectionAssembly={}; Client_SelectionAck(Packet.Request,Packet.Offset,TEXT("Selection upload was interrupted. Please retry.")); return;
    }
    SelectionAssemblyDeadline=Now+30;
    const int32 Next=SelectionCount(SelectionAssembly.Items);
    if(!FBSSelectionTransfer::Complete(SelectionAssembly)) {Client_SelectionAck(Packet.Request,Next,TEXT(""));return;}
    auto Complete=MoveTemp(SelectionAssembly); SelectionAssembly={};
    CompletedSelectionUpload=Complete.Request;
    Client_SelectionAck(Packet.Request,Next,TEXT(""));
    UE_LOG(LogBuildingSkills,Display,TEXT("Selection upload complete: action=%d groups=%d buildings=%d"),
        Complete.Action,Complete.TotalGroups,Complete.TotalBuildings);
    TGuardValue<FGuid> Dispatch(SelectionDispatch,Complete.Request);
    // Execute once, only after complete assembly. Existing authority, range,
    // revision and object-identity checks remain the final gate for every action.
    if(Complete.Action<3)Server_Apply_Implementation(EBSGroupOperation(Complete.Action),Complete.Items,Complete.Revision);
    else if(Complete.Action==3)Server_OpenBlueprintSave_Implementation(Complete.Items,Complete.Request,Complete.Anchor);
    else Server_CopySelection_Implementation(Complete.Items,Complete.Request,Complete.Anchor);
}

void UBSGroupRemoteCallObject::Client_SelectionAck_Implementation(FGuid Request,int32 Next,const FString& Error)
{
    if(Request!=SelectionUpload.Request || (Error.IsEmpty() && Next!=SelectionUploadNext))return;
    auto* Edit=SelectionEdit(this); const uint8 Action=SelectionUpload.Action;
    if(!Error.IsEmpty())
    {
        SelectionUpload={}; ExpectedSelectionResult.Invalidate();
        if(!Edit)return;
        if(Action<3)Edit->OnOperationResult(false,{},Error,0);
        else if(Action==3)Edit->OnBlueprintSaveReply(Request,Error);
        else Edit->OnCopyReady(Request,nullptr,Error);
        return;
    }
    if(!Edit || !Edit->IsEditing() || (Action<3?!Edit->bRequestPending:Action==3?!Edit->bOpeningBlueprint:!Edit->bCopyPending))
    {SelectionUpload={};return;}
    const double Now=FPlatformTime::Seconds(); SelectionUploadDeadline=Now+30;
    if(Action<3)Edit->GroupRequestDeadline=Now+30;
    else if(Action==3)Edit->BlueprintRequestDeadline=Now+30;
    else Edit->CopyDeadline=Now+180;
    if(Next==SelectionUpload.TotalGroups+SelectionUpload.TotalBuildings) {SelectionUpload={};return;}
    SendSelectionPacket(false);
}

void UBSGroupRemoteCallObject::SendGroupResult(bool Success,const FBSSelection& Selection,const FString& Message,int32 Revision)
{
    if(!SelectionDispatch.IsValid() && SelectionCount(Selection)<=FBSSelectionTransfer::BatchSize)
    {Client_Result(Success,Selection,Message,Revision);return;}
    SelectionReply={}; SelectionReply.Request=SelectionDispatch.IsValid()?SelectionDispatch:FGuid::NewGuid();
    SelectionReply.Action=5; SelectionReply.Success=Success; SelectionReply.Message=Message.Left(2048);
    SelectionReply.Revision=Revision; SelectionReply.Items=Selection;
    SelectionReply.TotalGroups=Selection.Groups.Num(); SelectionReply.TotalBuildings=Selection.Buildings.Num();
    SelectionReplyNext=0; SelectionReplyDeadline=FPlatformTime::Seconds()+30;
    SendSelectionPacket(true);
}

void UBSGroupRemoteCallObject::Client_SelectionResultPacket_Implementation(const FBSSelectionPacket& Packet)
{
    if(Packet.Action!=5 || Packet.Request==CompletedSelectionResult || !ExpectedSelectionResult.IsValid() || Packet.Request!=ExpectedSelectionResult)return;
    auto* Edit=SelectionEdit(this);
    if(!Edit || !Edit->bRequestPending)return;
    if(!FBSSelectionTransfer::Append(SelectionResult,Packet))
    {
        SelectionResult={}; ExpectedSelectionResult.Invalidate();
        Edit->OnOperationResult(false,{},TEXT("Group response was interrupted. Review the selection and retry."),0);return;
    }
    Edit->GroupRequestDeadline=FPlatformTime::Seconds()+30;
    const int32 Next=SelectionCount(SelectionResult.Items);
    if(!FBSSelectionTransfer::Complete(SelectionResult)) {Server_SelectionResultAck(Packet.Request,Next);return;}
    auto Complete=MoveTemp(SelectionResult); SelectionResult={}; ExpectedSelectionResult.Invalidate();
    CompletedSelectionResult=Complete.Request;
    Server_SelectionResultAck(Packet.Request,Next);
    Client_Result_Implementation(Complete.Success,Complete.Items,Complete.Message,Complete.Revision);
}

void UBSGroupRemoteCallObject::Server_SelectionResultAck_Implementation(FGuid Request,int32 Next)
{
    if(Request!=SelectionReply.Request || Next!=SelectionReplyNext)return;
    if(FPlatformTime::Seconds()>SelectionReplyDeadline || Next==SelectionReply.TotalGroups+SelectionReply.TotalBuildings)
    {SelectionReply={};return;}
    SelectionReplyDeadline=FPlatformTime::Seconds()+30; SendSelectionPacket(true);
}

void UBSGroupRemoteCallObject::TickSelectionTransfer()
{
    if(SelectionUpload.Request.IsValid() && FPlatformTime::Seconds()>=SelectionUploadDeadline)
        Client_SelectionAck_Implementation(SelectionUpload.Request,SelectionUploadNext,TEXT("Selection upload timed out. Please retry."));
    if(auto* Edit=SelectionEdit(this); !Edit || !Edit->bRequestPending)
    {ExpectedSelectionResult.Invalidate();SelectionResult={};}
}
