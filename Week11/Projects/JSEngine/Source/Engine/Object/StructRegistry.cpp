#include "Object/StructRegistry.h"

#include "Object/ScriptStruct.h"

void FStructRegistry::RegisterStruct(UScriptStruct* Struct)
{
	Structs.insert({ Struct->Name, Struct });
}

UScriptStruct* FStructRegistry::FindStruct(const FString& Name) const
{
	auto It = Structs.find(Name);
	if (It != Structs.end())
	{
		return It->second;
	}
	return nullptr;
}

FStructRegistry::~FStructRegistry()
{
	Structs.clear();
}
