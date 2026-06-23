#pragma once

#include "AnimGraph/LuaAnimGraph.h"
#include "ImGui/imgui.h"

class FEditorViewer;
class UDebugSkelMeshComponent;
class UEditorEngine;

class FLuaAnimGraphPreviewOverlayWidget
{
public:
    void Initialize(UEditorEngine* InEditorEngine);
    void Shutdown();

    void DrawOverlay(
        float DeltaTime,
        FLuaAnimGraph& Graph,
        int32 SelectedStateId,
        int32 SelectedTransitionId,
        ImVec2 CanvasScreenPos,
        ImVec2 CanvasSize,
        ImGuiID OwnerViewportId = 0);

private:
    bool EnsurePreviewViewer(const FString& PreviewSkeletalMeshPath);
    UDebugSkelMeshComponent* ResolvePreviewComponent() const;
    bool SyncStatePreview(FLuaAnimGraph& Graph, const FLuaAnimStateNode& State);
    bool SyncTransitionPreview(
        FLuaAnimGraph& Graph,
        const FLuaAnimTransitionLink& Transition,
        const FLuaAnimStateNode& FromState,
        const FLuaAnimStateNode& ToState);
    void ClearPreviewSelection();
    void DrawViewport(const ImVec2& Size);
    void DrawPreviewControls();

private:
    UEditorEngine* EditorEngine = nullptr;
    FEditorViewer* Viewer = nullptr;
    UDebugSkelMeshComponent* PreviewComponent = nullptr;

    int32 LastPreviewStateId = 0;
    int32 LastPreviewTransitionId = 0;
    bool bPlaying = true;
    FString LastError;
    FString LastPreviewSignature;
    FString LastPreviewMeshPath;
    FString PreviewViewerDocumentPath;
    FString CurrentStateName;
    FString CurrentFromStateName;
    FString CurrentToStateName;
    FString CurrentBlendModeLabel;
    float CurrentBlendTime = 0.0f;
};
