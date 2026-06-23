#pragma once

#include "Render/Types/RenderTypes.h"

class AActor;
class FViewportClient;

// UE의 FViewport 대응 — 오프스크린 렌더 타깃 + D3D 리소스
class FViewport
{
public:
	FViewport() = default;
	~FViewport();

	// D3D 리소스 생성/해제/리사이즈
	bool Initialize(ID3D11Device* InDevice, uint32 InWidth, uint32 InHeight);
	void Release();
	void Resize(uint32 InWidth, uint32 InHeight);

	// 지연 리사이즈 — ImGui 렌더 중에 요청, RenderViewport 직전에 적용
	void RequestResize(uint32 InWidth, uint32 InHeight);
	bool ApplyPendingResize();

	// 오프스크린 RT 클리어 + 바인딩 (렌더 시작 시 호출)
	void BeginRender(ID3D11DeviceContext* Ctx, const float ClearColor[4] = nullptr);
	void BeginScopeLensRender(ID3D11DeviceContext* Ctx, const float ClearColor[4] = nullptr);

	// ViewportClient 참조
	void SetClient(FViewportClient* InClient) { ViewportClient = InClient; }
	FViewportClient* GetClient() const { return ViewportClient; }

	// 크기
	uint32 GetWidth() const { return Width; }
	uint32 GetHeight() const { return Height; }
	uint32 GetBloomWidth() const { return BloomWidth; }
	uint32 GetBloomHeight() const { return BloomHeight; }

	// D3D 리소스 접근자
	ID3D11RenderTargetView* GetRTV() const { return RTV; }
	ID3D11ShaderResourceView* GetSRV() const { return SRV; }
	ID3D11Texture2D* GetRTTexture() const { return RTTexture; }
	ID3D11ShaderResourceView* GetSceneColorCopySRV() const { return SceneColorCopySRV; }
	ID3D11Texture2D* GetSceneColorCopyTexture() const { return SceneColorCopyTexture; }
	ID3D11RenderTargetView* GetScopeLensRTV() const { return ScopeLensRTV; }
	ID3D11ShaderResourceView* GetScopeLensSRV() const { return ScopeLensSRV; }
	ID3D11DepthStencilView* GetDSV() const { return DSV; }
	ID3D11Texture2D* GetDepthTexture() const { return DepthTexture; }
	ID3D11Texture2D* GetEditorIdPickTexture() const { return EditorIdPickTexture; }
	ID3D11RenderTargetView* GetEditorIdPickRTV() const { return EditorIdPickRTV; }
	ID3D11ShaderResourceView* GetEditorIdPickSRV() const { return EditorIdPickSRV; }
	ID3D11Texture2D* GetEditorIdPickReadbackTexture() const { return EditorIdPickReadbackTexture; }
	ID3D11RenderTargetView* GetEditorIdPickDebugRTV() const { return EditorIdPickDebugRTV; }
	ID3D11ShaderResourceView* GetEditorIdPickDebugSRV() const { return EditorIdPickDebugSRV; }
	bool ReadEditorIdPickAt(uint32 X, uint32 Y, ID3D11DeviceContext* Ctx, uint32& OutPickId) const;
	void SetEditorIdPickActors(TArray<AActor*>&& InActors);
	AActor* GetEditorIdPickActor(uint32 PickId) const;

	// CopyResource 대상 — 패스 간 안전하게 Depth/Stencil 읽기용
	ID3D11Texture2D* GetDepthCopyTexture() const { return DepthCopyTexture; }
	ID3D11ShaderResourceView* GetDepthCopySRV() const { return DepthCopySRV; }
	ID3D11ShaderResourceView* GetStencilCopySRV() const { return StencilCopySRV; }

	// Depth of Field CoC RT
	ID3D11RenderTargetView* GetCoCRTV() const { return CoCRTV; }
	ID3D11ShaderResourceView* GetCoCSRV() const { return CoCSRV; }
	ID3D11RenderTargetView* GetDoFBackgroundRTV() const { return DoFBackgroundRTV; }
	ID3D11ShaderResourceView* GetDoFBackgroundSRV() const { return DoFBackgroundSRV; }
	ID3D11RenderTargetView* GetDoFForegroundRTV() const { return DoFForegroundRTV; }
	ID3D11ShaderResourceView* GetDoFForegroundSRV() const { return DoFForegroundSRV; }
	ID3D11RenderTargetView* GetDoFBokehRTV() const { return DoFBokehRTV; }
	ID3D11ShaderResourceView* GetDoFBokehSRV() const { return DoFBokehSRV; }
	uint32 GetDoFBokehWidth() const { return DoFBokehWidth; }
	uint32 GetDoFBokehHeight() const { return DoFBokehHeight; }

	// Bloom ping-pong RTs
	ID3D11RenderTargetView* GetBloomRTVA() const { return BloomRTVA; }
	ID3D11ShaderResourceView* GetBloomSRVA() const { return BloomSRVA; }
	ID3D11RenderTargetView* GetBloomRTVB() const { return BloomRTVB; }
	ID3D11ShaderResourceView* GetBloomSRVB() const { return BloomSRVB; }

	const D3D11_VIEWPORT& GetViewportRect() const { return ViewportRect; }

private:
	bool CreateResources();
	void ReleaseResources();

private:
	FViewportClient* ViewportClient = nullptr;

	ID3D11Device* Device = nullptr;

	// 렌더 타깃
	ID3D11Texture2D* RTTexture = nullptr;
	ID3D11RenderTargetView* RTV = nullptr;
	ID3D11ShaderResourceView* SRV = nullptr;		// ImGui::Image() 출력용

	// 뎁스/스텐실
	ID3D11Texture2D* DepthTexture = nullptr;
	ID3D11DepthStencilView* DSV = nullptr;

	ID3D11Texture2D* EditorIdPickTexture = nullptr;
	ID3D11RenderTargetView* EditorIdPickRTV = nullptr;
	ID3D11ShaderResourceView* EditorIdPickSRV = nullptr;
	ID3D11Texture2D* EditorIdPickReadbackTexture = nullptr;
	ID3D11Texture2D* EditorIdPickDebugTexture = nullptr;
	ID3D11RenderTargetView* EditorIdPickDebugRTV = nullptr;
	ID3D11ShaderResourceView* EditorIdPickDebugSRV = nullptr;
	TArray<AActor*> EditorIdPickActors;

	// CopyResource 대상 — DSV 전환 없이 안전하게 Depth/Stencil 읽기
	ID3D11Texture2D* DepthCopyTexture = nullptr;
	ID3D11ShaderResourceView* DepthCopySRV = nullptr;		// t16: SceneDepth
	ID3D11ShaderResourceView* StencilCopySRV = nullptr;	// t19: Stencil

	// SceneColor 복사본 — FXAA 등 PostProcess에서 최종 화면을 읽기 위한 CopyResource 대상
	ID3D11Texture2D* SceneColorCopyTexture = nullptr;
	ID3D11ShaderResourceView* SceneColorCopySRV = nullptr;

	ID3D11Texture2D* ScopeLensTexture = nullptr;
	ID3D11RenderTargetView* ScopeLensRTV = nullptr;
	ID3D11ShaderResourceView* ScopeLensSRV = nullptr;

	// DoF CoC RT — DoFSetup에서 R16_FLOAT로 기록, DoF composite에서 읽기
	ID3D11Texture2D* CoCTexture = nullptr;
	ID3D11RenderTargetView* CoCRTV = nullptr;
	ID3D11ShaderResourceView* CoCSRV = nullptr;

	// DoF layer RTs — background color and foreground color+alpha mask
	ID3D11Texture2D* DoFBackgroundTexture = nullptr;
	ID3D11RenderTargetView* DoFBackgroundRTV = nullptr;
	ID3D11ShaderResourceView* DoFBackgroundSRV = nullptr;
	ID3D11Texture2D* DoFForegroundTexture = nullptr;
	ID3D11RenderTargetView* DoFForegroundRTV = nullptr;
	ID3D11ShaderResourceView* DoFForegroundSRV = nullptr;
	ID3D11Texture2D* DoFBokehTexture = nullptr;
	ID3D11RenderTargetView* DoFBokehRTV = nullptr;
	ID3D11ShaderResourceView* DoFBokehSRV = nullptr;
	uint32 DoFBokehWidth = 0;
	uint32 DoFBokehHeight = 0;

	D3D11_VIEWPORT ViewportRect = {};

	uint32 Width = 0;
	uint32 Height = 0;
	uint32 BloomWidth = 0;
	uint32 BloomHeight = 0;

	//Bloom용 텍스쳐
	ID3D11Texture2D* BloomTextureA = nullptr;
	ID3D11RenderTargetView* BloomRTVA = nullptr;
	ID3D11ShaderResourceView* BloomSRVA = nullptr;

	ID3D11Texture2D* BloomTextureB = nullptr;
	ID3D11RenderTargetView* BloomRTVB = nullptr;
	ID3D11ShaderResourceView* BloomSRVB = nullptr;

	// 지연 리사이즈 요청
	uint32 PendingWidth = 0;
	uint32 PendingHeight = 0;
	bool bPendingResize = false;
};
