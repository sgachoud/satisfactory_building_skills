#include "BSTransformers.h"
#include "BSActionHistory.h"
#include "BSClipboard.h"
#include "BSConnected.h"
#include "BSEditModeComponent.h"
#include "BuildingSkills.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGInventoryComponent.h"
#include "FGGameState.h"
#include "FGBlueprintProxy.h"
#include "FGBlueprintSubsystem.h"
#include "FGBlueprintSettings.h"
#include "Hologram/FGBlueprintHologram.h"
#include "UObject/UnrealType.h"
#include "FGRecipe.h"
#include "FGCustomizationRecipe.h"
#include "FGRecipeManager.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Equipment/FGBuildGunDismantle.h"
#include "Equipment/FGBuildGunPaint.h"
#include "Hologram/FGHologram.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildablePipeline.h"
#include "FGBuildableSubsystem.h"
#include "FGPipeConnectionComponent.h"
#include "FGPowerConnectionComponent.h"
#include "Buildables/FGBuildableWire.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableSplitterSmart.h"
#include "FGFactoryConnectionComponent.h"
#include "EnhancedPlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Components/StaticMeshComponent.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

class FBSTransformerSmoke
{
public:
 static bool Run(AFGCharacterPlayer* P,int32 Phase)
 {
    auto* W=P->GetWorld();auto* PC=Cast<AFGPlayerController>(P->GetController());auto* E=P->FindComponentByClass<UBSEditModeComponent>();
    auto* R=PC->GetRemoteCallObjectOfClass<UBSGroupRemoteCallObject>();auto* Gun=P->GetBuildGun();auto* LW=AFGLightweightBuildableSubsystem::Get(W);
    auto* Input=Cast<UEnhancedPlayerInput>(PC->PlayerInput);
    static FBSBuildingRef Anchor;static TSubclassOf<UFGRecipe> Recipe;static FVector Base;static TArray<FBSBuildingRef> Built;
    static TArray<FItemAmount> Costs;static TArray<int32> Before;static FBSTransformer Mirror;static TArray<FBSBuildingRef> Network;
    static TArray<FBSBuildingRef> Machines;
    static TArray<FBSBuildingRef> ActionRefs;static TSubclassOf<UFGCustomizationRecipe> Swatch,Material;
    static TSubclassOf<AFGBuildable> ConcreteClass;static TSubclassOf<UFGRecipe> ConcreteRecipe;
    bool Passed=true;auto Check=[&](const TCHAR* Label,bool OK){Passed &= OK;UE_LOG(LogBuildingSkills,Display,TEXT("RUNTIME %s: Transformers: %s"),OK?TEXT("PASS"):TEXT("FAIL"),Label);};
    auto Key=[&](FKey K,bool Down){Input->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,K,Down?IE_Pressed:IE_Released,Down?1.f:0.f,false,0));};
    if(Phase==0)
    {
        Key(EKeys::E,false);Key(EKeys::LeftAlt,false);R->Transformers.Reset();R->BuildHistory.Reset();R->TransformersBuildAsBlueprint=false;
        auto* Class=LoadClass<AFGBuildable>(nullptr,TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x1_01.Build_Foundation_8x1_01_C"));
        Recipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Foundations/Recipe_Foundation_8x1_01.Recipe_Foundation_8x1_01_C"));
        Check(TEXT("fixture is available"),E && R && Class && Recipe && LW);if(!Class || !Recipe)return false;
        AFGRecipeManager::Get(W)->AddAvailableRecipe(Recipe);Base=P->GetActorLocation()+FVector(1800,0,200);
        FRuntimeBuildableInstanceData D;D.Transform=FTransform(Base);D.BuiltWithRecipe=Recipe;D.CustomizationData.SwatchDesc=Class->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(W);
        Anchor=FBSBuildingRef::FromLightweight(W,Class,LW->AddFromBuildableInstanceData(Class,D));E->BlueprintAnchor=Anchor;
        E->TickTransformers();Check(TEXT("add shortcut requires a live anchor"),E->AddTransformerContext!=nullptr);
    }
    if(Phase==1){Key(EKeys::LeftAlt,true);Key(EKeys::X,true);}
    if(Phase==2)
    {
        Key(EKeys::X,false);Key(EKeys::LeftAlt,false);Check(TEXT("physical Alt+X adds a transformer"),R->Transformers.Num()==1);
        if(R->Transformers.IsEmpty()) R->Server_AddTransformer_Implementation(Anchor); // Keep independent coverage after a failed physical-input assertion.
        Check(TEXT("anchor frame captured without scale"),R->Transformers.Num()==1 && R->Transformers[0].Frame.Equals(Anchor.Transform,.01));
        Key(EKeys::K,true);
    }
    if(Phase==3)
    {
        Key(EKeys::K,false);Check(TEXT("physical K opens transformer list with mouse input"),E->TransformersWindow && PC->bShowMouseCursor);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsTransformersSettings.png"),true,false);
    }
    if(Phase==4)
    {
        E->CloseTransformerSettings();R->Server_AddTransformer_Implementation(Anchor);
        if(R->Transformers.Num()!=2){Check(TEXT("second transformer added"),false);return false;}
        auto List=R->Transformers;List[0].Radius=64;List[1].Mirror=false;List[1].Rotation=true;List[1].Angle=90;List[1].RepeatTransformers=true;
        R->Server_SetTransformers_Implementation(List,R->TransformerRevision,true);Mirror=R->Transformers[0];
        Check(TEXT("settings apply on authority"),R->Transformers[1].Rotation && R->Transformers[1].RepeatTransformers);
        auto Edited=List;Edited[0].RelativeToOriginalAnchor=true;
        Edited[0].SetDisplayFrame(FTransform(FRotator(10,20,30),FVector(100,200,300)));
        R->Server_SetTransformers_Implementation(Edited,R->TransformerRevision,true);
        Check(TEXT("authority accepts editable relative pose and shared blueprint option"),R->TransformersBuildAsBlueprint && R->Transformers[0].Frame.Equals(Edited[0].Frame,.01));
        auto Invalid=Edited;Invalid[0].OriginalAnchor.AddToTranslation(FVector(100,0,0));
        R->Server_SetTransformers_Implementation(Invalid,R->TransformerRevision,false);
        Check(TEXT("clients cannot rewrite the original anchor or partially apply rejected settings"),R->Transformers[0].OriginalAnchor.Equals(Anchor.Transform,.01) && R->TransformersBuildAsBlueprint);
        R->Server_SetTransformers_Implementation(List,R->TransformerRevision,true);
        R->Server_SetTransformers_Implementation(Edited,R->TransformerRevision-1,false);
        Check(TEXT("stale settings cannot overwrite current pose or shared option"),R->Transformers[0].Frame.Equals(Anchor.Transform,.01) && R->TransformersBuildAsBlueprint);
        E->LeaveEditMode(false);P->HotKeyRecipe(Recipe);W->GetGameState<AFGGameState>()->SetCheatNoCost(false);
        PC->SetControlRotation((Base+FVector(800,900,0)-P->GetPawnViewLocation()).Rotation());
    }
    if(Phase==5)
    {
        auto* State=Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState());auto* H=State?State->GetHologram():nullptr;
        Check(TEXT("native foundation hologram equipped"),H!=nullptr);if(!H)return false;
        auto* Support=Anchor.ResolveActor(W);
        const FVector Point=Base+FVector(0,0,50);
        FHitResult Hit(Support,Cast<UPrimitiveComponent>(Support->GetRootComponent()),Point,FVector::UpVector);
        Hit.bBlockingHit=true;Hit.Location=Hit.ImpactPoint=Point;Hit.Normal=Hit.ImpactNormal=FVector::UpVector;
        H->UpdateHologramPlacement(Hit);
        // Freeze only the fixture's gun tick so native aim updates cannot move
        // this controlled placement between the preview screenshot and build.
        Gun->SetActorTickEnabled(false);
        H->SetActorTransform(FTransform(FRotator(0,20,0),Base+FVector(800,900,0)));
        P->GetInventory()->Resize(200);Costs=H->GetCost(true);Before.Reset();
        for(const auto& C:Costs){P->GetInventory()->AddStack(FInventoryStack(C.Amount*50,C.ItemClass));Before.Add(P->GetInventory()->GetNumItems(C.ItemClass));}
        H->ResetConstructDisqualifiers();H->SetPlacementMaterialState(EHologramMaterialState::HMS_OK);
        E->NextTransformerPreview=0;E->TickTransformers();
        Check(TEXT("copy holograms appear while holding native hologram"),!E->TransformerPreview.IsEmpty());
        Check(TEXT("both mirror and rotation markers are present"),E->TransformerMarkers.Num()==2);
        bool Visible=false;for(auto& C:E->TransformerPreview)Visible |= C->IsVisible() && !C->bHiddenInGame;
        Check(TEXT("generated preview meshes are visible"),Visible);
        bool Stencil=false;for(auto& C:E->TransformerPreview)Stencil |= C->bRenderCustomDepth && C->CustomDepthStencilValue!=0;
        Check(TEXT("generated previews use native hologram stencil rendering"),Stencil);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsTransformersPreview.png"),true,false);
    }
    if(Phase==6)
    {
        auto* State=Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState());if(!State)return false;
        State->InternalConstructHologram(FNetConstructionID());
        Check(TEXT("construction and cascade are one undo action"),R->BuildHistory.Num()==1 && R->BuildHistory[0]->Members.Num()==4);
        if(!R->BuildHistory.IsEmpty()) Built=R->BuildHistory[0]->Members;
        bool Paid=!Costs.IsEmpty();for(int32 I=0;I<Costs.Num();++I)Paid &= Before[I]-P->GetInventory()->GetNumItems(Costs[I].ItemClass)==Costs[I].Amount*4;
        Check(TEXT("each generated building pays normal material cost"),Paid);
        bool Positive=true;for(const auto& B:Built)Positive &= B.Transform.GetScale3D().GetMin()>0;
        Check(TEXT("copies have positive scale"),Positive && Built.Num()==4);
        if(Built.Num()==4) Check(TEXT("mirror copy matches reflected original pose"),Built[1].Transform.Equals(Mirror.Pose(Built[0].Transform),.1));
        auto* Proxy=Built.IsEmpty()?nullptr:Built[0].Blueprint(W);bool Together=Proxy!=nullptr;
        for(const auto& B:Built)Together &= B.Blueprint(W)==Proxy;
        Check(TEXT("shared blueprint option combines original and entire cascade"),Together && Proxy->AreProxyBuildingsRegisteredAndValid());
    }
    if(Phase==7){Gun->SetActorTickEnabled(true);Key(EKeys::LeftControl,true);Key(EKeys::Z,true);}
    if(Phase==8)
    {
        Key(EKeys::Z,false);Key(EKeys::LeftControl,false);bool Gone=!Built.IsEmpty();for(const auto& B:Built)Gone &= !B.IsAlive(W);
        Check(TEXT("one physical undo removes original and all transformer copies"),Gone && R->BuildHistory.IsEmpty());R->Transformers.Reset();R->TransformersBuildAsBlueprint=false;
        Check(TEXT("anchor remains after undoing replicated construction"),Anchor.IsAlive(W));
        E->NextTransformerPreview=0;E->TickTransformers();bool Hidden=true;for(auto& C:E->TransformerPreview)Hidden &= !C->IsVisible();Check(TEXT("clearing list hides copy holograms"),Hidden);
    }
    if(Phase==9)
    {
        auto* Factory=AFGBuildableSubsystem::Get(W);
        auto* PipeClass=LoadClass<AFGBuildablePipeline>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/Pipeline/Build_Pipeline.Build_Pipeline_C"));
        auto PipeRecipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_Pipeline.Recipe_Pipeline_C"));
        AFGRecipeManager::Get(W)->AddAvailableRecipe(PipeRecipe);Network.Reset();AFGBuildablePipeline* Previous=nullptr;
        for(int32 I=0;I<2;++I)
        {
            auto* Pipe=Cast<AFGBuildablePipeline>(Factory->BeginSpawnBuildable(PipeClass,FTransform(Base+FVector(I*1000,3000+I*400,500))));
            if(!Pipe)return false;Pipe->SetBuiltWithRecipe(PipeRecipe);
            *Pipe->GetMutableSplinePointData()={FSplinePointData(FVector::ZeroVector,FVector(1000,400,0)),FSplinePointData(FVector(1000,400,0),FVector(1000,400,0))};
            Pipe->FinishSpawning(Pipe->GetActorTransform());if(Previous)Previous->GetPipeConnection1()->SetConnection(Pipe->GetPipeConnection0());Previous=Pipe;
            Network.Add(FBSBuildingRef::FromActor(Pipe));
        }
        W->GetGameState<AFGGameState>()->SetCheatNoCost(true);TArray<FBSBuildingRef> Copy;FString Error;
        const bool OK=FBSTransformers::Copy(R,Network,Mirror,Copy,Error);Check(TEXT("connected pipe batch mirrors"),OK && Copy.Num()==2);
        auto* A=Copy.Num()>0?Cast<AFGBuildablePipeline>(Copy[0].Actor):nullptr;auto* B=Copy.Num()>1?Cast<AFGBuildablePipeline>(Copy[1].Actor):nullptr;
        Check(TEXT("copied pipe connections are reciprocal and isolated"),A && B && A->GetPipeConnection1()->GetConnection()==B->GetPipeConnection0() && B->GetPipeConnection0()->GetConnection()==A->GetPipeConnection1());
        auto* S0=Cast<AFGBuildablePipeline>(Network[0].Actor);auto* S1=Cast<AFGBuildablePipeline>(Network[1].Actor);
        Check(TEXT("original pipe network remains unchanged"),S0->GetPipeConnection1()->GetConnection()==S1->GetPipeConnection0());
        if(A)Check(TEXT("mirrored pipe has expected world endpoints"),A->GetPipeConnection0()->GetComponentLocation().Equals(Mirror.Point(S0->GetPipeConnection0()->GetComponentLocation()),.1) && A->GetPipeConnection1()->GetComponentLocation().Equals(Mirror.Point(S0->GetPipeConnection1()->GetComponentLocation()),.1));
        FString MatchMessage;const auto PipeMatches=FBSTransformers::Matching(W,{Mirror},Network,MatchMessage);
        Check(TEXT("matching finds both existing mirrored pipe geometries"),Copy.Num()==2 && PipeMatches.Num()==2 && PipeMatches.Contains(Copy[0]) && PipeMatches.Contains(Copy[1]));
        if(A)
        {
            const auto SavedPoints=*A->GetMutableSplinePointData();A->GetMutableSplinePointData()->Last().Location.Z+=10;
            Check(TEXT("same pipe pose with different route geometry is rejected"),!FBSTransformers::Matching(W,{Mirror},Network,MatchMessage).Contains(Copy[0]));
            *A->GetMutableSplinePointData()=SavedPoints;
        }
        if(!OK)UE_LOG(LogBuildingSkills,Error,TEXT("Transformer test copy: %s"),*Error);
    }
    if(Phase==10)
    {
        auto* Factory=AFGBuildableSubsystem::Get(W);
        auto Spawn=[&](const TCHAR* Directory,const TCHAR* Name,const TCHAR* RecipeName,FVector Offset,bool Deferred=false)
        {
            auto* Class=LoadClass<AFGBuildable>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/%s/%s.%s_C"),Directory,Name,Name));
            auto RC=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/%s.%s_C"),RecipeName,RecipeName));
            auto* B=Class && RC?Factory->BeginSpawnBuildable(Class,FTransform(Base+Offset)):nullptr;
            if(B){B->SetBuiltWithRecipe(RC);AFGRecipeManager::Get(W)->AddAvailableRecipe(RC);if(!Deferred)B->FinishSpawning(B->GetActorTransform());}return B;
        };
        auto* A=Spawn(TEXT("ConstructorMk1"),TEXT("Build_ConstructorMk1"),TEXT("Recipe_ConstructorMk1"),FVector(1000,2000,1000));
        auto* B=Spawn(TEXT("ConstructorMk1"),TEXT("Build_ConstructorMk1"),TEXT("Recipe_ConstructorMk1"),FVector(-1000,2000,1000));
        TArray<FBSBuildingRef> CA,CB,CW;FString Error;
        bool OK=A && B && FBSTransformers::Copy(R,{FBSBuildingRef::FromActor(A)},Mirror,CA,Error) && FBSTransformers::Copy(R,{FBSBuildingRef::FromActor(B)},Mirror,CB,Error);
        Check(TEXT("separate machine constructions can each be mirrored"),OK && CA.Num()==1 && CB.Num()==1);
        if(!OK || CA.Num()!=1 || CB.Num()!=1)return false;
        auto* PA=A->FindComponentByClass<UFGPowerConnectionComponent>();auto* PB=B->FindComponentByClass<UFGPowerConnectionComponent>();
        auto* QA=CA[0].Actor->FindComponentByClass<UFGPowerConnectionComponent>();auto* QB=CB[0].Actor->FindComponentByClass<UFGPowerConnectionComponent>();
        auto* Wire=Cast<AFGBuildableWire>(Spawn(TEXT("PowerLine"),TEXT("Build_PowerLine"),TEXT("Recipe_PowerLine"),FVector(0,2000,1000),true));
        if(!Wire || !PA || !PB || !QA || !QB)return false;
        Wire->Connect(PA,PB);Wire->FinishSpawning(Wire->GetActorTransform());
        OK=FBSTransformers::Copy(R,{FBSBuildingRef::FromActor(Wire)},Mirror,CW,Error);
        auto* CopyWire=CW.Num()==1?Cast<AFGBuildableWire>(CW[0].Actor):nullptr;
        Check(TEXT("later cable connects the corresponding mirrored machines"),OK && CopyWire && CopyWire->GetConnection(0)==QA && CopyWire->GetConnection(1)==QB);
        Check(TEXT("later cable preserves original machine circuit connections"),Wire->GetConnection(0)==PA && Wire->GetConnection(1)==PB && PA->GetNumConnections()==1 && PB->GetNumConnections()==1);
        Machines={FBSBuildingRef::FromActor(A),FBSBuildingRef::FromActor(B),CA[0],CB[0],FBSBuildingRef::FromActor(Wire),CW[0]};
        FString MatchMessage;const auto WireMatches=FBSTransformers::Matching(W,{Mirror},{FBSBuildingRef::FromActor(Wire)},MatchMessage);
        Check(TEXT("matching finds an existing mirrored cable through its endpoints"),WireMatches.Num()==1 && WireMatches[0].Actor==CopyWire);
        FBSTransformer Missing=Mirror;Missing.Frame.AddToTranslation(FVector(20000,0,0));TArray<FBSBuildingRef> Rejected;
        Check(TEXT("missing repeated cable endpoints fail without leaving an orphan"),!FBSTransformers::Copy(R,{FBSBuildingRef::FromActor(Wire)},Missing,Rejected,Error) && Rejected.IsEmpty());
        if(!OK)UE_LOG(LogBuildingSkills,Error,TEXT("Transformer power test: %s"),*Error);
    }
    if(Phase==11)
    {
        TArray<FBSBuildingRef> Copy;FString Error;
        Check(TEXT("identical mirror-plane foundation is skipped before creating an ambiguous duplicate"),!FBSTransformers::Copy(R,{Anchor},Mirror,Copy,Error) && Copy.IsEmpty() && Anchor.IsAlive(W));
    }
    if(Phase==12)
    {
        const FTransform Pose(FRotator(0,23,0),Base+FVector(-1500,2500,1500));
        auto* Proxy=W->SpawnActorDeferred<AFGBlueprintProxy>(AFGBlueprintProxy::StaticClass(),Pose);
        if(!Proxy)return false;
        if(auto* F=FindFProperty<FTextProperty>(Proxy->GetClass(),TEXT("mBlueprintName")))F->SetPropertyValue_InContainer(Proxy,FText::FromString(TEXT("Transformer test")));
        if(auto* F=FindFProperty<FStructProperty>(Proxy->GetClass(),TEXT("mLocalBounds")))*F->ContainerPtrToValuePtr<FBox>(Proxy)=FBox(FVector(-400,-400,-100),FVector(1200,400,0));
        TArray<FBSBuildingRef> Source;
        for(int32 I=0;I<2;++I)
        {
            FRuntimeBuildableInstanceData D;D.Transform=FTransform(FVector(I*800,0,0))*Pose;D.BuiltWithRecipe=Recipe;D.BlueprintProxy=Proxy;
            D.CustomizationData.SwatchDesc=Anchor.LightweightClass->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(W);
            const int32 Index=LW->AddFromBuildableInstanceData(Anchor.LightweightClass,D);Proxy->RegisterLightweightInstance(Anchor.LightweightClass,Index);
            Source.Add(FBSBuildingRef::FromLightweight(W,Anchor.LightweightClass,Index));
        }
        Proxy->FinishSpawning(Pose);
        auto* Groups=ABSGroupSubsystem::Get(W);FBSGroup Parent,Child;Parent.Id=FGuid::NewGuid();Child.Id=FGuid::NewGuid();Child.Parent=Parent.Id;
        Parent.Buildings={Source[0]};Child.Buildings={Source[1]};Parent.Pivot=Child.Pivot=Pose.GetLocation();
        Groups->Groups.Add(Parent);Groups->Groups.Add(Child);const int32 GroupCount=Groups->Groups.Num();
        TArray<FBSBuildingRef> Copy;FString Error;bool OK=FBSTransformers::Copy(R,Source,Mirror,Copy,Error);
        auto* NewProxy=Copy.Num()==2?Copy[0].Blueprint(W):nullptr;
        Check(TEXT("whole blueprint retains a separate native selection identity"),OK && NewProxy && NewProxy!=Proxy && Copy[1].Blueprint(W)==NewProxy && NewProxy->GetActorTransform().Equals(Mirror.Pose(Pose),.1));
        Check(TEXT("nested groups are preserved by transformer copies"),Groups->Groups.Num()==GroupCount+2 && Groups->Groups.Last().Parent==Groups->Groups[GroupCount].Id);
        TArray<FBSBuildingRef> Combined=Source;Combined.Append(Copy);Combined.Append(Network);
        const bool Joined=FBSTransformers::BuildAsBlueprint(R,Combined);
        auto* JoinedProxy=Source[0].Blueprint(W);bool Same=Joined && JoinedProxy;
        for(const auto& Ref:Combined)Same &= Ref.Blueprint(W)==JoinedProxy;
        Check(TEXT("blueprint assignment merges native blueprints and actor buildings"),Same && JoinedProxy->AreProxyBuildingsRegisteredAndValid());
        Check(TEXT("old blueprint proxies are removed and nested groups unchanged"),!IsValid(Proxy) && !IsValid(NewProxy) && Groups->Groups.Num()==GroupCount+2);
        auto* Clipboard=NewObject<UBSClipboard>(R);Clipboard->RequestId=FGuid::NewGuid();FBSSelection Selection;Selection.Buildings=Source;
        if(!Clipboard->Capture(Selection,Error,{},false)){Check(TEXT("blueprint preview archive captured"),false);return false;}
        R->Clipboards.Add(Clipboard);R->Transformers={Mirror};
        auto* State=Cast<UFGBuildGunStateBuild>(Gun->GetBuildGunStateFor(EBuildGunState::BGS_BUILD));
        State->SetActiveBlueprintDescriptor(Clipboard->Descriptor);
        AFGBlueprintSubsystem::Get(W)->SetActiveBlueprintHologramDescriptor(Clipboard->Descriptor);
        Gun->GotoBuildState(UFGBlueprintSettings::Get()->mBlueprintRecipeClass.LoadSynchronous());
    }
    if(Phase==13)
    {
        auto* State=Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState());auto* H=State?Cast<AFGBlueprintHologram>(State->GetHologram()):nullptr;
        Check(TEXT("native clipboard blueprint hologram equipped"),H!=nullptr);if(!H)return false;
        auto* Support=Anchor.ResolveActor(W);const FVector Point=Base+FVector(0,0,50);
        FHitResult Hit(Support,Cast<UPrimitiveComponent>(Support->GetRootComponent()),Point,FVector::UpVector);
        Hit.bBlockingHit=true;Hit.Location=Hit.ImpactPoint=Point;Hit.Normal=Hit.ImpactNormal=FVector::UpVector;H->UpdateHologramPlacement(Hit);
        Gun->SetActorTickEnabled(false);H->SetActorTransform(FTransform(FRotator(0,20,0),Base+FVector(800,900,600)));
        H->ResetConstructDisqualifiers();H->SetPlacementMaterialState(EHologramMaterialState::HMS_OK);
        E->NextTransformerPreview=0;E->TickTransformers();int32 Visible=0;for(auto& C:E->TransformerPreview)Visible+=C->IsVisible() && !C->bHiddenInGame;
        Check(TEXT("blueprint ghost includes multiple building meshes"),Visible>=2);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BuildingSkillsTransformersBlueprintPreview.png"),true,false);
    }
    if(Phase==14){Gun->SetActorTickEnabled(true);R->Transformers.Reset();E->NextTransformerPreview=0;E->TickTransformers();}
    if(Phase==15)
    {
        Swatch=LoadClass<UFGCustomizationRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/-Shared/Customization/Swatches/Recipe_Swatch_Slot1.Recipe_Swatch_Slot1_C"));
        Material=LoadClass<UFGCustomizationRecipe>(nullptr,TEXT("/Game/FactoryGame/Buildable/-Shared/Customization/Materials/Recipe_Material_Foundation_Concrete.Recipe_Material_Foundation_Concrete_C"));
        auto Desc=Material?UFGCustomizationRecipe::GetCustomizationDescriptor(Material):nullptr;
        auto* Mat=Desc?Cast<UFGFactoryCustomizationDescriptor_Material>(Desc.GetDefaultObject()):nullptr;
        if(Mat) if(const auto* RC=Mat->GetBuildableMap().Find(Anchor.LightweightClass)) ConcreteRecipe=*RC;
        ConcreteClass=ConcreteRecipe?AFGBuildable::GetBuildableClassFromRecipe(ConcreteRecipe):nullptr;
        Check(TEXT("paint material variant fixtures load"),Swatch && Material && ConcreteClass && ConcreteRecipe);
        if(!Swatch || !Material || !ConcreteClass)return false;
        AFGRecipeManager::Get(W)->AddAvailableRecipe(Swatch);AFGRecipeManager::Get(W)->AddAvailableRecipe(Material);AFGRecipeManager::Get(W)->AddAvailableRecipe(ConcreteRecipe);
        R->Transformers={Mirror};R->TransformersBuildAsBlueprint=false;R->DeleteHistory.Reset();++R->TransformerRevision;
        ActionRefs.Reset();
        const FTransform Pose(FRotator(0,17,0),Base+FVector(0,1200,1800));
        for(int32 I=0;I<3;++I)
        {
            const auto Class=I==1?ConcreteClass:Anchor.LightweightClass;
            FRuntimeBuildableInstanceData D;D.Transform=I?Mirror.Pose(Pose):Pose;if(I==2)D.Transform.SetScale3D(FVector(2));
            D.BuiltWithRecipe=I==1?ConcreteRecipe:Recipe;
            D.CustomizationData.SwatchDesc=I==1?UFGCustomizationRecipe::GetCustomizationDescriptor(Swatch):Class->GetDefaultObject<AFGBuildable>()->GetDefaultSwatchCustomizationOverride(W);
            ActionRefs.Add(FBSBuildingRef::FromLightweight(W,Class,LW->AddFromBuildableInstanceData(Class,D)));
        }
        FString Message;auto Matches=FBSTransformers::Matching(W,R->Transformers,{ActionRefs[0]},Message);
        Check(TEXT("matching ignores material and swatch but rejects different scale"),Matches.Num()==1 && Matches[0]==ActionRefs[1]);
        FRuntimeBuildableInstanceData Duplicate;Duplicate.Transform=ActionRefs[1].Transform;Duplicate.BuiltWithRecipe=ConcreteRecipe;
        const int32 DuplicateIndex=LW->AddFromBuildableInstanceData(ConcreteClass,Duplicate);
        Check(TEXT("ambiguous overlapping counterparts are skipped"),FBSTransformers::Matching(W,R->Transformers,{ActionRefs[0]},Message).IsEmpty() && !Message.IsEmpty());
        LW->RemoveByInstanceIndex(ConcreteClass,DuplicateIndex);
        auto Disabled=Mirror;Disabled.RepeatPlayer=false;
        Check(TEXT("matching honors repeat-player and effect radius"),FBSTransformers::Matching(W,{Disabled},{ActionRefs[0]},Message).IsEmpty());
        Disabled=Mirror;Disabled.Radius=.1;
        Check(TEXT("out-of-radius actions have no matching counterparts"),FBSTransformers::Matching(W,{Disabled},{ActionRefs[0]},Message).IsEmpty());
        E->bConnectedPaint=false;Gun->GotoPaintState(Swatch,true);
    }
    if(Phase==16 && ActionRefs.Num()==3)
    {
        // Paint the concrete side, whose counterpart starts with a different swatch.
        auto* Paint=Cast<UFGBuildGunStatePaint>(Gun->GetCurrentState());
        auto* Aim=FindFProperty<FObjectProperty>(UFGBuildGunStatePaint::StaticClass(),TEXT("mCurrentlyAimedAtActor"));
        if(!Paint || !Aim)return false;
        Gun->SetActorTickEnabled(false);Aim->SetObjectPropertyValue_InContainer(Paint,ActionRefs[1].ResolveActor(W));
        FBSConnectedPaint::TickPreview(E,Paint);
        Check(TEXT("default spread previews the matching transformer counterpart"),E->ConnectedPaintPreviewMembers.Contains(ActionRefs[0]));
        Gun->OnPrimaryFirePressed();Gun->OnPrimaryFireReleased();
        const auto Desc=UFGCustomizationRecipe::GetCustomizationDescriptor(Swatch);
        bool Colored=true;for(int32 I=0;I<2;++I){auto* D=LW->GetRuntimeDataForBuildableClassAndIndex(ActionRefs[I].LightweightClass,ActionRefs[I].ResolveIndex(W));Colored &= D && D->CustomizationData.SwatchDesc==Desc;}
        Check(TEXT("native primary fire paints counterpart across material variants"),Colored);
        auto* Other=LW->GetRuntimeDataForBuildableClassAndIndex(ActionRefs[2].LightweightClass,ActionRefs[2].ResolveIndex(W));
        Check(TEXT("paint leaves nonmatching overlapping building unchanged"),Other && Other->CustomizationData.SwatchDesc!=Desc);
        Gun->GotoPaintState(Material,true);
    }
    if(Phase==17 && ActionRefs.Num()==3)
    {
        R->Server_PaintTransformers_Implementation(ActionRefs[0],Material);
        Check(TEXT("transformer material application replaces original variant"),!ActionRefs[0].IsAlive(W));
        TArray<FBSBuildingRef> Nearby;int32 Examined=0;
        FBSTransformNative::QueryLightweightBounds(W,FBox(ActionRefs[0].Location()-FVector(500),ActionRefs[0].Location()+FVector(500)),Nearby,Examined);
        auto* New=Nearby.FindByPredicate([&](const auto& Ref){return Ref.LightweightClass==ConcreteClass && Ref.Transform.Equals(ActionRefs[0].Transform,.01);});
        const auto* Data=New?LW->GetRuntimeDataForBuildableClassAndIndex(New->LightweightClass,New->ResolveIndex(W)):nullptr;
        Check(TEXT("replacement retains pose and persistent swatch"),Data && Data->CustomizationData.SwatchDesc==UFGCustomizationRecipe::GetCustomizationDescriptor(Swatch));
        UE_LOG(LogBuildingSkills,Display,TEXT("TRANSFORMER MATERIAL: replacement=%d swatch=%s expected=%s"),New!=nullptr,Data?*GetNameSafe(Data->CustomizationData.SwatchDesc):TEXT("missing"),*GetNameSafe(UFGCustomizationRecipe::GetCustomizationDescriptor(Swatch)));
        if(New)ActionRefs[0]=*New;
        Check(TEXT("counterpart with requested material stays intact"),ActionRefs[1].IsAlive(W));
        Gun->SetActorTickEnabled(true);Gun->GotoDismantleState();
        auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState());if(State)Gun->SetCurrentBuildGunMode(State->mDefaultDismantleMode);
    }
    if(Phase==18 && ActionRefs.Num()==3)
    {
        auto* State=Cast<UFGBuildGunStateDismantle>(Gun->GetCurrentState());
        FDismantleLightweightBundle Bundle(ActionRefs[0].LightweightClass);Bundle.RemovalIndices={ActionRefs[0].ResolveIndex(W)};
        if(State)State->Server_DismantleActors_Implementation({}, {Bundle});
        Check(TEXT("native deletion also removes matching counterpart"),!ActionRefs[0].IsAlive(W) && !ActionRefs[1].IsAlive(W));
        Check(TEXT("replicated deletion preserves nonmatching object and records one undo"),ActionRefs[2].IsAlive(W) && R->DeleteHistory.Num()==1 && R->DeleteHistory[0]->Members.Num()==2);
    }
    if(Phase==19 && ActionRefs.Num()==3)
    {
        R->Server_UndoGunAction_Implementation(true);
        Check(TEXT("one deletion undo restores both counterparts"),ActionRefs[0].IsAlive(W) && ActionRefs[1].IsAlive(W) && R->DeleteHistory.IsEmpty());
        bool Visible=true;for(int32 I=0;I<2;++I){auto* D=LW->GetRuntimeDataForBuildableClassAndIndex(ActionRefs[I].LightweightClass,ActionRefs[I].ResolveIndex(W));Visible &= D && !D->Handles.IsEmpty() && D->Handles[0]->IsInstanced();}
        Check(TEXT("replicated deletion undo restores live instance handles immediately"),Visible);
        R->Transformers.Reset();++R->TransformerRevision;
    }
    if(Phase>=20 && Phase<=23)
    {
        Gun->GotoDismantleState();Gun->SetCurrentBuildGunMode(UBSEditModeDescriptor::StaticClass());
        auto* Groups=ABSGroupSubsystem::Get(W);R->Transformers={Mirror};
        auto Refresh=[&](const FBSBuildingRef& Ref){return Ref.LightweightClass?FBSBuildingRef::FromLightweight(W,Ref.LightweightClass,Ref.RuntimeIndex):FBSBuildingRef::FromActor(Ref.Actor);};
        auto Move=[&](const TArray<FBSBuildingRef>& Refs,EBSTransformMode Mode,double Amount,FVector Delta,FBSSelection& Result)
        {
            FBSSelection Selection;for(const auto& Ref:Refs)Selection.Buildings.Add(Refresh(Ref));
            FBSTransformOptions Options;Options.Pivot=EBSTransformPivot::Individual;Options.Space=EBSTransformSpace::World;Options.Gesture=FGuid::NewGuid();
            FString Error;const bool OK=Groups->TransformSelection(P,Selection,Groups->Revision,Mode,Amount,Delta,Result,Error,&Options);
            if(!OK)UE_LOG(LogBuildingSkills,Error,TEXT("Transformer movement regression: %s"),*Error);
            return OK;
        };
        auto Undo=[&](){FBSSelection Result;FString Error;return Groups->RestoreTransform(P,false,Result,Error);};
        if(Phase==20 && ActionRefs.Num()==3)
        {
            for(auto& Ref:ActionRefs)Ref.RuntimeIndex=Ref.ResolveIndex(W);
            const auto A=Refresh(ActionRefs[0]),B=Refresh(ActionRefs[1]),Other=Refresh(ActionRefs[2]);
            for(int32 I=-1;I<10;++I)
            {
                Groups->TransformHistory.Reset();FBSSelection Result;
                const auto Mode=I<0?EBSTransformMode::RotateX:EBSTransformMode(I);
                const bool OK=Move({A},Mode,I<3?15:1.1,I<0?FVector(75,120,50):FVector::ZeroVector,Result);
                const auto After=Refresh(A),Counterpart=Refresh(B);
                Check(*FString::Printf(TEXT("movement mode %d repeats matching pose and preserves explicit selection"),I),OK && Counterpart.Transform.Equals(Mirror.Pose(After.Transform),.01) && Result.Buildings.Num()==1 && Groups->TransformHistory.Num()==1 && Refresh(Other).Transform.Equals(Other.Transform,.001));
                Check(*FString::Printf(TEXT("movement mode %d undo restores both poses"),I),Undo() && Refresh(A).Transform.Equals(A.Transform,.01) && Refresh(B).Transform.Equals(B.Transform,.01));
                if(!OK)break;
            }
            FBSSelection Result;FString Error;
            Check(TEXT("explicitly selected counterparts receive only the requested translation"),Move({A,B},EBSTransformMode::RotateX,0,FVector(0,100,0),Result) && Refresh(A).Location().Equals(A.Location()+FVector(0,100,0),.01) && Refresh(B).Location().Equals(B.Location()+FVector(0,100,0),.01));
            Check(TEXT("explicit pair undo succeeds"),Undo());
            R->Transformers[0].RepeatPlayer=false;
            Check(TEXT("disabled repeat-player leaves counterpart stationary"),Move({A},EBSTransformMode::RotateX,0,FVector(0,100,0),Result) && Refresh(B).Transform.Equals(B.Transform,.01));
            Check(TEXT("single move undo succeeds"),Undo());R->Transformers={Mirror};
            Check(TEXT("repeated translation is recorded"),Move({A},EBSTransformMode::RotateX,0,FVector(30,40,50),Result));
            const auto Moved=Refresh(B).Transform;
            Check(TEXT("translation undo succeeds before redo"),Undo());
            R->Transformers.Reset();
            Check(TEXT("redo restores recorded counterpart even after transformers are removed"),Groups->RestoreTransform(P,true,Result,Error) && Refresh(B).Transform.Equals(Moved,.01) && Result.Buildings.Num()==1);
            Check(TEXT("redo can be undone as one action"),Undo());
        }
        if(Phase==21 && Machines.Num()==6)
        {
            TArray<FTransform> MachineBefore;for(const auto& Ref:Machines)MachineBefore.Add(Refresh(Ref).Transform);
            const auto CableBeforeA=FBSTransformNative::Capture(Machines[4].Actor),CableBeforeB=FBSTransformNative::Capture(Machines[5].Actor);
            FBSSelection Result;const bool OK=Move({Machines[0]},EBSTransformMode::RotateX,0,FVector(100,200,50),Result);
            Check(TEXT("moving constructor repeats motion and keeps other machines fixed"),OK && Refresh(Machines[2]).Transform.Equals(FBSTransformers::BuildingPose(Mirror,Refresh(Machines[0]).Transform,Machines[0].Actor),.01) && Refresh(Machines[1]).Transform.Equals(MachineBefore[1],.01) && Refresh(Machines[3]).Transform.Equals(MachineBefore[3],.01));
            bool Connected=true;
            for(int32 I=0;I<2;++I){auto* Wire=Cast<AFGBuildableWire>(Machines[4+I].Actor);auto* A=Machines[I*2].Actor->FindComponentByClass<UFGPowerConnectionComponent>();auto* B=Machines[I*2+1].Actor->FindComponentByClass<UFGPowerConnectionComponent>();Connected &= Wire && Wire->GetConnection(0)==A && Wire->GetConnection(1)==B;}
            Check(TEXT("original and repeated cables remain attached to their own machines"),Connected);
            const bool Restored=Undo();bool Same=Restored;for(int32 I=0;I<4;++I)Same &= Refresh(Machines[I]).Transform.Equals(MachineBefore[I],.01);
            Check(TEXT("one undo restores both connected constructor networks"),Same);
            Check(TEXT("constructor undo restores both cable geometries"),FBSTransformNative::Matches(W,CableBeforeA) && FBSTransformNative::Matches(W,CableBeforeB));
        }
        if(Phase==22 && Network.Num()==2)
        {
            FString Message;const auto Matches=FBSTransformers::Matching(W,{Mirror},{Refresh(Network[0])},Message);
            Check(TEXT("pipe counterpart is found before movement"),Matches.Num()==1);
            if(Matches.Num()==1)
            {
                auto* A=Cast<AFGBuildablePipeline>(Network[0].Actor);auto* B=Cast<AFGBuildablePipeline>(Matches[0].Actor);
                const auto BeforeA=FBSTransformNative::Capture(A),BeforeB=FBSTransformNative::Capture(B);
                FBSSelection Result;const bool OK=Move({Network[0]},EBSTransformMode::RotateX,0,FVector(100,0,50),Result);
                Check(TEXT("moving a pipe repeats the connected counterpart"),OK && B->GetActorTransform().Equals(Mirror.Pose(A->GetActorTransform()),.01));
                const bool Restored=Undo();
                Check(TEXT("pipe movement undo restores both native poses and spline geometry"),Restored && FBSTransformNative::Matches(W,BeforeA) && FBSTransformNative::Matches(W,BeforeB));
            }
        }
        if(Phase==23)
        {
            FBSTransformer Turn=Mirror;Turn.Id=FGuid::NewGuid();Turn.Mirror=false;Turn.Rotation=true;Turn.Angle=90;Turn.RepeatTransformers=true;
            R->Transformers={Mirror,Turn};
            const FTransform Source(FRotator(0,31,0),Base+FVector(2100,1700,2800));
            TArray<FTransform> Poses={Source,Mirror.Pose(Source),Turn.Pose(Source),Turn.Pose(Mirror.Pose(Source))};TArray<FBSBuildingRef> Refs;
            for(const auto& Pose:Poses){FRuntimeBuildableInstanceData D;D.Transform=Pose;D.BuiltWithRecipe=Recipe;Refs.Add(FBSBuildingRef::FromLightweight(W,Anchor.LightweightClass,LW->AddFromBuildableInstanceData(Anchor.LightweightClass,D)));}
            FBSSelection Result;const bool OK=Move({Refs[0]},EBSTransformMode::RotateZ,15,FVector::ZeroVector,Result);
            const auto Moved=Refresh(Refs[0]).Transform;
            Check(TEXT("ordered transformers repeat rotation through earlier counterparts"),OK && Refresh(Refs[1]).Transform.Equals(Mirror.Pose(Moved),.01) && Refresh(Refs[2]).Transform.Equals(Turn.Pose(Moved),.01) && Refresh(Refs[3]).Transform.Equals(Turn.Pose(Mirror.Pose(Moved)),.01));
            const bool Restored=Undo();bool Same=Restored;for(int32 I=0;I<Refs.Num();++I)Same &= Refresh(Refs[I]).Transform.Equals(Poses[I],.01);
            Check(TEXT("one undo restores the whole ordered transformer chain"),Same);
        }
        R->Transformers.Reset();++R->TransformerRevision;
    }
    if(Phase>=24 && Phase<=26)
    {
        auto* Factory=AFGBuildableSubsystem::Get(W);
        auto Spawn=[&](const TCHAR* Directory,const TCHAR* Name,const TCHAR* RecipeName,const FTransform& Pose,bool Deferred=false)->AFGBuildable*
        {
            auto* Class=LoadClass<AFGBuildable>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/%s/%s.%s_C"),Directory,Name,Name));
            auto RC=LoadClass<UFGRecipe>(nullptr,*FString::Printf(TEXT("/Game/FactoryGame/Recipes/Buildings/%s.%s_C"),RecipeName,RecipeName));
            auto* B=Class && RC?Factory->BeginSpawnBuildable(Class,Pose):nullptr;
            if(B){B->SetBuiltWithRecipe(RC);AFGRecipeManager::Get(W)->AddAvailableRecipe(RC);if(!Deferred)B->FinishSpawning(Pose);}return B;
        };
        auto Port=[&](AFGBuildable* B,EFactoryConnectionDirection Direction)->UFGFactoryConnectionComponent*
        {if(B)for(auto* C:TInlineComponentArray<UFGFactoryConnectionComponent*>(B))if(C->GetDirection()==Direction)return C;return nullptr;};
        auto Aligned=[&](AFGBuildable* Source,AFGBuildable* Target)
        {
            if(!Source || !Target)return false;
            for(auto* A:TInlineComponentArray<UFGFactoryConnectionComponent*>(Source))
            {
                int32 Count=0;for(auto* B:TInlineComponentArray<UFGFactoryConnectionComponent*>(Target))
                    Count+=A->GetDirection()==B->GetDirection() && B->GetComponentLocation().Equals(Mirror.Point(A->GetComponentLocation()),.1) && B->GetForwardVector().Equals(Mirror.Vector(A->GetForwardVector()),.0001);
                if(Count!=1)return false;
            }
            return true;
        };
        if(Phase==24)
        {
            for(double Yaw:{0.,37.,180.})
            {
                auto* A=Spawn(TEXT("ConstructorMk1"),TEXT("Build_ConstructorMk1"),TEXT("Recipe_ConstructorMk1"),FTransform(FRotator(0,Yaw,0),Base+FVector(3000,2000,4000+Yaw*10)));
                TArray<FBSBuildingRef> Copy;FString Error;
                const bool OK=A && FBSTransformers::Copy(R,{FBSBuildingRef::FromActor(A)},Mirror,Copy,Error);
                auto* B=Copy.Num()==1?Copy[0].Actor.Get():nullptr;
                Check(*FString::Printf(TEXT("constructor yaw %.0f mirrors input and output positions and directions"),Yaw),OK && Aligned(A,B));
                if(Yaw==180 && A && B)
                {
                    auto* OutA=Port(A,EFactoryConnectionDirection::FCD_OUTPUT);auto* OutB=Port(B,EFactoryConnectionDirection::FCD_OUTPUT);
                    Check(TEXT("constructor outputs on both sides face toward the mirror"),OutA && OutB && FVector::DotProduct(OutA->GetForwardVector(),Mirror.Frame.GetLocation()-OutA->GetComponentLocation())>0 && FVector::DotProduct(OutB->GetForwardVector(),Mirror.Frame.GetLocation()-OutB->GetComponentLocation())>0);
                }
                if(!OK)UE_LOG(LogBuildingSkills,Error,TEXT("Port regression copy: %s"),*Error);
            }
        }
        if(Phase==25)
        {
            auto* A=Spawn(TEXT("ConstructorMk1"),TEXT("Build_ConstructorMk1"),TEXT("Recipe_ConstructorMk1"),FTransform(FRotator(0,180,0),Base+FVector(3200,4000,2500)));
            auto* B=Spawn(TEXT("ConstructorMk1"),TEXT("Build_ConstructorMk1"),TEXT("Recipe_ConstructorMk1"),FTransform(FRotator(0,180,0),Base+FVector(3200,2200,2500)));
            auto* Out=Port(A,EFactoryConnectionDirection::FCD_OUTPUT);auto* In=Port(B,EFactoryConnectionDirection::FCD_INPUT);
            if(!Out || !In){Check(TEXT("constructor belt fixture has ports"),false);return false;}
            const auto Start=Out->GetComponentLocation(),Delta=In->GetComponentLocation()-Start;
            auto* Belt=Cast<AFGBuildableConveyorBelt>(Spawn(TEXT("ConveyorBeltMk1"),TEXT("Build_ConveyorBeltMk1"),TEXT("Recipe_ConveyorBeltMk1"),FTransform(Start),true));
            if(!Belt)return false;
            *Belt->GetMutableSplinePointData()={FSplinePointData(FVector::ZeroVector,Delta),FSplinePointData(Delta,Delta)};
            Belt->FinishSpawning(Belt->GetActorTransform());Out->SetConnection(Belt->GetConnection0());In->SetConnection(Belt->GetConnection1());
            TArray<FBSBuildingRef> Copy;FString Error;const bool OK=FBSTransformers::Copy(R,{FBSBuildingRef::FromActor(A),FBSBuildingRef::FromActor(B),FBSBuildingRef::FromActor(Belt)},Mirror,Copy,Error);
            AFGBuildableConveyorBelt* Repeated=nullptr;for(const auto& Ref:Copy)if(auto* C=Cast<AFGBuildableConveyorBelt>(Ref.Actor))Repeated=C;
            Check(TEXT("constructor and belt batch mirrors"),OK && Copy.Num()==3 && Repeated);
            if(Repeated)
            {
                auto* C0=Repeated->GetConnection0();auto* C1=Repeated->GetConnection1();
                Check(TEXT("mirrored belt joins output to input on copied constructors"),C0->GetConnection() && C1->GetConnection() && C0->GetConnection()->GetDirection()==EFactoryConnectionDirection::FCD_OUTPUT && C1->GetConnection()->GetDirection()==EFactoryConnectionDirection::FCD_INPUT && C0->GetConnection()->GetOwner()!=A && C1->GetConnection()->GetOwner()!=B);
                Check(TEXT("mirrored belt endpoints meet their machine ports"),C0->GetConnection() && C1->GetConnection() && C0->GetComponentLocation().Equals(C0->GetConnection()->GetComponentLocation(),.1) && C1->GetComponentLocation().Equals(C1->GetConnection()->GetComponentLocation(),.1));
                Check(TEXT("mirrored connected belt remains matchable for later transformer actions"),FBSTransformers::Matching(W,{Mirror},{FBSBuildingRef::FromActor(Belt)},Error).Num()==1);
            }
            Check(TEXT("mirroring leaves original constructor belt connected"),Out->GetConnection()==Belt->GetConnection0() && In->GetConnection()==Belt->GetConnection1());
        }
        if(Phase==26)
        {
          for(int32 SmartCase=0;SmartCase<2;++SmartCase)
          {
            auto* Splitter=Spawn(SmartCase?TEXT("CA_SplitterSmart"):TEXT("CA_Splitter"),SmartCase?TEXT("Build_ConveyorAttachmentSplitterSmart"):TEXT("Build_ConveyorAttachmentSplitter"),SmartCase?TEXT("Recipe_ConveyorAttachmentSplitterSmart"):TEXT("Recipe_ConveyorAttachmentSplitter"),FTransform(FRotator(0,23,0),Base+FVector(-3200,2600,3000+SmartCase*1500)));
            if(!Splitter){Check(TEXT("splitter fixture loads"),false);return false;}
            struct FTestOutputOrder : AFGBuildableConveyorAttachment
            {static const TArray<UFGFactoryConnectionComponent*>& Get(const AFGBuildableConveyorAttachment* Owner){return Owner->*&FTestOutputOrder::mOutputs;}};
            auto* Smart=Cast<AFGBuildableSplitterSmart>(Splitter);
            if(Smart)Smart->SetSortRules({FSplitterSortRule(UFGRecipe::GetProducts(Recipe)[0].ItemClass,0)});
            TArray<FBSBuildingRef> Source{FBSBuildingRef::FromActor(Splitter)};
            for(auto* Out:TInlineComponentArray<UFGFactoryConnectionComponent*>(Splitter))if(Out->GetDirection()==EFactoryConnectionDirection::FCD_OUTPUT)
            {
                const auto Delta=Out->GetForwardVector()*1200;
                auto* Belt=Cast<AFGBuildableConveyorBelt>(Spawn(TEXT("ConveyorBeltMk1"),TEXT("Build_ConveyorBeltMk1"),TEXT("Recipe_ConveyorBeltMk1"),FTransform(Out->GetComponentLocation()),true));
                if(!Belt)return false;
                *Belt->GetMutableSplinePointData()={FSplinePointData(FVector::ZeroVector,Delta),FSplinePointData(Delta,Delta)};
                Belt->FinishSpawning(Belt->GetActorTransform());Out->SetConnection(Belt->GetConnection0());Source.Add(FBSBuildingRef::FromActor(Belt));
            }
            TArray<FBSBuildingRef> Copy;FString Error;const bool OK=FBSTransformers::Copy(R,Source,Mirror,Copy,Error);
            AFGBuildable* Target=nullptr;for(const auto& Ref:Copy)if(Ref.Actor && Ref.Actor->GetClass()==Splitter->GetClass())Target=Ref.Actor;
            Check(TEXT("splitter input and all three outputs mirror by role"),OK && Aligned(Splitter,Target));
            int32 Connected=0;for(const auto& Ref:Copy)if(auto* Belt=Cast<AFGBuildableConveyorBelt>(Ref.Actor))
            {auto* C=Belt->GetConnection0();if(C->GetConnection() && C->GetConnection()->GetOwner()==Target && C->GetConnection()->GetDirection()==EFactoryConnectionDirection::FCD_OUTPUT && C->GetComponentLocation().Equals(C->GetConnection()->GetComponentLocation(),.1))++Connected;}
            Check(TEXT("splitter side-output connections are remapped to matching mirrored ports"),OK && Connected==3);
            if(Smart)
            {
                auto* CopySmart=Cast<AFGBuildableSplitterSmart>(Target);bool RulesOK=CopySmart && CopySmart->GetNumSortRules()==Smart->GetNumSortRules();
                if(RulesOK)for(int32 I=0;I<Smart->GetNumSortRules();++I)
                {
                    const auto From=Smart->GetSortRuleAt(I),To=CopySmart->GetSortRuleAt(I);
                    const auto& OldOutputs=FTestOutputOrder::Get(Smart);const auto& NewOutputs=FTestOutputOrder::Get(CopySmart);
                    RulesOK &= From.ItemClass==To.ItemClass && OldOutputs.IsValidIndex(From.OutputIndex) && NewOutputs.IsValidIndex(To.OutputIndex) && NewOutputs[To.OutputIndex]->GetComponentLocation().Equals(Mirror.Point(OldOutputs[From.OutputIndex]->GetComponentLocation()),.1);
                }
                Check(TEXT("smart splitter filters follow their mirrored output ports"),RulesOK);
            }
          }
        }
    }
    return Passed;
 }
};
bool RunBuildingSkillsTransformerChecks(AFGCharacterPlayer* P,int32 Phase)
{check(FParse::Param(FCommandLine::Get(),TEXT("BuildingSkillsRuntimeTest")));return FBSTransformerSmoke::Run(P,Phase);}
