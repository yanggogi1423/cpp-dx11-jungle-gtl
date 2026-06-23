#pragma once

#include "Object/ObjectFactory.h"
#include "SceneComponent.h"
#include "Render/Pipeline/ViewContext.h"
#include "Render/Types/RenderTypes.h"
#include "Core/RayTypes.h"
#include "Core/CollisionTypes.h"
#include "Core/EngineTypes.h"
#include "Render/Types/VertexTypes.h"


class FPrimitiveProxy;

class UPrimitiveComponent : public USceneComponent
{
	friend class FPrimitiveProxy;

public:
	DECLARE_CLASS(UPrimitiveComponent, USceneComponent)

	~UPrimitiveComponent() override;
	void DuplicateSubObjects() override;

	void OnRegister() override;
	void OnUnregister() override;
	
	void MarkRenderStateDirty();

	FPrimitiveProxy* GetProxy() const { return Proxy; }

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

	virtual FMeshBuffer* GetMeshBuffer() const { return nullptr; }
	virtual const FMeshData* GetMeshData() const { return nullptr; }

	inline void SetVisibility(bool bVisible) { bIsVisible = bVisible; }
	inline bool IsVisible() const { return bIsVisible; }

	// 월드 공간 AABB를 FBoundingBox로 반환 (파트 B LineBatcher와의 인터페이스)
	FBoundingBox GetWorldBoundingBox() const;

	//Collision
	virtual void UpdateWorldAABB() const;
	virtual bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult, float InClosestT = FLT_MAX);
	void UpdateWorldMatrix() const override;

	virtual bool SupportsOutline() const { return true; }
	
protected:
	virtual FPrimitiveProxy* CreateProxy();

protected:
	FPrimitiveProxy* Proxy = nullptr;
	FVector LocalExtents = { 0.5f, 0.5f, 0.5f };
	mutable FVector WorldAABBMinLocation;
	mutable FVector WorldAABBMaxLocation;
	bool bIsVisible = true;

};