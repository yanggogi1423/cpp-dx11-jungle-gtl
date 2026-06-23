#pragma once
#include "Render/Pipeline/RenderCommand.h"
#include "Render/Pipeline/ViewContext.h"

class UPrimitiveComponent;
class FWorldRenderProxy;

class FPrimitiveProxy
{
public:
	FPrimitiveProxy(UPrimitiveComponent* InOwner);
	virtual ~FPrimitiveProxy() = default;

	virtual void UpdateProxy() = 0;
	virtual void SubmitRenderCommand(FViewContext& View);

	void MarkDirty() { bIsDirty = true; }
	bool IsDirty() const { return bIsDirty; }

	UPrimitiveComponent* GetOwner() const { return Owner; }
	void SetSelected(bool bInSelected) { bSelected = bInSelected; }

	// WorldRenderProxy 연동
	void SetWorldRenderProxy(FWorldRenderProxy* InProxy) { WorldProxy = InProxy; }
	FWorldRenderProxy* GetWorldRenderProxy() const { return WorldProxy; }

	uint32 GetId() const;
	struct FBoundingBox GetAABB() const;
	void MarkFrustumVisibleForPick(uint32 FrameTag) { LastFrustumVisiblePickFrameTag = FrameTag; }
	bool IsFrustumVisibleForPick(uint32 FrameTag) const { return LastFrustumVisiblePickFrameTag == FrameTag; }

	// Occlusion용 캐시 — Owner 포인터 체이싱 제거 (L2 캐시 미스 방지)
	FVector CachedAABBMin;
	FVector CachedAABBMax;
	uint32 CachedProxyId = 0;
	uint32 LastFrustumVisiblePickFrameTag = 0;
	void RefreshOcclusionCache();

protected:
	UPrimitiveComponent* Owner;
	FWorldRenderProxy* WorldProxy = nullptr;
	FRenderCommand CachedCommand;
	bool bIsDirty = true;
	bool bSelected = false;
};

class FDefaultPrimitiveProxy : public FPrimitiveProxy
{
public:
	FDefaultPrimitiveProxy(UPrimitiveComponent* InOwner);
	void UpdateProxy() override;
};
