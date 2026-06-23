#include "Engine/Runtime/GameRenderPipeline.h"

#include "Engine/Runtime/GameEngine.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "GameFramework/World.h"
#include "Component/Camera/CameraComponent.h"
#include "Component/Camera/CineCameraComponent.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Input/InputSystem.h"
#include "Viewport/GameViewportClient.h"
#include "Viewport/Viewport.h"
#include "Math/MathUtils.h"
#include "Core/Logging/Log.h"

namespace
{
	void ApplyLetterboxAspect(FMinimalViewInfo& POV, const FCameraLetterboxState& Letterbox, float ViewportWidth, float ViewportHeight)
	{
		if (!Letterbox.bEnabled || Letterbox.Amount <= 0.0f || ViewportWidth <= 0.0f || ViewportHeight <= 0.0f)
		{
			return;
		}

		const float Thickness = FMath::Clamp(Letterbox.Thickness * Letterbox.Amount, 0.0f, 0.49f);
		const float VisibleHeightScale = 1.0f - Thickness * 2.0f;
		if (VisibleHeightScale <= FMath::Epsilon)
		{
			return;
		}

		POV.AspectRatio = (ViewportWidth / ViewportHeight) / VisibleHeightScale;
	}
}

FGameRenderPipeline::FGameRenderPipeline(UGameEngine* InGame, FRenderer& InRenderer)
	: Game(InGame)
{
}

FGameRenderPipeline::~FGameRenderPipeline()
{
}

void FGameRenderPipeline::OnSceneCleared()
{
	Frame.ClearViewportResources();
}

void FGameRenderPipeline::Execute(float DeltaTime, FRenderer& Renderer)
{
	ID3D11DeviceContext* Ctx = Renderer.GetFD3DDevice().GetDeviceContext();
	if (!Ctx) return;

	Frame.ClearViewportResources();

	FDrawCommandBuilder& Builder = Renderer.GetBuilder();

	UWorld* World = Game->GetWorld();
	FViewport* VP = Game->GetStandaloneViewport();
	if (!World || !VP)
	{
		Renderer.BeginFrame();
		Renderer.EndFrame();
		return;
	}

	FMinimalViewInfo POV;
	const bool bHasActivePOV = World->GetActivePOV(POV);
	if (!bHasActivePOV)
	{
		static bool bLoggedMissingPOV = false;
		if (!bLoggedMissingPOV)
		{
			UE_LOG("[GameRenderPipeline] Active POV missing; rendering with fallback POV so screen UI remains visible.");
			bLoggedMissingPOV = true;
		}
	}

	Frame.WorldType = World->GetWorldType();

	FViewportRenderOptions Opts;
	Opts.ViewMode = EViewMode::Lit_Phong;
	Frame.SetRenderOptions(Opts);

	FScene* Scene = &World->GetScene();

	PrepareViewport(VP, Ctx);
	BuildFrame(VP, POV, Scene, World);
	RenderScopeLensCapture(VP, POV, Scene, World, Renderer, Ctx);

	FCollectOutput Output;
	CollectCommands(Scene, Renderer, Output);

	Renderer.Render(Frame, World, *Scene);

	Renderer.BeginFrame();
	Renderer.BlitToBackBuffer(VP->GetSRV());
	Renderer.EndFrame();
}

void FGameRenderPipeline::PrepareViewport(FViewport* VP, ID3D11DeviceContext* Ctx)
{
	if (VP->ApplyPendingResize())
	{
		// OnResize 는 액터 컴포넌트(Pawn 카메라) 본질이라 PC->PlayerCameraManager 경유.
		UWorld* World = Game->GetWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APlayerCameraManager* CM = PC ? PC->GetPlayerCameraManager() : nullptr;
		if (UCameraComponent* AC = CM ? CM->GetActiveCamera() : nullptr)
		{
			AC->OnResize(static_cast<int32>(VP->GetWidth()), static_cast<int32>(VP->GetHeight()));
		}
	}
	VP->BeginRender(Ctx);
}

void FGameRenderPipeline::BuildFrame(FViewport* VP, const FMinimalViewInfo& POV, FScene* Scene, UWorld* World)
{
	Frame.ClearViewportResources();
	Frame.SetViewportInfo(VP);

	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CamManager = PC ? PC->GetPlayerCameraManager() : nullptr;

	Frame.CameraFade.bEnabled = CamManager ? CamManager->IsFadeEnabled() : false;
	if (Frame.CameraFade.bEnabled)
	{
		Frame.CameraFade.Color = CamManager->GetFadeColor();
		Frame.CameraFade.Amount = CamManager->GetFadeAmount();
	}

	Frame.CameraVignette.bEnabled = CamManager ? CamManager->IsVignetteEnabled() : false;
	if (Frame.CameraVignette.bEnabled)
	{
		Frame.CameraVignette.Intensity = CamManager->GetVignetteIntensity();
		Frame.CameraVignette.Radius = CamManager->GetVignetteRadius();
		Frame.CameraVignette.Softness = CamManager->GetVignetteSoftness();
		Frame.CameraVignette.Color = CamManager->GetVignetteColor();
	}
	Frame.CameraShockWaves = CamManager ? CamManager->GetWorldShockWaves() : TArray<FCameraShockWaveState>();

	if (CamManager && CamManager->IsDepthOfFieldEnabled())
	{
		FViewportRenderOptions Opts = Frame.RenderOptions;
		Opts.ShowFlags.bDoF = true;
		Opts.DoFFocusDistance = CamManager->GetDoFFocusDistance();
		Opts.DoFFocusRange = CamManager->GetDoFFocusRange();
		Opts.DoFMaxBlurRadius = CamManager->GetDoFMaxBlurRadius();
		Opts.DoFBokehRadiusThreshold = CamManager->GetDoFBokehRadiusThreshold();
		Opts.DoFBokehLumaThreshold = CamManager->GetDoFBokehLumaThreshold();
		Opts.DoFBokehIntensity = CamManager->GetDoFBokehIntensity();
		Frame.SetRenderOptions(Opts);
	}

	Frame.CameraScopeLens.bEnabled = CamManager ? CamManager->IsScopeLensEnabled() : false;
	if (Frame.CameraScopeLens.bEnabled)
	{
		Frame.CameraScopeLens = CamManager->GetScopeLensState();
		FViewportRenderOptions Opts = Frame.RenderOptions;
		Opts.ShowFlags.bScopeLens = true;
		Opts.ScopeLensRadius = Frame.CameraScopeLens.Radius;
		Opts.ScopeLensFeather = Frame.CameraScopeLens.Feather;
		Opts.ScopeLensOuterBlurRadius = Frame.CameraScopeLens.OuterBlurRadius;
		Opts.ScopeLensEdgeBlurRadius = Frame.CameraScopeLens.EdgeBlurRadius;
		Opts.ScopeLensZoomFOV = Frame.CameraScopeLens.ZoomFOV;
		Opts.ScopeLensIntensity = Frame.CameraScopeLens.Intensity;
		Frame.SetRenderOptions(Opts);
	}

	UCameraComponent* ActiveCamera = CamManager ? CamManager->GetActiveCamera() : nullptr;
	if (ActiveCamera)
	{
		const FCameraLetterboxState& LetterboxSettings = ActiveCamera->GetLetterboxSettings();
		Frame.CameraLetterbox.bEnabled = LetterboxSettings.bEnabled;
		if (Frame.CameraLetterbox.bEnabled)
		{
			Frame.CameraLetterbox.Amount = LetterboxSettings.Amount;
			Frame.CameraLetterbox.Thickness = LetterboxSettings.Thickness;
			Frame.CameraLetterbox.Color = LetterboxSettings.Color;
		}
	}
	else
	{
		Frame.CameraLetterbox.bEnabled = false;
	}

	FMinimalViewInfo RenderPOV = POV;
	ApplyLetterboxAspect(RenderPOV, Frame.CameraLetterbox, Frame.ViewportWidth, Frame.ViewportHeight);
	Frame.SetCameraInfo(RenderPOV);

	const UGameViewportClient* ViewportClient = GEngine ? GEngine->GetGameViewportClient() : nullptr;
	const POINT MousePos = (ViewportClient && ViewportClient->HasVirtualCursorPosition())
		? ViewportClient->GetVirtualCursorClientPos()
		: InputSystem::Get().GetMouseClientPos();
	if (MousePos.x >= 0 && MousePos.y >= 0
		&& MousePos.x < static_cast<LONG>(Frame.ViewportWidth)
		&& MousePos.y < static_cast<LONG>(Frame.ViewportHeight))
	{
		Frame.CursorViewportX = static_cast<uint32>(MousePos.x);
		Frame.CursorViewportY = static_cast<uint32>(MousePos.y);
	}
	else
	{
		Frame.CursorViewportX = UINT32_MAX;
		Frame.CursorViewportY = UINT32_MAX;
	}
}

void FGameRenderPipeline::CollectCommands(FScene* Scene, FRenderer& Renderer, FCollectOutput& Output)
{
	FDrawCommandBuilder& Builder = Renderer.GetBuilder();
	Builder.BeginCollect(Frame);

	Collector.Collect(Game->GetWorld(), Frame, Output);
	Builder.BuildCommands(Frame, Scene, Output);
}

void FGameRenderPipeline::RenderScopeLensCapture(FViewport* VP, const FMinimalViewInfo& POV, FScene* Scene, UWorld* World, FRenderer& Renderer, ID3D11DeviceContext* Ctx)
{
	if (!VP || !Scene || !World || !Ctx || !Frame.CameraScopeLens.bEnabled || !Frame.RenderOptions.ShowFlags.bScopeLens || !Frame.ScopeLensRTV)
	{
		return;
	}

	const FFrameContext MainFrame = Frame;
	FFrameContext ScopeFrame = MainFrame;
	ScopeFrame.CameraScopeLens.bEnabled = false;
	ScopeFrame.CameraFade.bEnabled = false;
	ScopeFrame.CameraVignette.bEnabled = false;
	ScopeFrame.CameraLetterbox.bEnabled = false;
	ScopeFrame.bRenderScreenUI = false;
	ScopeFrame.ViewportRTV = MainFrame.ScopeLensRTV;
	ScopeFrame.SceneColorCopySRV = nullptr;
	ScopeFrame.SceneColorCopyTexture = nullptr;
	ScopeFrame.ViewportRenderTexture = nullptr;
	ScopeFrame.ScopeLensSRV = nullptr;
	ScopeFrame.RenderOptions.ShowFlags.bDoF = false;
	ScopeFrame.RenderOptions.ShowFlags.bFXAA = false;
	ScopeFrame.RenderOptions.ShowFlags.bBloom = false;
	ScopeFrame.RenderOptions.ShowFlags.bGammaCorrection = false;
	ScopeFrame.RenderOptions.ShowFlags.bScopeLens = false;

	FMinimalViewInfo ScopePOV = POV;
	ScopePOV.FOV = MainFrame.CameraScopeLens.ZoomFOV;
	ApplyLetterboxAspect(ScopePOV, ScopeFrame.CameraLetterbox, ScopeFrame.ViewportWidth, ScopeFrame.ViewportHeight);
	ScopeFrame.SetCameraInfo(ScopePOV);

	VP->BeginScopeLensRender(Ctx);

	Frame = ScopeFrame;
	FCollectOutput ScopeOutput;
	CollectCommands(Scene, Renderer, ScopeOutput);
	Renderer.Render(Frame, World, *Scene);

	Frame = MainFrame;
	VP->BeginRender(Ctx);
}
