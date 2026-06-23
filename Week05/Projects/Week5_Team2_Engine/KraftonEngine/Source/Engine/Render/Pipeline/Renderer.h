#pragma once

/*
	실제 렌더링을 담당하는 Class 입니다. (Rendering 최상위 클래스)
*/

#include "Render/Types/RenderTypes.h"

#include "Render/Pipeline/ViewContext.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Batcher/LineBatcher.h"
#include "Render/Batcher/FontBatcher.h"
#include "Render/Batcher/SubUVBatcher.h"

#include <functional>

class FViewport;

// 패스별 Batcher DrawBatch 바인딩
struct FPassBatcherBinding
{
	std::function<void(ERenderPass, const FViewContext&, ID3D11DeviceContext*)> DrawBatch;

	explicit operator bool() const { return DrawBatch != nullptr; }
};

// 패스별 기본 렌더 상태 — Single Source of Truth
struct FPassRenderState
{
	EDepthStencilState       DepthStencil   = EDepthStencilState::Default;
	EBlendState              Blend          = EBlendState::Opaque;
	ERasterizerState         Rasterizer     = ERasterizerState::SolidBackCull;
	D3D11_PRIMITIVE_TOPOLOGY Topology       = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	bool                     bWireframeAware = false;  // Wireframe 모드 시 래스터라이저 전환
};

class FRenderer
{
public:
	void Create(HWND hWindow);
	void Release();

	void PrepareBatchers(const FViewContext& InRenderBus);
	void BeginFrame();
	void Render(const FViewContext& InRenderBus);
	void RenderPicking(const FViewContext& InRenderBus, FViewport* InViewport);
	void EndFrame();

	FD3DDevice& GetFD3DDevice() { return Device; }
	FRenderResources& GetResources() { return Resources; }

private:
	void InitializePassRenderStates();
	void InitializePassBatchers();

	void ApplyPassRenderState(ERenderPass Pass, ID3D11DeviceContext* Context, EViewMode ViewMode);

	void DrawCommandFromSoA(ID3D11DeviceContext* Context, const FPassQueueSoA& Queue, uint32 Idx);
	void DrawStaticMeshSectionsFromSoA(ID3D11DeviceContext* Context, const FPassQueueSoA& Queue, uint32 Idx, const TArray<FMeshSectionDraw>& GlobalSections);

	void UpdateFrameBuffer(ID3D11DeviceContext* Context, const FViewContext& InRenderBus);

	// 기본 패스 실행기 — BindCommand + DrawCommand 루프
	void ExecuteDefaultPass(const FPassQueueSoA& Queue, const FViewContext& Bus, ID3D11DeviceContext* Context);

	// LineBatcher DrawBatch 공통 — EditorShader 바인딩 + DrawBatch
	void DrawLineBatcher(FLineBatcher& Batcher, ID3D11DeviceContext* Context);

	// PostProcess Outline — StencilSRV 읽어 edge detection 후 fullscreen draw
	void DrawPostProcessOutline(const FViewContext& Bus, ID3D11DeviceContext* Context);

private:
	FD3DDevice Device;
	FRenderResources Resources;
	FLineBatcher   EditorLineBatcher;
	FLineBatcher   GridLineBatcher;
	FFontBatcher   FontBatcher;
	FSubUVBatcher  SubUVBatcher;

	// SubUV 정렬용 멤버 버퍼 (재할당 방지)
	TArray<FSubUVEntry> SortedSubUVBuffer;

	FPassRenderState    PassRenderStates[(uint32)ERenderPass::MAX];
	FPassBatcherBinding PassBatchers[(uint32)ERenderPass::MAX];

	// 중복 바인딩 감지용 캐시
	const void* LastBoundShader = nullptr;
	const void* LastBoundMeshBuffer = nullptr;
	const void* LastBoundDiffuseSRV = nullptr;
};
