#pragma once

#include "Engine/Runtime/Engine.h"

#include "Editor/Viewport/FLevelViewportLayout.h"
#include "Editor/Selection/PickingTypes.h"
#include "Editor/Subsystem/OverlayStatSystem.h"
#include "Editor/UI/EditorMainPanel.h"
#include "Editor/UI/EditorFooterLogSystem.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Selection/SelectionManager.h"
#include "Viewport/ViewportClient.h"
#include <functional>

class FTransformGizmo;
class FLevelEditorViewportClient;
class FEditorViewportClient;
class FOverlayStatSystem;
class FViewportCamera;

// 액터 배치 UI에서 사용할 액터 등록 구조체
struct FPlaceActorDesc
{
	FString Name; // 액터 이름 (예: "Cube", "Point Light")
	FString Category; // 아직은 "Basic" 만 존재
	std::function<AActor*(UWorld*)> SpawnFunc; // 에디터에서 직접 액터를 생성하는 경우에 호출되는 함수.
};

class UEditorEngine : public UEngine
{
public:
	enum class EPIEControlMode : uint8
	{
		Possessed,
		Ejected
	};

	DECLARE_CLASS(UEditorEngine, UEngine)

	UEditorEngine() = default;
	~UEditorEngine() override = default;

	// Lifecycle overrides
	void Init(FWindowsWindow* InWindow) override;
	void Shutdown() override;
	void Tick(float DeltaTime) override;
	void OnWindowResized(uint32 Width, uint32 Height) override;

	// Editor-specific API
	FTransformGizmo* GetGizmo() const { return SelectionManager.GetGizmo(); }
	FViewportCamera* GetCamera() const;

	void ClearWorlds();
	void ResetViewport();
	void CloseLevel();
	void NewLevel();
	bool LoadLevelFromPath(const FString& InLevelFilePath);
	bool LoadLevelWithDialog();
	bool SaveLevel();
	bool SaveLevelAsWithDialog();
	bool SaveLevelAsName(const FString& InLevelName);
	bool OpenAssetFolder();
	const FString& GetCurrentLevelFilePath() const { return CurrentLevelFilePath; }
	bool HasCurrentLevelFilePath() const { return !CurrentLevelFilePath.empty(); }
	TArray<FString> GetActiveFooterLogMessages() const;
	
	void StartPIE();
	void EndPIE();
	bool TogglePIEControlMode();
	bool EnterPIEPossessedMode();
	bool EnterPIEEjectedMode();
	EPIEControlMode GetPIEControlMode() const { return PIEControlMode; }
	FLevelEditorViewportClient* GetPIEEntryViewportClient() const { return PIEEntryEditorViewportClient; }

	FEditorSettings& GetSettings() { return FEditorSettings::Get(); }
	const FEditorSettings& GetSettings() const { return FEditorSettings::Get(); }

	bool IsPIEEnabled() const { return bPIEEnabled; }
	
	FWorldContext* GetEditorWorldContext();
	
	FSelectionManager& GetSelectionManager() { return SelectionManager; }

	void RenderUI(float DeltaTime);

	FOverlayStatSystem& GetOverlayStatSystem() { return OverlayStatSystem; }
	const FOverlayStatSystem& GetOverlayStatSystem() const { return OverlayStatSystem; }

	void SetPickingMode(EPickingMode InMode);
	EPickingMode GetPickingMode() const { return PickingMode; }

	void SetupVisualization(AActor* Actor);

	// Placement
	const TArray<FPlaceActorDesc>& GetPlaceableActors() const { return PlaceableActors; }
	void RegisterPlaceableActor(const FPlaceActorDesc& Desc) { PlaceableActors.push_back(Desc); }
	AActor* SpawnPlaceableActor(int32 PlaceableIndex, const FVector& SpawnLocation);

	// PIE preparation: swap viewport sub-client through host client.
	bool SetViewportSubClient(FViewport* InViewport, FViewportClient* InSubClient);
	bool ResetViewportSubClient(FViewport* InViewport);
	bool SetViewportSubClientForWorldType(FViewport* InViewport, EWorldType InWorldType);
	FViewportClient* GetViewportSubClient(FViewport* InViewport) const;
	bool SetActiveViewportSubClient(FViewportClient* InSubClient);
	bool ResetActiveViewportSubClient();
	bool SetActiveViewportSubClientForWorldType(EWorldType InWorldType);
	FViewportClient* GetActiveViewportSubClient() const;

private:
	bool ApplyPIEControlMode(FViewport* InViewport, EPIEControlMode InMode);
	FViewportClient* ResolveInputTargetClient(FViewport* InViewport, FViewportClient* InClient) const override;
	void PruneInputTargetHosts();
	FLevelEditorViewportClient* FindLevelViewportClientByViewport(FViewport* InViewport) const;

	FSelectionManager SelectionManager;
	FEditorMainPanel MainPanel;
	FOverlayStatSystem OverlayStatSystem;
	FEditorFooterLogSystem FooterLogSystem;
	EPickingMode PickingMode = EPickingMode::IDBuffer;
	mutable TMap<FViewport*, FViewportHostClient> InputTargetHosts;
	UGameViewportClient* PIEViewportClient = nullptr;
	bool bPIEEnabled = false;
	EPIEControlMode PIEControlMode = EPIEControlMode::Possessed;
	FViewport* PIEEntryViewport = nullptr;
	FLevelEditorViewportClient* PIEEntryEditorViewportClient = nullptr;
	bool bPIEEntryPrevShowGizmo = true;
	bool bPIEEntryPrevShowGizmoValid = false;

	TArray<FPlaceActorDesc> PlaceableActors;
	FString CurrentLevelFilePath;
};
