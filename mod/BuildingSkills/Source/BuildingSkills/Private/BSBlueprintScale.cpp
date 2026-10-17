#include "BSBlueprintScale.h"
#include "BSGroups.h"
#include "BSDeduplication.h"
#include "BuildingSkills.h"
#include "FGActorSaveHeaderTypes.h"
#include "FGBlueprintSubsystem.h"
#include "BlueprintArchiveObjectTOCProxy.h"
#include "BlueprintArchiveObjectDataProxy.h"
#include "FGCircuitConnectionComponent.h"
#include "FGConnectionComponent.h"
#include "UObject/UnrealType.h"
#include "SaveCustomVersion.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableBlueprintDesigner.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Patching/NativeHookManager.h"

namespace
{
    struct FBlueprintWriteContext { UWorld* World; bool Clipboard = false; TSet<const AFGBuildable*> Members; bool InvalidObject = false; };
    struct FBlueprintLoadContext
    {
        bool Preview = false;
        int32 ResetConveyorChains = 0;
        FTransform Origin;
        TOptional<FActorSaveHeader> Pending;
        TMap<AActor*, FVector> Spawning;
        TArray<TWeakObjectPtr<AFGBuildable>> Scaled;
        TSet<const AFGBuildable*> Members;
    };
    // Archive serialization is shared with world saves. Only change it inside
    // these synchronous blueprint calls; nested calls restore the outer context.
    thread_local FBlueprintWriteContext* BlueprintWrite = nullptr;
    thread_local FBlueprintLoadContext* BlueprintLoad = nullptr;
    thread_local bool TestVanillaLoad = false;
    using FReadObject = FArchive& (*)(FBlueprintArchiveObjectTOCProxy*, UObject*&);
    FReadObject NativeReadObject = nullptr;

    FArchive& ReadBlueprintObject(FBlueprintArchiveObjectTOCProxy* Archive, UObject*& Object)
    {
        if (!BlueprintLoad || !Archive->IsLoading()) return NativeReadObject(Archive, Object);
        TGuardValue<TOptional<FActorSaveHeader>> Pending(BlueprintLoad->Pending, {});
        const int64 Start = Archive->Tell();
        bool IsActor = false;
        FString Source;
        static_cast<FArchive&>(*Archive) << IsActor;
        if (IsActor)
        {
            FActorSaveHeader Header;
            Header.Serialize(*Archive, Archive->CustomVer(FSaveCustomVersion::GUID));
            Source = Header.ObjectHeader.Reference.ToString();
            if (!Archive->IsError()) BlueprintLoad->Pending = MoveTemp(Header);
        }
        else
        {
            FObjectSaveHeader Header;
            Header.Serialize(*Archive, Archive->CustomVer(FSaveCustomVersion::GUID));
            Source = Header.BaseHeader.Reference.ToString();
        }
        if (!Archive->IsError() && FBSDeduplication::SkipObject(Source))
        {
            // Preserve the TOC slot and its blob index, but create no object.
            // Native blueprint loading already supports null/blacklisted entries.
            Object = nullptr;
            return *Archive;
        }
        Archive->Seek(Start);
        FArchive& Result = NativeReadObject(Archive, Object);
        if (auto* Building = Cast<AFGBuildable>(Object))
        { BlueprintLoad->Members.Add(Building); FBSDeduplication::RecordSource(Building, Source, BlueprintLoad->Preview); }
        return Result;
    }

    using FSerializeDataObject = FArchive& (*)(FBlueprintArchiveObjectDataProxy*, UObject*&);
    FSerializeDataObject NativeSerializeDataObject = nullptr;
    bool BlueprintOwnsObject(UObject* Object, const TSet<const AFGBuildable*>& Members)
    {
        const auto* Building = Cast<AFGBuildable>(Object);
        if (!Building && Object) Building = Object->GetTypedOuter<AFGBuildable>();
        // Assets and world resources keep native behavior. Placed buildings and
        // their subobjects must refer only to members of this archive.
        return !Building || Members.Contains(Building);
    }
    FArchive& SerializeBlueprintDataObject(FBlueprintArchiveObjectDataProxy* Archive, UObject*& Object)
    {
        // This overload writes complete TOC objects, not references inside their
        // properties. Native dependency gathering may include an object outside
        // Members. Replacing it with null crashes FObjectWriterFName and also
        // breaks the correspondence between TOC entries and object data.
        // External connection references are cleaned on the loaded copy instead.
        if (Archive->IsSaving() && BlueprintWrite && !IsValid(Object))
        {
            BlueprintWrite->InvalidObject = true;
            Archive->SetError();
            UE_LOG(LogBuildingSkills, Warning, TEXT("Blueprint archive rejected an invalid bulk object; no snapshot will be retained."));
            return *Archive;
        }
        return NativeSerializeDataObject(Archive, Object);
    }

    void CleanLoadedNetworkReferences(AFGBuildable* Building)
    {
        if (!BlueprintLoad || !BlueprintLoad->Members.Contains(Building)) return;
        // A live-world snapshot contains SaveGame references to conveyor-chain
        // actors. These are simulation caches, never members of the blueprint.
        // In the source world the native reader can resolve them to the ORIGINAL
        // chain. Clear only the newly deserialized belt/lift, before BeginPlay;
        // native registration then builds its own chain from the copied ports.
        // Do not call RevertChainActor or SetConveyorChainActor here: either can
        // touch the original chain or rendering state while it is still in use.
        if (auto* Conveyor = Cast<AFGBuildableConveyorBase>(Building);
            Conveyor && !Conveyor->HasActorBegunPlay())
        {
            if (auto* Property = FindFProperty<FObjectPropertyBase>(Conveyor->GetClass(), TEXT("mConveyorChainActor")))
                if (Property->GetObjectPropertyValue_InContainer(Conveyor))
                {
                    ++BlueprintLoad->ResetConveyorChains;
                    Property->SetObjectPropertyValue_InContainer(Conveyor, nullptr);
                    if (auto* Index = FindFProperty<FIntProperty>(Conveyor->GetClass(), TEXT("mChainSegmentIndex")))
                        Index->SetPropertyValue_InContainer(Conveyor, INDEX_NONE);
                }
        }
        auto ClearForeignObject = [&](UObject* Owner, FName Name)
        {
            auto* Property = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), Name);
            if (!Property) return;
            for (int32 I = 0; I < Property->ArrayDim; ++I)
            {
                void* Value = Property->ContainerPtrToValuePtr<void>(Owner, I);
                UObject* Reference = Property->GetObjectPropertyValue(Value);
                if (Reference && !BlueprintOwnsObject(Reference, BlueprintLoad->Members))
                {
                    UE_LOG(LogBuildingSkills, Verbose, TEXT("Blueprint placement excluded external connection: %s.%s"), *Owner->GetName(), *Name.ToString());
                    Property->SetObjectPropertyValue(Value, nullptr);
                }
            }
        };
        // Native cleanup disconnects both endpoints of a nonreciprocal link.
        // Clear only the new object's reference first, or that cleanup can break
        // an original pipe/belt/rail while placing a copy elsewhere in the world.
        ClearForeignObject(Building, TEXT("mConnections"));
        for (auto* Port : TInlineComponentArray<UFGConnectionComponent*>(Building))
        {
            ClearForeignObject(Port, TEXT("mConnectedComponent"));
            for (const FName Name : {FName(TEXT("mWires")), FName(TEXT("mHiddenConnections")), FName(TEXT("mConnectedComponents"))})
            {
                auto* Property = FindFProperty<FArrayProperty>(Port->GetClass(), Name);
                auto* Inner = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
                if (!Inner) continue;
                FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(Port));
                for (int32 I = Array.Num()-1; I >= 0; --I)
                {
                    UObject* Reference = Inner->GetObjectPropertyValue(Array.GetRawPtr(I));
                    if (!IsValid(Reference) || !BlueprintOwnsObject(Reference, BlueprintLoad->Members)) Array.RemoveValues(I);
                }
                if (Name == TEXT("mWires"))
                    if (auto* Count = FindFProperty<FByteProperty>(Port->GetClass(), TEXT("mNumWiresConnected")))
                        Count->SetPropertyValue_InContainer(Port, uint8(FMath::Min(Array.Num(), 255)));
            }
        }
    }

    bool ValidScale(const FVector& Scale)
    {
        return !Scale.ContainsNaN() && Scale.GetAbsMin() >= .0001 && Scale.GetAbsMax() <= 10000;
    }

    struct FStoredActorHeader
    {
        FActorSaveHeader Header;
        int32 ObjectIndex = INDEX_NONE;
        int64 Start = 0;
        int64 End = 0;
    };

    bool ReadStoredHeaders(const FBlueprintSaveData& Data, TArray<FStoredActorHeader>& Headers)
    {
        Headers.Reset();
        FMemoryReader Reader(Data.TOCData, true);
        Data.BlueprintHeader.SaveObjectVersionData.ApplyToArchive(Reader);
        Reader.ArIsSaveGame = true;
        Reader.ArMaxSerializeSize = Data.TOCData.Num();
        int32 Count = 0;
        Reader << Count;
        if (Reader.IsError() || Count < 0 || Count > Data.TOCData.Num() / 4) return false;
        for (int32 I = 0; I < Count && !Reader.IsError(); ++I)
        {
            bool IsActor = false;
            Reader << IsActor;
            if (IsActor)
            {
                auto& Entry = Headers.AddDefaulted_GetRef();
                Entry.ObjectIndex = I;
                Entry.Start = Reader.Tell();
                Entry.Header.Serialize(Reader, Data.BlueprintHeader.SaveVersion);
                Entry.End = Reader.Tell();
            }
            else
            {
                FObjectSaveHeader Header;
                Header.Serialize(Reader, Data.BlueprintHeader.SaveVersion);
            }
        }
        return !Reader.IsError() && Reader.Tell() == Data.TOCData.Num();
    }
}

bool ReadBuildingSkillsBlueprintHeaders(const FBlueprintSaveData& Data, TArray<FActorSaveHeader>& Headers)
{
    Headers.Reset();
    TArray<FStoredActorHeader> Stored;
    if (!ReadStoredHeaders(Data, Stored)) return false;
    for (const auto& Entry : Stored) Headers.Add(Entry.Header);
    return true;
}

bool RestoreBuildingSkillsBlueprintArchiveScales(AFGBlueprintSubsystem* System)
{
    if (!BlueprintWrite) return true;
    if (BlueprintWrite->InvalidObject) return false;
    auto& Data = System->mStoredBlueprintData;
    TArray<FStoredActorHeader> Headers;
    if (!ReadStoredHeaders(Data, Headers)) return false;
    // Patch a copy, using native version-aware serialization and requiring every
    // rewritten entry to occupy exactly its original span. Blob data is untouched.
    TArray<uint8> Patched = Data.TOCData;
    FMemoryWriter Writer(Patched, true);
    Data.BlueprintHeader.SaveObjectVersionData.ApplyToArchive(Writer);
    Writer.ArIsSaveGame = true;
    for (auto& Entry : Headers)
        if (auto* Actor = Entry.Header.ObjectHeader.Reference.Resolve<AFGBuildable>(BlueprintWrite->World))
        {
            const FVector Scale = Actor->GetActorScale3D();
            if (!ValidScale(Scale)) continue;
            Entry.Header.Transform.SetScale3D(FVector3f(Scale));
            Writer.Seek(Entry.Start);
            Entry.Header.Serialize(Writer, Data.BlueprintHeader.SaveVersion);
            if (Writer.IsError() || Writer.Tell() != Entry.End) return false;
        }
    if (Patched.Num() != Data.TOCData.Num()) return false;
    Data.TOCData = MoveTemp(Patched);
    return true;
}

void InstallBuildingSkillsBlueprintScale()
{
    if (WITH_EDITOR) return;
    SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::SaveBlueprint,
        [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, FBlueprintRecord Record,
            AFGPlayerController* Controller, bool UpdateAuthor)
        {
            if (!Designer->HasAuthority()) return;
            FBlueprintWriteContext Context{Designer->GetWorld()};
            for (auto* Building : Designer->GetBuildablesInBlueprintDesigner()) Context.Members.Add(Building);
            TGuardValue<FBlueprintWriteContext*> Guard(BlueprintWrite, &Context);
            Scope(Designer, Record, Controller, UpdateAuthor);
        });
    SUBSCRIBE_METHOD(AFGBlueprintSubsystem::WriteBlueprintToDisk,
        [](auto& Scope, AFGBlueprintSubsystem* System, const FBlueprintRecord&)
        {
            if (!RestoreBuildingSkillsBlueprintArchiveScales(System))
            {
                UE_LOG(LogBuildingSkills, Error, TEXT("Blueprint Save rejected invalid object data or unreadable native TOC"));
                Scope.Override(false);
                return;
            }
            // WriteBlueprintToArchive itself calls the native disk writer.
            // Clipboard snapshots finish serialization but never write files.
            if (BlueprintWrite && BlueprintWrite->Clipboard) Scope.Override(true);
        });

    // The native actor-header serializer is inline and Linux does not call its
    // exported copy. Inspect each TOC entry at the virtual object-loader boundary,
    // then rewind so the original loader keeps all spawning and redirect behavior.
    TArray<uint8> SampleBytes;
    FMemoryReader SampleArchive(SampleBytes, true);
    FBlueprintArchiveObjectTOCProxy SampleProxy(SampleArchive, nullptr, FTransform::Identity);
    using FObjectSerialize = FArchive& (FBlueprintArchiveObjectTOCProxy::*)(UObject*&);
    const FObjectSerialize ObjectSerialize = &FBlueprintArchiveObjectTOCProxy::operator<<;
    // SML's generic TCallScope cannot store a reference return. Register this
    // adapter through the same hook manager with the exact native signature;
    // there is no struct-return ABI conversion or executable offset involved.
    FNativeHookManagerInternal::RegisterHookFunction(TEXT("BuildingSkills.BlueprintObjectLoader"),
        ConvertFunctionPointer(ObjectSerialize), &SampleProxy,
        reinterpret_cast<void*>(&ReadBlueprintObject), reinterpret_cast<void**>(&NativeReadObject));
    FBlueprintArchiveObjectDataProxy DataProxy(SampleArchive, nullptr);
    using FDataSerialize = FArchive& (FBlueprintArchiveObjectDataProxy::*)(UObject*&);
    const FDataSerialize DataSerialize = &FBlueprintArchiveObjectDataProxy::operator<<;
    FNativeHookManagerInternal::RegisterHookFunction(TEXT("BuildingSkills.BlueprintDataObjects"),
        ConvertFunctionPointer(DataSerialize), &DataProxy,
        reinterpret_cast<void*>(&SerializeBlueprintDataObject), reinterpret_cast<void**>(&NativeSerializeDataObject));
    SUBSCRIBE_UOBJECT_METHOD(AFGBuildable, PostSerializedFromBlueprint,
        [](auto& Scope, AFGBuildable* Building, bool BlueprintWorld)
        {
            // Remove stripped array entries before native circuit rebuilding.
            // ClearConnection/Disconnect would also mutate the original endpoint.
            CleanLoadedNetworkReferences(Building);
        });
    using FSpawn = AActor* (UWorld::*)(UClass*, const FTransform*, const FActorSpawnParameters&);
    SUBSCRIBE_METHOD_EXPLICIT(FSpawn, UWorld::SpawnActor,
        [](auto& Scope, UWorld* World, UClass* Class, const FTransform* Transform, const FActorSpawnParameters& Parameters)
        {
            auto* Context = BlueprintLoad;
            if (!Context || !Context->Pending || !Transform || !Parameters.bDeferConstruction ||
                !Class || !Class->IsChildOf(AFGBuildable::StaticClass()) ||
                Class->GetPathName() != Context->Pending->ObjectHeader.ClassName) return;
            const FTransform Expected = FTransform(Context->Pending->Transform) * Context->Origin;
            if (!Transform->GetLocation().Equals(Expected.GetLocation(), .1) ||
                !Transform->GetRotation().Equals(Expected.GetRotation(), .0001)) return;
            Context->Pending.Reset();
            const FVector Scale = Expected.GetScale3D();
            if (!ValidScale(Scale) || Scale.Equals(FVector::OneVector, .000001)) return;
            FTransform ScaledTransform = *Transform;
            ScaledTransform.SetScale3D(Scale);
            // Do not let construction of child actors reuse this header.
            auto* Actor = Scope(World, Class, &ScaledTransform, Parameters);
            if (IsValid(Actor)) Context->Spawning.Add(Actor, Scale);
        });
    SUBSCRIBE_METHOD(AActor::FinishSpawning,
        [](auto& Scope, AActor* Actor, const FTransform& Transform, bool DefaultTransform,
            const FComponentInstanceDataCache* Cache, ESpawnActorScaleMethod ScaleMethod)
        {
            if (!BlueprintLoad) return;
            FVector Scale;
            if (!BlueprintLoad->Spawning.RemoveAndCopyValue(Actor, Scale)) return;
            FTransform ScaledTransform = Transform;
            ScaledTransform.SetScale3D(Scale);
            // Native TOC loading passes a second unit-scale transform here. Apply
            // the saved scale before construction, collision and native instancing.
            Scope(Actor, ScaledTransform, DefaultTransform, Cache, ScaleMethod);
            BlueprintLoad->Scaled.Add(Cast<AFGBuildable>(Actor));
        });
    SUBSCRIBE_METHOD(AFGBlueprintSubsystem::LoadStoredBlueprint,
        [](auto& Scope, AFGBlueprintSubsystem* System, UFGBlueprintDescriptor* Descriptor,
            const FTransform& Origin, TArray<AFGBuildable*>& Buildings, bool BlueprintWorld,
            AFGBuildableBlueprintDesigner* Designer, APawn* Instigator, AFGBlueprintProxy* Proxy,
            const TFunction<void(AFGBuildable*, int32)>& PostSerialize, const TFunction<void()>& PreBeginPlay)
        {
            if (TestVanillaLoad) return;
            FBlueprintLoadContext Context;
            Context.Preview = BlueprintWorld;
            Context.Origin = Origin;
            {
                TGuardValue<FBlueprintLoadContext*> Guard(BlueprintLoad, &Context);
                Scope(System, Descriptor, Origin, Buildings, BlueprintWorld, Designer, Instigator, Proxy, PostSerialize, PreBeginPlay);
            }
            if (Context.ResetConveyorChains)
                UE_LOG(LogBuildingSkills, Display, TEXT("Blueprint placement reset %d copied conveyor simulation references before registration (preview=%d)."),
                    Context.ResetConveyorChains, BlueprintWorld);
            // Lightweight creation sends its full native instance transform.
            // Actor movement replication lacks scale, so use the existing bounded
            // transform transport and late-join snapshot for ordinary buildings.
            if (!BlueprintWorld && System->HasAuthority())
                if (auto* Groups = ABSGroupSubsystem::Get(System->GetWorld()))
                {
                    TArray<FBSNativeTransform> Changes;
                    for (const auto& Weak : Context.Scaled)
                        if (auto* Actor = Weak.Get(); IsValid(Actor) && !Actor->ShouldConvertToLightweight() && !Actor->GetIsLightweightTemporary())
                            Changes.Add(FBSTransformNative::Capture(Actor));
                    if (!Changes.IsEmpty())
                    {
                        FBSSelection Unselected;
                        Groups->PublishTransforms(Changes, Unselected);
                    }
                }
        });
}

void LoadBuildingSkillsBlueprintVanillaForTest(AFGBlueprintSubsystem* System,
    UFGBlueprintDescriptor* Descriptor, TArray<AFGBuildable*>& Buildings)
{
    check(FParse::Param(FCommandLine::Get(), TEXT("BuildingSkillsRuntimeTest")));
    TGuardValue<bool> Guard(TestVanillaLoad, true);
    System->LoadStoredBlueprint(Descriptor, FTransform::Identity, Buildings, true);
}

FBlueprintHeader WriteBuildingSkillsBlueprintForTest(AFGBlueprintSubsystem* System,
    const FBlueprintRecord& Record, const FTransform& Origin, const TArray<AFGBuildable*>& Buildings)
{
    check(FParse::Param(FCommandLine::Get(), TEXT("BuildingSkillsRuntimeTest")));
    FBlueprintWriteContext Context{System->GetWorld()};
    for (auto* Building : Buildings) Context.Members.Add(Building);
    TGuardValue<FBlueprintWriteContext*> Guard(BlueprintWrite, &Context);
    return System->WriteBlueprintToArchive(Record, Origin, Buildings, FIntVector(8, 8, 8));
}

bool ReadBuildingSkillsBlueprintObjectPaths(const FBlueprintSaveData& Data, TMap<int32, FString>& Paths)
{
    TArray<FStoredActorHeader> Headers;
    if (!ReadStoredHeaders(Data, Headers)) return false;
    // Native post-serialize callback counts buildables, not all TOC objects.
    int32 BuildableIndex = 0;
    for (const auto& Entry : Headers)
        if (LoadClass<AFGBuildable>(nullptr, *Entry.Header.ObjectHeader.ClassName))
            Paths.Add(BuildableIndex++, Entry.Header.ObjectHeader.Reference.ToString());
    return true;
}

bool WriteBuildingSkillsClipboard(AFGBlueprintSubsystem* System, const FBlueprintRecord& Record,
    const FTransform& Origin, const TArray<AFGBuildable*>& Buildings, FIntVector Dimensions, FBlueprintSaveData& Out)
{
    Out = {};
    if (!IsValid(System) || Buildings.IsEmpty()) return false;
    for (auto* Building : Buildings)
        if (!IsValid(Building) || Building->GetIsDismantled()) return false;
    FBlueprintWriteContext Context{System->GetWorld(), true};
    for (auto* Building : Buildings) Context.Members.Add(Building);
    TGuardValue<FBlueprintWriteContext*> Guard(BlueprintWrite, &Context);
    TGuardValue<FBlueprintSaveData> StoredGuard(System->mStoredBlueprintData, {});
    System->mStoredBlueprintData.BlueprintHeader = System->WriteBlueprintToArchive(Record, Origin, Buildings, Dimensions);
    System->mStoredBlueprintData.BlueprintHeader.BlueprintName = Record.BlueprintName;
    if (!RestoreBuildingSkillsBlueprintArchiveScales(System)) return false;
    Out = System->mStoredBlueprintData;
    return Out.IsValid();
}

bool TestBuildingSkillsArchiveObjects(AFGBuildable* Selected, UObject* Dependency)
{
    check(FParse::Param(FCommandLine::Get(), TEXT("BuildingSkillsRuntimeTest")));
    if (!IsValid(Selected) || !IsValid(Dependency)) return false;
    TArray<uint8> Expected, Actual;
    auto Write = [&](TArray<uint8>& Bytes, bool Contextual)
    {
        FMemoryWriter Writer(Bytes, true);
        Writer.SetCustomVersion(FSaveCustomVersion::GUID, FSaveCustomVersion::LatestVersion, FName(TEXT("SaveVersion")));
        FBlueprintArchiveObjectDataProxy Proxy(Writer, Selected->GetWorld());
        FBlueprintWriteContext Context{Selected->GetWorld(), true};
        Context.Members.Add(Selected);
        TGuardValue<FBlueprintWriteContext*> Guard(BlueprintWrite, Contextual ? &Context : nullptr);
        UObject* Object = Dependency;
        Proxy << Object;
        return !Writer.IsError() && !Context.InvalidObject && Object == Dependency;
    };
    // A native-gathered dependency is allowed even when its owner was not selected.
    if (!Write(Expected, false) || !Write(Actual, true) || Expected.IsEmpty() || Expected != Actual) return false;
    FMemoryWriter Writer(Actual, true);
    FBlueprintArchiveObjectDataProxy Proxy(Writer, Selected->GetWorld());
    FBlueprintWriteContext Context{Selected->GetWorld(), true};
    TGuardValue<FBlueprintWriteContext*> Guard(BlueprintWrite, &Context);
    UObject* Missing = nullptr;
    Proxy << Missing;
    return Context.InvalidObject && Proxy.IsError() &&
        !RestoreBuildingSkillsBlueprintArchiveScales(AFGBlueprintSubsystem::Get(Selected->GetWorld()));
}
