#include "Object/EnumRegistry.h"

#include "Object/Enum.h"

void FEnumRegistry::RegisterEnum(UEnum* Enum)
{
	Enums.push_back(Enum);
}

UEnum* FEnumRegistry::FindEnum(const FString& Name) const
{
	for (UEnum* Enum : Enums)
	{
		if (Enum->Name == Name)
		{
			return Enum;
		}
	}
	return nullptr;
}

FEnumRegistry::~FEnumRegistry()
{
	for (UEnum* Enum : Enums)
	{
		delete Enum;
	}
	Enums.clear();
}
