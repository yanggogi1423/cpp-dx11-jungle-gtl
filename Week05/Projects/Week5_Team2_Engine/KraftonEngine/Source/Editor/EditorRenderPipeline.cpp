#include "EditorRenderPipeline.h"
#include "Editor/EditorEngine.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Editor/Selection/PickingTypes.h"
#include "Render/Pipeline/Renderer.h"
#include "Render/Pipeline/FrustumCulling.h"
#include "Render/Pipeline/OcclusionCulling.h"
#include "Render/Pipeline/OcclusionManager.h"
#include "Render/Pipeline/WorldRenderProxy.h"
#include "Viewport/Viewport.h"
#include "Editor/Input/EditorViewportController.h"
#include "Editor/Viewport/ViewportCamera.h"
#include "GameFramework/World.h"
#include "Profiling/Stats.h"

FEditorRenderPipeline::FEditorRenderPipeline(UEditorEngine* InEditor, FRenderer& InRenderer)
	: Editor(InEditor)
{
}

FEditorRenderPipeline::~FEditorRenderPipeline()
{
}

void FEditorRenderPipeline::Execute(float DeltaTime, FRenderer& Renderer)
{
#if STATS
	FStatManager::Get().TakeSnapshot();
#endif

	// 뷰포트별 오프스크린 렌더 (각 VP의 RT에 3D 씬 렌더)
	for (FLevelEditorViewportClient* VC : Editor->GetLevelViewportClients())
	{
		RenderViewport(VC, Renderer);
	}

	// 스왑체인 백버퍼 복귀 → ImGui 합성 → Present
	Renderer.BeginFrame();
	Editor->RenderUI(DeltaTime);

	Renderer.EndFrame();
}

void FEditorRenderPipeline::RenderViewport(FLevelEditorViewportClient* VC, FRenderer& Renderer)
{
	FViewportCamera* Camera = VC->GetCamera();
	if (!Camera) return;

	FViewport* VP = VC->GetViewport();
	if (!VP || VP->GetWidth() == 0 || VP->GetHeight() == 0) return;

	ID3D11DeviceContext* Ctx = Renderer.GetFD3DDevice().GetDeviceContext();
	if (!Ctx) return;

	UWorld* World = Editor->GetWorld();
	if (!World) return;

	// 뷰포트별 렌더 옵션 사용
	FViewportRenderOptions EffectiveOpts = VC->GetRenderOptions();

	const bool bPIEPossessedViewport =
		Editor->IsPIEEnabled()
		&& Editor->GetPIEControlMode() == UEditorEngine::EPIEControlMode::Possessed
		&& VC == Editor->GetPIEEntryViewportClient();
	if (bPIEPossessedViewport)
	{
		// PIE Possessed에서는 편집 디버그 오버레이를 숨긴다.
		EffectiveOpts.ShowFlags.bGizmo = false;
		EffectiveOpts.ShowFlags.bBoundingVolume = false;
	}

	const FShowFlags& ShowFlags = EffectiveOpts.ShowFlags;
	EViewMode ViewMode = EffectiveOpts.ViewMode;

	// 지연 리사이즈 적용 + 오프스크린 RT 바인딩
	if (VP->ApplyPendingResize())
	{
		Camera->OnResize(static_cast<int32>(VP->GetWidth()), static_cast<int32>(VP->GetHeight()));
	}

	// 렌더 시작 (RT 클리어 + DSV 바인딩)
	VP->BeginRender(Ctx);

	FOcclusionManager::Get().ResetStats();

	// 1. ViewContext 수집
	ViewContext.Clear();

	ViewContext.SetCameraInfo(
		Camera->GetViewMatrix(),
		Camera->GetProjectionMatrix(),
		Camera->GetForwardVector(),
		Camera->GetRightVector(),
		Camera->GetUpVector(),
		Camera->IsOrthogonal(),
		Camera->GetOrthoWidth());
	ViewContext.SetRenderOptions(EffectiveOpts);
	ViewContext.SetViewportInfo(VP);

	// 2. RenderCommand(DefaultPass), Entry(Batcher)를 ERenderPass별로 수집
	const TArray<AActor*>& SelectedActors = Editor->GetSelectionManager().GetSelectedActors();
	const TArray<AActor*> EmptySelectedActors;
	const TArray<AActor*>& EffectiveSelectedActors = bPIEPossessedViewport ? EmptySelectedActors : SelectedActors;
	if (ULevel* PersistentLevel = World->GetPersistentLevel())
	{
		PersistentLevel->GetRenderProxy().GatherCandidates(ViewContext);
	}
	if (ULevel* ActiveLevel = World->GetActiveLevel())
	{
		ActiveLevel->GetRenderProxy().GatherCandidates(ViewContext);
	}

	// 3. 오클루전 테스트를 위해 프러스텀 컬링만 통과한 전체 후보군을 따로 보관
	TArray<FPrimitiveProxy*> AllFrustumVisible = ViewContext.GetCandidateProxies();

	OcclusionCulling::ApplyOcclusionCulling(ViewContext);

	// Gizmo / Selected Actor는 컬링 결과와 무관하게 항상 렌더 후보로 유지한다.
	if (ULevel* PersistentLevel = World->GetPersistentLevel())
	{
		PersistentLevel->GetRenderProxy().InjectAlwaysVisibleCandidates(ViewContext, EffectiveSelectedActors, true);
	}
	if (ULevel* ActiveLevel = World->GetActiveLevel())
	{
		ActiveLevel->GetRenderProxy().InjectAlwaysVisibleCandidates(ViewContext, EffectiveSelectedActors, false);
	}

	// 4. 컬링을 통과한 후보군만 RenderCommand를 ViewContext에 제출
	if (ULevel* PersistentLevel = World->GetPersistentLevel())
	{
		PersistentLevel->GetRenderProxy().SubmitRenderCommands(ViewContext, EffectiveSelectedActors);
	}
	if (ULevel* ActiveLevel = World->GetActiveLevel())
	{
		ActiveLevel->GetRenderProxy().SubmitRenderCommands(ViewContext, EffectiveSelectedActors);
	}
	// 4-1. 에디터 오브젝트는 컬링 적용하지 않고 수집
	ViewContext.CollectViewElements();
	
	// 5. Frustum Culling을 통과한 것들에 오클루전 테스트 실행 (이전 프레임에서의 HZB 사용)
	if (ShowFlags.bOcclusionCulling)
	{
		FOcclusionManager::Get().ExecuteOcclusionTest(Ctx, ViewContext, AllFrustumVisible);
	}

	// 6. Batcher 사용하는 렌더객체 처리
	Renderer.PrepareBatchers(ViewContext);

	// 7. ViewContext에 담긴 커맨드와 엔트리를 기반으로 GPU 드로우 콜 실행
	Renderer.Render(ViewContext);

	// 8. IDBuffer 방식의 picking 지원
	if (Editor->GetPickingMode() == EPickingMode::IDBuffer && VC->IsActive())
	{
		FEditorViewportController* InputController = VC->GetInputController();
		if (!InputController)
		{
			return;
		}

		if (InputController->HasPendingIdPickReadback())
		{
			uint32 PickedId = 0u;
			bool bReady = false;
			const bool bPollOk = VP->TryReadPickingIdReadback(Ctx, InputController->GetPendingIdPickReadbackRequestId(), PickedId, bReady, nullptr);
			if (!bPollOk)
			{
				InputController->CancelPendingIdPickReadback();
				InputController->SetIdPickResult(0u);
			}
			else if (bReady)
			{
				InputController->SetIdPickResult(PickedId);
			}
			return;
		}

		if (InputController->HasPendingIdPickRequest())
		{
			uint32 PickX = 0;
			uint32 PickY = 0;
			InputController->GetPendingIdPickCoord(PickX, PickY);

			Renderer.RenderPicking(ViewContext, VP);

			uint32 RequestId = 0u;
			const bool bEnqueueOk = VP->EnqueuePickingIdReadback(Ctx, PickX, PickY, RequestId);
			if (!bEnqueueOk)
			{
				InputController->SetIdPickResult(0u);
				return;
			}

			uint32 PickedId = 0u;
			bool bReady = false;
			const bool bPollOk = VP->TryReadPickingIdReadback(Ctx, RequestId, PickedId, bReady, nullptr);

			if (!bPollOk)
			{
				InputController->SetIdPickResult(0u);
			}
			else if (bReady)
			{
				InputController->SetIdPickResult(PickedId);
			}
			else
			{
				InputController->BeginPendingIdPickReadback(RequestId);
			}
			return;
		}
	}
}

void FEditorRenderPipeline::Reset()
{
	ViewContext.Reset();
}
