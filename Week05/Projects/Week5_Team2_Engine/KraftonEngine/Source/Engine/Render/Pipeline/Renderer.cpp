#include "Renderer.h"
#include <algorithm>
#include "RenderStats.h"
#include "OcclusionManager.h"
#include "Resource/ResourceManager.h"
#include "Render/Types/RenderTypes.h"
#include "Render/Resource/ConstantBufferPool.h"
#include "Profiling/Stats.h"
#include "Engine/Runtime/Engine.h"
#include "Profiling/Timer.h"
#include "Viewport/Viewport.h"


void FRenderer::Create(HWND hWindow)
{
	Device.Create(hWindow);

	if (Device.GetDevice() == nullptr)
	{
		OutputDebugStringA("Failed to create D3D Device.\n");
	}

	FShaderManager::Get().Initialize(Device.GetDevice());
	FConstantBufferPool::Get().Initialize(Device.GetDevice());
	Resources.Create(Device.GetDevice());

	EditorLineBatcher.Create(Device.GetDevice());
	GridLineBatcher.Create(Device.GetDevice());
	FontBatcher.Create(Device.GetDevice());
	SubUVBatcher.Create(Device.GetDevice());

	InitializePassRenderStates();
	InitializePassBatchers();

	FOcclusionManager::Get().Initialize(Device.GetDevice());

}

void FRenderer::Release()
{
	FOcclusionManager::Get().Release();

	EditorLineBatcher.Release();
	GridLineBatcher.Release();
	FontBatcher.Release();
	SubUVBatcher.Release();

	Resources.Release();
	FConstantBufferPool::Get().Release();
	FShaderManager::Get().Release();
	Device.Release();
}

//	ViewContext에서 Batcher 데이터 수집 (CPU). BeginFrame 이전에 호출.
void FRenderer::PrepareBatchers(const FViewContext& ViewContext)
{
	// --- Editor 패스: AABB 디버그 박스 → EditorLineBatcher ---
	EditorLineBatcher.Clear();
	for (const auto& Entry : ViewContext.GetAABBEntries())
	{
		EditorLineBatcher.AddAABB(FBoundingBox{ Entry.AABB.Min, Entry.AABB.Max }, Entry.AABB.Color);
	}

	// --- Grid 패스: 월드 그리드 + 축 → GridLineBatcher ---
	GridLineBatcher.Clear();
	for (const auto& Proxy : ViewContext.GetGridProxies())
	{
		const FVector CameraPos = ViewContext.GetView().GetInverseFast().GetLocation();
		const FVector& CameraFwd = ViewContext.GetCameraForward();

		GridLineBatcher.AddWorldHelpers(
			ViewContext.GetShowFlags(),
			Proxy.Grid.GridSpacing,
			Proxy.Grid.GridHalfLineCount,
			CameraPos, CameraFwd, ViewContext.IsFixedOrtho());
	}

	// --- Font 패스: 월드 공간 텍스트 → FontBatcher ---
	FontBatcher.Clear();
	for (const auto& Entry : ViewContext.GetFontEntries())
	{
		if (!Entry.Font.Text.empty())
		{
			FontBatcher.AddText(
				Entry.Font.Text,
				Entry.PerObject.Model.GetLocation(),
				ViewContext.GetCameraRight(),
				ViewContext.GetCameraUp(),
				Entry.PerObject.Model.GetScale(),
				Entry.Font.Scale
			);
		}
	}

	// --- OverlayFont 패스: 스크린 공간 텍스트 → FontBatcher ---
	FontBatcher.ClearScreen();
	for (const auto& Entry : ViewContext.GetOverlayFontEntries())
	{
		if (!Entry.Font.Text.empty())
		{
			FontBatcher.AddScreenText(
				Entry.Font.Text,
				Entry.Font.ScreenPosition.X,
				Entry.Font.ScreenPosition.Y,
				ViewContext.GetViewportWidth(),
				ViewContext.GetViewportHeight(),
				Entry.Font.Scale
			);
		}
	}

	// --- SubUV 패스: 스프라이트 → SubUVBatcher (Particle SRV 기준 정렬) ---
	SubUVBatcher.Clear();
	{
		const auto& Entries = ViewContext.GetSubUVEntries();
		SortedSubUVBuffer.assign(Entries.begin(), Entries.end());

		if (SortedSubUVBuffer.size() > 1)
		{
			std::sort(SortedSubUVBuffer.begin(), SortedSubUVBuffer.end(),
				[](const FSubUVEntry& A, const FSubUVEntry& B) {
					return A.SubUV.Particle < B.SubUV.Particle;
				});
		}

		for (const auto& Entry : SortedSubUVBuffer)
		{
			if (Entry.SubUV.Particle)
			{
				SubUVBatcher.AddSprite(
					Entry.SubUV.Particle->SRV,
					Entry.PerObject.Model.GetLocation(),
					ViewContext.GetCameraRight(),
					ViewContext.GetCameraUp(),
					Entry.PerObject.Model.GetScale(),
					Entry.SubUV.FrameIndex,
					Entry.SubUV.Particle->Columns,
					Entry.SubUV.Particle->Rows,
					Entry.SubUV.Width,
					Entry.SubUV.Height
				);
			}
		}
	}
}

void FRenderer::RenderPicking(const FViewContext& InRenderBus, FViewport* InViewport)
{
	if (!InViewport) return;

	ID3D11DeviceContext* Context = Device.GetDeviceContext();
	if (!Context) return;

	// ── Fix: 메인 렌더/HZB/OcclusionTest 이후 잔여 GPU 상태 초기화 ──
	//	NOTE : Depth 관련 정보 등을 초기화하지 않으면 픽킹 렌더링이 실패할 수 있음
	LastBoundMeshBuffer = nullptr;
	LastBoundShader = nullptr;
	LastBoundDiffuseSRV = nullptr;
	Device.ResetDepthStencilCache();

	InViewport->BeginPickingRender(Context);
	UpdateFrameBuffer(Context, InRenderBus);

	FShader* PickingShader = FShaderManager::Get().GetShader(EShaderType::Picking);
	FShader* BillboardPickingShader = FShaderManager::Get().GetShader(EShaderType::BillboardPicking);
	if (!PickingShader) return;

	const ERenderPass PickPasses[] = {
		ERenderPass::Opaque,
		ERenderPass::Billboard,
		ERenderPass::VisualizationBillboard,
		ERenderPass::GizmoOuter,
		ERenderPass::GizmoInner
	};
	Device.SetDepthStencilState(EDepthStencilState::Default);
	Device.SetBlendState(EBlendState::Opaque);
	Device.SetRasterizerState(ERasterizerState::SolidBackCull);
	Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (ERenderPass Pass : PickPasses)
	{
		if (Pass == ERenderPass::Billboard || Pass == ERenderPass::VisualizationBillboard)
		{
			Device.SetDepthStencilState(EDepthStencilState::Default);
			Device.SetRasterizerState(ERasterizerState::SolidNoCull);
		}
		else
		{
			Device.SetRasterizerState(ERasterizerState::SolidBackCull);
		}

		if (Pass == ERenderPass::GizmoInner)
		{
			Device.SetDepthStencilState(EDepthStencilState::GizmoInside);
		}
		else if (Pass == ERenderPass::GizmoOuter)
		{
			Device.SetDepthStencilState(EDepthStencilState::GizmoOutside);
		}
		else
		{
			Device.SetDepthStencilState(EDepthStencilState::Default);
		}

		const FPassQueueSoA& Queue = InRenderBus.GetPassQueue(Pass);
		for (uint32 Idx : Queue.SortedIndices)
		{
			FMeshBuffer* MeshBuffer = Queue.MeshBuffers[Idx];
			if (!MeshBuffer || !MeshBuffer->IsValid() || Queue.PickingIds[Idx] == 0u)
			{
				continue;
			}

			FShader* ActivePickingShader = PickingShader;
			if ((Pass == ERenderPass::Billboard || Pass == ERenderPass::VisualizationBillboard) && BillboardPickingShader)
			{
				ActivePickingShader = BillboardPickingShader;
			}
			ActivePickingShader->Bind(Context);

			if ((Pass == ERenderPass::Billboard || Pass == ERenderPass::VisualizationBillboard) && ActivePickingShader == BillboardPickingShader)
			{
				ID3D11ShaderResourceView* SpriteSRV = Queue.SpriteSRVs[Idx];
				if (!SpriteSRV)
				{
					continue;
				}
				Context->PSSetShaderResources(0, 1, &SpriteSRV);
				Context->PSSetSamplers(0, 1, &Resources.DefaultSampler);
			}

			Resources.PerObjectConstantBuffer.Update(Context, &Queue.Constants[Idx], sizeof(FPerObjectConstants));
			{
				ID3D11Buffer* CB = Resources.PerObjectConstantBuffer.GetBuffer();
				Context->VSSetConstantBuffers(ECBSlot::PerObject, 1, &CB);
			}

			FPickingConstants PickingConstants = {};
			PickingConstants.PickingId = Queue.PickingIds[Idx];
			FConstantBuffer* PickingCB = FConstantBufferPool::Get().GetBuffer(ECBSlot::Picking, sizeof(FPickingConstants));
			PickingCB->Update(Context, &PickingConstants, sizeof(FPickingConstants));
			{
				ID3D11Buffer* CB = PickingCB->GetBuffer();
				Context->PSSetConstantBuffers(ECBSlot::Picking, 1, &CB);
			}

			DrawCommandFromSoA(Context, Queue, Idx);

			if ((Pass == ERenderPass::Billboard || Pass == ERenderPass::VisualizationBillboard) && ActivePickingShader == BillboardPickingShader)
			{
				ID3D11ShaderResourceView* NullSRV = nullptr;
				Context->PSSetShaderResources(0, 1, &NullSRV);
			}
		}
	}
}

//	스왑체인 백버퍼 복귀 — ImGui 합성 직전에 호출
void FRenderer::BeginFrame()
{
	GRenderStatsSnapshot = GRenderStats;
	GRenderStats.Reset();
	LastBoundShader = nullptr;
	LastBoundMeshBuffer = nullptr;
	LastBoundDiffuseSRV = nullptr;

	ID3D11DeviceContext* Context = Device.GetDeviceContext();
	ID3D11RenderTargetView* RTV = Device.GetFrameBufferRTV();
	ID3D11DepthStencilView* DSV = Device.GetDepthStencilView();

	Context->ClearRenderTargetView(RTV, Device.GetClearColor());
	Context->ClearDepthStencilView(DSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);

	const D3D11_VIEWPORT& Viewport = Device.GetViewport();
	Context->RSSetViewports(1, &Viewport);
	Context->OMSetRenderTargets(1, &RTV, DSV);
}

//	RenderBus에 담긴 모든 RenderCommand에 대해서 Draw Call 수행 (GPU)
void FRenderer::Render(const FViewContext& InRenderBus)
{
	// 정렬을 위해 const 캐스트 (가장 저렴한 위치)
	FViewContext& MutableBus = const_cast<FViewContext&>(InRenderBus);

	ID3D11DeviceContext* Context = Device.GetDeviceContext();

	// ── 상태 캐시 및 리소스 바인딩 캐시 초기화 ──
	LastBoundShader = nullptr;
	LastBoundMeshBuffer = nullptr;
	LastBoundDiffuseSRV = nullptr;
	Device.ResetDepthStencilCache();
	Device.ResetBlendCache();
	Device.ResetRasterizerCache();

	UpdateFrameBuffer(Context, InRenderBus);

	// ── 패스 루프 ──
	for (uint32 i = 0; i < (uint32)ERenderPass::MAX; ++i)
	{
		ERenderPass CurPass = static_cast<ERenderPass>(i);

		if (CurPass == ERenderPass::Opaque || CurPass == ERenderPass::Translucent || CurPass == ERenderPass::SelectionMask)
		{
			MutableBus.SortPass(CurPass);
		}

		ApplyPassRenderState(CurPass, Context, InRenderBus.GetViewMode());

		if (PassBatchers[i])
		{
			PassBatchers[i].DrawBatch(CurPass, InRenderBus, Context);
		}
		else
		{
			const FPassQueueSoA& Queue = InRenderBus.GetPassQueue(CurPass);
			if (!Queue.SortedIndices.empty())
			{
				ExecuteDefaultPass(Queue, InRenderBus, Context);
			}
		}

		if (CurPass == ERenderPass::Opaque)
		{
			// DX11 Conflict: Depth buffer cannot be bound as both DSV and SRV.
			// Unbind all RTVs and DSV before building HZB.
			ID3D11RenderTargetView* nullRTV = nullptr;
			Context->OMSetRenderTargets(1, &nullRTV, nullptr);

			FOcclusionManager::Get().BuildHZB(Context, InRenderBus);

			// Rebind the viewport RTV and DSV for subsequent passes (Translucent, etc.)
			ID3D11RenderTargetView* RTV = InRenderBus.GetViewportRTV();
			ID3D11DepthStencilView* DSV = InRenderBus.GetViewportDSV();
			Context->OMSetRenderTargets(1, &RTV, DSV);
		}
	}
}

// ============================================================
// 기본 패스 실행기
// ============================================================
void FRenderer::ExecuteDefaultPass(const FPassQueueSoA& Queue, const FViewContext& Bus, ID3D11DeviceContext* Context)
{
	const auto& GlobalSections = Bus.GetGlobalSectionDraws();

	for (uint32 Idx : Queue.SortedIndices)
	{
		// Bind Shader
		FShader* Shader = Queue.Shaders[Idx];
		if (Shader)
		{
			if (Shader != LastBoundShader)
			{
				++GRenderStats.ShaderBinds;
				Shader->Bind(Context);
				LastBoundShader = Shader;
			}
			else
			{
				++GRenderStats.RedundantShaderBinds;
			}
		}

		uint32 SectionCount = Queue.SectionCount[Idx];

		// Update PerObject CB if no sections
		if (SectionCount == 0)
		{
			Resources.PerObjectConstantBuffer.Update(Context, &Queue.Constants[Idx], sizeof(FPerObjectConstants));
			ID3D11Buffer* cb = Resources.PerObjectConstantBuffer.GetBuffer();
			Context->VSSetConstantBuffers(ECBSlot::PerObject, 1, &cb);
		}

		// Extra CB
		const FConstantBufferBinding& ExtraCB = Queue.ExtraCBs[Idx];
		if (ExtraCB.Buffer)
		{
			ExtraCB.Buffer->Update(Context, ExtraCB.Data, ExtraCB.Size);
			ID3D11Buffer* cb = ExtraCB.Buffer->GetBuffer();
			Context->VSSetConstantBuffers(ExtraCB.Slot, 1, &cb);
			Context->PSSetConstantBuffers(ExtraCB.Slot, 1, &cb);
		}

		// StaticMesh: 섹션별 SRV 바인딩 + 분할 드로우
		// Billboard: SpriteSRV t0 바인딩 후 단일 드로우
		if (SectionCount > 0)
		{
			DrawStaticMeshSectionsFromSoA(Context, Queue, Idx, GlobalSections);
		}
		else
		{
			if (ID3D11ShaderResourceView* SpriteSRV = Queue.SpriteSRVs[Idx])
			{
				Context->PSSetShaderResources(0, 1, &SpriteSRV);
				Context->PSSetSamplers(0, 1, &Resources.DefaultSampler);
			}
			DrawCommandFromSoA(Context, Queue, Idx);
		}
	}
}

void FRenderer::DrawCommandFromSoA(ID3D11DeviceContext* Context, const FPassQueueSoA& Queue, uint32 Idx)
{
	FMeshBuffer* MeshBuffer = Queue.MeshBuffers[Idx];
	if (!MeshBuffer || !MeshBuffer->IsValid()) return;

	// 버텍스 버퍼 바인딩 (캐싱 적용)
	if (MeshBuffer != LastBoundMeshBuffer)
	{
		++GRenderStats.MeshBinds;
		uint32 offset = 0;
		ID3D11Buffer* vertexBuffer = MeshBuffer->GetVertexBuffer().GetBuffer();
		if (!vertexBuffer) return;
		uint32 stride = MeshBuffer->GetVertexBuffer().GetStride();
		Context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

		ID3D11Buffer* indexBuffer = MeshBuffer->GetIndexBuffer().GetBuffer();
		if (indexBuffer)
		{
			Context->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
		}
		LastBoundMeshBuffer = MeshBuffer;
	}
	else
	{
		++GRenderStats.RedundantMeshBinds;
	}

	uint32 vertexCount = MeshBuffer->GetVertexBuffer().GetVertexCount();
	ID3D11Buffer* indexBuffer = MeshBuffer->GetIndexBuffer().GetBuffer();
	if (indexBuffer)
	{
		uint32 indexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
		Context->DrawIndexed(indexCount, 0, 0);
		++GRenderStats.DrawCalls;
		++GRenderStats.DrawIndexedCalls;
		GRenderStats.TrianglesRendered += indexCount / 3;
	}
	else
	{
		Context->Draw(vertexCount, 0);
		++GRenderStats.DrawCalls;
		++GRenderStats.DrawVertexCalls;
		GRenderStats.TrianglesRendered += vertexCount / 3;
	}
}

void FRenderer::DrawStaticMeshSectionsFromSoA(ID3D11DeviceContext* Context, const FPassQueueSoA& Queue, uint32 Idx, const TArray<FMeshSectionDraw>& GlobalSections)
{
	FMeshBuffer* MeshBuffer = Queue.MeshBuffers[Idx];
	if (!MeshBuffer || !MeshBuffer->IsValid()) return;

	// 버텍스 버퍼 바인딩 (캐싱 적용)
	if (MeshBuffer != LastBoundMeshBuffer)
	{
		++GRenderStats.MeshBinds;
		uint32 offset = 0;
		ID3D11Buffer* vertexBuffer = MeshBuffer->GetVertexBuffer().GetBuffer();
		if (!vertexBuffer) return;
		uint32 stride = MeshBuffer->GetVertexBuffer().GetStride();
		Context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

		ID3D11Buffer* indexBuffer = MeshBuffer->GetIndexBuffer().GetBuffer();
		if (!indexBuffer) return;
		Context->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);

		LastBoundMeshBuffer = MeshBuffer;
	}
	else
	{
		++GRenderStats.RedundantMeshBinds;
	}

	Context->PSSetSamplers(0, 1, &Resources.DefaultSampler);

	uint32 SectionStart = Queue.SectionStart[Idx];
	uint32 SectionCount = Queue.SectionCount[Idx];

	for (uint32 s = 0; s < SectionCount; ++s)
	{
		const FMeshSectionDraw& Section = GlobalSections[SectionStart + s];
		if (Section.IndexCount == 0) continue;

		// 섹션별 SRV 바인딩 (캐싱 적용)
		if (Section.DiffuseSRV != LastBoundDiffuseSRV)
		{
			++GRenderStats.SRVChanges;
			ID3D11ShaderResourceView* srv = Section.DiffuseSRV;
			Context->PSSetShaderResources(0, 1, &srv);
			LastBoundDiffuseSRV = Section.DiffuseSRV;
		}
		else
		{
			++GRenderStats.RedundantSRVChanges;
		}

		// 섹션별 DiffuseColor를 PrimitiveColor(b1)에 반영
		FPerObjectConstants SectionConstants = Queue.Constants[Idx];
		SectionConstants.Color = Section.DiffuseColor;
		Resources.PerObjectConstantBuffer.Update(Context, &SectionConstants, sizeof(FPerObjectConstants));
		ID3D11Buffer* cb = Resources.PerObjectConstantBuffer.GetBuffer();
		Context->VSSetConstantBuffers(ECBSlot::PerObject, 1, &cb);

		Context->DrawIndexed(Section.IndexCount, Section.FirstIndex, 0);
		++GRenderStats.DrawCalls;
		++GRenderStats.DrawIndexedCalls;
		GRenderStats.TrianglesRendered += Section.IndexCount / 3;
	}
}

void FRenderer::ApplyPassRenderState(ERenderPass Pass, ID3D11DeviceContext* Context, EViewMode CurViewMode)
{
	const FPassRenderState& State = PassRenderStates[(uint32)Pass];

	ERasterizerState Rasterizer = State.Rasterizer;
	if (State.bWireframeAware && CurViewMode == EViewMode::Wireframe)
	{
		Rasterizer = ERasterizerState::WireFrame;
	}

	Device.SetDepthStencilState(State.DepthStencil);
	Device.SetBlendState(State.Blend);
	Device.SetRasterizerState(Rasterizer);
	Context->IASetPrimitiveTopology(State.Topology);
}

// ============================================================
// 커맨드 바인딩 — 셰이더 + PerObject CB + Extra CB (데이터 드리븐)
// ============================================================
// (BindCommand was removed and its logic inlined into SoA loops)

void FRenderer::EndFrame()
{
	Device.Present();
}

void FRenderer::UpdateFrameBuffer(ID3D11DeviceContext* Context, const FViewContext& InRenderBus)
{
	FFrameConstants frameConstantData = {};
	frameConstantData.View = InRenderBus.GetView();
	frameConstantData.Projection = InRenderBus.GetProj();
	frameConstantData.bIsWireframe = (InRenderBus.GetViewMode() == EViewMode::Wireframe);
	frameConstantData.WireframeColor = InRenderBus.GetWireframeColor();

	if (GEngine && GEngine->GetTimer())
	{
		frameConstantData.Time = static_cast<float>(GEngine->GetTimer()->GetTotalTime());
	}

	Resources.FrameBuffer.Update(Context, &frameConstantData, sizeof(FFrameConstants));
	ID3D11Buffer* b0 = Resources.FrameBuffer.GetBuffer();
	Context->VSSetConstantBuffers(ECBSlot::Frame, 1, &b0);
	Context->PSSetConstantBuffers(ECBSlot::Frame, 1, &b0);
}

// ============================================================
// 패스별 기본 렌더 상태 테이블 초기화
// ============================================================
void FRenderer::InitializePassRenderStates()
{
	using E = ERenderPass;
	auto& S = PassRenderStates;

	//                              DepthStencil                    Blend                Rasterizer                   Topology                                WireframeAware
	S[(uint32)E::Opaque] = { EDepthStencilState::Default,      EBlendState::Opaque,     ERasterizerState::SolidBackCull,  D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true };
	S[(uint32)E::Translucent] = { EDepthStencilState::Default,      EBlendState::AlphaBlend, ERasterizerState::SolidBackCull,  D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
	S[(uint32)E::SelectionMask] = { EDepthStencilState::StencilWrite,  EBlendState::NoColor,    ERasterizerState::SolidNoCull,    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
	S[(uint32)E::PostProcess] = { EDepthStencilState::NoDepth,       EBlendState::AlphaBlend, ERasterizerState::SolidNoCull,    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
	S[(uint32)E::Editor] = { EDepthStencilState::Default,      EBlendState::AlphaBlend, ERasterizerState::SolidBackCull,  D3D11_PRIMITIVE_TOPOLOGY_LINELIST,     true };
	S[(uint32)E::Grid] = { EDepthStencilState::Default,      EBlendState::AlphaBlend, ERasterizerState::SolidBackCull,  D3D11_PRIMITIVE_TOPOLOGY_LINELIST,     false };
	S[(uint32)E::GizmoOuter] = { EDepthStencilState::GizmoOutside, EBlendState::Opaque,     ERasterizerState::SolidBackCull,  D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
	S[(uint32)E::GizmoInner] = { EDepthStencilState::GizmoInside,  EBlendState::AlphaBlend, ERasterizerState::SolidBackCull,  D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
	S[(uint32)E::VisualizationBillboard] = { EDepthStencilState::DepthReadOnly, EBlendState::AlphaBlend, ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
	S[(uint32)E::Font] = { EDepthStencilState::Default,      EBlendState::AlphaBlend, ERasterizerState::SolidBackCull,  D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true };
	S[(uint32)E::OverlayFont] = { EDepthStencilState::NoDepth,      EBlendState::AlphaBlend, ERasterizerState::SolidBackCull,  D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
	S[(uint32)E::SubUV]     = { EDepthStencilState::Default,      EBlendState::AlphaBlend, ERasterizerState::SolidBackCull,  D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true  };
	S[(uint32)E::Billboard] = { EDepthStencilState::DepthReadOnly, EBlendState::AlphaBlend, ERasterizerState::SolidNoCull,    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

// ============================================================
// Pass Batcher DrawBatch 바인딩 초기화
// ============================================================
void FRenderer::InitializePassBatchers()
{
	PassBatchers[(uint32)ERenderPass::Editor] = {
		[this](ERenderPass, const FViewContext&, ID3D11DeviceContext* Ctx) {
			DrawLineBatcher(EditorLineBatcher, Ctx);
		}
	};

	PassBatchers[(uint32)ERenderPass::Grid] = {
		[this](ERenderPass, const FViewContext&, ID3D11DeviceContext* Ctx) {
			DrawLineBatcher(GridLineBatcher, Ctx);
		}
	};

	PassBatchers[(uint32)ERenderPass::Font] = {
		[this](ERenderPass, const FViewContext&, ID3D11DeviceContext* Ctx) {
			const FFontResource* FontRes = FResourceManager::Get().FindFont(FName("Default"));
			FontBatcher.DrawBatch(Ctx, FontRes);
		}
	};

	PassBatchers[(uint32)ERenderPass::OverlayFont] = {
		[this](ERenderPass, const FViewContext&, ID3D11DeviceContext* Ctx) {
			const FFontResource* FontRes = FResourceManager::Get().FindFont(FName("Default"));
			FontBatcher.DrawScreenBatch(Ctx, FontRes);
		}
	};

	PassBatchers[(uint32)ERenderPass::SubUV] = {
		[this](ERenderPass, const FViewContext&, ID3D11DeviceContext* Ctx) {
			SubUVBatcher.DrawBatch(Ctx);
		}
	};

	PassBatchers[(uint32)ERenderPass::PostProcess] = {
		[this](ERenderPass Pass, const FViewContext& Bus, ID3D11DeviceContext* Ctx) {
			DrawPostProcessOutline(Bus, Ctx);
		}
	};
}

// ============================================================
// LineBatcher DrawBatch 공통
// ============================================================
void FRenderer::DrawLineBatcher(FLineBatcher& Batcher, ID3D11DeviceContext* Context)
{
	if (Batcher.GetLineCount() == 0) return;

	FShader* EditorShader = FShaderManager::Get().GetShader(EShaderType::Editor);
	if (EditorShader) EditorShader->Bind(Context);

	Batcher.DrawBatch(Context);
}

// ============================================================
// PostProcess Outline — DSV unbind → StencilSRV bind → Fullscreen Draw
// ============================================================
void FRenderer::DrawPostProcessOutline(const FViewContext& Bus, ID3D11DeviceContext* Context)
{
	ID3D11ShaderResourceView* StencilSRV = Bus.GetViewportStencilSRV();
	ID3D11DepthStencilView* DSV = Bus.GetViewportDSV();
	ID3D11RenderTargetView* RTV = Bus.GetViewportRTV();
	if (!StencilSRV || !RTV) return;

	// SelectionMask 큐가 비어 있으면 선택된 오브젝트 없음 → 스킵
	if (Bus.GetPassQueue(ERenderPass::SelectionMask).SortedIndices.empty()) return;

	// 1) DSV 언바인딩 (StencilSRV와 동시 바인딩 불가)
	Context->OMSetRenderTargets(1, &RTV, nullptr);

	// 2) StencilSRV → PS t0 바인딩
	Context->PSSetShaderResources(0, 1, &StencilSRV);

	// 3) PostProcess 셰이더 바인딩
	FShader* PPShader = FShaderManager::Get().GetShader(EShaderType::OutlinePostProcess);
	if (PPShader) PPShader->Bind(Context);

	// 4) Outline CB (b3) 업데이트
	FConstantBuffer* OutlineCB = FConstantBufferPool::Get().GetBuffer(ECBSlot::PostProcess, sizeof(FOutlinePostProcessConstants));
	FOutlinePostProcessConstants PPConstants;
	PPConstants.OutlineColor = FVector4(1.0f, 0.5f, 0.0f, 1.0f);
	PPConstants.OutlineThickness = 3.0f;
	OutlineCB->Update(Context, &PPConstants, sizeof(PPConstants));
	ID3D11Buffer* cb = OutlineCB->GetBuffer();
	Context->PSSetConstantBuffers(ECBSlot::PostProcess, 1, &cb);

	// 5) Fullscreen Triangle 드로우 (vertex buffer 없이 SV_VertexID 사용)
	Context->IASetInputLayout(nullptr);
	Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	Context->Draw(3, 0);
	++GRenderStats.DrawCalls;
	++GRenderStats.DrawVertexCalls;

	// 6) StencilSRV 언바인딩
	ID3D11ShaderResourceView* nullSRV = nullptr;
	Context->PSSetShaderResources(0, 1, &nullSRV);

	// 7) DSV 재바인딩 (후속 패스에서 뎁스 사용)
	Context->OMSetRenderTargets(1, &RTV, DSV);

	// 8) 렌더러 캐시 초기화 (이후 패스에서 상태 어긋남 방지)
	LastBoundMeshBuffer = nullptr;
	LastBoundShader = nullptr;
}
