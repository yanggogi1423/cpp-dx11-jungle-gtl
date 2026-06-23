#include "Renderer.h"

#include "Render/Types/RenderTypes.h"
#include "Render/Shader/ShaderManager.h"
#include "Core/Logging/Log.h"
#include "Render/Scene/FScene.h"
#include "GameFramework/World.h"
#include "Profiling/Stats/Stats.h"
#include "Profiling/GPUProfiler.h"
#include "Profiling/StartupProfiler.h"
#include "Materials/MaterialManager.h"
#include "Materials/Material.h"
#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"
#include "Viewport/Viewport.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"

#include <utility>

namespace
{
	bool IsEditorIdPickPass(ERenderPass Pass)
	{
		return Pass == ERenderPass::Opaque ||
			Pass == ERenderPass::Transparent ||
			Pass == ERenderPass::EditorIcon;
	}

	bool IsEditorIconIdPickCommand(const FDrawCommand& Cmd)
	{
		return Cmd.Pass == ERenderPass::EditorIcon;
	}

	bool IsMeshIdPickCommand(const FDrawCommand& Cmd)
	{
		const FPrimitiveSceneProxy* Proxy = Cmd.SourceProxy;
		return Proxy &&
			(Proxy->HasProxyFlag(EPrimitiveProxyFlags::StaticMesh) ||
				Proxy->HasProxyFlag(EPrimitiveProxyFlags::SkeletalMesh));
	}

	bool IsEditorIdPickCompatibleCommand(const FDrawCommand& Cmd)
	{
		const FPrimitiveSceneProxy* Proxy = Cmd.SourceProxy;
		if (!Proxy || !Proxy->GetOwnerComponent())
		{
			return false;
		}

		return IsEditorIconIdPickCommand(Cmd) || IsMeshIdPickCommand(Cmd);
	}

	AActor* GetEditorIdPickActor(const FDrawCommand& Cmd)
	{
		const FPrimitiveSceneProxy* Proxy = Cmd.SourceProxy;
		if (!IsEditorIdPickCompatibleCommand(Cmd))
		{
			return nullptr;
		}

		UPrimitiveComponent* Component = Proxy->GetOwnerComponent();
		AActor* Actor = Component ? Component->GetOwner() : nullptr;
		if (!Actor || !Actor->IsVisible())
		{
			return nullptr;
		}
		return Actor;
	}

	FShader* GetEditorIdPickShader(const FDrawCommand& Cmd)
	{
		const char* VSEntry = "VS_StaticMesh";
		if (IsEditorIconIdPickCommand(Cmd))
		{
			VSEntry = "VS_EditorIcon";
		}
		return FShaderManager::Get().GetOrCreate(
			FShaderKey(EShaderPath::EditorIdPick, nullptr, VSEntry, "PS"));
	}

	bool UsesEditorIdPickAlphaTest(const FDrawCommand& Cmd)
	{
		if (!Cmd.Bindings.SRVs[(int)EMaterialTextureSlot::Diffuse])
		{
			return false;
		}

		return Cmd.Pass == ERenderPass::Transparent ||
			Cmd.Pass == ERenderPass::EditorIcon ||
			(Cmd.SourceMaterial && Cmd.SourceMaterial->GetBlendMode() == EBlendMode::Masked);
	}

	void BindEditorIdPickGeometry(ID3D11DeviceContext* Ctx, const FDrawCommand& Cmd)
	{
		if (!Cmd.Buffer.HasBuffers())
		{
			Ctx->IASetInputLayout(nullptr);
			return;
		}

		uint32 Offset = 0;
		Ctx->IASetVertexBuffers(0, 1, &Cmd.Buffer.VB, &Cmd.Buffer.VBStride, &Offset);
		if (Cmd.Buffer.IB)
		{
			Ctx->IASetIndexBuffer(Cmd.Buffer.IB, DXGI_FORMAT_R32_UINT, 0);
		}
	}

	void DrawEditorIdPickGeometry(ID3D11DeviceContext* Ctx, const FDrawCommand& Cmd)
	{
		if (Cmd.Buffer.IndexCount > 0)
		{
			Ctx->DrawIndexed(Cmd.Buffer.IndexCount, Cmd.Buffer.FirstIndex, Cmd.Buffer.BaseVertex);
		}
		else if (Cmd.Buffer.VertexCount > 0)
		{
			Ctx->Draw(Cmd.Buffer.VertexCount, 0);
		}
	}
}


void FRenderer::Create(HWND hWindow)
{
	{
		SCOPE_STARTUP_STAT("  D3DDevice::CreateHW");
		Device.Create(hWindow);
	}

	if (Device.GetDevice() == nullptr)
	{
		UE_LOG("Failed to create D3D Device.");
	}

	{
		SCOPE_STARTUP_STAT("  ShaderManager::Init");
		FShaderManager::Get().Initialize(Device.GetDevice());
	}

	{
		SCOPE_STARTUP_STAT("  SystemResources::Create");
		Resources.Create(Device.GetDevice());
	}

	{
		SCOPE_STARTUP_STAT("  TileCulling::Init");
		Resources.TileBasedCulling.Initialize(Device.GetDevice());
	}

	{
		SCOPE_STARTUP_STAT("  ClusteredCuller::Init");
		Resources.ClusteredLightCuller.Initialize(Device.GetDevice(), Device.GetDeviceContext());
	}

	{
		SCOPE_STARTUP_STAT("  RenderPassPipeline::Init");
		Pipeline.Initialize();
	}

	{
		SCOPE_STARTUP_STAT("  DrawCommandBuilder::Create");
		Builder.Create(Device.GetDevice(), Device.GetDeviceContext(), &Pipeline.GetStateTable());
	}

	// GPU Profiler 초기화
	FGPUProfiler::Get().Initialize(Device.GetDevice(), Device.GetDeviceContext());
}

void FRenderer::Release()
{
	// Break every immediate-context binding before individual resource owners release COM refs.
	// This covers viewport RTV/SRV, shadow maps, tile/cluster culling UAV/SRV, ImGui leftovers,
	// and state objects that may still be cached by the D3D11 immediate context.
	Device.ReleaseImmediateContextBindings(false);

	FGPUProfiler::Get().Shutdown();

	Builder.Release();
	Pipeline.Release();

	Resources.TileBasedCulling.Release();
	Resources.ClusteredLightCuller.Release();
	Resources.Release();
	FShaderManager::Get().Release();
	FMaterialManager::Get().Release();

	// One more detach after managers have released resources, then release the device itself.
	Device.ReleaseImmediateContextBindings(false);
	Device.Release();
}

//	스왑체인 백버퍼 복귀 — ImGui 합성 직전에 호출
void FRenderer::BeginFrame()
{
	Device.BeginFrame();
}

// ============================================================
// Render — 정렬 + GPU 제출
// BeginCollect + Collector + BuildDynamicCommands 이후에 호출.
// ============================================================
void FRenderer::Render(const FFrameContext& Frame, UWorld* World, FScene& Scene)
{
	FDrawCallStats::Reset();

	{
		SCOPE_STAT_CAT("UpdateFrameBuffer", "4_ExecutePass");
		Resources.UpdateFrameBuffer(Device, Frame);
	}
	{
		SCOPE_STAT_CAT("UpdateLightBuffer", "4_ExecutePass");
		Resources.UpdateLightBuffer(Device, Scene, Frame);
	}
	{
		SCOPE_STAT_CAT("UpdateForwardFogBuffer", "4_ExecutePass");
		Resources.UpdateForwardFogBuffer(Device, Scene, Frame);
	}

	// 시스템 샘플러 영구 바인딩 (s0-s2)
	Resources.BindSystemSamplers(Device);

	FDrawCommandList& CommandList = Builder.GetCommandList();

	// 커맨드 정렬 + 패스별 오프셋 빌드
	CommandList.Sort();

	// 단일 StateCache — 패스 간 상태 유지 (DSV Read-Only 전환 등)
	FStateCache Cache;
	Cache.Reset();
	Cache.RTV = Frame.ViewportRTV;
	Cache.DSV = Frame.ViewportDSV;

	FPassContext PassCtx{ Device, Frame, Cache, Resources, CommandList, this, World, &Scene };
	Pipeline.Execute(PassCtx);

	RenderEditorIdPickBuffer(Frame, Scene, CommandList);

	CleanupPassState(Cache);
}

void FRenderer::RenderEditorIdPickBuffer(const FFrameContext& Frame, FScene& Scene, const FDrawCommandList& CommandList)
{
	(void)Scene;

	if (!Frame.SourceViewport || !Frame.bEnableEditorIdPicking)
	{
		return;
	}

	if (!Frame.EditorIdPickRTV || !Frame.EditorIdPickSRV || !Frame.EditorIdPickDebugRTV || !Frame.EditorIdPickDebugSRV)
	{
		TArray<AActor*> EmptyActors;
		Frame.SourceViewport->SetEditorIdPickActors(std::move(EmptyActors));
		return;
	}

	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
	if (!Ctx)
	{
		return;
	}

	TArray<AActor*> IdActors;
	TMap<AActor*, uint32> ActorToPickId;

	auto ResolvePickId = [&ActorToPickId, &IdActors](AActor* Actor) -> uint32
	{
		auto It = ActorToPickId.find(Actor);
		if (It != ActorToPickId.end())
		{
			return It->second;
		}

		const uint32 PickId = static_cast<uint32>(IdActors.size()) + 1;
		ActorToPickId.emplace(Actor, PickId);
		IdActors.push_back(Actor);
		return PickId;
	};

	const float ClearId[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	const float ClearDebug[4] = { 0.02f, 0.02f, 0.025f, 1.0f };
	Ctx->ClearRenderTargetView(Frame.EditorIdPickRTV, ClearId);
	Ctx->ClearRenderTargetView(Frame.EditorIdPickDebugRTV, ClearDebug);
	if (Frame.ViewportDSV)
	{
		Ctx->ClearDepthStencilView(Frame.ViewportDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
	}

	ID3D11RenderTargetView* IdRTV = Frame.EditorIdPickRTV;
	Ctx->OMSetRenderTargets(1, &IdRTV, Frame.ViewportDSV);
	D3D11_VIEWPORT VP = {};
	VP.TopLeftX = 0.0f;
	VP.TopLeftY = 0.0f;
	VP.Width = Frame.ViewportWidth;
	VP.Height = Frame.ViewportHeight;
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	Ctx->RSSetViewports(1, &VP);

	const TArray<FDrawCommand>& Commands = CommandList.GetCommands();
	for (const FDrawCommand& Cmd : Commands)
	{
		if (!IsEditorIdPickPass(Cmd.Pass) || !Cmd.Buffer.HasBuffers() || Cmd.Buffer.InstanceCount > 0)
		{
			continue;
		}

		AActor* Actor = GetEditorIdPickActor(Cmd);
		if (!Actor)
		{
			continue;
		}

		FShader* PickShader = GetEditorIdPickShader(Cmd);
		if (!PickShader || !PickShader->IsValid())
		{
			continue;
		}

		Resources.SetDepthStencilState(Device, Cmd.RenderState.DepthStencil);
		Resources.SetBlendState(Device, EBlendState::Opaque);
		Resources.SetRasterizerState(Device, Cmd.RenderState.Rasterizer);
		Ctx->IASetPrimitiveTopology(Cmd.RenderState.Topology);

		PickShader->Bind(Ctx);
		BindEditorIdPickGeometry(Ctx, Cmd);

		if (Cmd.PerObjectCB)
		{
			ID3D11Buffer* PerObjectRaw = Cmd.PerObjectCB->GetBuffer();
			if (PerObjectRaw)
			{
				Ctx->VSSetConstantBuffers(ECBSlot::PerObject, 1, &PerObjectRaw);
			}
		}

		const bool bUseAlphaTest = UsesEditorIdPickAlphaTest(Cmd);
		FEditorPickingConstants PickingConstants = {};
		PickingConstants.PickingId = ResolvePickId(Actor);
		PickingConstants.bUseAlphaTest = bUseAlphaTest ? 1u : 0u;
		if (IsEditorIconIdPickCommand(Cmd))
		{
			PickingConstants.AlphaCutoff = 0.5f;
		}
		Resources.EditorPickingBuffer.Update(Ctx, &PickingConstants, sizeof(FEditorPickingConstants));
		ID3D11Buffer* PickingCB = Resources.EditorPickingBuffer.GetBuffer();
		Ctx->VSSetConstantBuffers(ECBSlot::EditorPicking, 1, &PickingCB);
		Ctx->PSSetConstantBuffers(ECBSlot::EditorPicking, 1, &PickingCB);

		ID3D11ShaderResourceView* DiffuseSRV = bUseAlphaTest
			? Cmd.Bindings.SRVs[(int)EMaterialTextureSlot::Diffuse]
			: nullptr;
		Ctx->PSSetShaderResources((int)EMaterialTextureSlot::Diffuse, 1, &DiffuseSRV);

		ID3D11ShaderResourceView* SkinMatrixSRV = Cmd.Bindings.SkinMatrixSRV;
		Ctx->VSSetShaderResources(EVertexFactoryTexSlot::SkinMatrices, 1, &SkinMatrixSRV);

		DrawEditorIdPickGeometry(Ctx, Cmd);
	}

	Frame.SourceViewport->SetEditorIdPickActors(std::move(IdActors));

	ID3D11ShaderResourceView* NullSRV = nullptr;
	ID3D11Buffer* NullCB = nullptr;
	Ctx->PSSetShaderResources((int)EMaterialTextureSlot::Diffuse, 1, &NullSRV);
	Ctx->VSSetShaderResources(EVertexFactoryTexSlot::SkinMatrices, 1, &NullSRV);
	Ctx->VSSetConstantBuffers(ECBSlot::EditorPicking, 1, &NullCB);
	Ctx->PSSetConstantBuffers(ECBSlot::EditorPicking, 1, &NullCB);

	ID3D11RenderTargetView* DebugRTV = Frame.EditorIdPickDebugRTV;
	Ctx->OMSetRenderTargets(1, &DebugRTV, nullptr);
	Resources.SetDepthStencilState(Device, EDepthStencilState::NoDepth);
	Resources.SetBlendState(Device, EBlendState::Opaque);
	Resources.SetRasterizerState(Device, ERasterizerState::SolidNoCull);
	Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	FShader* DebugShader = FShaderManager::Get().GetOrCreate(
		FShaderKey(EShaderPath::EditorIdPickDebug, nullptr, "VS", "PS"));
	if (DebugShader && DebugShader->IsValid())
	{
		DebugShader->Bind(Ctx);
		ID3D11ShaderResourceView* IdSRV = Frame.EditorIdPickSRV;
		Ctx->PSSetShaderResources(0, 1, &IdSRV);
		Ctx->Draw(3, 0);
		Ctx->PSSetShaderResources(0, 1, &NullSRV);
	}

	ID3D11RenderTargetView* ViewportRTV = Frame.ViewportRTV;
	Ctx->OMSetRenderTargets(1, &ViewportRTV, Frame.ViewportDSV);
}

// ============================================================
// CleanupPassState — 패스 루프 종료 후 시스템 텍스처 언바인딩 + 캐시 정리
// ============================================================
void FRenderer::CleanupPassState(FStateCache& Cache)
{
	Resources.UnbindSystemTextures(Device);
	Resources.UnbindTileCullingBuffers(Device);
	Resources.UnbindClusterCullingResources(Device);

	Cache.Cleanup(Device.GetDeviceContext());
	Builder.GetCommandList().Reset();
}

void FRenderer::SubmitCullingDebugLines(UWorld* World)
{
	Resources.SubmitCullingDebugLines(Device.GetDeviceContext(), World);
}

//	Present the rendered frame to the screen. 반드시 Render 이후에 호출되어야 함.
void FRenderer::EndFrame()
{
	Device.Present();
}

void FRenderer::BlitToBackBuffer(ID3D11ShaderResourceView* SourceSRV)
{
	if (!SourceSRV)
	{
		return;
	}

	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();

	FShader* Shader = FShaderManager::Get().FindOrCreate(EShaderPath::Blit);
	Shader->Bind(Ctx);

	Ctx->PSSetShaderResources(0, 1, &SourceSRV);

	Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Ctx->Draw(3, 0);

	ID3D11ShaderResourceView* NullSRV[1] = { nullptr };
	Ctx->PSSetShaderResources(0, 1, NullSRV);
}
