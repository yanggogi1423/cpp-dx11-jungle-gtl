#include "EditorRenderPipeline.h"
#include "Editor/EditorEngine.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Render/Pipeline/Renderer.h"
#include "Render/Pipeline/SceneRenderSetup.h"
#include "Viewport/Viewport.h"
#include "Components/CameraComponent.h"
#include "Components/GizmoComponent.h"
#include "GameFramework/DecalActor.h"
#include "GameFramework/ProjectionDecalActor.h"
#include "GameFramework/World.h"
#include "Profiling/Stats.h"
#include "Profiling/GPUProfiler.h"
#include "Render/Proxy/DecalSceneProxy.h"

FEditorRenderPipeline::FEditorRenderPipeline(UEditorEngine* InEditor, FRenderer& InRenderer)
	: Editor(InEditor)
{
}

FEditorRenderPipeline::~FEditorRenderPipeline()
{
}

void FEditorRenderPipeline::OnSceneCleared()
{
	GPUOcclusion.InvalidateResults();
}

void FEditorRenderPipeline::Execute(float DeltaTime, FRenderer& Renderer)
{
#if STATS
	FStatManager::Get().TakeSnapshot();
	FGPUProfiler::Get().TakeSnapshot();
	FGPUProfiler::Get().BeginFrame();
#endif

	for (FLevelEditorViewportClient* ViewportClient : Editor->GetLevelViewportClients())
	{
		SCOPE_STAT_CAT("RenderViewport", "2_Render");
		RenderViewport(ViewportClient, Renderer);
	}
	// 뷰포트별 오프스크린 렌더 (각 VP의 RT에 3D 씬 렌더)

	// 스왑체인 백버퍼 복귀 → ImGui 합성 → Present
	Renderer.BeginFrame();
	{
		SCOPE_STAT_CAT("EditorUI", "5_UI");
		Editor->RenderUI(DeltaTime);
	}

#if STATS
	FGPUProfiler::Get().EndFrame();
#endif

	{
		SCOPE_STAT_CAT("Present", "2_Render");
		Renderer.EndFrame();
	}
}

void FEditorRenderPipeline::RenderViewport(FLevelEditorViewportClient* VC, FRenderer& Renderer)
{
	UCameraComponent* Camera = VC->GetCamera();
	if (!Camera) return;

	FViewport* VP = VC->GetViewport();
	if (!VP) return;

	ID3D11DeviceContext* Ctx = Renderer.GetFD3DDevice().GetDeviceContext();
	if (!Ctx) return;

	UWorld* World = Editor->GetWorld();
	if (!World) return;
	TArray<FPrimitiveSceneProxy*> VisibleProxiesForViewport;
	World->GatherVisibleProxiesForCamera(Camera, VisibleProxiesForViewport);

	// GPU Occlusion readback/result는 현재 전역 상태를 공유하므로
	// 멀티 뷰포트에서 카메라별 결과가 섞여 아티팩트가 발생할 수 있다.
	// 멀티 뷰포트에서는 occlusion을 비활성화하고 frustum culling만 사용한다.
	const bool bEnableGPUOcclusion = !Editor->IsSplitViewport();

	// GPU Occlusion 지연 초기화
	if (bEnableGPUOcclusion && !GPUOcclusion.IsInitialized())
		GPUOcclusion.Initialize(Renderer.GetFD3DDevice().GetDevice());

	// 이전 프레임 Occlusion 결과 읽기 (staging → OccludedSet)
	if (bEnableGPUOcclusion)
	{
		GPUOcclusion.ReadbackResults(Ctx);
	}

	// 뷰포트별 렌더 옵션 사용
	const FViewportRenderOptions& Opts = VC->GetRenderOptions();
	FShowFlags EffectiveShowFlags = Opts.ShowFlags;
	const bool bPIEPossessedMode =
		Editor->IsPlayingInEditor()
		&& Editor->GetPIEControlMode() == UEditorEngine::EPIEControlMode::Possessed;
	if (bPIEPossessedMode)
	{
		EffectiveShowFlags.bSelectionOutline = false;
		EffectiveShowFlags.bBoundingVolume = false;
		EffectiveShowFlags.bDebugDraw = false;
		EffectiveShowFlags.bOctree = false;
		// Possessed PIE에서는 에디터용 Billboard/UUID(TextRender) 오버레이를 숨긴다.
		EffectiveShowFlags.bBillboardText = false;
	}
	EViewMode ViewMode = Opts.ViewMode;

	// 지연 리사이즈 적용 + 오프스크린 RT 바인딩
	if (VP->ApplyPendingResize())
	{
		Camera->OnResize(static_cast<int32>(VP->GetWidth()), static_cast<int32>(VP->GetHeight()));
	}

	// 렌더 시작 (RT 클리어 + DSV 바인딩)
	VP->BeginRender(Ctx);

	// 1. Bus 수집
	Bus.Clear();

	Bus.SetCameraInfo(Camera);
	Bus.SetRenderSettings(ViewMode, EffectiveShowFlags);
	PopulateScenePostProcessConstants(World, Bus);
	Bus.SetViewportInfo(VP);
	Bus.SetViewportType(Opts.ViewportType);
	Bus.SetOcclusionCulling(bEnableGPUOcclusion ? &GPUOcclusion : nullptr);
	Bus.SetLODContext(World->PrepareLODContextForCamera(Camera));

	// 2. 프록시 + Batcher Entry를 ERenderPass별로 수집
	{
		SCOPE_STAT_CAT("Collector", "3_Collect");
		// Gizmo axis mask must be updated before per-viewport proxy collection.
		// Otherwise gizmo proxy can read stale mask from a previous viewport/frame.
		if (UGizmoComponent* Gizmo = Editor->GetGizmo())
			Gizmo->UpdateAxisMask(Opts.ViewportType, Camera->IsOrthogonal());

		Collector.CollectVisibleList(World, VisibleProxiesForViewport, Bus);

		Collector.CollectGrid(Opts.GridSpacing, Opts.GridHalfLineCount, Bus);
		Collector.CollectDebugDraw(World->GetDebugDrawQueue(), Bus);

		if (EffectiveShowFlags.bDebugDraw)
		{
			for (AActor* SelectedActor : Editor->GetSelectionManager().GetSelectedActors())
			{
				if (!SelectedActor || SelectedActor->GetWorld() != World)
				{
					continue;
				}

				for (UActorComponent* ActorComponent : SelectedActor->GetComponents())
				{
					if (!ActorComponent)
					{
						continue;
					}

					ActorComponent->CollectEditorVisualizations(Bus);
				}
			}
		}

		if (EffectiveShowFlags.bOctree)
			Collector.CollectOctreeDebug(World->GetOctree(), Bus);

	}

	{
		uint32 DecalActorCount = 0;
		uint32 ProjectionDecalActorCount = 0;
		for (AActor* Actor : World->GetActors())
		{
			if (Cast<ADecalActor>(Actor))
			{
				++DecalActorCount;
			}
			else if (Cast<AProjectionDecalActor>(Actor))
			{
				++ProjectionDecalActorCount;
			}
		}

     const TArray<const FPrimitiveSceneProxy*>& RenderedDecalProxies = Bus.GetProxies(ERenderPass::Decal);
     const TArray<const FPrimitiveSceneProxy*>& RenderedProjectionDecalProxies = Bus.GetProxies(ERenderPass::ProjectionDecal);
		uint32 AffectedObjectCountSum = 0;
		uint32 AffectedObjectCountMax = 0;
        for (const FPrimitiveSceneProxy* Proxy : RenderedDecalProxies)
		{
			if (!Proxy)
			{
				continue;
			}

           const FDecalSceneProxy* DecalProxy = static_cast<const FDecalSceneProxy*>(Proxy);
			const uint32 OverlapCount = DecalProxy->GetLastOverlappingObjectCount();
			AffectedObjectCountSum += OverlapCount;
			if (OverlapCount > AffectedObjectCountMax)
			{
				AffectedObjectCountMax = OverlapCount;
			}
		}

		uint32 ProjectionDecalVertexCount = 0;
		uint32 ProjectionDecalTriangleCount = 0;
		uint32 ProjectionDecalSectionCount = 0;
		for (const FPrimitiveSceneProxy* Proxy : RenderedProjectionDecalProxies)
		{
			if (!Proxy || !Proxy->MeshBuffer)
			{
				continue;
			}

			ProjectionDecalVertexCount += Proxy->MeshBuffer->GetVertexBuffer().GetVertexCount();
			const uint32 IndexCount = Proxy->MeshBuffer->GetIndexBuffer().GetIndexCount();
			ProjectionDecalTriangleCount += IndexCount / 3u;
			ProjectionDecalSectionCount += static_cast<uint32>(Proxy->SectionDraws.size());
		}

		FDecalStats::SetDecalActorCount(DecalActorCount);
		FDecalStats::SetProjectionDecalActorCount(ProjectionDecalActorCount);
		FDecalStats::SetRenderedDecalCount(static_cast<uint32>(RenderedDecalProxies.size()));
		FDecalStats::SetRenderedProjectionDecalCount(static_cast<uint32>(RenderedProjectionDecalProxies.size()));
		FDecalStats::SetAffectedObjectCountSum(AffectedObjectCountSum);
		FDecalStats::SetAffectedObjectCountMax(AffectedObjectCountMax);
		FDecalStats::SetProjectionDecalVertexCount(ProjectionDecalVertexCount);
		FDecalStats::SetProjectionDecalTriangleCount(ProjectionDecalTriangleCount);
		FDecalStats::SetProjectionDecalSectionCount(ProjectionDecalSectionCount);
	}

	// 3. Batcher 준비
	{
		SCOPE_STAT_CAT("PrepareBatcher", "3_Collect");
		Renderer.PrepareBatchers(Bus);
	}

	// 4. GPU 드로우 콜 실행
	{
		SCOPE_STAT_CAT("Renderer.Render", "4_ExecutePass");
		Renderer.Render(Bus);
	}

	Renderer.RenderIdPickBuffer(
		Bus,
		VP->GetIdPickRTV(),
		VP->GetDSV(),
		VP->GetIdPickSRV(),
		VP->GetIdPickDebugRTV());

	// 5. GPU Occlusion — DSV 언바인딩 후 Hi-Z 생성 + Occlusion Test 디스패치
	if (bEnableGPUOcclusion && GPUOcclusion.IsInitialized())
	{
		SCOPE_STAT_CAT("GPUOcclusion", "4_ExecutePass");

		// DSV 언바인딩 (DepthSRV 읽기와 동시 바인딩 불가)
		ID3D11RenderTargetView* rtv = VP->GetRTV();
		Ctx->OMSetRenderTargets(1, &rtv, nullptr);

		GPUOcclusion.DispatchOcclusionTest(
			Ctx,
			VP->GetDepthSRV(),
			World->GetVisibleProxies(),
			Bus.GetView(), Bus.GetProj(),
			VP->GetWidth(), VP->GetHeight());
	}
}

