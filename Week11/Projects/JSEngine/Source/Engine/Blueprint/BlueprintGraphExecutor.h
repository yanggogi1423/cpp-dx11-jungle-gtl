#pragma once

#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/Map.h"
#include "Core/Containers/String.h"

class UObject;
class FProperty;
struct FFunctionParameter;
class FBlueprintGraph;
struct FBlueprintNode;
struct FBlueprintPin;

struct FBlueprintPinValue
{
	FProperty* Property = nullptr;
	TArray<uint8> Data;
};

struct FBlueprintExecutionContext
{
	UObject* Self = nullptr;
	TMap<FString, FBlueprintPinValue> PinValues;
	TMap<FString, void*> EventParameterValues;
};

class FBlueprintGraphExecutor
{
public:
	bool ExecuteEvent(const FBlueprintGraph& Graph, const FString& EventName, FBlueprintExecutionContext& Context);

private:
	bool ExecuteNode(const FBlueprintGraph& Graph, const FBlueprintNode& Node, FBlueprintExecutionContext& Context);

	const FBlueprintNode* FindNextExecNode(const FBlueprintGraph& Graph, const FBlueprintNode& Node) const;
	const FBlueprintNode* FindNextExecNodeByPinName(const FBlueprintGraph& Graph, const FBlueprintNode& Node, const FString& PinName) const;
};

FString MakePinValueKey(int32 NodeId, int32 PinId);

void* ResolveInputPinValueAddress(const FBlueprintGraph& Graph, FBlueprintExecutionContext& Context, int32 NodeId, const FBlueprintPin& InputPin);

bool SetPinValue(FBlueprintExecutionContext& Context, int32 NodeId, const FBlueprintPin& Pin, const void* ValueAddress);
void* GetPinValueAddress(FBlueprintExecutionContext& Context, int32 NodeId, const FBlueprintPin& Pin);

void* GetOrCreatePinValueAddress(FBlueprintExecutionContext& Context, int32 NodeId, const FBlueprintPin& Pin);

const FBlueprintPin* FindDataInputPinForParameter(const FBlueprintNode& Node, const FFunctionParameter* Parameter);
const FBlueprintPin* FindDataOutputPinForReturn(const FBlueprintNode& Node);
