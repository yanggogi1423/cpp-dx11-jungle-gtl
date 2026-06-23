#include "Renderer.h"

#include <iostream>
#include <algorithm>
#include <utility>
#include "Resource/ResourceManager.h"
#include "Render/Types/RenderTypes.h"
#include "Render/Resource/ConstantBufferPool.h"
#include "Profiling/Stats.h"
#include "Profiling/GPUProfiler.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Runtime/Engine.h"
#include "Profiling/Timer.h"

namespace
{
	template <typename T>
	void SafeRelease(T*& Resource)
	{
		if (Resource)
		{
			Resource->Release();
			Resource = nullptr;
		}
	}
}

void FRenderer::Create(HWND hWindow)
{
	Device.Create(hWindow);

	if (Device.GetDevice() == nullptr)
	{
		std::cout << "Failed to create D3D Device." << std::endl;
	}

	FShaderManager::Get().Initialize(Device.GetDevice());
	FConstantBufferPool::Get().Initialize(Device.GetDevice());
	Resources.Create(Device.GetDevice());

	EditorLineBatcher.Create(Device.GetDevice());
	GridLineBatcher.Create(Device.GetDevice());
	FontBatcher.Create(Device.GetDevice());
	SubUVBatcher.Create(Device.GetDevice());
	BillboardBatcher.Create(Device.GetDevice());
	IdPickPerObjectCB.Create(Device.GetDevice(), sizeof(FPerObjectConstants));
	{
		D3D11_SAMPLER_DESC SampDesc = {};
		SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		SampDesc.MinLOD = 0.0f;
		SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
		Device.GetDevice()->CreateSamplerState(&SampDesc, &IdPickSampler);
	}

	InitializePassRenderStates();
	InitializePassBatchers();
	
	//	PostEffect 등록 API를 통해 연결한다.
	RegisterPostEffect(EPostEffectType::Outline,
		[this](const FRenderBus& Bus, ID3D11DeviceContext* Context, FPostProcessIO& IO)
		{
			DrawPostProcessOutline(Bus, Context, IO.ColorInput, IO.ColorOutput);
		});

	RegisterPostEffect(EPostEffectType::Fog,
		[this](const FRenderBus& Bus, ID3D11DeviceContext* Context, FPostProcessIO& IO)
		{
			DrawPostProcessFog(Bus, Context, IO);
		});

	RegisterPostEffect(EPostEffectType::FXAA,
		[this](const FRenderBus& Bus, ID3D11DeviceContext* Context, FPostProcessIO& IO)
		{
			DrawPostProcessFXAA(Bus, Context, IO.ColorInput, IO.ColorOutput);
		});
	

	//SetPostEffectEnabled(EPostEffectType::Decal, true);
	SetPostEffectEnabled(EPostEffectType::Fog, true);
	SetPostEffectEnabled(EPostEffectType::Outline, true);
	SetPostEffectEnabled(EPostEffectType::FXAA, true);

	// GPU Profiler 초기화
	FGPUProfiler::Get().Initialize(Device.GetDevice(), Device.GetDeviceContext());
}

void FRenderer::Release()
{
	FGPUProfiler::Get().Shutdown();

	EditorLineBatcher.Release();
	GridLineBatcher.Release();
	FontBatcher.Release();
	SubUVBatcher.Release();
	BillboardBatcher.Release();
	IdPickPerObjectCB.Release();
	SafeRelease(IdPickSampler);

	for (FConstantBuffer& CB : PerObjectCBPool)
	{
		CB.Release();
	}
	PerObjectCBPool.clear();
	ReleasePostProcessTargets();
	for (auto& Effect : PostEffects)
	{
		Effect.Callback = nullptr;
		Effect.bEnabled = false;
	}

	Resources.Release();
	FConstantBufferPool::Get().Release();
	FShaderManager::Get().Release();
	Device.Release();
}

//	Bus → Batcher 데이터 수집 (CPU). BeginFrame 이전에 호출.
void FRenderer::PrepareBatchers(const FRenderBus& Bus)
{
	// --- Editor 패스: AABB 디버그 박스 + DebugDraw 라인 → EditorLineBatcher ---
	EditorLineBatcher.Clear();
	for (const auto& Entry : Bus.GetAABBEntries())
	{
		EditorLineBatcher.AddAABB(FBoundingBox{ Entry.AABB.Min, Entry.AABB.Max }, Entry.AABB.Color);
	}
	for (const auto& Entry : Bus.GetOBBEntries())
	{
		EditorLineBatcher.AddOBB(Entry.OBB.LocalBox, Entry.OBB.Transform, Entry.OBB.Color);
	}
	for (const auto& Entry : Bus.GetDebugLineEntries())
	{
		EditorLineBatcher.AddLine(Entry.Start, Entry.End, Entry.Color.ToVector4());
	}

	// --- Grid 패스: 월드 그리드 + 축 → GridLineBatcher ---
	GridLineBatcher.Clear();
	for (const auto& Entry : Bus.GetGridEntries())
	{
		const FVector CameraPos = Bus.GetView().GetInverseFast().GetLocation();
		FVector CameraFwd = Bus.GetCameraRight().Cross(Bus.GetCameraUp());
		CameraFwd.Normalize();

		GridLineBatcher.AddWorldHelpers(
			Bus.GetShowFlags(),
			Entry.Grid.GridSpacing,
			Entry.Grid.GridHalfLineCount,
			CameraPos, CameraFwd, Bus.IsFixedOrtho());
	}

	// --- Font 패스: 월드 공간 텍스트 → FontBatcher ---
	FontBatcher.Clear();
	for (const auto& Entry : Bus.GetFontEntries())
	{
		if (!Entry.Font.Text.empty())
		{
			FontBatcher.AddText(
				Entry.Font.Text,
				Entry.PerObject.Model.GetLocation(),
				Bus.GetCameraRight(),
				Bus.GetCameraUp(),
				Entry.PerObject.Model.GetScale(),
				Entry.Font.Scale,
				Entry.bSelected
			);
		}
	}

	// --- OverlayFont 패스: 스크린 공간 텍스트 → FontBatcher ---
	FontBatcher.ClearScreen();
	for (const auto& Entry : Bus.GetOverlayFontEntries())
	{
		if (!Entry.Font.Text.empty())
		{
			FontBatcher.AddScreenText(
				Entry.Font.Text,
				Entry.Font.ScreenPosition.X,
				Entry.Font.ScreenPosition.Y,
				Bus.GetViewportWidth(),
				Bus.GetViewportHeight(),
				Entry.Font.Scale
			);
		}
	}

	// --- SubUV 패스: 스프라이트 → SubUVBatcher (Particle SRV 기준 정렬) ---
	SubUVBatcher.Clear();
	{
		const auto& Entries = Bus.GetSubUVEntries();
		SortedSubUVBuffer.clear();
		SortedSubUVBuffer.insert(SortedSubUVBuffer.end(), Entries.begin(), Entries.end());

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
					Bus.GetCameraRight(),
					Bus.GetCameraUp(),
					Entry.PerObject.Model.GetScale(),
					Entry.SubUV.FrameIndex,
					Entry.SubUV.Columns,
					Entry.SubUV.Rows,
					Entry.SubUV.Width,
                  Entry.SubUV.Height,
					Entry.bSelected
				);
			}
		}
	}

	// --- Billboard 패스: 컬러 텍스처 quad → BillboardBatcher (Texture SRV 기준 정렬) ---
	BillboardBatcher.Clear();
	{
		const auto& Entries = Bus.GetBillboardEntries();
		SortedBillboardBuffer.clear();
		SortedBillboardBuffer.insert(SortedBillboardBuffer.end(), Entries.begin(), Entries.end());

		if (SortedBillboardBuffer.size() > 1)
		{
			std::sort(SortedBillboardBuffer.begin(), SortedBillboardBuffer.end(),
				[](const FBillboardEntry& A, const FBillboardEntry& B) {
					return A.Billboard.Texture < B.Billboard.Texture;
				});
		}

		for (const auto& Entry : SortedBillboardBuffer)
		{
			if (Entry.Billboard.Texture)
			{
				// Billboard visual size is authored by Width/Height only.
				// Do not feed PerObject matrix scale here, otherwise ID-pass scale compensation
				// leaks into color rendering and causes pick/visual size mismatch.
				const FVector BillboardVisualScale(1.0f, 1.0f, 1.0f);
				BillboardBatcher.AddSprite(
					Entry.Billboard.Texture->SRV,
					Entry.PerObject.Model.GetLocation(),
					Bus.GetCameraRight(),
					Bus.GetCameraUp(),
					BillboardVisualScale,
					Entry.Billboard.Width,
                  Entry.Billboard.Height,
					Entry.bSelected
				);
			}
		}
	}
}

//	스왑체인 백버퍼 복귀 — ImGui 합성 직전에 호출
void FRenderer::BeginFrame()
{
	ID3D11DeviceContext* Context = Device.GetDeviceContext();
	ID3D11RenderTargetView* RTV = Device.GetFrameBufferRTV();
	ID3D11DepthStencilView* DSV = Device.GetDepthStencilView();

	Context->ClearRenderTargetView(RTV, Device.GetClearColor());
	Context->ClearDepthStencilView(DSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	const D3D11_VIEWPORT& Viewport = Device.GetViewport();
	Context->RSSetViewports(1, &Viewport);
	Context->OMSetRenderTargets(1, &RTV, DSV);
}

void FRenderer::RegisterPostEffect(EPostEffectType Type, FPostEffectCallback Callback)
{
	//	팀원이 개별 post 효과를 주입하는 등록 지점
	const uint32 Idx = static_cast<uint32>(Type);
	if (Idx >= static_cast<uint32>(EPostEffectType::MAX))
	{
		return;
	}

	PostEffects[Idx].Callback = std::move(Callback);
}

void FRenderer::SetPostEffectEnabled(EPostEffectType Type, bool bEnabled)
{
	//	효과 활성화/비활성화 토글 (런타임 옵션 연결용)
	const uint32 Idx = static_cast<uint32>(Type);
	if (Idx >= static_cast<uint32>(EPostEffectType::MAX))
	{
		return;
	}

	PostEffects[Idx].bEnabled = bEnabled;
}

bool FRenderer::IsPostEffectEnabled(EPostEffectType Type) const
{
	const uint32 Idx = static_cast<uint32>(Type);
	if (Idx >= static_cast<uint32>(EPostEffectType::MAX))
	{
		return false;
	}

	return PostEffects[Idx].bEnabled;
}

//	RenderBus에 담긴 모든 RenderCommand에 대해서 Draw Call 수행 (GPU)
void FRenderer::Render(const FRenderBus& InRenderBus)
{
	FDrawCallStats::Reset();

	ID3D11DeviceContext* Context = Device.GetDeviceContext();
	EnsurePostProcessTargets(InRenderBus);
	{
		SCOPE_STAT_CAT("UpdateFrameBuffer", "4_ExecutePass");
		UpdateFrameBuffer(Context, InRenderBus);
		UpdateSceneEffectBuffer(Context, InRenderBus);
	}

	// 패스 실행 순서:
	// - Grid/Axis가 PostProcess/Font 위를 덮지 않도록 Grid를 먼저 그린다.
	// - SelectionMask는 PostProcess 내부에서만 실행한다.
	const ERenderPass PassOrder[] = {
		ERenderPass::Opaque,
		ERenderPass::Decal,
		ERenderPass::ProjectionDecal,
		ERenderPass::Translucent,
		ERenderPass::Grid,
		ERenderPass::SubUV,
		ERenderPass::Billboard,
		ERenderPass::Editor,
		ERenderPass::GizmoOuter,
		ERenderPass::GizmoInner,
		ERenderPass::Font,
		ERenderPass::PostProcess,
		ERenderPass::OverlayFont
	};

	for (ERenderPass CurPass : PassOrder)
	{
		if (CurPass == ERenderPass::SelectionMask)
		{
			continue;
		}

		if ((CurPass == ERenderPass::Decal || CurPass == ERenderPass::ProjectionDecal) && !InRenderBus.GetShowFlags().bDecal)
		{
			continue;
		}

		const uint32 PassIndex = static_cast<uint32>(CurPass);
		const auto& Batcher = PassBatchers[PassIndex];
		const bool bHasBatcher = static_cast<bool>(Batcher);
		const bool bHasProxies = !InRenderBus.GetProxies(CurPass).empty();
		const bool bAlwaysExecutePass = (CurPass == ERenderPass::SelectionMask || CurPass == ERenderPass::PostProcess);
		if (!bAlwaysExecutePass && !bHasBatcher && !bHasProxies) continue;
		if (!bAlwaysExecutePass && bHasBatcher && !bHasProxies && Batcher.IsEmpty && Batcher.IsEmpty()) continue;

		const char* PassName = GetRenderPassName(CurPass);
		SCOPE_STAT_CAT(PassName, "4_ExecutePass");
		GPU_SCOPE_STAT(PassName);

		ApplyPassRenderState(CurPass, Context, InRenderBus.GetViewMode());
		if (CurPass == ERenderPass::PostProcess)
		{
            ApplyPassRenderState(ERenderPass::SelectionMask, Context, InRenderBus.GetViewMode());
			ExecuteSelectionMaskPass(InRenderBus, Context);
			ApplyPassRenderState(ERenderPass::PostProcess, Context, InRenderBus.GetViewMode());

			const bool bIsSceneDepth = (InRenderBus.GetViewMode() == EViewMode::SceneDepth);
			if (bIsSceneDepth)
			{
				DrawScenenDepthVisualize(InRenderBus, Context);
			}
			ExecutePostProcessChain(InRenderBus, Context);
			DrawPostProcessOverlays(InRenderBus, Context, bIsSceneDepth);
			continue;
		}

		ID3D11RenderTargetView* pCurrentRTV = nullptr;
		ID3D11DepthStencilView* pCurrentDSV = nullptr;

		if (CurPass == ERenderPass::Decal)
		{
			// 1. 현재 바인딩된 RTV와 DSV를 가져옵니다. (가져올 때 내부적으로 Ref Count가 1 증가함)
			Context->OMGetRenderTargets(1, &pCurrentRTV, &pCurrentDSV);

			// 2. DSV(Depth) 자리에 nullptr을 꽂아서 바인딩을 끊습니다. 
			// -> 이제 픽셀 셰이더에서 Depth SRV(t1)를 마음껏 읽을 수 있습니다!
			Context->OMSetRenderTargets(1, &pCurrentRTV, nullptr);
		}

        if (bHasBatcher)
			PassBatchers[PassIndex].DrawBatch(CurPass, InRenderBus, Context);
		else
			ExecutePass(InRenderBus.GetProxies(CurPass), InRenderBus, Context);

		if (CurPass == ERenderPass::Decal)
		{
			// 3. 다시 원래의 DSV를 연결하여 다음 패스(Translucent 등)가 정상 작동하도록 복구합니다.
			Context->OMSetRenderTargets(1, &pCurrentRTV, pCurrentDSV);

			// 4. OMGetRenderTargets 호출로 인해 증가했던 메모리 참조 카운트(Ref Count)를 해제합니다. (메모리 누수 방지)
			if (pCurrentRTV) pCurrentRTV->Release();
			if (pCurrentDSV) pCurrentDSV->Release();
		}
	}
}

// ============================================================
// 패스별 기본 렌더 상태 테이블 초기화
// ============================================================
void FRenderer::InitializePassRenderStates()
{
	using E = ERenderPass;
	auto& S = PassRenderStates;

	//								DepthStencil							Blend						Rasterizer							Topology								WireframeAware
	S[(uint32)E::Opaque] =			{ EDepthStencilState::Default,			EBlendState::Opaque,		ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	true };
	S[(uint32)E::Decal] =			{ EDepthStencilState::NoDepth,		EBlendState::AlphaBlend,	ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	true };
	//	Outline에 대한 Masking 방해로 인해 Patch
	
	// S[(uint32)E::Font] = { EDepthStencilState::Default,      EBlendState::AlphaBlend, ERasterizerState::SolidBackCull,  D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true };
	S[(uint32)E::ProjectionDecal] =		{ EDepthStencilState::DepthReadOnly,	EBlendState::AlphaBlend,	ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	true };
	S[(uint32)E::Translucent] =		{ EDepthStencilState::DepthReadOnly,	EBlendState::AlphaBlend,	ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	false };
	S[(uint32)E::SubUV] =			{ EDepthStencilState::DepthReadOnly,	EBlendState::AlphaBlend,	ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	true };
	S[(uint32)E::Billboard] =		{ EDepthStencilState::DepthReadOnly,	EBlendState::AlphaBlend,	ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	true };
	S[(uint32)E::Font] =			{ EDepthStencilState::DepthReadOnly,	EBlendState::AlphaBlend,	ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	true };

	// PostProcess는 ping RT를 매 단계 "완전한 새 프레임"으로 갱신해야 한다.
	// AlphaBlend 상태면 이전 뷰포트/이전 단계의 잔상이 합성되어
	// 멀티 뷰포트에서 selection 시 화면이 덮여 보이는 아티팩트가 발생한다.
	S[(uint32)E::PostProcess] =		{ EDepthStencilState::NoDepth,			EBlendState::Opaque,		ERasterizerState::SolidNoCull,		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	false };
	S[(uint32)E::SelectionMask] =	{ EDepthStencilState::NoDepth,			EBlendState::Opaque,		ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	false };
	S[(uint32)E::Editor] =			{ EDepthStencilState::Default,			EBlendState::AlphaBlend,	ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_LINELIST,		true };
	S[(uint32)E::Grid] =			{ EDepthStencilState::Default,			EBlendState::AlphaBlend,	ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_LINELIST,		false };
	S[(uint32)E::GizmoOuter] =		{ EDepthStencilState::GizmoOutsideDepthWrite, EBlendState::Opaque,	ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	false };
	S[(uint32)E::GizmoInner] =		{ EDepthStencilState::GizmoInsideDepthWrite, EBlendState::AlphaBlend, ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	false };
	S[(uint32)E::OverlayFont] =		{ EDepthStencilState::NoDepth,			EBlendState::AlphaBlend,	ERasterizerState::SolidBackCull,	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };

}

// ============================================================
// Pass Batcher DrawBatch 바인딩 초기화
// ============================================================
void FRenderer::InitializePassBatchers()
{
	PassBatchers[(uint32)ERenderPass::Editor] = {
		[this](ERenderPass, const FRenderBus&, ID3D11DeviceContext* Ctx) {
			DrawLineBatcher(EditorLineBatcher, Ctx);
		},
		[this]() { return EditorLineBatcher.GetLineCount() == 0; }
	};

	PassBatchers[(uint32)ERenderPass::Grid] = {
		[this](ERenderPass, const FRenderBus&, ID3D11DeviceContext* Ctx) {
			DrawLineBatcher(GridLineBatcher, Ctx);
		},
		[this]() { return GridLineBatcher.GetLineCount() == 0; }
	};

	PassBatchers[(uint32)ERenderPass::Font] = {
		[this](ERenderPass, const FRenderBus&, ID3D11DeviceContext* Ctx) {
			const FFontResource* FontRes = FResourceManager::Get().FindFont(FName("Default"));
			FontBatcher.DrawBatch(Ctx, FontRes);
		},
		[this]() { return FontBatcher.GetQuadCount() == 0; }
	};

	PassBatchers[(uint32)ERenderPass::OverlayFont] = {
		[this](ERenderPass, const FRenderBus&, ID3D11DeviceContext* Ctx) {
			const FFontResource* FontRes = FResourceManager::Get().FindFont(FName("Default"));
			FontBatcher.DrawScreenBatch(Ctx, FontRes);
		},
		nullptr  // OverlayFont(Stats 등)는 항상 존재
	};

	PassBatchers[(uint32)ERenderPass::SubUV] = {
		[this](ERenderPass, const FRenderBus&, ID3D11DeviceContext* Ctx) {
			SubUVBatcher.DrawBatch(Ctx);
		},
		[this]() { return SubUVBatcher.GetSpriteCount() == 0; }
	};

	PassBatchers[(uint32)ERenderPass::Billboard] = {
		[this](ERenderPass, const FRenderBus&, ID3D11DeviceContext* Ctx) {
			BillboardBatcher.DrawBatch(Ctx);
		},
		[this]() { return BillboardBatcher.GetSpriteCount() == 0; }
	};

	PassBatchers[(uint32)ERenderPass::PostProcess] = {
		[this](ERenderPass, const FRenderBus&, ID3D11DeviceContext*) {
		},
		nullptr  // PostProcess는 내부에서 SelectionMask 체크
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

void FRenderer::DrawPostProcessFog(const FRenderBus& Bus, ID3D11DeviceContext* Context, FPostProcessIO& IO)
{
	if (!IO.ColorInput || !IO.ColorOutput || !Context)
	{
		return;
	}

	const FFogPostProcessConstants& FogConstants = Bus.GetFogPostProcessConstants();
	if (FogConstants.FogCount == 0 || !IO.DepthInput)
	{
		BlitSRVToRTV(IO.ColorInput, IO.ColorOutput, Context);
		return;
	}

	Context->OMSetRenderTargets(1, &IO.ColorOutput, nullptr);

	ID3D11ShaderResourceView* SRVs[2] = { IO.ColorInput, IO.DepthInput };
	Context->PSSetShaderResources(0, 2, SRVs);

	FShader* FogShader = FShaderManager::Get().GetShader(EShaderType::FogPostProcess);
	if (!FogShader)
	{
		ID3D11ShaderResourceView* NullSRV[2] = { nullptr, nullptr };
		Context->PSSetShaderResources(0, 2, NullSRV);
		BlitSRVToRTV(IO.ColorInput, IO.ColorOutput, Context);
		return;
	}
	FogShader->Bind(Context);

	FConstantBuffer* FogCB = FConstantBufferPool::Get().GetBuffer(ECBSlot::Fog, sizeof(FFogPostProcessConstants));
	FogCB->Update(Context, &FogConstants, sizeof(FogConstants));
	ID3D11Buffer* cb = FogCB->GetBuffer();
	Context->PSSetConstantBuffers(ECBSlot::Fog, 1, &cb);

	Context->IASetInputLayout(nullptr);
	Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	Context->Draw(3, 0);
	FDrawCallStats::Increment();

	ID3D11ShaderResourceView* NullSRV[2] = { nullptr, nullptr };
	Context->PSSetShaderResources(0, 2, NullSRV);
}

// ============================================================
// 프록시 패스 실행기 — FPrimitiveSceneProxy* 순회
// ============================================================
void FRenderer::ExecutePass(const TArray<const FPrimitiveSceneProxy*>& Proxies, const FRenderBus& Bus, ID3D11DeviceContext* Context)
{
	SortProxies(Proxies);

	FDrawState State;
	ActiveDepthSRV = Bus.GetViewportDepthSRV();

	{
		SCOPE_STAT_CAT("ExecutePass::Draw", "4_ExecutePass");
		for (const FPrimitiveSceneProxy* RawProxy : SortedProxyBuffer)
		{
			const FPrimitiveSceneProxy& Proxy = *RawProxy;
			if (Proxy.Pass == ERenderPass::Decal && !ActiveDepthSRV) continue;
			if (!Proxy.MeshBuffer || !Proxy.MeshBuffer->IsValid()) continue;
			BindShader(Proxy, Context, State);	
			BindExtraCB(Proxy, Context);
			
			if(Proxy.SectionDraws.size() == 1)
				DrawSingleSection(Proxy, Context, State);
			else if (!Proxy.SectionDraws.empty())
				DrawSections(Proxy, Context, State);
			else
				DrawSimple(Proxy, Context, State);
		}
	}

	CleanupSRV(Context, State);
   ActiveDepthSRV = nullptr;
}

// ============================================================
// ExecutePass 헬퍼 함수들
// ============================================================

void FRenderer::SortProxies(const TArray<const FPrimitiveSceneProxy*>& Proxies)
{
	SCOPE_STAT_CAT("ExecutePass::Sort", "4_ExecutePass");

	const bool bSortByDecalOrder = !Proxies.empty() && Proxies[0] && Proxies[0]->Pass == ERenderPass::Decal;
	const auto ProxyLess = [](const FPrimitiveSceneProxy* A, const FPrimitiveSceneProxy* B)
	{
		if (A->SortKey != B->SortKey)
		{
			return A->SortKey < B->SortKey;
		}

		return A->MaterialSortKey < B->MaterialSortKey;
	};

	// A: capacity 유지 — assign() 대신 clear() + insert()
	SortedProxyBuffer.clear();
	SortedProxyBuffer.insert(SortedProxyBuffer.end(), Proxies.begin(), Proxies.end());

	if (SortedProxyBuffer.size() <= 1) return;

	if (bSortByDecalOrder)
	{
		const auto DecalProxyLess = [](const FPrimitiveSceneProxy* A, const FPrimitiveSceneProxy* B)
		{
			if (A->SortOrder != B->SortOrder)
			{
				return A->SortOrder < B->SortOrder;
			}

			return A->ProxyId < B->ProxyId;
		};

		bool bAlreadySortedByDecalOrder = true;
		for (size_t i = 1; i < SortedProxyBuffer.size(); ++i)
		{
			if (DecalProxyLess(SortedProxyBuffer[i], SortedProxyBuffer[i - 1]))
			{
				bAlreadySortedByDecalOrder = false;
				break;
			}
		}
		if (!bAlreadySortedByDecalOrder)
		{
			std::sort(SortedProxyBuffer.begin(), SortedProxyBuffer.end(), DecalProxyLess);
		}
		return;
	}

	// B: 이미 정렬되어 있으면 O(N) 체크 후 skip
	bool bAlreadySorted = true;
	for (size_t i = 1; i < SortedProxyBuffer.size(); ++i)
	{
		if (ProxyLess(SortedProxyBuffer[i], SortedProxyBuffer[i - 1]))
		{
			bAlreadySorted = false;
			break;
		}
	}
	if (bAlreadySorted) return;

	// C: Shader/MeshBuffer 뒤에 Material layout까지 정렬해 draw-state grouping을 강화한다.
	std::sort(SortedProxyBuffer.begin(), SortedProxyBuffer.end(), ProxyLess);
}

void FRenderer::BindShader(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx, FDrawState& State)
{
	if (Proxy.Shader && Proxy.Shader != State.LastShader)
	{
		Proxy.Shader->Bind(Ctx);
		State.LastShader = Proxy.Shader;
	}
}

void FRenderer::EnsurePerObjectCBPoolCapacity(uint32 RequiredCount)
{
	if (PerObjectCBPool.size() >= RequiredCount)
	{
		return;
	}

	const size_t OldCount = PerObjectCBPool.size();
	PerObjectCBPool.resize(RequiredCount);

	ID3D11Device* D3DDevice = Device.GetDevice();
	for (size_t Index = OldCount; Index < PerObjectCBPool.size(); ++Index)
	{
		PerObjectCBPool[Index].Create(D3DDevice, sizeof(FPerObjectConstants));
	}
}

FConstantBuffer* FRenderer::GetPerObjectCBForProxy(const FPrimitiveSceneProxy& Proxy)
{
	if (Proxy.ProxyId == UINT32_MAX)
	{
		return nullptr;
	}

	EnsurePerObjectCBPoolCapacity(Proxy.ProxyId + 1);
	return &PerObjectCBPool[Proxy.ProxyId];
}

bool FRenderer::BindPerObjectCB(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx, FDrawState& State)
{
	FConstantBuffer* CB = GetPerObjectCBForProxy(Proxy);
	if (!CB || !CB->GetBuffer())
	{
		return false;
	}

	if (Proxy.NeedsPerObjectCBUpload())
	{
		SCOPE_STAT_CAT("ExecutePass::PerObjectConstantBuffer.Update", "4_ExecutePass");
		CB->Update(Ctx, &Proxy.PerObjectConstants, sizeof(FPerObjectConstants));
		Proxy.ClearPerObjectCBDirty();
	}

	ID3D11Buffer* RawCB = CB->GetBuffer();
	if (RawCB != State.LastPerObjectCB)
	{
		Ctx->VSSetConstantBuffers(ECBSlot::PerObject, 1, &RawCB);
      Ctx->PSSetConstantBuffers(ECBSlot::PerObject, 1, &RawCB);
		State.LastPerObjectCB = RawCB;
	}

	return true;
}

void FRenderer::BindExtraCB(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx)
{
	if (Proxy.ExtraCB.Buffer)
	{
		Proxy.ExtraCB.Buffer->Update(Ctx, Proxy.ExtraCB.Data, Proxy.ExtraCB.Size);
		ID3D11Buffer* cb = Proxy.ExtraCB.Buffer->GetBuffer();
		Ctx->VSSetConstantBuffers(Proxy.ExtraCB.Slot, 1, &cb);
		Ctx->PSSetConstantBuffers(Proxy.ExtraCB.Slot, 1, &cb);
	}
}

bool FRenderer::BindMeshBuffer(FMeshBuffer* Buffer, ID3D11DeviceContext* Ctx, FDrawState& State)
{
	if (Buffer == State.LastMeshBuffer) return true;

	uint32 offset = 0;
	ID3D11Buffer* vertexBuffer = Buffer->GetVertexBuffer().GetBuffer();
	if (!vertexBuffer) return false;

	uint32 stride = Buffer->GetVertexBuffer().GetStride();
	if (stride == 0) return false;

	Ctx->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

	ID3D11Buffer* indexBuffer = Buffer->GetIndexBuffer().GetBuffer();
	if (indexBuffer)
		Ctx->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);

	State.LastMeshBuffer = Buffer;
	return true;
}

void FRenderer::DrawSections(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx, FDrawState& State)
{
	if (!BindMeshBuffer(Proxy.MeshBuffer, Ctx, State)) return;

	// SectionDraw는 IB 필수
	if (!Proxy.MeshBuffer->GetIndexBuffer().GetBuffer()) return;
	if (!BindPerObjectCB(Proxy, Ctx, State)) return;

	if (!State.bSamplerBound)
	{
		Ctx->PSSetSamplers(0, 1, &Resources.DefaultSampler);
		State.bSamplerBound = true;
	}

	if (Proxy.Pass == ERenderPass::Decal)
	{
		Ctx->PSSetShaderResources(1, 1, &ActiveDepthSRV);
	}
	
	// Material CB 슬롯 바인딩 (1회)
	FConstantBuffer* MaterialCB = FConstantBufferPool::Get().GetBuffer(ECBSlot::Material, sizeof(FMaterialConstants));
	if (!State.bMaterialBound)
	{
		ID3D11Buffer* b4 = MaterialCB->GetBuffer();
		Ctx->VSSetConstantBuffers(ECBSlot::Material, 1, &b4);
        Ctx->PSSetConstantBuffers(ECBSlot::Material, 1, &b4);
		State.bMaterialBound = true;
	}

	for (const FMeshSectionDraw& Section : Proxy.SectionDraws)
	{
		if (Section.IndexCount == 0) continue;

		// SRV 변경 시에만 바인딩
		if (Section.DiffuseSRV != State.LastSRV)
		{
			ID3D11ShaderResourceView* srv = Section.DiffuseSRV;
			Ctx->PSSetShaderResources(0, 1, &srv);
			State.LastSRV = Section.DiffuseSRV;
		}

		// Material CB — SectionColor 또는 UVScroll 변경 시만 업데이트
		int32 curUVScroll = Section.bIsUVScroll ? 1 : 0;
		if (curUVScroll != State.LastUVScroll
			|| memcmp(&Section.DiffuseColor, &State.LastSectionColor, sizeof(FVector4)) != 0)
		{
			FMaterialConstants MatConstants = {};
			MatConstants.bIsUVScroll = curUVScroll;
			MatConstants.SectionColor = Section.DiffuseColor;
			MaterialCB->Update(Ctx, &MatConstants, sizeof(MatConstants));
			State.LastUVScroll = curUVScroll;
			State.LastSectionColor = Section.DiffuseColor;
		}

		Ctx->DrawIndexed(Section.IndexCount, Section.FirstIndex, 0);
		FDrawCallStats::Increment();
	}
}

void FRenderer::DrawSingleSection(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx, FDrawState& State)
{
	const FMeshSectionDraw& Section = Proxy.SectionDraws[0];
	if (Section.IndexCount == 0) return;

	if (!BindMeshBuffer(Proxy.MeshBuffer, Ctx, State)) return;
	// SectionDraw는 IB 필수
	if (!Proxy.MeshBuffer->GetIndexBuffer().GetBuffer()) return;
	if (!BindPerObjectCB(Proxy, Ctx, State)) return;

	if (!State.bSamplerBound)
	{
		Ctx->PSSetSamplers(0, 1, &Resources.DefaultSampler);
		State.bSamplerBound = true;
	}

	if (Proxy.Pass == ERenderPass::Decal)
	{
		Ctx->PSSetShaderResources(1, 1, &ActiveDepthSRV);
	}

	// Material CB 슬롯 바인딩 (1회)
	FConstantBuffer* MaterialCB = FConstantBufferPool::Get().GetBuffer(ECBSlot::Material, sizeof(FMaterialConstants));
	if (!State.bMaterialBound)
	{
		ID3D11Buffer* b4 = MaterialCB->GetBuffer();
		Ctx->VSSetConstantBuffers(ECBSlot::Material, 1, &b4);
		Ctx->PSSetConstantBuffers(ECBSlot::Material, 1, &b4);
		State.bMaterialBound = true;
	}

	// SRV 변경 시에만 바인딩
	if (Section.DiffuseSRV != State.LastSRV)
	{
		ID3D11ShaderResourceView* srv = Section.DiffuseSRV;
		Ctx->PSSetShaderResources(0, 1, &srv);
		State.LastSRV = Section.DiffuseSRV;
	}

	// Material CB — SectionColor 또는 UVScroll 변경 시만 업데이트
	int32 curUVScroll = Section.bIsUVScroll ? 1 : 0;
	if (curUVScroll != State.LastUVScroll
		|| memcmp(&Section.DiffuseColor, &State.LastSectionColor, sizeof(FVector4)) != 0)
	{
		FMaterialConstants MatConstants = {};
		MatConstants.bIsUVScroll = curUVScroll;
		MatConstants.SectionColor = Section.DiffuseColor;
		MaterialCB->Update(Ctx, &MatConstants, sizeof(MatConstants));
		State.LastUVScroll = curUVScroll;
		State.LastSectionColor = Section.DiffuseColor;
	}

	Ctx->DrawIndexed(Section.IndexCount, Section.FirstIndex, 0);
	FDrawCallStats::Increment();
}

void FRenderer::DrawSimple(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx, FDrawState& State)
{
	if (!BindPerObjectCB(Proxy, Ctx, State)) return;

	if (!BindMeshBuffer(Proxy.MeshBuffer, Ctx, State)) return;

	uint32 indexCount = Proxy.MeshBuffer->GetIndexBuffer().GetIndexCount();
	if (indexCount > 0)
		Ctx->DrawIndexed(indexCount, 0, 0);
	else
		Ctx->Draw(Proxy.MeshBuffer->GetVertexBuffer().GetVertexCount(), 0);
	FDrawCallStats::Increment();
}

void FRenderer::CleanupSRV(ID3D11DeviceContext* Ctx, const FDrawState& State)
{
	if (State.HasBoundSRV())
	{
		ID3D11ShaderResourceView* nullSRV = nullptr;
		Ctx->PSSetShaderResources(0, 1, &nullSRV);
	}

	ID3D11ShaderResourceView* nullDepthSRV = nullptr;
	Ctx->PSSetShaderResources(1, 1, &nullDepthSRV);
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

void FRenderer::ExecuteSelectionMaskPass(const FRenderBus& Bus, ID3D11DeviceContext* Context)
{
	//	선택 마스크 RT는 매 프레임 비워서 이전 프레임 outline 잔상이 남지 않게 한다.
	if (!OutlineMaskRTV)
	{
		return;
	}

	ID3D11DepthStencilView* DSV = Bus.GetViewportDSV();
	ID3D11RenderTargetView* ViewportRTV = Bus.GetViewportRTV();
	const float ClearMask[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	Context->ClearRenderTargetView(OutlineMaskRTV, ClearMask);
	if (!Bus.GetShowFlags().bSelectionOutline)
	{
		if (ViewportRTV)
		{
			Context->OMSetRenderTargets(1, &ViewportRTV, DSV);
		}
		return;
	}

	const auto& MaskProxies = Bus.GetProxies(ERenderPass::SelectionMask);
	const bool bHasMaskProxies = !MaskProxies.empty();
	const bool bHasSelectedFont = FontBatcher.GetSelectedQuadCount() > 0;
	const bool bHasSelectedSubUV = SubUVBatcher.GetSelectedSpriteCount() > 0;
	const bool bHasSelectedBillboard = BillboardBatcher.GetSelectedSpriteCount() > 0;
	const bool bHasAnySelectionMask =
		bHasMaskProxies || bHasSelectedFont || bHasSelectedSubUV || bHasSelectedBillboard;

	if (!bHasAnySelectionMask)
	{
		if (ViewportRTV)
		{
			Context->OMSetRenderTargets(1, &ViewportRTV, DSV);
		}
		return;
	}

	Context->OMSetRenderTargets(1, &OutlineMaskRTV, nullptr);

	if (bHasMaskProxies)
	{
		ExecutePass(MaskProxies, Bus, Context);
	}

	if (bHasSelectedFont)
	{
		const FFontResource* FontRes = FResourceManager::Get().FindFont(FName("Default"));
		FontBatcher.DrawSelectionMaskBatch(Context, FontRes);
	}

	if (bHasSelectedSubUV)
	{
		SubUVBatcher.DrawSelectionMaskBatch(Context);
	}

	if (bHasSelectedBillboard)
	{
		BillboardBatcher.DrawSelectionMaskBatch(Context);
	}

	if (ViewportRTV)
	{
		Context->OMSetRenderTargets(1, &ViewportRTV, DSV);
	}
}

void FRenderer::ExecutePostProcessChain(const FRenderBus& Bus, ID3D11DeviceContext* Context)
{
	//	Depth 모드는 후처리를 건너뛴다. SceneDepth는 outline만 허용한다.
	const EViewMode ViewMode = Bus.GetViewMode();
	if (ViewMode == EViewMode::Depth)
	{
		return;
	}

	ID3D11RenderTargetView* ViewportRTV = Bus.GetViewportRTV();
	ID3D11DepthStencilView* ViewportDSV = Bus.GetViewportDSV();
	ID3D11ShaderResourceView* SceneColorSRV = Bus.GetViewportSRV();
	if (!ViewportRTV || !SceneColorSRV || !PostPingRTV[0] || !PostPingSRV[0])
	{
		return;
	}

	Context->OMSetRenderTargets(0, nullptr, nullptr);
	bool bExecutedAnyPost = false;

	//	기본 체인 순서: Fog -> Outline -> FXAA
	const EPostEffectType Order[] = {
		//EPostEffectType::Decal,
		EPostEffectType::Fog,
		EPostEffectType::Outline,
		EPostEffectType::FXAA
	};

	uint32 WriteIndex = 0;
	ID3D11ShaderResourceView* CurrentColor = SceneColorSRV;

	for (EPostEffectType Type : Order)
	{
		const uint32 Idx = static_cast<uint32>(Type);
		// SceneDepth 모드에서는 Outline만 허용하고 나머지(Fog, FXAA)는 건너뜀
		const bool bSkipForSceneDepth = (ViewMode == EViewMode::SceneDepth) && (Type != EPostEffectType::Outline);
		if (bSkipForSceneDepth)
		{
			continue;
		}

		if (!PostEffects[Idx].bEnabled)
		{
			continue;
		}

		FPostProcessIO IO = {};
		IO.ColorInput = CurrentColor;
		IO.DepthInput = Bus.GetViewportDepthSRV();
		IO.OutlineMask = OutlineMaskSRV;
		IO.ColorOutput = PostPingRTV[WriteIndex];

		const auto& Callback = PostEffects[Idx].Callback;
		if (Callback)
		{
			Callback(Bus, Context, IO);
		}
		else
		{
			BlitSRVToRTV(IO.ColorInput, IO.ColorOutput, Context);
		}

		CurrentColor = PostPingSRV[WriteIndex];
		WriteIndex = 1 - WriteIndex;
		bExecutedAnyPost = true;
	}

	if (bExecutedAnyPost)
	{
		BlitSRVToRTV(CurrentColor, ViewportRTV, Context);
	}

	Context->OMSetRenderTargets(1, &ViewportRTV, ViewportDSV);
}

void FRenderer::EnsurePostProcessTargets(const FRenderBus& Bus)
{
	//	뷰포트 해상도 기준으로 post 중간 버퍼를 재생성한다.
	if (!Bus.GetViewportRTV())
	{
		ReleasePostProcessTargets();
		return;
	}

	const uint32 Width = static_cast<uint32>(Bus.GetViewportWidth());
	const uint32 Height = static_cast<uint32>(Bus.GetViewportHeight());
	if (Width == 0 || Height == 0)
	{
		ReleasePostProcessTargets();
		return;
	}

	if (PostTargetWidth == Width && PostTargetHeight == Height && PostPingRTV[0] && OutlineMaskRTV)
	{
		return;
	}

	ReleasePostProcessTargets();

	ID3D11Device* D3DDevice = Device.GetDevice();
	if (!D3DDevice)
	{
		return;
	}

	D3D11_TEXTURE2D_DESC ColorDesc = {};
	ColorDesc.Width = Width;
	ColorDesc.Height = Height;
	ColorDesc.MipLevels = 1;
	ColorDesc.ArraySize = 1;
	ColorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	ColorDesc.SampleDesc.Count = 1;
	ColorDesc.Usage = D3D11_USAGE_DEFAULT;
	ColorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	D3D11_TEXTURE2D_DESC MaskDesc = ColorDesc;

	for (int32 i = 0; i < 2; ++i)
	{
		if (FAILED(D3DDevice->CreateTexture2D(&ColorDesc, nullptr, &PostPingTexture[i]))) return;
		if (FAILED(D3DDevice->CreateRenderTargetView(PostPingTexture[i], nullptr, &PostPingRTV[i]))) return;
		if (FAILED(D3DDevice->CreateShaderResourceView(PostPingTexture[i], nullptr, &PostPingSRV[i]))) return;
	}

	if (FAILED(D3DDevice->CreateTexture2D(&MaskDesc, nullptr, &OutlineMaskTexture))) return;
	if (FAILED(D3DDevice->CreateRenderTargetView(OutlineMaskTexture, nullptr, &OutlineMaskRTV))) return;
	if (FAILED(D3DDevice->CreateShaderResourceView(OutlineMaskTexture, nullptr, &OutlineMaskSRV))) return;

	PostTargetWidth = Width;
	PostTargetHeight = Height;
}

void FRenderer::ReleasePostProcessTargets()
{
	//	post ping-pong/mask 리소스를 모두 해제한다.
	for (int32 i = 0; i < 2; ++i)
	{
		SafeRelease(PostPingSRV[i]);
		SafeRelease(PostPingRTV[i]);
		SafeRelease(PostPingTexture[i]);
	}

	SafeRelease(OutlineMaskSRV);
	SafeRelease(OutlineMaskRTV);
	SafeRelease(OutlineMaskTexture);

	PostTargetWidth = 0;
	PostTargetHeight = 0;
}

void FRenderer::BlitSRVToRTV(ID3D11ShaderResourceView* SourceSRV, ID3D11RenderTargetView* DestRTV, ID3D11DeviceContext* Context)
{
	if (!SourceSRV || !DestRTV || !Context)
	{
		return;
	}

	ID3D11Resource* SrcResource = nullptr;
	ID3D11Resource* DstResource = nullptr;
	SourceSRV->GetResource(&SrcResource);
	DestRTV->GetResource(&DstResource);

	if (SrcResource && DstResource)
	{
		Context->CopyResource(DstResource, SrcResource);
	}

	SafeRelease(SrcResource);
	SafeRelease(DstResource);
}

// ============================================================
// PostProcess Outline — SceneColor + OutlineMask를 결합
// ============================================================
void FRenderer::DrawPostProcessOutline(const FRenderBus& Bus, ID3D11DeviceContext* Context, ID3D11ShaderResourceView* SceneColorSRV, ID3D11RenderTargetView* OutputRTV)
{
	if (!SceneColorSRV || !OutlineMaskSRV || !OutputRTV)
	{
		return;
	}

	Context->OMSetRenderTargets(1, &OutputRTV, nullptr);

	ID3D11ShaderResourceView* SRVs[2] = { SceneColorSRV, OutlineMaskSRV };
	Context->PSSetShaderResources(0, 2, SRVs);

	FShader* PPShader = FShaderManager::Get().GetShader(EShaderType::OutlinePostProcess);
	if (!PPShader)
	{
		BlitSRVToRTV(SceneColorSRV, OutputRTV, Context);
		ID3D11ShaderResourceView* NullSRV[2] = { nullptr, nullptr };
		Context->PSSetShaderResources(0, 2, NullSRV);
		return;
	}
	PPShader->Bind(Context);

	FConstantBuffer* OutlineCB = FConstantBufferPool::Get().GetBuffer(ECBSlot::PostProcess, sizeof(FOutlinePostProcessConstants));
	FOutlinePostProcessConstants PPConstants;
	PPConstants.OutlineColor = FVector4(1.0f, 0.5f, 0.0f, 1.0f);
	PPConstants.OutlineThickness = 7.0f;
	PPConstants.OutlineFalloff = 1.6f;
    const bool bUseFXAAAfterOutline = IsPostEffectEnabled(EPostEffectType::FXAA) && (Bus.GetViewMode() != EViewMode::SceneDepth);
	PPConstants.bOutputLumaToAlpha = bUseFXAAAfterOutline ? 1.0f : 0.0f;
	PPConstants.OutputAlpha = 1.0f;
	OutlineCB->Update(Context, &PPConstants, sizeof(PPConstants));
	ID3D11Buffer* cb = OutlineCB->GetBuffer();
	Context->PSSetConstantBuffers(ECBSlot::PostProcess, 1, &cb);

	Context->IASetInputLayout(nullptr);
	Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	Context->Draw(3, 0);
	FDrawCallStats::Increment();

	ID3D11ShaderResourceView* NullSRV[2] = { nullptr, nullptr };
	Context->PSSetShaderResources(0, 2, NullSRV);
}

void FRenderer::DrawPostProcessFXAA(const FRenderBus& Bus, ID3D11DeviceContext* Context, ID3D11ShaderResourceView* SceneColorSRV, ID3D11RenderTargetView* OutputRTV)
{
	if (!Context || !SceneColorSRV || !OutputRTV)
	{
		return;
	}

	const float Width = Bus.GetViewportWidth();
	const float Height = Bus.GetViewportHeight();
	if (Width <= 0.0f || Height <= 0.0f)
	{
		BlitSRVToRTV(SceneColorSRV, OutputRTV, Context);
		return;
	}

	Context->OMSetRenderTargets(1, &OutputRTV, nullptr);

	ID3D11ShaderResourceView* SRV = SceneColorSRV;
	Context->PSSetShaderResources(0, 1, &SRV);
	ID3D11SamplerState* Sampler = Resources.DefaultSampler;
	Context->PSSetSamplers(0, 1, &Sampler);

	FShader* FXAAShader = FShaderManager::Get().GetShader(EShaderType::FXAAPostProcess);
	if (!FXAAShader)
	{
		BlitSRVToRTV(SceneColorSRV, OutputRTV, Context);
		ID3D11ShaderResourceView* NullSRV = nullptr;
		Context->PSSetShaderResources(0, 1, &NullSRV);
		return;
	}
	FXAAShader->Bind(Context);

	FFXAAConstants FXAAData = FXAAConstants;
	FXAAData.TexelSize = FVector2(1.0f / Width, 1.0f / Height);

	FConstantBuffer* FXAACB = FConstantBufferPool::Get().GetBuffer(ECBSlot::PostProcess_FXAA, sizeof(FFXAAConstants));
	if (FXAACB && FXAACB->GetBuffer())
	{
		FXAACB->Update(Context, &FXAAData, sizeof(FXAAData));
		ID3D11Buffer* CB = FXAACB->GetBuffer();
		Context->PSSetConstantBuffers(ECBSlot::PostProcess_FXAA, 1, &CB);
	}

	Context->IASetInputLayout(nullptr);
	Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	Context->Draw(3, 0);
	FDrawCallStats::Increment();

	ID3D11ShaderResourceView* NullSRV = nullptr;
	Context->PSSetShaderResources(0, 1, &NullSRV);
}

void FRenderer::DrawScenenDepthVisualize(const FRenderBus& Bus, ID3D11DeviceContext* Context)
{
	ID3D11ShaderResourceView* DepthSRV = Bus.GetViewportDepthSRV();
	ID3D11DepthStencilView* DSV = Bus.GetViewportDSV();
	ID3D11RenderTargetView* RTV = Bus.GetViewportRTV();
	if (!DepthSRV || !RTV)
	{
		return;
	}

	Context->OMSetRenderTargets(1, &RTV, nullptr);
	Context->PSSetShaderResources(0, 1, &DepthSRV);
	FShader* Shader = FShaderManager::Get().GetShader(EShaderType::DepthView);
	if (Shader) Shader->Bind(Context);
	Context->PSSetSamplers(0, 1, &Resources.DefaultSampler);
	// Full-Screen Triangle (SV_VertexID, no VB)
	Context->IASetInputLayout(nullptr);
	Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	Context->Draw(3, 0);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	Context->PSSetShaderResources(0, 1, &nullSRV);
	Context->OMSetRenderTargets(1, &RTV, DSV);
}

void FRenderer::ExecuteIdPickPass(const TArray<const FPrimitiveSceneProxy*>& Proxies, ID3D11DeviceContext* Context, FShader* PrimitiveShader, FShader* BillboardShader, FShader* StaticMeshShader)
{
	if (!Context || !PrimitiveShader || !BillboardShader || !StaticMeshShader || !IdPickPerObjectCB.GetBuffer())
	{
		return;
	}
	FConstantBuffer* PickingCB = FConstantBufferPool::Get().GetBuffer(ECBSlot::Picking, sizeof(FPickingConstants));
	if (!PickingCB || !PickingCB->GetBuffer())
	{
		return;
	}

	ID3D11SamplerState* Sampler = IdPickSampler ? IdPickSampler : Resources.DefaultSampler;
	Context->PSSetSamplers(0, 1, &Sampler);

	FMeshBuffer* LastMeshBuffer = nullptr;
	FShader* LastShader = nullptr;
	ID3D11ShaderResourceView* LastSRV = reinterpret_cast<ID3D11ShaderResourceView*>(~0ull);
	for (const FPrimitiveSceneProxy* Proxy : Proxies)
	{
		if (!Proxy || !Proxy->Owner || !Proxy->MeshBuffer || !Proxy->MeshBuffer->IsValid() || Proxy->ProxyId == UINT32_MAX)
		{
			continue;
		}
		if (!Proxy->Owner->SupportsPicking())
		{
			continue;
		}
		if (!Proxy->bVisible)
		{
			continue;
		}

		const bool bIsStaticMesh = (Proxy->Shader == FShaderManager::Get().GetShader(EShaderType::StaticMesh));
		const bool bIsProjectionDecal = (Proxy->Pass == ERenderPass::ProjectionDecal)
			|| (Proxy->Shader == FShaderManager::Get().GetShader(EShaderType::ProjectionDecal));
		const bool bIsBillboard = (Proxy->Pass == ERenderPass::Billboard);
		const bool bUseTextureAlphaPick = bIsStaticMesh || bIsProjectionDecal || bIsBillboard;
		FShader* IdShader = PrimitiveShader;
		if (bIsStaticMesh || bIsProjectionDecal)
		{
			IdShader = StaticMeshShader;
		}
		else if (bIsBillboard)
		{
			IdShader = BillboardShader;
		}
		if (IdShader != LastShader)
		{
			IdShader->Bind(Context);
			LastShader = IdShader;
		}

		const FPerObjectConstants PickCB = Proxy->PerObjectConstants;
		IdPickPerObjectCB.Update(Context, &PickCB, sizeof(PickCB));
		ID3D11Buffer* CB = IdPickPerObjectCB.GetBuffer();
		Context->VSSetConstantBuffers(ECBSlot::PerObject, 1, &CB);
		Context->PSSetConstantBuffers(ECBSlot::PerObject, 1, &CB);

		FPickingConstants PickingData = {};
		PickingData.PickingId = Proxy->ProxyId + 1u;
		PickingCB->Update(Context, &PickingData, sizeof(PickingData));
		ID3D11Buffer* b7 = PickingCB->GetBuffer();
		Context->VSSetConstantBuffers(ECBSlot::Picking, 1, &b7);
		Context->PSSetConstantBuffers(ECBSlot::Picking, 1, &b7);

		if (Proxy->MeshBuffer != LastMeshBuffer)
		{
			uint32 Offset = 0;
			ID3D11Buffer* VB = Proxy->MeshBuffer->GetVertexBuffer().GetBuffer();
			if (!VB)
			{
				continue;
			}
			const uint32 Stride = Proxy->MeshBuffer->GetVertexBuffer().GetStride();
			if (Stride == 0)
			{
				continue;
			}
			Context->IASetVertexBuffers(0, 1, &VB, &Stride, &Offset);
			ID3D11Buffer* IB = Proxy->MeshBuffer->GetIndexBuffer().GetBuffer();
			if (IB)
			{
				Context->IASetIndexBuffer(IB, DXGI_FORMAT_R32_UINT, 0);
			}
			LastMeshBuffer = Proxy->MeshBuffer;
		}

		if (!Proxy->SectionDraws.empty() && Proxy->MeshBuffer->GetIndexBuffer().GetBuffer())
		{
			for (const FMeshSectionDraw& Section : Proxy->SectionDraws)
			{
				if (Section.IndexCount == 0)
				{
					continue;
				}

				if (bUseTextureAlphaPick && Section.DiffuseSRV != LastSRV)
				{
					ID3D11ShaderResourceView* SRV = Section.DiffuseSRV;
					Context->PSSetShaderResources(0, 1, &SRV);
					LastSRV = SRV;
				}

				Context->DrawIndexed(Section.IndexCount, Section.FirstIndex, 0);
			}
			continue;
		}

		if (bUseTextureAlphaPick && LastSRV != nullptr)
		{
			ID3D11ShaderResourceView* NullSRV = nullptr;
			Context->PSSetShaderResources(0, 1, &NullSRV);
			LastSRV = nullptr;
		}

		const uint32 IndexCount = Proxy->MeshBuffer->GetIndexBuffer().GetIndexCount();
		if (IndexCount > 0)
		{
			Context->DrawIndexed(IndexCount, 0, 0);
		}
		else
		{
			Context->Draw(Proxy->MeshBuffer->GetVertexBuffer().GetVertexCount(), 0);
		}
	}

	if (LastSRV != nullptr)
	{
		ID3D11ShaderResourceView* NullSRV = nullptr;
		Context->PSSetShaderResources(0, 1, &NullSRV);
	}
}

void FRenderer::RenderIdPickDebugVisualization(ID3D11DeviceContext* Context, ID3D11ShaderResourceView* IdPickSRV, ID3D11RenderTargetView* IdDebugRTV)
{
	if (!Context || !IdPickSRV || !IdDebugRTV)
	{
		return;
	}

	FShader* DebugShader = FShaderManager::Get().GetShader(EShaderType::IDPickDebugVisualize);
	if (!DebugShader)
	{
		return;
	}

	Context->OMSetRenderTargets(1, &IdDebugRTV, nullptr);
	DebugShader->Bind(Context);
	Context->PSSetShaderResources(0, 1, &IdPickSRV);
	Context->IASetInputLayout(nullptr);
	Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Context->Draw(3, 0);

	ID3D11ShaderResourceView* NullSRV = nullptr;
	Context->PSSetShaderResources(0, 1, &NullSRV);
}

void FRenderer::RenderIdPickBuffer(
	const FRenderBus& Bus,
	ID3D11RenderTargetView* IdPickRTV,
	ID3D11DepthStencilView* DSV,
	ID3D11ShaderResourceView* IdPickSRV,
	ID3D11RenderTargetView* IdDebugRTV)
{
	if (!IdPickRTV || !DSV)
	{
		return;
	}

	ID3D11DeviceContext* Context = Device.GetDeviceContext();
	if (!Context)
	{
		return;
	}

	const uint32 ClearValue[4] = { 0u, 0u, 0u, 0u };
	Context->ClearRenderTargetView(IdPickRTV, reinterpret_cast<const float*>(ClearValue));
	Context->ClearDepthStencilView(DSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	Context->OMSetRenderTargets(1, &IdPickRTV, DSV);

	// ID 버퍼도 "실제로 보이는 표면" 기준으로 선택되도록 깊이 쓰기를 활성화한다.
	// (DepthReadOnly면 draw order에 따라 뒤에 그린 프록시가 앞을 덮어 오선택이 발생한다.)
	Device.SetDepthStencilState(EDepthStencilState::Default);
	Device.SetBlendState(EBlendState::Opaque);
	// ID Picking은 배처 경로(예: Billboard)를 우회해 proxy 메시를 직접 그리므로
	// winding/축 변환 차이로 백페이스 컬링에 걸리면 ID가 비어버릴 수 있다.
	// 픽킹 정확도 우선을 위해 ID 패스는 양면 래스터로 고정한다.
	Device.SetRasterizerState(ERasterizerState::SolidNoCull);
	Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	FShader* PrimitiveShader = FShaderManager::Get().GetShader(EShaderType::IDPickPrimitive);
	FShader* BillboardShader = FShaderManager::Get().GetShader(EShaderType::IDPickBillboard);
	FShader* StaticMeshShader = FShaderManager::Get().GetShader(EShaderType::IDPickStaticMesh);
	if (!PrimitiveShader || !BillboardShader || !StaticMeshShader)
	{
		return;
	}

	const ERenderPass Passes[] =
	{
		ERenderPass::Opaque,
		ERenderPass::Decal,
		ERenderPass::ProjectionDecal,
		ERenderPass::Translucent,
		ERenderPass::Billboard
	};
	for (ERenderPass Pass : Passes)
	{
		ExecuteIdPickPass(Bus.GetProxies(Pass), Context, PrimitiveShader, BillboardShader, StaticMeshShader);
	}

	RenderIdPickDebugVisualization(Context, IdPickSRV, IdDebugRTV);
}

// PostProcess 이후 fog 영향을 받지 않아야 하는 에디터 오버레이를 렌더링한다.
void FRenderer::DrawPostProcessOverlays(const FRenderBus& InRenderBus, ID3D11DeviceContext* Context, bool bDrawFontOverlay)
{
	if (InRenderBus.GetShowFlags().bGizmo)
	{
		ID3D11DepthStencilView* DSV = InRenderBus.GetViewportDSV();
		if (DSV)
		{
			Context->ClearDepthStencilView(DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
		}

		const auto& GizmoOuterProxies = InRenderBus.GetProxies(ERenderPass::GizmoOuter);
		if (!GizmoOuterProxies.empty())
		{
			ApplyPassRenderState(ERenderPass::GizmoOuter, Context, InRenderBus.GetViewMode());
			ExecutePass(GizmoOuterProxies, InRenderBus, Context);
		}

		const auto& GizmoInnerProxies = InRenderBus.GetProxies(ERenderPass::GizmoInner);
		if (!GizmoInnerProxies.empty())
		{
			ApplyPassRenderState(ERenderPass::GizmoInner, Context, InRenderBus.GetViewMode());
			ExecutePass(GizmoInnerProxies, InRenderBus, Context);
		}
	}

	if (!bDrawFontOverlay)
	{
		return;
	}

	ApplyPassRenderState(ERenderPass::Font, Context, InRenderBus.GetViewMode());
	const auto& FontBatcher = PassBatchers[(uint32)ERenderPass::Font];
	if (FontBatcher && (!FontBatcher.IsEmpty || !FontBatcher.IsEmpty()))
	{
		FontBatcher.DrawBatch(ERenderPass::Font, InRenderBus, Context);
	}
}

//	Present the rendered frame to the screen. 반드시 Render 이후에 호출되어야 함.
void FRenderer::EndFrame()
{
	Device.Present();
}

void FRenderer::UpdateFrameBuffer(ID3D11DeviceContext* Context, const FRenderBus& InRenderBus)
{
	FFrameConstants frameConstantData = {};
	frameConstantData.View = InRenderBus.GetView();
	frameConstantData.Projection = InRenderBus.GetProj();
	frameConstantData.bIsWireframe = (InRenderBus.GetViewMode() == EViewMode::Wireframe);
	frameConstantData.WireframeColor = InRenderBus.GetWireframeColor();
	frameConstantData.NearPlane = InRenderBus.GetNearPlane();
	frameConstantData.FarPlane = InRenderBus.GetFarPlane();
	frameConstantData.CameraPosition = InRenderBus.GetCameraPosition();
	frameConstantData.InverseView = InRenderBus.GetView().GetInverse();
	frameConstantData.InverseProjection = InRenderBus.GetProj().GetInverse();
	frameConstantData.InverseViewProjection = (InRenderBus.GetView() * InRenderBus.GetProj()).GetInverse();

	const auto& Projection = frameConstantData.Projection;
	if (std::abs(Projection.M[3][2]) > 1e-6f)
	{
		frameConstantData.InvDeviceZToWorldZTransform2 = 1.0f / Projection.M[3][2];
		frameConstantData.InvDeviceZToWorldZTransform3 = Projection.M[2][2] / Projection.M[3][2];
	}

	if (GEngine && GEngine->GetTimer())
	{
		frameConstantData.Time = static_cast<float>(GEngine->GetTimer()->GetTotalTime());
	}

	Resources.FrameBuffer.Update(Context, &frameConstantData, sizeof(FFrameConstants));
	ID3D11Buffer* b0 = Resources.FrameBuffer.GetBuffer();
	Context->VSSetConstantBuffers(ECBSlot::Frame, 1, &b0);
	Context->PSSetConstantBuffers(ECBSlot::Frame, 1, &b0);
}

void FRenderer::UpdateSceneEffectBuffer(ID3D11DeviceContext* Context, const FRenderBus& InRenderBus)
{
	const FSceneEffectConstants& sceneEffectData = InRenderBus.GetSceneEffectConstants();
	Resources.SceneEffectBuffer.Update(Context, &sceneEffectData, sizeof(FSceneEffectConstants));
	ID3D11Buffer* b5 = Resources.SceneEffectBuffer.GetBuffer();
	Context->VSSetConstantBuffers(ECBSlot::SceneEffect, 1, &b5);
	Context->PSSetConstantBuffers(ECBSlot::SceneEffect, 1, &b5);
}

