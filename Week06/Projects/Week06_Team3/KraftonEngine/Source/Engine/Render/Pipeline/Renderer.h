#pragma once

/*
	실제 렌더링을 담당하는 Class 입니다. (Rendering 최상위 클래스)
*/

#include "Render/Types/RenderTypes.h"

#include "Render/Pipeline/RenderBus.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Batcher/LineBatcher.h"
#include "Render/Batcher/FontBatcher.h"
#include "Render/Batcher/SubUVBatcher.h"
#include "Render/Batcher/BillboardBatcher.h"

#include <functional>
#include <array>

// 패스별 Batcher DrawBatch 바인딩
struct FPassBatcherBinding
{
	std::function<void(ERenderPass, const FRenderBus&, ID3D11DeviceContext*)> DrawBatch;
	std::function<bool()> IsEmpty;		// true면 이 패스 skip (렌더 상태 적용도 생략)

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

enum class EPostEffectType : uint32
{
	//Decal,
	Fog = 0,
	Outline,
	FXAA,
	MAX
};

struct FPostProcessIO
{
	//	현재 post 효과의 컬러 입력 (이전 단계 결과)
	ID3D11ShaderResourceView* ColorInput = nullptr;
	//	깊이 기반 효과(Fog/Decal/DepthDebug)용 입력
	ID3D11ShaderResourceView* DepthInput = nullptr;
	//	SelectionMask로 생성된 외곽선 마스크
	ID3D11ShaderResourceView* OutlineMask = nullptr;
	//	현재 단계의 출력 타깃 (ping-pong RTV)
	ID3D11RenderTargetView*   ColorOutput = nullptr;
};

using FPostEffectCallback = std::function<void(const FRenderBus&, ID3D11DeviceContext*, FPostProcessIO&)>;

class FRenderer
{
public:
	void Create(HWND hWindow);
	void Release();

	void PrepareBatchers(const FRenderBus& InRenderBus);
	void BeginFrame();
	void Render(const FRenderBus& InRenderBus);
	void EndFrame();

	void RegisterPostEffect(EPostEffectType Type, FPostEffectCallback Callback);
	void SetPostEffectEnabled(EPostEffectType Type, bool bEnabled);
	bool IsPostEffectEnabled(EPostEffectType Type) const;
	void RenderIdPickBuffer(
		const FRenderBus& Bus,
		ID3D11RenderTargetView* IdPickRTV,
		ID3D11DepthStencilView* DSV,
		ID3D11ShaderResourceView* IdPickSRV = nullptr,
		ID3D11RenderTargetView* IdDebugRTV = nullptr);
	void SetFXAAConstants(const FFXAAConstants& InConstants) { FXAAConstants = InConstants; }
	const FFXAAConstants& GetFXAAConstants() const { return FXAAConstants; }

	FD3DDevice& GetFD3DDevice() { return Device; }
	FRenderResources& GetResources() { return Resources; }

private:
	void InitializePassRenderStates();
	void InitializePassBatchers();

	void ApplyPassRenderState(ERenderPass Pass, ID3D11DeviceContext* Context, EViewMode ViewMode);
	void UpdateFrameBuffer(ID3D11DeviceContext* Context, const FRenderBus& InRenderBus);

	//fireball 등 Fscene 효과용 상수버퍼 업데이트
	void UpdateSceneEffectBuffer(ID3D11DeviceContext* Context, const FRenderBus& InRenderBus);

	// 프록시 패스 실행기 — FPrimitiveSceneProxy* 순회, 필드 직접 접근
 void ExecutePass(const TArray<const FPrimitiveSceneProxy*>& Proxies, const FRenderBus& Bus, ID3D11DeviceContext* Context);

	// ExecutePass 내부 헬퍼
	struct FDrawState
	{
		FShader*     LastShader     = nullptr;
		FMeshBuffer* LastMeshBuffer = nullptr;
		ID3D11ShaderResourceView* LastSRV = reinterpret_cast<ID3D11ShaderResourceView*>(~0ull);
		ID3D11Buffer* LastPerObjectCB = nullptr;
		int32        LastUVScroll   = -1;
		FVector4     LastSectionColor = { -1.0f, -1.0f, -1.0f, -1.0f }; // 초기값: 불일치 보장

		bool         bSamplerBound  = false;
		bool         bMaterialBound  = false;

		bool HasBoundSRV() const { return LastSRV != reinterpret_cast<ID3D11ShaderResourceView*>(~0ull); }
	};

	void SortProxies(const TArray<const FPrimitiveSceneProxy*>& Proxies);
	void BindShader(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx, FDrawState& State);
	void EnsurePerObjectCBPoolCapacity(uint32 RequiredCount);
	FConstantBuffer* GetPerObjectCBForProxy(const FPrimitiveSceneProxy& Proxy);
	bool BindPerObjectCB(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx, FDrawState& State);
	void BindExtraCB(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx);
	bool BindMeshBuffer(FMeshBuffer* Buffer, ID3D11DeviceContext* Ctx, FDrawState& State);
	void DrawSections(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx, FDrawState& State);
	void DrawSingleSection(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx, FDrawState& State);
	void DrawSimple(const FPrimitiveSceneProxy& Proxy, ID3D11DeviceContext* Ctx, FDrawState& State);
	void CleanupSRV(ID3D11DeviceContext* Ctx, const FDrawState& State);

	// LineBatcher DrawBatch 공통 — EditorShader 바인딩 + DrawBatch
	void DrawLineBatcher(FLineBatcher& Batcher, ID3D11DeviceContext* Context);

	void DrawPostProcessFog(const FRenderBus& Bus, ID3D11DeviceContext* Context, FPostProcessIO& IO);
	// PostProcess Outline — StencilSRV 읽어 edge detection 후 fullscreen draw
	void DrawPostProcessOutline(const FRenderBus& Bus, ID3D11DeviceContext* Context, ID3D11ShaderResourceView* SceneColorSRV, ID3D11RenderTargetView* OutputRTV);
 // PostProcess FXAA — SceneColor를 입력으로 받아 fullscreen FXAA 적용
	void DrawPostProcessFXAA(const FRenderBus& Bus, ID3D11DeviceContext* Context, ID3D11ShaderResourceView* SceneColorSRV, ID3D11RenderTargetView* OutputRTV);
	
	void DrawScenenDepthVisualize(const FRenderBus& Bus, ID3D11DeviceContext* Context);
	void DrawPostProcessOverlays(const FRenderBus& InRenderBus, ID3D11DeviceContext* Context, bool bDrawFontOverlay);
	void ExecuteIdPickPass(const TArray<const FPrimitiveSceneProxy*>& Proxies, ID3D11DeviceContext* Context, FShader* PrimitiveShader, FShader* BillboardShader, FShader* StaticMeshShader);
	void RenderIdPickDebugVisualization(ID3D11DeviceContext* Context, ID3D11ShaderResourceView* IdPickSRV, ID3D11RenderTargetView* IdDebugRTV);
	//	SelectionMask 패스를 전용 마스크 RT로 실행
	void ExecuteSelectionMaskPass(const FRenderBus& Bus, ID3D11DeviceContext* Context);

	//	Post 체인(Decal/Fog/Outline/FXAA) 실행
	void ExecutePostProcessChain(const FRenderBus& Bus, ID3D11DeviceContext* Context);
	//	뷰포트 크기에 맞는 post 중간 RT/Mask를 보장
	void EnsurePostProcessTargets(const FRenderBus& Bus);
	void ReleasePostProcessTargets();
	//	SRV 내용을 RTV로 복사 (기본 pass-through/fallback)
	void BlitSRVToRTV(ID3D11ShaderResourceView* SourceSRV, ID3D11RenderTargetView* DestRTV, ID3D11DeviceContext* Context);

private:
	FD3DDevice Device;
	FRenderResources Resources;
	FLineBatcher   EditorLineBatcher;
	FLineBatcher   GridLineBatcher;
	FFontBatcher   FontBatcher;
	FSubUVBatcher  SubUVBatcher;
	FBillboardBatcher BillboardBatcher;

	// 정렬용 멤버 버퍼 (재할당 방지)
	TArray<const FPrimitiveSceneProxy*> SortedProxyBuffer;
	TArray<FSubUVEntry> SortedSubUVBuffer;
	TArray<FBillboardEntry> SortedBillboardBuffer;
	TArray<FConstantBuffer> PerObjectCBPool;
	FConstantBuffer IdPickPerObjectCB;
	ID3D11SamplerState* IdPickSampler = nullptr;
	ID3D11ShaderResourceView* ActiveDepthSRV = nullptr;

	FPassRenderState    PassRenderStates[(uint32)ERenderPass::MAX];
	FPassBatcherBinding PassBatchers[(uint32)ERenderPass::MAX];

	struct FPostEffectState
	{
		bool bEnabled = false;
		FPostEffectCallback Callback = nullptr;
	};
	std::array<FPostEffectState, (uint32)EPostEffectType::MAX> PostEffects = {};

	//	Post chain ping-pong color buffers
	ID3D11Texture2D* PostPingTexture[2] = { nullptr, nullptr };
	ID3D11RenderTargetView* PostPingRTV[2] = { nullptr, nullptr };
	ID3D11ShaderResourceView* PostPingSRV[2] = { nullptr, nullptr };

	//	SelectionMask 결과를 저장하는 아웃라인 마스크 RT
	ID3D11Texture2D* OutlineMaskTexture = nullptr;
	ID3D11RenderTargetView* OutlineMaskRTV = nullptr;
	ID3D11ShaderResourceView* OutlineMaskSRV = nullptr;

	uint32 PostTargetWidth = 0;
	uint32 PostTargetHeight = 0;
	FFXAAConstants FXAAConstants = {};
};
