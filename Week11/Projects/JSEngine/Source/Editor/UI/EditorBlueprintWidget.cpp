#include "UI/EditorBlueprintWidget.h"

#include "Blueprint/BlueprintAsset.h"
#include "Blueprint/BlueprintGraph.h"
#include "Component/BlueprintComponent.h"
#include "Component/Movement/InterpToMovementComponent.h"
#include "Core/Paths.h"
#include "Editor/EditorEngine.h"
#include "Editor/Notification/EditorNotificationService.h"
#include "Object/Function.h"
#include "Object/Class.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_node_editor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace ed = ax::NodeEditor;

namespace
{

#pragma region NodeIdHelper

constexpr int32 BlueprintEditorNodeIdOffset = 1000000;
constexpr int32 BlueprintEditorPinIdOffset = 2000000;
constexpr int32 BlueprintEditorLinkIdOffset = 3000000;

std::unordered_map<int32, ImVec2> GBlueprintPinScreenCenters;

FString GetFileNameFromPath(const FString& Path)
{
	const size_t SlashIndex = Path.find_last_of("/\\");
	return SlashIndex == FString::npos ? Path : Path.substr(SlashIndex + 1);
}

ed::NodeId MakeEditorNodeId(int32 NodeId)
{
	return ed::NodeId(BlueprintEditorNodeIdOffset + NodeId);
}

ed::PinId MakeEditorPinId(int32 PinId)
{
	return ed::PinId(BlueprintEditorPinIdOffset + PinId);
}

ed::LinkId MakeEditorLinkId(int32 LinkId)
{
	return ed::LinkId(BlueprintEditorLinkIdOffset + LinkId);
}

int32 FromEditorLinkId(ed::LinkId LinkId)
{
	return static_cast<int32>(LinkId.Get()) - BlueprintEditorLinkIdOffset;
}

int32 FromEditorPinId(ed::PinId PinId)
{
	return static_cast<int32>(PinId.Get()) - BlueprintEditorPinIdOffset;
}

int32 FromEditorNodeId(ed::NodeId NodeId)
{
	return static_cast<int32>(NodeId.Get()) - BlueprintEditorNodeIdOffset;
}

#pragma endregion

#pragma region Link Helper

bool CanCreateLink(const FBlueprintGraph& Graph, int32 FromPinId, int32 ToPinId, FString* OutReason = nullptr)
{
	const FBlueprintNode* FromNode = Graph.FindNodeByPinId(FromPinId);
	const FBlueprintNode* ToNode = Graph.FindNodeByPinId(ToPinId);

	const FBlueprintPin* FromPin = nullptr;
	const FBlueprintPin* ToPin = nullptr;

	if (FromNode)
	{
		FromPin = Graph.FindPin(FromNode->Id, FromPinId);
	}

	if (ToNode)
	{
		ToPin = Graph.FindPin(ToNode->Id, ToPinId);
	}

	if (!FromNode || !ToNode || !FromPin || !ToPin)
	{
		if (OutReason) *OutReason = "Invalid pin";
		return false;
	}

	if (FromNode->Id == ToNode->Id)
	{
		if (OutReason) *OutReason = "Cannot link pins on the same node";
		return false;
	}

	if (FromPin->Direction == ToPin->Direction)
	{
		if (OutReason) *OutReason = "Pin directions must be Output -> Input";
		return false;
	}

	if (FromPin->Direction == EBlueprintPinDirection::Input)
	{
		std::swap(FromPinId, ToPinId);
		std::swap(FromPin, ToPin);
	}

	if (FromPin->Direction != EBlueprintPinDirection::Output ||
		ToPin->Direction != EBlueprintPinDirection::Input)
	{
		if (OutReason) *OutReason = "Link must start from output and end at input";
		return false;
	}

	if (ToNode->Type == EBlueprintNodeType::Event)
	{
		if (OutReason) *OutReason = "Cannot link to event nodes";
		return false;
	}

	if (FromPin->Kind != ToPin->Kind)
	{
		if (OutReason) *OutReason = "Pin kinds do not match";
		return false;
	}

	if (FromPin->Kind == EBlueprintPinKind::Data &&
		FromPin->TypeName != ToPin->TypeName)
	{
		if (OutReason) *OutReason = "Data pin types do not match";
		return false;
	}

	return true;
}

bool AddEditorLink(FBlueprintGraph& Graph, int32 PinAId, int32 PinBId)
{
	FBlueprintNode* NodeA = Graph.FindNodeByPinId(PinAId);
	FBlueprintNode* NodeB = Graph.FindNodeByPinId(PinBId);

	if (!NodeA || !NodeB) return false;

	FBlueprintPin* PinA = Graph.FindPin(NodeA->Id, PinAId);
	FBlueprintPin* PinB = Graph.FindPin(NodeB->Id, PinBId);

	if (!PinA || !PinB) return false;

	FBlueprintPin* FromPin = PinA;
	FBlueprintPin* ToPin = PinB;

	int32 FromNodeId = NodeA->Id;
	int32 ToNodeId = NodeB->Id;

	if (FromPin->Direction == EBlueprintPinDirection::Input)
	{
		std::swap(FromPin, ToPin);
		std::swap(FromNodeId, ToNodeId);
	}

	if (FromPin->Direction != EBlueprintPinDirection::Output ||
		ToPin->Direction != EBlueprintPinDirection::Input)
	{
		return false;
	}

	const int32 FromPinId = FromPin->Id;
	const int32 ToPinId = ToPin->Id;

	for (auto It = Graph.Links.begin(); It != Graph.Links.end(); )
	{
		const bool bSameInput = It->ToPinId == ToPinId;

		const bool bSameExecOutput =
			FromPin->Kind == EBlueprintPinKind::Exec &&
			It->FromPinId == FromPinId;

		if (bSameInput || bSameExecOutput)
		{
			It = Graph.Links.erase(It);
		}
		else
		{
			++It;
		}
	}

	FBlueprintLink Link;
	Link.FromNodeId = FromNodeId;
	Link.FromPinId = FromPinId;
	Link.ToNodeId = ToNodeId;
	Link.ToPinId = ToPinId;

	return Graph.AddLink(Link) != nullptr;
}

#pragma endregion

FFloatProperty* GetFloatLiteralProperty()
{
	static FFloatProperty Property;
	static bool bInitialized = false;

	if (!bInitialized)
	{
		Property.Name = "Value";
		Property.DisplayName = "Value";
		Property.TypeName = "float";
		Property.Size = sizeof(float);
		bInitialized = true;
	}

	return &Property;
}

FBoolProperty* GetBoolLiteralProperty()
{
	static FBoolProperty Property;
	static bool bInitialized = false;

	if (!bInitialized)
	{
		Property.Name = "Value";
		Property.DisplayName = "Value";
		Property.TypeName = "bool";
		Property.Size = sizeof(bool);
		bInitialized = true;
	}

	return &Property;
}

bool IsBlueprintCallableFunction(const FFunction* Function)
{
	return Function && Function->HasAnyFunctionFlags(static_cast<uint32>(EFunctionFlags::BlueprintCallable));
}

bool IsBlueprintEventFunction(const FFunction* Function)
{
	return Function && Function->HasAnyFunctionFlags(static_cast<uint32>(EFunctionFlags::BlueprintEvent));
}

AActor* ResolveOwnerActorFromContext(UObject* ContextObject)
{
	if (!ContextObject) return nullptr;

	if (AActor* Actor = Cast<AActor>(ContextObject)) return Actor;
	if (UActorComponent* Component = Cast<UActorComponent>(ContextObject)) return Component->GetOwner();

	return nullptr;
}

bool HasIncomingLink(const FBlueprintGraph& Graph, int32 NodeId, int32 PinId)
{
	for (const FBlueprintLink& Link : Graph.Links)
	{
		if (Link.ToNodeId == NodeId && Link.ToPinId == PinId)
		{
			return true;
		}
	}

	return false;
}

UClass* ResolveValidationTargetClass(const FBlueprintNode& Node)
{
	switch (Node.TargetType)
	{
	case EBlueprintFunctionTargetType::Self:
		return UBlueprintComponent::StaticClass();
	case EBlueprintFunctionTargetType::Owner:
		return AActor::StaticClass();
	case EBlueprintFunctionTargetType::Component:
		if (Node.TargetClassName == UInterpToMovementComponent::StaticClass()->ClassName)
		{
			return UInterpToMovementComponent::StaticClass();
		}
		return nullptr;
	default:
		return nullptr;
	}
}

const char* GetNodeTypeText(EBlueprintNodeType Type)
{
	switch (Type)
	{
	case EBlueprintNodeType::Event:
		return "Event";
	case EBlueprintNodeType::FunctionCall:
		return "FunctionCall";
	case EBlueprintNodeType::Literal:
		return "Literal";
	case EBlueprintNodeType::Branch:
		return "Branch";
	case EBlueprintNodeType::Sequence:
		return "Sequence";
	default:
		return "Unknown";
	}
}

const char* GetTargetTypeText(EBlueprintFunctionTargetType Type)
{
	switch (Type)
	{
	case EBlueprintFunctionTargetType::Self:
		return "Self";
	case EBlueprintFunctionTargetType::Owner:
		return "Owner";
	case EBlueprintFunctionTargetType::Component:
		return "Component";
	default:
		return "Unknown";
	}
}

const char* GetPinKindText(EBlueprintPinKind Kind)
{
	switch (Kind)
	{
	case EBlueprintPinKind::Exec:
		return "Exec";
	case EBlueprintPinKind::Data:
		return "Data";
	default:
		return "Unknown";
	}
}

const char* GetPinDirectionText(EBlueprintPinDirection Direction)
{
	switch (Direction)
	{
	case EBlueprintPinDirection::Input:
		return "Input";
	case EBlueprintPinDirection::Output:
		return "Output";
	default:
		return "Unknown";
	}
}

bool DrawFStringInput(const char* Label, FString& Value)
{
	char Buffer[256] = {};
	std::strncpy(Buffer, Value.c_str(), sizeof(Buffer) - 1);

	if (ImGui::InputText(Label, Buffer, sizeof(Buffer)))
	{
		Value = Buffer;
		return true;
	}

	return false;
}

ImVec4 GetNodeAccentColor(EBlueprintNodeType Type)
{
	switch (Type)
	{
	case EBlueprintNodeType::Event:
		return ImVec4(0.86f, 0.22f, 0.20f, 1.0f);
	case EBlueprintNodeType::FunctionCall:
		return ImVec4(0.22f, 0.48f, 0.86f, 1.0f);
	case EBlueprintNodeType::Literal:
		return ImVec4(0.18f, 0.68f, 0.55f, 1.0f);
	case EBlueprintNodeType::Branch:
		return ImVec4(0.72f, 0.48f, 0.86f, 1.0f);
	case EBlueprintNodeType::Sequence:
		return ImVec4(0.64f, 0.68f, 0.76f, 1.0f);
	default:
		return ImVec4(0.46f, 0.50f, 0.58f, 1.0f);
	}
}

ImVec4 GetNodeBackgroundColor(EBlueprintNodeType Type)
{
	const ImVec4 Accent = GetNodeAccentColor(Type);
	return ImVec4(
		0.075f + Accent.x * 0.035f,
		0.080f + Accent.y * 0.035f,
		0.095f + Accent.z * 0.035f,
		0.96f);
}

ImVec4 GetPinColor(const FBlueprintPin& Pin)
{
	if (Pin.Kind == EBlueprintPinKind::Exec)
	{
		return ImVec4(0.92f, 0.94f, 0.98f, 1.0f);
	}

	if (Pin.TypeName == "float")
	{
		return ImVec4(0.46f, 0.84f, 0.36f, 1.0f);
	}

	if (Pin.TypeName == "bool")
	{
		return ImVec4(0.90f, 0.32f, 0.28f, 1.0f);
	}

	if (Pin.TypeName.find('*') != FString::npos ||
		Pin.TypeName.find("Component") != FString::npos ||
		Pin.TypeName.find("Actor") != FString::npos ||
		Pin.TypeName.find("Object") != FString::npos)
	{
		return ImVec4(0.35f, 0.64f, 0.95f, 1.0f);
	}

	return ImVec4(0.70f, 0.74f, 0.82f, 1.0f);
}

void DrawSectionLabel(const char* Label)
{
	ImGui::Spacing();
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.76f, 0.92f, 1.0f));
	ImGui::TextUnformatted(Label);
	ImGui::PopStyleColor();
	ImGui::Separator();
}

void DrawPinMarker(const FBlueprintPin& Pin, const ImVec4& PinColor)
{
	constexpr float MarkerSize = 12.0f;
	constexpr float MarkerRadius = MarkerSize * 0.5f;

	const ImVec2 MarkerMin = ImGui::GetCursorScreenPos();
	const ImVec2 MarkerMax = ImVec2(MarkerMin.x + MarkerSize, MarkerMin.y + MarkerSize);
	const ImVec2 MarkerCenter = ImVec2(MarkerMin.x + MarkerRadius, MarkerMin.y + MarkerRadius);
	GBlueprintPinScreenCenters[Pin.Id] = MarkerCenter;

	ImGui::Dummy(ImVec2(MarkerSize, MarkerSize));

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImU32 FillColor = ImGui::ColorConvertFloat4ToU32(PinColor);
	const ImU32 BorderColor = IM_COL32(24, 28, 36, 255);

	if (Pin.Kind == EBlueprintPinKind::Exec)
	{
		const ImVec2 Points[4] = {
			ImVec2(MarkerCenter.x, MarkerMin.y),
			ImVec2(MarkerMax.x, MarkerCenter.y),
			ImVec2(MarkerCenter.x, MarkerMax.y),
			ImVec2(MarkerMin.x, MarkerCenter.y)
		};

		DrawList->AddConvexPolyFilled(Points, 4, FillColor);
		DrawList->AddPolyline(Points, 4, BorderColor, ImDrawFlags_Closed, 1.5f);
	}
	else
	{
		DrawList->AddCircleFilled(MarkerCenter, MarkerRadius - 1.0f, FillColor, 16);
		DrawList->AddCircle(MarkerCenter, MarkerRadius - 1.0f, BorderColor, 16, 1.5f);
	}

	ed::PinRect(MarkerMin, MarkerMax);
	ed::PinPivotRect(MarkerMin, MarkerMax);
	ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
}

const FBlueprintPin* FindLinkSourcePin(const FBlueprintGraph& Graph, const FBlueprintLink& Link)
{
	return Graph.FindPin(Link.FromNodeId, Link.FromPinId);
}

void DrawOverlayLink(const FBlueprintLink& Link, const ImVec4& Color, float Thickness)
{
	const auto FromIt = GBlueprintPinScreenCenters.find(Link.FromPinId);
	const auto ToIt = GBlueprintPinScreenCenters.find(Link.ToPinId);
	if (FromIt == GBlueprintPinScreenCenters.end() || ToIt == GBlueprintPinScreenCenters.end())
	{
		return;
	}

	const ImVec2 From = FromIt->second;
	const ImVec2 To = ToIt->second;
	const float DeltaX = std::abs(To.x - From.x);
	const float ControlDistance = std::max(80.0f, DeltaX * 0.5f);
	const ImVec2 ControlA = ImVec2(From.x + ControlDistance, From.y);
	const ImVec2 ControlB = ImVec2(To.x - ControlDistance, To.y);

	ImGui::GetWindowDrawList()->AddBezierCubic(
		From,
		ControlA,
		ControlB,
		To,
		ImGui::ColorConvertFloat4ToU32(Color),
		Thickness,
		32);
}

}

void FEditorBlueprintWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);

	if (!NodeEditorContext)
	{
		ed::Config Config;
		Config.SettingsFile = "BlueprintNodeEditor.json";
		NodeEditorContext = ed::CreateEditor(&Config);
	}
}

void FEditorBlueprintWidget::Shutdown()
{
	if (NodeEditorContext)
	{
		ed::DestroyEditor(NodeEditorContext);
		NodeEditorContext = nullptr;
	}

	delete Asset;
	Asset = nullptr;
}

void FEditorBlueprintWidget::Render(float DeltaTime)
{
	ImGui::SetNextWindowSize(ImVec2(1280.0f, 780.0f), ImGuiCond_Once);

	FString WindowName = "Blueprint Editor";
	if (!AssetPath.empty())
	{
		WindowName += " - ";
		WindowName += GetFileNameFromPath(AssetPath);
	}

	if (!ImGui::Begin(WindowName.c_str()))
	{
		ImGui::End();
		return;
	}

	DrawContent(DeltaTime);
	ImGui::End();
}

void FEditorBlueprintWidget::RenderEmbedded(float DeltaTime)
{
	DrawContent(DeltaTime);
}

bool FEditorBlueprintWidget::OpenAsset(const FString& InAssetPath)
{
	AssetPath = FPaths::Normalize(InAssetPath);

	delete Asset;
	Asset = new UBlueprintAsset();

	bLoaded = Asset->LoadFromFile(AssetPath);
	bDirty = false;
	bInitializedNodePositions = false;
	SelectedNodeId = 0;
	ValidationMessages.clear();

	if (!bLoaded)
	{
		delete Asset;
		Asset = nullptr;
		return false;
	}

	return true;
}

bool FEditorBlueprintWidget::SaveAsset()
{
	if (!Asset || AssetPath.empty())
	{
		return false;
	}

	const bool bSaved = Asset->SaveToFile(AssetPath);
	if (bSaved)
	{
		bDirty = false;
	}

	return bSaved;
}

void FEditorBlueprintWidget::DrawContent(float DeltaTime)
{
	RenderToolbar();

	if (ImGui::BeginTable("##BlueprintEditorLayout", 2,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 320.0f);

		ImGui::TableNextColumn();
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.060f, 0.074f, 1.0f));
		ImGui::BeginChild("##BlueprintGraphPanel", ImVec2(0.0f, 0.0f), false);
		RenderGraph();
		ImGui::EndChild();
		ImGui::PopStyleColor();

		ImGui::TableNextColumn();
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.082f, 0.100f, 1.0f));
		ImGui::BeginChild("##BlueprintDetailsPanel", ImVec2(0.0f, 0.0f), false);
		RenderDetailsPanel();
		ImGui::EndChild();
		ImGui::PopStyleColor();

		ImGui::EndTable();
	}
}

void FEditorBlueprintWidget::RenderToolbar()
{
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 5.0f));

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.84f, 0.94f, 1.0f));
	ImGui::TextUnformatted(AssetPath.empty() ? "No Blueprint" : AssetPath.c_str());
	ImGui::PopStyleColor();

	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.25f, 0.31f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.13f, 0.28f, 0.50f, 1.0f));
	if (ImGui::Button("Reload"))
	{
		if (!OpenAsset(AssetPath) && EditorEngine)
		{
			EditorEngine->GetNotificationService().Error("Failed to reload Blueprint asset.");
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Save"))
	{
		if (!SaveAsset() && EditorEngine)
		{
			EditorEngine->GetNotificationService().Error("Failed to save Blueprint asset.");
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Validate"))
	{
		ValidateGraph();
	}
	ImGui::PopStyleColor(3);

	if (bDirty)
	{
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.74f, 0.32f, 1.0f));
		ImGui::TextUnformatted("Unsaved");
		ImGui::PopStyleColor();
	}

	ImGui::PopStyleVar(2);
	ImGui::Separator();

	RenderValidationMessages();
}

void FEditorBlueprintWidget::RenderValidationMessages()
{
	if (ValidationMessages.empty())
	{
		return;
	}

	ImGui::Separator();
	DrawSectionLabel("Validation");

	for (const FBlueprintValidationMessage& Message : ValidationMessages)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.66f, 0.45f, 1.0f));
		ImGui::TextWrapped(
			"Node %d Pin %d: %s",
			Message.NodeId,
			Message.PinId,
			Message.Message.c_str());
		ImGui::PopStyleColor();
	}
}

void FEditorBlueprintWidget::ValidateGraph()
{
	ValidationMessages.clear();

	if (!Asset)
	{
		ValidationMessages.push_back({ 0, 0, "No Blueprint asset loaded." });
		return;
	}

	const FBlueprintGraph& Graph = Asset->GetGraph();
	bool bHasBeginPlay = false;

	for (const FBlueprintNode& Node : Graph.Nodes)
	{
		if (Node.Type == EBlueprintNodeType::Event && Node.EventName == "BeginPlay")
		{
			bHasBeginPlay = true;
		}

		if (Node.Type == EBlueprintNodeType::FunctionCall)
		{
			ValidateFunctionNode(Node);
		}

		ValidateNodePins(Graph, Node);
	}

	if (!bHasBeginPlay)
	{
		ValidationMessages.push_back({ 0, 0, "Missing BeginPlay event." });
	}

	if (ValidationMessages.empty() && EditorEngine)
	{
		EditorEngine->GetNotificationService().Info("Blueprint graph is valid.");
	}
}

void FEditorBlueprintWidget::ValidateNodePins(const FBlueprintGraph& Graph, const FBlueprintNode& Node)
{
	for (const FBlueprintPin& Pin : Node.Pins)
	{
		if (Pin.Direction != EBlueprintPinDirection::Input)
		{
			continue;
		}

		if (Node.Type == EBlueprintNodeType::Event)
		{
			continue;
		}

		if (!HasIncomingLink(Graph, Node.Id, Pin.Id))
		{
			const char* KindText = Pin.Kind == EBlueprintPinKind::Exec ? "Exec" : "Data";
			ValidationMessages.push_back({
				Node.Id,
				Pin.Id,
				FString(KindText) + " input is not connected: " + Pin.Name
			});
		}
	}
}

void FEditorBlueprintWidget::ValidateFunctionNode(const FBlueprintNode& Node)
{
	UClass* TargetClass = ResolveValidationTargetClass(Node);
	if (!TargetClass)
	{
		ValidationMessages.push_back({
			Node.Id,
			0,
			"Cannot resolve function target class: " + Node.TargetClassName
		});
		return;
	}

	FFunction* Function = TargetClass->FindFunction(Node.FunctionName);
	if (!Function)
	{
		ValidationMessages.push_back({
			Node.Id,
			0,
			"Function not found: " + Node.FunctionName
		});
	}
}

void FEditorBlueprintWidget::RenderGraph()
{
	if (!Asset)
	{
		ImGui::TextDisabled("No Blueprint asset loaded.");
		return;
	}

	if (!NodeEditorContext)
	{
		return;
	}

	FBlueprintGraph& Graph = Asset->GetGraph();

	ed::SetCurrentEditor(NodeEditorContext);

	const ImVec2 CanvasSize = ImGui::GetContentRegionAvail();
	ed::PushStyleColor(ed::StyleColor_Bg, ImVec4(0.050f, 0.055f, 0.068f, 1.0f));
	ed::PushStyleColor(ed::StyleColor_Grid, ImVec4(0.19f, 0.21f, 0.26f, 0.35f));
	ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.31f, 0.36f, 0.45f, 0.90f));
	ed::PushStyleColor(ed::StyleColor_HovNodeBorder, ImVec4(0.58f, 0.72f, 0.96f, 1.0f));
	ed::PushStyleColor(ed::StyleColor_SelNodeBorder, ImVec4(1.0f, 0.76f, 0.34f, 1.0f));
	ed::PushStyleColor(ed::StyleColor_HovLinkBorder, ImVec4(0.72f, 0.82f, 1.0f, 1.0f));
	ed::PushStyleColor(ed::StyleColor_SelLinkBorder, ImVec4(1.0f, 0.76f, 0.34f, 1.0f));
	ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(10.0f, 8.0f, 10.0f, 8.0f));
	ed::PushStyleVar(ed::StyleVar_NodeRounding, 7.0f);
	ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.2f);
	ed::PushStyleVar(ed::StyleVar_SelectedNodeBorderWidth, 2.4f);
	ed::PushStyleVar(ed::StyleVar_PinRounding, 4.0f);
	ed::PushStyleVar(ed::StyleVar_PinBorderWidth, 1.0f);

	ed::Begin("BlueprintGraph", CanvasSize);

	if (!bInitializedNodePositions)
	{
		for (const FBlueprintNode& Node : Graph.Nodes)
		{
			ed::SetNodePosition(MakeEditorNodeId(Node.Id), ImVec2(Node.EditorX, Node.EditorY));
		}

		bInitializedNodePositions = true;
	}

	for (FBlueprintNode& Node : Graph.Nodes)
	{
		ed::PushStyleColor(ed::StyleColor_NodeBg, GetNodeBackgroundColor(Node.Type));
		ed::BeginNode(MakeEditorNodeId(Node.Id));

		RenderNodeHeader(Node);

		if (Node.Type == EBlueprintNodeType::Literal)
		{
			ImGui::PushID(Node.Id);
			RenderLiteralNodeBody(Node);
			ImGui::PopID();
		}

		for (FBlueprintPin& Pin : Node.Pins)
		{
			if (Pin.Direction != EBlueprintPinDirection::Input)
			{
				continue;
			}

			RenderPin(Pin);
		}

		for (FBlueprintPin& Pin : Node.Pins)
		{
			if (Pin.Direction != EBlueprintPinDirection::Output)
			{
				continue;
			}

			RenderPin(Pin);
		}

		ed::EndNode();
		ed::PopStyleColor();
	}

	for (FBlueprintNode& Node : Graph.Nodes)
	{
		const ImVec2 Pos = ed::GetNodePosition(MakeEditorNodeId(Node.Id));
		if (Node.EditorX != Pos.x || Node.EditorY != Pos.y)
		{
			Node.EditorX = Pos.x;
			Node.EditorY = Pos.y;
			bDirty = true;
		}
	}

	for (const FBlueprintLink& Link : Graph.Links)
	{
		const FBlueprintPin* SourcePin = FindLinkSourcePin(Graph, Link);
		const ImVec4 LinkColor = SourcePin
			? GetPinColor(*SourcePin)
			: ImVec4(0.78f, 0.82f, 0.90f, 1.0f);
		const float LinkThickness = SourcePin && SourcePin->Kind == EBlueprintPinKind::Exec ? 2.5f : 1.8f;
		const ImVec4 HiddenLinkColor = ImVec4(LinkColor.x, LinkColor.y, LinkColor.z, 0.0f);

		ed::Link(MakeEditorLinkId(Link.Id), MakeEditorPinId(Link.FromPinId), MakeEditorPinId(Link.ToPinId),
			HiddenLinkColor, 1.0f);
		DrawOverlayLink(Link, LinkColor, LinkThickness);
	}

	const bool bCanCreate = ed::BeginCreate();

	if (bCanCreate)
	{
		ed::PinId StartPinId;
		ed::PinId EndPinId;

		if (ed::QueryNewLink(&StartPinId, &EndPinId))
		{
			if (StartPinId && EndPinId)
			{
				const int32 PinAId = FromEditorPinId(StartPinId);
				const int32 PinBId = FromEditorPinId(EndPinId);
				const FBlueprintNode* StartNode = Graph.FindNodeByPinId(PinAId);
				const FBlueprintPin* StartPin = StartNode ? Graph.FindPin(StartNode->Id, PinAId) : nullptr;
				const ImVec4 PreviewColor = StartPin ? GetPinColor(*StartPin) : ImVec4(0.92f, 0.94f, 0.98f, 1.0f);

				FString Reason;
				if (CanCreateLink(Graph, PinAId, PinBId, &Reason))
				{
					if (ed::AcceptNewItem(PreviewColor, 2.2f))
					{
						if (AddEditorLink(Graph, PinAId, PinBId))
						{
							bDirty = true;
						}
					}
				}
				else
				{
					ed::RejectNewItem(ImVec4(0.95f, 0.28f, 0.24f, 1.0f), 2.0f);
					ImGui::SetTooltip("%s", Reason.c_str());
				}
			}
		}
	}

	ed::EndCreate();

	const bool bCanDelete = ed::BeginDelete();

	if (bCanDelete)
	{
		ed::NodeId DeletedNodeId;

		while (ed::QueryDeletedNode(&DeletedNodeId))
		{
			if (ed::AcceptDeletedItem())
			{
				const int32 NodeId = FromEditorNodeId(DeletedNodeId);

				if (Graph.RemoveNode(NodeId))
				{
					bDirty = true;
				}
			}
		}

		ed::LinkId DeletedLinkId;

		while (ed::QueryDeletedLink(&DeletedLinkId))
		{
			if (ed::AcceptDeletedItem())
			{
				const int32 LinkId = FromEditorLinkId(DeletedLinkId);

				if (Graph.RemoveLink(LinkId))
				{
					bDirty = true;
				}
			}
		}
	}

	ed::EndDelete();

	UpdateSelectionFromEditor(Graph);

	ed::Suspend();

	if (ed::ShowBackgroundContextMenu())
	{
		ContextMenuCanvasPosition = ed::ScreenToCanvas(ImGui::GetMousePos());
		ImGui::OpenPopup("BlueprintNodeContextMenu");
	}

	RenderNodeContextMenu();

	ed::Resume();

	ed::End();
	ed::PopStyleVar(6);
	ed::PopStyleColor(7);
	ed::SetCurrentEditor(nullptr);
}

void FEditorBlueprintWidget::RenderNodeHeader(const FBlueprintNode& Node)
{
	const FString& Title = Node.DisplayName.empty() ? Node.Name : Node.DisplayName;
	const ImVec4 Accent = GetNodeAccentColor(Node.Type);
	const ImVec2 HeaderMin = ImGui::GetCursorScreenPos();
	const float HeaderHeight = ImGui::GetTextLineHeightWithSpacing();

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(
		HeaderMin,
		ImVec2(HeaderMin.x + 4.0f, HeaderMin.y + HeaderHeight),
		ImGui::ColorConvertFloat4ToU32(Accent),
		2.0f);

	ImGui::Indent(10.0f);

	ImGui::PushStyleColor(ImGuiCol_Text, Accent);
	ImGui::TextUnformatted(GetNodeTypeText(Node.Type));
	ImGui::PopStyleColor();

	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.95f, 1.0f, 1.0f));
	ImGui::TextUnformatted(Title.c_str());
	ImGui::PopStyleColor();

	ImGui::Unindent(10.0f);
	ImGui::Spacing();
}

void FEditorBlueprintWidget::RenderPin(const FBlueprintPin& Pin)
{
	const ImVec4 PinColor = GetPinColor(Pin);
	const ed::PinKind EditorPinKind =
		Pin.Direction == EBlueprintPinDirection::Output ? ed::PinKind::Output : ed::PinKind::Input;

	ed::PushStyleColor(ed::StyleColor_PinRect, ImVec4(PinColor.x, PinColor.y, PinColor.z, 0.92f));
	ed::PushStyleColor(ed::StyleColor_PinRectBorder, PinColor);
	ed::BeginPin(MakeEditorPinId(Pin.Id), EditorPinKind);
	ImGui::PushID(Pin.Id);

	const FString& Label = Pin.DisplayName.empty() ? Pin.Name : Pin.DisplayName;

	if (Pin.Direction == EBlueprintPinDirection::Input)
	{
		DrawPinMarker(Pin, PinColor);
		ImGui::SameLine();
	}

	ImGui::TextUnformatted(Label.c_str());

	if (Pin.Kind == EBlueprintPinKind::Data && !Pin.TypeName.empty())
	{
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.54f, 0.59f, 0.68f, 1.0f));
		ImGui::Text("(%s)", Pin.TypeName.c_str());
		ImGui::PopStyleColor();
	}

	if (Pin.Direction == EBlueprintPinDirection::Output)
	{
		ImGui::SameLine();
		DrawPinMarker(Pin, PinColor);
	}

	ImGui::PopID();
	ed::EndPin();
	ed::PopStyleColor(2);
}

void FEditorBlueprintWidget::UpdateSelectionFromEditor(const FBlueprintGraph& Graph)
{
	int32 NewSelectedNodeId = 0;

	if (ed::GetSelectedObjectCount() > 0)
	{
		ed::NodeId SelectedNodes[1];
		if (ed::GetSelectedNodes(SelectedNodes, 1) > 0)
		{
			const int32 NodeId = FromEditorNodeId(SelectedNodes[0]);
			if (Graph.FindNode(NodeId))
			{
				NewSelectedNodeId = NodeId;
			}
		}
	}

	SelectedNodeId = NewSelectedNodeId;
}

void FEditorBlueprintWidget::RenderDetailsPanel()
{
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.94f, 1.0f, 1.0f));
	ImGui::TextUnformatted("Details");
	ImGui::PopStyleColor();
	ImGui::Separator();

	if (!Asset)
	{
		ImGui::TextDisabled("No Blueprint asset loaded.");
		ImGui::PopStyleVar(2);
		return;
	}

	FBlueprintGraph& Graph = Asset->GetGraph();
	FBlueprintNode* SelectedNode = Graph.FindNode(SelectedNodeId);
	if (!SelectedNode)
	{
		ImGui::TextDisabled("No node selected.");
		ImGui::PopStyleVar(2);
		return;
	}

	RenderSelectedNodeDetails(*SelectedNode);
	ImGui::PopStyleVar(2);
}

void FEditorBlueprintWidget::RenderSelectedNodeDetails(FBlueprintNode& Node)
{
	ImGui::PushID(Node.Id);

	const ImVec4 Accent = GetNodeAccentColor(Node.Type);
	ImGui::PushStyleColor(ImGuiCol_Text, Accent);
	ImGui::TextUnformatted(GetNodeTypeText(Node.Type));
	ImGui::PopStyleColor();

	ImGui::SameLine();
	ImGui::Text("Node Id: %d", Node.Id);

	DrawSectionLabel("Identity");

	if (DrawFStringInput("Name", Node.Name))
	{
		bDirty = true;
	}

	if (DrawFStringInput("Display Name", Node.DisplayName))
	{
		bDirty = true;
	}

	if (!Node.Category.empty())
	{
		ImGui::TextDisabled("Category: %s", Node.Category.c_str());
	}

	if (Node.Type == EBlueprintNodeType::Event)
	{
		DrawSectionLabel("Event");

		if (DrawFStringInput("Event Name", Node.EventName))
		{
			bDirty = true;
		}
	}
	else if (Node.Type == EBlueprintNodeType::FunctionCall)
	{
		DrawSectionLabel("Function");
		ImGui::TextDisabled("Function Name");
		ImGui::TextUnformatted(Node.FunctionName.c_str());
		ImGui::TextDisabled("Target Type");
		ImGui::TextUnformatted(GetTargetTypeText(Node.TargetType));
		ImGui::TextDisabled("Target Class");
		ImGui::TextUnformatted(Node.TargetClassName.c_str());

		if (!Node.TargetComponentName.empty())
		{
			ImGui::TextDisabled("Target Component");
			ImGui::TextUnformatted(Node.TargetComponentName.c_str());
		}
	}
	else if (Node.Type == EBlueprintNodeType::Literal)
	{
		DrawSectionLabel("Literal");
		ImGui::TextDisabled("Literal Type");
		ImGui::TextUnformatted(Node.LiteralTypeName.c_str());
		RenderLiteralNodeBody(Node);
	}

	DrawSectionLabel("Pins");

	if (Node.Pins.empty())
	{
		ImGui::TextDisabled("No pins.");
	}
	else if (ImGui::BeginTable("##NodePins", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
	{
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Dir");
		ImGui::TableSetupColumn("Kind");
		ImGui::TableSetupColumn("Type");
		ImGui::TableHeadersRow();

		for (const FBlueprintPin& Pin : Node.Pins)
		{
			const FString& PinName = Pin.DisplayName.empty() ? Pin.Name : Pin.DisplayName;
			const ImVec4 PinColor = GetPinColor(Pin);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushStyleColor(ImGuiCol_Text, PinColor);
			ImGui::TextUnformatted(PinName.c_str());
			ImGui::PopStyleColor();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(GetPinDirectionText(Pin.Direction));
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(GetPinKindText(Pin.Kind));
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(Pin.TypeName.c_str());
		}

		ImGui::EndTable();
	}

	ImGui::PopID();
}

void FEditorBlueprintWidget::RenderNodeContextMenu()
{
	if (!Asset) return;

	if (ImGui::BeginPopup("BlueprintNodeContextMenu"))
	{
		FBlueprintGraph& Graph = Asset->GetGraph();

		RenderEventMenu(UBlueprintComponent::StaticClass());

		if (ImGui::MenuItem("Flow / Sequence"))
		{
			FBlueprintNode* Node = AddSequenceNode(Graph, 2);
			if (Node)
			{
				Node->EditorX = ContextMenuCanvasPosition.x;
				Node->EditorY = ContextMenuCanvasPosition.y;
				bDirty = true;
				bInitializedNodePositions = false;
			}
		}

		if (ImGui::BeginMenu("Literal"))
		{
			if (ImGui::MenuItem("Float"))
			{
				AddFloatLiteralAtContextPosition(0.0f);
			}

			if (ImGui::MenuItem("Bool"))
			{
				AddBoolLiteralAtContextPosition(false);
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::BeginMenu("Function"))
		{
			RenderFunctionMenu("Self", UBlueprintComponent::StaticClass(), EBlueprintFunctionTargetType::Self);
			RenderFunctionMenu("Owner", AActor::StaticClass(), EBlueprintFunctionTargetType::Owner);

			if (ImGui::BeginMenu("Components"))
			{
				AActor* OwnerActor = ResolveOwnerActorFromContext(ContextObject);

				if (!OwnerActor)
				{
					ImGui::TextDisabled("No context actor");
				}
				else
				{
					for (UActorComponent* Component : OwnerActor->GetComponents())
					{
						if (!Component || !Component->GetClass()) continue;

						const FString ComponentLabel = Component->GetName().empty()
							? Component->GetClass()->ClassName
							: Component->GetName();

						RenderFunctionMenu(ComponentLabel.c_str(), Component->GetClass(),
							EBlueprintFunctionTargetType::Component, Component->GetName());
					}
				}

				ImGui::EndMenu();
			}

			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}
}

void FEditorBlueprintWidget::AddEventNodeAtContextPosition(const FFunction* Function)
{
	if (!Asset || !Function) return;

	FBlueprintNode* Node = AddEventNode(Asset->GetGraph(), Function);
	if (!Node) return;

	Node->EditorX = ContextMenuCanvasPosition.x;
	Node->EditorY = ContextMenuCanvasPosition.y;

	bDirty = true;
	bInitializedNodePositions = false;
}

void FEditorBlueprintWidget::AddFunctionNodeAtContextPosition(UClass* TargetClass, const FString& FunctionName,
	EBlueprintFunctionTargetType TargetType, const FString& TargetComponentName)
{
	if (!Asset || !TargetClass) return;

	FFunction* Function = TargetClass->FindFunction(FunctionName);
	if (!Function)
	{
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Warning("Function not found.");
		}
		return;
	}

	FBlueprintNode* Node = AddFunctionCallNode(Asset->GetGraph(), TargetClass, Function);
	if (!Node) return;

	Node->TargetType = TargetType;
	Node->TargetComponentName = TargetComponentName;
	Node->EditorX = ContextMenuCanvasPosition.x;
	Node->EditorY = ContextMenuCanvasPosition.y;

	bDirty = true;
	bInitializedNodePositions = false;
}

void FEditorBlueprintWidget::RenderEventMenu(UClass* TargetClass)
{
	if (!TargetClass) return;

	TArray<FFunction*> Functions;
	TargetClass->GetAllFunctions(Functions);

	bool bHasAnyEvent = false;
	for (FFunction* Function : Functions)
	{
		if (IsBlueprintEventFunction(Function))
		{
			bHasAnyEvent = true;
			break;
		}
	}

	if (!bHasAnyEvent) return;

	if (ImGui::BeginMenu("Event"))
	{
		for (FFunction* Function : Functions)
		{
			if (!IsBlueprintEventFunction(Function)) continue;

			const char* FunctionLabel = Function->DisplayName.empty()
				? Function->Name.c_str()
				: Function->DisplayName.c_str();

			if (ImGui::MenuItem(FunctionLabel))
			{
				AddEventNodeAtContextPosition(Function);
			}
		}

		ImGui::EndMenu();
	}
}

void FEditorBlueprintWidget::RenderFunctionMenu(const char* Label, UClass* TargetClass, EBlueprintFunctionTargetType TargetType, const FString& TargetComponentName)
{
	if (!TargetClass) return;

	TArray<FFunction*> Functions;
	TargetClass->GetAllFunctions(Functions);

	bool bHasAnyCallable = false;
	for (FFunction* Function : Functions)
	{
		if (IsBlueprintCallableFunction(Function))
		{
			bHasAnyCallable = true;
			break;
		}
	}

	if (!bHasAnyCallable) return;

	if (ImGui::BeginMenu(Label))
	{
		for (FFunction* Function : Functions)
		{
			if (!IsBlueprintCallableFunction(Function)) continue;

			const char* FunctionLabel = Function->DisplayName.empty()
				? Function->Name.c_str()
				: Function->DisplayName.c_str();

			if (ImGui::MenuItem(FunctionLabel))
			{
				AddFunctionNodeAtContextPosition(TargetClass, Function->Name, TargetType, TargetComponentName);
			}
		}

		ImGui::EndMenu();
	}
}

void FEditorBlueprintWidget::RenderLiteralNodeBody(FBlueprintNode& Node)
{
	if (Node.LiteralTypeName == "float")
	{
		if (Node.LiteralData.size() < sizeof(float))
		{
			Node.LiteralData.resize(sizeof(float));
		}

		float Value = 0.0f;
		std::memcpy(&Value, Node.LiteralData.data(), sizeof(float));

		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::DragFloat("##Value", &Value, 0.1f))
		{
			std::memcpy(Node.LiteralData.data(), &Value, sizeof(float));
			bDirty = true;
		}
	}
	else if (Node.LiteralTypeName == "bool")
	{
		if (Node.LiteralData.size() < sizeof(bool))
		{
			Node.LiteralData.resize(sizeof(bool));
		}

		bool bValue = false;
		std::memcpy(&bValue, Node.LiteralData.data(), sizeof(bool));

		if (ImGui::Checkbox("Value", &bValue))
		{
			std::memcpy(Node.LiteralData.data(), &bValue, sizeof(bool));
			bDirty = true;
		}
	}
}

void FEditorBlueprintWidget::AddFloatLiteralAtContextPosition(float Value)
{
	if (!Asset) return;

	FBlueprintNode* Node = AddLiteralNode(Asset->GetGraph(), GetFloatLiteralProperty(), &Value);
	if (!Node) return;

	Node->EditorX = ContextMenuCanvasPosition.x;
	Node->EditorY = ContextMenuCanvasPosition.y;

	bDirty = true;
	bInitializedNodePositions = false;
}

void FEditorBlueprintWidget::AddBoolLiteralAtContextPosition(bool Value)
{
	if (!Asset) return;

	FBlueprintNode* Node = AddLiteralNode(Asset->GetGraph(), GetBoolLiteralProperty(), &Value);
	if (!Node) return;

	Node->EditorX = ContextMenuCanvasPosition.x;
	Node->EditorY = ContextMenuCanvasPosition.y;

	bDirty = true;
	bInitializedNodePositions = false;
}
