#pragma once
#include "FGInventoryComponent.h"
#include "UObject/UnrealType.h"

// Resolve inventory identity through the running game's reflected fields.
// This remains useful across native class layout changes.
namespace BSFactoryAccess
{
    inline FObjectPropertyBase* Property(UObject* Owner, FName Name)
    {
        return IsValid(Owner) ? FindFProperty<FObjectPropertyBase>(Owner->GetClass(), Name) : nullptr;
    }
    inline UObject* Object(UObject* Owner, FName Name)
    {
        const auto* Field = Property(Owner, Name);
        return Field ? Field->GetObjectPropertyValue_InContainer(Owner) : nullptr;
    }
    inline UFGInventoryComponent* Inventory(UObject* Owner, FName Name)
    {
        return Cast<UFGInventoryComponent>(Object(Owner, Name));
    }
    inline UClass* Recipe(UObject* Owner)
    {
        return Cast<UClass>(Object(Owner, TEXT("mCurrentRecipe")));
    }
}