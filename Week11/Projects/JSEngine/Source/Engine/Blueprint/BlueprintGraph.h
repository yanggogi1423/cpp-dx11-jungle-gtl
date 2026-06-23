#pragma once

#include "Blueprint/BlueprintFunctionSignature.h"

#include "Core/CoreTypes.h"
#include "Core/Containers/String.h"

struct FArchive;
class UObject;

enum class EBlueprintNodeType : uint8
{
	Unknown,
	Event,
	FunctionCall,
	Literal,
	Branch,
	Sequence,
};

enum class EBlueprintFunctionTargetType
{
	Self,
	Owner,
	Component
};

struct FBlueprintPin
{
	int32 Id = 0;
	FString Name;
	FString DisplayName;
	FString TypeName;

	EBlueprintPinDirection Direction = EBlueprintPinDirection::Input;
	EBlueprintPinKind Kind = EBlueprintPinKind::Data;

	FProperty* Property = nullptr;
};

struct FBlueprintNode
{
	int32 Id = 0;
	EBlueprintNodeType Type = EBlueprintNodeType::Unknown;

	FString Name;
	FString DisplayName;
	FString Category;

	FString EventName;
	FString FunctionName;
	FString TargetClassName;

	TArray<FBlueprintPin> Pins;

	FString LiteralTypeName;
	TArray<uint8> LiteralData;
	FProperty* LiteralProperty = nullptr;

	EBlueprintFunctionTargetType TargetType = EBlueprintFunctionTargetType::Self;
	FString TargetComponentName;

	float EditorX = 0.0f;
	float EditorY = 0.0f;
};

struct FBlueprintLink
{
	int32 Id = 0;

	int32 FromNodeId = 0;
	int32 FromPinId = 0;

	int32 ToNodeId = 0;
	int32 ToPinId = 0;
};

class UClass;
class FFunction;

class FBlueprintGraph
{
public:
	FBlueprintNode* AddNode(const FBlueprintNode& Node);
	FBlueprintLink* AddLink(const FBlueprintLink& Link);

	bool RemoveNode(int32 NodeId);
	bool RemoveLink(int32 LinkId);

	FBlueprintNode* FindNode(int32 NodeId);
	const FBlueprintNode* FindNode(int32 NodeId) const;

	FBlueprintNode* FindNodeByPinId(int32 PinId);
	const FBlueprintNode* FindNodeByPinId(int32 PinId) const;

	FBlueprintPin* FindPin(int32 NodeId, int32 PinId);
	const FBlueprintPin* FindPin(int32 NodeId, int32 PinId) const;

	FBlueprintPin* FindPinByName(int32 NodeId, const FString& PinName);
	const FBlueprintPin* FindPinByName(int32 NodeId, const FString& PinName) const;

	bool ResolveRuntimeReferences(UObject* ContextObject);
	void ResolveLiteralPropertiesFromLinks();
	void ResolveLinkedDataPinProperties();

	UObject* ResolveNodeTargetObject(const FBlueprintNode& Node, UObject* ContextObject) const;

	void Serialize(FArchive& Ar, int32 PayloadVersion = 2);

private:
	void RebuildNextIds();

private:
	int32 NextNodeId = 1;
	int32 NextPinId = 1;
	int32 NextLinkId = 1;

public:
	TArray<FBlueprintNode> Nodes;
	TArray<FBlueprintLink> Links;
};

FBlueprintNode* AddEventNode(FBlueprintGraph& Graph, const FString& EventName);
FBlueprintNode* AddEventNode(FBlueprintGraph& Graph, const FFunction* Function);
FBlueprintNode* AddFunctionCallNode(FBlueprintGraph& Graph, UClass* TargetClass, const FFunction* Function);
FBlueprintNode* AddLiteralNode(FBlueprintGraph& Graph, FProperty* Property, const void* ValueAddress);
FBlueprintNode* AddBranchNode(FBlueprintGraph& Graph, FProperty* BoolProperty);
FBlueprintNode* AddSequenceNode(FBlueprintGraph& Graph, int32 OutputCount = 2);
FBlueprintLink* AddPinLink(FBlueprintGraph& Graph, int32 FromNodeId, const FString& FromPinName, int32 ToNodeId, const FString& ToPinName);

FArchive& operator<<(FArchive& Ar, FBlueprintNode& Node);
FArchive& operator<<(FArchive& Ar, FBlueprintPin& Pin);
FArchive& operator<<(FArchive& Ar, FBlueprintLink& Link);
