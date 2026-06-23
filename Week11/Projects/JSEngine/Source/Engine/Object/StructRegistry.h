#pragma once

#include "Core/Singleton.h"
#include "Core/Containers/Map.h"
#include "Core/Containers/String.h"

class UScriptStruct;

class FStructRegistry : public TSingleton<FStructRegistry>
{
	friend class TSingleton<FStructRegistry>;
public:
	void RegisterStruct(UScriptStruct* Struct);
	UScriptStruct* FindStruct(const FString& Name) const;

private:
	FStructRegistry() = default;
	~FStructRegistry();

private:
	TMap<FString, UScriptStruct*> Structs;
};
