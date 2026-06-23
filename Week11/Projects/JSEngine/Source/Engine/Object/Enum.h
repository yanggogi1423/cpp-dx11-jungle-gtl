#pragma once

#include "Core/Containers/String.h"
#include "Core/Containers/Array.h"
#include "Core/CoreTypes.h"

struct FEnumValue
{
	FString Name;
	int64 Value = 0;
};

class UEnum
{
public:
	FString Name;
	TArray<FEnumValue> Values;

	int32 NumEnums() const { return static_cast<int32>(Values.size()); }
	const FString& GetNameByIndex(int32 Index) const { return Values[Index].Name; }
	int64 GetValueByIndex(int32 Index) const { return Values[Index].Value; }
};
