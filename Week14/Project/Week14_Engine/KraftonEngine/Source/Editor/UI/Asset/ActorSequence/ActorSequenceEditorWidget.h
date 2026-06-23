#pragma once

#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Editor/Undo/EditorUndoSystem.h"
#include "Object/Ptr/WeakObjectPtr.h"

class UActorSequenceComponent;
struct FActorSequenceChannel;
struct FActorSequenceSection;
struct FFloatCurve;

class FActorSequenceEditorWidget : public FAssetEditorWidget
{
public:
	bool CanEdit(UObject* Object) const override;
	void Open(UObject* Object) override;
	void Close() override;
	void Tick(float DeltaTime) override;
	void Render(float DeltaTime) override;
	void RenderDocument(float DeltaTime) override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	bool AllowsMultipleInstances() const override { return true; }
	FString GetDocumentTitle() const override;
	FString GetDocumentPayloadId() const override;
	EEditorDocumentTabKind GetDocumentTabKind() const override { return EEditorDocumentTabKind::Unsupported; }

private:
	UActorSequenceComponent* ResolveSequenceComponent() const;
	void RenderToolbar(UActorSequenceComponent* SequenceComp);
	void RenderTimeline(UActorSequenceComponent* SequenceComp);
	void RenderTrackList(UActorSequenceComponent* SequenceComp);
	void RenderKeyTable(UActorSequenceComponent* SequenceComp);
	void RenderCurveCanvas(UActorSequenceComponent* SequenceComp, FActorSequenceSection& Section, FActorSequenceChannel& Channel);
	void FitCurveView(const FFloatCurve& Curve);
	void DrawAddTrackPopup(UActorSequenceComponent* SequenceComp);
	void ResetAddTrackState(UActorSequenceComponent* SequenceComp);
	void BeginSequenceDragUndo(UActorSequenceComponent* SequenceComp, const FString& Label);
	void EndSequenceDragUndo(UActorSequenceComponent* SequenceComp);
	void ClearSequenceDragUndo();

private:
	TWeakObjectPtr<UActorSequenceComponent> SequenceComponent = nullptr;
	TWeakObjectPtr<UObject> PendingTrackTarget = nullptr;
	FString DocumentPayloadId;
	FString PendingTrackPropertyName;
	int32 SelectedBindingIndex = -1;
	int32 SelectedTrackIndex = -1;
	int32 SelectedSectionIndex = -1;
	int32 SelectedChannelIndex = -1;
	int32 SelectedKeyIndex = -1;
	int32 PendingTrackChannelIndex = 0;
	int32 DraggingKeyBindingIndex = -1;
	int32 DraggingKeyTrackIndex = -1;
	int32 DraggingKeySectionIndex = -1;
	int32 DraggingKeyChannelIndex = -1;
	int32 DraggingKeyIndex = -1;
	int32 DraggingSectionBindingIndex = -1;
	int32 DraggingSectionTrackIndex = -1;
	int32 DraggingSectionSectionIndex = -1;
	int32 DraggingSectionChannelIndex = -1;
	int32 DraggingSectionEdge = 0; // 1: start, 2: end
	int32 DraggingPlaybackRangeEdge = 0; // 1: start, 2: end
	int32 CurveViewBindingIndex = -1;
	int32 CurveViewTrackIndex = -1;
	int32 CurveViewSectionIndex = -1;
	int32 CurveViewChannelIndex = -1;
	int32 DraggingCurveKeyIndex = -1;
	int32 DraggingCurveTangentHandle = 0; // 1: arrive, 2: leave
	float ViewStartTime = 0.0f;
	float ViewEndTime = 5.0f;
	float CurveViewMinTime = 0.0f;
	float CurveViewMaxTime = 1.0f;
	float CurveViewMinValue = -1.0f;
	float CurveViewMaxValue = 1.0f;
	float PendingTrackStartTime = 0.0f;
	float PendingTrackDuration = 1.0f;
	bool bDraggingTimelineKey = false;
	bool bDraggingTimelineSection = false;
	bool bDraggingPlaybackRange = false;
	bool bDraggingCurveKey = false;
	bool bSuppressTimelineScrubUntilMouseUp = false;
	char PendingTrackCurveAssetPath[260] = {};
	FString PendingSequenceDragUndoLabel;
	TArray<FEditorSerializedActorState> PendingSequenceDragUndoStates;
	bool bRenderingDocument = false;
};
