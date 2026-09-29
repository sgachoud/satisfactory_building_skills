#include "BSEditModeComponent.h"
#include "BSTransform.h"
#include "EnhancedInputSubsystems.h"
#include "Equipment/FGBuildGun.h"
#include "FGCharacterPlayer.h"
#include "FGInputLibrary.h"
#include "FGPlayerController.h"
#include "InputMappingContext.h"

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
    const auto Config = FBSTransformSettings::Get(GetWorld());
    const FVector Tiers[] = {{Config.Move, Config.Rotate, Config.Scale}, {Config.TinyMove, Config.TinyRotate, Config.TinyScale},
        {Config.SmallMove, Config.SmallRotate, Config.SmallScale}, {Config.LargeMove, Config.LargeRotate, Config.LargeScale}};
    FVector Steps = Tiers[TransformStepTier()];
    if (Steps.ContainsNaN()) return FVector(100, 15, 1.1);
    Steps.X = FMath::Clamp(Steps.X, 0.01, 10000.0);
    Steps.Y = FMath::Clamp(Steps.Y, 0.01, 180.0);
    Steps.Z = FMath::Clamp(Steps.Z, 1.001, 100.0);
    return Steps;
}
void UBSEditModeComponent::OnTransform()
{
    if (!IsEditing() || bTransforming || !InputAllowed() || bRequestPending) return;
    const FBSSelection Chosen = EffectiveSelection();
    const auto Members = ResolveSelection(Chosen);
    if (Members.IsEmpty() || Members.Num() > 2048) { Feedback = TEXT("Select between 1 and 2048 buildings first."); return; }
    Selection = Chosen;
    Hover = {};
    TransformMode = EBSTransformMode::RotateX;
    TransformPivot = FVector::ZeroVector;
    for (const auto& Ref : Members) TransformPivot += Ref.Location();
    TransformPivot /= Members.Num();
    bTransforming = true;
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
    if (!bTransforming) return;
    bTransforming = false;
    TransformCommands.Reset();
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
}
void UBSEditModeComponent::ScrollTransform(int32 Delta)
{
    if (!bTransforming || !InputAllowed() || !Delta) return;
    const auto Steps = TransformSteps();
    const int32 Count = FMath::Min(FMath::Abs(Delta), 16);
    for (int32 I = 0; I < Count && TransformCommands.Num() < 32; ++I)
        TransformCommands.Add({TransformMode, uint8(TransformMode) < 4 ? Steps.Y * FMath::Sign(Delta) :
            (Delta > 0 ? Steps.Z : 1.0 / Steps.Z), FVector::ZeroVector});
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
    // Do not accumulate held movement behind a slow network round trip.
    if (TransformCommands.Num() < 2)
        TransformCommands.Add({TransformMode, 0, FBSTransformMath::ViewTranslation(Player->GetController()->GetControlRotation().Yaw, Direction) * TransformSteps().X});
}
void UBSEditModeComponent::OnMoveReleased(int32 Direction) { if (Direction >= 0 && Direction < 6) NextMove[Direction] = 0; }
void UBSEditModeComponent::TickTransform()
{
    if (!bTransforming || !InputAllowed() || bRequestPending || TransformCommands.IsEmpty()) return;
    auto* Player = Cast<AFGCharacterPlayer>(GetOwner());
    auto* PC = Player ? Cast<AFGPlayerController>(Player->GetController()) : nullptr;
    auto* RCO = PC ? PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>() : nullptr;
    auto* Groups = ABSGroupSubsystem::Get(GetWorld());
    if (!RCO || !Groups) return;
    const auto Command = TransformCommands[0];
    TransformCommands.RemoveAt(0);
    bRequestPending = true;
    RCO->Server_Transform(Selection, Groups->Revision, Command.Mode, Command.Amount, Command.Translation);
}
void UBSEditModeComponent::OnTransformResult(bool Success, const FBSSelection& Result, const FString& Message, int32 Revision)
{
    bWaitingForTransformReplication = Success && IsEditing();
    OnOperationResult(Success, Result, Message, Revision);
    Feedback = Message;
    if (!Success) { TransformCommands.Reset(); return; }
    const auto Members = ResolveSelection(Result);
    TransformPivot = FVector::ZeroVector;
    for (const auto& Ref : Members) TransformPivot += Ref.Location();
    if (!Members.IsEmpty()) TransformPivot /= Members.Num();
}
