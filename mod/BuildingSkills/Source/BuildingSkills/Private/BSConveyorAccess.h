#pragma once
#include "Buildables/FGBuildableConveyorBase.h"
#include "FGFactoryConnectionComponent.h"
#include "FGConveyorChainActor.h"
#include "UObject/UnrealType.h"

// Resolve inventory and connection identity through runtime metadata. Length
// and non-reflected state use the matching CL 520140 headers from SML 3.12.1;
// no field-relative offset reconstruction is needed.
namespace BSConveyorAccess
{
    inline UObject* Object(UObject* Owner, FName Name)
    {
        const auto* Field = Owner ? FindFProperty<FObjectPropertyBase>(Owner->GetClass(), Name) : nullptr;
        return Field ? Field->GetObjectPropertyValue_InContainer(Owner) : nullptr;
    }
    inline UFGFactoryConnectionComponent* Connection(UObject* Owner, int32 End)
    {
        return Cast<UFGFactoryConnectionComponent>(Object(Owner, End == 0 ? TEXT("mConnection0") : TEXT("mConnection1")));
    }
    template<class T> UFGFactoryConnectionComponent* Connection(const TWeakObjectPtr<T>& Owner, int32 End)
    { return Connection(Owner.Get(), End); }
    inline AFGConveyorChainActor* Chain(UObject* Owner)
    { return Cast<AFGConveyorChainActor>(Object(Owner, TEXT("mConveyorChainActor"))); }
    inline FTransform& Top(UObject* Owner)
    {
        const auto* Field = FindFProperty<FStructProperty>(Owner->GetClass(), TEXT("mTopTransform"));
        check(Field);
        return *Field->ContainerPtrToValuePtr<FTransform>(Owner);
    }
    inline FConveyorBeltItems& Items(UObject* Owner)
    {
        const auto* Field = FindFProperty<FStructProperty>(Owner->GetClass(), TEXT("mItems"));
        check(Field && Field->Struct->GetStructureSize() == sizeof(FConveyorBeltItems));
        return *Field->ContainerPtrToValuePtr<FConveyorBeltItems>(Owner);
    }
    inline float Length(AFGBuildableConveyorBase* Owner)
    {
        return Owner->GetLength();
    }
    inline void ReadItems(AFGBuildableConveyorBase* Owner, TArray<FConveyorBeltItem*>& Out)
    {
        if (auto* Actor = Chain(Owner)) Actor->GetItemsForSegment(Owner, Out);
        else
        {
            auto& Queue = Items(Owner);
            for (int32 Index = 0; Index < Queue.Num(); ++Index) Out.Add(&Queue[Index]);
        }
    }
}