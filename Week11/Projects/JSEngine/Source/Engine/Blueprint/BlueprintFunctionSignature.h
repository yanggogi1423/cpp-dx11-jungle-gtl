#pragma once

#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"

class FFunction;
class FProperty;

enum class EBlueprintPinDirection : uint8
{
	Input,
	Output,
};

enum class EBlueprintPinKind : uint8
{
	Exec,
	Data,
};

struct FBlueprintPinSignature
{
	FString Name;
	FString DisplayName;
	FString TypeName;

	EBlueprintPinDirection Direction = EBlueprintPinDirection::Input;
	EBlueprintPinKind Kind = EBlueprintPinKind::Data;

	FProperty* Property = nullptr;
};

struct FBlueprintNodeSignature
{
	FString Name;
	FString DisplayName;
	FString Category;

	TArray<FBlueprintPinSignature> Pins;
};

FBlueprintNodeSignature MakeFunctionNodeSignature(const FFunction* Function);
