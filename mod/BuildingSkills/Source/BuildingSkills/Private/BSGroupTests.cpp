#if WITH_DEV_AUTOMATION_TESTS
#include "BSGroups.h"
#include "Buildables/FGBuildable.h"
#include "FGRecipe.h"
#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace
{
    FBSBuildingRef Building(int32 Id)
    {
        FBSBuildingRef B;
        B.LightweightClass = AFGBuildable::StaticClass();
        B.Recipe = UFGRecipe::StaticClass();
        B.Transform.SetLocation(FVector(Id * 800.0, 0, 0));
        B.RuntimeIndex = Id;
        return B;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSHierarchyTest, "BuildingSkills.Groups.NestMergeUngroup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSHierarchyTest::RunTest(const FString&)
{
    TArray<FBSGroup> Tree;
    FString Error;
    FBSSelection Result;
    auto Apply = [&](EBSGroupOperation Op, const FBSSelection& Selection)
    { Error.Empty(); return FBSGroupTree::Apply(Tree, Op, Selection, Result, Error); };
    FBSSelection S;
    S.Buildings = {Building(1), Building(2), Building(2)};
    TestTrue(TEXT("Create group from individual buildings"), Apply(EBSGroupOperation::Group, S));
    const FGuid A = Result.Groups[0];
    TestEqual(TEXT("Deduplicated members"), FBSGroupTree::Find(Tree, A)->Buildings.Num(), 2);
    S.Buildings = {Building(3), Building(4)};
    TestTrue(TEXT("Create second group"), Apply(EBSGroupOperation::Group, S));
    const FGuid B = Result.Groups[0];
    S.Groups = {A, B}; S.Buildings = {Building(5), Building(1)};
    TestTrue(TEXT("Group groups and a single into a supergroup"), Apply(EBSGroupOperation::Group, S));
    const FGuid Parent = Result.Groups[0];
    TestEqual(TEXT("Original groups retained"), Tree.Num(), 3);
    TestTrue(TEXT("A nested under parent"), FBSGroupTree::Find(Tree, A)->Parent == Parent);
    TestEqual(TEXT("Ancestor has only the uncovered single directly"), FBSGroupTree::Find(Tree, Parent)->Buildings.Num(), 1);
    TArray<FBSBuildingRef> Members;
    FBSGroupTree::Collect(Tree, Parent, Members);
    TestEqual(TEXT("All descendants once"), Members.Num(), 5);
    S = {}; S.Groups = {Parent, A, A}; S.Buildings = {Building(1)};
    auto Normalized = FBSGroupTree::Normalize(Tree, S);
    TestEqual(TEXT("Ancestor covers overlapping descendant selections"), Normalized.Groups.Num(), 1);
    TestEqual(TEXT("Group covers overlapping single"), Normalized.Buildings.Num(), 0);
    TestTrue(TEXT("One-level ungroup"), Apply(EBSGroupOperation::Ungroup, S));
    TestEqual(TEXT("Only highest group removed"), Tree.Num(), 2);
    TestTrue(TEXT("Child retained at root"), !FBSGroupTree::Find(Tree, A)->Parent.IsValid());
    TestEqual(TEXT("Ungroup returns child group selection"), Result.Groups.Num(), 2);
    TestEqual(TEXT("Ungroup returns direct singles"), Result.Buildings.Num(), 1);
    S.Groups = {A}; S.Buildings = {Building(5)};
    TestTrue(TEXT("Merge adds a single to an existing group"), Apply(EBSGroupOperation::Merge, S));
    TestTrue(TEXT("Merge keeps existing group identity"), Result.Groups[0] == A);
    TestEqual(TEXT("Single added directly"), FBSGroupTree::Find(Tree, A)->Buildings.Num(), 3);
    S.Groups = {A, B}; S.Buildings = {};
    TestFalse(TEXT("Cannot ungroup two groups"), Apply(EBSGroupOperation::Ungroup, S));
    TestEqual(TEXT("Rejected command is atomic"), Tree.Num(), 2);
    TestTrue(TEXT("Merge combines two groups"), Apply(EBSGroupOperation::Merge, S));
    TestEqual(TEXT("Merged outer container removed"), Tree.Num(), 1);
    TestEqual(TEXT("All merged members retained"), Tree[0].Buildings.Num(), 5);
    S.Groups = {}; S.Buildings = {Building(1), Building(2)};
    TestFalse(TEXT("Merge needs an existing selected group"), Apply(EBSGroupOperation::Merge, S));
    TestTrue(TEXT("Blueprint-style individual selection can regroup members"), Apply(EBSGroupOperation::Group, S));
    const FGuid Child = Result.Groups[0];
    TestTrue(TEXT("Subgroup retains common parent"), FBSGroupTree::Find(Tree, Child)->Parent == A);
    TestEqual(TEXT("Single direct owner after extraction"), FBSGroupTree::Find(Tree, A)->Buildings.Num(), 3);
    TestTrue(TEXT("Tree remains valid"), FBSGroupTree::Validate(Tree));
    for (auto& G : Tree) if (G.Id == A) G.Parent = Child;
    TestFalse(TEXT("Cycles rejected"), FBSGroupTree::Validate(Tree));
    TestFalse(TEXT("Cannot mutate cyclic tree"), Apply(EBSGroupOperation::Group, S));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSMergeChildrenTest, "BuildingSkills.Groups.MergePreservesChildren",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSMergeChildrenTest::RunTest(const FString&)
{
    TArray<FBSGroup> Tree;
    for (int32 I = 0; I < 4; ++I)
    {
        FBSGroup G; G.Id = FGuid::NewGuid(); G.Buildings = {Building(I)};
        Tree.Add(G);
    }
    const FGuid A = Tree[0].Id, B = Tree[1].Id, C = Tree[2].Id, D = Tree[3].Id;
    Tree[2].Parent = A; Tree[3].Parent = B;
    FBSSelection Input; Input.Groups = {A, B};
    FBSSelection Result; FString Error;
    TestTrue(TEXT("Merge nested hierarchies"), FBSGroupTree::Apply(Tree, EBSGroupOperation::Merge, Input, Result, Error));
    TestEqual(TEXT("Only merged outer container removed"), Tree.Num(), 3);
    TestTrue(TEXT("First child retained"), FBSGroupTree::Find(Tree, C)->Parent == A);
    TestTrue(TEXT("Second child promoted into destination"), FBSGroupTree::Find(Tree, D)->Parent == A);
    TestTrue(TEXT("Merged hierarchy valid"), FBSGroupTree::Validate(Tree));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSGroupSaveTest, "BuildingSkills.Groups.MetadataSaveRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSGroupSaveTest::RunTest(const FString&)
{
    FBSGroup Original;
    Original.Id = FGuid::NewGuid(); Original.Parent = FGuid::NewGuid();
    Original.Name = TEXT("Factory floor"); Original.Pivot = FVector(10, 20, 30);
    Original.Buildings = {Building(42)};
    Original.Buildings[0].NetActorId = 987;
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    FObjectAndNameAsStringProxyArchive Save(Writer, false);
    Save.ArIsSaveGame = true;
    FBSGroup::StaticStruct()->SerializeItem(Save, &Original, nullptr);
    FBSGroup Loaded;
    FMemoryReader Reader(Bytes);
    FObjectAndNameAsStringProxyArchive Load(Reader, true);
    Load.ArIsSaveGame = true;
    FBSGroup::StaticStruct()->SerializeItem(Load, &Loaded, nullptr);
    TestTrue(TEXT("Group ID survives"), Loaded.Id == Original.Id);
    TestTrue(TEXT("Parent relationship survives"), Loaded.Parent == Original.Parent);
    TestEqual(TEXT("Group name survives"), Loaded.Name, Original.Name);
    TestEqual(TEXT("One saved building"), Loaded.Buildings.Num(), 1);
    if (Loaded.Buildings.Num() == 1)
    {
        TestTrue(TEXT("Fingerprint survives"), Loaded.Buildings[0] == Original.Buildings[0]);
        TestEqual(TEXT("Runtime index is never persisted"), Loaded.Buildings[0].RuntimeIndex, INDEX_NONE);
        TestEqual(TEXT("Connection-scoped actor IDs are never persisted"), Loaded.Buildings[0].NetActorId, uint64(0));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSDeepGroupTest, "BuildingSkills.Groups.DeepTraversalOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSDeepGroupTest::RunTest(const FString&)
{
    TArray<FBSGroup> Tree;
    for (int32 I = 0; I < 2048; ++I)
    {
        FBSGroup G; G.Id = FGuid::NewGuid(); G.Buildings = {Building(I)};
        if (I) G.Parent = Tree.Last().Id;
        Tree.Add(MoveTemp(G));
    }
    // A reversed storage order must not change member/anchor order.
    const FGuid Root = Tree[0].Id;
    Algo::Reverse(Tree);
    TestTrue(TEXT("Deep reversed hierarchy validates without repeated ancestry scans"), FBSGroupTree::Validate(Tree));
    TArray<FBSBuildingRef> Members = {Building(2047)};
    FBSGroupTree::Collect(Tree, Root, Members);
    TestEqual(TEXT("Deep hierarchy resolves every member once without recursion"), Members.Num(), 2048);
    for (int32 I = 0; I < Members.Num(); ++I)
        if (!(Members[I] == Building(2047-I))) { AddError(TEXT("Traversal changed selection order")); break; }
    Members.Reset();
    FBSGroupTree::Collect(Tree, {}, Members);
    TestTrue(TEXT("Invalid group is not a selection of every root"), Members.IsEmpty());
    Tree.Last().Parent = Tree[0].Id;
    TestFalse(TEXT("Deep cycle is rejected"), FBSGroupTree::Validate(Tree));
    FBSGroupTree::Collect(Tree, Root, Members);
    TestEqual(TEXT("Malformed cycle terminates and deduplicates"), Members.Num(), 2048);
    Tree.Last().Parent = FGuid::NewGuid();
    TestFalse(TEXT("Missing parent is rejected"), FBSGroupTree::Validate(Tree));
    Tree.Last().Parent.Invalidate();
    Tree[0].Buildings = Tree[1].Buildings;
    TestFalse(TEXT("Duplicate membership is still rejected"), FBSGroupTree::Validate(Tree));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSGroupTransportTest, "BuildingSkills.Groups.BoundedSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSGroupTransportTest::RunTest(const FString&)
{
    TArray<FBSGroup> Source;
    for (int32 I=0; I<18; ++I)
    {
        auto& G=Source.AddDefaulted_GetRef(); G.Id=FGuid::NewGuid(); G.Name=FString::Printf(TEXT("Group %d"),I);
        G.Pivot=FVector(I,2,3);
        if (I) G.Parent=Source[0].Id;
        for (int32 B=0; B<(I==1 ? 2048 : I==0 ? 0 : 5); ++B) G.Buildings.Add(Building(I*10000+B));
    }
    TArray<FBSGroup> Received;
    TArray<FBSGroupFragment> Batch, First;
    int32 Group=0, Member=0, Expected=0, Chunks=0;
    do
    {
        if (!TestTrue(TEXT("Snapshot packing makes progress"),FBSGroupTransfer::Pack(Source,Group,Member,Batch))) return false;
        if (!Chunks) First=Batch;
        int32 Count=0; for (const auto& F:Batch) Count+=F.Group.Buildings.Num();
        TestTrue(TEXT("Every message has bounded members and group headers"),Count<=64 && Batch.Num()<=8);
        if (!TestTrue(TEXT("Ordered fragment accepted"),FBSGroupTransfer::Append(Received,Expected,Batch,Source.Num(),Group==Source.Num()))) return false;
        if (++Chunks>1000) return false;
    } while (Group<Source.Num());
    TestTrue(TEXT("Large single group is split across messages"),Chunks>32);
    TestEqual(TEXT("All group headers restored"),Received.Num(),Source.Num());
    for (int32 I=0; I<Source.Num(); ++I)
    {
        TestTrue(TEXT("Group metadata retained"),Received[I].Id==Source[I].Id && Received[I].Parent==Source[I].Parent && Received[I].Name==Source[I].Name && Received[I].Pivot==Source[I].Pivot);
        TestTrue(TEXT("Every member retained in order"),Received[I].Buildings==Source[I].Buildings);
    }
    TArray<FBSGroup> Partial; Expected=0;
    TestFalse(TEXT("Truncated tree cannot be committed"),FBSGroupTransfer::Append(Partial,Expected,First,Source.Num(),true));
    Partial.Reset(); Expected=0;
    TestTrue(TEXT("Initial fragments accepted provisionally"),FBSGroupTransfer::Append(Partial,Expected,First,Source.Num(),false));
    TestFalse(TEXT("Duplicate initial fragments rejected"),FBSGroupTransfer::Append(Partial,Expected,First,Source.Num(),false));
    Partial.Reset(); Expected=0;
    auto OutOfOrder=First; OutOfOrder[0].Offset=1;
    TestFalse(TEXT("Missing initial fragment rejected"),FBSGroupTransfer::Append(Partial,Expected,OutOfOrder,Source.Num(),false));
    Partial.Reset(); Expected=0;
    TestTrue(TEXT("Empty snapshot clears the tree"),FBSGroupTransfer::Append(Partial,Expected,{},0,true));
    FBSBuildingRef DistantA, DistantB; DistantA.NetActorId=(uint64(1)<<40)+101; DistantB.NetActorId=(uint64(1)<<40)+103;
    TestFalse(TEXT("Unmapped actors retain separate network identities"),DistantA==DistantB);
    FBSGroupFragment Pending; Pending.Group.Id=FGuid::NewGuid(); Pending.TotalMembers=2;
    Pending.Group.Buildings={DistantA,DistantB};
    TestTrue(TEXT("Distant actors do not invalidate an otherwise complete group snapshot"),FBSGroupTransfer::Append(Partial,Expected,{Pending},1,true));
    TestEqual(TEXT("Both distant members survive without their actors loaded"),Partial[0].Buildings.Num(),2);
    auto* SavedGroups=FindFProperty<FProperty>(ABSGroupSubsystem::StaticClass(),TEXT("Groups"));
    auto* Published=FindFProperty<FProperty>(ABSGroupSubsystem::StaticClass(),TEXT("PublishedGroupRevision"));
    TestTrue(TEXT("Saved tree cannot become an oversized replicated property"),SavedGroups && SavedGroups->HasAnyPropertyFlags(CPF_SaveGame) && !SavedGroups->HasAnyPropertyFlags(CPF_Net));
    TestTrue(TEXT("Only the snapshot notification is replicated"),Published && Published->HasAnyPropertyFlags(CPF_Net));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSEmptyGroupTest, "BuildingSkills.Groups.PruneDeletedHierarchy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSEmptyGroupTest::RunTest(const FString&)
{
    FBSGroup Parent, Child, Sibling;
    Parent.Id = FGuid::NewGuid(); Child.Id = FGuid::NewGuid(); Sibling.Id = FGuid::NewGuid();
    Child.Parent = Parent.Id; Child.Buildings = {Building(1)}; Sibling.Buildings = {Building(2)};
    TArray<FBSGroup> Tree{Child, Parent, Sibling};
    FBSGroupTree::PruneEmpty(Tree);
    TestEqual(TEXT("An empty parent with a nonempty child survives"), Tree.Num(), 3);
    Tree[0].Buildings.Reset(); FBSGroupTree::PruneEmpty(Tree);
    TestEqual(TEXT("Last deletion removes both empty child and parent"), Tree.Num(), 1);
    TestTrue(TEXT("Unrelated group survives"), Tree[0].Id == Sibling.Id);
    Tree[0].Buildings.Reset(); FBSGroupTree::PruneEmpty(Tree);
    TestTrue(TEXT("No groups remain after final member deletion"), Tree.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBSSelectionTransportTest, "BuildingSkills.Groups.BoundedSelectionRequests",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBSSelectionTransportTest::RunTest(const FString&)
{
    for(int32 Count:{0,1,31,32,33,900,2048})
    {
        FBSSelection Source;
        for(int32 I=0;I<Count;++I)Source.Buildings.Add(Building(I));
        if(Count)for(int32 I=0;I<33;++I)Source.Groups.Add(FGuid::NewGuid());
        FBSSelectionPacket Received, Packet; Packet.Request=FGuid::NewGuid(); Packet.Action=5;
        Packet.TotalGroups=Source.Groups.Num(); Packet.TotalBuildings=Count;
        int32 Offset=0, Chunks=0;
        do
        {
            Packet.Offset=Offset;
            if(!TestTrue(TEXT("Bounded selection packing"),FBSSelectionTransfer::Pack(Source,Offset,Packet.Items)))return false;
            const int32 Size=Packet.Items.Groups.Num()+Packet.Items.Buildings.Num();
            TestTrue(TEXT("At most 32 entries per message"),Size<=32);
            if(!TestTrue(TEXT("Sequential packet accepted"),FBSSelectionTransfer::Append(Received,Packet)))return false;
            Offset+=Size; ++Chunks;
        }while(Offset<Source.Groups.Num()+Count);
        TestTrue(TEXT("Exact selection round trip"),FBSSelectionTransfer::Complete(Received) && Received.Items.Groups==Source.Groups && Received.Items.Buildings==Source.Buildings);
        if(Count>=900)TestTrue(TEXT("Large selection needs multiple acknowledgements"),Chunks>=29);
    }
    FBSSelection Source;for(int32 I=0;I<900;++I)Source.Buildings.Add(Building(I));
    FBSSelectionPacket First;First.Request=FGuid::NewGuid();First.TotalBuildings=900;
    FBSSelectionTransfer::Pack(Source,0,First.Items);
    FBSSelectionPacket Received;
    TestTrue(TEXT("First packet accepted"),FBSSelectionTransfer::Append(Received,First));
    TestFalse(TEXT("Partial selection cannot execute"),FBSSelectionTransfer::Complete(Received));
    TestFalse(TEXT("Duplicate packet rejected"),FBSSelectionTransfer::Append(Received,First));
    auto Next=First;Next.Offset=64;
    TestFalse(TEXT("Missing packet rejected"),FBSSelectionTransfer::Append(Received,Next));
    Next.Offset=32;Next.Action=3;
    TestFalse(TEXT("Mid-transfer operation switch rejected"),FBSSelectionTransfer::Append(Received,Next));
    Next=First;Next.Offset=32;Next.TotalBuildings=901;
    TestFalse(TEXT("Mid-transfer count change rejected"),FBSSelectionTransfer::Append(Received,Next));
    Next=First;Next.Offset=32;Next.Items.Buildings.Add(Building(32));
    TestFalse(TEXT("Oversized packet rejected"),FBSSelectionTransfer::Append(Received,Next));
    Next=First;Next.TotalBuildings=MAX_int32;
    TestFalse(TEXT("Oversized total rejected before allocation"),FBSSelectionTransfer::Append(Received,Next));
    TestEqual(TEXT("Rejected packets do not extend the assembled selection"),Received.Items.Buildings.Num(),32);
    return true;
}
#endif
