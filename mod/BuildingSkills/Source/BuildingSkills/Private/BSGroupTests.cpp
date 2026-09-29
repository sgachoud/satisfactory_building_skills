#if WITH_DEV_AUTOMATION_TESTS
#include "BSGroups.h"
#include "Buildables/FGBuildable.h"
#include "FGRecipe.h"
#include "Misc/AutomationTest.h"
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
    }
    return true;
}
#endif
