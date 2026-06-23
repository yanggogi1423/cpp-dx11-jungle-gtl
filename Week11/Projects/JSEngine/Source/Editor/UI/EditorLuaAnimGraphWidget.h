#pragma once

#include "AnimGraph/LuaAnimGraph.h"
#include "Editor/UI/AnimGraph/LuaAnimGraphPreviewOverlayWidget.h"
#include "Editor/UI/EditorWidget.h"
#include "ImGui/imgui_node_editor.h"

class FEditorLuaAnimGraphWidget : public FEditorWidget
{
public:
    void Initialize(UEditorEngine* InEditorEngine) override;
    void Shutdown();

    void Render(float DeltaTime) override;
    void RenderEmbedded(float DeltaTime);

    bool OpenAsset(const FString& InAssetPath);
    bool SaveAsset();
    void Close();
    bool IsOpen() const { return bOpen; }
    void SetOpen(bool bInOpen) { bOpen = bInOpen; }
    FString GetWindowName() const;
    bool ConsumeDockRequest();

    const FString& GetAssetPath() const { return AssetPath; }
    const FString& GetGeneratedLuaSource() const { return GeneratedLuaSource; }

private:
    struct FLuaAnimGraphUndoSnapshot
    {
        FLuaAnimGraph Graph;
        FString GeneratedLuaSource;
        int32 SelectedStateId = 0;
        int32 SelectedTransitionId = 0;
    };

    void DrawContent(float DeltaTime, bool bDetachedWindow = false);
    void DrawToolbar(bool bDetachedWindow);
    void HandleShortcuts();
    void DrawGraphCanvas(float DeltaTime);
    bool HandleGraphCreateDelete();
    void DrawTransitionDetailsOverlay(const ImVec2& CanvasScreenPos, const ImVec2& CanvasSize, ImGuiID OwnerViewportId = 0);
    void DrawLuaSourcePanel();

    void RegenerateLuaSource();
    void MarkGraphEdited();
    FLuaAnimGraphUndoSnapshot MakeUndoSnapshot() const;
    FString ComputeUndoFingerprint(const FLuaAnimGraphUndoSnapshot& Snapshot) const;
    void RestoreUndoSnapshot(const FLuaAnimGraphUndoSnapshot& Snapshot);
    void ResetUndoHistory();
    void CommitUndoSnapshot(bool bForce = false);
    bool CanUndoGraphEdit() const;
    bool CanRedoGraphEdit() const;
    void UndoGraphEdit();
    void RedoGraphEdit();
    bool LoadAssetPayload();
    void AssignDefaultGraphIfEmpty();
    void DrawInitialStateCombo();
    void FlowSelectedNodeTransitions();

private:
    void HandleCanvasAssetDrop(const ImVec2& CanvasScreenPos, const ImVec2& CanvasSize);
    bool TryCreateStateFromDroppedAnimSequence(const FString& AssetPath, const ImVec2& DropScreenPos);

private:
    FString AssetPath;
    FLuaAnimGraph Graph;
    FString GeneratedLuaSource;
    FString LastError;

    int32 SelectedStateId = 0;
    int32 SelectedTransitionId = 0;

    bool bDirty = false;
    bool bLoaded = false;
    bool bOpen = false;
    bool bDockRequested = false;
    bool bInitializedNodePositions = false;
    bool bRestoringUndoSnapshot = false;

    TArray<FLuaAnimGraphUndoSnapshot> UndoStack;
    TArray<FLuaAnimGraphUndoSnapshot> RedoStack;
    FLuaAnimGraphUndoSnapshot LastUndoSnapshot;
    FString LastUndoFingerprint;
    bool bUndoBaselineValid = false;

    ax::NodeEditor::EditorContext* NodeEditorContext = nullptr;
    FLuaAnimTransitionDetailsWidget TransitionDetailsWidget;
    FLuaAnimGraphPreviewOverlayWidget PreviewOverlayWidget;
};
