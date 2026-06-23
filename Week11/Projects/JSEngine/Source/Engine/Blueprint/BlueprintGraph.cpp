#include "Blueprint/BlueprintGraph.h"

#include "Component/ActorComponent.h"
#include "GameFramework/AActor.h"

#include "Object/Class.h"
#include "Object/Function.h"
#include "Object/Property.h"

#include "Serialization/Archive.h"

namespace
{
int32 GBlueprintSerializationPayloadVersion = 2;
}

FBlueprintNode* FBlueprintGraph::AddNode(const FBlueprintNode& Node)
{
	FBlueprintNode NewNode = Node;

	if (NewNode.Id == 0)
	{
		NewNode.Id = NextNodeId++;
	}
	else if (NewNode.Id >= NextNodeId)
	{
		NextNodeId = NewNode.Id + 1;
	}

	for (FBlueprintPin& Pin : NewNode.Pins)
	{
		if (Pin.Id == 0)
		{
			Pin.Id = NextPinId++;
		}
		else if (Pin.Id >= NextPinId)
		{
			NextPinId = Pin.Id + 1;
		}
	}

	Nodes.push_back(NewNode);
	return &Nodes.back();
}

FBlueprintLink* FBlueprintGraph::AddLink(const FBlueprintLink& Link)
{
	const FBlueprintPin* FromPin = FindPin(Link.FromNodeId, Link.FromPinId);
	const FBlueprintPin* ToPin = FindPin(Link.ToNodeId, Link.ToPinId);

	if (!FromPin || !ToPin) return nullptr;

	if (FromPin->Direction != EBlueprintPinDirection::Output) return nullptr;
	if (ToPin->Direction != EBlueprintPinDirection::Input) return nullptr;
	if (FromPin->Kind != ToPin->Kind) return nullptr;
	if (FromPin->TypeName != ToPin->TypeName) return nullptr;

	FBlueprintLink NewLink = Link;

	if (NewLink.Id == 0)
	{
		NewLink.Id = NextLinkId++;
	}
	else if (NewLink.Id >= NextLinkId)
	{
		NextLinkId = NewLink.Id + 1;
	}

	Links.push_back(NewLink);
	return &Links.back();
}

bool FBlueprintGraph::RemoveNode(int32 NodeId)
{
	bool bRemoved = false;

	for (auto It = Links.begin(); It != Links.end(); )
	{
		if (It->FromNodeId == NodeId || It->ToNodeId == NodeId)
		{
			It = Links.erase(It);
		}
		else
		{
			++It;
		}
	}

	for (auto It = Nodes.begin(); It != Nodes.end(); ++It)
	{
		if (It->Id == NodeId)
		{
			Nodes.erase(It);
			bRemoved = true;
			break;
		}
	}

	return bRemoved;
}

bool FBlueprintGraph::RemoveLink(int32 LinkId)
{
	for (auto It = Links.begin(); It != Links.end(); ++It)
	{
		if (It->Id == LinkId)
		{
			Links.erase(It);
			return true;
		}
	}

	return false;
}

FBlueprintNode* FBlueprintGraph::FindNode(int32 NodeId)
{
	for (FBlueprintNode& Node : Nodes)
	{
		if (Node.Id == NodeId)
		{
			return &Node;
		}
	}

	return nullptr;
}

const FBlueprintNode* FBlueprintGraph::FindNode(int32 NodeId) const
{
	for (const FBlueprintNode& Node : Nodes)
	{
		if (Node.Id == NodeId)
		{
			return &Node;
		}
	}

	return nullptr;
}

FBlueprintNode* FBlueprintGraph::FindNodeByPinId(int32 PinId)
{
	for (FBlueprintNode& Node : Nodes)
	{
		for (const FBlueprintPin& Pin : Node.Pins)
		{
			if (Pin.Id == PinId)
			{
				return &Node;
			}
		}
	}

	return nullptr;
}

const FBlueprintNode* FBlueprintGraph::FindNodeByPinId(int32 PinId) const
{
	for (const FBlueprintNode& Node : Nodes)
	{
		for (const FBlueprintPin& Pin : Node.Pins)
		{
			if (Pin.Id == PinId)
			{
				return &Node;
			}
		}
	}

	return nullptr;
}

FBlueprintPin* FBlueprintGraph::FindPin(int32 NodeId, int32 PinId)
{
	FBlueprintNode* Node = FindNode(NodeId);
	if (!Node) return nullptr;

	for (FBlueprintPin& Pin : Node->Pins)
	{
		if (Pin.Id == PinId)
		{
			return &Pin;
		}
	}

	return nullptr;
}

const FBlueprintPin* FBlueprintGraph::FindPin(int32 NodeId, int32 PinId) const
{
	const FBlueprintNode* Node = FindNode(NodeId);
	if (!Node) return nullptr;

	for (const FBlueprintPin& Pin : Node->Pins)
	{
		if (Pin.Id == PinId)
		{
			return &Pin;
		}
	}

	return nullptr;
}

FBlueprintPin* FBlueprintGraph::FindPinByName(int32 NodeId, const FString& PinName)
{
	FBlueprintNode* Node = FindNode(NodeId);
	if (!Node) return nullptr;

	for (FBlueprintPin& Pin : Node->Pins)
	{
		if (Pin.Name == PinName)
		{
			return &Pin;
		}
	}

	return nullptr;
}

const FBlueprintPin* FBlueprintGraph::FindPinByName(int32 NodeId, const FString& PinName) const
{
	const FBlueprintNode* Node = FindNode(NodeId);
	if (!Node) return nullptr;

	for (const FBlueprintPin& Pin : Node->Pins)
	{
		if (Pin.Name == PinName)
		{
			return &Pin;
		}
	}

	return nullptr;
}

bool FBlueprintGraph::ResolveRuntimeReferences(UObject* ContextObject)
{
	if (!ContextObject || !ContextObject->GetClass()) return false;

	bool bSuccess = true;

	for (FBlueprintNode& Node : Nodes)
	{
		switch (Node.Type)
		{
		case EBlueprintNodeType::Event:
		{
			FFunction* Function = ContextObject->GetClass()->FindFunction(Node.EventName);
			if (!Function)
			{
				bSuccess = false;
				break;
			}

			for (FBlueprintPin& Pin : Node.Pins)
			{
				if (Pin.Kind != EBlueprintPinKind::Data) continue;
				if (Pin.Direction != EBlueprintPinDirection::Output) continue;

				FFunctionParameter* Parameter = Function->FindParameter(Pin.Name);
				Pin.Property = Parameter ? Parameter->Property : nullptr;

				if (!Pin.Property)
				{
					bSuccess = false;
				}
			}

			break;
		}
		case EBlueprintNodeType::FunctionCall:
		{
			UObject* TargetObject = ResolveNodeTargetObject(Node, ContextObject);
			if (!TargetObject) return false;

			FFunction* Function = TargetObject->GetClass()->FindFunction(Node.FunctionName);
			if (!Function)
			{
				bSuccess = false;
				break;
			}

			for (FBlueprintPin& Pin : Node.Pins)
			{
				if (Pin.Kind != EBlueprintPinKind::Data) continue;

				if (Pin.Direction == EBlueprintPinDirection::Input)
				{
					FFunctionParameter* Parameter = Function->FindParameter(Pin.Name);
					Pin.Property = Parameter ? Parameter->Property : nullptr;
				}
				else if (Pin.Direction == EBlueprintPinDirection::Output && Pin.Name == "ReturnValue")
				{
					Pin.Property = Function->GetReturnProperty();
				}

				if (!Pin.Property)
				{
					bSuccess = false;
				}
			}
			
			break;
		}
		case EBlueprintNodeType::Literal:
		{
			break;
		}
		default:
			break;
		}
	}

	ResolveLiteralPropertiesFromLinks();
	ResolveLinkedDataPinProperties();

	return bSuccess;
}

void FBlueprintGraph::ResolveLiteralPropertiesFromLinks()
{
	for (const FBlueprintLink& Link : Links)
	{
		FBlueprintNode* FromNode = FindNode(Link.FromNodeId);
		FBlueprintPin* FromPin = FindPin(Link.FromNodeId, Link.FromPinId);
		FBlueprintPin* ToPin = FindPin(Link.ToNodeId, Link.ToPinId);

		if (!FromNode || !FromPin || !ToPin) continue;
		if (FromNode->Type != EBlueprintNodeType::Literal) continue;
		if (FromPin->Kind != EBlueprintPinKind::Data) continue;
		if (!ToPin->Property) continue;

		FromPin->Property = ToPin->Property;
		FromNode->LiteralProperty = ToPin->Property;
	}
}

void FBlueprintGraph::ResolveLinkedDataPinProperties()
{
	bool bChanged = true;

	while (bChanged)
	{
		bChanged = false;

		for (FBlueprintLink& Link : Links)
		{
			FBlueprintPin* FromPin = FindPin(Link.FromNodeId, Link.FromPinId);
			FBlueprintPin* ToPin = FindPin(Link.ToNodeId, Link.ToPinId);

			if (!FromPin || !ToPin) continue;
			if (FromPin->Kind != EBlueprintPinKind::Data || ToPin->Kind != EBlueprintPinKind::Data) continue;

			if (FromPin->Property && !ToPin->Property)
			{
				ToPin->Property = FromPin->Property;
				bChanged = true;
			}
			else if (!FromPin->Property && ToPin->Property)
			{
				FromPin->Property = ToPin->Property;
				bChanged = true;
			}
		}
	}

	for (FBlueprintNode& Node : Nodes)
	{
		if (Node.Type != EBlueprintNodeType::Literal) continue;

		for (FBlueprintPin& Pin : Node.Pins)
		{
			if (Pin.Kind == EBlueprintPinKind::Data && Pin.Property)
			{
				Node.LiteralProperty = Pin.Property;
				break;
			}
		}
	}
}

UObject* FBlueprintGraph::ResolveNodeTargetObject(const FBlueprintNode& Node, UObject* ContextObject) const
{
	if (!ContextObject) return nullptr;

	switch (Node.TargetType)
	{
	case EBlueprintFunctionTargetType::Self:
		return ContextObject;

	case EBlueprintFunctionTargetType::Owner:
	{
		if (UActorComponent* Component = Cast<UActorComponent>(ContextObject))
		{
			return Component->GetOwner();
		}

		if (AActor* Actor = Cast<AActor>(ContextObject))
		{
			return Actor;
		}

		return nullptr;
	}
	case EBlueprintFunctionTargetType::Component:
	{
		AActor* OwnerActor = nullptr;

		if (AActor* Actor = Cast<AActor>(ContextObject))
		{
			OwnerActor = Actor;
		}
		else if (UActorComponent* Component = Cast<UActorComponent>(ContextObject))
		{
			OwnerActor = Component->GetOwner();
		}

		if (!OwnerActor) return nullptr;

		for (UActorComponent* Component : OwnerActor->GetComponents())
		{
			if (!Component) continue;

			const bool bClassMatches =
				Node.TargetClassName.empty() ||
				Component->GetClass()->ClassName == Node.TargetClassName;

			const bool bNameMatches =
				Node.TargetComponentName.empty() ||
				Component->GetName() == Node.TargetComponentName;

			if (bClassMatches && bNameMatches)
			{
				return Component;
			}
		}

		return nullptr;
	}

	default:
		return nullptr;
	}
}

void FBlueprintGraph::Serialize(FArchive& Ar, int32 PayloadVersion)
{
	const int32 PreviousPayloadVersion = GBlueprintSerializationPayloadVersion;
	GBlueprintSerializationPayloadVersion = PayloadVersion;

	Ar << Nodes;
	Ar << Links;

	if (Ar.IsLoading())
	{
		RebuildNextIds();
	}

	GBlueprintSerializationPayloadVersion = PreviousPayloadVersion;
}

void FBlueprintGraph::RebuildNextIds()
{
	NextNodeId = 1;
	NextPinId = 1;
	NextLinkId = 1;

	for (const FBlueprintNode& Node : Nodes)
	{
		if (Node.Id >= NextNodeId)
		{
			NextNodeId = Node.Id + 1;
		}

		for (const FBlueprintPin& Pin : Node.Pins)
		{
			if (Pin.Id >= NextPinId)
			{
				NextPinId = Pin.Id + 1;
			}
		}
	}

	for (const FBlueprintLink& Link : Links)
	{
		if (Link.Id >= NextLinkId)
		{
			NextLinkId = Link.Id + 1;
		}
	}
}

FBlueprintNode* AddEventNode(FBlueprintGraph& Graph, const FString& EventName)
{
	FBlueprintNode Node;
	Node.Type = EBlueprintNodeType::Event;
	Node.Name = EventName;
	Node.DisplayName = EventName;
	Node.Category = "Event";
	Node.EventName = EventName;

	FBlueprintPin ExecPin;
	ExecPin.Name = "Then";
	ExecPin.DisplayName = "Then";
	ExecPin.TypeName = "exec";
	ExecPin.Direction = EBlueprintPinDirection::Output;
	ExecPin.Kind = EBlueprintPinKind::Exec;

	Node.Pins.push_back(ExecPin);
	
	return Graph.AddNode(Node);
}

FBlueprintNode* AddEventNode(FBlueprintGraph& Graph, const FFunction* Function)
{
	if (!Function) return nullptr;

	FBlueprintNode Node;
	Node.Type = EBlueprintNodeType::Event;
	Node.Name = Function->Name;
	Node.DisplayName = Function->DisplayName.empty()
		? Function->Name
		: Function->DisplayName;
	Node.Category = Function->Category.empty()
		? "Event"
		: Function->Category;
	Node.EventName = Function->Name;

	FBlueprintPin ExecPin;
	ExecPin.Name = "Then";
	ExecPin.DisplayName = "Then";
	ExecPin.TypeName = "exec";
	ExecPin.Direction = EBlueprintPinDirection::Output;
	ExecPin.Kind = EBlueprintPinKind::Exec;
	Node.Pins.push_back(ExecPin);

	for (int32 Index = 0; Index < Function->GetParameterCount(); ++Index)
	{
		FFunctionParameter* Parameter = Function->GetParameter(Index);
		if (!Parameter || !Parameter->Property) continue;

		FBlueprintPin Pin;
		Pin.Name = Parameter->Property->Name;
		Pin.DisplayName = Parameter->Property->DisplayName.empty()
			? Parameter->Property->Name
			: Parameter->Property->DisplayName;
		Pin.TypeName = Parameter->Property->TypeName;
		Pin.Direction = EBlueprintPinDirection::Output;
		Pin.Kind = EBlueprintPinKind::Data;
		Pin.Property = Parameter->Property;

		Node.Pins.push_back(Pin);
	}

	return Graph.AddNode(Node);
}

FBlueprintNode* AddFunctionCallNode(FBlueprintGraph& Graph, UClass* TargetClass, const FFunction* Function)
{
	if (!Function) return nullptr;

	FBlueprintNodeSignature Signature = MakeFunctionNodeSignature(Function);

	FBlueprintNode Node;
	Node.Type = EBlueprintNodeType::FunctionCall;
	Node.Name = Signature.Name;
	Node.DisplayName = Signature.DisplayName;
	Node.Category = Signature.Category;
	Node.FunctionName = Function->Name;
	Node.TargetClassName = TargetClass ? TargetClass->ClassName : "";

	for (const FBlueprintPinSignature& PinSignature : Signature.Pins)
	{
		FBlueprintPin Pin;
		Pin.Name = PinSignature.Name;
		Pin.DisplayName = PinSignature.DisplayName;
		Pin.TypeName = PinSignature.TypeName;
		Pin.Direction = PinSignature.Direction;
		Pin.Kind = PinSignature.Kind;
		Pin.Property = PinSignature.Property;

		Node.Pins.push_back(Pin);
	}

	return Graph.AddNode(Node);
}

FBlueprintNode* AddLiteralNode(FBlueprintGraph& Graph, FProperty* Property, const void* ValueAddress)
{
	if (!Property || !ValueAddress) return nullptr;

	FBlueprintNode Node;
	Node.Type = EBlueprintNodeType::Literal;
	Node.Name = "Literal";
	Node.DisplayName = Property->DisplayName.empty()
		? Property->TypeName
		: Property->DisplayName;
	Node.Category = "Literal";

	Node.LiteralTypeName = Property->TypeName;
	Node.LiteralProperty = Property;
	Node.LiteralData.resize(Property->Size);
	std::memcpy(Node.LiteralData.data(), ValueAddress, Property->Size);

	FBlueprintPin OutputPin;
	OutputPin.Name = "Value";
	OutputPin.DisplayName = "Value";
	OutputPin.TypeName = Property->TypeName;
	OutputPin.Direction = EBlueprintPinDirection::Output;
	OutputPin.Kind = EBlueprintPinKind::Data;
	OutputPin.Property = Property;

	Node.Pins.push_back(OutputPin);

	return Graph.AddNode(Node);
}

FBlueprintNode* AddBranchNode(FBlueprintGraph& Graph, FProperty* BoolProperty)
{
	FBlueprintNode Node;
	Node.Type = EBlueprintNodeType::Branch;
	Node.Name = "Branch";
	Node.DisplayName = "Branch";
	Node.Category = "Flow Control";

	FBlueprintPin ExecutePin;
	ExecutePin.Name = "Execute";
	ExecutePin.DisplayName = "Execute";	
	ExecutePin.TypeName = "exec";
	ExecutePin.Direction = EBlueprintPinDirection::Input;
	ExecutePin.Kind = EBlueprintPinKind::Exec;
	Node.Pins.push_back(ExecutePin);

	FBlueprintPin ConditionPin;
	ConditionPin.Name = "Condition";
	ConditionPin.DisplayName = "Condition";
	ConditionPin.TypeName = "bool";
	ConditionPin.Direction = EBlueprintPinDirection::Input;
	ConditionPin.Kind = EBlueprintPinKind::Data;
	ConditionPin.Property = BoolProperty;
	Node.Pins.push_back(ConditionPin);

	FBlueprintPin TruePin;
	TruePin.Name = "True";
	TruePin.DisplayName = "True";
	TruePin.TypeName = "exec";
	TruePin.Direction = EBlueprintPinDirection::Output;
	TruePin.Kind = EBlueprintPinKind::Exec;
	Node.Pins.push_back(TruePin);

	FBlueprintPin FalsePin;
	FalsePin.Name = "False";
	FalsePin.DisplayName = "False";
	FalsePin.TypeName = "exec";
	FalsePin.Direction = EBlueprintPinDirection::Output;
	FalsePin.Kind = EBlueprintPinKind::Exec;
	Node.Pins.push_back(FalsePin);

	return Graph.AddNode(Node);
}

FBlueprintNode* AddSequenceNode(FBlueprintGraph& Graph, int32 OutputCount)
{
	if (OutputCount <= 0) return nullptr;

	FBlueprintNode Node;
	Node.Type = EBlueprintNodeType::Sequence;
	Node.Name = "Sequence";
	Node.DisplayName = "Sequence";
	Node.Category = "Flow Control";

	FBlueprintPin ExecutePin;
	ExecutePin.Name = "Execute";
	ExecutePin.DisplayName = "Execute";
	ExecutePin.TypeName = "exec";
	ExecutePin.Direction = EBlueprintPinDirection::Input;
	ExecutePin.Kind = EBlueprintPinKind::Exec;
	Node.Pins.push_back(ExecutePin);

	for (int32 Index = 0; Index < OutputCount; ++Index)
	{
		FBlueprintPin ThenPin;
		ThenPin.Name = FString("Then") + std::to_string(Index);
		ThenPin.DisplayName = ThenPin.Name;
		ThenPin.TypeName = "exec";
		ThenPin.Direction = EBlueprintPinDirection::Output;
		ThenPin.Kind = EBlueprintPinKind::Exec;
		Node.Pins.push_back(ThenPin);
	}

	return Graph.AddNode(Node);
}

FBlueprintLink* AddPinLink(FBlueprintGraph& Graph, int32 FromNodeId, const FString& FromPinName, int32 ToNodeId, const FString& ToPinName)
{
	FBlueprintPin* FromPin = Graph.FindPinByName(FromNodeId, FromPinName);
	FBlueprintPin* ToPin = Graph.FindPinByName(ToNodeId, ToPinName);

	if (!FromPin || !ToPin) return nullptr;

	FBlueprintLink Link;
	Link.FromNodeId = FromNodeId;
	Link.FromPinId = FromPin->Id;
	Link.ToNodeId = ToNodeId;
	Link.ToPinId = ToPin->Id;

	return Graph.AddLink(Link);
}

static void SerializeNodeType(FArchive& Ar, EBlueprintNodeType& Type)
{
	int32 Value = static_cast<int32>(Type);
	Ar << "Type" << Value;

	if (Ar.IsLoading())
	{
		Type = static_cast<EBlueprintNodeType>(Value);
	}
}

static void SerializePinDirection(FArchive& Ar, EBlueprintPinDirection& Direction)
{
	int32 Value = static_cast<int32>(Direction);
	Ar << "Direction" << Value;

	if (Ar.IsLoading())
	{
		Direction = static_cast<EBlueprintPinDirection>(Value);
	}
}

static void SerializePinKind(FArchive& Ar, EBlueprintPinKind& Kind)
{
	int32 Value = static_cast<int32>(Kind);
	Ar << "Kind" << Value;

	if (Ar.IsLoading())
	{
		Kind = static_cast<EBlueprintPinKind>(Value);
	}
}

FArchive& operator<<(FArchive& Ar, FBlueprintPin& Pin)
{
	Ar << "Id" << Pin.Id;
	Ar << "Name" << Pin.Name;
	Ar << "DisplayName" << Pin.DisplayName;
	Ar << "TypeName" << Pin.TypeName;

	SerializePinDirection(Ar, Pin.Direction);
	SerializePinKind(Ar, Pin.Kind);

	if (Ar.IsLoading())
	{
		Pin.Property = nullptr;
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FBlueprintLink& Link)
{
	Ar << "Id" << Link.Id;
	Ar << "FromNodeId" << Link.FromNodeId;
	Ar << "FromPinId" << Link.FromPinId;
	Ar << "ToNodeId" << Link.ToNodeId;
	Ar << "ToPinId" << Link.ToPinId;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FBlueprintNode& Node)
{
	Ar << "Id" << Node.Id;

	SerializeNodeType(Ar, Node.Type);

	Ar << "Name" << Node.Name;
	Ar << "DisplayName" << Node.DisplayName;
	Ar << "Category" << Node.Category;

	Ar << "EventName" << Node.EventName;
	Ar << "FunctionName" << Node.FunctionName;
	Ar << "TargetClassName" << Node.TargetClassName;

	Ar << "Pins" << Node.Pins;

	Ar << "LiteralTypeName" << Node.LiteralTypeName;

	int32 LiteralDataSize = static_cast<int32>(Node.LiteralData.size());
	Ar << "LiteralDataSize" << LiteralDataSize;

	if (Ar.IsLoading())
	{
		Node.LiteralData.resize(LiteralDataSize);
	}

	if (LiteralDataSize > 0)
	{
		Ar.Serialize(Node.LiteralData.data(), static_cast<uint32>(LiteralDataSize));
	}

	if (Ar.IsLoading())
	{
		Node.LiteralProperty = nullptr;
	}

	int32 TargetTypeValue = static_cast<int32>(Node.TargetType);
	Ar << TargetTypeValue;

	if (Ar.IsLoading())
	{
		Node.TargetType = static_cast<EBlueprintFunctionTargetType>(TargetTypeValue);
	}

	Ar << Node.TargetComponentName;

	if (GBlueprintSerializationPayloadVersion >= 2)
	{
		Ar << "EditorX" << Node.EditorX;
		Ar << "EditorY" << Node.EditorY;
	}
	else if (Ar.IsLoading())
	{
		Node.EditorX = 0.0f;
		Node.EditorY = 0.0f;
	}

	return Ar;
}
