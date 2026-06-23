#pragma once

#include "UI/EditorWidget.h"

#include "Blueprint/BlueprintGraph.h"

#include "ImGui/imgui_node_editor.h"

class AActor;
class UClass;
class UObject;
class UBlueprintAsset;

struct FBlueprintValidationMessage
{
	int32 NodeId = 0;
	int32 PinId = 0;
	FString Message;
};

class FEditorBlueprintWidget : public FEditorWidget
{
public:
	void Initialize(UEditorEngine* InEditorEngine) override;
	void Shutdown();

	void Render(float DeltaTime) override;
	void RenderEmbedded(float DeltaTime);

	bool OpenAsset(const FString& InAssetPath);
	bool SaveAsset();

	const FString& GetAssetPath() const { return AssetPath; }

	void SetContextObject(UObject* InContextObject) { ContextObject = InContextObject; }

private:
	void DrawContent(float DeltaTime);
	void RenderToolbar();
	void RenderValidationMessages();
	void RenderGraph();
	void RenderDetailsPanel();
	void UpdateSelectionFromEditor(const FBlueprintGraph& Graph);
	void RenderSelectedNodeDetails(FBlueprintNode& Node);
	void RenderNodeHeader(const FBlueprintNode& Node);
	void RenderPin(const FBlueprintPin& Pin);
	void ValidateGraph();
	void ValidateNodePins(const FBlueprintGraph& Graph, const FBlueprintNode& Node);
	void ValidateFunctionNode(const FBlueprintNode& Node);

	void RenderNodeContextMenu();
	void AddNodeAtContextPosition(EBlueprintNodeType Type);
	void AddEventNodeAtContextPosition(const FFunction* Function);
	void AddFunctionNodeAtContextPosition(UClass* TargetClass, const FString& FunctionName,
		EBlueprintFunctionTargetType TargetType, const FString& TargetComponentName = "");

	void RenderEventMenu(UClass* TargetClass);
	void RenderFunctionMenu(const char* Label, UClass* TargetClass, EBlueprintFunctionTargetType TargetType, const FString& TargetComponentName = "");
	void RenderLiteralNodeBody(FBlueprintNode& Node);
	void AddFloatLiteralAtContextPosition(float Value);
	void AddBoolLiteralAtContextPosition(bool Value);

private:
	UBlueprintAsset* Asset = nullptr;
	FString AssetPath = "Asset/Blueprint/BP_TargetTest.uasset";

	UObject* ContextObject = nullptr;

	bool bLoaded = false;
	bool bDirty = false;
	bool bInitializedNodePositions = false;

	int32 SelectedNodeId = 0;

	TArray<FBlueprintValidationMessage> ValidationMessages;

	ImVec2 ContextMenuCanvasPosition = ImVec2(0.0f, 0.0f);

	ax::NodeEditor::EditorContext* NodeEditorContext = nullptr;
};
