#pragma once
#include "Render/Proxy/PrimitiveSceneProxy.h"

class UDecalComponent;

class FDecalSceneProxy : public FPrimitiveSceneProxy
{
public:
    explicit FDecalSceneProxy(UDecalComponent* InComponent);

    void UpdateMesh() override;
    void UpdateMaterial() override;
    void UpdateTransform() override;

	void CollectEntries(FRenderBus& Bus) override;

	// 매 프레임 카메라 기준 OBB-Frustum 컬링 수행 (bPerViewportUpdate = true)
	void UpdatePerViewport(const FRenderBus& Bus) override;
	uint32 GetLastOverlappingObjectCount() const { return LastOverlappingObjectCount; }

private:
    UDecalComponent* GetDecalComponent() const;
    void RebuildSectionDraw();

	// 지오메트리 교차 여부 (실제 데칼 프로젝션 렌더 여부)
	// bVisible과 분리하여 선택 시 OBB 박스는 항상 그리되, 프로젝션은 교차할 때만 수행
	bool bDecalProjectionVisible = false;
	uint32 LastOverlappingObjectCount = 0;
};

class FDecalArrowSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FDecalArrowSceneProxy(UDecalComponent* InComponent, bool bInner = false);

	void UpdateMesh() override;
	void UpdatePerViewport(const FRenderBus& Bus) override;

private:
	UDecalComponent* GetDecalComponent() const;

	bool bIsInner = false;
};
