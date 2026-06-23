#pragma once
#include "Core/CoreTypes.h"
#include "Render/Types/RenderTypes.h"
#include "Render/Resource/ComputeShader.h"
#include "Render/Resource/Buffer.h"

#include "Math/Vector.h"
#include "Math/Matrix.h"
#include <unordered_map>

class FViewContext;
class FPrimitiveProxy;
class FViewport;

struct FProxyAABB
{
	FVector Min;
	uint32 Id;
	FVector Max;
	uint32 Padding;
};

struct FViewportOcclusionState
{
	ID3D11Texture2D* HZBTexture = nullptr;
	ID3D11ShaderResourceView* HZBSRV = nullptr;
	TArray<ID3D11UnorderedAccessView*> HZBMipsUAV;
	TArray<ID3D11ShaderResourceView*> HZBMipsSRV;
	uint32 HZBMipCount = 0;
	uint32 HZBWidth = 0;
	uint32 HZBHeight = 0;

	// Double buffering for readback (N-2)
	ID3D11Buffer* ReadbackBuffers[2] = { nullptr, nullptr };
	TArray<uint32> ReadbackProxyIds[2];
	uint32 ReadbackIndex = 0;
	bool bReadbackReady[2] = { false, false };
	uint32 ReadbackCapacity = 0;

	std::vector<uint8> VisibilityArray;

	// Previous frame info for temporal stability
	FMatrix PrevViewProjection;
	bool bHasPrevViewProjection = false;

	void Release();
};

class FOcclusionManager
{
public:
	static FOcclusionManager& Get();

	void Initialize(ID3D11Device* InDevice);
	void Release();

	void BuildHZB(ID3D11DeviceContext* InContext, const FViewContext& InView);
	void ExecuteOcclusionTest(ID3D11DeviceContext* InContext, const FViewContext& InView, const TArray<FPrimitiveProxy*>& InProxies);

	ID3D11ShaderResourceView* GetHZBSRV(const FViewport* Viewport) const;
	uint32 GetHZBMipCount(const FViewport* Viewport) const;

	// 컬링 결과 반환 (N-2 프레임 지연된 결과일 수 있음)
	bool IsVisible(const FViewport* Viewport, uint32 ProxyId) const;

	// 뷰포트 삭제 시 리소스 해제
	void ReleaseViewportState(const FViewport* Viewport);

	// 통계
	void ResetStats() { NumTotalCandidates = 0; NumOccluded = 0; }
	void AddTotalCandidates(uint32 Count) { NumTotalCandidates += Count; }
	void AddOccluded(uint32 Count) { NumOccluded += Count; }
	uint32 GetTotalCandidates() const { return NumTotalCandidates; }
	uint32 GetOccludedCount() const { return NumOccluded; }

private:
	FOcclusionManager() = default;
	~FOcclusionManager() = default;

	void CreateHZBTexture(ID3D11Device* InDevice, uint32 Width, uint32 Height, FViewportOcclusionState& OutState);
	void UpdateGPUProxies(ID3D11DeviceContext* InContext, const TArray<FPrimitiveProxy*>& InProxies, FViewportOcclusionState& OutState);

private:
	FComputeShader HZBBuildCS;
	ID3D11Buffer* HZBConstantBuffer = nullptr;

	ID3D11SamplerState* PointClampSampler = nullptr;

	// Phase 2: Occlusion Test
	FComputeShader OcclusionTestCS;
	ID3D11Buffer* OcclusionTestCB = nullptr;

	ID3D11Buffer* ProxyBuffer = nullptr;
	ID3D11ShaderResourceView* ProxySRV = nullptr;
	uint32 ProxyBufferCapacity = 0;

	ID3D11Buffer* VisibilityBuffer = nullptr;
	ID3D11UnorderedAccessView* VisibilityUAV = nullptr;
	
	// Per-Viewport occlusion state
	std::unordered_map<const FViewport*, FViewportOcclusionState> ViewportStates;

	// Stats
	uint32 NumTotalCandidates = 0;
	uint32 NumOccluded = 0;
};
