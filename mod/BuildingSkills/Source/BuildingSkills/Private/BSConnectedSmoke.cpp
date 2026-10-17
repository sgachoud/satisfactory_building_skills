#include "BSConnected.h"
#include "BSEditModeComponent.h"
#include "BSEditModeWidget.h"
#include "BSConnectedSettingsWidget.h"
#include "BSTransformSettingsWidget.h"
#include "BuildingSkills.h"
#include "Buildables/FGBuildable.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "Equipment/FGBuildGunPaint.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGBuildableSubsystem.h"
#include "FGRecipe.h"
#include "FGCustomizationRecipe.h"
#include "FGRecipeManager.h"
#include "FGInventoryComponent.h"
#include "FGGameState.h"
#include "FGOutlineComponent.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"
#include "InputKeyEventArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Components/BackgroundBlur.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "TimerManager.h"

// Opt-in fresh-world fixture, called only by the runtime test ticker.
class FBSConnectedSmoke
{
public:
    static bool Run(AFGCharacterPlayer* Player, int32 Phase)
    {
        static TArray<FBSBuildingRef> Refs;
        static TSubclassOf<UFGCustomizationRecipe> Swatch, Material;
        static TSubclassOf<UFGRecipe> BuildRecipe;
        static FString ExternalKConflict;
        static TWeakObjectPtr<AActor> MaterialPreview;
        static bool SharedEditSettingsOpened=false;
        auto* World = Player->GetWorld();
        auto* Edit = Player->FindComponentByClass<UBSEditModeComponent>();
        auto* PC = Cast<AFGPlayerController>(Player->GetController());
        auto* Input = Cast<UEnhancedPlayerInput>(PC->PlayerInput);
        auto* Gun = Player->GetBuildGun();
        auto* LW = AFGLightweightBuildableSubsystem::Get(World);
        bool Passed = true;
        auto Check = [&](const TCHAR* Name, bool Value) { UE_LOG(LogBuildingSkills, Display, TEXT("RUNTIME %s: Connected: %s"), Value ? TEXT("PASS") : TEXT("FAIL"), Name); Passed &= Value; };
        auto Key = [&](FKey K, bool Down)
        {
            Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,K,Down?IE_Pressed:IE_Released,Down?1.f:0.f,false,0));
            // Existing cycling checks are taps; deliberate radial holds below use Slate.
            if (K == EKeys::R && Down)
            {
                FTimerHandle Timer;
                World->GetTimerManager().SetTimer(Timer,[Input]() { Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::R,IE_Released,0.f,false,0)); },.05f,false);
            }
        };
        // Route interoperability keys through Slate, as a physical keyboard does.
        // Injecting directly into PlayerInput would hide stale modal focus bugs.
        auto WindowKey = [&](FKey K, bool Down)
        {
            auto& App = FSlateApplication::Get();
            const auto Focus = App.GetKeyboardFocusedWidget();
            UE_LOG(LogBuildingSkills,Display,TEXT("CONNECTED FOCUS: phase=%d key=%s down=%d widget=%s cursor=%d lookIgnored=%d"),
                Phase,*K.ToString(),Down,Focus?*Focus->GetTypeAsString():TEXT("none"),PC->bShowMouseCursor,PC->IsLookInputIgnored());
            const FKeyEvent Event(K,FModifierKeysState(),0,false,0,0);
            if (Down) App.ProcessKeyDownEvent(Event); else App.ProcessKeyUpEvent(Event);
        };
        auto Search = [&](FBSConnectedSettings S) { FString Message; return Refs.IsEmpty() ? TArray<FBSBuildingRef>() : FBSConnected::Find(World, Refs[0], S, Message); };
        auto BuildingSkillsKCount = [&]()
        {
            int32 Count = 0;
            for (const auto& Mapping : Input->GetEnhancedActionMappings())
                if (Mapping.Key == EKeys::K && Mapping.Action && Mapping.Action->GetPathName().StartsWith(TEXT("/BuildingSkills/"))) ++Count;
            return Count;
        };
        auto FindExternalKConflict = [&]()
        {
            FString Owner;
            for (const auto& Mapping : Input->GetEnhancedActionMappings())
                if (Mapping.Key==EKeys::K && Mapping.Action && Mapping.Action->bConsumeInput &&
                    !Mapping.Action->GetPathName().StartsWith(TEXT("/BuildingSkills/")) &&
                    !Mapping.Action->GetPathName().StartsWith(TEXT("/SmartFoundations/")))
                    Owner += Mapping.Action->GetPathName()+TEXT(" (")+Mapping.GetMappingName().ToString()+TEXT(") ");
            return Owner;
        };
        auto ViewportFocusRestored = [&]()
        {
            const auto Focus=FSlateApplication::Get().GetKeyboardFocusedWidget();
            return Focus && Focus->GetType()==TEXT("SViewport") && !PC->bShowMouseCursor && !PC->IsLookInputIgnored();
        };
        auto HintsAttached = [&]()
        {
            if (!Edit->Indicator || !Edit->ControlHints || Edit->LastControlHints.IsEmpty() || !Edit->Indicator->IsInViewport()) return false;
            const auto Expected = Edit->ControlHints->TakeWidget();
            TFunction<bool(TSharedRef<SWidget>)> Contains = [&](TSharedRef<SWidget> Widget)
            {
                if (Widget == Expected) return true;
                auto* Children = Widget->GetChildren();
                for (int32 I = 0; I < Children->Num(); ++I) if (Contains(Children->GetChildAt(I))) return true;
                return false;
            };
            return Contains(Edit->Indicator->TakeWidget());
        };
        auto HasHint = [&](const TCHAR* Name)
        { return Edit->GetControlHints().ContainsByPredicate([&](const auto& Hint) { return Hint.Key.ToString() == Name; }); };
        auto NativeModeLabel = [&]() -> FString
        {
            auto* UI=PC->GetGameUI();
            if (!UI) return FString();
            TOptional<FString> Label;
            TSet<UUserWidget*> Visited;
            TFunction<void(UUserWidget*)> Find = [&](UUserWidget* Widget)
            {
                if (!Widget || !Widget->WidgetTree || !Widget->IsVisible() || Label.IsSet() || Visited.Contains(Widget)) return;
                Visited.Add(Widget);
                if (Widget->GetClass()->GetName()==TEXT("Widget_BuildMode_C"))
                    if (auto* Container=Widget->WidgetTree->FindWidget(TEXT("mBuildModeContainer")); Container && Container->IsVisible())
                        if (auto* Text=Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("mBuildModeText"))); Text && Text->IsVisible())
                            Label=Text->GetText().ToString();
                if (!Label.IsSet()) Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
                { if (auto* Nested=Cast<UUserWidget>(Child)) Find(Nested); });
            };
            // Only active native UI roots, never cached or detached previous gun widgets.
            const auto Stack=UI->GetInteractWidgetStack();
            for (int32 I=Stack.Num()-1; I>=0 && !Label.IsSet(); --I) Find(Stack[I]);
            return Label.Get(FString());
        };
        auto DumpHints = [&]()
        {
            auto DumpWidget = [&](UWidget* Widget)
            {
                if (!Widget) return;
                auto* Panel=Cast<UPanelWidget>(Widget);
                auto* Text=Cast<UTextBlock>(Widget);
                UE_LOG(LogBuildingSkills,Display,TEXT("CONNECTED HINT WIDGET: %s visibility=%d position=%s size=%s children=%d text=%s"),
                    *Widget->GetPathName(),int32(Widget->GetVisibility()),*Widget->GetCachedGeometry().GetAbsolutePosition().ToString(),
                    *Widget->GetCachedGeometry().GetLocalSize().ToString(),Panel?Panel->GetChildrenCount():-1,Text?*Text->GetText().ToString():TEXT(""));
            };
            DumpWidget(Edit->ControlHints);
            if (Edit->ControlHints && Edit->ControlHints->WidgetTree)
                Edit->ControlHints->WidgetTree->ForEachWidget(DumpWidget);
            UE_LOG(LogBuildingSkills,Display,TEXT("CONNECTED NATIVE MODE: %s"),*NativeModeLabel());
        };
        // Inspect the installed mod's public runtime widget identity only. No dependency
        // on, or copied implementation from, the other mod is required for this check.
        auto SmartMenu = [&]() -> UUserWidget*
        {
            for (TObjectIterator<UUserWidget> It; It; ++It)
                if (It->GetWorld()==World && It->IsInViewport() && It->IsVisible() && It->GetClass()->GetName()==TEXT("Smart_SettingsForm_Widget_C")) return *It;
            return nullptr;
        };
        auto EscapeMenu = [&]()
        {
            auto& App = FSlateApplication::Get();
            App.ProcessKeyDownEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
            App.ProcessKeyUpEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
        };
        auto DumpInput = [&]()
        {
            UE_LOG(LogBuildingSkills,Display,TEXT("CONNECTED INPUT: phase=%d build=%d paint=%d edit=%d transform=%d equipped=%d cursor=%d inputAllowed=%d"),
                Phase,Gun->IsInState(EBuildGunState::BGS_BUILD),Edit->IsPainting(),Edit->IsEditing(),Edit->IsTransforming(),Player->IsBuildGunEquipped(),PC->bShowMouseCursor,Edit->InputAllowed());
            for (const auto& Context : Input->GetAppliedInputContextData())
                UE_LOG(LogBuildingSkills,Display,TEXT("CONNECTED CONTEXT: %s priority=%d"),*GetPathNameSafe(Context.Key),Context.Value.Priority);
            for (const auto& Mapping : Input->GetEnhancedActionMappings())
                if (Mapping.Key == EKeys::K && Mapping.Action)
                {
                    const auto* Instance = Input->FindActionInstanceData(Mapping.Action);
                    UE_LOG(LogBuildingSkills,Display,TEXT("CONNECTED K: %s name=%s consume=%d event=%d"),*Mapping.Action->GetPathName(),*Mapping.GetMappingName().ToString(),Mapping.Action->bConsumeInput,Instance?int32(Instance->GetTriggerEvent()):-1);
                }
            if (auto* UI = PC->GetGameUI())
                for (auto* Widget : UI->GetInteractWidgetStack())
                    if (Widget) UE_LOG(LogBuildingSkills,Display,TEXT("CONNECTED MODAL: %s visible=%d"),*Widget->GetClass()->GetPathName(),Widget->IsVisible());
        };
        if (Phase == 0)
        {
            Check(TEXT("shared Connected default object limit is 64"),FBSConnectedSettings().Limit==64 && Edit->ConnectedSettings.Limit==64);
            Key(EKeys::E,false); Key(EKeys::LeftAlt,false);
            auto* Class = LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x4_01.Build_Foundation_8x4_01_C"));
            auto* Recipe = LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Foundations/Recipe_Foundation_8x4_01.Recipe_Foundation_8x4_01_C"));
            BuildRecipe = Recipe;
            Swatch = LoadClass<UFGCustomizationRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/-Shared/Customization/Swatches/Recipe_Swatch_Slot1.Recipe_Swatch_Slot1_C"));
            Material = LoadClass<UFGCustomizationRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/-Shared/Customization/Materials/Recipe_Material_Foundation_Concrete.Recipe_Material_Foundation_Concrete_C"));
            Check(TEXT("native fixture assets loaded"),Class && Recipe && Swatch && Material && LW);
            if (!Class || !Recipe || !LW || !Swatch || !Material) return false;
            AFGRecipeManager::Get(World)->AddAvailableRecipe(Swatch); AFGRecipeManager::Get(World)->AddAvailableRecipe(Material);
            AFGRecipeManager::Get(World)->AddAvailableRecipe(BuildRecipe);
            const FVector Base = Player->GetActorLocation() + FVector(2500,0,0);
            const double X[] = {0,800,1650,6000};
            for (int32 I=0; I<4; ++I)
            {
                FRuntimeBuildableInstanceData Data;
                Data.Transform = FTransform(FRotator::ZeroRotator,Base+FVector(X[I],0,0)); Data.BuiltWithRecipe=Recipe;
                if (I==1) Data.Transform.SetScale3D(FVector(1,1,1.5));
                Data.CustomizationData.SwatchDesc=Class->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(World);
                const int32 Index=LW->AddFromBuildableInstanceData(Class,Data);
                Refs.Add(FBSBuildingRef::FromLightweight(World,Class,Index));
            }
            FBSConnectedSettings S;
            Check(TEXT("two touching native foundations found"),Search(S).Num()==2);
            S.Gap=.5; Check(TEXT("half-meter gap reaches third foundation but not remote fourth"),Search(S).Num()==3);
            S.Limit=1; Check(TEXT("native result honors object cap"),Search(S).Num()==1);
            S.Limit=512; S.Blacklist=TEXT(".*"); Check(TEXT("blacklist excludes seed and all traversal"),Search(S).IsEmpty());
            S.Blacklist.Reset(); S.Scope=EBSConnectedScope::Category; Check(TEXT("native category metadata matches"),Search(S).Num()==3);
            S.Scope=EBSConnectedScope::Subcategory; Check(TEXT("native subcategory metadata matches"),Search(S).Num()==3);
            {
                FString Message;
                FBSConnectedSettings Local;
                FBSConnectedSearchStats Before, After;
                const auto Baseline=FBSConnected::Find(World,Refs[0],Local,Message,&Before);
                TArray<int32> Remote;
                for (int32 I=0; I<128; ++I)
                {
                    FRuntimeBuildableInstanceData Data;
                    Data.Transform=FTransform(Base+FVector(1000000+I*800,0,0)); Data.BuiltWithRecipe=Recipe;
                    Remote.Add(LW->AddFromBuildableInstanceData(Class,Data));
                }
                Check(TEXT("remote factory growth leaves local search and candidate work unchanged"),
                    FBSConnected::Find(World,Refs[0],Local,Message,&After)==Baseline && Before.CandidatesExamined==After.CandidatesExamined && After.SpatialQueries==2);
                UE_LOG(LogBuildingSkills,Display,TEXT("CONNECTED SEARCH: remoteFixtures=128 beforeCandidates=%d afterCandidates=%d queries=%d"),Before.CandidatesExamined,After.CandidatesExamined,After.SpatialQueries);
                for (int32 Index : Remote) LW->RemoveByInstanceIndex(Class,Index);
                FBSNativeTransform Move; Move.Building=Refs[1]; Move.InstanceIndex=Refs[1].ResolveIndex(World); Move.Target=Refs[1].Transform;
                Move.Target.AddToTranslation(FVector(0,1000000,0));
                Check(TEXT("spatial search drops a lightweight moved into another grid cell"),FBSTransformNative::Apply(World,Move) && Search(Local).Num()==1);
                Move.Building=FBSBuildingRef::FromLightweight(World,Class,Move.InstanceIndex); Move.Target=Refs[1].Transform;
                Check(TEXT("spatial search finds the lightweight after it moves back"),FBSTransformNative::Apply(World,Move) && Search(Local).Num()==2);
                auto* Buildables=AFGBuildableSubsystem::Get(World);
                auto* MachineClass=LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/ConstructorMk1/Build_ConstructorMk1.Build_ConstructorMk1_C"));
                const FTransform Pose(Base+FVector(0,100000,0));
                auto* A=Buildables->BeginSpawnBuildable(MachineClass,Pose);
                auto* B=Buildables->BeginSpawnBuildable(MachineClass,Pose);
                if (A) A->FinishSpawning(Pose); if (B) B->FinishSpawning(Pose);
                Check(TEXT("actor spatial index finds touching constructors without recipes"),A && B && FBSConnected::Find(World,FBSBuildingRef::FromActor(A),Local,Message).Num()==2);
                if (A && B)
                {
                    auto Shift=FBSTransformNative::Capture(B); Shift.Target.AddToTranslation(FVector(10000,0,0));
                    Check(TEXT("actor spatial index excludes moved-away constructor"),FBSTransformNative::Apply(World,Shift) && FBSConnected::Find(World,FBSBuildingRef::FromActor(A),Local,Message).Num()==1);
                }
                if (A) A->Destroy(); if (B) B->Destroy();
            }
            Gun->SetCurrentBuildGunMode(UBSEditConnectedModeDescriptor::StaticClass());
        }
        if (Refs.Num()!=4) return false;
        if (Phase == 1)
        {
            Check(TEXT("Edit supports Connected mode"),Edit->IsEditing() && Edit->IsConnectedSelection());
            if (auto* Groups=ABSGroupSubsystem::Get(World))
            {
                FBSSelection Singles, Grouped, Result; FString Error;
                Singles.Buildings={Refs[0],Refs[1]};
                const int32 Stale=Groups->Revision-1;
                const bool GroupedOK=Groups->Execute(Player,EBSGroupOperation::Group,Singles,Stale,Grouped,Error);
                Check(TEXT("server groups explicit individual buildings despite unrelated stale revision"),GroupedOK && Grouped.Groups.Num()==1);
                const int32 Revision=Groups->Revision;
                Groups->PreSaveGame_Implementation(0,0);
                Check(TEXT("no-op autosave leaves hierarchy revision unchanged"),Groups->Revision==Revision);
                if (GroupedOK)
                {
                    Check(TEXT("server still rejects stale selected containers atomically"),!Groups->Execute(Player,EBSGroupOperation::Ungroup,Grouped,Stale,Result,Error) && Groups->Revision==Revision && FBSGroupTree::Find(Groups->Groups,Grouped.Groups[0]));
                    Error.Reset();
                    Check(TEXT("current group selection can ungroup after stale request"),Groups->Execute(Player,EBSGroupOperation::Ungroup,Grouped,Groups->Revision,Result,Error));
                }
            }
            auto* Dismantle=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState());
            Check(TEXT("dismantle state available"),Dismantle!=nullptr);
            if (Dismantle)
            {
                Dismantle->SetAimedAtActor(Refs[0].ResolveActor(World));
                auto Hover=Edit->HoverSelection(Dismantle);
                Check(TEXT("Connected hover expands native target"),Hover.Buildings.Num()==2);
                Edit->Selection=Hover; Edit->bSelectionDirty=true;
                Gun->SetCurrentBuildGunMode(UBSEditModeDescriptor::StaticClass());
                Check(TEXT("mode switching retains Connected selection"),Edit->Selection.Buildings.Num()==2);
                Gun->SetCurrentBuildGunMode(UBSEditConnectedModeDescriptor::StaticClass());
            }
        }
        if (Phase == 2)
        {
            Key(EKeys::K,true);
            FTimerHandle Navigate;
            World->GetTimerManager().SetTimer(Navigate,[Edit,PC]()
            {
                SharedEditSettingsOpened=Edit->TransformersWindow && PC->bShowMouseCursor && PC->IsLookInputIgnored();
                if(!Edit->TransformersWindow)return;
                TSharedPtr<SWidget> Button;
                TFunction<bool(TSharedRef<SWidget>)> HasLabel=[&](TSharedRef<SWidget> W)
                {
                    if(W->GetType()==TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(W)->GetText().ToString()==TEXT("Connected settings"))return true;
                    auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)if(HasLabel(Children->GetChildAt(I)))return true;
                    return false;
                };
                TFunction<void(TSharedRef<SWidget>)> Find=[&](TSharedRef<SWidget> W)
                {
                    if(W->GetType()==TEXT("SButton") && HasLabel(W))Button=W;
                    auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)Find(Children->GetChildAt(I));
                };
                Find(Edit->TransformersWindow->TakeWidget());
                if(!Button)return;
                auto& App=FSlateApplication::Get();const auto Window=App.FindWidgetWindow(Button.ToSharedRef());
                const auto Point=Button->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(.5,.5));
                const TSet<FKey> Up,Down={EKeys::LeftMouseButton};
                App.ProcessMouseMoveEvent(FPointerEvent(0,Point,App.GetCursorPos(),Up,EKeys::Invalid,0,FModifierKeysState()));
                App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),FPointerEvent(0,Point,Point,Down,EKeys::LeftMouseButton,0,FModifierKeysState()));
                App.ProcessMouseButtonUpEvent(FPointerEvent(0,Point,Point,Up,EKeys::LeftMouseButton,0,FModifierKeysState()));
            },.4f,false);
        }
        if (Phase == 3)
        {
            Key(EKeys::K,false);
            Check(TEXT("K opens shared Edit settings and mouse navigation opens Connected settings"),SharedEditSettingsOpened && Edit->ConnectedWindow && !Edit->TransformersWindow && PC->bShowMouseCursor && PC->IsLookInputIgnored());
            if (Edit->ConnectedWindow)
            {
                TSharedPtr<SWidget> Field;
                TFunction<void(TSharedRef<SWidget>)> Find = [&](TSharedRef<SWidget> W)
                { if (W->GetType()==TEXT("SEditableText") && !Field) Field=W; auto* Children=W->GetChildren(); for(int32 I=0; I<Children->Num();++I) Find(Children->GetChildAt(I)); };
                Find(Edit->ConnectedWindow->TakeWidget());
                auto Capture = [World](const TCHAR* Name)
                {
                    for (TObjectIterator<UBackgroundBlur> It; It; ++It)
                        if (It->GetWorld()==World && It->IsVisible() && It->GetCachedGeometry().GetLocalSize().X>1)
                            UE_LOG(LogBuildingSkills,Display,TEXT("CONNECTED BLUR: %s parent=%s position=%s size=%s"),*It->GetPathName(),*GetNameSafe(It->GetParent()),*It->GetCachedGeometry().GetAbsolutePosition().ToString(),*It->GetCachedGeometry().GetLocalSize().ToString());
                    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/Name,true,false);
                };
                Capture(TEXT("BuildingSkillsConnectedTest.png"));
                FTimerHandle CaptureTimer;
                World->GetTimerManager().SetTimer(CaptureTimer,[Capture]() { Capture(TEXT("BuildingSkillsConnectedSettledTest.png")); },2.0f,false);
                Check(TEXT("numeric input present"),Field.IsValid());
                if (Field)
                {
                    auto& App=FSlateApplication::Get(); const auto Window=App.FindWidgetWindow(Field.ToSharedRef());
                    const FVector2D P=Field->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(.5,.5));
                    const TSet<FKey> Up,Down={EKeys::LeftMouseButton};
                    App.ProcessMouseMoveEvent(FPointerEvent(0,P,App.GetCursorPos(),Up,EKeys::Invalid,0,FModifierKeysState()));
                    App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),FPointerEvent(0,P,P,Down,EKeys::LeftMouseButton,0,FModifierKeysState()));
                    App.ProcessMouseButtonUpEvent(FPointerEvent(0,P,P,Up,EKeys::LeftMouseButton,0,FModifierKeysState()));
                    Check(TEXT("actual mouse click focuses gap input"),Field->HasKeyboardFocus());
                    StaticCastSharedPtr<SEditableText>(Field)->SelectAllText();
                    for (TCHAR C : FString(TEXT("0.5"))) App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
                    App.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
                }
            }
        }
        if (Phase == 4)
        {
            Check(TEXT("typed gap updates draft"),Edit->ConnectedWindow && FMath::IsNearlyEqual(Edit->ConnectedWindow->Draft.Gap,.5));
            if (Edit->ConnectedWindow)
            {
                TSharedPtr<SWidget> Button;
                TFunction<void(TSharedRef<SWidget>)> Find = [&](TSharedRef<SWidget> W)
                { if (W->GetType()==TEXT("SButton")) Button=W; auto* Children=W->GetChildren(); for(int32 I=0; I<Children->Num();++I) Find(Children->GetChildAt(I)); };
                Find(Edit->ConnectedWindow->TakeWidget()); // Apply is the last button.
                if (Button)
                {
                    auto& App=FSlateApplication::Get(); const auto Window=App.FindWidgetWindow(Button.ToSharedRef());
                    const FVector2D P=Button->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(.5,.5));
                    const TSet<FKey> Up,Down={EKeys::LeftMouseButton};
                    App.ProcessMouseMoveEvent(FPointerEvent(0,P,App.GetCursorPos(),Up,EKeys::Invalid,0,FModifierKeysState()));
                    App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),FPointerEvent(0,P,P,Down,EKeys::LeftMouseButton,0,FModifierKeysState()));
                    App.ProcessMouseButtonUpEvent(FPointerEvent(0,P,P,Up,EKeys::LeftMouseButton,0,FModifierKeysState()));
                }
            }
            Check(TEXT("mouse Apply stores gap and restores camera"),!Edit->ConnectedWindow && !PC->bShowMouseCursor && !PC->IsLookInputIgnored() && FMath::IsNearlyEqual(Edit->ConnectedSettings.Gap,.5));
            Edit->ConnectedSettings.Gap=0;
            Edit->LeaveEditMode(false);
            Gun->SetCurrentBuildGunMode(UBSConnectedModeDescriptor::StaticClass());
        }
        if (Phase == 5)
        {
            Check(TEXT("Dismantle supports Connected without Edit"),Edit->IsConnectedSelection() && !Edit->IsEditing());
            Player->SetActorLocation(Player->GetActorLocation()+FVector(0,-1000,1400),false,nullptr,ETeleportType::TeleportPhysics);
            Gun->GotoPaintState(Swatch,true);
        }
        if (Phase == 6)
        {
            Check(TEXT("native Customizer state entered"),Edit->IsPainting());
            PC->SetControlRotation((FBSTransformNative::Bounds(World,Refs[0]).GetCenter()-Player->GetPawnViewLocation()).Rotation());
            Key(EKeys::R,true);
        }
        if (Phase == 7)
        {
            Key(EKeys::R,false); Check(TEXT("configured R toggles Connected painting"),Edit->bConnectedPaint);
            Check(TEXT("Connected swatch previews the touching extra foundation"),Edit->ConnectedPaintPreviewMembers.Num()==1 && Edit->ConnectedPaintPreviewMembers.Contains(Refs[1]) && Edit->ConnectedPaintPreviewActors.Num()==1);
            Check(TEXT("Connected extra preview uses the native hologram outline color"),Edit->ConnectedPaintPreviewOutline && Edit->ConnectedPaintPreviewActors.Num()==1 && Edit->ConnectedPaintPreviewActors[0].IsValid() &&
                Edit->ConnectedPaintPreviewOutline->GetOutlineStateColorForActor(Edit->ConnectedPaintPreviewActors[0].Get())==EOutlineColor::OC_HOLOGRAMLINE);
            bool Unchanged=true;
            const auto SwatchDesc=UFGCustomizationRecipe::GetCustomizationDescriptor(Swatch);
            for(int32 I=0;I<2;++I)
            { const auto* Data=LW->GetRuntimeDataForBuildableClassAndIndex(Refs[I].LightweightClass,Refs[I].ResolveIndex(World)); Unchanged &= Data && Data->CustomizationData.SwatchDesc!=SwatchDesc; }
            Unchanged &= Edit->ConnectedPaintPreviewActors.Num()==Edit->ConnectedPaintPreviewOriginalData.Num();
            for (int32 I=0;Unchanged && I<Edit->ConnectedPaintPreviewActors.Num();++I)
                Unchanged &= Edit->ConnectedPaintPreviewActors[I].IsValid() && Edit->ConnectedPaintPreviewActors[I]->GetCustomizationData_Native()==Edit->ConnectedPaintPreviewOriginalData[I];
            Check(TEXT("prospective swatch preview does not apply customization before click"),Unchanged);
            Check(TEXT("native center row shows Connected spread"),NativeModeLabel()==TEXT("Spread: Connected"));
            DumpHints();
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsConnectedSwatchPreviewTest.png"),true,false);
            FTimerHandle AimTimer;
            World->GetTimerManager().SetTimer(AimTimer,[PC,Player,World]()
            { PC->SetControlRotation((FBSTransformNative::Bounds(World,Refs[3]).GetCenter()-Player->GetPawnViewLocation()).Rotation()); },.3f,false);
        }
        if (Phase == 8)
        {
            Check(TEXT("aiming a disconnected foundation clears previous Connected preview"),Edit->ConnectedPaintPreviewMembers.IsEmpty() && Edit->ConnectedPaintPreviewActors.IsEmpty() && Edit->ConnectedPaintPreviewReplacements.IsEmpty() && !Edit->ConnectedPaintPreviewOutline);
            // Give our fixture a known cleanup state before it is previewed again.
            if (auto* Actor=Refs[1].ResolveActor(World)) Actor->SetBlockCleanupOfTemporary(false);
            PC->SetControlRotation((FBSTransformNative::Bounds(World,Refs[0]).GetCenter()-Player->GetPawnViewLocation()).Rotation());
        }
        if (Phase == 9)
        {
            Check(TEXT("aiming the original component restores prospective preview"),Edit->ConnectedPaintPreviewMembers.Num()==1 && Edit->ConnectedPaintPreviewMembers.Contains(Refs[1]));
            auto* Actor=Edit->ConnectedPaintPreviewActors.Num()==1 ? Edit->ConnectedPaintPreviewActors[0].Get() : nullptr;
            const int32 Index=Refs[1].ResolveIndex(World);
            const auto* Initial=LW->GetRuntimeDataForBuildableClassAndIndex(Refs[1].LightweightClass,Index);
            if (Actor && Initial && Edit->ConnectedPaintPreviewOriginalCleanupBlocks.Num()==1)
            {
                const auto Customization=Initial->CustomizationData;
                const bool OriginalBlock=Edit->ConnectedPaintPreviewOriginalCleanupBlocks[0];
                Check(TEXT("preview movement fixture begins with a temporary cleanup block"),!OriginalBlock && Actor->GetBlockCleanupOfTemporary());
                FBSNativeTransform Move;
                Move.Building=Refs[1]; Move.InstanceIndex=Index; Move.Target=Refs[1].Transform;
                Move.Target.AddToTranslation(FVector(0,100,0));
                Check(TEXT("native placed-building move updates the previewed lightweight actor"),FBSTransformNative::Apply(World,Move) && Actor->GetActorTransform().Equals(Move.Target,.01));
                FBSConnectedPaint::ClearPreview(Edit);
                const auto* Moved=LW->GetRuntimeDataForBuildableClassAndIndex(Refs[1].LightweightClass,Index);
                Check(TEXT("moving a previewed lightweight building restores its original cleanup flag"),Actor->GetBlockCleanupOfTemporary()==OriginalBlock && Edit->ConnectedPaintPreviewActors.IsEmpty());
                Check(TEXT("moving and clearing preview preserves stored and native customization"),Moved && Moved->CustomizationData==Customization && Actor->GetCustomizationData_Native()==Customization);
                FBSNativeTransform Restore;
                Restore.Building=FBSBuildingRef::FromLightweight(World,Refs[1].LightweightClass,Index);
                Restore.InstanceIndex=Index; Restore.Target=Refs[1].Transform;
                Check(TEXT("native move restores preview fixture transform"),FBSTransformNative::Apply(World,Restore) && Refs[1].IsAlive(World));
                FBSConnectedPaint::TickPreview(Edit,Cast<UFGBuildGunStatePaint>(Gun->GetCurrentState()));
                Check(TEXT("restored fixture can reacquire Connected preview normally"),Edit->ConnectedPaintPreviewMembers.Num()==1 && Edit->ConnectedPaintPreviewMembers.Contains(Refs[1]) && Actor->GetBlockCleanupOfTemporary());
            }
            else Check(TEXT("preview movement regression has a valid lightweight fixture"),false);
            Key(EKeys::K,true);
        }
        if (Phase == 10)
        {
            Key(EKeys::K,false); Check(TEXT("K opens same settings in Customizer"),Edit->ConnectedWindow!=nullptr);
            Check(TEXT("settings modal clears prospective Connected paint preview"),Edit->ConnectedPaintPreviewMembers.IsEmpty() && Edit->ConnectedPaintPreviewActors.IsEmpty() && Edit->ConnectedPaintPreviewReplacements.IsEmpty());
            Edit->CloseConnectedSettings();
            if (auto* State = World->GetGameState<AFGGameState>()) State->SetCheatNoCost(true);
            auto* RCO=PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>();
            Check(TEXT("paint RPC available"),RCO!=nullptr);
            Gun->OnPrimaryFirePressed(); Gun->OnPrimaryFireReleased();
        }
        if (Phase == 11)
        {
            const auto Desc=UFGCustomizationRecipe::GetCustomizationDescriptor(Swatch);
            for(int32 I=0; I<4; ++I)
            { const auto* Data=LW->GetRuntimeDataForBuildableClassAndIndex(Refs[I].LightweightClass,Refs[I].ResolveIndex(World)); Check(I<2?TEXT("touching foundation receives swatch"):TEXT("disconnected foundation unchanged"),Data && (Data->CustomizationData.SwatchDesc==Desc)==(I<2)); }
            FBSConnectedSettings S; S.Gap=.5;
            Check(TEXT("swatch boundary stops expansion"),Search(S).Num()==2);
            S.IgnoreSwatch=true; Check(TEXT("ignore swatch reaches beyond boundary"),Search(S).Num()==3);
            Gun->GotoPaintState(Material,true);
        }
        if (Phase == 12)
        {
            Check(TEXT("native center row still shows Connected spread for material recipe"),NativeModeLabel().StartsWith(TEXT("Spread: Connected")));
            Check(TEXT("Connected material previews a native replacement for extra foundation"),Edit->ConnectedPaintPreviewMembers.Num()==1 && Edit->ConnectedPaintPreviewReplacements.Num()==1 && Edit->ConnectedPaintPreviewReplacements[0].IsValid());
            Check(TEXT("material preview preserves nonuniform source scale"),Edit->ConnectedPaintPreviewReplacements.Num()==1 && Edit->ConnectedPaintPreviewReplacements[0].IsValid() &&
                Edit->ConnectedPaintPreviewReplacements[0]->GetActorScale3D().Equals(Refs[1].Transform.GetScale3D()*1.01,.001));
            if (!Edit->ConnectedPaintPreviewReplacements.IsEmpty()) MaterialPreview=Edit->ConnectedPaintPreviewReplacements[0];
            Check(TEXT("material preview leaves original foundation instances intact before click"),Refs[0].IsAlive(World) && Refs[1].IsAlive(World));
            auto* Paint=Cast<UFGBuildGunStatePaint>(Gun->GetCurrentState());
            auto* AimProperty=FindFProperty<FObjectProperty>(UFGBuildGunStatePaint::StaticClass(),TEXT("mCurrentlyAimedAtActor"));
            Check(TEXT("native paint target is available for preview reuse regression"),Paint && AimProperty);
            if (Paint && AimProperty && MaterialPreview.IsValid())
            {
                const auto PreviousSettings=Edit->ConnectedSettings;
                auto* PreviousAim=AimProperty->GetObjectPropertyValue_InContainer(Paint);
                Edit->ConnectedSettings.Gap=.5; Edit->ConnectedSettings.IgnoreSwatch=true;
                FBSConnectedPaint::TickPreview(Edit,Paint);
                auto* NewAim=Refs[2].ResolveActor(World);
                for (int32 I=Edit->ConnectedPaintPreviewActors.Num()-1; I>=0; --I)
                    if (Edit->ConnectedPaintPreviewActors[I].Get()==NewAim) FBSConnectedPaint::RemovePreview(Edit,I);
                AimProperty->SetObjectPropertyValue_InContainer(Paint,NewAim);
                FBSConnectedPaint::TickPreview(Edit,Paint);
                Check(TEXT("aiming another connected member retains shared material replacement actor"),
                    MaterialPreview.IsValid() && Edit->ConnectedPaintPreviewReplacements.Contains(MaterialPreview) && Edit->ConnectedPaintPreviewMembers.Contains(Refs[1]));
                AimProperty->SetObjectPropertyValue_InContainer(Paint,PreviousAim); Edit->ConnectedSettings=PreviousSettings;
                FBSConnectedPaint::TickPreview(Edit,Paint);
                Check(TEXT("restoring paint settings removes only the departed preview members"),
                    Edit->ConnectedPaintPreviewMembers.Num()==1 && Edit->ConnectedPaintPreviewMembers.Contains(Refs[1]) && Edit->ConnectedPaintPreviewReplacements.Contains(MaterialPreview));
            }
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsConnectedMaterialPreviewTest.png"),true,false);
            FTimerHandle PaintTimer;
            World->GetTimerManager().SetTimer(PaintTimer,[Gun]() { Gun->OnPrimaryFirePressed(); Gun->OnPrimaryFireReleased(); },.3f,false);
        }
        if (Phase == 13)
        {
            Check(TEXT("native material replacement removed original connected instances"),!Refs[0].IsAlive(World) && !Refs[1].IsAlive(World));
            Check(TEXT("material painting leaves disconnected instances"),Refs[2].IsAlive(World) && Refs[3].IsAlive(World));
        }
        if (Phase == 14)
        {
            Check(TEXT("Connected paint hints are attached before leaving"),HintsAttached() && HasHint(TEXT("Connected settings")));
            Gun->GotoBuildState(BuildRecipe);
        }
        if (Phase == 15)
        {
            Check(TEXT("Paint to Build clears Building Skills HUD and K mapping"),Gun->IsInState(EBuildGunState::BGS_BUILD) && !Edit->Indicator && !Edit->ControlHints && BuildingSkillsKCount()==0);
            Check(TEXT("Paint to Build restores native mode row and removes Connected previews"),!NativeModeLabel().StartsWith(TEXT("Spread:")) && Edit->ConnectedPaintPreviewMembers.IsEmpty() && Edit->ConnectedPaintPreviewActors.IsEmpty() && Edit->ConnectedPaintPreviewReplacements.IsEmpty() && !MaterialPreview.IsValid());
            auto* Build = Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState());
            Check(TEXT("normal Build has real foundation hologram for Smart"),Build && Build->GetHologram());
            Check(TEXT("normal Build restores physical keyboard focus after painting"),ViewportFocusRestored());
            DumpInput();
            ExternalKConflict=FindExternalKConflict();
        }
        if (Phase == 16)
        {
            ExternalKConflict=FindExternalKConflict();
            if (ExternalKConflict.IsEmpty()) WindowKey(EKeys::K,true);
        }
        if (Phase == 17)
        {
            if (ExternalKConflict.IsEmpty()) WindowKey(EKeys::K,false);
            auto* Menu = SmartMenu();
            if (!ExternalKConflict.IsEmpty())
            {
                UE_LOG(LogBuildingSkills,Display,TEXT("RUNTIME SKIP: Connected: Smart menu after painting is blocked by existing external K mapping: %s; no other mod context or binding changed"),*ExternalKConflict);
            }
            else Check(TEXT("K opens installed Smart menu in normal Build after painting"),Menu && PC->bShowMouseCursor && !Edit->ConnectedWindow && !Edit->TransformSettingsWindow);
            DumpInput();
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsSmartAfterPaintTest.png"),true,false);
        }
        if (Phase == 18)
        {
            if (SmartMenu()) EscapeMenu();
            Check(TEXT("closing Smart menu leaves Build without Building Skills K"),BuildingSkillsKCount()==0);
            Gun->GotoDismantleState(); Gun->SetCurrentBuildGunMode(UBSConnectedModeDescriptor::StaticClass());
        }
        if (Phase == 19)
        {
            Check(TEXT("Connected Dismantle reentry attaches current hints"),Edit->IsConnectedSelection() && !Edit->IsEditing() && HintsAttached() && HasHint(TEXT("Connected settings")));
            Check(TEXT("Smart modal closed before Dismantle reentry"),!SmartMenu());
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsConnectedDismantleHintsTest.png"),true,false);
            Key(EKeys::K,true);
        }
        if (Phase == 20)
        {
            Key(EKeys::K,false);
            Check(TEXT("K still opens Connected settings after Build round trip"),Edit->ConnectedWindow!=nullptr);
            Edit->CloseConnectedSettings();
            Player->UnequipBuildGun();
        }
        if (Phase == 21)
        {
            Check(TEXT("unequip removes Connected HUD and K mapping"),!Player->IsBuildGunEquipped() && !Edit->Indicator && !Edit->ControlHints && BuildingSkillsKCount()==0);
            Player->EquipBuildGunAndGoToMenuState();
            Gun->GotoDismantleState(); Gun->SetCurrentBuildGunMode(UBSConnectedModeDescriptor::StaticClass());
        }
        if (Phase == 22)
        {
            Check(TEXT("Connected Dismantle reentry after unequip attaches hints"),Edit->IsConnectedSelection() && HintsAttached());
            Gun->GotoPaintState(Swatch,true);
        }
        if (Phase == 23)
        {
            Check(TEXT("Connected Paint reentry attaches both current hints"),Edit->IsPainting() && Edit->bConnectedPaint && HintsAttached() && HasHint(TEXT("Toggle spread")) && HasHint(TEXT("Connected settings")));
            DumpHints();
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsConnectedPaintHintsTest.png"),true,false);
            FTimerHandle TransitionTimer;
            World->GetTimerManager().SetTimer(TransitionTimer,[Gun]() { Gun->GotoBuildState(BuildRecipe); },.3f,false);
        }
        if (Phase == 24)
        {
            Check(TEXT("second Paint to Build clears hints and K mapping"),!Edit->Indicator && !Edit->ControlHints && BuildingSkillsKCount()==0);
            Gun->GotoPaintState(Swatch,true);
        }
        if (Phase == 25)
        {
            Check(TEXT("Paint Build Paint round trip reattaches hints"),Edit->IsPainting() && HintsAttached() && HasHint(TEXT("Connected settings")));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsConnectedPaintHintsTest.png"),true,false);
            Key(EKeys::R,true);
        }
        if (Phase == 26)
        {
            Key(EKeys::R,false);
            Check(TEXT("Default painting releases K and removes Connected hint"),Edit->IsPainting() && !Edit->bConnectedPaint && BuildingSkillsKCount()==0 && HintsAttached() && !HasHint(TEXT("Connected settings")));
            Check(TEXT("Default painting restores native preview and center spread label"),Edit->ConnectedPaintPreviewMembers.IsEmpty() && Edit->ConnectedPaintPreviewActors.IsEmpty() && Edit->ConnectedPaintPreviewReplacements.IsEmpty() && NativeModeLabel()==TEXT("Spread: Default"));
            Key(EKeys::LeftAlt,true); Key(EKeys::E,true);
        }
        if (Phase == 27)
        {
            Key(EKeys::E,false); Key(EKeys::LeftAlt,false);
            Gun->SetCurrentBuildGunMode(UBSEditConnectedModeDescriptor::StaticClass());
            Edit->Selection={}; Edit->Selection.Buildings.Add(Refs[2]); Edit->bSelectionDirty=true;
        }
        if (Phase == 28)
        {
            Check(TEXT("Connected Edit reentry attaches selection hints"),Edit->IsEditing() && Edit->IsConnectedSelection() && HintsAttached() && HasHint(TEXT("Edit settings")));
            Key(EKeys::T,true);
        }
        if (Phase == 29)
        {
            Key(EKeys::T,false);
            Check(TEXT("Transform from Connected shows only Step settings hint"),Edit->IsTransforming() && HintsAttached() && HasHint(TEXT("Step settings")) && !HasHint(TEXT("Connected settings")));
            Check(TEXT("Transform owns exactly one Building Skills K mapping"),BuildingSkillsKCount()==1 && !Edit->ConnectedContext && Edit->TransformContext);
            Key(EKeys::K,true);
        }
        if (Phase == 30)
        {
            Key(EKeys::K,false);
            Check(TEXT("K from Connected Transform opens only step settings"),Edit->TransformSettingsWindow && !Edit->ConnectedWindow && PC->bShowMouseCursor);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsConnectedTransformSettingsTest.png"),true,false);
        }
        if (Phase == 31)
        { Edit->CloseTransformSettings(); Edit->EndTransform(); }
        if (Phase == 32)
        {
            Check(TEXT("return from Transform restores Edit K and hints"),!Edit->IsTransforming() && HintsAttached() && HasHint(TEXT("Edit settings")) && !HasHint(TEXT("Step settings")) && BuildingSkillsKCount()==1);
            Gun->GotoBuildState(BuildRecipe);
        }
        if (Phase == 33)
        {
            Check(TEXT("Transform Edit to Build clears all Building Skills K mappings"),Gun->IsInState(EBuildGunState::BGS_BUILD) && !Edit->Indicator && !Edit->ControlHints && BuildingSkillsKCount()==0);
            Check(TEXT("normal Build restores physical keyboard focus after Transform settings"),ViewportFocusRestored());
            ExternalKConflict=FindExternalKConflict();
            if (ExternalKConflict.IsEmpty()) WindowKey(EKeys::K,true);
        }
        if (Phase == 34)
        {
            if (ExternalKConflict.IsEmpty()) WindowKey(EKeys::K,false);
            auto* Menu = SmartMenu();
            if (!ExternalKConflict.IsEmpty())
            {
                UE_LOG(LogBuildingSkills,Display,TEXT("RUNTIME SKIP: Connected: Smart menu after Transform is blocked by existing external K mapping: %s; no other mod context or binding changed"),*ExternalKConflict);
            }
            else Check(TEXT("K opens installed Smart menu after Connected Transform exit"),Menu && PC->bShowMouseCursor && !Edit->ConnectedWindow && !Edit->TransformSettingsWindow);
            DumpInput();
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsSmartAfterTransformTest.png"),true,false);
        }
        if (Phase == 35)
        { if (SmartMenu()) EscapeMenu(); }
        if (Phase == 36)
        {
            Check(TEXT("Smart close after Transform restores normal Build input"),!SmartMenu() && BuildingSkillsKCount()==0);
            Player->UnequipBuildGun();
        }
        if (Phase == 37)
        {
            Check(TEXT("final unequip leaves no Building Skills modal hints or K"),!Player->IsBuildGunEquipped() && !Edit->Indicator && !Edit->ControlHints && !Edit->ConnectedWindow && !Edit->TransformSettingsWindow && BuildingSkillsKCount()==0);
        }
        if (Phase == 38) Player->HotKeyDismantle();
        if (Phase == 39) { Gun->SetCurrentBuildGunMode(UBSConnectedModeDescriptor::StaticClass()); WindowKey(EKeys::R,true); }
        if (Phase == 40)
        {
            TArray<TSubclassOf<UFGBuildGunModeDescriptor>> Modes; Gun->GetCurrentState()->GetSupportedBuildModes(Modes);
            Check(TEXT("holding R opens native Dismantle radial with Group and Connected"),Gun->IsWaitingForBuildGunModeSelectUI() && Modes.Contains(UBSGroupModeDescriptor::StaticClass()) && Modes.Contains(UBSConnectedModeDescriptor::StaticClass()));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsDismantleRadialTest.png"),true,false);
        }
        if (Phase == 41) { Gun->SetCurrentBuildGunMode(UBSGroupModeDescriptor::StaticClass()); WindowKey(EKeys::R,false); }
        if (Phase == 42)
        {
            Edit->OnToggleTriggered(); Edit->OnToggleReleased();
            FTimerHandle Timer; World->GetTimerManager().SetTimer(Timer,[PC]()
            { PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId(), EKeys::R, IE_Pressed, 1.0, false, 0)); },.3f,false);
        }
        if (Phase == 43)
        {
            Check(TEXT("holding R in Edit keeps the native radial HUD visible"),Gun->IsWaitingForBuildGunModeSelectUI() && Edit->IsEditing() && !Edit->NativeGunHUD.IsValid());
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsEditRadialTest.png"),true,false);
        }
        if (Phase == 44)
        {
            Gun->SetCurrentBuildGunMode(UBSEditModeDescriptor::StaticClass()); WindowKey(EKeys::R,false);
            FTimerHandle Timer; World->GetTimerManager().SetTimer(Timer,[Edit,Input,World]()
            {
                Edit->Selection={}; Edit->Selection.Buildings={Refs[2]}; Edit->OnTransform();
                Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::R,IE_Pressed,1.f,false,0));
                FTimerHandle Release; World->GetTimerManager().SetTimer(Release,[Input]()
                { Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::R,IE_Released,0.f,false,0)); },.05f,false);
            },.3f,false);
        }
        if (Phase == 45)
        {
            Check(TEXT("tap R advances Transform exactly once"),Edit->GetTransformMode()==EBSTransformMode::RotateY);
            WindowKey(EKeys::R,true);
        }
        if (Phase == 46)
        {
            TArray<TSubclassOf<UFGBuildGunModeDescriptor>> Modes; Gun->GetCurrentState()->GetSupportedBuildModes(Modes);
            Check(TEXT("holding R opens native Transform radial with ten choices"),Gun->IsWaitingForBuildGunModeSelectUI() && Edit->IsTransforming() && Modes.Num()==10 && Modes.Contains(UBSScaleXYZModeDescriptor::StaticClass()));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsTransformRadialTest.png"),true,false);
            Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::MouseX,IE_Axis,1000.f,false,0));
            Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::MouseY,IE_Axis,-500.f,false,0));
        }
        if (Phase == 47) WindowKey(EKeys::R,false);
        if (Phase == 48)
        {
            Check(TEXT("native radial release selects uniform scale and retains Edit selection"),Edit->GetTransformMode()==EBSTransformMode::ScaleXYZ && Edit->IsEditing() && Edit->Selection.Buildings.Num()==1 && !Gun->IsWaitingForBuildGunModeSelectUI());
            Edit->EndTransform(); Edit->LeaveEditMode(false); Gun->GotoPaintState(Swatch,true);
            FTimerHandle Timer; World->GetTimerManager().SetTimer(Timer,[Input]()
            { Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::R,IE_Pressed,1.f,false,0)); },.3f,false);
        }
        if (Phase == 49)
        {
            TArray<TSubclassOf<UFGBuildGunModeDescriptor>> Modes; Gun->GetCurrentState()->GetSupportedBuildModes(Modes);
            Check(TEXT("holding R opens native spread radial with Default and Connected"),Gun->IsWaitingForBuildGunModeSelectUI() && Modes.Num()==2 && Modes.Contains(UBSSpreadDefaultModeDescriptor::StaticClass()) && Modes.Contains(UBSSpreadConnectedModeDescriptor::StaticClass()));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsSpreadRadialTest.png"),true,false);
            Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::MouseY,IE_Axis,-500.f,false,0));
        }
        if (Phase == 50)
        {
            WindowKey(EKeys::R,false);

        }
        if (Phase == 51)
        {
            Check(TEXT("releasing the native radial selects Connected spread and restores input"),Edit->bConnectedPaint && Edit->IsSpreadPainting() && !Gun->IsWaitingForBuildGunModeSelectUI());
            Edit->OnConnectedSettings();
        }
        if (Phase == 52) Check(TEXT("settings can open after spread radial closes"),Edit->ConnectedWindow!=nullptr);
        if (Phase == 52 && Edit->ConnectedWindow)
        {
            TArray<TSharedPtr<SEditableText>> Fields;
            TFunction<void(TSharedRef<SWidget>)> Find = [&](TSharedRef<SWidget> Widget)
            {
                if (Widget->GetType()==TEXT("SEditableText")) Fields.Add(StaticCastSharedRef<SEditableText>(Widget));
                auto* Children=Widget->GetChildren(); for(int32 I=0;I<Children->Num();++I) Find(Children->GetChildAt(I));
            };
            Find(Edit->ConnectedWindow->TakeWidget());
            Check(TEXT("Connected modal exposes regex text fields"),Fields.Num()==4);
            if (Fields.Num()==4)
            {
                auto& App=FSlateApplication::Get(); App.SetKeyboardFocus(Fields[2],EFocusCause::SetDirectly);
                const FModifierKeysState AltGr(false,false,true,false,false,true,false,false,false);
                const bool Consumed=App.ProcessKeyDownEvent(FKeyEvent(EKeys::Seven,AltGr,0,false,0,0));
                App.ProcessKeyCharEvent(FCharacterEvent('|',AltGr,0,false));
                App.ProcessKeyUpEvent(FKeyEvent(EKeys::Seven,AltGr,0,false,0,0));
                Check(TEXT("AltGr+7 is consumed by whitelist and enters the pipe character"),Consumed && Edit->ConnectedWindow && Edit->ConnectedWindow->Draft.Whitelist==TEXT("|") && Edit->IsSpreadPainting());
                Fields[2]->SetText(FText::FromString(TEXT("(?i)(?:belt|pipe)")));
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsRegexInputTest.png"),true,false);
            }
        }
        if (Phase == 53) { Edit->CloseConnectedSettings(); Player->UnequipBuildGun(); }
        return Passed;
    }
};
bool RunBuildingSkillsConnectedChecks(AFGCharacterPlayer* Player,int32 Phase) { return FBSConnectedSmoke::Run(Player,Phase); }
