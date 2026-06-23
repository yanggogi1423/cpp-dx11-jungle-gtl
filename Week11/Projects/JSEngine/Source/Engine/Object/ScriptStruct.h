#pragma once

#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"

class FProperty;

class UScriptStruct
{
public:
	~UScriptStruct();

	FString Name;
	uint32 Size = 0;
	TArray<FProperty*> Properties;

	void AddProperty(FProperty* Property);
	void GetProperties(TArray<FProperty*>& OutProperties) const;
	FProperty* FindProperty(const FString& PropertyName) const;
};
