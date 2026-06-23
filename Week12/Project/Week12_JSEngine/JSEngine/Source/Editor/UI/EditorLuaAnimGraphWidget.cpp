#include "Editor/UI/EditorLuaAnimGraphWidget.h"

#include "Animation/AnimLuaProgramAsset.h"
#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Core/Paths.h"
#include "Editor/EditorEngine.h"
#include "Editor/Notification/EditorNotificationService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

namespace
{
	constexpr float NodeWidth = 230.0f;
	constexpr float NodeHeight = 82.0f;
	constexpr float PinRadius = 5.0f;
	constexpr float LinkHitRadiusSq = 10.0f * 10.0f;

	ImVec2 Add(const ImVec2& A, const ImVec2& B)
	{
		return ImVec2(A.x + B.x, A.y + B.y);
	}

	ImVec2 Sub(const ImVec2& A, const ImVec2& B)
	{
		return ImVec2(A.x - B.x, A.y - B.y);
	}

	ImVec2 Mul(const ImVec2& V, float S)
	{
		return ImVec2(V.x * S, V.y * S);
	}

	ImVec2 GraphToScreen(const ImVec2& GraphOrigin, float Zoom, float X, float Y)
	{
		return Add(GraphOrigin, Mul(ImVec2(X, Y), Zoom));
	}

	ImVec2 MinVec(const ImVec2& A, const ImVec2& B)
	{
		return ImVec2(std::min(A.x, B.x), std::min(A.y, B.y));
	}

	ImVec2 MaxVec(const ImVec2& A, const ImVec2& B)
	{
		return ImVec2(std::max(A.x, B.x), std::max(A.y, B.y));
	}

	bool RectsOverlap(const ImVec2& AMin, const ImVec2& AMax, const ImVec2& BMin, const ImVec2& BMax)
	{
		return AMin.x <= BMax.x && AMax.x >= BMin.x && AMin.y <= BMax.y && AMax.y >= BMin.y;
	}

	ImVec2 ScreenToGraph(const ImVec2& GraphOrigin, float Zoom, const ImVec2& Screen)
	{
		return Zoom > 0.0001f ? Mul(Sub(Screen, GraphOrigin), 1.0f / Zoom) : ImVec2(0.0f, 0.0f);
	}

	float DistanceSquaredToSegment(const ImVec2& Point, const ImVec2& A, const ImVec2& B)
	{
		const float ABx = B.x - A.x;
		const float ABy = B.y - A.y;
		const float APx = Point.x - A.x;
		const float APy = Point.y - A.y;
		const float LenSq = ABx * ABx + ABy * ABy;
		float T = LenSq > 0.0001f ? (APx * ABx + APy * ABy) / LenSq : 0.0f;
		T = std::clamp(T, 0.0f, 1.0f);
		const float ClosestX = A.x + ABx * T;
		const float ClosestY = A.y + ABy * T;
		const float Dx = Point.x - ClosestX;
		const float Dy = Point.y - ClosestY;
		return Dx * Dx + Dy * Dy;
	}

	float DistanceSquared(const ImVec2& A, const ImVec2& B)
	{
		const float Dx = A.x - B.x;
		const float Dy = A.y - B.y;
		return Dx * Dx + Dy * Dy;
	}

	bool TryNormalizeDroppedPath(const ImGuiPayload* Payload, FString& OutPath)
	{
		if (!Payload || !Payload->Data || Payload->DataSize <= 0)
		{
			return false;
		}

		const FString PayloadPath = static_cast<const char*>(Payload->Data);
		const std::filesystem::path DroppedPath = FPaths::ToWide(PayloadPath);
		OutPath = DroppedPath.is_absolute()
			? FPaths::Normalize(FPaths::ToRelativeString(DroppedPath.wstring()))
			: FPaths::Normalize(PayloadPath);
		return !OutPath.empty();
	}

	bool InputFStringFixed(const char* Label, FString& Value, size_t Capacity = 512)
	{
		std::vector<char> Buffer(Capacity, '\0');
		std::strncpy(Buffer.data(), Value.c_str(), Buffer.size() - 1);
		if (ImGui::InputText(Label, Buffer.data(), Buffer.size()))
		{
			Value = Buffer.data();
			return true;
		}
		return false;
	}

	void DrawClippedText(ImDrawList* DrawList, const ImVec2& Pos, const ImVec2& ClipMin, const ImVec2& ClipMax, ImU32 Color, const FString& Text)
	{
		DrawList->PushClipRect(ClipMin, ClipMax, true);
		DrawList->AddText(Pos, Color, Text.c_str());
		DrawList->PopClipRect();
	}

	bool IsStateNameDuplicated(const FLuaAnimGraph& Graph, const FLuaAnimStateNode& State)
	{
		for (const auto& Pair : Graph.States)
		{
			const FLuaAnimStateNode& Other = Pair.second;
			if (Other.StateId != State.StateId && Other.Name == State.Name)
			{
				return true;
			}
		}
		return false;
	}

	const char* ToJoinLabel(EAnimConditionJoin Join)
	{
		return Join == EAnimConditionJoin::Or ? "Or" : "And";
	}

	const char* ToBlendModeLabel(EAnimBlendMode Mode)
	{
		switch (Mode)
		{
		case EAnimBlendMode::EaseIn:
			return "EaseIn";
		case EAnimBlendMode::EaseOut:
			return "EaseOut";
		case EAnimBlendMode::EaseInOut:
			return "EaseInOut";
		case EAnimBlendMode::Linear:
		default:
			return "Linear";
		}
	}

	const char* ToCompareLabel(EAnimCompareOp Op)
	{
		switch (Op)
		{
		case EAnimCompareOp::NotEqual:
			return "!=";
		case EAnimCompareOp::Less:
			return "<";
		case EAnimCompareOp::LessEqual:
			return "<=";
		case EAnimCompareOp::Greater:
			return ">";
		case EAnimCompareOp::GreaterEqual:
			return ">=";
		case EAnimCompareOp::Equal:
		default:
			return "==";
		}
	}
}

void FEditorLuaAnimGraphWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
}

void FEditorLuaAnimGraphWidget::Render(float DeltaTime)
{
	RenderEmbedded(DeltaTime);
}

void FEditorLuaAnimGraphWidget::RenderEmbedded(float DeltaTime)
{
	(void)DeltaTime;
	BeginEditFrame();
	HandleShortcuts();
	DrawContent(DeltaTime);
}

bool FEditorLuaAnimGraphWidget::OpenAsset(const FString& InAssetPath)
{
	AssetPath = FPaths::Normalize(InAssetPath);
	bLoaded = LoadAssetPayload();
	bOpen = bLoaded;
	SelectedStateId = 0;
	SelectedStateIds.clear();
	SelectedTransitionId = 0;
	PendingTransitionFromStateId = 0;
	UndoStack.clear();
	RedoStack.clear();
	FrameEditBaseline = Graph;
	bUndoSnapshotCapturedThisFrame = false;
	bDirty = false;

	if (bLoaded)
	{
		RegenerateLuaSource();
	}
	else if (EditorEngine)
	{
		EditorEngine->GetNotificationService().Error(LastError.empty() ? "Failed to open Lua Anim Graph." : LastError);
	}
	return bLoaded;
}

bool FEditorLuaAnimGraphWidget::LoadAssetPayload()
{
	LastError.clear();

	FAssetMetaData MetaData;
	FAnimLuaProgramAssetPayload Payload;
	const bool bLoaded = FAssetFile::Load(AssetPath, MetaData, [&](FArchive& Ar)
	{
		Payload.Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});

	if (!bLoaded)
	{
		LastError = "Failed to load Lua Anim Graph asset.";
		return false;
	}
	if (MetaData.ClassName != UAnimLuaProgramAsset::StaticClass()->GetName())
	{
		LastError = "Selected asset is not UAnimLuaProgramAsset.";
		return false;
	}

	Graph = Payload.Graph;
	if (Graph.MachineName.empty())
	{
		Graph.MachineName = "Machine";
	}
	if (Graph.NextId <= 0)
	{
		Graph.NextId = 1;
	}
	GeneratedLuaSource = Payload.GeneratedLuaSource;
	return true;
}

bool FEditorLuaAnimGraphWidget::SaveAsset()
{
	if (AssetPath.empty())
	{
		return false;
	}

	RegenerateLuaSource();

	FAssetMetaData MetaData;
	MetaData.PayloadVersion = 4;
	MetaData.ClassName = UAnimLuaProgramAsset::StaticClass()->GetName();
	MetaData.DisplayName = GetFileNameFromPath(AssetPath);

	FAnimLuaProgramAssetPayload Payload;
	Payload.Graph = Graph;
	Payload.GeneratedLuaSource = GeneratedLuaSource;

	const bool bSaved = FAssetFile::Save(AssetPath, MetaData, [&](FArchive& Ar)
	{
		Payload.Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});

	if (bSaved)
	{
		bDirty = false;
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Info("Saved Lua Anim Graph.");
		}
	}
	else if (EditorEngine)
	{
		EditorEngine->GetNotificationService().Error("Failed to save Lua Anim Graph.");
	}

	return bSaved;
}

void FEditorLuaAnimGraphWidget::Close()
{
	bOpen = false;
}

void FEditorLuaAnimGraphWidget::DrawContent(float DeltaTime)
{
	(void)DeltaTime;
	if (!bLoaded)
	{
		ImGui::TextDisabled("No Lua Anim Graph asset is loaded.");
		if (!LastError.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", LastError.c_str());
		}
		return;
	}

	DrawToolbar();
	ImGui::Separator();

	const ImVec2 Available = ImGui::GetContentRegionAvail();
	if (ImGui::BeginTable("##LuaAnimGraphLayout", 3,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp,
		Available))
	{
		ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 300.0f);
		ImGui::TableSetupColumn("Lua", ImGuiTableColumnFlags_WidthFixed, 390.0f);
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		DrawGraphCanvas(ImGui::GetContentRegionAvail());

		ImGui::TableSetColumnIndex(1);
		DrawDetailsPanel();

		ImGui::TableSetColumnIndex(2);
		DrawLuaSourcePanel();

		ImGui::EndTable();
	}
}

void FEditorLuaAnimGraphWidget::DrawToolbar()
{
	ImGui::TextUnformatted(GetFileNameFromPath(AssetPath).c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("%s", bDirty ? "Modified" : "Saved");
	ImGui::SameLine();
	ImGui::TextDisabled("States %d | Transitions %d",
		static_cast<int>(Graph.States.size()),
		static_cast<int>(Graph.Transitions.size()));
	if (PendingTransitionFromStateId != 0)
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.95f, 0.68f, 0.30f, 1.0f), "Pick target state");
	}
	ImGui::SameLine();
	if (ImGui::Button("Save"))
	{
		SaveAsset();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!CanUndo());
	if (ImGui::Button("Undo"))
	{
		Undo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!CanRedo());
	if (ImGui::Button("Redo"))
	{
		Redo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Regenerate Lua"))
	{
		RegenerateLuaSource();
		MarkDirty();
	}
	ImGui::SameLine();
	if (ImGui::Button("Add State"))
	{
		AddState();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(SelectedStateId == 0 && SelectedTransitionId == 0);
	if (ImGui::Button("Delete"))
	{
		DeleteSelected();
	}
	ImGui::EndDisabled();
}

void FEditorLuaAnimGraphWidget::DrawGraphCanvas(const ImVec2& Size)
{
	const ImVec2 CanvasSize(std::max(320.0f, Size.x), std::max(240.0f, Size.y));
	ImGui::BeginChild("##LuaAnimGraphCanvas", CanvasSize, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	const ImVec2 CanvasOrigin = ImGui::GetCursorScreenPos();
	const ImVec2 CanvasMax = Add(CanvasOrigin, CanvasSize);
	const ImVec2 GraphOrigin = Add(CanvasOrigin, CanvasPanOffset);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	DrawList->AddRectFilled(CanvasOrigin, CanvasMax, ImGui::GetColorU32(ImVec4(0.055f, 0.060f, 0.074f, 1.0f)));
	const float GridStep = 32.0f;
	const float GridStartX = CanvasOrigin.x + std::fmod(CanvasPanOffset.x, GridStep);
	const float GridStartY = CanvasOrigin.y + std::fmod(CanvasPanOffset.y, GridStep);
	for (float X = GridStartX; X < CanvasMax.x; X += GridStep)
	{
		DrawList->AddLine(ImVec2(X, CanvasOrigin.y), ImVec2(X, CanvasMax.y), ImGui::GetColorU32(ImVec4(0.10f, 0.11f, 0.13f, 0.55f)));
	}
	for (float Y = GridStartY; Y < CanvasMax.y; Y += GridStep)
	{
		DrawList->AddLine(ImVec2(CanvasOrigin.x, Y), ImVec2(CanvasMax.x, Y), ImGui::GetColorU32(ImVec4(0.10f, 0.11f, 0.13f, 0.55f)));
	}

	const ImVec2 MousePos = ImGui::GetIO().MousePos;
	bool bMouseOverStateNode = false;
	const float Zoom = std::max(0.1f, CanvasZoom);
	for (const auto& Pair : Graph.States)
	{
		const FLuaAnimStateNode& State = Pair.second;
		const ImVec2 NodeMin = GraphToScreen(GraphOrigin, Zoom, State.EditorPosX, State.EditorPosY);
		const ImVec2 NodeMax = Add(NodeMin, ImVec2(NodeWidth * Zoom, NodeHeight * Zoom));
		if (ImGui::IsMouseHoveringRect(NodeMin, NodeMax, true))
		{
			bMouseOverStateNode = true;
			break;
		}
	}

	ImGui::SetCursorScreenPos(CanvasOrigin);
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton(
		"##LuaAnimGraphCanvasInput",
		CanvasSize,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool bCanvasHovered = ImGui::IsItemHovered();
	if (bCanvasHovered)
	{
		const ImGuiIO& IO = ImGui::GetIO();
		constexpr float WheelPanSpeed = 48.0f;
		if (std::fabs(IO.MouseWheel) > 0.0f)
		{
			if (IO.KeyShift)
			{
				CanvasPanOffset.x += IO.MouseWheel * WheelPanSpeed;
			}
			else
			{
				const float OldZoom = CanvasZoom;
				const ImVec2 MouseGraphBefore = ScreenToGraph(GraphOrigin, OldZoom, IO.MousePos);
				CanvasZoom = std::clamp(CanvasZoom * std::pow(1.12f, IO.MouseWheel), 0.35f, 2.25f);
				if (std::fabs(CanvasZoom - OldZoom) > 0.0001f)
				{
					CanvasPanOffset.x = IO.MousePos.x - CanvasOrigin.x - MouseGraphBefore.x * CanvasZoom;
					CanvasPanOffset.y = IO.MousePos.y - CanvasOrigin.y - MouseGraphBefore.y * CanvasZoom;
				}
			}
		}
		if (std::fabs(IO.MouseWheelH) > 0.0f)
		{
			CanvasPanOffset.x += IO.MouseWheelH * WheelPanSpeed;
		}
	}
	if (bCanvasHovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
		(ImGui::IsMouseDragging(ImGuiMouseButton_Right) && !ImGui::IsPopupOpen("##LuaAnimGraphCanvasContext"))))
	{
		CanvasPanOffset = Add(CanvasPanOffset, ImGui::GetIO().MouseDelta);
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
	}
	const bool bCanvasBackgroundClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !bMouseOverStateNode;
	if (bCanvasBackgroundClicked)
	{
		const ImGuiIO& IO = ImGui::GetIO();
		bMarqueeSelecting = false;
		bMarqueeAppend = IO.KeyCtrl || IO.KeyShift;
		MarqueeStart = MousePos;
		MarqueeEnd = MousePos;
		if (!bMarqueeAppend)
		{
			ClearStateMultiSelection();
		}
		SelectedTransitionId = 0;
	}
	if (bCanvasHovered && ImGui::IsItemActive() && !bMouseOverStateNode && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
	{
		bMarqueeSelecting = true;
		MarqueeEnd = MousePos;
	}
	if (bMarqueeSelecting && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		MarqueeEnd = MousePos;
	}
	if (bMarqueeSelecting && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		const ImVec2 RectMin = MinVec(MarqueeStart, MarqueeEnd);
		const ImVec2 RectMax = MaxVec(MarqueeStart, MarqueeEnd);
		if ((RectMax.x - RectMin.x) >= 4.0f || (RectMax.y - RectMin.y) >= 4.0f)
		{
			ApplyMarqueeSelection(RectMin, RectMax, GraphOrigin, bMarqueeAppend);
		}
		bMarqueeSelecting = false;
	}
	if (ImGui::BeginPopupContextItem("##LuaAnimGraphCanvasContext"))
	{
		const ImVec2 LocalPos = ScreenToGraph(GraphOrigin, CanvasZoom, ImGui::GetMousePosOnOpeningCurrentPopup());
		if (ImGui::MenuItem("Add State"))
		{
			AddStateAtPosition(LocalPos);
		}
		ImGui::BeginDisabled(SelectedStateId == 0 && SelectedTransitionId == 0);
		if (ImGui::MenuItem("Delete Selected", "Delete"))
		{
			DeleteSelected();
		}
		ImGui::EndDisabled();
		if (ImGui::MenuItem("Reset View"))
		{
			CanvasPanOffset = ImVec2(0.0f, 0.0f);
			CanvasZoom = 1.0f;
		}
		ImGui::EndPopup();
	}
	else if (bCanvasHovered && !bMouseOverStateNode && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		AddStateAtPosition(ScreenToGraph(GraphOrigin, CanvasZoom, ImGui::GetIO().MousePos));
	}

	if (ImGui::BeginDragDropTarget())
	{
		FString DroppedAnimationPath;
		if (TryAcceptAnimationDrop(DroppedAnimationPath))
		{
			AddStateForAnimation(DroppedAnimationPath, ScreenToGraph(GraphOrigin, CanvasZoom, ImGui::GetIO().MousePos));
		}
		ImGui::EndDragDropTarget();
	}

	DrawTransitions(GraphOrigin, CanvasZoom);
	for (auto& Pair : Graph.States)
	{
		DrawNode(Pair.second, GraphOrigin, CanvasZoom);
	}
	DrawPendingTransition(GraphOrigin, CanvasZoom);

	if (bMarqueeSelecting)
	{
		const ImVec2 RectMin = MinVec(MarqueeStart, MarqueeEnd);
		const ImVec2 RectMax = MaxVec(MarqueeStart, MarqueeEnd);
		DrawList->AddRectFilled(RectMin, RectMax, ImGui::GetColorU32(ImVec4(0.30f, 0.48f, 0.82f, 0.18f)));
		DrawList->AddRect(RectMin, RectMax, ImGui::GetColorU32(ImVec4(0.50f, 0.68f, 1.0f, 0.85f)));
	}

	if (Graph.States.empty())
	{
		ImGui::SetCursorScreenPos(Add(CanvasOrigin, ImVec2(18.0f, 18.0f)));
		ImGui::TextDisabled("Add or drop animation states here.");
	}

	ImGui::SetCursorScreenPos(CanvasMax);
	ImGui::Dummy(ImVec2(1.0f, 1.0f));
	ImGui::EndChild();
}

void FEditorLuaAnimGraphWidget::DrawNode(FLuaAnimStateNode& State, const ImVec2& CanvasOrigin, float InCanvasZoom)
{
	const float Zoom = std::max(0.1f, InCanvasZoom);
	const float ScaledNodeWidth = NodeWidth * Zoom;
	const float ScaledNodeHeight = NodeHeight * Zoom;
	const ImVec2 NodeMin = GraphToScreen(CanvasOrigin, Zoom, State.EditorPosX, State.EditorPosY);
	const ImVec2 NodeMax = Add(NodeMin, ImVec2(ScaledNodeWidth, ScaledNodeHeight));
	const bool bSelected = IsStateNodeSelected(State.StateId);
	const bool bInitial = Graph.InitialStateId == State.StateId;

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImU32 BgColor = ImGui::GetColorU32(bSelected ? ImVec4(0.145f, 0.175f, 0.225f, 1.0f) : ImVec4(0.095f, 0.105f, 0.128f, 1.0f));
	const ImU32 HeaderColor = ImGui::GetColorU32(bInitial ? ImVec4(0.28f, 0.43f, 0.64f, 1.0f) : ImVec4(0.18f, 0.21f, 0.26f, 1.0f));
	const ImU32 BorderColor = ImGui::GetColorU32(bSelected ? ImVec4(0.42f, 0.62f, 0.92f, 1.0f) : ImVec4(0.20f, 0.23f, 0.29f, 1.0f));

	DrawList->AddRectFilled(NodeMin, NodeMax, BgColor, 6.0f);
	DrawList->AddRectFilled(NodeMin, ImVec2(NodeMax.x, NodeMin.y + 27.0f * Zoom), HeaderColor, 6.0f, ImDrawFlags_RoundCornersTop);
	DrawList->AddRect(NodeMin, NodeMax, BorderColor, 6.0f, 0, bSelected ? 2.0f : 1.0f);

	const ImVec2 InPin(NodeMin.x, NodeMin.y + ScaledNodeHeight * 0.5f);
	const ImVec2 OutPin(NodeMax.x, NodeMin.y + ScaledNodeHeight * 0.5f);
	const ImVec2 Mouse = ImGui::GetIO().MousePos;
	const bool bWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	bool bPinClickHandled = false;
	if (bWindowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		const float PinHitRadius = 14.0f * Zoom;
		if (DistanceSquared(Mouse, OutPin) <= PinHitRadius * PinHitRadius)
		{
			BeginPendingTransition(State.StateId);
			bPinClickHandled = true;
		}
		else if (DistanceSquared(Mouse, InPin) <= PinHitRadius * PinHitRadius && PendingTransitionFromStateId != 0)
		{
			bPinClickHandled = CompletePendingTransition(State.StateId);
		}
	}

	const bool bPendingSource = PendingTransitionFromStateId == State.StateId;
	DrawList->AddCircleFilled(InPin, PinRadius * Zoom, ImGui::GetColorU32(ImVec4(0.35f, 0.55f, 0.86f, 1.0f)), 12);
	DrawList->AddCircleFilled(
		OutPin,
		(PinRadius + (bPendingSource ? 2.0f : 0.0f)) * Zoom,
		ImGui::GetColorU32(bPendingSource ? ImVec4(0.95f, 0.68f, 0.30f, 1.0f) : ImVec4(0.86f, 0.58f, 0.32f, 1.0f)),
		12);

	const FString Header = bInitial ? FString("* ") + State.Name : State.Name;
	const float TextScale = std::clamp(Zoom, 0.65f, 1.0f);
	const float FontSize = ImGui::GetFontSize() * TextScale;
	DrawList->PushClipRect(ImVec2(NodeMin.x + 8.0f * Zoom, NodeMin.y + 4.0f * Zoom), ImVec2(NodeMax.x - 8.0f * Zoom, NodeMin.y + 27.0f * Zoom), true);
	DrawList->AddText(nullptr, FontSize, ImVec2(NodeMin.x + 12.0f * Zoom, NodeMin.y + 6.0f * Zoom), ImGui::GetColorU32(ImGuiCol_Text), Header.c_str());
	DrawList->PopClipRect();

	const FString AnimLabel = State.AnimationPath.empty() ? FString("No animation") : GetFileNameFromPath(State.AnimationPath);
	if (Zoom >= 0.48f)
	{
		DrawList->PushClipRect(ImVec2(NodeMin.x + 8.0f * Zoom, NodeMin.y + 32.0f * Zoom), ImVec2(NodeMax.x - 8.0f * Zoom, NodeMax.y - 8.0f * Zoom), true);
		DrawList->AddText(nullptr, FontSize, ImVec2(NodeMin.x + 12.0f * Zoom, NodeMin.y + 39.0f * Zoom), ImGui::GetColorU32(ImGuiCol_TextDisabled), AnimLabel.c_str());
		DrawList->PopClipRect();
	}
	if (!State.AnimationPath.empty() && ImGui::IsMouseHoveringRect(NodeMin, NodeMax))
	{
		ImGui::SetTooltip("%s", State.AnimationPath.c_str());
	}

	ImGui::SetCursorScreenPos(NodeMin);
	ImGui::PushID(State.StateId);
	ImGui::InvisibleButton("##LuaAnimStateNode", ImVec2(ScaledNodeWidth, ScaledNodeHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	if (!bPinClickHandled && ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		const ImGuiIO& IO = ImGui::GetIO();
		SelectStateNode(State.StateId, IO.KeyCtrl || IO.KeyShift);
		SelectedTransitionId = 0;
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		if (!IsStateNodeSelected(State.StateId))
		{
			SelectStateNode(State.StateId, false);
		}
		SelectedTransitionId = 0;
		ImGui::OpenPopup("##LuaAnimStateContext");
	}
	if (ImGui::BeginPopup("##LuaAnimStateContext"))
	{
		ImGui::TextDisabled("%s", State.Name.c_str());
		ImGui::Separator();
		if (ImGui::MenuItem("Set Initial State"))
		{
			Graph.InitialStateId = State.StateId;
			MarkDirty();
		}
		if (ImGui::MenuItem("Start Transition"))
		{
			BeginPendingTransition(State.StateId);
		}
		if (ImGui::MenuItem("Delete State", "Delete"))
		{
			DeleteSelected();
			ImGui::EndPopup();
			ImGui::PopID();
			return;
		}
		ImGui::EndPopup();
	}
	const bool bSelectedForDrag = IsStateNodeSelected(State.StateId);
	if (bSelectedForDrag && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		const ImVec2 Delta = ImGui::GetIO().MouseDelta;
		if (SelectedStateIds.size() > 1)
		{
			for (auto& SelectedPair : Graph.States)
			{
				FLuaAnimStateNode& SelectedState = SelectedPair.second;
				if (SelectedStateIds.find(SelectedState.StateId) != SelectedStateIds.end())
				{
					SelectedState.EditorPosX = std::max(0.0f, SelectedState.EditorPosX + Delta.x / Zoom);
					SelectedState.EditorPosY = std::max(0.0f, SelectedState.EditorPosY + Delta.y / Zoom);
				}
			}
		}
		else
		{
			State.EditorPosX = std::max(0.0f, State.EditorPosX + Delta.x / Zoom);
			State.EditorPosY = std::max(0.0f, State.EditorPosY + Delta.y / Zoom);
		}
		MarkDirty();
	}
	if (ImGui::BeginDragDropTarget())
	{
		FString DroppedAnimationPath;
		if (TryAcceptAnimationDrop(DroppedAnimationPath))
		{
			State.AnimationPath = DroppedAnimationPath;
			if (State.Name.empty() || State.Name == "New State")
			{
				State.Name = GetFileNameFromPath(DroppedAnimationPath);
			}
			SelectStateNode(State.StateId, false);
			SelectedTransitionId = 0;
			MarkDirty();
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::PopID();
}

void FEditorLuaAnimGraphWidget::DrawTransitions(const ImVec2& CanvasOrigin, float InCanvasZoom)
{
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Mouse = ImGui::GetIO().MousePos;
	const float Zoom = std::max(0.1f, InCanvasZoom);
	int32 ContextTransitionId = 0;

	for (auto& Pair : Graph.Transitions)
	{
		FLuaAnimTransitionLink& Transition = Pair.second;
		const FLuaAnimStateNode* From = Graph.FindState(Transition.FromStateId);
		const FLuaAnimStateNode* To = Graph.FindState(Transition.ToStateId);
		if (!From || !To)
		{
			continue;
		}

		const ImVec2 FromPos = GraphToScreen(CanvasOrigin, Zoom, From->EditorPosX + NodeWidth, From->EditorPosY + NodeHeight * 0.5f);
		const ImVec2 ToPos = GraphToScreen(CanvasOrigin, Zoom, To->EditorPosX, To->EditorPosY + NodeHeight * 0.5f);
		const bool bSelected = SelectedTransitionId == Transition.TransitionId;
		const ImU32 Color = ImGui::GetColorU32(bSelected ? ImVec4(0.95f, 0.68f, 0.30f, 1.0f) : ImVec4(0.45f, 0.55f, 0.68f, 1.0f));
		DrawList->AddLine(FromPos, ToPos, Color, bSelected ? 3.0f : 2.0f);

		const ImVec2 Direction = Sub(ToPos, FromPos);
		const ImVec2 Tip = ToPos;
		const float Length = std::max(0.001f, std::sqrt(Direction.x * Direction.x + Direction.y * Direction.y));
		const ImVec2 Unit(Direction.x / Length, Direction.y / Length);
		const ImVec2 Normal(-Unit.y, Unit.x);
		const ImVec2 Base = Sub(Tip, Mul(Unit, 12.0f * Zoom));
		DrawList->AddTriangleFilled(Tip, Add(Base, Mul(Normal, 6.0f * Zoom)), Sub(Base, Mul(Normal, 6.0f * Zoom)), Color);

		const float HitRadiusSq = LinkHitRadiusSq * Zoom * Zoom;
		if (DistanceSquaredToSegment(Mouse, FromPos, ToPos) <= HitRadiusSq && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			SelectedTransitionId = Transition.TransitionId;
			ClearStateMultiSelection();
		}
		if (DistanceSquaredToSegment(Mouse, FromPos, ToPos) <= HitRadiusSq && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			SelectedTransitionId = Transition.TransitionId;
			ClearStateMultiSelection();
			ContextTransitionId = Transition.TransitionId;
		}
	}

	if (ContextTransitionId != 0)
	{
		ImGui::OpenPopup("##LuaAnimTransitionContext");
	}
	if (ImGui::BeginPopup("##LuaAnimTransitionContext"))
	{
		ImGui::TextDisabled("Transition #%d", SelectedTransitionId);
		ImGui::Separator();
		if (ImGui::MenuItem("Delete Transition", "Delete"))
		{
			DeleteSelected();
		}
		ImGui::EndPopup();
	}
}

void FEditorLuaAnimGraphWidget::DrawDetailsPanel()
{
	ImGui::TextUnformatted("Details");
	ImGui::Separator();

	if (InputFStringFixed("Machine", Graph.MachineName, 128))
	{
		MarkDirty();
	}
	if (RenderSkeletalMeshPathField("Preview Mesh", Graph.PreviewSkeletalMeshPath))
	{
		MarkDirty();
	}

	DrawRuntimeChecksPanel();

	if (Graph.States.empty())
	{
		ImGui::TextDisabled("No states.");
		return;
	}

	FString InitialLabel = "None";
	if (const FLuaAnimStateNode* Initial = Graph.FindState(Graph.InitialStateId))
	{
		InitialLabel = Initial->Name;
	}
	if (ImGui::BeginCombo("Initial State", InitialLabel.c_str()))
	{
		for (auto& Pair : Graph.States)
		{
			FLuaAnimStateNode& State = Pair.second;
			const bool bSelected = Graph.InitialStateId == State.StateId;
			if (ImGui::Selectable(State.Name.c_str(), bSelected))
			{
				Graph.InitialStateId = State.StateId;
				MarkDirty();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::Separator();
	if (FLuaAnimStateNode* State = FindSelectedState())
	{
		DrawStateDetails(*State);
	}
	else if (FLuaAnimTransitionLink* Transition = FindSelectedTransition())
	{
		DrawTransitionDetails(*Transition);
	}
	else
	{
		ImGui::TextDisabled("Select a state or transition.");
	}
}

void FEditorLuaAnimGraphWidget::DrawRuntimeChecksPanel()
{
	std::vector<FString> Warnings;
	std::vector<FString> Notes;

	if (Graph.States.empty())
	{
		Warnings.push_back("Graph has no states.");
	}
	else if (!Graph.FindState(Graph.InitialStateId))
	{
		Warnings.push_back("Initial state is not assigned.");
	}

	for (const auto& Pair : Graph.States)
	{
		const FLuaAnimStateNode& State = Pair.second;
		const FString StateLabel = State.Name.empty() ? FString("<Unnamed>") : State.Name;
		if (State.Name.empty())
		{
			Warnings.push_back("State has an empty name.");
		}
		else if (IsStateNameDuplicated(Graph, State))
		{
			Warnings.push_back(FString("Duplicated state name: ") + State.Name);
		}
		if (State.AnimationPath.empty())
		{
			Warnings.push_back(FString("State has no animation: ") + StateLabel);
		}
	}

	for (const auto& Pair : Graph.Transitions)
	{
		const FLuaAnimTransitionLink& Transition = Pair.second;
		const FLuaAnimStateNode* From = Graph.FindState(Transition.FromStateId);
		const FLuaAnimStateNode* To = Graph.FindState(Transition.ToStateId);
		if (!From || !To)
		{
			Warnings.push_back("Transition has a missing endpoint.");
			continue;
		}
		if (Transition.Conditions.empty())
		{
			Notes.push_back(FString("Always-true transition: ") + From->Name + " -> " + To->Name);
		}
	}

	const int WarningCount = static_cast<int>(Warnings.size());
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_DefaultOpen;
	if (WarningCount == 0)
	{
		Flags = 0;
	}

	if (ImGui::TreeNodeEx("Runtime Checks", Flags, "Runtime Checks (%d)", WarningCount))
	{
		if (Warnings.empty() && Notes.empty())
		{
			ImGui::TextDisabled("Ready for runtime playback.");
		}
		for (const FString& Warning : Warnings)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.58f, 0.28f, 1.0f), "- %s", Warning.c_str());
		}
		for (const FString& Note : Notes)
		{
			ImGui::TextDisabled("- %s", Note.c_str());
		}
		ImGui::TreePop();
	}
}

void FEditorLuaAnimGraphWidget::DrawStateDetails(FLuaAnimStateNode& State)
{
	ImGui::Text("State #%d", State.StateId);
	if (InputFStringFixed("Name", State.Name, 128))
	{
		MarkDirty();
	}
	if (RenderAnimationPathField("Animation", State.AnimationPath))
	{
		MarkDirty();
	}
	if (ImGui::Checkbox("Loop", &State.bLoop))
	{
		MarkDirty();
	}
	if (ImGui::DragFloat("Play Rate", &State.PlayRate, 0.01f, 0.01f, 4.0f, "%.2f"))
	{
		MarkDirty();
	}
	if (ImGui::Button("Set Initial"))
	{
		Graph.InitialStateId = State.StateId;
		MarkDirty();
	}
	FString TargetLabel = "Add Transition To...";
	if (ImGui::BeginCombo("##AddTransitionTarget", TargetLabel.c_str()))
	{
		for (auto& Pair : Graph.States)
		{
			FLuaAnimStateNode& Target = Pair.second;
			if (Target.StateId == State.StateId)
			{
				continue;
			}
			const bool bCanCreate = Graph.CanCreateTransition(State.StateId, Target.StateId);
			ImGui::BeginDisabled(!bCanCreate);
			if (ImGui::Selectable(Target.Name.c_str(), false))
			{
				SelectOrCreateTransition(State.StateId, Target.StateId);
			}
			ImGui::EndDisabled();
			if (!bCanCreate && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("Transition already exists.");
			}
		}
		ImGui::EndCombo();
	}
}

void FEditorLuaAnimGraphWidget::DrawTransitionDetails(FLuaAnimTransitionLink& Transition)
{
	const FLuaAnimStateNode* From = Graph.FindState(Transition.FromStateId);
	const FLuaAnimStateNode* To = Graph.FindState(Transition.ToStateId);
	ImGui::Text("Transition #%d", Transition.TransitionId);
	ImGui::TextDisabled("%s -> %s", From ? From->Name.c_str() : "Missing", To ? To->Name.c_str() : "Missing");

	if (ImGui::DragFloat("Blend Time", &Transition.BlendTime, 0.01f, 0.0f, 5.0f, "%.2f"))
	{
		MarkDirty();
	}
	if (ImGui::Checkbox("Reset Time", &Transition.bResetTime))
	{
		MarkDirty();
	}

	const char* BlendLabels[] = { "Linear", "EaseIn", "EaseOut", "EaseInOut" };
	int BlendIndex = static_cast<int>(Transition.BlendMode);
	if (ImGui::Combo("Blend Mode", &BlendIndex, BlendLabels, static_cast<int>(std::size(BlendLabels))))
	{
		Transition.BlendMode = static_cast<EAnimBlendMode>(BlendIndex);
		MarkDirty();
	}

	const char* JoinLabels[] = { "And", "Or" };
	int JoinIndex = Transition.Join == EAnimConditionJoin::Or ? 1 : 0;
	if (ImGui::Combo("Condition Join", &JoinIndex, JoinLabels, static_cast<int>(std::size(JoinLabels))))
	{
		Transition.Join = JoinIndex == 1 ? EAnimConditionJoin::Or : EAnimConditionJoin::And;
		MarkDirty();
	}

	ImGui::Separator();
	ImGui::Text("Conditions");
	for (int32 Index = 0; Index < static_cast<int32>(Transition.Conditions.size()); ++Index)
	{
		FAnimCondition& Condition = Transition.Conditions[Index];
		ImGui::PushID(Index);
		if (ImGui::TreeNodeEx("Condition", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (InputFStringFixed("Context", Condition.ContextName, 128))
			{
				MarkDirty();
			}
			const char* OpLabels[] = { "==", "!=", "<", "<=", ">", ">=" };
			int OpIndex = static_cast<int>(Condition.Operator);
			if (ImGui::Combo("Operator", &OpIndex, OpLabels, static_cast<int>(std::size(OpLabels))))
			{
				Condition.Operator = static_cast<EAnimCompareOp>(OpIndex);
				MarkDirty();
			}
			if (InputFStringFixed("Value", Condition.Value, 128))
			{
				MarkDirty();
			}
			if (ImGui::Checkbox("Use Default", &Condition.bUseDefaultValue))
			{
				MarkDirty();
			}
			if (Condition.bUseDefaultValue && InputFStringFixed("Default", Condition.DefaultValue, 128))
			{
				MarkDirty();
			}
			if (ImGui::SmallButton("Remove"))
			{
				Transition.Conditions.erase(Transition.Conditions.begin() + Index);
				MarkDirty();
				ImGui::TreePop();
				ImGui::PopID();
				break;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (ImGui::Button("Add Condition"))
	{
		FAnimCondition NewCondition;
		NewCondition.ContextName = "speed";
		NewCondition.Operator = EAnimCompareOp::Greater;
		NewCondition.Value = "0";
		Transition.Conditions.push_back(NewCondition);
		MarkDirty();
	}

	ImGui::TextDisabled("Join: %s, Blend: %s", ToJoinLabel(Transition.Join), ToBlendModeLabel(Transition.BlendMode));
	if (!Transition.Conditions.empty())
	{
		ImGui::TextDisabled("First op: %s", ToCompareLabel(Transition.Conditions.front().Operator));
	}
}

void FEditorLuaAnimGraphWidget::DrawLuaSourcePanel()
{
	DrawPreviewPanel();
	ImGui::Separator();
	ImGui::TextUnformatted("Generated Lua");
	ImGui::SameLine();
	if (ImGui::SmallButton("Copy"))
	{
		ImGui::SetClipboardText(GeneratedLuaSource.c_str());
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Info("Copied generated Lua.");
		}
	}
	ImGui::Separator();
	ImGui::BeginChild("##GeneratedLuaSource", ImGui::GetContentRegionAvail(), true, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::TextUnformatted(GeneratedLuaSource.c_str());
	ImGui::EndChild();
}

void FEditorLuaAnimGraphWidget::DrawPreviewPanel()
{
	ImGui::TextUnformatted("Preview");
	ImGui::Separator();

	FLuaAnimStateNode* SelectedState = FindSelectedState();
	FLuaAnimTransitionLink* SelectedTransition = FindSelectedTransition();
	const FLuaAnimStateNode* FromState = SelectedTransition ? Graph.FindState(SelectedTransition->FromStateId) : nullptr;
	const FLuaAnimStateNode* ToState = SelectedTransition ? Graph.FindState(SelectedTransition->ToStateId) : nullptr;

	if (!Graph.PreviewSkeletalMeshPath.empty())
	{
		if (ImGui::Button("Open Preview Mesh"))
		{
			OpenViewerForPath(Graph.PreviewSkeletalMeshPath);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("Mesh: %s", GetFileNameFromPath(Graph.PreviewSkeletalMeshPath).c_str());
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", Graph.PreviewSkeletalMeshPath.c_str());
		}
	}
	else
	{
		ImGui::TextDisabled("No preview mesh selected. Drop or assign one in Details.");
	}

	if (SelectedState)
	{
		ImGui::Text("State: %s", SelectedState->Name.c_str());
		ImGui::TextDisabled("Animation: %s",
			SelectedState->AnimationPath.empty()
				? "<None>"
				: GetFileNameFromPath(SelectedState->AnimationPath).c_str());
		ImGui::BeginDisabled(SelectedState->AnimationPath.empty());
		if (ImGui::Button("Open State Animation"))
		{
			OpenViewerForPath(SelectedState->AnimationPath);
		}
		ImGui::EndDisabled();
	}
	else if (SelectedTransition)
	{
		ImGui::Text("Transition: %s -> %s", FromState ? FromState->Name.c_str() : "Missing", ToState ? ToState->Name.c_str() : "Missing");
		ImGui::TextDisabled("Blend %.2fs | Conditions %d | Reset %s",
			SelectedTransition->BlendTime,
			static_cast<int>(SelectedTransition->Conditions.size()),
			SelectedTransition->bResetTime ? "On" : "Off");
		ImGui::BeginDisabled(!FromState || FromState->AnimationPath.empty());
		if (ImGui::Button("Open From Animation"))
		{
			OpenViewerForPath(FromState->AnimationPath);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!ToState || ToState->AnimationPath.empty());
		if (ImGui::Button("Open To Animation"))
		{
			OpenViewerForPath(ToState->AnimationPath);
		}
		ImGui::EndDisabled();
	}
	else
	{
		const FLuaAnimStateNode* InitialState = Graph.FindState(Graph.InitialStateId);
		ImGui::TextDisabled("Select a state or transition.");
		ImGui::TextDisabled("Initial: %s", InitialState ? InitialState->Name.c_str() : "<None>");
	}
}

void FEditorLuaAnimGraphWidget::HandleShortcuts()
{
	if (!bLoaded)
	{
		return;
	}

	const ImGuiIO& IO = ImGui::GetIO();
	const bool bInGraphEditorContext =
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
		ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
	if (!bInGraphEditorContext || IO.WantTextInput)
	{
		return;
	}
	if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
	{
		if (IO.KeyShift)
		{
			Redo();
		}
		else
		{
			Undo();
		}
		return;
	}
	if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
	{
		Redo();
		return;
	}
	if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		SaveAsset();
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		DeleteSelected();
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
	{
		PendingTransitionFromStateId = 0;
	}
}

void FEditorLuaAnimGraphWidget::DrawPendingTransition(const ImVec2& CanvasOrigin, float InCanvasZoom)
{
	const FLuaAnimStateNode* From = Graph.FindState(PendingTransitionFromStateId);
	if (!From)
	{
		return;
	}

	const float Zoom = std::max(0.1f, InCanvasZoom);
	const ImVec2 FromPos = GraphToScreen(CanvasOrigin, Zoom, From->EditorPosX + NodeWidth, From->EditorPosY + NodeHeight * 0.5f);
	const ImVec2 Mouse = ImGui::GetIO().MousePos;
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImU32 Color = ImGui::GetColorU32(ImVec4(0.95f, 0.68f, 0.30f, 0.85f));
	DrawList->AddLine(FromPos, Mouse, Color, 2.0f);
	DrawList->AddCircleFilled(Mouse, 4.0f, Color, 10);
}

void FEditorLuaAnimGraphWidget::RegenerateLuaSource()
{
	GeneratedLuaSource = FLuaAnimGraphCodeGenerator().Generate(Graph);
}

void FEditorLuaAnimGraphWidget::BeginEditFrame()
{
	FrameEditBaseline = Graph;
	bUndoSnapshotCapturedThisFrame = false;
}

void FEditorLuaAnimGraphWidget::CaptureUndoSnapshot()
{
	if (bUndoSnapshotCapturedThisFrame)
	{
		return;
	}

	constexpr size_t MaxUndoSnapshots = 80;
	UndoStack.push_back(FrameEditBaseline);
	if (UndoStack.size() > MaxUndoSnapshots)
	{
		UndoStack.erase(UndoStack.begin());
	}
	RedoStack.clear();
	bUndoSnapshotCapturedThisFrame = true;
}

void FEditorLuaAnimGraphWidget::RestoreGraphSnapshot(const FLuaAnimGraph& Snapshot)
{
	Graph = Snapshot;
	if (SelectedStateId != 0 && !Graph.FindState(SelectedStateId))
	{
		SelectedStateId = 0;
	}
	for (auto It = SelectedStateIds.begin(); It != SelectedStateIds.end();)
	{
		if (!Graph.FindState(*It))
		{
			It = SelectedStateIds.erase(It);
		}
		else
		{
			++It;
		}
	}
	if (SelectedTransitionId != 0 && !Graph.FindTransition(SelectedTransitionId))
	{
		SelectedTransitionId = 0;
	}
	if (PendingTransitionFromStateId != 0 && !Graph.FindState(PendingTransitionFromStateId))
	{
		PendingTransitionFromStateId = 0;
	}
	RegenerateLuaSource();
	bDirty = true;
}

void FEditorLuaAnimGraphWidget::Undo()
{
	if (UndoStack.empty())
	{
		return;
	}

	RedoStack.push_back(Graph);
	const FLuaAnimGraph Snapshot = UndoStack.back();
	UndoStack.pop_back();
	RestoreGraphSnapshot(Snapshot);
	bUndoSnapshotCapturedThisFrame = true;
}

void FEditorLuaAnimGraphWidget::Redo()
{
	if (RedoStack.empty())
	{
		return;
	}

	UndoStack.push_back(Graph);
	const FLuaAnimGraph Snapshot = RedoStack.back();
	RedoStack.pop_back();
	RestoreGraphSnapshot(Snapshot);
	bUndoSnapshotCapturedThisFrame = true;
}

void FEditorLuaAnimGraphWidget::MarkDirty()
{
	CaptureUndoSnapshot();
	RegenerateLuaSource();
	bDirty = true;
}

void FEditorLuaAnimGraphWidget::AddState()
{
	const float Offset = static_cast<float>(Graph.States.size());
	AddStateAtPosition(ImVec2(
		80.0f + std::fmod(Offset, 4.0f) * 260.0f,
		80.0f + std::floor(Offset / 4.0f) * 130.0f));
}

void FEditorLuaAnimGraphWidget::AddStateAtPosition(const ImVec2& LocalPos)
{
	FLuaAnimStateNode& State = Graph.AddState(
		"New State",
		"",
		std::max(0.0f, LocalPos.x),
		std::max(0.0f, LocalPos.y));
	if (Graph.InitialStateId == 0)
	{
		Graph.InitialStateId = State.StateId;
	}
	SelectStateNode(State.StateId, false);
	SelectedTransitionId = 0;
	MarkDirty();
}

void FEditorLuaAnimGraphWidget::AddStateForAnimation(const FString& AnimationPath, const ImVec2& LocalPos)
{
	FString StateName = GetFileNameFromPath(AnimationPath);
	const size_t DotIndex = StateName.find_last_of('.');
	if (DotIndex != FString::npos)
	{
		StateName = StateName.substr(0, DotIndex);
	}
	if (StateName.empty())
	{
		StateName = "Animation State";
	}

	FLuaAnimStateNode& State = Graph.AddState(
		StateName,
		AnimationPath,
		std::max(0.0f, LocalPos.x - NodeWidth * 0.5f),
		std::max(0.0f, LocalPos.y - NodeHeight * 0.5f));
	if (Graph.InitialStateId == 0)
	{
		Graph.InitialStateId = State.StateId;
	}
	SelectStateNode(State.StateId, false);
	SelectedTransitionId = 0;
	PendingTransitionFromStateId = 0;
	MarkDirty();
}

bool FEditorLuaAnimGraphWidget::TryAcceptAnimationDrop(FString& OutAnimationPath)
{
	if (TryNormalizeDroppedPath(ImGui::AcceptDragDropPayload("AnimSequenceContentItem"), OutAnimationPath))
	{
		return true;
	}
	return false;
}

bool FEditorLuaAnimGraphWidget::RenderAnimationPathField(const char* Label, FString& Path)
{
	bool bChanged = false;
	const FString PreviewText = Path.empty() ? FString("<None>") : Path;
	if (ImGui::BeginCombo(Label, PreviewText.c_str()))
	{
		if (ImGui::Selectable("<None>", Path.empty()))
		{
			Path.clear();
			bChanged = true;
		}
		static const TArray<FString> EmptyAnimPaths;
		const TArray<FString>& AnimPaths = EditorEngine
			? EditorEngine->GetAssetService().GetAnimSequenceAssetPaths()
			: EmptyAnimPaths;
		for (const FString& AnimPath : AnimPaths)
		{
			const bool bSelected = Path == AnimPath;
			if (ImGui::Selectable(AnimPath.c_str(), bSelected))
			{
				Path = AnimPath;
				bChanged = true;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	if (ImGui::BeginDragDropTarget())
	{
		FString DroppedAnimationPath;
		if (TryAcceptAnimationDrop(DroppedAnimationPath))
		{
			Path = DroppedAnimationPath;
			bChanged = true;
		}
		ImGui::EndDragDropTarget();
	}

	return bChanged;
}

bool FEditorLuaAnimGraphWidget::RenderSkeletalMeshPathField(const char* Label, FString& Path)
{
	bool bChanged = false;
	const FString PreviewText = Path.empty() ? FString("<None>") : Path;
	if (ImGui::BeginCombo(Label, PreviewText.c_str()))
	{
		if (ImGui::Selectable("<None>", Path.empty()))
		{
			Path.clear();
			bChanged = true;
		}
		static const TArray<FString> EmptyMeshPaths;
		const TArray<FString>& MeshPaths = EditorEngine
			? EditorEngine->GetAssetService().GetSkeletalMeshAssetPaths()
			: EmptyMeshPaths;
		for (const FString& MeshPath : MeshPaths)
		{
			const bool bSelected = Path == MeshPath;
			if (ImGui::Selectable(MeshPath.c_str(), bSelected))
			{
				Path = MeshPath;
				bChanged = true;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	if (ImGui::BeginDragDropTarget())
	{
		FString DroppedPath;
		if (TryNormalizeDroppedPath(ImGui::AcceptDragDropPayload("ObjectContentItem"), DroppedPath))
		{
			static const TArray<FString> EmptyMeshPaths;
			const TArray<FString>& MeshPaths = EditorEngine
				? EditorEngine->GetAssetService().GetSkeletalMeshAssetPaths()
				: EmptyMeshPaths;
			if (std::find(MeshPaths.begin(), MeshPaths.end(), DroppedPath) != MeshPaths.end())
			{
				Path = DroppedPath;
				bChanged = true;
			}
			else if (EditorEngine)
			{
				EditorEngine->GetNotificationService().Warning("Drop a Skeletal Mesh asset for preview.");
			}
		}
		ImGui::EndDragDropTarget();
	}

	return bChanged;
}

void FEditorLuaAnimGraphWidget::BeginPendingTransition(int32 FromStateId)
{
	PendingTransitionFromStateId = FromStateId;
	SelectStateNode(FromStateId, false);
	SelectedTransitionId = 0;
}

bool FEditorLuaAnimGraphWidget::CompletePendingTransition(int32 ToStateId)
{
	if (PendingTransitionFromStateId == 0 || PendingTransitionFromStateId == ToStateId)
	{
		PendingTransitionFromStateId = 0;
		return false;
	}

	const int32 FromStateId = PendingTransitionFromStateId;
	PendingTransitionFromStateId = 0;
	return SelectOrCreateTransition(FromStateId, ToStateId);
}

bool FEditorLuaAnimGraphWidget::SelectOrCreateTransition(int32 FromStateId, int32 ToStateId)
{
	for (auto& Pair : Graph.Transitions)
	{
		FLuaAnimTransitionLink& Existing = Pair.second;
		if (Existing.FromStateId == FromStateId && Existing.ToStateId == ToStateId)
		{
			SelectedTransitionId = Existing.TransitionId;
			ClearStateMultiSelection();
			return true;
		}
	}

	FLuaAnimTransitionLink* Link = Graph.AddTransition(FromStateId, ToStateId);
	if (!Link)
	{
		return false;
	}

	SelectedTransitionId = Link->TransitionId;
	ClearStateMultiSelection();
	MarkDirty();
	return true;
}

void FEditorLuaAnimGraphWidget::OpenViewerForPath(const FString& Path)
{
	if (EditorEngine && !Path.empty())
	{
		EditorEngine->CreateViewer(Path);
	}
}

void FEditorLuaAnimGraphWidget::DeleteSelected()
{
	if (SelectedTransitionId != 0)
	{
		Graph.DeleteTransition(SelectedTransitionId);
		SelectedTransitionId = 0;
		PendingTransitionFromStateId = 0;
		MarkDirty();
		return;
	}
	if (SelectedStateId != 0)
	{
		if (SelectedStateIds.size() > 1)
		{
			const std::vector<int32> StateIdsToDelete(SelectedStateIds.begin(), SelectedStateIds.end());
			for (int32 StateIdToDelete : StateIdsToDelete)
			{
				Graph.DeleteState(StateIdToDelete);
			}
		}
		else
		{
			Graph.DeleteState(SelectedStateId);
		}
		ClearStateMultiSelection();
		PendingTransitionFromStateId = 0;
		MarkDirty();
	}
}

void FEditorLuaAnimGraphWidget::ClearStateMultiSelection()
{
	SelectedStateId = 0;
	SelectedStateIds.clear();
}

void FEditorLuaAnimGraphWidget::SelectStateNode(int32 StateId, bool bAppendOrToggle)
{
	if (StateId == 0)
	{
		ClearStateMultiSelection();
		return;
	}

	if (!bAppendOrToggle)
	{
		SelectedStateIds.clear();
		SelectedStateIds.insert(StateId);
		SelectedStateId = StateId;
		return;
	}

	if (SelectedStateIds.find(StateId) != SelectedStateIds.end())
	{
		SelectedStateIds.erase(StateId);
		if (SelectedStateId == StateId)
		{
			SelectedStateId = SelectedStateIds.empty() ? 0 : *SelectedStateIds.begin();
		}
	}
	else
	{
		SelectedStateIds.insert(StateId);
		SelectedStateId = StateId;
	}
}

bool FEditorLuaAnimGraphWidget::IsStateNodeSelected(int32 StateId) const
{
	return StateId != 0 &&
		(SelectedStateId == StateId || SelectedStateIds.find(StateId) != SelectedStateIds.end());
}

void FEditorLuaAnimGraphWidget::ApplyMarqueeSelection(const ImVec2& RectMin, const ImVec2& RectMax, const ImVec2& GraphOrigin, bool bAppend)
{
	if (!bAppend)
	{
		ClearStateMultiSelection();
	}

	SelectedTransitionId = 0;
	const float Zoom = std::max(0.1f, CanvasZoom);
	for (const auto& Pair : Graph.States)
	{
		const FLuaAnimStateNode& State = Pair.second;
		const ImVec2 NodeMin = GraphToScreen(GraphOrigin, Zoom, State.EditorPosX, State.EditorPosY);
		const ImVec2 NodeMax = Add(NodeMin, ImVec2(NodeWidth * Zoom, NodeHeight * Zoom));
		if (RectsOverlap(RectMin, RectMax, NodeMin, NodeMax))
		{
			SelectedStateIds.insert(State.StateId);
			SelectedStateId = State.StateId;
		}
	}
}

FLuaAnimStateNode* FEditorLuaAnimGraphWidget::FindSelectedState()
{
	return SelectedStateId != 0 ? Graph.FindState(SelectedStateId) : nullptr;
}

FLuaAnimTransitionLink* FEditorLuaAnimGraphWidget::FindSelectedTransition()
{
	return SelectedTransitionId != 0 ? Graph.FindTransition(SelectedTransitionId) : nullptr;
}

FString FEditorLuaAnimGraphWidget::GetFileNameFromPath(const FString& Path) const
{
	const size_t SlashIndex = Path.find_last_of("/\\");
	return SlashIndex == FString::npos ? Path : Path.substr(SlashIndex + 1);
}
