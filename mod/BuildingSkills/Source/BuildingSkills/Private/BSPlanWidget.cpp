#include "BSPlan.h"
#include "BSStick.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Engine/LocalPlayer.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/SecureHash.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"

UBSPlanWidget::UBSPlanWidget(const FObjectInitializer& I):Super(I)
{
    mUseKeyboard=true; mUseMouse=true; mMouseLockMode=EMouseLockMode::DoNotLock; mHideCursorDuringCapture=false;
    mDisableBuildGunActions=true; mSupportsCaching=false; mSupportsStacking=true;
    SetDesiredHorizontalAlignment(HAlign_Center); SetDesiredVerticalAlignment(VAlign_Center); SetIsFocusable(true);
}
void UBSPlanWidget::OnEscapePressed_Implementation() { if (auto* P=Cast<UBSPlanComponent>(mInteractObject)) P->CloseSettings(); }
FReply UBSPlanWidget::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{ if (E.GetKey()==EKeys::Escape) { OnEscapePressed_Implementation(); return FReply::Handled(); } return Super::NativeOnPreviewKeyDown(G,E); }
FReply UBSPlanWidget::NativeOnKeyDown(const FGeometry&,const FKeyEvent&) { return FReply::Handled(); }
TSharedRef<SWidget> UBSPlanWidget::Section(FName Id,const TCHAR* Title,TSharedRef<SWidget> Body)
{
    FString Identity=TEXT("Offline/0");
    if (const auto* Local=GetOwningLocalPlayer())
    {
        const auto Account=Local->GetPreferredUniqueNetId();
        Identity=Account.IsValid() ? Account.ToString() : FString::Printf(TEXT("Offline/%d"),Local->GetPlatformUserId().GetInternalId());
    }
    const FString ConfigSection=TEXT("BuildingSkills.PlanMenu.")+FMD5::HashAnsiString(*Identity);
    // Runtime fixtures use their own local file, never a player's preferences.
    const FString File=FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsRuntimeTest"))
        ? FPaths::ProjectSavedDir()/TEXT("BuildingSkillsPlanTest.ini")
        : FPaths::ProjectSavedDir()/TEXT("Config/BuildingSkills/PlanMenu.ini");
    const FString Key=Id.ToString();
    bool Expanded=true;
    FConfigFile Preferences;
    Preferences.Read(File);
    Preferences.GetBool(*ConfigSection,*Key,Expanded);
    TSharedPtr<SExpandableArea> Area;
    SAssignNew(Area,SExpandableArea).AreaTitle(FText::FromString(Title))
        .AreaTitleFont(FCoreStyle::GetDefaultFontStyle("Bold",16))
        .BorderBackgroundColor(FLinearColor(.04,.055,.065,1))
        .HeaderPadding(FMargin(8)).Padding(FMargin(8)).InitiallyCollapsed(!Expanded).AllowAnimatedTransition(false)
        .OnAreaExpansionChanged_Lambda([ConfigSection,Key,File](bool Open) {
            // A dedicated local file also works before any preference exists;
            // the engine's global cache does not create unknown ini branches.
            FConfigFile Updated;
            Updated.Read(File);
            Updated.SetBool(*ConfigSection,*Key,Open);
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
            Updated.Write(File,false);
        }).BodyContent()[Body];
    Sections.Add(Id,Area);
    return Area.ToSharedRef();
}
TSharedRef<SWidget> UBSPlanWidget::RebuildWidget()
{
    auto* P=Cast<UBSPlanComponent>(mInteractObject);
    if (!P) return SNullWidget::NullWidget;
    Sections.Reset();
    SAssignNew(SavedPlanList,SVerticalBox);
    RebuildSavedPlans();
    auto List=SNew(SScrollBox)+SScrollBox::Slot()[SavedPlanList.ToSharedRef()];
    auto Buttons=SNew(SHorizontalBox);
    auto AddButton=[&](const TCHAR* Label,TFunction<void()> Action,TFunction<bool()> Enabled) {
        Buttons->AddSlot().AutoWidth().Padding(4)[SNew(SButton).Text(FText::FromString(Label)).IsEnabled_Lambda([this,Enabled]() { return !RefreshAfterAction && Enabled(); }).OnClicked_Lambda([Action]() { Action(); return FReply::Handled(); })];
    };
    AddButton(TEXT("Duplicate"),[this,P]() { FBSPlanCommand C; C.Action=EBSPlanAction::Duplicate; SubmitAction(C); },[P]() { return !P->Waiting && P->MenuCommands.IsEmpty() && P->Current(); });
    AddButton(TEXT("Make shared"),[this,P]() { FBSPlanCommand C; C.Action=EBSPlanAction::Share; SubmitAction(C); },[P]() { return !P->Waiting && P->MenuCommands.IsEmpty() && P->Current() && !P->Current()->Shared; });
    AddButton(TEXT("New plan"),[this,P]() { FBSPlanCommand C; C.Action=EBSPlanAction::New; C.Name=TEXT("New plan"); SubmitAction(C); },[P]() { return !P->Waiting && P->MenuCommands.IsEmpty(); });
    AddButton(TEXT("Undo"),[this,P]() { FBSPlanCommand C; C.Action=EBSPlanAction::Undo; SubmitAction(C); },[P]() { return !P->Waiting && P->Current(); });
    AddButton(TEXT("Redo"),[this,P]() { FBSPlanCommand C; C.Action=EBSPlanAction::Redo; SubmitAction(C); },[P]() { return !P->Waiting && P->Current(); });
    auto PlansBody=SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SBox).HeightOverride(120)[List]]
        +SVerticalBox::Slot().AutoHeight()[Buttons]
        +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Select a plan to load it. Double-click its name to rename it. Edits are retained when you close the gun and stored in world saves. Shared plans can be edited and built by everyone. Duplicates start as personal plans.")))];
    auto Root=SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",22)).Text(FText::FromString(TEXT("Rail plans")))]
        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([P]() { return P->Status(); })];
    Root->AddSlot().AutoHeight().Padding(0,6)[Section(TEXT("Plans"),TEXT("Saved plans"),PlansBody)];
    auto Items=SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("Click to select; Ctrl+click to add or remove items.")))]
        +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(160)[SAssignNew(ItemView,SListView<TSharedPtr<FBSPlanListItem>>)
            .ListItemsSource(&ListItems).SelectionMode(ESelectionMode::Multi)
            .OnGenerateRow_Lambda([](TSharedPtr<FBSPlanListItem> Item,const TSharedRef<STableViewBase>& Owner) {
                return SNew(STableRow<TSharedPtr<FBSPlanListItem>>,Owner).Padding(5)[SNew(STextBlock).Text(FText::FromString(Item->Label))];
            })
            .OnSelectionChanged_Lambda([this,P](TSharedPtr<FBSPlanListItem>,ESelectInfo::Type) {
                if (SyncingList || !ItemView) return;
                P->Selection.Reset(); for (const auto& Item:ItemView->GetSelectedItems()) P->Selection.Add(Item->Id);
                P->Refresh(); RebuildDetails();
            })]];
    Root->AddSlot().AutoHeight().Padding(0,6)[Section(TEXT("Items"),TEXT("Nodes and splines"),Items)];
    Root->AddSlot().AutoHeight()[SAssignNew(Details,SVerticalBox).IsEnabled_Lambda([this]() { return !RefreshAfterAction; })]; RebuildDetails(); RebuildList();
    auto Stick=SNew(SVerticalBox);
    const TCHAR* Labels[]={TEXT("Distance (m)"),TEXT("Scroll / movement step (m)"),TEXT("Slope (degrees)"),TEXT("Slope (%)"),TEXT("Horizontal radius (m)"),TEXT("Tilt step (degrees)")};
    auto SlopeRow=SNew(SHorizontalBox);
    for (int32 I=1;I<6;++I)
    {
        auto Field=SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(4)[SNew(STextBlock).Text(FText::FromString(Labels[I]))]
            +SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(SNumericEntryBox<double>).AllowSpin(false).MinDesiredValueWidth(100)
                .Value_Lambda([this,I]() -> TOptional<double> { const auto& S=GetWorld()->GetSubsystem<UBSStickSession>()->GetSettings(); return I==0 ? S.StickDistance : I==1 ? S.StickStep : I==2 ? S.StickSlope : I==3 ? FMath::Tan(FMath::DegreesToRadians(S.StickSlope))*100 : I==4 ? S.StickRadius : S.StickTiltStep; })
                .OnValueCommitted_Lambda([this,I](double V,ETextCommit::Type) { auto S=GetWorld()->GetSubsystem<UBSStickSession>()->GetSettings(); if (I==0) S.StickDistance=V; else if (I==1) S.StickStep=V; else if (I==2) S.StickSlope=V; else if (I==3) S.StickSlope=FMath::RadiansToDegrees(FMath::Atan(V/100)); else if (I==4) S.StickRadius=V; else S.StickTiltStep=V; if (S.Valid()) GetWorld()->GetSubsystem<UBSStickSession>()->GetSettings()=S; })];
        if (I==2 || I==3) { SlopeRow->AddSlot().FillWidth(1)[Field]; if (I==3) Stick->AddSlot().AutoHeight()[SlopeRow]; }
        else Stick->AddSlot().AutoHeight()[Field];
    }
    for (bool Vertical:{true,false}) Stick->AddSlot().AutoHeight()[SNew(SCheckBox)
        .IsChecked_Lambda([this,Vertical]() { const auto& S=GetWorld()->GetSubsystem<UBSStickSession>()->GetSettings(); return (Vertical ? S.VerticalConstraint : S.HorizontalConstraint) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this,Vertical](ECheckBoxState State) { auto& S=GetWorld()->GetSubsystem<UBSStickSession>()->GetSettings(); (Vertical ? S.VerticalConstraint : S.HorizontalConstraint)=State==ECheckBoxState::Checked; })
        [SNew(STextBlock).Text(FText::FromString(Vertical ? TEXT("Vertical constraint") : TEXT("Horizontal constraint")))]];
    Root->AddSlot().AutoHeight().Padding(0,6)[Section(TEXT("Stick"),TEXT("Stick and movement steps"),Stick)];
    Root->AddSlot().AutoHeight().Padding(0,12)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Close"))).OnClicked_Lambda([this]() { OnEscapePressed_Implementation(); return FReply::Handled(); })]
        +SHorizontalBox::Slot().FillWidth(1)
        +SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Build full plan"))).IsEnabled_Lambda([P]() { return !P->Waiting && P->Current() && !P->Current()->Edges.IsEmpty(); }).OnClicked_Lambda([this,P]() { FBSPlanCommand C; C.Action=EBSPlanAction::Build; SubmitAction(C); return FReply::Handled(); })]];
    return SNew(SBox).WidthOverride(850).MaxDesiredHeight(820)[SNew(SBorder).Padding(24).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025,.035,.04,.98))[SNew(SScrollBox)+SScrollBox::Slot()[Root]]];
}
void UBSPlanWidget::RebuildSavedPlans()
{
    auto* P=Cast<UBSPlanComponent>(mInteractObject); if (!P || !SavedPlanList) return;
    SavedPlanList->ClearChildren(); ListedSavedPlans.Reset();
    for (const auto& Plan:P->Plans)
    {
        const FGuid Id=Plan.Id; ListedSavedPlans.Add(Id);
        auto Select=[this,P,Id]() {
            // Commit any focused field before switching graphs. In multiplayer
            // wait for its acknowledgement, including queued auto-saved edits.
            FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Mouse);
            if (P->Waiting || !P->MenuCommands.IsEmpty()) PendingPlanSelection=Id;
            else { PendingPlanSelection.Invalidate(); if (P->CurrentId!=Id) P->Load(Id); }
        };
        TSharedPtr<SInlineEditableTextBlock> Label;
        SAssignNew(Label,SInlineEditableTextBlock)
            .MaximumLength(80).DelayedLeftClickEntersEditMode(false)
            .Text_Lambda([P,Id]() {
                const auto* Saved=P->Plans.FindByPredicate([Id](const auto& V) { return V.Id==Id; });
                return Saved ? FText::FromString(Saved->Name) : FText::GetEmpty();
            })
            .IsReadOnly_Lambda([P,Id]() { return P->Waiting || !P->MenuCommands.IsEmpty() || P->CurrentId!=Id; })
            .OnVerifyTextChanged_Lambda([](const FText& Text,FText& Error) {
                const auto Name=Text.ToString().TrimStartAndEnd();
                if (Name.IsEmpty() || Name.Len()>80) { Error=FText::FromString(TEXT("Use a name of 1–80 characters.")); return false; }
                return true;
            })
            .OnTextCommitted_Lambda([P,Id](const FText& Text,ETextCommit::Type Commit) {
                const FString Name=Text.ToString().TrimStartAndEnd();
                if (Commit!=ETextCommit::OnCleared && P->CurrentId==Id && P->Current() &&
                    !Name.IsEmpty() && Name.Len()<=80 && P->Current()->Name!=Name)
                { FBSPlanCommand C; C.Action=EBSPlanAction::Rename; C.Name=Name; P->QueueMenu(C); }
            });
        TWeakPtr<SInlineEditableTextBlock> WeakLabel=Label;
        SavedPlanList->AddSlot().AutoHeight().Padding(2)[SNew(SBorder).Padding(6)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor_Lambda([P,Id]() { return P->CurrentId==Id ? FLinearColor(.1,.35,.45) : FLinearColor(.1,.12,.15); })
            .OnMouseButtonDown_Lambda([Select](const FGeometry&,const FPointerEvent& Event) {
                if (Event.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
                Select(); return FReply::Handled();
            })
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(SBorder).Padding(0)
                    .BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
                    .OnMouseDoubleClick_Lambda([P,Id,Select,WeakLabel](const FGeometry&,const FPointerEvent& Event) {
                        if (Event.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
                        Select();
                        if (!P->Waiting && P->MenuCommands.IsEmpty() && P->CurrentId==Id)
                            if (const auto Text=WeakLabel.Pin()) Text->EnterEditingMode();
                        return FReply::Handled();
                    })[Label.ToSharedRef()]]
                +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(STextBlock).Text_Lambda([P,Id]() {
                    const auto* Saved=P->Plans.FindByPredicate([Id](const auto& V) { return V.Id==Id; });
                    return FText::FromString(Saved && Saved->Shared ? TEXT("[shared]") : TEXT("[personal]"));
                })]]];
    }
}
void UBSPlanWidget::RebuildDetails()
{
    auto* P=Cast<UBSPlanComponent>(mInteractObject); if (!P || !Details) return;
    DetailsRefreshPending=false;
    Details->ClearChildren(); Sections.Remove(TEXT("Selection")); Sections.Remove(TEXT("Parallels")); Sections.Remove(TEXT("Segments")); auto* Plan=P->Current(); if (!Plan) return;
    auto Body=SNew(SVerticalBox);
    auto Heading=[&](const TCHAR* Text) { Body->AddSlot().AutoHeight().Padding(0,12,0,4)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",16)).Text(FText::FromString(Text))]; };
    auto VectorRow=[](TFunction<FVector()> Get,TFunction<void(FVector)> Set) -> TSharedRef<SWidget> {
        auto Row=SNew(SHorizontalBox); const TCHAR* Names[]={TEXT("X"),TEXT("Y"),TEXT("Z")};
        for (int32 I=0;I<3;++I) Row->AddSlot().FillWidth(1).Padding(4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(STextBlock).Text(FText::FromString(Names[I]))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SNumericEntryBox<double>).AllowSpin(false).MinFractionalDigits(2).MaxFractionalDigits(2)
                .Value_Lambda([Get,I]() -> TOptional<double> { return Get()[I]/100; })
                .OnValueCommitted_Lambda([Get,Set,I](double V,ETextCommit::Type) { if (!FMath::IsFinite(V)) return; auto Value=Get(); Value[I]=FMath::RoundToDouble(V*100); Set(Value); })]];
        return Row;
    };
    if (P->Selection.Num()==1)
    {
        if (const auto* N=FBSPlanMath::Node(*Plan,P->Selection[0]))
        {
            Heading(TEXT("Node position — world meters")); NodePosition=N->Position; NodeRotation=FBSPlanMath::Rotation(*N); const FGuid Id=N->Id;
            Body->AddSlot().AutoHeight()[VectorRow([this]() { return NodePosition; },[this,Id](FVector V) { NodePosition=V; SaveNode(Id); })];
            Heading(TEXT("Node orientation - world degrees"));
            auto Angles=SNew(SHorizontalBox); const TCHAR* Names[]={TEXT("Yaw"),TEXT("Roll"),TEXT("Pitch")};
            for (int32 I=0;I<3;++I) Angles->AddSlot().FillWidth(1).Padding(4)[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Names[I]))]
                +SVerticalBox::Slot().AutoHeight()[SNew(SNumericEntryBox<double>).AllowSpin(false).MinFractionalDigits(2).MaxFractionalDigits(2)
                    .Value_Lambda([this,I]() -> TOptional<double> { return I==0 ? NodeRotation.Yaw : I==1 ? NodeRotation.Roll : NodeRotation.Pitch; })
                    .OnValueCommitted_Lambda([this,I,Id](double V,ETextCommit::Type) { if (FMath::IsFinite(V)) { (I==0 ? NodeRotation.Yaw : I==1 ? NodeRotation.Roll : NodeRotation.Pitch)=FRotator::NormalizeAxis(V); SaveNode(Id); } })]];
            Body->AddSlot().AutoHeight()[Angles];

        }
        else if (const auto* Edge=Plan->Edges.FindByPredicate([&](const auto& E) { return E.Id==P->Selection[0]; }))
        {
            Heading(TEXT("Spline trace")); EdgeDraft=*Edge;
            auto Menu=SNew(SVerticalBox);
            const TCHAR* Modes[]={TEXT("Curve"),TEXT("Straight"),TEXT("Free — through points"),TEXT("Free — Bezier handles")};
            for (int32 I=0;I<4;++I) Menu->AddSlot().AutoHeight()[SNew(SButton).Text(FText::FromString(Modes[I])).OnClicked_Lambda([this,P,I]() {
                FBSPlanCommand C; C.Action=EBSPlanAction::Edge; C.Edge=EdgeDraft; C.Edge.Trace=EBSPlanTrace(I);
                if (C.Edge.Trace!=EdgeDraft.Trace) C.Edge.ControlDirections.Reset();
                if (I==2 && C.Edge.Controls.IsEmpty() && P->Current())
                { const auto* A=FBSPlanMath::Node(*P->Current(),C.Edge.Start); const auto* B=FBSPlanMath::Node(*P->Current(),C.Edge.End); if (A && B) C.Edge.Controls.Add((A->Position+B->Position)*.5); }
                if (I==3 && P->Current())
                {
                    const auto Points=FBSPlanMath::Curve(*P->Current(),EdgeDraft);
                    if (Points.Num()>=2) C.Edge.Controls={Points[0].Location+Points[0].LeaveTangent/3,Points.Last().Location-Points.Last().ArriveTangent/3};
                }
                SubmitAction(C); return FReply::Handled();
            })];
            Body->AddSlot().AutoHeight()[SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text(FText::FromString(Modes[int32(EdgeDraft.Trace)]))].MenuContent()[Menu]];
            Body->AddSlot().AutoHeight().Padding(4)[SNew(SButton)
                .Text(FText::FromString(TEXT("Reverse spline direction")))
                .ToolTipText(FText::FromString(TEXT("Swap this spline's start and end without moving its nodes or changing its shape.")))
                .IsEnabled_Lambda([P]() { return !P->Waiting; })
                .OnClicked_Lambda([this,P]() {
                    FBSPlanCommand C; C.Action=EBSPlanAction::ReverseSpline; C.Selection={EdgeDraft.Id};
                    SubmitAction(C); return FReply::Handled();
                })];
            if (EdgeDraft.Trace==EBSPlanTrace::Free || EdgeDraft.Trace==EBSPlanTrace::Bezier)
            {
                Body->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(EdgeDraft.Trace==EBSPlanTrace::Bezier ? TEXT("Two Bezier handles shape the curve; it passes through the endpoint nodes. Moving a handle also aligns adjacent rail curves at its node.") : TEXT("Through-point controls lie on the curve. Tangents are calculated automatically.")))];
                for (int32 I=0;I<EdgeDraft.Controls.Num();++I) Body->AddSlot().AutoHeight()[VectorRow([this,I]() { return EdgeDraft.Controls[I]; },[this,I](FVector V) { EdgeDraft.Controls[I]=V; SaveEdge(); })];
                Body->AddSlot().AutoHeight()[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Add control"))).IsEnabled(EdgeDraft.Trace==EBSPlanTrace::Free).OnClicked_Lambda([this,P]() {
                        if (EdgeDraft.Controls.Num()<32 && P->Current()) if (const auto* N=FBSPlanMath::Node(*P->Current(),EdgeDraft.End)) { FBSPlanCommand C; C.Action=EBSPlanAction::Edge; C.Edge=EdgeDraft; C.Edge.Controls.Add((N->Position+(EdgeDraft.Controls.IsEmpty() ? FBSPlanMath::Node(*P->Current(),EdgeDraft.Start)->Position : EdgeDraft.Controls.Last()))*.5); SubmitAction(C); } return FReply::Handled(); })]
                    +SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Remove last control"))).IsEnabled(EdgeDraft.Trace==EBSPlanTrace::Free).OnClicked_Lambda([this,P]() { if (!EdgeDraft.Controls.IsEmpty()) { FBSPlanCommand C; C.Action=EBSPlanAction::Edge; C.Edge=EdgeDraft; C.Edge.Controls.Pop(); C.Edge.ControlDirections.SetNum(FMath::Min(C.Edge.ControlDirections.Num(),C.Edge.Controls.Num())); SubmitAction(C); } return FReply::Handled(); })]];
            }
        }
    }
    if (!P->Selection.IsEmpty()) Body->AddSlot().AutoHeight().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Remove selected plan items"))).OnClicked_Lambda([this,P]() { FBSPlanCommand C; C.Action=EBSPlanAction::Delete; C.Selection=P->Selection; SubmitAction(C); return FReply::Handled(); })];
    if (!P->Selection.IsEmpty()) Details->AddSlot().AutoHeight().Padding(0,6)[Section(TEXT("Selection"),TEXT("Selected items"),Body)];
    Body=SNew(SVerticalBox);
    Heading(TEXT("Parallel lines — local meters (X forward, Y right, Z up)")); ParallelDraft=Plan->Parallels; ParallelReversedDraft=Plan->ParallelReversed; ParallelReversedDraft.SetNum(ParallelDraft.Num());
    ParallelNodeOffsetsDraft=Plan->ParallelNodeOffsets; ParallelNodeOffsetsDraft.SetNumZeroed(ParallelDraft.Num());
    ParallelStartOffsetsDraft=Plan->ParallelStartOffsets; ParallelStartOffsetsDraft.SetNumZeroed(ParallelDraft.Num());
    for (int32 I=0;I<ParallelDraft.Num();++I)
    {
        Body->AddSlot().AutoHeight()[VectorRow([this,I]() { return ParallelDraft[I]; },[this,I](FVector V) { ParallelDraft[I]=V; SaveParallels(); })];
        for (bool Nodes:{true,false})
            Body->AddSlot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(Nodes ? TEXT("Node offset along local X (m)") : TEXT("Rail start placement offset (m)")))]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SNumericEntryBox<double>).AllowSpin(false)
                    .MinValue(-1000).MaxValue(1000).MinFractionalDigits(2).MaxFractionalDigits(2).MinDesiredValueWidth(120)
                    .Value_Lambda([this,I,Nodes]() -> TOptional<double> { return (Nodes ? ParallelNodeOffsetsDraft[I] : ParallelStartOffsetsDraft[I])/100; })
                    .OnValueCommitted_Lambda([this,I,Nodes](double V,ETextCommit::Type) {
                        if (!FMath::IsFinite(V)) return;
                        (Nodes ? ParallelNodeOffsetsDraft[I] : ParallelStartOffsetsDraft[I])=FMath::RoundToDouble(FMath::Clamp(V,-1000.0,1000.0)*100);
                        SaveParallels();
                    })]];
        Body->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Nodes stay linked to the main plan. Positive placement offset leaves a starting gap; negative extends backward along the starting tangent.")))];
        Body->AddSlot().AutoHeight().Padding(4)[SNew(SCheckBox).IsChecked_Lambda([this,I]() { return ParallelReversedDraft[I] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this,I](ECheckBoxState State) { ParallelReversedDraft[I]=State==ECheckBoxState::Checked; SaveParallels(); })[SNew(STextBlock).Text(FText::FromString(TEXT("Reverse")))]];
    }
    Body->AddSlot().AutoHeight()[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Add parallel"))).OnClicked_Lambda([this]() { ChangeParallelCount(true); return FReply::Handled(); })]
        +SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Remove last parallel"))).OnClicked_Lambda([this]() { ChangeParallelCount(false); return FReply::Handled(); })]];
    Details->AddSlot().AutoHeight().Padding(0,6)[Section(TEXT("Parallels"),TEXT("Parallel lines"),Body)];
    Body=SNew(SVerticalBox);
    const double Maximum=FBSPlanBuild::MaximumLength(),Minimum=FBSPlanBuild::MinimumLength();
    SegmentLengthDraft=Plan->SegmentLength==0 ? 8000 : FMath::Clamp(Plan->SegmentLength,Minimum,Maximum); RemainderDraft=Plan->Remainder;
    SyncParallelsInTurnsDraft=Plan->SyncParallelsInTurns;
    PlaceBlockSignalDraft=Plan->PlaceBlockSignal; SignalEveryDraft=Plan->SignalEvery; LeftSideSignalDraft=Plan->LeftSideSignal;
    Body->AddSlot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Segment length (m), %.2f to %.2f"),Minimum/100,Maximum/100))) ]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SNumericEntryBox<double>).AllowSpin(false).MinDesiredValueWidth(100).MinValue(Minimum/100).MaxValue(Maximum/100).MinFractionalDigits(2).MaxFractionalDigits(2)
            .Value_Lambda([this]() -> TOptional<double> { return SegmentLengthDraft/100; })
            .OnValueCommitted_Lambda([this,Minimum,Maximum](double V,ETextCommit::Type) { if (FMath::IsFinite(V)) { SegmentLengthDraft=FMath::Clamp(FMath::RoundToDouble(V*100),Minimum,Maximum); SaveSpecification(); } })]];
    auto PolicyName=[](EBSPlanRemainder Value) { return Value==EBSPlanRemainder::Last ? TEXT("Change only last") : Value==EBSPlanRemainder::Reduce ? TEXT("Reduce evenly") : TEXT("Increase evenly"); };
    auto Menu=SNew(SVerticalBox);
    for (int32 I=0;I<3;++I) Menu->AddSlot().AutoHeight()[SNew(SButton).Text(FText::FromString(PolicyName(EBSPlanRemainder(I))))
        .OnClicked_Lambda([this,I]() { RemainderDraft=EBSPlanRemainder(I); SaveSpecification(); return FReply::Handled(); })];
    Body->AddSlot().AutoHeight().Padding(4)[SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text_Lambda([this,PolicyName]() { return FText::FromString(PolicyName(RemainderDraft)); })].MenuContent()[Menu]];
    Body->AddSlot().AutoHeight().Padding(4)[SNew(SCheckBox)
        .IsChecked_Lambda([this]() { return SyncParallelsInTurnsDraft ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { SyncParallelsInTurnsDraft=State==ECheckBoxState::Checked; SaveSpecification(); })
        [SNew(STextBlock).Text(FText::FromString(TEXT("Sync parallels in turns")))]];
    Body->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Align rail joints using the main path, including each parallel's placement offset. Shared spacing is reduced if any parallel exceeds the rail length limit.")))];
    Body->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Length follows the whole connected path. Junctions and existing rail connections force segment endpoints. If the chosen policy cannot satisfy rail limits, nothing is built.")))];
    Body->AddSlot().AutoHeight().Padding(4)[SNew(SCheckBox).IsChecked_Lambda([this]() { return PlaceBlockSignalDraft ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { PlaceBlockSignalDraft=State==ECheckBoxState::Checked; SaveSpecification(); })[SNew(STextBlock).Text(FText::FromString(TEXT("Place block signal")))]];
    Body->AddSlot().AutoHeight().Padding(4)[SNew(SHorizontalBox).IsEnabled_Lambda([this]() { return PlaceBlockSignalDraft; })
        +SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("Every")))]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SNumericEntryBox<int32>).AllowSpin(false).MinDesiredValueWidth(100).MinValue(1).MaxValue(2048).Value_Lambda([this]() -> TOptional<int32> { return SignalEveryDraft; }).OnValueCommitted_Lambda([this](int32 V,ETextCommit::Type) { SignalEveryDraft=FMath::Clamp(V,1,2048); SaveSpecification(); })]
        +SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("segment(s)")))]];
    Body->AddSlot().AutoHeight().Padding(4)[SNew(SCheckBox).IsEnabled_Lambda([this]() { return PlaceBlockSignalDraft; }).IsChecked_Lambda([this]() { return LeftSideSignalDraft ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { LeftSideSignalDraft=State==ECheckBoxState::Checked; SaveSpecification(); })[SNew(STextBlock).Text(FText::FromString(TEXT("Left side signal")))]];
    Body->AddSlot().AutoHeight().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("Reverse direction"))).IsEnabled_Lambda([P]() { return !P->Waiting; })
        .OnClicked_Lambda([this,P]() { FBSPlanCommand C; C.Action=EBSPlanAction::ReverseDirection; C.Selection=P->Selection; SubmitAction(C); return FReply::Handled(); })];
    Body->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("Arrows show signal direction. Reverse affects selected spline paths, or all paths with no selection. Geometry stays unchanged. Parallel Reverse flips its signals relative to the main path.")))];
    Details->AddSlot().AutoHeight().Padding(0,6)[Section(TEXT("Segments"),TEXT("Build specification"),Body)];
}

void UBSPlanWidget::RebuildList()
{
    auto* P=Cast<UBSPlanComponent>(mInteractObject); if (!P || !ItemView) return;
    TGuardValue<bool> Sync(SyncingList,true);
    ItemView->ClearSelection(); ListItems.Reset();
    const auto* Plan=P->Current(); ListedPlan=P->CurrentId; ListedRevision=Plan ? Plan->Revision : -1;
    if (Plan)
    {
        auto Add=[&](FGuid Id,FString Label) { auto Item=MakeShared<FBSPlanListItem>(); Item->Id=Id; Item->Label=MoveTemp(Label); ListItems.Add(Item); if (P->Selection.Contains(Id)) ItemView->SetItemSelection(Item,true); };
        for (int32 I=0;I<Plan->Nodes.Num();++I)
        {
            const auto& N=Plan->Nodes[I];
            Add(N.Id,FString::Printf(TEXT("Node %d   |   X %.2f   Y %.2f   Z %.2f m"),I+1,N.Position.X/100,N.Position.Y/100,N.Position.Z/100));
        }
        for (int32 I=0;I<Plan->Edges.Num();++I)
        {
            const auto& E=Plan->Edges[I];
            const int32 A=Plan->Nodes.IndexOfByPredicate([&](const auto& N) { return N.Id==E.Start; })+1;
            const int32 B=Plan->Nodes.IndexOfByPredicate([&](const auto& N) { return N.Id==E.End; })+1;
            Add(E.Id,FString::Printf(TEXT("Spline %d   |   Node %d - Node %d"),I+1,A,B));
        }
    }
    ItemView->RequestListRefresh();
}
void UBSPlanWidget::NativeTick(const FGeometry& G,float Delta)
{
    Super::NativeTick(G,Delta);
    auto* P=Cast<UBSPlanComponent>(mInteractObject);
    if (P)
    {
        if (PendingPlanSelection.IsValid() && !P->Waiting && P->MenuCommands.IsEmpty())
        {
            if (P->Plans.ContainsByPredicate([this](const auto& V) { return V.Id==PendingPlanSelection; })) P->Load(PendingPlanSelection);
            PendingPlanSelection.Invalidate();
        }
        TArray<FGuid> Ids; for (const auto& Saved:P->Plans) Ids.Add(Saved.Id);
        if (Ids!=ListedSavedPlans) RebuildSavedPlans();
    }
    const auto* Plan=P ? P->Current() : nullptr;
    if (P && (ListedPlan!=P->CurrentId || ListedRevision!=(Plan ? Plan->Revision : -1)))
    {
        const bool ChangedPlan=ListedPlan!=P->CurrentId;
        RebuildList();
        DetailsRefreshPending=true;
        if (ChangedPlan || !GetCachedWidget().IsValid() || !GetCachedWidget()->HasFocusedDescendants()) RebuildDetails();
    }
    if (RefreshAfterAction && P && !P->Waiting && P->MenuCommands.IsEmpty())
    {
        // Structural actions can leave a button focused. Refresh after the
        // authoritative response, outside the click callback, without closing
        // the interact window or rebuilding controls while an RPC is pending.
        RefreshAfterAction=false;
        FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
        RebuildDetails();
    }
    else if (DetailsRefreshPending && P && !P->Waiting && (!GetCachedWidget().IsValid() || !GetCachedWidget()->HasFocusedDescendants())) RebuildDetails();
}

void UBSPlanWidget::SaveNode(FGuid Id)
{
    auto* P=Cast<UBSPlanComponent>(mInteractObject);
    if (P && P->Current()) if (const auto* N=FBSPlanMath::Node(*P->Current(),Id))
    { FBSPlanCommand C; C.Action=EBSPlanAction::Node; C.Node=*N; C.Node.Position=NodePosition; C.Node.Direction=NodeRotation.Vector(); C.Node.Roll=NodeRotation.Roll; P->QueueMenu(C); }
}
void UBSPlanWidget::SaveEdge()
{ if (auto* P=Cast<UBSPlanComponent>(mInteractObject)) { FBSPlanCommand C; C.Action=EBSPlanAction::Edge; C.Edge=EdgeDraft; P->QueueMenu(C); } }
void UBSPlanWidget::SaveParallels()
{ if (auto* P=Cast<UBSPlanComponent>(mInteractObject)) { FBSPlanCommand C; C.Action=EBSPlanAction::Parallels; C.Parallels=ParallelDraft; C.ParallelReversed=ParallelReversedDraft; C.ParallelNodeOffsets=ParallelNodeOffsetsDraft; C.ParallelStartOffsets=ParallelStartOffsetsDraft; P->QueueMenu(C); } }
void UBSPlanWidget::SaveSpecification()
{ if (auto* P=Cast<UBSPlanComponent>(mInteractObject)) { FBSPlanCommand C; C.Action=EBSPlanAction::Segments; C.SegmentLength=SegmentLengthDraft; C.Remainder=RemainderDraft; C.SyncParallelsInTurns=SyncParallelsInTurnsDraft; C.PlaceBlockSignal=PlaceBlockSignalDraft; C.SignalEvery=SignalEveryDraft; C.LeftSideSignal=LeftSideSignalDraft; P->QueueMenu(C); } }

void UBSPlanWidget::SubmitAction(FBSPlanCommand Command)
{
    auto* P=Cast<UBSPlanComponent>(mInteractObject);
    if (!P || RefreshAfterAction) return;
    FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Mouse);
    FSlateApplication::Get().DismissAllMenus();
    RefreshAfterAction=true;
    P->QueueMenu(Command);
}
void UBSPlanWidget::ChangeParallelCount(bool Add)
{
    if (RefreshAfterAction || (Add ? ParallelDraft.Num()>=16 : ParallelDraft.IsEmpty())) return;
    FBSPlanCommand C; C.Action=EBSPlanAction::Parallels;
    C.Parallels=ParallelDraft; C.ParallelReversed=ParallelReversedDraft;
    C.ParallelNodeOffsets=ParallelNodeOffsetsDraft; C.ParallelStartOffsets=ParallelStartOffsetsDraft;
    if (Add)
    {
        C.Parallels.Add(FVector(0,600*(C.Parallels.Num()+1),0));
        C.ParallelReversed.Add(false); C.ParallelNodeOffsets.Add(0); C.ParallelStartOffsets.Add(0);
    }
    else
    {
        C.Parallels.Pop(); C.ParallelReversed.SetNum(C.Parallels.Num());
        C.ParallelNodeOffsets.SetNum(C.Parallels.Num()); C.ParallelStartOffsets.SetNum(C.Parallels.Num());
    }
    // Keep old row backing arrays intact until the acknowledged graph is
    // rebuilt. Existing Slate value bindings can still run while awaiting RPC.
    SubmitAction(C);
}
