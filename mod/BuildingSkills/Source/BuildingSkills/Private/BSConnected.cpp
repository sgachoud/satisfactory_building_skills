#include "BSConnected.h"
#include "BSBlueprintBounds.h"
#include "Buildables/FGBuildable.h"
#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGBlueprintFunctionLibrary.h"
#include "FGRecipe.h"
#include "Algo/Sort.h"
THIRD_PARTY_INCLUDES_START
#include <unicode/uregex.h>
THIRD_PARTY_INCLUDES_END

namespace
{
    // ICU is already bundled with the engine. Fail closed on syntax errors or
    // excessive matching work; never treat an invalid blacklist as empty.
    class FNameFilter
    {
        URegularExpression* White = nullptr;
        URegularExpression* Black = nullptr;
        static URegularExpression* Compile(const FString& Text, FString& Error)
        {
            if (Text.IsEmpty()) return nullptr;
            auto UTF16 = StringCast<UTF16CHAR>(*Text);
            UErrorCode Status = U_ZERO_ERROR;
            UParseError Parse{};
            auto* Result = uregex_open(reinterpret_cast<const UChar*>(UTF16.Get()), UTF16.Length(), 0, &Parse, &Status);
            if (U_FAILURE(Status)) { Error = FString::Printf(TEXT("Invalid regex near character %d: %s"), Parse.offset, UTF8_TO_TCHAR(u_errorName(Status))); return nullptr; }
            uregex_setTimeLimit(Result, 10, &Status);
            uregex_setStackLimit(Result, 256 * 1024, &Status);
            return Result;
        }
        static bool Matches(URegularExpression* Pattern, const FString& Name, FString& Error)
        {
            auto UTF16 = StringCast<UTF16CHAR>(*Name);
            UErrorCode Status = U_ZERO_ERROR;
            uregex_setText(Pattern, reinterpret_cast<const UChar*>(UTF16.Get()), UTF16.Length(), &Status);
            const bool Match = uregex_find(Pattern, 0, &Status) != 0;
            if (U_FAILURE(Status)) Error = TEXT("Regex matching exceeded its limit. Simplify the expression.");
            // ICU borrows the input text, so detach before the conversion dies.
            UErrorCode Detach = U_ZERO_ERROR;
            static const UChar Empty[] = {0};
            uregex_setText(Pattern, Empty, 0, &Detach);
            return Match;
        }
    public:
        FNameFilter(const FBSConnectedSettings& S, FString& Error)
        { White = Compile(S.Whitelist, Error); if (Error.IsEmpty()) Black = Compile(S.Blacklist, Error); }
        ~FNameFilter() { if (White) uregex_close(White); if (Black) uregex_close(Black); }
        bool Accept(const FString& Name, FString& Error)
        {
            if (Black && Matches(Black, Name, Error)) return false;
            if (!Error.IsEmpty()) return false;
            return !White || Matches(White, Name, Error);
        }
    };

    struct FNode { FBox Box{ForceInit}; int32 Begin = 0, End = 0, Left = -1, Right = -1; };
    // A bounds tree avoids quadratic whole-factory comparisons and huge spatial
    // grid allocations for long belts or a large user-specified gap.
    struct FBoundsTree
    {
        const TArray<FBox>& Boxes;
        TArray<int32> Order;
        TArray<FNode> Nodes;
        explicit FBoundsTree(const TArray<FBox>& In) : Boxes(In)
        { for (int32 I = 0; I < In.Num(); ++I) if (In[I].IsValid) Order.Add(I); if (!Order.IsEmpty()) Build(0, Order.Num()); }
        int32 Build(int32 Begin, int32 End)
        {
            FNode N; N.Begin = Begin; N.End = End;
            for (int32 I = Begin; I < End; ++I) N.Box += Boxes[Order[I]];
            const int32 Index = Nodes.Add(N);
            if (End - Begin > 12)
            {
                const FVector Size = N.Box.GetSize();
                const int32 Axis = Size.X >= Size.Y && Size.X >= Size.Z ? 0 : Size.Y >= Size.Z ? 1 : 2;
                Algo::Sort(MakeArrayView(Order.GetData() + Begin, End - Begin), [&](int32 A, int32 B)
                { const double X = Boxes[A].GetCenter()[Axis], Y = Boxes[B].GetCenter()[Axis]; return X == Y ? A < B : X < Y; });
                const int32 Mid = (Begin + End) / 2;
                const int32 Left = Build(Begin, Mid), Right = Build(Mid, End);
                Nodes[Index].Left = Left; Nodes[Index].Right = Right;
            }
            return Index;
        }
        void Query(int32 Index, const FBox& Box, TArray<int32>& Out) const
        {
            if (!Nodes.IsValidIndex(Index)) return;
            const auto& N = Nodes[Index];
            if (!N.Box.Intersect(Box)) return;
            if (N.Left >= 0) { Query(N.Left, Box, Out); Query(N.Right, Box, Out); }
            else for (int32 I = N.Begin; I < N.End; ++I) if (Boxes[Order[I]].Intersect(Box)) Out.Add(Order[I]);
        }
    };
}

bool FBSConnectedSettings::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Gap) || Gap < 0 || Gap > 1000 || Limit < 1 || Limit > 2048 || uint8(Scope) > uint8(EBSConnectedScope::Strict))
    { Error = TEXT("Gap must be 0–1000 m and object limit 1–2048."); return false; }
    if (Whitelist.Len() > 256 || Blacklist.Len() > 256)
    { Error = TEXT("Each regex may contain up to 256 characters."); return false; }
    FNameFilter Filter(*this, Error);
    return Error.IsEmpty();
}

FString FBSConnected::BuildingName(UClass* Class, TSubclassOf<UFGRecipe> Recipe)
{
    const auto* Defaults = Class ? Cast<AFGBuildable>(Class->GetDefaultObject()) : nullptr;
    if (Defaults && !Defaults->mDisplayName.IsEmpty()) return Defaults->mDisplayName.ToString();
    const auto Desc = Recipe ? UFGRecipe::GetDescriptorForRecipe(Recipe) : nullptr;
    // A missing recipe must never expose internal Build_... class names to a
    // filter documented as matching the player's display names.
    return Desc ? UFGItemDescriptor::GetItemName(Desc).ToString() : FString();
}

bool FBSConnected::NameMatches(const FString& Name, const FBSConnectedSettings& Settings, FString& Error)
{
    if (!Settings.Validate(Error)) return false;
    FNameFilter Filter(Settings, Error);
    const bool Result = Filter.Accept(Name, Error);
    return Result && Error.IsEmpty();
}

TArray<int32> FBSConnected::Traverse(const TArray<FBox>& Boxes, int32 Seed, double Gap, int32 Limit, bool& Truncated)
{
    TArray<int32> Result;
    Truncated = false;
    if (!Boxes.IsValidIndex(Seed) || !Boxes[Seed].IsValid || !FMath::IsFinite(Gap) || Gap < 0 || Limit < 1) return Result;
    FBoundsTree Tree(Boxes);
    TBitArray<> Visited(false, Boxes.Num());
    Result.Add(Seed); Visited[Seed] = true;
    for (int32 Cursor = 0; Cursor < Result.Num(); ++Cursor)
    {
        TArray<int32> Neighbors;
        // Expand only the current reference box, not both boxes. 0.1 cm handles rounding.
        Tree.Query(0, Boxes[Result[Cursor]].ExpandBy(Gap * 100.0 + .1), Neighbors);
        Neighbors.Sort();
        for (int32 Next : Neighbors)
        {
            if (Visited[Next]) continue;
            Visited[Next] = true;
            if (Result.Num() == Limit) { Truncated = true; return Result; }
            Result.Add(Next);
        }
    }
    return Result;
}

TArray<FBSBuildingRef> FBSConnected::Find(UWorld* World, const FBSBuildingRef& Seed,
    const FBSConnectedSettings& S, FString& Message, FBSConnectedSearchStats* Stats)
{
    if (Stats) *Stats = {};
    TArray<FBSBuildingRef> Result;
    if (!World || !S.Validate(Message) || !Seed.IsAlive(World)) return Result;
    auto* LW = AFGLightweightBuildableSubsystem::Get(World);
    UClass* SeedClass = Seed.LightweightClass ? Seed.LightweightClass.Get() : Seed.Actor->GetClass();
    const auto SeedRecipe = Seed.LightweightClass ? Seed.Recipe : Seed.Actor->GetBuiltWithRecipe();
    const bool NeedsCategory = S.Scope == EBSConnectedScope::Category || S.Scope == EBSConnectedScope::Subcategory;
    const auto SeedDesc = NeedsCategory && SeedRecipe ? UFGRecipe::GetDescriptorForRecipe(SeedRecipe) : nullptr;
    const auto SeedCategory = SeedDesc ? UFGItemDescriptor::GetCategory(SeedDesc) : nullptr;
    TArray<TSubclassOf<UFGCategory>> SeedSubs;
    if (SeedDesc) UFGItemDescriptor::GetSubCategories(SeedDesc, SeedSubs);
    const int32 SeedIndex = Seed.LightweightClass ? Seed.ResolveIndex(World) : INDEX_NONE;
    const auto* SeedData = Seed.LightweightClass && LW ? LW->GetRuntimeDataForBuildableClassAndIndex(Seed.LightweightClass, SeedIndex) : nullptr;
    const auto SeedColor = SeedData ? SeedData->CustomizationData : Seed.Actor ? Seed.Actor->GetCustomizationData_Native() : FFactoryCustomizationData{};
    const auto SeedMaterial = UFGBlueprintFunctionLibrary::GetMaterialDescriptorForBuildableClass(World, SeedClass);
    FNameFilter Filter(S, Message);
    TMap<UClass*, TMap<UClass*, bool>> ClassMatches;
    auto Accept = [&](UClass* Class, TSubclassOf<UFGRecipe> Recipe, const FFactoryCustomizationData& Color)
    {
        auto& RecipeMatches = ClassMatches.FindOrAdd(Class);
        bool* Cached = RecipeMatches.Find(Recipe.Get());
        if (!Cached)
        {
            const auto Desc = NeedsCategory && Recipe ? UFGRecipe::GetDescriptorForRecipe(Recipe) : nullptr;
            bool Match = true;
            if (S.Scope == EBSConnectedScope::Strict)
            {
                // Material variants have different classes. Map both to the seed's
                // material family when IgnoreMaterial is enabled (2 m stays 2 m).
                Match = Class == SeedClass;
                if (!Match && S.IgnoreMaterial && SeedMaterial)
                    if (const auto* Mapped = SeedMaterial.GetDefaultObject()->GetBuildableMap().Find(Class))
                        Match = AFGBuildable::GetBuildableClassFromRecipe(*Mapped) == SeedClass;
            }
            else if (S.Scope != EBSConnectedScope::All)
            {
                Match = Desc && SeedCategory && UFGItemDescriptor::GetCategory(Desc) == SeedCategory;
                if (Match && S.Scope == EBSConnectedScope::Subcategory)
                {
                    TArray<TSubclassOf<UFGCategory>> Subs;
                    UFGItemDescriptor::GetSubCategories(Desc, Subs);
                    Match = Subs.ContainsByPredicate([&](auto Sub) { return SeedSubs.Contains(Sub); });
                }
            }
            if (Match && !S.IgnoreMaterial)
                Match = UFGBlueprintFunctionLibrary::GetMaterialDescriptorForBuildableClass(World, Class) == SeedMaterial;
            if (Match) Match = Filter.Accept(BuildingName(Class, Recipe), Message);
            Cached = &RecipeMatches.Add(Recipe.Get(), Match);
        }
        if (!*Cached || !Message.IsEmpty()) return false;
        if (!S.IgnoreSwatch && (Color.SwatchDesc != SeedColor.SwatchDesc ||
            (Color.SwatchDesc && Color.SwatchDesc.GetDefaultObject()->ID == INDEX_CUSTOM_COLOR_SLOT && !(Color.OverrideColorData == SeedColor.OverrideColorData)))) return false;
        return true;
    };
    // Seed first makes count-limited results deterministic for a given snapshot.
    if (!Accept(SeedClass, SeedRecipe, SeedColor))
    { if (Message.IsEmpty()) Message = TEXT("The pointed object is excluded by the filters."); return Result; }
    FBox SeedBox(ForceInit);
    if (SeedData) SeedBox = SeedData->BoundingBox.TransformBy(SeedData->Transform);
    else if (Seed.Actor)
    { TArray<FVector> Points; BSBlueprintBounds::AppendActorGeometry(Seed.Actor, Points); for (const auto& Point : Points) SeedBox += Point; }
    if (!SeedBox.IsValid) { Message = TEXT("The pointed building has no valid bounds."); return {}; }
    // Query the game's maintained indices as the component grows. A small
    // selection must not rescan/rebuild a bounds tree for the entire factory.
    auto* System = AFGBuildableSubsystem::Get(World);
    TSet<uint64> Visited;
    TMap<uint64, FBox> Bounds;
    Result.Add(Seed);
    if (Seed.LightweightClass) Result[0].RuntimeIndex = SeedIndex;
    const uint64 SeedKey = FBSTransformNative::Key(Seed, SeedIndex);
    Visited.Add(SeedKey); Bounds.Add(SeedKey, SeedBox);
    bool Truncated = false;
    for (int32 Cursor = 0; Cursor < Result.Num() && !Truncated; ++Cursor)
    {
        const auto& Current = Result[Cursor];
        const FBox Query = Bounds.FindChecked(FBSTransformNative::Key(Current)).ExpandBy(S.Gap * 100.0 + .1);
        TArray<FBSBuildingRef> Neighbors;
        int32 Examined = 0;
        FBSTransformNative::QueryLightweightBounds(World, Query, Neighbors, Examined);
        if (System)
        {
            TArray<AFGBuildable*> Actors;
            System->GetCollidingBuildablesInBoundingBox(Actors, Query);
            Examined += Actors.Num();
            for (auto* Actor : Actors)
                if (IsValid(Actor) && !Actor->GetIsDismantled() && !Actor->GetIsLightweightTemporary())
                    Neighbors.Add(FBSBuildingRef::FromActor(Actor));
        }
        if (Stats) { ++Stats->SpatialQueries; Stats->CandidatesExamined += Examined; }
        // Stable spatial ordering gives count-limited previews the same ordering
        // on client and server without depending on native grid insertion order.
        Neighbors.Sort([](const FBSBuildingRef& A, const FBSBuildingRef& B)
        {
            const FVector AP = A.Location(), BP = B.Location();
            for (int32 Axis = 0; Axis < 3; ++Axis) if (AP[Axis] != BP[Axis]) return AP[Axis] < BP[Axis];
            UClass* AC = A.LightweightClass ? A.LightweightClass.Get() : A.Actor->GetClass();
            UClass* BC = B.LightweightClass ? B.LightweightClass.Get() : B.Actor->GetClass();
            if (AC != BC) return AC->GetPathName() < BC->GetPathName();
            return A.RuntimeIndex < B.RuntimeIndex;
        });
        for (const auto& Next : Neighbors)
        {
            const uint64 Identity = FBSTransformNative::Key(Next);
            if (Visited.Contains(Identity)) continue;
            const auto* Data = Next.LightweightClass && LW ? LW->GetRuntimeDataForBuildableClassAndIndex(Next.LightweightClass, Next.RuntimeIndex) : nullptr;
            if (Next.LightweightClass && !Data) continue;
            UClass* Class = Next.LightweightClass ? Next.LightweightClass.Get() : Next.Actor->GetClass();
            const auto Recipe = Data ? Data->BuiltWithRecipe : Next.Actor->GetBuiltWithRecipe();
            const auto Color = Data ? Data->CustomizationData : Next.Actor->GetCustomizationData_Native();
            if (!Accept(Class, Recipe, Color))
            {
                Visited.Add(Identity);
                if (!Message.IsEmpty()) return {};
                continue;
            }
            FBox* Box = Bounds.Find(Identity);
            if (!Box)
            {
                FBox Exact(ForceInit);
                if (Data) Exact = Data->BoundingBox.TransformBy(Data->Transform);
                else { TArray<FVector> Points; BSBlueprintBounds::AppendActorGeometry(Next.Actor, Points); for (const auto& Point : Points) Exact += Point; }
                Box = &Bounds.Add(Identity, Exact);
            }
            // Broadphase candidates still need the requested geometry test.
            if (!Box->IsValid || !Box->Intersect(Query)) continue;
            Visited.Add(Identity);
            if (Result.Num() == S.Limit) { Truncated = true; break; }
            Result.Add(Next);
        }
    }
    Message = Truncated ? FString::Printf(TEXT("Object limit reached (%d). Only this preview is included."), S.Limit) : FString();
    return Result;
}
