#pragma once
#include "Core/CoreTypes.h"
#include "Render/Pipeline/RenderCommand.h"
#include "Render/Types/ViewTypes.h"

class FViewport;
class FPrimitiveProxy;

// 각 View마다 독립적으로 가져야 하는 정보들을 관리
// e.g. 시야 정보, 그리드, UUID 렌더링 정보 등
// 다만 현재는 SubUV, WorldText, AABB 등의 기능이 아직 WorldRenderProxy로 이관되지 못하고 남아있는 상태
class FViewContext
{
public:
	void Clear();
	void Reset();

	// Mesh 패스용 (Opaque, StencilMask, Outline, Gizmo, Translucent)
	void AddCommand(ERenderPass Pass, const FRenderCommand& InCommand);
	void AddCommand(ERenderPass Pass, FRenderCommand&& InCommand);
	void SortPass(ERenderPass Pass);

	const FPassQueueSoA& GetPassQueue(ERenderPass Pass) const { return PassQueues[(uint32)Pass]; }
	const TArray<FMeshSectionDraw>& GetGlobalSectionDraws() const { return GlobalSectionDraws; }
	uint32 GetPassSectionOffset(ERenderPass Pass) const { return PassSectionOffsets[(uint32)Pass]; }

	// DEPRECATED: Batcher 패스용 큐 (Font/SubUV/OverlayFont)
	void AddFontEntry(FFontEntry&& Entry);
	void AddOverlayFontEntry(FFontEntry&& Entry);
	void AddSubUVEntry(FSubUVEntry&& Entry);

	// 범용 큐
	void AddAABBEntry(FAABBEntry&& Entry);
	void AddGridProxy(FGridProxy&& Proxy);

	const TArray<FFontEntry>& GetFontEntries() const { return FontEntries; }
	const TArray<FFontEntry>& GetOverlayFontEntries() const { return OverlayFontEntries; }
	const TArray<FSubUVEntry>& GetSubUVEntries() const { return SubUVEntries; }
	const TArray<FAABBEntry>& GetAABBEntries() const { return AABBEntries; }
	const TArray<FGridProxy>& GetGridProxies() const { return GridProxies; }

	// 컬링 후보군 관리
	void AddCandidateProxy(FPrimitiveProxy* Proxy);
	void AddCandidateProxyUnique(FPrimitiveProxy* Proxy);
	const TArray<FPrimitiveProxy*>& GetCandidateProxies() const { return CandidateProxies; }
	TArray<FPrimitiveProxy*>& GetCandidateProxiesMutable() { return CandidateProxies; }

	// Getter,Setter
	void SetCameraInfo(
		const FMatrix& InView,
		const FMatrix& InProj,
		const FVector& InCameraForward,
		const FVector& InCameraRight,
		const FVector& InCameraUp,
		bool bInIsOrtho,
		float InOrthoWidth);
	void SetViewportInfo(const FViewport* VP);
	void SetViewportSize(float InWidth, float InHeight);
	void SetRenderOptions(const FViewportRenderOptions& InOptions);

	const FMatrix& GetView() const { return View; }
	const FMatrix& GetProj() const { return Proj; }
	const FVector& GetCameraForward() const { return CameraForward; }
	const FVector& GetCameraUp() const { return CameraUp; }
	const FVector& GetCameraRight() const { return CameraRight; }
	bool  IsOrtho()        const { return bIsOrtho; }
	bool  IsFixedOrtho()   const { return bIsOrtho && Options.ViewportType != ELevelViewportType::Perspective && Options.ViewportType != ELevelViewportType::FreeOrthographic; }
	float GetOrthoWidth()  const { return OrthoWidth; }
	ELevelViewportType GetViewportType() const { return Options.ViewportType; }
	void SetViewportType(ELevelViewportType InType) { Options.ViewportType = InType; }
	EViewMode GetViewMode() const { return Options.ViewMode; }
	const FShowFlags& GetShowFlags() const { return Options.ShowFlags; }
	const FViewportRenderOptions& GetRenderOptions() const { return Options; }
	const FVector& GetWireframeColor() const { return WireframeColor; }
	void SetWireframeColor(const FVector& InColor) { WireframeColor = InColor; }

	const float GetViewportWidth() const { return viewportWidth; }
	const float GetViewportHeight() const { return viewportHeight; }
	const FViewport* GetViewport()	const { return Viewport; }
	ID3D11RenderTargetView*  GetViewportRTV()        const { return ViewportRTV; }
	ID3D11DepthStencilView*  GetViewportDSV()        const { return ViewportDSV; }
	ID3D11ShaderResourceView* GetViewportDepthSRV()   const { return ViewportDepthSRV; }
	ID3D11ShaderResourceView* GetViewportStencilSRV() const { return ViewportStencilSRV; }

	// View별 로컬 요소 (그리드 등) 수집
	void CollectViewElements();

private:
	// Mesh 패스 큐 (SoA)
	FPassQueueSoA PassQueues[(uint32)ERenderPass::MAX];
	TArray<FMeshSectionDraw> GlobalSectionDraws;
	uint32 PassSectionOffsets[(uint32)ERenderPass::MAX] = {};

	// DEPRECATED: Batcher 패스 큐 (Font/SubUV/OverlayFont)
	TArray<FFontEntry>  FontEntries;
	TArray<FFontEntry>  OverlayFontEntries;
	TArray<FSubUVEntry> SubUVEntries;

	// 범용 큐
	TArray<FAABBEntry>  AABBEntries;
	TArray<FGridProxy>  GridProxies;
	TArray<FPrimitiveProxy*> CandidateProxies;

	FMatrix View;
	FMatrix Proj;
	FVector CameraForward;
	FVector CameraRight;
	FVector CameraUp;

	float viewportWidth = 0.0f;
	float viewportHeight = 0.0f;

	bool  bIsOrtho = false;
	float OrthoWidth = 10.0f;

	// PostProcess용 뷰포트 D3D 리소스 (프레임 내 유효)
	ID3D11RenderTargetView*   ViewportRTV        = nullptr;
	ID3D11DepthStencilView*   ViewportDSV        = nullptr;
	ID3D11ShaderResourceView* ViewportDepthSRV   = nullptr;
	ID3D11ShaderResourceView* ViewportStencilSRV = nullptr;

	// 어느 Viewport에 그려질 예정인지
	const FViewport* Viewport = nullptr;

	//Editor Settings
	FViewportRenderOptions Options;
	FVector WireframeColor = FVector(0.0f, 0.0f, 0.7f);
};

