#pragma once

#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Math/Vector.h"

class URuntimeUILayoutAsset;
struct FRuntimeUIWidgetNode;

class FRuntimeUILayoutEditorWidget : public FAssetEditorWidget
{
public:
	FRuntimeUILayoutEditorWidget() = default;

	bool CanEdit(UObject* Object) const override;
	void Open(UObject* Object) override;
	void Render(float DeltaTime) override;
	FString GetDocumentTitle() const override;
	FString GetDocumentPayloadId() const override;
	EEditorDocumentTabKind GetDocumentTabKind() const override { return EEditorDocumentTabKind::RuntimeUILayoutEditor; }

private:
	URuntimeUILayoutAsset* GetLayout() const;
	void RenderToolbar(URuntimeUILayoutAsset* Layout);
	void RenderHierarchy(URuntimeUILayoutAsset* Layout);
	void RenderCanvasPreview(URuntimeUILayoutAsset* Layout);
	void RenderDetails(URuntimeUILayoutAsset* Layout);
	void RenderHierarchyNode(URuntimeUILayoutAsset* Layout, int32 WidgetIndex);
	void AddWidget(URuntimeUILayoutAsset* Layout, int32 TypeIndex);
	bool SaveAndExport(URuntimeUILayoutAsset* Layout, bool bOpenGeneratedRml);
	bool ImportGeneratedRmlAndRcss(URuntimeUILayoutAsset* Layout, bool bConfirmStructuralChanges);
	void EnsureGeneratedPaths(URuntimeUILayoutAsset* Layout);
	void MarkLayoutDirty();
	void HandleUndoRedoShortcuts(URuntimeUILayoutAsset* Layout);
	void CaptureInitialUndoSnapshot(URuntimeUILayoutAsset* Layout);
	void CommitLayoutEdit(URuntimeUILayoutAsset* Layout);
	void UndoLayoutEdit(URuntimeUILayoutAsset* Layout);
	void RedoLayoutEdit(URuntimeUILayoutAsset* Layout);
	TArray<uint8> CaptureLayoutSnapshot(URuntimeUILayoutAsset* Layout) const;
	bool RestoreLayoutSnapshot(URuntimeUILayoutAsset* Layout, const TArray<uint8>& Snapshot);

private:
	int32 SelectedWidgetIndex = 0;
	int32 DraggingWidgetIndex = -1;
	FVector2 DragGrabOffset = FVector2(0.0f, 0.0f);
	float CanvasPreviewZoom = 1.0f;
	bool bDragMovedSinceCommit = false;
	bool bRestoringSnapshot = false;
	TArray<TArray<uint8>> UndoStack;
	TArray<TArray<uint8>> RedoStack;
	FString LastStatus;
	bool bLastOperationFailed = false;
	bool bPendingStructuralImportConfirmation = false;
	FString PendingStructuralImportSignature;
};
