#include "Blueprint/BlueprintGraphExecutor.h"

#include "Blueprint/BlueprintGraph.h"

#include "Object/Function.h"
#include "Object/Object.h"

bool FBlueprintGraphExecutor::ExecuteEvent(const FBlueprintGraph& Graph, const FString& EventName, FBlueprintExecutionContext& Context)
{
	for (const FBlueprintNode& Node : Graph.Nodes)
	{
		if (Node.Type == EBlueprintNodeType::Event && Node.EventName == EventName)
		{
			for (const FBlueprintPin& Pin : Node.Pins)
			{
				if (Pin.Kind != EBlueprintPinKind::Data) continue;
				if (Pin.Direction != EBlueprintPinDirection::Output) continue;

				auto It = Context.EventParameterValues.find(Pin.Name);
				if (It == Context.EventParameterValues.end()) continue;

				SetPinValue(Context, Node.Id, Pin, It->second);
			}

			const FBlueprintNode* NextNode = FindNextExecNode(Graph, Node);
			if (!NextNode) return true;

			return ExecuteNode(Graph, *NextNode, Context);
		}
	}

	return false;
}

bool FBlueprintGraphExecutor::ExecuteNode(const FBlueprintGraph& Graph, const FBlueprintNode& Node, FBlueprintExecutionContext& Context)
{
	if (!Context.Self) return false;

	switch (Node.Type)
	{
	case EBlueprintNodeType::FunctionCall:
	{
		UObject* TargetObject = Graph.ResolveNodeTargetObject(Node, Context.Self);
		if (!TargetObject) return false;

		FFunction* Function = TargetObject->GetClass()->FindFunction(Node.FunctionName);
		if (!Function) return false;

		FFunctionCallContext CallContext;
		CallContext.Object = TargetObject;

		if (Function->HasReturnValue())
		{
			FFunctionParameter* ReturnParameter = Function->GetReturnParameter();
			if (!ReturnParameter || !ReturnParameter->Property) return false;

			const FBlueprintPin* ReturnPin = FindDataOutputPinForReturn(Node);
			if (!ReturnPin) return false;

			void* ReturnAddress = GetOrCreatePinValueAddress(Context, Node.Id, *ReturnPin);

			if (!ReturnAddress) return false;

			CallContext.ReturnValue = { ReturnParameter->Property, ReturnAddress };
		}

		for (int32 ParamIndex = 0; ParamIndex < Function->GetParameterCount(); ++ParamIndex)
		{
			FFunctionParameter* Parameter = Function->GetParameter(ParamIndex);
			if (!Parameter || !Parameter->Property) return false;

			const FBlueprintPin* InputPin = FindDataInputPinForParameter(Node, Parameter);
			if (!InputPin) return false;

			void* Address = ResolveInputPinValueAddress(Graph, Context, Node.Id, *InputPin);
			if (!Address) return false;

			CallContext.Arguments.push_back({ Parameter, Address });
		}

		if (!Function->Invoke(CallContext)) return false;
	
		const FBlueprintNode* NextNode = FindNextExecNode(Graph, Node);
		if (NextNode)
		{
			return ExecuteNode(Graph, *NextNode, Context);
		}

		return true;
	}
	case EBlueprintNodeType::Branch:
	{
		const FBlueprintPin* ConditionPin = nullptr;

		for (const FBlueprintPin& Pin : Node.Pins)
		{
			if (Pin.Name == "Condition")
			{
				ConditionPin = &Pin;
				break;
			}
		}

		if (!ConditionPin) return false;

		void* Address = ResolveInputPinValueAddress(Graph, Context, Node.Id, *ConditionPin);
		if (!Address) return false;

		const bool bCondition = *reinterpret_cast<bool*>(Address);

		const FBlueprintNode* NextNode = FindNextExecNodeByPinName(Graph, Node, bCondition ? "True" : "False");

		if (NextNode)
		{
			return ExecuteNode(Graph, *NextNode, Context);
		}

		return true;
	}
	case EBlueprintNodeType::Sequence:
	{
		for (const FBlueprintPin& Pin : Node.Pins)
		{
			if (Pin.Kind != EBlueprintPinKind::Exec) continue;
			if (Pin.Direction != EBlueprintPinDirection::Output) continue;

			const FBlueprintNode* NextNode = FindNextExecNodeByPinName(Graph, Node, Pin.Name);
			if (NextNode)
			{
				if (!ExecuteNode(Graph, *NextNode, Context)) return false;
			}
		}

		return true;
	}
	default:
		return false;
	}
}

const FBlueprintNode* FBlueprintGraphExecutor::FindNextExecNode(const FBlueprintGraph& Graph, const FBlueprintNode& Node) const
{
	const FBlueprintPin* ExecOutPin = nullptr;

	for (const FBlueprintPin& Pin : Node.Pins)
	{
		if (Pin.Kind == EBlueprintPinKind::Exec && Pin.Direction == EBlueprintPinDirection::Output)
		{
			ExecOutPin = &Pin;
			break;
		}
	}

	if (!ExecOutPin) return nullptr;

	for (const FBlueprintLink& Link : Graph.Links)
	{
		if (Link.FromNodeId == Node.Id && Link.FromPinId == ExecOutPin->Id)
		{
			return Graph.FindNode(Link.ToNodeId);
		}
	}

	return nullptr;
}

const FBlueprintNode* FBlueprintGraphExecutor::FindNextExecNodeByPinName(const FBlueprintGraph& Graph, const FBlueprintNode& Node, const FString& PinName) const
{
	const FBlueprintPin* ExecOutPin = nullptr;

	for (const FBlueprintPin& Pin : Node.Pins)
	{
		if (Pin.Name == PinName && Pin.Kind == EBlueprintPinKind::Exec && Pin.Direction == EBlueprintPinDirection::Output)
		{
			ExecOutPin = &Pin;
			break;
		}
	}

	if (!ExecOutPin) return nullptr;

	for (const FBlueprintLink& Link : Graph.Links)
	{
		if (Link.FromNodeId == Node.Id && Link.FromPinId == ExecOutPin->Id)
		{
			return Graph.FindNode(Link.ToNodeId);
		}
	}

	return nullptr;
}

FString MakePinValueKey(int32 NodeId, int32 PinId)
{
	return std::to_string(NodeId) + ":" + std::to_string(PinId);
}

void* ResolveInputPinValueAddress(const FBlueprintGraph& Graph, FBlueprintExecutionContext& Context, int32 NodeId, const FBlueprintPin& InputPin)
{
	for (const FBlueprintLink& Link : Graph.Links)
	{
		if (Link.ToNodeId == NodeId && Link.ToPinId == InputPin.Id)
		{
			const FBlueprintPin* FromPin = Graph.FindPin(Link.FromNodeId, Link.FromPinId);
			if (!FromPin) return nullptr;

			const FBlueprintNode* FromNode = Graph.FindNode(Link.FromNodeId);
			if (!FromNode) return nullptr;

			if (FromNode->Type == EBlueprintNodeType::Literal)
			{
				const void* LiteralAddress = FromNode->LiteralData.empty()
					? nullptr
					: FromNode->LiteralData.data();

				if (!LiteralAddress) return nullptr;

				SetPinValue(Context, FromNode->Id, *FromPin, LiteralAddress);
			}

			return GetPinValueAddress(Context, Link.FromNodeId, *FromPin);
		}
	}

	return GetPinValueAddress(Context, NodeId, InputPin);
}

bool SetPinValue(FBlueprintExecutionContext& Context, int32 NodeId, const FBlueprintPin& Pin, const void* ValueAddress)
{
	if (!Pin.Property || !ValueAddress) return false;

	FBlueprintPinValue Value;
	Value.Property = Pin.Property;
	Value.Data.resize(Pin.Property->Size);

	std::memcpy(Value.Data.data(), ValueAddress, Pin.Property->Size);

	Context.PinValues[MakePinValueKey(NodeId, Pin.Id)] = Value;
	return true;
}

void* GetPinValueAddress(FBlueprintExecutionContext& Context, int32 NodeId, const FBlueprintPin& Pin)
{
	const FString Key = MakePinValueKey(NodeId, Pin.Id);

	auto It = Context.PinValues.find(Key);
	if (It == Context.PinValues.end()) return nullptr;

	FBlueprintPinValue& Value = It->second;

	if (Value.Property != Pin.Property) return nullptr;
	if (Value.Data.empty()) return nullptr;

	return Value.Data.data();
}

void* GetOrCreatePinValueAddress(FBlueprintExecutionContext& Context, int32 NodeId, const FBlueprintPin& Pin)
{
	if (!Pin.Property) return nullptr;

	const FString Key = MakePinValueKey(NodeId, Pin.Id);

	FBlueprintPinValue& Value = Context.PinValues[Key];

	if (!Value.Property)
	{
		Value.Property = Pin.Property;
		Value.Data.resize(Pin.Property->Size);
	}

	if (Value.Property != Pin.Property) return nullptr;
	if (Value.Data.size() != Pin.Property->Size)
	{
		Value.Data.resize(Pin.Property->Size);
	}

	return Value.Data.data();
}

const FBlueprintPin* FindDataInputPinForParameter(const FBlueprintNode& Node, const FFunctionParameter* Parameter)
{
	if (!Parameter || !Parameter->Property) return nullptr;

	for (const FBlueprintPin& Pin : Node.Pins)
	{
		if (Pin.Kind != EBlueprintPinKind::Data) continue;
		if (Pin.Direction != EBlueprintPinDirection::Input) continue;
		if (Pin.Property == Parameter->Property)
		{
			return &Pin;
		}
	}

	return nullptr;
}

const FBlueprintPin* FindDataOutputPinForReturn(const FBlueprintNode& Node)
{
	for (const FBlueprintPin& Pin : Node.Pins)
	{
		if (Pin.Kind != EBlueprintPinKind::Data) continue;
		if (Pin.Direction != EBlueprintPinDirection::Output) continue;
		if (Pin.Name == "ReturnValue")
		{
			return &Pin;
		}
	}

	return nullptr;
}
