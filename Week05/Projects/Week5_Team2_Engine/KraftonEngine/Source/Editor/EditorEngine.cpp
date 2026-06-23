#include "Editor/EditorEngine.h"

#include "Engine/Runtime/WindowsWindow.h"
#include "Engine/Serialization/LevelSaveManager.h"
#include "Engine/Viewport/GameViewportClient.h"
#include "GameFramework/World.h"
#include "GameFramework/Level.h"
#include "Editor/EditorRenderPipeline.h"
#include "Editor/Viewport/ViewportCamera.h"
#include "Engine/Input/InputSystem.h"
#include "Profiling/Stats.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Object/ObjectFactory.h"
#include "Mesh/ObjManager.h"
#include "Viewport/Viewport.h"
#include "Component/BillboardComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/SceneComponent.h"
#include "Engine/GameFramework/StaticMeshActor.h"
#include "GameFramework/TestRotateActor.h"
#include "Texture/Texture2D.h"

#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>

namespace
{
FString ToProjectRelativePathUtf8(const std::filesystem::path& InPath)
{
	const std::filesystem::path RootPath(FPaths::RootDir());
	const std::filesystem::path Normalized = InPath.lexically_normal();
	std::filesystem::path Relative = Normalized.lexically_relative(RootPath);
	if (!Relative.empty() && Relative.wstring().find(L"..") != 0)
	{
		return FPaths::ToUtf8(Relative.generic_wstring());
	}

	return FPaths::ToUtf8(Normalized.generic_wstring());
}

std::filesystem::path ToAbsolutePath(const FString& InPath)
{
	std::filesystem::path P(FPaths::ToWide(InPath));
	if (P.is_absolute())
	{
		return P.lexically_normal();
	}

	return (std::filesystem::path(FPaths::RootDir()) / P).lexically_normal();
}

bool BuildPerspectiveCameraData(UEditorEngine* InEditor, FPerspectiveCameraData& OutCamData)
{
	if (!InEditor)
	{
		return false;
	}

	for (FLevelEditorViewportClient* VC : InEditor->GetLevelViewportClients())
	{
		if (!VC)
		{
			continue;
		}

		const ELevelViewportType ViewportType = VC->GetRenderOptions().ViewportType;
		const bool bCanUseAsPerspectiveSource =
			(ViewportType == ELevelViewportType::Perspective)
			|| (ViewportType == ELevelViewportType::FreeOrthographic);
		if (!bCanUseAsPerspectiveSource)
		{
			continue;
		}

		FViewportCamera* Cam = VC->GetCamera();
		if (!Cam)
		{
			continue;
		}

		OutCamData.Location = Cam->GetWorldLocation();
		OutCamData.Rotation = Cam->GetWorldMatrix().GetEuler();
		OutCamData.FOV = Cam->GetFOV();
		OutCamData.NearClip = Cam->GetNearPlane();
		OutCamData.FarClip = Cam->GetFarPlane();
		OutCamData.bValid = true;
		return true;
	}

	return false;
}

bool OpenLevelFileDialog(bool bSave, FString& OutFilePath, const FString& InSuggestedName = "")
{
	OutFilePath.clear();

	wchar_t FilePathBuffer[MAX_PATH] = {};
	const std::wstring SceneDir = FLevelSaveManager::GetSceneDirectory();
	const std::wstring InitialFileName = InSuggestedName.empty() ? L"" : FPaths::ToWide(InSuggestedName + ".Scene");
	if (!InitialFileName.empty())
	{
		wcsncpy_s(FilePathBuffer, InitialFileName.c_str(), _TRUNCATE);
	}

	OPENFILENAMEW Ofn = {};
	Ofn.lStructSize = sizeof(Ofn);
	Ofn.hwndOwner = nullptr;
	Ofn.lpstrFilter = L"Level Files (*.Scene;*.scene)\0*.Scene;*.scene\0All Files (*.*)\0*.*\0";
	Ofn.lpstrFile = FilePathBuffer;
	Ofn.nMaxFile = MAX_PATH;
	Ofn.lpstrInitialDir = SceneDir.c_str();
	Ofn.lpstrDefExt = L"Scene";
	Ofn.lpstrTitle = bSave ? L"Save Level As" : L"Load Level";
	Ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (bSave)
	{
		Ofn.Flags |= OFN_OVERWRITEPROMPT;
	}
	else
	{
		Ofn.Flags |= OFN_FILEMUSTEXIST;
	}

	const BOOL bSuccess = bSave ? GetSaveFileNameW(&Ofn) : GetOpenFileNameW(&Ofn);
	if (!bSuccess)
	{
		return false;
	}

	std::filesystem::path PickedPath(FilePathBuffer);
	if (PickedPath.extension().empty())
	{
		PickedPath.replace_extension(L".Scene");
	}

	OutFilePath = ToProjectRelativePathUtf8(PickedPath);
	return true;
}
}

IMPLEMENT_CLASS(UEditorEngine, UEngine)

FWorldContext* UEditorEngine::GetEditorWorldContext()
{
	for (auto& Ctx : WorldList)
	{
		if (Ctx.WorldType == EWorldType::Editor)
		{
			return &Ctx;
		}
	}
	return nullptr;
}

void UEditorEngine::Init(FWindowsWindow* InWindow)
{
	// 엔진 공통 초기화 (Renderer, D3D, 싱글턴 등)
	UEngine::Init(InWindow);

	FObjManager::ScanMeshAssets();
	FObjManager::ScanMaterialAssets();

	// Register Default Placeable Actors
	RegisterPlaceableActor({ "Empty Actor", "Basic", [](UWorld* W) {
		AActor* A = W->SpawnActor<AActor>();
		USceneComponent* Root = A->AddComponent<USceneComponent>();
		A->SetRootComponent(Root);
		return A;
	} });

	RegisterPlaceableActor({ "Cube", "Basic", [](UWorld* W) {
		AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>();
		A->InitDefaultComponents("Data/BasicShape/Cube.OBJ");
		return static_cast<AActor*>(A);
	} });

	RegisterPlaceableActor({ "Rotate Test (TickOn)", "Basic", [](UWorld* W) {
		ATestRotateActor* A = W->SpawnActor<ATestRotateActor>();
		A->InitializeTest(true);
		return static_cast<AActor*>(A);
	} });

	RegisterPlaceableActor({ "Rotate Test (TickOff)", "Basic", [](UWorld* W) {
		ATestRotateActor* A = W->SpawnActor<ATestRotateActor>();
		A->InitializeTest(false);
		return static_cast<AActor*>(A);
	} });

	// 에디터 전용 초기화
	FEditorSettings::Get().LoadFromFile(FEditorSettings::GetDefaultSettingsPath());

	MainPanel.Create(Window, Renderer, this);

	// World
	if (WorldList.empty())
	{
		CreateWorldContext(EWorldType::Editor, FName("Default"));
	}
	SetActiveWorld(WorldList[0].ContextHandle);
	GetWorld()->InitWorld();

	// Selection & Gizmo
	SelectionManager.Init(GetWorld());

	// 뷰포트 레이아웃 초기화 + 저장된 설정 복원
	ViewportLayout.Initialize(this, Window, Renderer, &SelectionManager);
	ViewportLayout.LoadFromSettings();

	// Editor render pipeline
	SetRenderPipeline(std::make_unique<FEditorRenderPipeline>(this, Renderer));

	ResetViewport();
}

void UEditorEngine::Shutdown()
{
	// 에디터 해제 (엔진보다 먼저)
	ViewportLayout.SaveToSettings();
	FEditorSettings::Get().SaveToFile(FEditorSettings::GetDefaultSettingsPath());
	CloseLevel();
	SelectionManager.Shutdown();
	MainPanel.Release();

	// 뷰포트 레이아웃 해제
	ViewportLayout.Release();
	InputTargetHosts.clear();

	// 엔진 공통 해제 (Renderer, D3D 등)
	UEngine::Shutdown();
}

void UEditorEngine::OnWindowResized(uint32 Width, uint32 Height)
{
	UEngine::OnWindowResized(Width, Height);
	// 윈도우 리사이즈 시에는 ImGui 패널이 실제 크기를 결정하므로
	// FViewport RT는 SSplitter 레이아웃에서 지연 리사이즈로 처리됨
}

void UEditorEngine::Tick(float DeltaTime)
{
	FooterLogSystem.Tick(DeltaTime);
	MainPanel.Update();
	SetImGuiInputCapture(MainPanel.IsCapturingMouse(), MainPanel.IsCapturingKeyboard());
	PruneInputTargetHosts();

	ClearInputTargets();
	for (FLevelEditorViewportClient* VC : ViewportLayout.GetLevelViewportClients())
	{
		if (!VC)
		{
			continue;
		}

		FViewport* VP = VC->GetViewport();
		if (!VP)
		{
			continue;
		}

		if (FViewportClient* HostClient = ResolveInputTargetClient(VP, VC))
		{
			VP->SetClient(HostClient);
		}

		RegisterInputTarget(
			VP,
			VC,
			EInputRouteDomain::Editor,
			[VC](FRect& OutRect)
			{
				const FRect& R = VC->GetViewportScreenRect();
				if (R.Width <= 0.0f || R.Height <= 0.0f)
				{
					return false;
				}
				OutRect = R;
				return true;
			});
	}

	DispatchInput();

	for (FEditorViewportClient* VC : ViewportLayout.GetAllViewportClients())
	{
		if (bPIEEnabled
			&& PIEControlMode == EPIEControlMode::Possessed
			&& VC == PIEEntryEditorViewportClient)
		{
			// Possessed PIE 동안 엔트리 에디터 카메라 로직을 중지해
			// PIE 카메라 동기화와 충돌하지 않도록 한다.
			VC->TickPIEOutlineFlashOnly(DeltaTime);
			continue;
		}
		VC->Tick(DeltaTime);
	}

	WorldTick(DeltaTime);
	Render(DeltaTime);
}

FViewportCamera* UEditorEngine::GetCamera() const
{
	if (FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport())
	{
		return ActiveVC->GetCamera();
	}
	return nullptr;
}

void UEditorEngine::RenderUI(float DeltaTime)
{
	MainPanel.Render(DeltaTime);
}

void UEditorEngine::SetPickingMode(EPickingMode InMode)
{
	if (PickingMode == InMode)
	{
		return;
	}

	PickingMode = InMode;
	for (FEditorViewportClient* VC : ViewportLayout.GetAllViewportClients())
	{
		if (VC)
		{
			VC->ResetIdPickingState();
		}
	}
}

// ─── 기존 메서드 ──────────────────────────────────────────

void UEditorEngine::ResetViewport()
{
	SelectionManager.SetWorld(GetWorld());
	ViewportLayout.ResetViewport(GetWorld());
}

void UEditorEngine::CloseLevel()
{
	ClearWorlds();
}

void UEditorEngine::NewLevel()
{
	ClearWorlds();
	FWorldContext& Ctx = CreateWorldContext(EWorldType::Editor, FName("NewLevel"), "New Level");
	SetActiveWorld(Ctx.ContextHandle);
	CurrentLevelFilePath.clear();
	FooterLogSystem.Push("New Level created");

	ResetViewport();
}

void UEditorEngine::StartPIE()
{
	ViewportLayout.BeginPIEViewportMode();

	FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport();
	PIEEntryEditorViewportClient = ActiveVC;
	PIEEntryViewport = ActiveVC ? ActiveVC->GetViewport() : nullptr;
	PIEControlMode = EPIEControlMode::Possessed;
	if (PIEEntryEditorViewportClient)
	{
		bPIEEntryPrevShowGizmo = PIEEntryEditorViewportClient->GetRenderOptions().ShowFlags.bGizmo;
		bPIEEntryPrevShowGizmoValid = true;
	}
	else
	{
		bPIEEntryPrevShowGizmoValid = false;
	}

	FWorldContext* Context = GetEditorWorldContext();
	FWorldContext PIEWorldContext = Context->Duplicate();
	PIEWorldContext.WorldType = EWorldType::PIE;
	
	WorldList.push_back(PIEWorldContext);
	SetActiveWorld(WorldList.back().ContextHandle);

	SelectionManager.SetWorld(WorldList.back().World);

	// AActor::BeginPlay()
	PIEWorldContext.World->InitWorld();
	PIEWorldContext.World->BeginPlay();
	
	bPIEEnabled = true;

	// ViewportClient 전환
	if (PIEEntryViewport)
	{
		SetViewportSubClientForWorldType(PIEEntryViewport, EWorldType::PIE);
	}
	else
	{
		SetActiveViewportSubClientForWorldType(EWorldType::PIE);
	}
}

void UEditorEngine::EndPIE()
{
	InputSystem::Get().EndRelativeMouseMode();
	if (PIEViewportClient)
	{
		PIEViewportClient->OnEndPIE();
	}

	UWorld* PIEWorld = GetWorld();
	// Get PIE world context
	FWorldContext* PIEContext = GetWorldContextFromWorld(PIEWorld);
	if (PIEContext && PIEContext->WorldType == EWorldType::PIE)
	{
		SelectionManager.ClearSelection();

		// 1. WorldContext를 에디터로 복구
		FWorldContext* EditorContext = GetEditorWorldContext();
		if (EditorContext)
		{
			SetActiveWorld(EditorContext->ContextHandle);
			// SelectionManager의 선택 대상을 에디터 월드로 복구
			SelectionManager.SetWorld(GetWorld());
		}

		// 2. ViewportClient 및 레이어 원상 복구
		if (bPIEEntryPrevShowGizmoValid && PIEEntryEditorViewportClient)
		{
			PIEEntryEditorViewportClient->GetRenderOptions().ShowFlags.bGizmo = bPIEEntryPrevShowGizmo;
		}
		if (PIEEntryViewport)
		{
			SetViewportSubClientForWorldType(PIEEntryViewport, EWorldType::Editor);
		}
		else
		{
			SetActiveViewportSubClientForWorldType(EWorldType::Editor);
		}

		// 3. PIE 월드 정리
		PIEContext->World->EndPlay();
		auto WorldListIter = find_if(WorldList.begin(), WorldList.end(), 
			[PIEContext](const FWorldContext& a) 
			{
				return a.ContextHandle == PIEContext->ContextHandle;
			});
		if (WorldListIter != WorldList.end())
		{
			UObjectManager::Get().DestroyObject(PIEContext->World);
			WorldList.erase(WorldListIter);
		}
	}
	
	bPIEEnabled = false;
	PIEControlMode = EPIEControlMode::Possessed;
	PIEEntryViewport = nullptr;
	PIEEntryEditorViewportClient = nullptr;
	bPIEEntryPrevShowGizmoValid = false;
	ViewportLayout.EndPIEViewportMode();
}

bool UEditorEngine::TogglePIEControlMode()
{
	if (!bPIEEnabled)
	{
		return false;
	}

	if (PIEControlMode == EPIEControlMode::Possessed)
	{
		return EnterPIEEjectedMode();
	}

	return EnterPIEPossessedMode();
}

bool UEditorEngine::EnterPIEPossessedMode()
{
	if (!bPIEEnabled)
	{
		return false;
	}

	FViewport* TargetViewport = PIEEntryViewport;
	if (!TargetViewport)
	{
		FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport();
		TargetViewport = ActiveVC ? ActiveVC->GetViewport() : nullptr;
	}

	if (!TargetViewport)
	{
		return false;
	}

	if (!ApplyPIEControlMode(TargetViewport, EPIEControlMode::Possessed))
	{
		return false;
	}

	PIEEntryViewport = TargetViewport;
	PIEEntryEditorViewportClient = FindLevelViewportClientByViewport(TargetViewport);
	PIEControlMode = EPIEControlMode::Possessed;
	return true;
}

bool UEditorEngine::EnterPIEEjectedMode()
{
	if (!bPIEEnabled)
	{
		return false;
	}

	FViewport* TargetViewport = PIEEntryViewport;
	if (!TargetViewport)
	{
		FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport();
		TargetViewport = ActiveVC ? ActiveVC->GetViewport() : nullptr;
	}

	if (!TargetViewport)
	{
		return false;
	}

	if (!ApplyPIEControlMode(TargetViewport, EPIEControlMode::Ejected))
	{
		return false;
	}

	InputSystem::Get().EndRelativeMouseMode();
	PIEEntryViewport = TargetViewport;
	PIEEntryEditorViewportClient = FindLevelViewportClientByViewport(TargetViewport);
	PIEControlMode = EPIEControlMode::Ejected;
	return true;
}

void UEditorEngine::ClearWorlds()
{
	FStatManager::Get().ResetStats();

	SelectionManager.ClearSelection();
	SelectionManager.SetWorld(nullptr);

	for (FWorldContext& Ctx : WorldList)
	{
		Ctx.World->EndPlay();
		UObjectManager::Get().DestroyObject(Ctx.World);
	}

	WorldList.clear();
	ActiveWorldHandle = FName::None;

	// Reset rendering bus and name counters to free memory
	ResetRenderPipeline();
	UObjectManager::Get().ClearNameCounters();

	ViewportLayout.DestroyAllCameras();
}

bool UEditorEngine::SaveLevelAsName(const FString& InLevelName)
{
	FWorldContext* Ctx = GetWorldContextFromHandle(GetActiveWorldHandle());
	if (!Ctx || !Ctx->World)
	{
		return false;
	}

	const FString TrimmedName = InLevelName;
	if (TrimmedName.empty())
	{
		return false;
	}

	FPerspectiveCameraData PerspectiveCamData;
	const FPerspectiveCameraData* PerspectiveCam = BuildPerspectiveCameraData(this, PerspectiveCamData)
		? &PerspectiveCamData
		: nullptr;
	FLevelSaveManager::SaveLevelAsJSON(TrimmedName, *Ctx, PerspectiveCam);

	const std::filesystem::path ScenePath =
		std::filesystem::path(FLevelSaveManager::GetSceneDirectory())
		/ (FPaths::ToWide(TrimmedName) + FLevelSaveManager::LevelExtension);
	CurrentLevelFilePath = ToProjectRelativePathUtf8(ScenePath);
	FooterLogSystem.Push("Level saved: " + TrimmedName);
	return true;
}

bool UEditorEngine::SaveLevelAsWithDialog()
{
	FString PickedFilePath;
	FString SuggestedName;
	if (HasCurrentLevelFilePath())
	{
		SuggestedName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(CurrentLevelFilePath)).stem().wstring());
	}
	else if (const FWorldContext* ActiveCtx = GetWorldContextFromHandle(GetActiveWorldHandle()))
	{
		SuggestedName = ActiveCtx->ContextName;
	}
	if (!OpenLevelFileDialog(true, PickedFilePath, SuggestedName))
	{
		return false;
	}

	const FString LevelName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(PickedFilePath)).stem().wstring());
	return SaveLevelAsName(LevelName);
}

bool UEditorEngine::SaveLevel()
{
	if (!HasCurrentLevelFilePath())
	{
		return SaveLevelAsWithDialog();
	}

	const FString LevelName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(CurrentLevelFilePath)).stem().wstring());
	return SaveLevelAsName(LevelName);
}

bool UEditorEngine::LoadLevelFromPath(const FString& InLevelFilePath)
{
	if (InLevelFilePath.empty())
	{
		return false;
	}

	ClearWorlds();

	FWorldContext LoadCtx;
	FPerspectiveCameraData CamData;
	const std::filesystem::path AbsolutePath = ToAbsolutePath(InLevelFilePath);
	FLevelSaveManager::LoadLevelFromJSON(FPaths::ToUtf8(AbsolutePath.wstring()), LoadCtx, CamData);
	if (!LoadCtx.World)
	{
		return false;
	}

	WorldList.push_back(LoadCtx);
	SetActiveWorld(LoadCtx.ContextHandle);
	ResetViewport();

	// ResetViewport()가 카메라를 기본값으로 초기화하므로 이후 복원한다.
	if (CamData.bValid)
	{
		for (FLevelEditorViewportClient* VC : GetLevelViewportClients())
		{
			if (!VC)
			{
				continue;
			}

			const ELevelViewportType ViewportType = VC->GetRenderOptions().ViewportType;
			const bool bCanRestoreToViewport =
				(ViewportType == ELevelViewportType::Perspective)
				|| (ViewportType == ELevelViewportType::FreeOrthographic);
			if (!bCanRestoreToViewport)
			{
				continue;
			}

			if (FViewportCamera* Cam = VC->GetCamera())
			{
				Cam->SetWorldLocation(CamData.Location);
				Cam->SetRelativeRotation(CamData.Rotation);
				FViewportCameraState CS = Cam->GetCameraState();
				CS.FOV = CamData.FOV;
				CS.NearZ = CamData.NearClip;
				CS.FarZ = CamData.FarClip;
				Cam->SetCameraState(CS);
			}
			break;
		}
	}

	CurrentLevelFilePath = ToProjectRelativePathUtf8(AbsolutePath);
	FooterLogSystem.Push("Level loaded: " + FPaths::ToUtf8(AbsolutePath.stem().wstring()));
	return true;
}

bool UEditorEngine::LoadLevelWithDialog()
{
	FString PickedFilePath;
	if (!OpenLevelFileDialog(false, PickedFilePath))
	{
		return false;
	}

	return LoadLevelFromPath(PickedFilePath);
}

bool UEditorEngine::OpenAssetFolder()
{
	const std::wstring AssetDir = FPaths::RootDir() + L"Asset";
	FPaths::CreateDir(AssetDir);
	const HINSTANCE OpenResult = ShellExecuteW(nullptr, L"open", AssetDir.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
	return reinterpret_cast<INT_PTR>(OpenResult) > 32;
}

TArray<FString> UEditorEngine::GetActiveFooterLogMessages() const
{
	return FooterLogSystem.GetActiveMessages();
}

FViewportClient* UEditorEngine::ResolveInputTargetClient(FViewport* InViewport, FViewportClient* InClient) const
{
	if (!InViewport || !InClient)
	{
		return nullptr;
	}

	auto Found = InputTargetHosts.find(InViewport);
	if (Found == InputTargetHosts.end())
	{
		Found = InputTargetHosts.emplace(InViewport, FViewportHostClient()).first;
	}

	FViewportHostClient& Host = Found->second;
	if (!Host.GetActiveSubClient())
	{
		Host.SetActiveSubClient(InClient);
	}
	return &Host;
}

void UEditorEngine::PruneInputTargetHosts()
{
	TSet<FViewport*> LiveViewports;
	for (FLevelEditorViewportClient* VC : ViewportLayout.GetLevelViewportClients())
	{
		if (VC && VC->GetViewport())
		{
			LiveViewports.insert(VC->GetViewport());
		}
	}

	for (auto It = InputTargetHosts.begin(); It != InputTargetHosts.end();)
	{
		if (LiveViewports.find(It->first) == LiveViewports.end())
		{
			It = InputTargetHosts.erase(It);
		}
		else
		{
			++It;
		}
	}
}

FLevelEditorViewportClient* UEditorEngine::FindLevelViewportClientByViewport(FViewport* InViewport) const
{
	if (!InViewport)
	{
		return nullptr;
	}

	for (FLevelEditorViewportClient* VC : ViewportLayout.GetLevelViewportClients())
	{
		if (VC && VC->GetViewport() == InViewport)
		{
			return VC;
		}
	}

	return nullptr;
}

bool UEditorEngine::SetViewportSubClient(FViewport* InViewport, FViewportClient* InSubClient)
{
	if (!InViewport || !InSubClient)
	{
		return false;
	}

	FViewportHostClient& Host = InputTargetHosts[InViewport];
	Host.SetActiveSubClient(InSubClient);
	InViewport->SetClient(&Host);
	return true;
}

bool UEditorEngine::ResetViewportSubClient(FViewport* InViewport)
{
	if (!InViewport)
	{
		return false;
	}

	FLevelEditorViewportClient* DefaultClient = FindLevelViewportClientByViewport(InViewport);
	if (!DefaultClient)
	{
		return false;
	}

	// 레이어 제거 및 월드 포인터 복구
	// NOTE: 이 시점에서 World가 이미 Reset된 상태혀야 함.
	auto Found = InputTargetHosts.find(InViewport);
	if (Found != InputTargetHosts.end())
	{
		if (PIEViewportClient)
		{
			Found->second.RemoveLayerClient(PIEViewportClient);
		}
		Found->second.RemoveLayerClient(DefaultClient);
	}
	DefaultClient->SetWorld(GetWorld());

	return SetViewportSubClient(InViewport, DefaultClient);
}

bool UEditorEngine::SetViewportSubClientForWorldType(FViewport* InViewport, EWorldType InWorldType)
{
	if (!InViewport)
	{
		return false;
	}

	switch (InWorldType)
	{
	case EWorldType::Editor:
		return ResetViewportSubClient(InViewport);
	case EWorldType::PIE:
	{
		if (!PIEViewportClient)
		{
			PIEViewportClient = UObjectManager::Get().CreateObject<UGameViewportClient>();
		}
		PIEViewportClient->SetViewport(InViewport);
		PIEViewportClient->OnBeginPIE();

		// 기존 레벨 에디터 클라이언트를 ViewportHost의 레이어로 추가
		// => PIE 모드에서도 에디터 기능(기즈모, 선택 등) 유지
		FLevelEditorViewportClient* EditorVC = FindLevelViewportClientByViewport(InViewport);
		if (EditorVC)
		{
			EditorVC->SetWorld(GetWorld());
			PIEEntryViewport = InViewport;
			PIEEntryEditorViewportClient = EditorVC;
			if (!ApplyPIEControlMode(InViewport, PIEControlMode))
			{
				return false;
			}
			return true;
		}

		return SetViewportSubClient(InViewport, PIEViewportClient);
	}
	default:
		// Game specific client is not wired yet.
		return false;
	}
}

bool UEditorEngine::ApplyPIEControlMode(FViewport* InViewport, EPIEControlMode InMode)
{
	if (!InViewport || !PIEViewportClient)
	{
		return false;
	}

	FLevelEditorViewportClient* EditorVC = FindLevelViewportClientByViewport(InViewport);
	if (!EditorVC)
	{
		return false;
	}

	FViewportHostClient& Host = InputTargetHosts[InViewport];
	EditorVC->SetWorld(GetWorld());
	EditorVC->GetRenderOptions().ShowFlags.bGizmo = (InMode == EPIEControlMode::Ejected);
	Host.RemoveLayerClient(EditorVC);
	Host.RemoveLayerClient(PIEViewportClient);

	if (InMode == EPIEControlMode::Possessed)
	{
		Host.SetActiveSubClient(PIEViewportClient);
	}
	else
	{
		Host.SetActiveSubClient(EditorVC);
	}

	InViewport->SetClient(&Host);
	return true;
}

FViewportClient* UEditorEngine::GetViewportSubClient(FViewport* InViewport) const
{
	if (!InViewport)
	{
		return nullptr;
	}

	auto Found = InputTargetHosts.find(InViewport);
	if (Found != InputTargetHosts.end())
	{
		return Found->second.GetActiveSubClient();
	}

	if (FLevelEditorViewportClient* DefaultClient = FindLevelViewportClientByViewport(InViewport))
	{
		return DefaultClient;
	}

	return nullptr;
}

bool UEditorEngine::SetActiveViewportSubClient(FViewportClient* InSubClient)
{
	FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport();
	if (!ActiveVC)
	{
		return false;
	}

	return SetViewportSubClient(ActiveVC->GetViewport(), InSubClient);
}

bool UEditorEngine::ResetActiveViewportSubClient()
{
	FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport();
	if (!ActiveVC)
	{
		return false;
	}

	return ResetViewportSubClient(ActiveVC->GetViewport());
}

bool UEditorEngine::SetActiveViewportSubClientForWorldType(EWorldType InWorldType)
{
	FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport();
	if (!ActiveVC)
	{
		return false;
	}

	return SetViewportSubClientForWorldType(ActiveVC->GetViewport(), InWorldType);
}

FViewportClient* UEditorEngine::GetActiveViewportSubClient() const
{
	FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport();
	if (!ActiveVC)
	{
		return nullptr;
	}

	return GetViewportSubClient(ActiveVC->GetViewport());
}

AActor* UEditorEngine::SpawnPlaceableActor(int32 PlaceableIndex, const FVector& SpawnLocation)
{
	UWorld* World = GetWorld();
	if (!World || PlaceableIndex < 0 || PlaceableIndex >= static_cast<int32>(PlaceableActors.size()))
	{
		return nullptr;
	}

	const FPlaceActorDesc& Desc = PlaceableActors[PlaceableIndex];
	AActor* SpawnedActor = Desc.SpawnFunc(World);
	if (!SpawnedActor)
	{
		return nullptr;
	}

	SpawnedActor->SetActorLocation(SpawnLocation);
	SetupVisualization(SpawnedActor);
	return SpawnedActor;
}

// TODO: 여기에 있어선 안되는 함수..
void UEditorEngine::SetupVisualization(AActor* Actor)
{
	if (!Actor) return;

	ID3D11Device* Device = GetRenderer().GetFD3DDevice().GetDevice();
	USceneComponent* Root = Actor->GetRootComponent();

	// 1. UUID 텍스트 주입 (액터당 하나, 루트에 부착)
	if (Root)
	{
		bool bHasUUIDText = false;
		for (auto Comp : Actor->GetComponents())
		{
			if (Comp->IsVisualizationComponent() && Comp->IsA<UTextRenderComponent>())
			{
				bHasUUIDText = true;
				break;
			}
		}

		if (!bHasUUIDText)
		{
			UTextRenderComponent* TextComp = Actor->AddComponent<UTextRenderComponent>();
			TextComp->SetIsVisualizationComponent(true);
			TextComp->SetRelativeLocation(FVector(0.0f, 0.0f, 1.3f));
			TextComp->SetText("UUID : " + TextComp->GetOwnerUUIDToString());
			TextComp->AttachToComponent(Root);
			TextComp->SetFont(FName("Default"));
		}
	}

	// 2. 컴포넌트 단위 빌보드 주입 (Billboard는 SceneComponent에 무조건 붙는 친구)
	// AddComponent 호출 시 OwnedComponents가 변경되므로 스냅샷으로 순회
	TArray<UActorComponent*> CompSnapshot = Actor->GetComponents();
	for (auto Comp : CompSnapshot)
	{
		USceneComponent* SceneComp = Cast<USceneComponent>(Comp);
		if (!SceneComp || SceneComp->IsVisualizationComponent()) continue;

		// 렌더링 개체가 아닌 순수 SceneComponent인 경우에만 아이콘 표시
		if (!SceneComp->IsA<UPrimitiveComponent>())
		{
			bool bAlreadyHasBillboard = false;
			for (auto Child : SceneComp->GetChildren())
			{
				if (Child->IsVisualizationComponent() && Child->IsA<UBillboardComponent>())
				{
					bAlreadyHasBillboard = true;
					break;
				}
			}

			if (!bAlreadyHasBillboard)
			{
				UBillboardComponent* BillboardComp = Actor->AddComponent<UBillboardComponent>();
				BillboardComp->SetIsVisualizationComponent(true);
				BillboardComp->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
				BillboardComp->AttachToComponent(SceneComp);

				UTexture2D* ActorIcon = UTexture2D::LoadFromFile("Asset/Editor/Icon/EmptyActor_256x.png", Device);
				BillboardComp->SetSprite(ActorIcon);
			}
		}
	}
}
