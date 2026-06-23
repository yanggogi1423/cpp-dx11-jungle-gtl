#pragma once

#include "Editor/UI/EditorCurveEditorWidget.h"
#include "Editor/UI/EditorWidget.h"
#include "Editor/Viewport/FSceneViewport.h"
#include "Editor/Viewport/ParticleSystemViewportClient.h"
#include "Asset/CurveFloatAsset.h"
#include "Particle/ParticleTypes.h"
#include "Render/Common/ComPtr.h"
#include "ImGui/imgui.h"

class UParticleEmitter;
class UParticleModule;
class UParticleLODLevel;
class UParticleRendererProperties;
class UParticleSystemComponent;
class UParticleSystem;
class UObject;
class AActor;
struct FProperty;
struct ID3D11ShaderResourceView;

class FEditorParticleSystemWidget : public FEditorWidget
{
public:
	~FEditorParticleSystemWidget() override;

	void Initialize(UEditorEngine* InEditorEngine) override;
	void Render(float DeltaTime) override;
	void RenderEmbedded(float DeltaTime);
	void RenderDetachedDocumentChrome(bool& bDockRequested, bool& bCloseRequested);
	void RenderDocumentToolbarControls();
	void RenderDocumentViewMenu();
	void RenderDocumentParticleMenu();
	void Shutdown();
	bool Save();
	bool CanUndo() const;
	bool CanRedo() const;
	bool Undo();
	bool Redo();
	void CloseDocument(const FString& InDocumentPath);

	void OpenParticleSystem(const FString& InDocumentPath);
	const FString& GetDocumentPath() const { return DocumentPath; }
	bool IsDirty() const { return bDirty; }
	bool IsPreviewViewportVisible() const { return bPreviewViewportVisible; }
	bool HasValidPreviewViewportRect() const { return bPreviewViewportRectValid; }
	FSceneViewport* GetPreviewViewport() { return bPreviewViewportInitialized ? &PreviewViewport : nullptr; }
	const FSceneViewport* GetPreviewViewport() const { return bPreviewViewportInitialized ? &PreviewViewport : nullptr; }
	FParticleSystemViewportClient* GetPreviewClient() { return bPreviewViewportInitialized ? &PreviewClient : nullptr; }
	const FParticleSystemViewportClient* GetPreviewClient() const { return bPreviewViewportInitialized ? &PreviewClient : nullptr; }

private:
	enum class ECascadeToolbarIcon : int32
	{
		Save,
		Find,
		RestartSim,
		RestartLevel,
		Undo,
		Redo,
		Thumbnail,
		Bounds,
		OriginAxis,
		BackgroundColor,
		RegenLOD,
		LowestLOD,
		LowerLOD,
		AddLODBeforeCurrent,
		AddLODAfterCurrent,
		HigherLOD,
		HighestLOD,
		DeleteLOD,
		Count
	};

	static constexpr int32 CascadeToolbarIconCount = static_cast<int32>(ECascadeToolbarIcon::Count);

	struct FParticleEditorUndoEntry
	{
		FString Label;
		FString Snapshot;
		TMap<FString, int32> ParticleDistributionKinds;
		TMap<FString, float> ParticleDistributionFloatMaxValues;
		TMap<FString, FVector> ParticleDistributionVectorMaxValues;
		TMap<FString, FFloatCurve> ParticleDistributionCurves;
		int32 CurrentLOD = 0;
		int32 SelectedEmitterIndex = 0;
		int32 SelectedModuleIndex = -1;
	};

	struct FParticleSystemDocumentState
	{
		UParticleSystem* Asset = nullptr;
		bool bDirty = false;
		int32 CurrentLOD = 0;
		int32 SelectedEmitterIndex = 0;
		int32 SelectedModuleIndex = -1;
		TArray<FParticleEditorUndoEntry> UndoHistory;
		TArray<FParticleEditorUndoEntry> RedoHistory;
		TMap<FString, int32> ParticleDistributionKinds;
		TMap<FString, float> ParticleDistributionFloatMaxValues;
		TMap<FString, FVector> ParticleDistributionVectorMaxValues;
		TMap<FString, FFloatCurve> ParticleDistributionCurves;
		TArray<int32> SoloEmitterIndices;
		EViewMode PreviewViewMode = EViewMode::Lit_BlinnPhong;
		FParticleSystemViewportShowFlags PreviewShowFlags;
		FColor PreviewBackgroundColor = FParticleSystemViewportClient::GetDefaultBackgroundColor();
		bool bShowThumbnail = false;
		bool bShowBounds = false;
		bool bShowOriginAxis = true;
		bool bPreviewPaused = false;
		bool bPreviewLoop = true;
		bool bPreviewPlaybackComplete = false;
		int32 PreviewAnimSpeedIndex = 0;
		float PreviewPlaybackElapsed = 0.0f;
	};

	void EnsurePreviewViewport();
	void EnsurePreviewActor();
	void RefreshPreviewComponent(bool bRestartSimulation);
	void RefreshPlacedParticleSystemComponents(bool bRestartSimulation);
	void SyncPreviewWorld();
	void SetPreviewBoundsVisible(bool bVisible);
	void SetPreviewOriginAxisVisible(bool bVisible);
	float GetPreviewAnimSpeed() const;
	float GetPreviewMaxEmitterDuration() const;
	void RestartPreviewPlayback();
	void DrivePreviewPlayback(float DeltaTime);
	void ShutdownPreviewViewport();
	void LoadCascadeToolbarIcons();
	ID3D11ShaderResourceView* GetCascadeToolbarIcon(ECascadeToolbarIcon Icon) const;
	void DrawMainLayout();
	void DrawViewportPanel(const ImVec2& Size);
	void DrawViewportMenuBar(const ImVec2& CanvasMin);
	void DrawBackgroundColorPopup();
	void DrawEmittersPanel(const ImVec2& Size);
	void DrawEmitterContextMenu();
	void AddDefaultEmitter();
	void AddDefaultEmitterAt(int32 InsertIndex);
	void AddLODToSelectedEmitter();
	void AddLODToSelectedEmitterAt(int32 InsertIndex);
	void SelectLowerLOD();
	void SelectHigherLOD();
	void SelectLowestLOD();
	void DeleteSelectedEmitter();
	void DeleteEmitter(int32 EmitterIndex);
	void DuplicateEmitter(int32 EmitterIndex);
	void AddModuleToEmitter(int32 EmitterIndex, UParticleModule* Module);
	void DeleteModule(int32 EmitterIndex, int32 ModuleIndex);
	void AddLODRelativeToCurrent(int32 Offset);
	void DeleteCurrentLOD();
	void SetCurrentLOD(int32 NewLOD);
	int32 GetMaxLODCount() const;
	void SyncParticleSystemLODPropertiesFromEmitters();
	void ApplyParticleSystemLODPropertiesToEmitters();
	void DuplicateModuleFromHigherLOD(int32 EmitterIndex, int32 ModuleIndex, bool bHighest);
	void SyncInheritedModuleFromHigherLOD(UParticleEmitter* OwnerEmitter, UParticleModule* SourceModule);
	void ChangeEmitterRenderMode(int32 EmitterIndex, EParticleEmitterRenderMode RenderMode);
	void BeginRenameEmitter(int32 EmitterIndex);
	void RenameEmitter(int32 EmitterIndex, const FString& NewName);
	bool ApplyEmitterName(int32 EmitterIndex, const FString& NewName, bool bCaptureUndo, bool bWarnOnEmpty);
	void DrawEmitterRenamePopup();
	void SelectParticleSystem();
	void SelectEmitter(int32 EmitterIndex);
	void SelectModule(int32 EmitterIndex, int32 ModuleIndex);
	bool IsEmitterSolo(int32 EmitterIndex) const;
	bool HasSoloEmitters() const;
	void ToggleEmitterSolo(int32 EmitterIndex);
	void ClearInvalidSoloEmitters();
	void ApplyPreviewSoloEmitters();
	void OpenEmitterContextMenu(int32 EmitterIndex, int32 ModuleIndex);
	void ClearEmitterContext();
	void ShowCenterToast(const FString& Message);
	void DrawCenterToast(const ImVec2& AreaMin, const ImVec2& AreaSize);
	void StoreCurrentDocumentState();
	bool RestoreDocumentState(const FString& InDocumentPath);
	void ClearActiveDocumentState();
	void DestroyUncachedParticleSystem(UParticleSystem*& Asset);
	void CaptureUndoSnapshot(const char* Label);
	FString CaptureParticleSnapshot() const;
	bool RestoreParticleSnapshot(const FString& Snapshot, int32 InCurrentLOD, int32 InSelectedEmitterIndex, int32 InSelectedModuleIndex);
	void ClearUndoHistory();
	void PushUndoEntry(TArray<FParticleEditorUndoEntry>& Stack, const FParticleEditorUndoEntry& Entry, bool bSkipDuplicate);
	void ClampSelectionToParticleSystem();
	void ResetPendingReorders();
	void ApplyPendingReorders();
	void ReorderEmitter(int32 SourceIndex, int32 InsertIndex);
	void ReorderModule(int32 SourceEmitterIndex, int32 SourceModuleIndex, int32 TargetEmitterIndex, int32 InsertIndex);
	void DrawEmitterColumn(UParticleEmitter* Emitter, int32 EmitterIndex, float ColumnHeight);
	void DrawEmitterRendererRow(UParticleLODLevel* LODLevel, int32 EmitterIndex, float RowHeight);
	void DrawEmitterModuleRow(UParticleModule* Module, int32 EmitterIndex, int32 ModuleIndex, bool bRequired, float RowHeight);
	void DrawDetailsPanel(const ImVec2& Size);
	UParticleLODLevel* GetEmitterLODLevel(UParticleEmitter* Emitter) const;
	UParticleLODLevel* GetSelectedLODLevel() const;
	UParticleModule* GetSelectedModule() const;
	UParticleEmitter* GetSelectedEmitter() const;
	void DrawEmitterDetails(UParticleEmitter* Emitter, int32 EmitterIndex);
	void DrawRendererPropertiesDetails(UParticleRendererProperties* RendererProperties);
	void DrawParticleSystemDetails(UParticleSystem* ParticleSystem);
	void DrawParticleModuleDetails(UParticleModule* Module, UParticleEmitter* OwnerEmitter);
	bool DrawParticleObjectProperty(UObject* Object, const FProperty& Property);
	bool DrawParticleModuleProperty(UParticleModule* Module, const FProperty& Property);
	bool DrawParticlePropertyValue(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget, const char* Label);
	bool DrawParticleStructPropertyValue(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget, const char* Label);
	bool IsParticleDistributionProperty(UParticleModule* Module, const FProperty& Property) const;
	bool IsEmitterTimeDistributionProperty(UParticleModule* Module, const FProperty& Property) const;
	FString MakeParticleDistributionKey(UParticleModule* Module, const FProperty& Property) const;
	FString MakeParticleDistributionCurveKey(UParticleModule* Module, const FProperty& Property, const char* ChannelName) const;
	FString MakeParticleModuleCurveKey(UParticleModule* Module) const;
	FFloatCurve& GetOrCreateParticleDistributionCurve(UParticleModule* Module, const FProperty& Property, const char* ChannelName, float InitialValue);
	void SyncParticleDistributionRuntimeDataToAsset();
	void OpenParticleModuleCurves(int32 EmitterIndex, int32 ModuleIndex);
	void NotifyParticleModulePropertyChanged(UParticleModule* Module, UParticleEmitter* OwnerEmitter, const FProperty& Property);
	void DrawCurveEditorPanel(const ImVec2& Size);

	FEditorCurveEditorWidget CurveEditorWidget;
	FSceneViewport PreviewViewport;
	FParticleSystemViewportClient PreviewClient;
	UParticleSystem* ParticleSystemAsset = nullptr;
	AActor* PreviewActor = nullptr;
	UParticleSystemComponent* PreviewComponent = nullptr;
	FName PreviewWorldHandle = FName::None;
	FString SelectedCurveAssetPath;
	FString DocumentPath;
	TMap<FString, FParticleSystemDocumentState> ParticleDocumentStates;
	TMap<FString, int32> ParticleDistributionKinds;
	TMap<FString, float> ParticleDistributionFloatMaxValues;
	TMap<FString, FVector> ParticleDistributionVectorMaxValues;
	TMap<FString, FFloatCurve> ParticleDistributionCurves;
	FString ActiveParticleCurveModuleKey;
	FString ActiveParticleCurveChannelKey;
	FString ParticleCurveViewModuleKey;
	float ParticleCurveViewMinTime = 0.0f;
	float ParticleCurveViewMaxTime = 1.0f;
	float ParticleCurveViewMinValue = -1.0f;
	float ParticleCurveViewMaxValue = 1.0f;
	bool bParticleCurveViewInitialized = false;
	bool bParticleCurveViewUserAdjusted = false;
	bool bDirty = true;
	bool bShowThumbnail = false;
	bool bShowBounds = false;
	bool bShowOriginAxis = true;
	bool bPreviewPaused = false;
	bool bPreviewLoop = true;
	bool bPreviewPlaybackComplete = false;
	bool bPreviewViewportInitialized = false;
	bool bPreviewViewportVisible = false;
	bool bPreviewViewportRectValid = false;
	int32 CurrentLOD = 0;
	int32 PreviewAnimSpeedIndex = 0;
	int32 SelectedEmitterIndex = 0;
	int32 SelectedModuleIndex = -1;
	int32 ActiveParticleCurveEmitterIndex = -1;
	int32 ActiveParticleCurveModuleIndex = -1;
	int32 ActiveParticleCurveKeyIndex = -1;
	int32 DragParticleCurveKeyIndex = -1;
	FString DragParticleCurveChannelKey;
	int32 ContextEmitterIndex = -1;
	int32 ContextModuleIndex = -1;
	int32 RenameEmitterIndex = -1;
	int32 DetailEmitterNameEditIndex = -1;
	int32 PendingEmitterMoveSource = -1;
	int32 PendingEmitterMoveInsertIndex = -1;
	int32 PendingModuleMoveEmitterIndex = -1;
	int32 PendingModuleMoveTargetEmitterIndex = -1;
	int32 PendingModuleMoveSource = -1;
	int32 PendingModuleMoveInsertIndex = -1;
	TArray<int32> SoloEmitterIndices;
	char RenameEmitterBuffer[128] = {};
	char DetailEmitterNameEditBuffer[128] = {};
	FString CenterToastMessage;
	TArray<FParticleEditorUndoEntry> UndoHistory;
	TArray<FParticleEditorUndoEntry> RedoHistory;
	TComPtr<ID3D11ShaderResourceView> CascadeToolbarIcons[CascadeToolbarIconCount];
	bool bOpenEmitterContextMenu = false;
	bool bOpenRenameEmitterPopup = false;
	bool bRestoringParticleSnapshot = false;
	bool bPropertyEditUndoCaptured = false;
	bool bEmitterNameEditUndoCaptured = false;
	bool bParticleCurveEditUndoCaptured = false;
	bool bCascadeToolbarIconsLoadAttempted = false;
	float TopAreaHeight = 0.0f;
	float TopLeftWidth = 0.0f;
	float BottomLeftWidth = 0.0f;
	float LastDeltaTime = 0.0f;
	float PreviewPlaybackElapsed = 0.0f;
	float CenterToastRemainingTime = 0.0f;
};
