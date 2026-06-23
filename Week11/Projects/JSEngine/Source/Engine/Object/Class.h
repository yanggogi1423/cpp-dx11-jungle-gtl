#pragma once

#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"
#include "Object/Property.h"

class UObject;
class FFunction;

enum class EClassFlags : uint32
{
	None = 0,
	Abstract = 1 << 0,
	Actor = 1 << 1,
	Component = 1 << 2,
	BlueprintTable = 1 << 3,
};

class UClass
{
public:
	using ConstructFunc = UObject* (*)();

public:
	UClass() = default;
	~UClass();

	UClass(const FString& InClassName, UClass* InSuperClass, uint32 InClassSize, ConstructFunc InConstructor = nullptr)
		: ClassName(InClassName), SuperClass(InSuperClass), ClassSize(InClassSize), Constructor(InConstructor)
	{
	}

public:
	FString ClassName;
	UClass* SuperClass = nullptr;
	uint32 ClassSize = 0;
	ConstructFunc Constructor = nullptr;
	uint32 ClassFlags = 0;

	TArray<FProperty*> Properties;
	TArray<FFunction*> Functions;

public:
	bool IsAbstract() const;
	bool HasAnyClassFlags(uint32 Flags) const;
	FProperty* FindProperty(const FString& Name) const;
	void GetAllProperties(TArray<FProperty*>& OutProperties) const;

	bool IsChildOf(const UClass* Other) const;

	void AddProperty(FProperty* Prop);

	void AddFunction(FFunction* Function);
	FFunction* FindFunction(const FString& Name) const;
	void GetFunctions(TArray<FFunction*>& OutFunctions) const;
	void GetAllFunctions(TArray<FFunction*>& OutFunctions) const;
};