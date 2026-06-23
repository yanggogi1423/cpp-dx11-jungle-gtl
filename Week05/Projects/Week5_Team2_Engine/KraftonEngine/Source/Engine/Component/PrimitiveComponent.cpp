#include "PrimitiveComponent.h"
#include "Core/RayTypes.h"
#include "Collision/RayUtils.h"
#include "Core/CollisionTypes.h"
#include "GameFramework/World.h"
#include "GameFramework/Level.h"
#include "Render/Pipeline/WorldRenderProxy.h"
#include "Render/Pipeline/PrimitiveProxy.h"

DEFINE_CLASS(UPrimitiveComponent, USceneComponent)


UPrimitiveComponent::~UPrimitiveComponent()
{
	if (Proxy)
	{
		delete Proxy;
		Proxy = nullptr;
	}
}

void UPrimitiveComponent::DuplicateSubObjects()
{
	USceneComponent::DuplicateSubObjects();
	Proxy = CreateProxy();
}

FPrimitiveProxy* UPrimitiveComponent::CreateProxy()
{
	return new FDefaultPrimitiveProxy(this);
}

void UPrimitiveComponent::OnRegister()
{
	if (!Proxy)
	{
		Proxy = CreateProxy();
	}

	// 등록 직후 frustum/octree가 이 컴포넌트를 읽을 수 있으므로
	// transform 변경이 없던 컴포넌트도 최소 1회 AABB를 보장한다.
	UpdateWorldAABB();

	ULevel* Level = GetLevel();
	if (Proxy && Level)
	{
		Level->GetRenderProxy().AddProxy(Proxy);
	}
}

void UPrimitiveComponent::OnUnregister()
{
	ULevel* Level = GetLevel();
	if (Proxy && Level)
	{
		Level->GetRenderProxy().RemoveProxy(Proxy);
		delete Proxy;
		Proxy = nullptr;
	}
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	if (Proxy)
	{
		Proxy->MarkDirty();
	}
}

void UPrimitiveComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	USceneComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Visible", EPropertyType::Bool, &bIsVisible });
}

FBoundingBox UPrimitiveComponent::GetWorldBoundingBox() const
{
	return FBoundingBox(WorldAABBMinLocation, WorldAABBMaxLocation);
}

void UPrimitiveComponent::UpdateWorldAABB() const
{
	FVector LExt = LocalExtents;

	FMatrix worldMatrix = GetWorldMatrix();

	float NewEx = std::abs(worldMatrix.M[0][0]) * LExt.X + std::abs(worldMatrix.M[1][0]) * LExt.Y + std::abs(worldMatrix.M[2][0]) * LExt.Z;
	float NewEy = std::abs(worldMatrix.M[0][1]) * LExt.X + std::abs(worldMatrix.M[1][1]) * LExt.Y + std::abs(worldMatrix.M[2][1]) * LExt.Z;
	float NewEz = std::abs(worldMatrix.M[0][2]) * LExt.X + std::abs(worldMatrix.M[1][2]) * LExt.Y + std::abs(worldMatrix.M[2][2]) * LExt.Z;

	FVector WorldCenter = GetWorldLocation();
	WorldAABBMinLocation = WorldCenter - FVector(NewEx, NewEy, NewEz);
	WorldAABBMaxLocation = WorldCenter + FVector(NewEx, NewEy, NewEz);
}

bool UPrimitiveComponent::LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult, float InClosestT)
{
	const FMeshData* Data = GetMeshData();
	if (!Data || Data->Indices.empty()) return false;

	bool bHit = FRayUtils::RaycastTriangles(
		Ray, CachedWorldMatrix,
		&Data->Vertices[0].Position,
		sizeof(FVertex),
		Data->Indices,
		OutHitResult,
		InClosestT);

	if (bHit)
	{
		OutHitResult.HitComponent = this;
	}
	return bHit;
}

void UPrimitiveComponent::UpdateWorldMatrix() const
{
	if (!bTransformDirty)
	{
		return;
	}

	USceneComponent::UpdateWorldMatrix();
	UpdateWorldAABB();
	const_cast<UPrimitiveComponent*>(this)->MarkRenderStateDirty();

	if (ULevel* Level = GetLevel())
	{
		Level->GetRenderProxy().MarkSpatialIndexDirty();
	}
	if (Proxy)
	{
		Proxy->MarkDirty();
	}
}
