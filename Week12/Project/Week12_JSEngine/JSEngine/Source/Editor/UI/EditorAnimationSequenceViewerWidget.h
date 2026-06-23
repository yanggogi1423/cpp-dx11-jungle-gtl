#pragma once

#include "Asset/SkeletalMeshTypes.h"
#include "Editor/UI/EditorCurveEditorWidget.h"
#include "Editor/UI/EditorWidget.h"

class FEditorViewer;
class USkeletalMeshComponent;
class UDebugSkelMeshComponent;
class UAnimSequence;
class UAnimPreviewInstance;
struct FAnimSequenceViewerContext;
struct FViewportInputContext;

class FEditorAnimationSequenceViewerWidget : public FEditorWidget
{
public:
	void Initialize(UEditorEngine* InEditorEngine) override;
	void Render(float DeltaTime) override;
	void RenderEmbedded(float DeltaTime);

	void SetViewer(FEditorViewer* InViewer);
	FEditorViewer* GetViewer() const { return Viewer; }
	void SetContext(const FAnimSequenceViewerContext& InContext);
	const FString& GetAssetPath() const { return AssetPath; }
	UAnimSequence* GetAnimSequence() const { return AnimSequence; }

	bool IsOpen() const { return bOpen; }
	void SetOpen(bool bInOpen) { bOpen = bInOpen; }

	FString GetWindowName() const;

private:
	void RenderDetachedDocumentChrome(bool& bDockRequested, bool& bCloseRequested);
	void RenderDetachedDocumentToolbar(bool& bDockRequested);
	void RenderContent(float DeltaTime);
	void RenderSkeletonPanel(USkeletalMeshComponent* SkelComp);
	void RenderViewportPanel(float DeltaTime);
	void RenderTimelinePanel(UDebugSkelMeshComponent* SkelComp, float DeltaTime);
	void RenderTimelineToolbar(UDebugSkelMeshComponent* SkelComp, float PlayLength, float FramesPerSecond, int32 LastFrame);
	void RenderTimelineCanvas(UDebugSkelMeshComponent* SkelComp, float PlayLength, float FramesPerSecond, int32 LastFrame);
	void ProcessKeyboardShortcuts(UDebugSkelMeshComponent* SkelComp, float PlayLength, float FramesPerSecond, int32 LastFrame);
	bool HandleViewportShortcutInput(const FViewportInputContext& Context);
	bool HasKeyboardFocusForShortcuts() const;
	bool IsAssetAvailable() const;
	bool CanPreviewPlayback(UDebugSkelMeshComponent* SkelComp) const;
	void TogglePreviewPlayback(UDebugSkelMeshComponent* SkelComp);
	bool DeleteSelectedTimelineItem();
	void RenderDetailsPanel(USkeletalMeshComponent* SkelComp);
	void RenderNotifyContextMenu(float FramesPerSecond, float PlayLength, int32 LastFrame);
	void RenderCurveContextMenu(float PlayLength);
	void OpenCurveTrackEditor(int32 CurveTrackIndex);
	void DrawBoneNode(int32 BoneIndex, const TArray<FBoneInfo>& Bones, const TArray<TArray<int32>>& Children);
	void RebuildBoneTreeCaches(const FSkeletalMesh* MeshData);
	FSkeletalMesh* ResolveCurrentMeshData(UDebugSkelMeshComponent** OutSkelComp = nullptr) const;
	float ResolvePlayLength() const;
	UAnimPreviewInstance* EnsurePreviewInstance(UDebugSkelMeshComponent* SkelComp);
	void ReleasePreviewInstance();
	bool SyncPreviewInstance(UDebugSkelMeshComponent* SkelComp);
	void SetPreviewFrame(UDebugSkelMeshComponent* SkelComp, int32 Frame, float FramesPerSecond, float PlayLength, int32 LastFrame);
	void StepPreviewFrame(UDebugSkelMeshComponent* SkelComp, int32 FrameDelta, float FramesPerSecond, float PlayLength, int32 LastFrame);
	bool SeekPreviewTime(UDebugSkelMeshComponent* SkelComp, float TimeSeconds);
	bool SaveAnimationAsset();

private:
	FEditorCurveEditorWidget CurveEditorWidget;
	FEditorViewer* Viewer = nullptr;
	FString AssetPath;
	FString TargetSkeletalMeshPath;
	UAnimSequence* AnimSequence = nullptr;
	UDebugSkelMeshComponent* PreviewInstanceComponent = nullptr;
	bool bOpen = false;

	TArray<TArray<int32>> Children;
	FSkeletalMesh* CachedMesh = nullptr;

	float LeftPanelWidth = 250.0f;
	float RightPanelWidth = 280.0f;
	float TimelineHeight = 220.0f;
	float TimelineLeftPanelWidth = 300.0f;
	float ViewStartFrame = 0.0f;
	float ViewEndFrame = 30.0f;
	float TrackScrollY = 0.0f;
	int32 LastTimelineRowCount = 0;
	bool bTimelineViewInitialized = false;

	float PreviewTime = 0.0f;
	float PreviewPlayRate = 1.0f;
	bool bPreviewPlaying = false;
	bool bPreviewLooping = true;
	bool bPreviewReverse = false;
	bool bDraggingPlayhead = false;
	bool bDraggingNotify = false;
	bool bDraggingNotifyDirty = false;
	int32 DraggingNotifyTrackIndex = -1;
	int32 DraggingNotifyIndex = -1;
	int32 DraggingNotifyMode = 0;
	int32 DraggingNotifyGrabFrameOffset = 0;
	int32 SelectedNotifyTrackIndex = -1;
	int32 SelectedNotifyIndex = -1;
	char SelectedNotifyNameBuffer[128] = {};
	int32 SelectedNotifyNameBufferTrackIndex = -1;
	int32 SelectedNotifyNameBufferNotifyIndex = -1;
	char SelectedNotifyLuaEventNameBuffer[128] = {};
	char SelectedNotifyLuaTargetScriptBuffer[256] = {};
	int32 SelectedNotifyLuaBufferTrackIndex = -1;
	int32 SelectedNotifyLuaBufferNotifyIndex = -1;
	int32 SelectedTrackIndex = -1;
	int32 ContextNotifyTrackIndex = -1;
	int32 ContextNotifyIndex = -1;
	int32 ContextNotifyFrame = 0;
	int32 SelectedCurveTrackIndex = -1;
	int32 ContextCurveTrackIndex = -1;
};
