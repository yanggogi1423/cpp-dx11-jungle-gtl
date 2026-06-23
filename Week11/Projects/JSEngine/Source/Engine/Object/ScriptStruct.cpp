#include "Object/ScriptStruct.h"

#include "Object/Property.h"

UScriptStruct::~UScriptStruct()
{
	for (FProperty* Property : Properties)
	{
		delete Property;
	}
	Properties.clear();
}

void UScriptStruct::AddProperty(FProperty* Property)
{
	Properties.push_back(Property);
}

void UScriptStruct::GetProperties(TArray<FProperty*>& OutProperties) const
{
	OutProperties = Properties;
}

FProperty* UScriptStruct::FindProperty(const FString& PropertyName) const
{
	for (FProperty* Property : Properties)
	{
		if (Property->Name == PropertyName)
		{
			return Property;
		}
	}
	return nullptr;
}
