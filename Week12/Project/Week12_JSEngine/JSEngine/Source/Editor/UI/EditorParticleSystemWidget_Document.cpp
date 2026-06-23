// Handles particle system document lifecycle, saving, dirty state, undo, and redo.
#include "Editor/UI/EditorParticleSystemWidgetPrivate.h"

namespace
{
	bool AreNearlyEqual(float A, float B)
	{
		return std::fabs(A - B) < 0.0001f;
	}

	bool AreCurveKeysEqual(const FCurveKey& A, const FCurveKey& B)
	{
		return
			AreNearlyEqual(A.Time, B.Time) &&
			AreNearlyEqual(A.Value, B.Value) &&
			A.InterpMode == B.InterpMode &&
			A.TangentMode == B.TangentMode &&
			AreNearlyEqual(A.ArriveTangent, B.ArriveTangent) &&
			AreNearlyEqual(A.LeaveTangent, B.LeaveTangent);
	}

	bool AreFloatCurvesEqual(const FFloatCurve& A, const FFloatCurve& B)
	{
		if (A.Keys.size() != B.Keys.size())
		{
			return false;
		}
		for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(A.Keys.size()); ++KeyIndex)
		{
			if (!AreCurveKeysEqual(A.Keys[KeyIndex], B.Keys[KeyIndex]))
			{
				return false;
			}
		}
		return true;
	}

	bool AreCurveMapsEqual(const TMap<FString, FFloatCurve>& A, const TMap<FString, FFloatCurve>& B)
	{
		if (A.size() != B.size())
		{
			return false;
		}
		for (const auto& [Key, Curve] : A)
		{
			auto It = B.find(Key);
			if (It == B.end() || !AreFloatCurvesEqual(Curve, It->second))
			{
				return false;
			}
		}
		return true;
	}

	bool AreVectorMapsEqual(const TMap<FString, FVector>& A, const TMap<FString, FVector>& B)
	{
		if (A.size() != B.size())
		{
			return false;
		}
		for (const auto& [Key, Value] : A)
		{
			auto It = B.find(Key);
			if (It == B.end() ||
				!AreNearlyEqual(Value.X, It->second.X) ||
				!AreNearlyEqual(Value.Y, It->second.Y) ||
				!AreNearlyEqual(Value.Z, It->second.Z))
			{
				return false;
			}
		}
		return true;
	}

}

FEditorParticleSystemWidget::~FEditorParticleSystemWidget() = default;

void FEditorParticleSystemWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	CurveEditorWidget.Initialize(InEditorEngine);
}

void FEditorParticleSystemWidget::Shutdown()
{
	TArray<UParticleSystem*> AssetsToRelease;
	if (ParticleSystemAsset)
	{
		AssetsToRelease.push_back(ParticleSystemAsset);
	}
	for (const auto& [Path, State] : ParticleDocumentStates)
	{
		if (State.Asset && std::find(AssetsToRelease.begin(), AssetsToRelease.end(), State.Asset) == AssetsToRelease.end())
		{
			AssetsToRelease.push_back(State.Asset);
		}
	}

	ShutdownPreviewViewport();
	for (UParticleSystem* Asset : AssetsToRelease)
	{
		DestroyUncachedParticleSystem(Asset);
	}
	ParticleSystemAsset = nullptr;
	ParticleDocumentStates.clear();
	for (TComPtr<ID3D11ShaderResourceView>& Icon : CascadeToolbarIcons)
	{
		Icon.Reset();
	}
	bCascadeToolbarIconsLoadAttempted = false;
}

bool FEditorParticleSystemWidget::Save()
{
	if (!ParticleSystemAsset)
	{
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Warning("No particle system to save.");
		}
		return false;
	}

	if (DocumentPath.empty() || !IsParticleSystemAssetDocumentPath(DocumentPath))
	{
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Warning("Particle system has no asset path to save.");
		}
		return false;
	}

	SyncParticleDistributionRuntimeDataToAsset();
	ParticleSystemAsset->CacheEmitterModuleInfo();

	TArray<FString> Errors;
	if (!ParticleSystemAsset->Validate(&Errors))
	{
		if (EditorEngine)
		{
			const FString Message = Errors.empty() ? FString("Particle system validation failed.") : Errors.front();
			EditorEngine->GetNotificationService().Warning(Message);
		}
		return false;
	}

	if (!FResourceManager::Get().SaveParticleSystem(ParticleSystemAsset, DocumentPath))
	{
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Error("Failed to save particle system.");
		}
		return false;
	}

	bDirty = false;
	StoreCurrentDocumentState();
	if (EditorEngine)
	{
		EditorEngine->GetNotificationService().Info("Particle system saved.");
	}
	return true;
}

bool FEditorParticleSystemWidget::CanUndo() const
{
	return !UndoHistory.empty();
}

bool FEditorParticleSystemWidget::CanRedo() const
{
	return !RedoHistory.empty();
}

bool FEditorParticleSystemWidget::Undo()
{
	if (UndoHistory.empty())
	{
		return false;
	}

	FParticleEditorUndoEntry CurrentEntry;
	CurrentEntry.Label = "Current";
	CurrentEntry.Snapshot = CaptureParticleSnapshot();
	CurrentEntry.ParticleDistributionKinds = ParticleDistributionKinds;
	CurrentEntry.ParticleDistributionFloatMaxValues = ParticleDistributionFloatMaxValues;
	CurrentEntry.ParticleDistributionVectorMaxValues = ParticleDistributionVectorMaxValues;
	CurrentEntry.ParticleDistributionCurves = ParticleDistributionCurves;
	CurrentEntry.CurrentLOD = CurrentLOD;
	CurrentEntry.SelectedEmitterIndex = SelectedEmitterIndex;
	CurrentEntry.SelectedModuleIndex = SelectedModuleIndex;

	const FParticleEditorUndoEntry PreviousEntry = UndoHistory.back();
	UndoHistory.pop_back();
	PushUndoEntry(RedoHistory, CurrentEntry, false);

	bRestoringParticleSnapshot = true;
	const bool bRestored = RestoreParticleSnapshot(
		PreviousEntry.Snapshot,
		PreviousEntry.CurrentLOD,
		PreviousEntry.SelectedEmitterIndex,
		PreviousEntry.SelectedModuleIndex);
	if (bRestored)
	{
		ParticleDistributionKinds = PreviousEntry.ParticleDistributionKinds;
		ParticleDistributionFloatMaxValues = PreviousEntry.ParticleDistributionFloatMaxValues;
		ParticleDistributionVectorMaxValues = PreviousEntry.ParticleDistributionVectorMaxValues;
		ParticleDistributionCurves = PreviousEntry.ParticleDistributionCurves;
		bParticleCurveViewInitialized = false;
		ActiveParticleCurveChannelKey.clear();
		ActiveParticleCurveKeyIndex = -1;
		DragParticleCurveChannelKey.clear();
		DragParticleCurveKeyIndex = -1;
		bParticleCurveEditUndoCaptured = false;
	}
	bRestoringParticleSnapshot = false;

	if (bRestored && EditorEngine)
	{
		EditorEngine->GetNotificationService().Info("Undo: " + PreviousEntry.Label);
	}
	return bRestored;
}

bool FEditorParticleSystemWidget::Redo()
{
	if (RedoHistory.empty())
	{
		return false;
	}

	FParticleEditorUndoEntry CurrentEntry;
	CurrentEntry.Label = "Current";
	CurrentEntry.Snapshot = CaptureParticleSnapshot();
	CurrentEntry.ParticleDistributionKinds = ParticleDistributionKinds;
	CurrentEntry.ParticleDistributionFloatMaxValues = ParticleDistributionFloatMaxValues;
	CurrentEntry.ParticleDistributionVectorMaxValues = ParticleDistributionVectorMaxValues;
	CurrentEntry.ParticleDistributionCurves = ParticleDistributionCurves;
	CurrentEntry.CurrentLOD = CurrentLOD;
	CurrentEntry.SelectedEmitterIndex = SelectedEmitterIndex;
	CurrentEntry.SelectedModuleIndex = SelectedModuleIndex;

	const FParticleEditorUndoEntry NextEntry = RedoHistory.back();
	RedoHistory.pop_back();
	PushUndoEntry(UndoHistory, CurrentEntry, false);

	bRestoringParticleSnapshot = true;
	const bool bRestored = RestoreParticleSnapshot(
		NextEntry.Snapshot,
		NextEntry.CurrentLOD,
		NextEntry.SelectedEmitterIndex,
		NextEntry.SelectedModuleIndex);
	if (bRestored)
	{
		ParticleDistributionKinds = NextEntry.ParticleDistributionKinds;
		ParticleDistributionFloatMaxValues = NextEntry.ParticleDistributionFloatMaxValues;
		ParticleDistributionVectorMaxValues = NextEntry.ParticleDistributionVectorMaxValues;
		ParticleDistributionCurves = NextEntry.ParticleDistributionCurves;
		bParticleCurveViewInitialized = false;
		ActiveParticleCurveChannelKey.clear();
		ActiveParticleCurveKeyIndex = -1;
		DragParticleCurveChannelKey.clear();
		DragParticleCurveKeyIndex = -1;
		bParticleCurveEditUndoCaptured = false;
	}
	bRestoringParticleSnapshot = false;

	if (bRestored && EditorEngine)
	{
		EditorEngine->GetNotificationService().Info("Redo: " + NextEntry.Label);
	}
	return bRestored;
}

void FEditorParticleSystemWidget::CloseDocument(const FString& InDocumentPath)
{
	UParticleSystem* AssetToRelease = nullptr;
	auto StateIt = ParticleDocumentStates.find(InDocumentPath);
	if (StateIt != ParticleDocumentStates.end())
	{
		AssetToRelease = StateIt->second.Asset;
		ParticleDocumentStates.erase(StateIt);
	}

	if (DocumentPath == InDocumentPath)
	{
		if (!AssetToRelease)
		{
			AssetToRelease = ParticleSystemAsset;
		}
		ClearActiveDocumentState();
		DocumentPath.clear();
	}
	DestroyUncachedParticleSystem(AssetToRelease);
}

void FEditorParticleSystemWidget::OpenParticleSystem(const FString& InDocumentPath)
{
	if (InDocumentPath.empty())
	{
		return;
	}
	if (DocumentPath == InDocumentPath && ParticleSystemAsset)
	{
		return;
	}

	StoreCurrentDocumentState();
	DocumentPath = InDocumentPath;
	ClearActiveDocumentState();
	bPropertyEditUndoCaptured = false;
	bEmitterNameEditUndoCaptured = false;
	bParticleCurveEditUndoCaptured = false;

	if (!RestoreDocumentState(InDocumentPath))
	{
		ParticleSystemAsset = FResourceManager::Get().LoadParticleSystem(InDocumentPath);
		bDirty = false;
		SelectEmitter(0);
		ClearEmitterContext();
		ClearUndoHistory();
	}
	if (ParticleSystemAsset)
	{
		ParticleSystemAsset->SetAssetPath(InDocumentPath);
	}

	EnsurePreviewViewport();
	RefreshPreviewComponent(true);
}

void FEditorParticleSystemWidget::StoreCurrentDocumentState()
{
	if (DocumentPath.empty() || !ParticleSystemAsset)
	{
		return;
	}

	FParticleSystemDocumentState& State = ParticleDocumentStates[DocumentPath];
	State.Asset = ParticleSystemAsset;
	State.bDirty = bDirty;
	State.CurrentLOD = CurrentLOD;
	State.SelectedEmitterIndex = SelectedEmitterIndex;
	State.SelectedModuleIndex = SelectedModuleIndex;
	State.UndoHistory = UndoHistory;
	State.RedoHistory = RedoHistory;
	State.ParticleDistributionKinds = ParticleDistributionKinds;
	State.ParticleDistributionFloatMaxValues = ParticleDistributionFloatMaxValues;
	State.ParticleDistributionVectorMaxValues = ParticleDistributionVectorMaxValues;
	State.ParticleDistributionCurves = ParticleDistributionCurves;
	State.SoloEmitterIndices = SoloEmitterIndices;
	State.PreviewViewMode = bPreviewViewportInitialized
		? PreviewViewport.GetState().ViewMode
		: EViewMode::Lit_BlinnPhong;
	State.PreviewShowFlags = bPreviewViewportInitialized
		? PreviewClient.GetParticleShowFlags()
		: FParticleSystemViewportShowFlags{};
	State.PreviewShowFlags.bBounds = bShowBounds;
	State.PreviewShowFlags.bAxis = bShowOriginAxis;
	State.PreviewBackgroundColor = bPreviewViewportInitialized
		? PreviewClient.GetBackgroundColor()
		: FParticleSystemViewportClient::GetDefaultBackgroundColor();
	State.bShowThumbnail = bShowThumbnail;
	State.bShowBounds = bShowBounds;
	State.bShowOriginAxis = bShowOriginAxis;
	State.bPreviewPaused = bPreviewPaused;
	State.bPreviewLoop = bPreviewLoop;
	State.bPreviewPlaybackComplete = bPreviewPlaybackComplete;
	State.PreviewAnimSpeedIndex = PreviewAnimSpeedIndex;
	State.PreviewPlaybackElapsed = PreviewPlaybackElapsed;
}

bool FEditorParticleSystemWidget::RestoreDocumentState(const FString& InDocumentPath)
{
	auto It = ParticleDocumentStates.find(InDocumentPath);
	if (It == ParticleDocumentStates.end() || !It->second.Asset)
	{
		return false;
	}

	const FParticleSystemDocumentState& State = It->second;
	ParticleSystemAsset = State.Asset;
	bDirty = State.bDirty;
	CurrentLOD = State.CurrentLOD;
	SelectedEmitterIndex = State.SelectedEmitterIndex;
	SelectedModuleIndex = State.SelectedModuleIndex;
	UndoHistory = State.UndoHistory;
	RedoHistory = State.RedoHistory;
	ParticleDistributionKinds = State.ParticleDistributionKinds;
	ParticleDistributionFloatMaxValues = State.ParticleDistributionFloatMaxValues;
	ParticleDistributionVectorMaxValues = State.ParticleDistributionVectorMaxValues;
	ParticleDistributionCurves = State.ParticleDistributionCurves;
	SoloEmitterIndices = State.SoloEmitterIndices;
	bShowThumbnail = State.bShowThumbnail;
	bShowBounds = State.bShowBounds;
	bShowOriginAxis = State.bShowOriginAxis;
	bPreviewPaused = State.bPreviewPaused;
	bPreviewLoop = State.bPreviewLoop;
	bPreviewPlaybackComplete = State.bPreviewPlaybackComplete;
	PreviewAnimSpeedIndex = State.PreviewAnimSpeedIndex;
	PreviewPlaybackElapsed = State.PreviewPlaybackElapsed;
	if (bPreviewViewportInitialized)
	{
		PreviewViewport.GetState().ViewMode = State.PreviewViewMode;
		PreviewClient.GetParticleShowFlags() = State.PreviewShowFlags;
		PreviewClient.SetBackgroundColor(State.PreviewBackgroundColor);
	}
	ClearEmitterContext();
	ResetPendingReorders();
	ClampSelectionToParticleSystem();
	ClearInvalidSoloEmitters();
	ApplyPreviewSoloEmitters();
	return true;
}

void FEditorParticleSystemWidget::ClearActiveDocumentState()
{
	ParticleSystemAsset = nullptr;
	bDirty = false;
	CurrentLOD = 0;
	SelectedEmitterIndex = 0;
	SelectedModuleIndex = NoParticleModuleSelection;
	SelectedCurveAssetPath.clear();
	ParticleDistributionKinds.clear();
	ParticleDistributionFloatMaxValues.clear();
	ParticleDistributionVectorMaxValues.clear();
	ParticleDistributionCurves.clear();
	SoloEmitterIndices.clear();
	ActiveParticleCurveModuleKey.clear();
	ActiveParticleCurveChannelKey.clear();
	ParticleCurveViewModuleKey.clear();
	ParticleCurveViewMinTime = 0.0f;
	ParticleCurveViewMaxTime = 1.0f;
	bParticleCurveViewInitialized = false;
	bParticleCurveViewUserAdjusted = false;
	ActiveParticleCurveEmitterIndex = -1;
	ActiveParticleCurveModuleIndex = -1;
	ActiveParticleCurveKeyIndex = -1;
	DragParticleCurveKeyIndex = -1;
	DragParticleCurveChannelKey.clear();
	ClearEmitterContext();
	ClearUndoHistory();
	ResetPendingReorders();
	RestartPreviewPlayback();
	bShowThumbnail = false;
	bShowBounds = false;
	bShowOriginAxis = true;
	bPreviewPaused = false;
	bPreviewLoop = true;
	PreviewAnimSpeedIndex = 0;
	if (bPreviewViewportInitialized)
	{
		PreviewViewport.GetState().ViewMode = EViewMode::Lit_BlinnPhong;
		PreviewClient.GetParticleShowFlags() = FParticleSystemViewportShowFlags{};
		PreviewClient.ResetBackgroundColor();
	}
	bPropertyEditUndoCaptured = false;
	bEmitterNameEditUndoCaptured = false;
	bParticleCurveEditUndoCaptured = false;
}

void FEditorParticleSystemWidget::DestroyUncachedParticleSystem(UParticleSystem*& Asset)
{
	if (!Asset)
	{
		return;
	}

	const FString AssetPath = Asset->GetAssetPath();
	if (!AssetPath.empty() && FResourceManager::Get().FindParticleSystem(AssetPath) == Asset)
	{
		Asset = nullptr;
		return;
	}

	UObjectManager::Get().DestroyObject(Asset);
	Asset = nullptr;
}

void FEditorParticleSystemWidget::CaptureUndoSnapshot(const char* Label)
{
	if (bRestoringParticleSnapshot)
	{
		return;
	}

	FParticleEditorUndoEntry Entry;
	Entry.Label = (Label && Label[0] != '\0') ? Label : "Edit Particle System";
	Entry.Snapshot = CaptureParticleSnapshot();
	Entry.ParticleDistributionKinds = ParticleDistributionKinds;
	Entry.ParticleDistributionFloatMaxValues = ParticleDistributionFloatMaxValues;
	Entry.ParticleDistributionVectorMaxValues = ParticleDistributionVectorMaxValues;
	Entry.ParticleDistributionCurves = ParticleDistributionCurves;
	Entry.CurrentLOD = CurrentLOD;
	Entry.SelectedEmitterIndex = SelectedEmitterIndex;
	Entry.SelectedModuleIndex = SelectedModuleIndex;
	PushUndoEntry(UndoHistory, Entry, true);
	RedoHistory.clear();
}

FString FEditorParticleSystemWidget::CaptureParticleSnapshot() const
{
	return ParticleSystemAsset
		? FResourceManager::Get().SerializeParticleSystemToString(ParticleSystemAsset)
		: FString();
}

bool FEditorParticleSystemWidget::RestoreParticleSnapshot(
	const FString& Snapshot,
	int32 InCurrentLOD,
	int32 InSelectedEmitterIndex,
	int32 InSelectedModuleIndex)
{
	UParticleSystem* PreviousAsset = ParticleSystemAsset;
	UParticleSystem* RestoredAsset = nullptr;
	if (!Snapshot.empty())
	{
		RestoredAsset = FResourceManager::Get().LoadParticleSystemFromString(Snapshot);
		if (!RestoredAsset)
		{
			return false;
		}
	}

	ParticleSystemAsset = RestoredAsset;
	if (ParticleSystemAsset && !DocumentPath.empty())
	{
		ParticleSystemAsset->SetAssetPath(DocumentPath);
	}
	DestroyUncachedParticleSystem(PreviousAsset);
	CurrentLOD = std::max(0, InCurrentLOD);
	SelectedEmitterIndex = InSelectedEmitterIndex;
	SelectedModuleIndex = InSelectedModuleIndex;
	ClearEmitterContext();
	RenameEmitterIndex = -1;
	bOpenEmitterContextMenu = false;
	bOpenRenameEmitterPopup = false;
	bPropertyEditUndoCaptured = false;
	bEmitterNameEditUndoCaptured = false;
	bParticleCurveEditUndoCaptured = false;

	if (ParticleSystemAsset)
	{
		ParticleSystemAsset->CacheEmitterModuleInfo();
	}
	ClampSelectionToParticleSystem();
	bDirty = true;
	RefreshPreviewComponent(true);
	return true;
}

void FEditorParticleSystemWidget::ClearUndoHistory()
{
	UndoHistory.clear();
	RedoHistory.clear();
}

void FEditorParticleSystemWidget::PushUndoEntry(
	TArray<FParticleEditorUndoEntry>& Stack,
	const FParticleEditorUndoEntry& Entry,
	bool bSkipDuplicate)
{
	if (bSkipDuplicate &&
		!Stack.empty() &&
		Stack.back().Snapshot == Entry.Snapshot &&
		Stack.back().ParticleDistributionKinds == Entry.ParticleDistributionKinds &&
		Stack.back().ParticleDistributionFloatMaxValues == Entry.ParticleDistributionFloatMaxValues &&
		AreVectorMapsEqual(Stack.back().ParticleDistributionVectorMaxValues, Entry.ParticleDistributionVectorMaxValues) &&
		AreCurveMapsEqual(Stack.back().ParticleDistributionCurves, Entry.ParticleDistributionCurves))
	{
		return;
	}

	constexpr int32 MaxParticleUndoHistory = 50;
	Stack.push_back(Entry);
	if (static_cast<int32>(Stack.size()) > MaxParticleUndoHistory)
	{
		Stack.erase(Stack.begin());
	}
}
