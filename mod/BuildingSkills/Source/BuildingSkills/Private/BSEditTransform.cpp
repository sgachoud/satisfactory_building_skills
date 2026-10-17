#include "BSEditModeComponent.h"
#include "BSTransform.h"
#include "EnhancedInputSubsystems.h"
#include "Equipment/FGBuildGun.h"
#include "FGCharacterPlayer.h"
#include "FGInputLibrary.h"
#include "FGPlayerController.h"
#include "InputMappingContext.h"
#include "Buildables/FGBuildable.h"
#include "FGBlueprintProxy.h"

int32 UBSEditModeComponent::TransformStepTier() const
{
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
    if (!PC) return 0;
    const TCHAR* Names[] = {TEXT("BuildingSkills.Tiny"), TEXT("BuildingSkills.Small"), TEXT("BuildingSkills.Large")};
    for (int32 I = 0; I < 3; ++I)
    {
        FKey Key;
        TArray<FKey> Modifiers;
        UFGInputLibrary::GetCurrentMappingForAction(PC, FName(Names[I]), Key, Modifiers);
        bool Down = Key.IsValid() && PC->IsInputKeyDown(Key);
        for (const auto& Modifier : Modifiers) Down &= PC->IsInputKeyDown(Modifier);
        if (Down) return I + 1;
    }
    return 0;
}
FVector UBSEditModeComponent::TransformSteps() const
{
    return GetTransformStepValues()[TransformStepTier()];
}
TArray<FVector> UBSEditModeComponent::GetTransformStepValues() const
{
    if (TemporaryTransformSteps.Num() == 4) return TemporaryTransformSteps;
    const auto Config = FBSTransformSettings::Get(GetWorld());
    TArray<FVector> Tiers = {{Config.Move, Config.Rotate, Config.Scale}, {Config.TinyMove, Config.TinyRotate, Config.TinyScale},
        {Config.SmallMove, Config.SmallRotate, Config.SmallScale}, {Config.LargeMove, Config.LargeRotate, Config.LargeScale}};
    for (auto& Steps : Tiers)
    {
        if (Steps.ContainsNaN()) Steps = FVector(100, 15, 1.1);
        Steps.X = FMath::Clamp(Steps.X, 0.01, 10000.0);
        Steps.Y = FMath::Clamp(Steps.Y, 0.01, 180.0);
        Steps.Z = FMath::Clamp(Steps.Z, 1.001, 100.0);
    }
    return Tiers;
}
void UBSEditModeComponent::SetTransformStepValue(int32 Tier, int32 Axis, double Value)
{
    if (Tier < 0 || Tier >= 4 || Axis < 0 || Axis >= 3 || !FMath::IsFinite(Value)) return;
    if (TemporaryTransformSteps.IsEmpty()) TemporaryTransformSteps = GetTransformStepValues();
    const double Minimum[] = {.01, .01, 1.001}, Maximum[] = {10000, 180, 100};
    TemporaryTransformSteps[Tier][Axis] = FMath::Clamp(Value, Minimum[Axis], Maximum[Axis]);
    TransformOptions.Gesture.Invalidate();
}
void UBSEditModeComponent::OnTransform()
{
    if (!IsEditing() || bTransforming || !InputAllowed() || bRequestPending || bOpeningBlueprint || bCopyPending || BlueprintWindow) return;
    const FBSSelection Chosen = EffectiveSelection();
    const auto Members = ResolveSelection(Chosen);
    if (Members.IsEmpty() || Members.Num() > 2048) { Feedback = TEXT("Select between 1 and 2048 buildings first."); return; }
    Selection = Chosen;
    TransformOptions = {};
    TransformOptions.Pivot = EBSTransformPivot::Bounds;
    const auto Aimed = ResolveSelection(Hover);
    if (!Aimed.IsEmpty())
    {
        const int32 Index = Members.IndexOfByKey(Aimed[0]);
        if (Index != INDEX_NONE) TransformOptions.AnchorIndex = Index;
    }
    Hover = {};
    TransformMode = EBSTransformMode::RotateX;
    TransformPivot = FVector::ZeroVector;
    for (const auto& Ref : Members) TransformPivot += Ref.Location();
    TransformPivot /= Members.Num();
    bTransforming = true;
    RefreshTransformFrame();
    Feedback.Empty();
    bSelectionDirty = true;
    TransformCommands.Reset();
    for (auto& Time : NextMove) Time = 0;
    if (InputSubsystem.IsValid())
    {
        if (OperationsContext) InputSubsystem->RemoveMappingContext(OperationsContext);
        if (auto* Source = LoadObject<UInputMappingContext>(nullptr, TEXT("/BuildingSkills/Inputs/MC_Transform.MC_Transform")))
        {
            TransformContext = DuplicateObject<UInputMappingContext>(Source, this);
            InputSubsystem->AddMappingContext(TransformContext, 1001);
        }
    }
}
void UBSEditModeComponent::EndTransform()
{
    CloseTransformSettings();
    if (!bTransforming) return;
    bTransforming = false;
    TransformCommands.Reset();
    PreviewBoxes.Reset();
    InFlightTransform.Reset();
    TransformMembers.Reset();
    if (InputSubsystem.IsValid())
    {
        if (TransformContext) InputSubsystem->RemoveMappingContext(TransformContext);
        if (OperationsContext && IsEditing()) InputSubsystem->AddMappingContext(OperationsContext, 1000);
    }
    TransformContext = nullptr;
    Hover = {}; LastHover = {};
    HoverActor.Reset(); HoverMode.Reset(); HoverRevision = INDEX_NONE;
    bSelectionDirty = true;
    Feedback.Empty();
}
void UBSEditModeComponent::CycleTransform(int32 Delta)
{
    if (!bTransforming || !InputAllowed()) return;
    TransformMode = EBSTransformMode((int32(TransformMode) + Delta % 10 + 10) % 10);
    TransformOptions.Gesture.Invalidate();
}
void UBSEditModeComponent::ScrollTransform(int32 Delta)
{
    if (!bTransforming || !InputAllowed() || !Delta) return;
    const auto Steps = TransformSteps();
    const int32 Count = FMath::Min(FMath::Abs(Delta), 16);
    for (int32 I = 0; I < Count; ++I)
        QueueTransform(TransformMode, uint8(TransformMode) < 3 ? Steps.Y * FMath::Sign(Delta) : (Delta > 0 ? Steps.Z : 1.0 / Steps.Z), FVector::ZeroVector);
}
void UBSEditModeComponent::OnMove(int32 Direction)
{
    if (!bTransforming || !InputAllowed() || Direction < 0 || Direction >= 6) return;
    const double Now = GetWorld()->GetRealTimeSeconds();
    if (Now < NextMove[Direction]) return;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    if (!Player || !Player->GetController()) return;
    const auto Settings = FBSTransformSettings::Get(GetWorld());
    NextMove[Direction] = Now + FMath::Clamp(Settings.RepeatDelay, 0.05f, 2.0f);
    QueueTransform(TransformMode, 0, FBSTransformMath::FrameTranslation(GetTransformFrame(), Player->GetController()->GetControlRotation().Vector(), Direction) * TransformSteps().X);
}
void UBSEditModeComponent::OnMoveReleased(int32 Direction) { if (Direction >= 0 && Direction < 6) NextMove[Direction] = 0; }
void UBSEditModeComponent::TickTransform()
{
    if (!bTransforming || !InputAllowed() || bRequestPending || TransformCommands.IsEmpty() || GetWorld()->GetRealTimeSeconds() < NextTransformSend) return;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    auto* RCO = PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    if (!RCO || !Groups) return;
    auto Command = TransformCommands[0];
    Command.Options.UseFixedPivot = true;
    Command.Options.FixedPivot = TransformPivot;
    TransformCommands.RemoveAt(0);
    bRequestPending = true;
    InFlightTransform = Command;
    NextTransformSend = GetWorld()->GetRealTimeSeconds() + .075;
    const FGuid Request = FGuid::NewGuid();
    RCO->Server_BeginTransform(Request, Selection.Groups, Command.Options, Command.Mode, Command.Amount, Command.Translation, TransformMembers.Num(), Selection.Buildings.Num());
    auto Send = [&](const TArray<FBSBuildingRef>& Members, bool Direct)
    {
        for (int32 Start = 0; Start < Members.Num(); Start += 48)
        {
            TArray<FBSBuildingRef> Batch; Batch.Append(Members.GetData() + Start, FMath::Min(48, Members.Num() - Start));
            const bool Final = Start + Batch.Num() == Members.Num() && (Direct || Selection.Buildings.IsEmpty());
            RCO->Server_TransformChunk(Request, Batch, Direct, Final);
        }
    };
    Send(TransformMembers, false); Send(Selection.Buildings, true);
}
void UBSEditModeComponent::OnTransformResult(bool Success, const FBSSelection& Result, const FString& Message, int32 Revision)
{
    bWaitingForTransformReplication = Success && IsEditing();
    TransformReplicationDeadline = GetWorld()->GetRealTimeSeconds() + 10;
    OnOperationResult(Success, Result, Message, Revision);
    Feedback = Message;
    if (!Success) { TransformCommands.Reset(); InFlightTransform.Reset(); PreviewBoxes.Reset(); RefreshTransformFrame(); return; }
    // The frame and preview are rebased when native transforms have arrived.
}

FQuat UBSEditModeComponent::GetTransformFrame() const
{
    if (TransformOptions.Space == EBSTransformSpace::Anchor)
    {
        if (BlueprintAnchor.IsAlive(GetWorld()))
            return BlueprintAnchor.Actor ? BlueprintAnchor.Actor->GetActorQuat() : BlueprintAnchor.Transform.GetRotation();
        // A removed external anchor falls back to the reference's local frame.
    }
    if (TransformOptions.Space == EBSTransformSpace::World || !TransformMembers.IsValidIndex(TransformOptions.AnchorIndex)) return FQuat::Identity;
    return TransformMembers[TransformOptions.AnchorIndex].Transform.GetRotation();
}
void UBSEditModeComponent::RefreshTransformFrame(bool KeepPivot)
{
    const FVector PreviousPivot = TransformPivot;
    TransformMembers = ResolveSelection(Selection);
    TransformBounds.Reset();
    TransformPivot = FVector::ZeroVector;
    FBox Bounds(ForceInit);
    for (auto& Ref : TransformMembers)
    {
        if (Ref.Actor) Ref.Transform = Ref.Actor->GetActorTransform();
        else Ref.RuntimeIndex = Ref.ResolveIndex(GetWorld());
        if (const auto* Groups = ABSGroupSubsystem::Get(GetWorld()))
            Ref.TransformVersion = (Groups->HasAuthority() ? Groups->BuildingVersions : Groups->ReceivedVersions).FindRef(FBSTransformNative::Key(Ref));
        const auto Box = FBSTransformNative::Bounds(GetWorld(), Ref);
        TransformBounds.Add(Box);
        Bounds += Box;
        TransformPivot += Ref.Location();
    }
    if (!TransformMembers.IsEmpty()) TransformPivot /= TransformMembers.Num();
    TransformOptions.AnchorIndex = FMath::Clamp(TransformOptions.AnchorIndex, 0, FMath::Max(0, TransformMembers.Num() - 1));
    if (TransformOptions.Pivot == EBSTransformPivot::Bounds && Bounds.IsValid) TransformPivot = Bounds.GetCenter();
    if ((TransformOptions.Pivot == EBSTransformPivot::Anchor || TransformOptions.Pivot == EBSTransformPivot::Individual) && !TransformMembers.IsEmpty()) TransformPivot = TransformMembers[TransformOptions.AnchorIndex].Location();
    if (KeepPivot && (TransformOptions.Pivot == EBSTransformPivot::Origins || TransformOptions.Pivot == EBSTransformPivot::Bounds)) TransformPivot = PreviousPivot;
    RefreshTransformPreview();
}
void UBSEditModeComponent::OnPivot()
{
    if (!bTransforming || !InputAllowed() || bRequestPending || !TransformCommands.IsEmpty()) return;
    TransformOptions.Pivot = EBSTransformPivot((uint8(TransformOptions.Pivot) + 1) % 4);
    TransformOptions.Gesture.Invalidate(); RefreshTransformFrame();
}
void UBSEditModeComponent::OnAxisSpace()
{
    if (!bTransforming || !InputAllowed() || bRequestPending || !TransformCommands.IsEmpty()) return;
    const int32 Count = BlueprintAnchor.IsAlive(GetWorld()) ? 3 : 2;
    TransformOptions.Space = EBSTransformSpace((uint8(TransformOptions.Space) + 1) % Count);
    TransformOptions.Gesture.Invalidate(); RefreshTransformFrame();
}
void UBSEditModeComponent::OnAnchor()
{
    if (!bTransforming || !InputAllowed() || bRequestPending || !TransformCommands.IsEmpty() || TransformMembers.IsEmpty()) return;
    TransformOptions.AnchorIndex = (TransformOptions.AnchorIndex + 1) % TransformMembers.Num();
    TransformOptions.Gesture.Invalidate(); RefreshTransformFrame();
}
void UBSEditModeComponent::OnUndo() { RequestHistory(false); }
void UBSEditModeComponent::OnRedo() { RequestHistory(true); }
void UBSEditModeComponent::RequestHistory(bool Redo)
{
    if (!IsEditing() || !InputAllowed() || bRequestPending || bOpeningBlueprint || bCopyPending || BlueprintWindow) return;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    auto* RCO = PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    if (!RCO) return;
    TransformCommands.Reset(); PreviewBoxes.Reset(); TransformOptions.Gesture.Invalidate();
    bRequestPending = true;
    RCO->Server_TransformHistory(Redo);
}
void UBSEditModeComponent::QueueTransform(EBSTransformMode Mode, double Amount, FVector Translation)
{
    if (TransformOptions.Space == EBSTransformSpace::Anchor)
    {
        if (!BlueprintAnchor.IsAlive(GetWorld())) TransformOptions.Space = EBSTransformSpace::Local;
        else TransformOptions.AnchorFrame = GetTransformFrame();
    }
    const double Now = GetWorld()->GetRealTimeSeconds();
    if (!TransformOptions.Gesture.IsValid() || Now - LastTransformInput > .35) TransformOptions.Gesture = FGuid::NewGuid();
    LastTransformInput = Now;
    bool Combined = false;
    if (!TransformCommands.IsEmpty())
    {
        auto& Last = TransformCommands.Last();
        if (Last.Mode == Mode && Last.Options.Gesture == TransformOptions.Gesture && Last.Translation.IsNearlyZero() == Translation.IsNearlyZero())
        {
            bool Cancelled = false;
            if (FBSTransformMath::CombineSteps(Mode, Last.Amount, Last.Translation, Amount, Translation, Cancelled))
            {
                if (Cancelled) TransformCommands.Pop(EAllowShrinking::No);
                Combined = true;
            }
        }
    }
    if (!Combined)
    {
        if (TransformCommands.Num() >= 32) { Feedback = TEXT("Waiting for the server to finish these edits."); return; }
        TransformCommands.Add({Mode, Amount, Translation, TransformOptions});
        if (!bRequestPending && TransformCommands.Num() == 1) NextTransformSend = Now + .05;
    }
    RefreshTransformPreview();
}
void UBSEditModeComponent::RefreshTransformPreview()
{
    PreviewBoxes.Reset();
    if (!InFlightTransform.IsSet() && TransformCommands.IsEmpty()) return;
    TArray<FTransformCommand> Commands;
    if (InFlightTransform.IsSet()) Commands.Add(InFlightTransform.GetValue());
    Commands.Append(TransformCommands);
    FVector Pivot = TransformPivot;
    TArray<FTransform> Transforms;
    for (const auto& Ref : TransformMembers) Transforms.Add(Ref.Transform);
    for (const auto& C : Commands)
    {
        if (Transforms.IsEmpty()) break;
        const FQuat Anchor = Transforms[FMath::Clamp(C.Options.AnchorIndex, 0, Transforms.Num() - 1)].GetRotation();
        for (auto& T : Transforms)
        {
            const bool Individual = C.Options.Pivot == EBSTransformPivot::Individual;
            const FQuat Frame = C.Options.Space == EBSTransformSpace::Anchor ? C.Options.AnchorFrame : C.Options.Space == EBSTransformSpace::Local ? (Individual ? T.GetRotation() : Anchor) : FQuat::Identity;
            T = FBSTransformMath::ApplyInFrame(T, Individual ? T.GetLocation() : Pivot, Frame, C.Mode, C.Amount, C.Translation);
        }
        Pivot += C.Translation;
    }
    for (int32 I = 0; I < Transforms.Num(); ++I)
        if (TransformBounds.IsValidIndex(I)) PreviewBoxes.Add(TransformBounds[I].TransformBy(TransformMembers[I].Transform.Inverse()).TransformBy(Transforms[I]));
}

void UBSEditModeComponent::RefreshTransformReferences(const TArray<FBSNativeTransform>& Changes)
{
    auto Update=[&](FBSBuildingRef& Ref) {
        for (const auto& Change:Changes) if (FBSTransformNative::UpdateReference(Ref,Change)) return true;
        return false;
    };
    bool Changed=false;
    for (auto& Ref:Selection.Buildings) Changed |= Update(Ref);
    for (auto& Ref:Hover.Buildings) Changed |= Update(Ref);
    Changed |= Update(BlueprintAnchor);
    if (Changed || !Selection.Groups.IsEmpty() || !Hover.Groups.IsEmpty())
    {
        bSelectionDirty=true; HoverRevision=OutlinedRevision=INDEX_NONE;
    }
    // Keep the in-flight preview/frame until its normal result acknowledgement.
}

FString UBSEditModeComponent::TransformPoseText() const
{
    if (TransformMembers.IsEmpty()) return {};
    FTransform Pose;
    FString Label = TEXT("Object");
    if (TransformMembers.Num() == 1)
        Pose = TransformMembers[0].Actor ? TransformMembers[0].Actor->GetActorTransform() : TransformMembers[0].Transform;
    else
    {
        if (!Selection.Groups.IsEmpty()) return {};
        auto* Proxy = TransformMembers[0].Blueprint(GetWorld());
        if (!IsValid(Proxy) || !Proxy->AreProxyBuildingsRegisteredAndValid()) return {};
        int32 Count = Proxy->GetBuildables().Num();
        for (const auto& Bundle : Proxy->GetLightweightClassAndIndices()) Count += Bundle.Indices.Num();
        if (Count != TransformMembers.Num()) return {};
        for (const auto& Member : TransformMembers) if (Member.Blueprint(GetWorld()) != Proxy) return {};
        Pose = Proxy->GetActorTransform(); Label = TEXT("Blueprint");
    }
    const bool Relative = BlueprintAnchor.IsAlive(GetWorld());
    if (Relative)
    {
        const FTransform Anchor = BlueprintAnchor.Actor ? BlueprintAnchor.Actor->GetActorTransform() : BlueprintAnchor.Transform;
        Pose = Pose.GetRelativeTransform(Anchor);
    }
    const FVector P = Pose.GetLocation(), S = Pose.GetScale3D();
    const FRotator R = Pose.Rotator();
    return FString::Printf(TEXT("\n%s: %s\nPosition (cm): X %.2f  Y %.2f  Z %.2f\nRotation (deg): X %.2f  Y %.2f  Z %.2f\nScale: X %.4f  Y %.4f  Z %.4f"),
        *Label, Relative ? TEXT("relative to anchor") : TEXT("world"), P.X, P.Y, P.Z, R.Roll, R.Pitch, R.Yaw, S.X, S.Y, S.Z);
}
