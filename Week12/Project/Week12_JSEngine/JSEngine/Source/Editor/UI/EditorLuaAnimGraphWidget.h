#pragma once

#include "Animation/LuaAnimGraph.h"
#include "Editor/UI/EditorWidget.h"

#include "ImGui/imgui.h"

#include <unordered_set>
#include <vector>

class FEditorLuaAnimGraphWidget : public FEditorWidget
{
public:
	void Initialize(UEditorEngine* InEditorEngine) override;
	void Render(float DeltaTime) override;
	void RenderEmbedded(float DeltaTime);

	bool OpenAsset(const FString& InAssetPath);
	bool SaveAsset();
	void Close();

	bool IsOpen() const { return bOpen; }
	bool IsDirty() const { return bDirty; }
	const FString& GetAssetPath() const { return AssetPath; }
	const FString& GetGeneratedLuaSource() const { return GeneratedLuaSource; }

private:
	void DrawContent(float DeltaTime);
	void DrawToolbar();
	void DrawGraphCanvas(const ImVec2& Size);
	void DrawNode(FLuaAnimStateNode& State, const ImVec2& CanvasOrigin, float CanvasZoom);
	void DrawTransitions(const ImVec2& CanvasOrigin, float CanvasZoom);
	void DrawDetailsPanel();
	void DrawStateDetails(FLuaAnimStateNode& State);
	void DrawTransitionDetails(FLuaAnimTransitionLink& Transition);
	void DrawPreviewPanel();
	void DrawLuaSourcePanel();
	void DrawRuntimeChecksPanel();
	void HandleShortcuts();
	void DrawPendingTransition(const ImVec2& CanvasOrigin, float CanvasZoom);
	void BeginEditFrame();
	void CaptureUndoSnapshot();
	void Undo();
	void Redo();
	bool CanUndo() const { return !UndoStack.empty(); }
	bool CanRedo() const { return !RedoStack.empty(); }
	void RestoreGraphSnapshot(const FLuaAnimGraph& Snapshot);

	bool LoadAssetPayload();
	void RegenerateLuaSource();
	void MarkDirty();
	void AddState();
	void AddStateAtPosition(const ImVec2& LocalPos);
	void AddStateForAnimation(const FString& AnimationPath, const ImVec2& LocalPos);
	bool TryAcceptAnimationDrop(FString& OutAnimationPath);
	bool RenderAnimationPathField(const char* Label, FString& Path);
	bool RenderSkeletalMeshPathField(const char* Label, FString& Path);
	void BeginPendingTransition(int32 FromStateId);
	bool CompletePendingTransition(int32 ToStateId);
	bool SelectOrCreateTransition(int32 FromStateId, int32 ToStateId);
	void OpenViewerForPath(const FString& Path);
	void DeleteSelected();
	void ClearStateMultiSelection();
	void SelectStateNode(int32 StateId, bool bAppendOrToggle);
	bool IsStateNodeSelected(int32 StateId) const;
	void ApplyMarqueeSelection(const ImVec2& RectMin, const ImVec2& RectMax, const ImVec2& GraphOrigin, bool bAppend);
	FLuaAnimStateNode* FindSelectedState();
	FLuaAnimTransitionLink* FindSelectedTransition();
	FString GetFileNameFromPath(const FString& Path) const;

private:
	FString AssetPath;
	FLuaAnimGraph Graph;
	FLuaAnimGraph FrameEditBaseline;
	std::vector<FLuaAnimGraph> UndoStack;
	std::vector<FLuaAnimGraph> RedoStack;
	FString GeneratedLuaSource;
	FString LastError;

	int32 SelectedStateId = 0;
	std::unordered_set<int32> SelectedStateIds;
	int32 SelectedTransitionId = 0;
	int32 PendingTransitionFromStateId = 0;
	ImVec2 CanvasPanOffset = ImVec2(0.0f, 0.0f);
	ImVec2 MarqueeStart = ImVec2(0.0f, 0.0f);
	ImVec2 MarqueeEnd = ImVec2(0.0f, 0.0f);
	float CanvasZoom = 1.0f;
	bool bUndoSnapshotCapturedThisFrame = false;
	bool bMarqueeSelecting = false;
	bool bMarqueeAppend = false;
	bool bOpen = false;
	bool bLoaded = false;
	bool bDirty = false;
};
