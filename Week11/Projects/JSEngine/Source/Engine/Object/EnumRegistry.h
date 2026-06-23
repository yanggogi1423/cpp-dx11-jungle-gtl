#pragma once

#include "Core/Singleton.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"

class UEnum;

class FEnumRegistry : public TSingleton<FEnumRegistry>
{
	friend class TSingleton<FEnumRegistry>;
public:
	void RegisterEnum(UEnum* Enum);
	UEnum* FindEnum(const FString& Name) const;

private:
	~FEnumRegistry();

private:
	TArray<UEnum*> Enums;
};
