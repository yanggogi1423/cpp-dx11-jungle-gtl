#pragma once
#include "Core/CoreTypes.h"
#include "Render/Pipeline/RenderConstants.h"
#include "Render/Types/ViewTypes.h"
#include "Render/Culling/ConvexVolume.h"

class UCameraComponent;
class FViewport;
class FPrimitiveSceneProxy;
class FGPUOcclusionCulling;

#include "Render/Pipeline/LODContext.h"


class FRenderBus
{
public:
	void Clear();

	// 프록시 직접 제출 — 포인터만 저장, 데이터는 프록시 소유
	void AddProxy(ERenderPass Pass, const FPrimitiveSceneProxy* Proxy);
	const TArray<const FPrimitiveSceneProxy*>& GetProxies(ERenderPass Pass) const;

	// Batcher 패스용 — 타입 안전한 전용 큐
	void AddFontEntry(FFontEntry&& Entry);
	void AddOverlayFontEntry(FFontEntry&& Entry);
	void AddSubUVEntry(FSubUVEntry&& Entry);
	void AddBillboardEntry(FBillboardEntry&& Entry);
	void AddAABBEntry(FAABBEntry&& Entry);
	void AddOBBEntry(FOBBEntry&& Entry);
	void AddGridEntry(FGridEntry&& Entry);
	void AddDebugLineEntry(FDebugLineEntry&& Entry);
	void SetSceneEffectConstants(const FSceneEffectConstants& InConstants) { SceneEffectConstants = InConstants; }
	void SetFogPostProcessConstants(const FFogPostProcessConstants& InConstants) { FogPostProcessConstants = InConstants; }

	const TArray<FFontEntry>& GetFontEntries() const { return FontEntries; }
	const TArray<FFontEntry>& GetOverlayFontEntries() const { return OverlayFontEntries; }
	const TArray<FSubUVEntry>& GetSubUVEntries() const { return SubUVEntries; }
	const TArray<FBillboardEntry>& GetBillboardEntries() const { return BillboardEntries; }
	const TArray<FAABBEntry>& GetAABBEntries() const { return AABBEntries; }
	const TArray<FOBBEntry>& GetOBBEntries() const { return OBBEntries; }
	const TArray<FGridEntry>& GetGridEntries() const { return GridEntries; }
	const TArray<FDebugLineEntry>& GetDebugLineEntries() const { return DebugLineEntries; }
	const FSceneEffectConstants& GetSceneEffectConstants() const { return SceneEffectConstants; }
	const FFogPostProcessConstants& GetFogPostProcessConstants() const { return FogPostProcessConstants; }

	// Getter,Setter
	void SetCameraInfo(const UCameraComponent* Camera);
	void SetViewportInfo(const FViewport* VP);
	void SetViewportSize(float InWidth, float InHeight);
	void SetRenderSettings(const EViewMode NewViewMode, const FShowFlags NewShowFlags);

	const FMatrix& GetView() const { return View; }
	const FMatrix& GetProj() const { return Proj; }
	const FVector& GetCameraPosition() const { return CameraPosition; }
	const FVector& GetCameraForward() const { return CameraForward; }
	const FVector& GetCameraUp() const { return CameraUp; }
	const FVector& GetCameraRight() const { return CameraRight; }
	float GetNearPlane() const { return NearPlane; }
	float GetFarPlane() const { return FarPlane; }
	bool  IsOrtho()        const { return bIsOrtho; }
	bool  IsFixedOrtho()   const { return bIsOrtho && ViewportType != ELevelViewportType::Perspective && ViewportType != ELevelViewportType::FreeOrthographic; }
	float GetOrthoWidth()  const { return OrthoWidth; }
	ELevelViewportType GetViewportType() const { return ViewportType; }
	void SetViewportType(ELevelViewportType InType) { ViewportType = InType; }
	EViewMode GetViewMode() const { return ViewMode; }
	const FShowFlags& GetShowFlags() const { return ShowFlags; }
	const FVector& GetWireframeColor() const { return WireframeColor; }
	void SetWireframeColor(const FVector& InColor) { WireframeColor = InColor; }

	const float GetViewportWidth() const { return viewportWidth; }
	const float GetViewportHeight() const { return viewportHeight; }
	ID3D11RenderTargetView*  GetViewportRTV()        const { return ViewportRTV; }
	ID3D11DepthStencilView*  GetViewportDSV()        const { return ViewportDSV; }
	//	뷰포트 SceneColor SRV (post chain 입력)
	ID3D11ShaderResourceView* GetViewportSRV()       const { return ViewportSRV; }
	//	뷰포트 Depth SRV (Fog/Decal/DepthDebug 입력)
	ID3D11ShaderResourceView* GetViewportDepthSRV()  const { return ViewportDepthSRV; }
	ID3D11ShaderResourceView* GetViewportStencilSRV() const { return ViewportStencilSRV; }

	// GPU Occlusion Culling — set by render pipeline, read by collector
	void SetOcclusionCulling(FGPUOcclusionCulling* InOcclusion) { OcclusionCulling = InOcclusion; }
	const FGPUOcclusionCulling* GetOcclusionCulling() const { return OcclusionCulling; }
	FGPUOcclusionCulling* GetOcclusionCullingMutable() const { return OcclusionCulling; }

	// LOD Update Context — WorldTick에서 설정, Collect에서 사용
	void SetLODContext(const FLODUpdateContext& InCtx) { LODContext = InCtx; }
	const FLODUpdateContext& GetLODContext() const { return LODContext; }

	// Frustum - SetCameraInfo 시 View*Proj로 자동 갱신, UpdatePerViewport에서 OBB 컬링 사용
	const FConvexVolume& GetConvexVolume() const { return CachedConvexVolume; }

private:
	// 프록시 패스 큐 — 포인터만 저장, 데이터는 프록시 소유
	TArray<const FPrimitiveSceneProxy*> ProxyQueues[(uint32)ERenderPass::MAX];

	// Batcher 패스 큐
	TArray<FFontEntry>  FontEntries;
	TArray<FFontEntry>  OverlayFontEntries;
	TArray<FSubUVEntry> SubUVEntries;
	TArray<FBillboardEntry> BillboardEntries;
	TArray<FAABBEntry>  AABBEntries;
	TArray<FOBBEntry>   OBBEntries;
	TArray<FGridEntry>  GridEntries;
	TArray<FDebugLineEntry> DebugLineEntries;
	FSceneEffectConstants SceneEffectConstants = {};
	FFogPostProcessConstants FogPostProcessConstants = {};

	FMatrix View;
	FMatrix Proj;
	FVector CameraPosition;
	FVector CameraForward;
	FVector CameraRight;
	FVector CameraUp;
	float NearPlane = 0.1f;
	float FarPlane = 1000.0f;

	float viewportWidth = 0.0f;
	float viewportHeight = 0.0f;

	bool  bIsOrtho = false;
	float OrthoWidth = 10.0f;
	ELevelViewportType ViewportType = ELevelViewportType::Perspective;

	// PostProcess용 뷰포트 D3D 리소스 (프레임 내 유효)
	ID3D11RenderTargetView*   ViewportRTV        = nullptr;
	ID3D11DepthStencilView*   ViewportDSV        = nullptr;
	//	post 체인이 읽는 원본 SceneColor
	ID3D11ShaderResourceView* ViewportSRV        = nullptr;
	//	post 체인이 읽는 원본 Depth
	ID3D11ShaderResourceView* ViewportDepthSRV   = nullptr;
	ID3D11ShaderResourceView* ViewportStencilSRV = nullptr;

	// GPU Occlusion
	FGPUOcclusionCulling* OcclusionCulling = nullptr;

	// LOD
	FLODUpdateContext LODContext;

	// Frustum (View*Proj로부터 매 프레임 갱신)
	FConvexVolume CachedConvexVolume;

	//Editor Settings
	EViewMode ViewMode;
	FShowFlags ShowFlags;
	FVector WireframeColor = FVector(0.0f, 0.0f, 0.7f);
};
