#pragma once

#include "Editor/UI/EditorWidget.h"
#include "UI/RuntimeUILayoutAsset.h"
#include "UI/RuntimeUITypes.h"
#include "ImGui/imgui.h"

#include <functional>

class FEditorRuntimeUIPreviewWidget : public FEditorWidget
{
public:
	void Initialize(UEditorEngine* InEditorEngine) override;
	void Render(float DeltaTime) override;
	void RenderEmbedded(float DeltaTime);
	void SetRmlRenderQueue(std::function<void(const FRuntimeUIRenderContext&)> InQueueCallback);
	bool OpenPreviewDocument(const FString& Path);
	bool OpenLayoutAsset(const FString& Path);
	FString GetPreviewDocumentPath() const;
	FString GetLayoutAssetPath() const;

private:
	enum class EDesignerDragMode : uint8
	{
		None,
		Move,
		ResizeBottomRight,
	};

	struct FLayoutUndoSnapshot
	{
		TArray<FRuntimeUIWidgetNode> Widgets;
		int32 SelectedWidgetIndex = 0;
		TArray<int32> SelectedWidgetIndices;
		FString CopiedWidgetId;
	};

	struct FDragStartWidget
	{
		int32 WidgetIndex = 0;
		FVector2 Position = FVector2(0.0f, 0.0f);
		FVector2 Size = FVector2(0.0f, 0.0f);
	};

	struct FDesignerGuideLine
	{
		bool bVertical = true;
		float Position = 0.0f;
		float Min = 0.0f;
		float Max = 0.0f;
	};

	void DrawContent(float DeltaTime);
	void DrawToolbar();
	void DrawPreviewSurface(float DeltaTime);
	void DrawDesignerHierarchy();
	void DrawDesignerDetails();
	void DrawButtonActionEditor(FRuntimeUIWidgetNode& Node);
	void DrawWidgetContextMenu(int32 WidgetIndex);
	void DrawDesignerGrid(ImDrawList* DrawList, const ImVec2& CanvasMin, const ImVec2& CanvasSize, float Scale) const;
	void DrawDesignerOverlay(ImDrawList* DrawList, const ImVec2& CanvasMin, float Scale);
	void DrawDocumentInfo() const;
	void DrawActionEvents();
	void DrawAuthoringGuidance() const;
	bool LoadPreviewDocument();
	void RefreshPreviewDocument();
	bool SaveLayoutAsset();
	bool ExportLayoutToPreview();
	bool SaveAndExportLayout();
	void SyncGeneratedPathsFromLayoutPath(bool bForce);
	void UpdateLayoutDirtyState();
	uint64 ComputeLayoutFingerprint() const;
	FLayoutUndoSnapshot MakeUndoSnapshot() const;
	void RestoreUndoSnapshot(const FLayoutUndoSnapshot& Snapshot);
	void ResetUndoHistory();
	void PushUndoSnapshot(const FLayoutUndoSnapshot& Snapshot);
	void CommitPendingUndoSnapshot(bool bForce);
	bool CanUndoLayoutEdit() const;
	bool CanRedoLayoutEdit() const;
	void UndoLayoutEdit();
	void RedoLayoutEdit();
	void HandleUndoRedoShortcuts();
	bool OpenRmlFileDialog(FString& OutPath) const;
	bool SetPreviewDocumentPath(const FString& Path);
	bool AcceptRmlDragDropTarget();
	bool AcceptLayoutDragDropTarget();
	void SelectWidgetAtCanvasPosition(const ImVec2& CanvasPosition, float Scale, bool bToggleSelection, bool bAddSelection);
	int32 HitTestWidgetAtCanvasPosition(const ImVec2& CanvasPosition, float Scale) const;
	void SelectSingleWidget(int32 WidgetIndex);
	void ToggleWidgetSelection(int32 WidgetIndex);
	void AddWidgetSelection(int32 WidgetIndex);
	void SelectAllWidgets();
	void ClearWidgetSelection();
	void NormalizeWidgetSelection();
	bool IsWidgetSelected(int32 WidgetIndex) const;
	bool HasMultiSelection() const;
	TArray<int32> GetEditableSelectedWidgets() const;
	bool GetSelectionBounds(FVector2& OutMin, FVector2& OutMax) const;
	void DeleteSelectedWidgets();
	void DuplicateSelectedWidgets();
	void MoveSelectedWidgets(const FVector2& Delta);
	void AlignSelectedWidgetsToSelection(char Axis, float Factor);
	void DistributeSelectedWidgets(bool bHorizontal);
	void WrapSelectedWidgetsInPanel();
	FVector2 SnapWidgetMovePosition(int32 WidgetIndex, const FVector2& DesiredPosition);
	FVector2 SnapSelectionMoveDelta(const FVector2& DesiredDelta);
	FVector2 GetDesignerCanvasSize() const;
	FVector2 GetWidgetAuthoringParentSize(int32 WidgetIndex) const;
	bool GetWidgetResolvedRect(int32 WidgetIndex, FVector2& OutPosition, FVector2& OutSize) const;
	FVector2 GetWidgetAbsolutePosition(int32 WidgetIndex) const;
	bool GetWidgetScreenRect(int32 WidgetIndex, const ImVec2& CanvasMin, float Scale, ImVec2& OutMin, ImVec2& OutMax) const;
	bool IsMouseOverResizeHandle(int32 WidgetIndex, const ImVec2& CanvasMin, float Scale) const;
	FVector2 SnapPosition(const FVector2& Value) const;
	FVector2 SnapSize(const FVector2& Value) const;
	TArray<FString> CollectKnownActionNames() const;

private:
	std::function<void(const FRuntimeUIRenderContext&)> QueueRmlRenderContext;
	URuntimeUILayoutAsset LayoutAsset;
	TArray<FString> PreviewActionEvents;
	char PreviewScreenIdBuffer[64] = "__RuntimeUIPreview";
	char PreviewDocumentPathBuffer[260] = "Asset/UI/Test/Test.rml";
	char LayoutAssetPathBuffer[260] = "Asset/UI/Layouts/NewRuntimeUI.uasset";
	char GeneratedRmlPathBuffer[260] = "Asset/UI/Generated/NewRuntimeUI.rml";
	char GeneratedRcssPathBuffer[260] = "Asset/UI/Generated/NewRuntimeUI.rcss";
	char HierarchySearchBuffer[128] = "";
	int32 ResolutionPresetIndex = 0;
	int32 CustomWidth = 1920;
	int32 CustomHeight = 1080;
	float PreviewZoom = 1.0f;
	int32 SelectedWidgetIndex = 0;
	FString CopiedWidgetId;
	float DesignerGridSize = 10.0f;
	bool bEnableInteraction = true;
	bool bShowGuidance = true;
	bool bPreviewDocumentLoaded = false;
	bool bLayoutDirty = false;
	bool bPreviewExportDirty = true;
	bool bDesignMode = false;
	bool bShowDesignerGrid = true;
	bool bSnapToGrid = true;
	bool bSmartGuides = true;
	bool bAddWidgetsAsChild = false;
	bool bDraggingWidget = false;
	EDesignerDragMode DesignerDragMode = EDesignerDragMode::None;
	ImVec2 DragStartMouse = ImVec2(0.0f, 0.0f);
	FVector2 DragStartPosition = FVector2(0.0f, 0.0f);
	FVector2 DragStartSize = FVector2(0.0f, 0.0f);
	TArray<int32> SelectedWidgetIndices;
	TArray<FDragStartWidget> DragStartWidgets;
	TArray<FDesignerGuideLine> ActiveGuideLines;
	uint64 SavedLayoutFingerprint = 0;
	uint64 ExportedLayoutFingerprint = 0;
	TArray<FLayoutUndoSnapshot> UndoStack;
	TArray<FLayoutUndoSnapshot> RedoStack;
	FLayoutUndoSnapshot LastUndoSnapshot;
	uint64 LastUndoFingerprint = 0;
	bool bUndoBaselineValid = false;
};
