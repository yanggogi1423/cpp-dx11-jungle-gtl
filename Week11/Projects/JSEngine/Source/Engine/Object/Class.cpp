#include "Class.h"

#include "Core/Logging/Log.h"
#include "Object/Function.h"

UClass::~UClass()
{
	for (FProperty* Prop : Properties)
	{
		delete Prop;
	}
	Properties.clear();

	for (FFunction* Func : Functions)
	{
		delete Func;
	}
	Functions.clear();
}

bool UClass::IsAbstract() const
{
	return HasAnyClassFlags(static_cast<uint32>(EClassFlags::Abstract));
}

bool UClass::HasAnyClassFlags(uint32 Flags) const
{
	return (ClassFlags & Flags) != 0;
}

FProperty* UClass::FindProperty(const FString& Name) const
{
	for (FProperty* Prop : Properties)
	{
		if (Prop->Name == Name)
		{
			return Prop;
		}
	}
	return SuperClass ? SuperClass->FindProperty(Name) : nullptr;
}

void UClass::GetAllProperties(TArray<FProperty*>& OutProperties) const
{
	if (SuperClass)
	{
		SuperClass->GetAllProperties(OutProperties);
	}
	
	for (FProperty* Prop : Properties)
	{
		OutProperties.push_back(Prop);
	}
}

bool UClass::IsChildOf(const UClass* Other) const
{
	for (const UClass* Class = this; Class; Class = Class->SuperClass)
	{
		if (Class == Other)
		{
			return true;
		}
	}

	return false;
}

void UClass::AddProperty(FProperty* Prop)
{
	if (!Prop) return;

	for (FProperty* Existing : Properties)
	{
		if (Existing && Existing->Name == Prop->Name)
		{
			UE_LOG("Duplicate property on class %s: %s", ClassName.c_str(), Prop->Name.c_str());
			delete Prop;
			return;
		}
	}

	Properties.push_back(Prop);
}

void UClass::AddFunction(FFunction* Function)
{
	if (!Function) return;
	
	for (FFunction* Existing : Functions)
	{
		if (Existing && Existing->Name == Function->Name)
		{
			UE_LOG("Duplicate function on class %s: %s", ClassName.c_str(), Function->Name.c_str());
			delete Function;
			return;
		}
	}

	Functions.push_back(Function);
}

FFunction* UClass::FindFunction(const FString& Name) const
{
	for (FFunction* Func : Functions)
	{
		if (Func->Name == Name)
		{
			return Func;
		}
	}
	return SuperClass ? SuperClass->FindFunction(Name) : nullptr;
}

void UClass::GetFunctions(TArray<FFunction*>& OutFunctions) const
{
	for (FFunction* Func : Functions)
	{
		OutFunctions.push_back(Func);
	}
}

void UClass::GetAllFunctions(TArray<FFunction*>& OutFunctions) const
{
	if (SuperClass)
	{
		SuperClass->GetAllFunctions(OutFunctions);
	}
	
	for (FFunction* Func : Functions)
	{
		OutFunctions.push_back(Func);
	}
}
