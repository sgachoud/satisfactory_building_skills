#include "BSGroups.h"
#include "Buildables/FGBuildable.h"
#include "Engine/World.h"

void UBSGroupRemoteCallObject::Server_BeginTransform_Implementation(FGuid Request, const TArray<FGuid>& Groups, FBSTransformOptions Options,
    EBSTransformMode Mode, double Amount, FVector Translation, int32 ExpectedCount, int32 DirectCount)
{
    IncomingRequest.Invalidate(); IncomingSelection = {}; IncomingMembers.Reset();
    if (!Request.IsValid() || !GetOwnerPlayerCharacter() || ExpectedCount < 1 || ExpectedCount > 2048 || DirectCount < 0 || DirectCount > 2048 || Groups.Num() + DirectCount > 2048)
    { SendTransformResult(false, {}, TEXT("Invalid transform request size."), 0); return; }
    IncomingRequest = Request; IncomingSelection.Groups = Groups; IncomingOptions = Options;
    IncomingMode = Mode; IncomingAmount = Amount; IncomingTranslation = Translation;
    IncomingExpectedCount = ExpectedCount; IncomingDirectCount = DirectCount;
    IncomingDeadline = GetWorld()->GetRealTimeSeconds() + 15;
}
void UBSGroupRemoteCallObject::Server_TransformChunk_Implementation(FGuid Request, const TArray<FBSBuildingRef>& Members, bool Direct, bool Final)
{
    if (!IncomingRequest.IsValid() || Request != IncomingRequest) return;
    auto& Destination = Direct ? IncomingSelection.Buildings : IncomingMembers;
    const int32 Limit = Direct ? IncomingDirectCount : IncomingExpectedCount;
    if (GetWorld()->GetRealTimeSeconds() > IncomingDeadline || Members.Num() > 48 || Destination.Num() + Members.Num() > Limit)
    {
        IncomingRequest.Invalidate(); IncomingSelection = {}; IncomingMembers.Reset();
        SendTransformResult(false, {}, TEXT("Incomplete or oversized transform request. Try again."), 0); return;
    }
    Destination.Append(Members);
    if (!Final) return;
    IncomingRequest.Invalidate();
    if (IncomingMembers.Num() != IncomingExpectedCount || IncomingSelection.Buildings.Num() != IncomingDirectCount)
        SendTransformResult(false, {}, TEXT("Incomplete transform request. Try again."), 0);
    else Server_TransformAdvanced_Implementation(IncomingSelection, IncomingMembers, IncomingOptions, IncomingMode, IncomingAmount, IncomingTranslation);
    IncomingSelection = {}; IncomingMembers.Reset();
}
void UBSGroupRemoteCallObject::SendTransformResult(bool Success, const FBSSelection& Selection, const FString& Message, int32 Revision)
{
    TArray<FBSBuildingRef> Targets = Selection.Buildings;
    if (auto* Groups = ABSGroupSubsystem::Get(GetWorld()); Success && Groups)
        for (const auto& Id : Selection.Groups) FBSGroupTree::Collect(Groups->Groups, Id, Targets);
    for (auto& Ref : Targets) if (Ref.Actor) Ref.Transform = Ref.Actor->GetActorTransform();
    // Keep both the expanded target list and persistent selection out of oversized RPCs.
    bool Reset = true;
    auto Send = [&](const TArray<FBSBuildingRef>& Values, bool Direct)
    {
        for (int32 Start = 0; Start < Values.Num(); Start += 48)
        {
            TArray<FBSBuildingRef> Batch;
            Batch.Append(Values.GetData() + Start, FMath::Min(48, Values.Num() - Start));
            Client_TransformMembers(Batch, Direct, Reset); Reset = false;
        }
    };
    Send(Targets, false); Send(Selection.Buildings, true);
    if (Reset) Client_TransformMembers({}, false, true);
    FBSSelection GroupSelection; GroupSelection.Groups = Selection.Groups;
    Client_TransformSettled(Success, GroupSelection, Message, Revision, {});
}
void UBSGroupRemoteCallObject::Client_TransformMembers_Implementation(const TArray<FBSBuildingRef>& Members, bool Direct, bool Reset)
{
    if (Reset) { ReceivedSelection = {}; ReceivedMembers.Reset(); }
    (Direct ? ReceivedSelection.Buildings : ReceivedMembers).Append(Members);
}
