#pragma once

#include "Render/Proxy/BillboardSceneProxy.h"
#include "Core/Types/ResourceTypes.h"
#include "Math/Vector.h"
#include "Render/Types/RenderStateTypes.h"

class UTextRenderComponent;
class UMaterial;

// ============================================================
// FTextRenderSceneProxy — UTextRenderComponent 전용 프록시
// ============================================================
// Collector가 CachedText를 읽어 FFontGeometry로 배칭.
// PerObjectConstants는 SelectionMask 전용 아웃라인 행렬.
class FTextRenderSceneProxy : public FBillboardSceneProxy
{
public:
	FTextRenderSceneProxy(UTextRenderComponent* InComponent);
	~FTextRenderSceneProxy() override;

	void UpdateTransform() override;
	void UpdateMaterial() override;
	void UpdateMesh() override;
	void UpdatePerViewport(const FFrameContext& Frame) override;

	// Collector가 FFontGeometry 배칭에 사용하는 캐싱된 텍스트 데이터
	FString CachedText;
	float   CachedFontScale = 1.0f;
	FMatrix CachedBillboardMatrix;
	FVector CachedTextRight = FVector(1.0f, 0.0f, 0.0f);
	FVector CachedTextUp = FVector(0.0f, 0.0f, 1.0f);
	const FFontResource* CachedFont = nullptr;
	FVector4 CachedColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	EDepthStencilState CachedDepthStencil = EDepthStencilState::DepthReadOnly;
	float GetCachedCharWidth() const { return CachedCharWidth; }
	float GetCachedCharHeight() const { return CachedCharHeight; }
	float GetCachedSpacing() const { return CachedSpacing; }
	float GetCachedLineSpacing() const { return CachedLineSpacing; }
	int32 GetCachedHorizontalAlign() const { return CachedHorizontalAlign; }
	int32 GetCachedVerticalAlign() const { return CachedVerticalAlign; }

private:
	UTextRenderComponent* GetTextRenderComponent() const;
	UMaterial* TextMaterial = nullptr;
	FMatrix CachedComponentWorldMatrix = FMatrix::Identity;
	bool CachedBillboardEnabled = true;

	// 아웃라인 행렬 계산용 캐싱 데이터 (UpdateMesh에서 갱신)
	float CachedCharWidth  = 0.5f;
	float CachedCharHeight = 0.5f;
	float CachedSpacing    = 0.1f;
	float CachedLineSpacing = 0.0f;
	int32 CachedHorizontalAlign = 1;
	int32 CachedVerticalAlign = 1;
};
