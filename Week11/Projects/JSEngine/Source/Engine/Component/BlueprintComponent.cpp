#include "Component/BlueprintComponent.h"

#include "Blueprint/BlueprintAsset.h"
#include "Blueprint/BlueprintGraph.h"
#include "Blueprint/BlueprintGraphExecutor.h"
#include "Component/Movement/InterpToMovementComponent.h"
#include "GameFramework/AActor.h"
#include "Object/Class.h"
#include "Object/Function.h"

void UBlueprintComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);
	Ar << "BlueprintAssetPath" << BlueprintAssetPath;
}

void UBlueprintComponent::BeginPlay()
{
	UActorComponent::BeginPlay();

	UE_LOG("BlueprintComponent BeginPlay");
	UE_LOG("BlueprintAssetPath: %s", BlueprintAssetPath.c_str());

	if (!Blueprint && !LoadBlueprint())
	{
		UE_LOG("Blueprint load failed");
		return;
	}

	if (!Blueprint)
	{
		UE_LOG("Blueprint is null");
		return;
	}

	FBlueprintGraph& Graph = Blueprint->GetGraph();

	UE_LOG("Blueprint graph loaded. Nodes=%d Links=%d",
		static_cast<int32>(Graph.Nodes.size()),
		static_cast<int32>(Graph.Links.size()));

	if (!Graph.ResolveRuntimeReferences(this))
	{
		UE_LOG("Blueprint resolve failed");
		return;
	}

	UE_LOG("Blueprint resolve success");

	FBlueprintExecutionContext Context;
	Context.Self = this;

	FBlueprintGraphExecutor Executor;
	const bool bExecuted = Executor.ExecuteEvent(Graph, "BeginPlay", Context);

	UE_LOG("Blueprint ExecuteEvent result: %d", bExecuted ? 1 : 0);
}

void UBlueprintComponent::TickComponent(float DeltaTime)
{
	UActorComponent::TickComponent(DeltaTime);

	if (!Blueprint && !LoadBlueprint())
	{
		return;
	}

	if (!Blueprint)
	{
		return;
	}

	FBlueprintGraph& Graph = Blueprint->GetGraph();
	if (!Graph.ResolveRuntimeReferences(this))
	{
		return;
	}

	FBlueprintExecutionContext Context;
	Context.Self = this;
	Context.EventParameterValues["DeltaTime"] = &DeltaTime;

	FBlueprintGraphExecutor Executor;
	Executor.ExecuteEvent(Graph, "Tick", Context);
}

void UBlueprintComponent::Tick(float DeltaTime)
{
}

void UBlueprintComponent::TestSelfFunction()
{
	UE_LOG("TestSelfFunction called self.");
}

void UBlueprintComponent::SaveTargetTestBlueprint()
{
	const FString Path = "Asset/Blueprint/BP_TargetTest.uasset";

	UBlueprintAsset Asset;
	FBlueprintGraph& Graph = Asset.GetGraph();

	FBlueprintNode* EventNode = AddEventNode(Graph, "BeginPlay");
	if (!EventNode)
	{
		UE_LOG("Failed to create blueprint test event node.");
		return;
	}
	const int32 EventNodeId = EventNode->Id;

	FBlueprintNode* SequenceNode = AddSequenceNode(Graph, 3);
	if (!SequenceNode)
	{
		UE_LOG("Failed to create blueprint test sequence node.");
		return;
	}
	const int32 SequenceNodeId = SequenceNode->Id;

	if (!AddPinLink(Graph, EventNodeId, "Then", SequenceNodeId, "Execute"))
	{
		UE_LOG("Failed to link Event BeginPlay to Sequence.");
		return;
	}

	FFunction* SelfFunction = UBlueprintComponent::StaticClass()->FindFunction("TestSelfFunction");
	FBlueprintNode* SelfNode = AddFunctionCallNode(Graph, UBlueprintComponent::StaticClass(), SelfFunction);
	if (!SelfNode)
	{
		UE_LOG("Failed to create Self function call node.");
		return;
	}
	SelfNode->TargetType = EBlueprintFunctionTargetType::Self;
	const int32 SelfNodeId = SelfNode->Id;
	if (!AddPinLink(Graph, SequenceNodeId, "Then0", SelfNodeId, "Execute"))
	{
		UE_LOG("Failed to link Sequence Then0 to Self function.");
		return;
	}

	FFunction* OwnerFunction = AActor::StaticClass()->FindFunction("TestOwnerFunction");
	FBlueprintNode* OwnerNode = AddFunctionCallNode(Graph, AActor::StaticClass(), OwnerFunction);
	if (!OwnerNode)
	{
		UE_LOG("Failed to create Owner function call node.");
		return;
	}
	OwnerNode->TargetType = EBlueprintFunctionTargetType::Owner;
	const int32 OwnerNodeId = OwnerNode->Id;
	if (!AddPinLink(Graph, SequenceNodeId, "Then1", OwnerNodeId, "Execute"))
	{
		UE_LOG("Failed to link Sequence Then1 to Owner function.");
		return;
	}

	FFunction* ComponentFunction = UInterpToMovementComponent::StaticClass()->FindFunction("TestComponentFunction");
	FBlueprintNode* ComponentNode = AddFunctionCallNode(Graph, UInterpToMovementComponent::StaticClass(), ComponentFunction);
	if (!ComponentNode)
	{
		UE_LOG("Failed to create Component function call node.");
		return;
	}
	ComponentNode->TargetType = EBlueprintFunctionTargetType::Component;
	ComponentNode->TargetComponentName = UInterpToMovementComponent::StaticClass()->ClassName;
	const int32 ComponentNodeId = ComponentNode->Id;
	if (!AddPinLink(Graph, SequenceNodeId, "Then2", ComponentNodeId, "Execute"))
	{
		UE_LOG("Failed to link Sequence Then2 to Component function.");
		return;
	}

	if (!Asset.SaveToFile(Path))
	{
		UE_LOG("Failed to save blueprint target test asset: %s", Path.c_str());
		return;
	}

	BlueprintAssetPath = Path;
	UE_LOG("Saved blueprint target test asset: %s", Path.c_str());
}

bool UBlueprintComponent::LoadBlueprint()
{
	if (BlueprintAssetPath.empty()) return false;

	UBlueprintAsset* Loaded = new UBlueprintAsset();
	if (!Loaded->LoadFromFile(BlueprintAssetPath))
	{
		delete Loaded;
		return false;
	}

	Blueprint = Loaded;
	return true;
}
