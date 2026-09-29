#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "EnhancedInputSubsystemInterface.h"
#include "EnhancedPlayerInput.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Input/FGChildInputMappingContext.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"

namespace
{
    // Exercise the real engine mapping rebuild and chord-blocker injection without
    // requiring the stubbed FactoryGame player-controller implementation.
    class FEditInputTestSubsystem : public IEnhancedInputSubsystemInterface
    {
    public:
        UEnhancedPlayerInput* Input = nullptr;
        virtual UEnhancedPlayerInput* GetPlayerInput() const override { return Input; }
    protected:
        virtual TMap<TObjectPtr<const UInputAction>, FInjectedInput>& GetContinuouslyInjectedInputs() override
        {
            return Injections;
        }
        TMap<TObjectPtr<const UInputAction>, FInjectedInput> Injections;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSEditChordTest, "BuildingSkills.Input.EditChord",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBSEditChordTest::RunTest(const FString& Parameters)
{
    UFGChildInputMappingContext* Context = LoadObject<UFGChildInputMappingContext>(nullptr,
        TEXT("/BuildingSkills/Inputs/MC_BuildingSkills.MC_BuildingSkills"));
    if (!TestNotNull(TEXT("Edit context is packaged"), Context)) return false;
    if (!TestEqual(TEXT("One editable shortcut"), Context->GetMappings().Num(), 1)) return false;
    const FEnhancedActionKeyMapping& Mapping = Context->GetMappings()[0];
    const UInputAction* Edit = Mapping.Action;
    if (!TestNotNull(TEXT("Edit action exists"), Edit)) return false;
    TestTrue(TEXT("Shortcut is player remappable"), Mapping.IsPlayerMappable());
    TestTrue(TEXT("Edit shortcut appears in the native button hint bar"), !!Mapping.bDisplayButtonHint);
    TestEqual(TEXT("Default primary key"), Mapping.Key, EKeys::E);
    if (!TestEqual(TEXT("One modifier trigger"), Mapping.Triggers.Num(), 1)) return false;
    UInputTriggerChordAction* Chord = Cast<UInputTriggerChordAction>(Mapping.Triggers[0]);
    if (!TestNotNull(TEXT("Modifier is an engine chord"), Chord) ||
        !TestNotNull(TEXT("Modifier action exists"), Chord->ChordAction.Get())) return false;

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    APlayerController* Controller = NewObject<APlayerController>(World->PersistentLevel);
    UEnhancedPlayerInput* Input = NewObject<UEnhancedPlayerInput>(Controller);
    Controller->PlayerInput = Input;
    FEditInputTestSubsystem Subsystem;
    Subsystem.Input = Input;
    UInputMappingContext* Vanilla = NewObject<UInputMappingContext>();
    UInputAction* Use = NewObject<UInputAction>();
    Use->bConsumeInput = false;
    Vanilla->MapKey(Use, EKeys::E);
    Vanilla->MapKey(Chord->ChordAction, EKeys::LeftAlt);
    Subsystem.AddMappingContext(Vanilla, 100);
    Subsystem.AddMappingContext(Context, 0);
    FModifyContextOptions Options;
    Options.bForceImmediately = true;
    Options.bIgnoreAllPressedKeysUntilRelease = false;
    Subsystem.RequestRebuildControlMappings(Options);

    auto Key = [Input](FKey Key, bool bDown)
    {
        Input->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Key,
            bDown ? IE_Pressed : IE_Released, bDown ? 1.0f : 0.0f, false, 0));
    };
    auto Tick = [Input]() { Input->ProcessInputStack({}, 1.0f / 60.0f, false); };
    auto Fired = [Input](const UInputAction* Action)
    {
        const FInputActionInstance* Instance = Input->FindActionInstanceData(Action);
        // UE 5.6 can expose a nonzero raw GetActionValue even when a chord blocks
        // its trigger. Gameplay binds Triggered, so assert that event directly.
        return Instance && Instance->GetTriggerEvent() == ETriggerEvent::Triggered;
    };

    Key(EKeys::E, true);
    Tick();
    TestTrue(TEXT("Plain E still interacts"), Fired(Use));
    TestFalse(TEXT("Plain E does not open Edit"), Fired(Edit));
    Key(EKeys::E, false);
    Tick();
    Key(EKeys::LeftAlt, true);
    Key(EKeys::E, true);
    Tick();
    TestTrue(TEXT("Alt+E opens Edit"), Fired(Edit));
    TestFalse(TEXT("Alt+E blocks vanilla E despite higher vanilla context priority"), Fired(Use));
    Tick();
    TestFalse(TEXT("Held Alt+E keeps vanilla E blocked"), Fired(Use));
    Key(EKeys::E, false);
    Tick();
    TestFalse(TEXT("Releasing E ends Edit activation"), Fired(Edit));
    TestFalse(TEXT("Releasing E does not interact"), Fired(Use));
    Key(EKeys::LeftAlt, false);
    Tick();
    Key(EKeys::E, true);
    Tick();
    TestTrue(TEXT("Normal E works again after the chord"), Fired(Use));
    Key(EKeys::E, false);
    Tick();

    // User-remapped modifier changes must remove the default Alt requirement.
    Subsystem.CSS_SetModifierKeyInputActions({Chord->ChordAction});
    Subsystem.CSS_AddPlayerMappedChordedActions(Mapping.GetMappingName(), {});
    Subsystem.RequestRebuildControlMappings(Options);
    Key(EKeys::E, true);
    Tick();
    TestTrue(TEXT("Removing the modifier makes plain E activate Edit"), Fired(Edit));
    Key(EKeys::E, false);
    Tick();

    auto* Operations = LoadObject<UFGChildInputMappingContext>(nullptr,
        TEXT("/BuildingSkills/Inputs/MC_EditOperations.MC_EditOperations"));
    if (TestNotNull(TEXT("Edit operations context exists"), Operations))
    {
        TestEqual(TEXT("Four configurable commands"), Operations->GetMappings().Num(), 4);
        UInputAction* Map = NewObject<UInputAction>();
        Vanilla->MapKey(Map, EKeys::M);
        Subsystem.RequestRebuildControlMappings(Options);
        Key(EKeys::M, true); Tick();
        TestTrue(TEXT("M opens Map outside Edit"), Fired(Map));
        Key(EKeys::M, false); Tick();
        Subsystem.AddMappingContext(Operations, 1000);
        Subsystem.RequestRebuildControlMappings(Options);
        for (const auto& Op : Operations->GetMappings())
        {
            TestTrue(TEXT("Operation is rebindable"), Op.IsPlayerMappable());
            TestEqual(TEXT("Group/Merge have native hints; Ungroup uses the conditional HUD hint"),
                !!Op.bDisplayButtonHint, Op.GetMappingName() != FName(TEXT("BuildingSkills.Ungroup")));
            Key(Op.Key, true); Tick();
            TestTrue(TEXT("Configured operation activates"), Fired(Op.Action));
            if (Op.Key == EKeys::M) TestFalse(TEXT("Merge suppresses Map inside Edit"), Fired(Map));
            Key(Op.Key, false); Tick();
        }
        Subsystem.RemoveMappingContext(Operations);
        Subsystem.RequestRebuildControlMappings(Options);
        Key(EKeys::M, true); Tick();
        TestTrue(TEXT("Map restored after closing Edit"), Fired(Map));
        Key(EKeys::M, false); Tick();
    }
    auto* SaveContext = LoadObject<UFGChildInputMappingContext>(nullptr, TEXT("/BuildingSkills/Inputs/MC_Save.MC_Save"));
    if (TestNotNull(TEXT("Conditional save context exists"), SaveContext) && TestEqual(TEXT("One Save command"), SaveContext->GetMappings().Num(),1))
    {
        const auto& Save=SaveContext->GetMappings()[0];
        TestTrue(TEXT("Save is configurable"),Save.IsPlayerMappable());
        TestEqual(TEXT("Save defaults to S"),Save.Key,EKeys::S);
        auto* Backward=NewObject<UInputAction>();
        Vanilla->MapKey(Backward,EKeys::S);
        Subsystem.RequestRebuildControlMappings(Options);
        Key(EKeys::S,true); Tick();
        TestTrue(TEXT("S moves normally without a single group"),Fired(Backward));
        Key(EKeys::S,false); Tick();
        Subsystem.AddMappingContext(SaveContext,1000);
        Subsystem.RequestRebuildControlMappings(Options);
        Key(EKeys::S,true); Tick();
        TestTrue(TEXT("S activates Save when its context is enabled"),Fired(Save.Action));
        TestFalse(TEXT("Saving consumes backward movement"),Fired(Backward));
        Key(EKeys::S,false); Tick();
        Subsystem.RemoveMappingContext(SaveContext);
        Subsystem.RequestRebuildControlMappings(Options);
        Key(EKeys::S,true); Tick();
        TestTrue(TEXT("S movement returns after Save context removal"),Fired(Backward));
        Key(EKeys::S,false); Tick();
    }
    auto* Transform = LoadObject<UFGChildInputMappingContext>(nullptr, TEXT("/BuildingSkills/Inputs/MC_Transform.MC_Transform"));
    if (TestNotNull(TEXT("Transform context exists"), Transform))
    {
        TestEqual(TEXT("Six movements, wheel directions and three modifiers"), Transform->GetMappings().Num(), 11);
        Subsystem.AddMappingContext(Transform, 1001);
        Subsystem.RequestRebuildControlMappings(Options);
        for (const auto& TransformMapping : Transform->GetMappings())
        {
            TestTrue(TEXT("Transform control is rebindable"), TransformMapping.IsPlayerMappable());
            Key(TransformMapping.Key, true); Tick();
            TestTrue(TEXT("Transform binding activates"), Fired(TransformMapping.Action));
            Key(TransformMapping.Key, false); Tick();
        }
    }
    World->DestroyWorld(false);
    return true;
}

#endif
