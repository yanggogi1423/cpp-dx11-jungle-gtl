#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"

class UProjectionDecalComponent;

class FProjectionDecalSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FProjectionDecalSceneProxy(UProjectionDecalComponent* InComponent);
	~FProjectionDecalSceneProxy() override = default;

	void UpdateTransform() override;
	void UpdateMaterial() override;
	void UpdateMesh() override;
	void CollectEntries(FRenderBus& Bus) override;

private:
	UProjectionDecalComponent* GetProjectionDecalComponent() const;

private:
	FMeshBuffer OwnedMeshBuffer;
};

class FProjectionDecalArrowSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FProjectionDecalArrowSceneProxy(UProjectionDecalComponent* InComponent, bool bInner = false);

	void UpdateMesh() override;
	void UpdatePerViewport(const FRenderBus& Bus) override;

private:
	UProjectionDecalComponent* GetProjectionDecalComponent() const;

	bool bIsInner = false;
};

