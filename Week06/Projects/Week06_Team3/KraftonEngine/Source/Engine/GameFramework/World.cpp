#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ProjectionDecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CameraComponent.h"	
#include "Components/TextRenderComponent.h"
#include "Engine/Render/Culling/ConvexVolume.h"
#include "Render/Pipeline/LODContext.h"
#include "Collision/RayUtils.h"
#include <cmath>
#include <algorithm>
#include <cfloat>
#include "Profiling/Stats.h"

IMPLEMENT_CLASS(UWorld, UObject)

namespace
{
	constexpr float VisibleCameraMoveThresholdSq = 0.0001f;
	constexpr float VisibleCameraRotationDotThreshold = 0.99999f;

	bool NearlyEqual(float A, float B, float Epsilon = 0.0001f)
	{
		return std::abs(A - B) <= Epsilon;
	}
}

UWorld::~UWorld()
{
	if (PersistentLevel && !GetActors().empty())
	{
		EndPlay();
	}
}

UObject* UWorld::Duplicate(UObject* NewOuter) const
{
	// UE의 CreatePIEWorldByDuplication 대응 (간소화 버전).
	// 새 UWorld를 만들고, 소스의 Actor들을 하나씩 복제해 NewWorld를 Outer로 삼아 등록한다.
	// AActor::Duplicate 내부에서 Dup->GetTypedOuter<UWorld>() 경유 AddActor가 호출되므로
	// 여기서는 World 단위 상태만 챙기면 된다.
	UWorld* NewWorld = UObjectManager::Get().CreateObject<UWorld>();
	if (!NewWorld)
	{
		return nullptr;
	}
	NewWorld->SetOuter(NewOuter);
	NewWorld->InitWorld(); // Partition/VisibleSet 초기화 — 이거 없으면 복제 액터가 렌더링되지 않음

	for (AActor* Src : GetActors())
	{
		if (!Src) continue;
		Src->Duplicate(NewWorld);
	}

	NewWorld->PostDuplicate();
	return NewWorld;
}

void UWorld::DestroyActor(AActor* Actor)
{
	// remove and clean up
	if (!Actor) return;
	Actor->EndPlay();
	// Remove from actor list
	PersistentLevel->RemoveActor(Actor);

	MarkWorldPrimitivePickingBVHDirty();
	InvalidateVisibleSet();
	Partition.RemoveActor(Actor);
	MarkProjectionDecalsDirty();

	// Mark for garbage collection
	UObjectManager::Get().DestroyObject(Actor);
}

void UWorld::AddActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	PersistentLevel->AddActor(Actor);

	// 생성자 시점에 AddComponent된 컴포넌트는 Owner/World가 아직 완성되지 않아
	// CreateRenderState가 early-return될 수 있다. 월드 등록 시점에 한 번 더 보장한다.
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (Comp)
		{
			Comp->CreateRenderState();
		}
	}

	InsertActorToOctree(Actor);
	MarkWorldPrimitivePickingBVHDirty();
	InvalidateVisibleSet();
	MarkProjectionDecalsDirty();

	// PIE 중 Duplicate(Ctrl+D)나 SpawnActor로 들어온 액터에도 BeginPlay를 보장.
	if (bHasBegunPlay && !Actor->HasActorBegunPlay())
	{
		Actor->BeginPlay();
	}
}

void UWorld::MarkWorldPrimitivePickingBVHDirty()
{
	if (DeferredPickingBVHUpdateDepth > 0)
	{
		bDeferredPickingBVHDirty = true;
		return;
	}

	WorldPrimitivePickingBVH.MarkDirty();
}

void UWorld::MarkProjectionDecalsDirty(const UPrimitiveComponent* ChangedPrimitive)
{
	for (AActor* Actor : GetActors())
	{
		if (!Actor)
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			UProjectionDecalComponent* ProjectionDecalComponent = Cast<UProjectionDecalComponent>(Component);
			if (!ProjectionDecalComponent || ProjectionDecalComponent == ChangedPrimitive)
			{
				continue;
			}

			// Projection decal render meshes cache receiver geometry in world space,
			// so receiver transform/visibility changes must invalidate that cache.
			ProjectionDecalComponent->MarkProjectionDecalDirty();
		}
	}
}

void UWorld::InvalidateVisibleSet()
{
	Scene.InvalidateVisibleSet();
}

void UWorld::BuildWorldPrimitivePickingBVHNow() const
{
	WorldPrimitivePickingBVH.BuildNow(GetActors());
}

void UWorld::BeginDeferredPickingBVHUpdate()
{
	++DeferredPickingBVHUpdateDepth;
}

void UWorld::EndDeferredPickingBVHUpdate()
{
	if (DeferredPickingBVHUpdateDepth <= 0)
	{
		return;
	}

	--DeferredPickingBVHUpdateDepth;
	if (DeferredPickingBVHUpdateDepth == 0 && bDeferredPickingBVHDirty)
	{
		bDeferredPickingBVHDirty = false;
		BuildWorldPrimitivePickingBVHNow();
	}
}

void UWorld::WarmupPickingData() const
{
	for (AActor* Actor : GetActors())
	{
		if (!Actor || !Actor->IsVisible())
		{
			continue;
		}

		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (!Primitive || !Primitive->IsVisible() || !Primitive->IsA<UStaticMeshComponent>())
			{
				continue;
			}

			UStaticMeshComponent* StaticMeshComponent = static_cast<UStaticMeshComponent*>(Primitive);
			if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
			{
				StaticMesh->EnsureMeshTrianglePickingBVHBuilt();
			}
		}
	}

	BuildWorldPrimitivePickingBVHNow();
}

bool UWorld::RaycastPrimitives(const FRay& Ray, FHitResult& OutHitResult, AActor*& OutActor) const
{
	//혹시라도 BVH 트리가 업데이트 되지 않았다면 업데이트
	WorldPrimitivePickingBVH.EnsureBuilt(GetActors());
	return WorldPrimitivePickingBVH.Raycast(Ray, OutHitResult, OutActor);
}

bool UWorld::RaycastPrimitivesById(const FRay& Ray, FHitResult& OutHitResult, AActor*& OutActor) const
{
	OutHitResult = {};
	OutHitResult.Distance = FLT_MAX;
	OutActor = nullptr;

	TArray<UPrimitiveComponent*> CandidatePrimitives;
	Partition.QueryRayAllPrimitive(Ray, CandidatePrimitives);
	if (CandidatePrimitives.empty())
	{
		return false;
	}

	float BestDistance = FLT_MAX;
	UPrimitiveComponent* BestComponent = nullptr;
	uint32 BestActorUUID = UINT32_MAX;

	for (UPrimitiveComponent* Primitive : CandidatePrimitives)
	{
		if (!Primitive || !Primitive->IsVisible())
		{
			continue;
		}
		if (!Primitive->SupportsPicking() || Primitive->IsA<UTextRenderComponent>())
		{
			continue;
		}

		AActor* OwnerActor = Primitive->GetOwner();
		if (!OwnerActor || !OwnerActor->IsVisible())
		{
			continue;
		}

		float TMin = 0.0f;
		float TMax = 0.0f;
		const FBoundingBox Bounds = Primitive->GetWorldBoundingBox();
		if (!FRayUtils::IntersectRayAABB(Ray, Bounds.Min, Bounds.Max, TMin, TMax))
		{
			continue;
		}

		const float HitDistance = (TMin >= 0.0f) ? TMin : 0.0f;
		if (HitDistance < BestDistance)
		{
			BestDistance = HitDistance;
			BestComponent = Primitive;
			BestActorUUID = OwnerActor->GetUUID();
		}
	}

	if (!BestComponent || BestActorUUID == UINT32_MAX)
	{
		return false;
	}

	AActor* ResolvedActor = Cast<AActor>(UObjectManager::Get().FindByUUID(BestActorUUID));
	if (!ResolvedActor)
	{
		return false;
	}

	OutHitResult.bHit = true;
	OutHitResult.Distance = BestDistance;
	OutHitResult.HitComponent = BestComponent;
	OutHitResult.WorldHitLocation = Ray.Origin + (Ray.Direction * BestDistance);
	OutActor = ResolvedActor;
	return true;
}


void UWorld::InsertActorToOctree(AActor* Actor)
{
	Partition.InsertActor(Actor);
	InvalidateVisibleSet();
}

void UWorld::RemoveActorToOctree(AActor* Actor)
{
	Partition.RemoveActor(Actor);
	InvalidateVisibleSet();
}

void UWorld::UpdateActorInOctree(AActor* Actor)
{
	Partition.UpdateActor(Actor);
	InvalidateVisibleSet();
}

// LOD 상수 및 SelectLOD는 LODContext.h에 정의

static float DistanceSquared(const FVector& A, const FVector& B)
{
	const FVector D = A - B;
	return D.X * D.X + D.Y * D.Y + D.Z * D.Z;
}

bool UWorld::NeedsVisibleProxyRebuild() const
{
	if (Scene.IsVisibleSetDirty() || !bHasVisibleCameraState || !ActiveCamera)
	{
		return true;
	}
	if (ActiveCamera != LastVisibleCamera)
	{
		return true;
	}

	const FVector CameraPos = ActiveCamera->GetWorldLocation();
	if (DistanceSquared(CameraPos, LastVisibleCameraPos) >= VisibleCameraMoveThresholdSq)
	{
		return true;
	}

	const FVector CameraForward = ActiveCamera->GetForwardVector();
	if (CameraForward.Dot(LastVisibleCameraForward) < VisibleCameraRotationDotThreshold)
	{
		return true;
	}

	const FCameraState& CameraState = ActiveCamera->GetCameraState();
	return !NearlyEqual(CameraState.FOV, LastVisibleCameraState.FOV)
		|| !NearlyEqual(CameraState.AspectRatio, LastVisibleCameraState.AspectRatio)
		|| !NearlyEqual(CameraState.NearZ, LastVisibleCameraState.NearZ)
		|| !NearlyEqual(CameraState.FarZ, LastVisibleCameraState.FarZ)
		|| !NearlyEqual(CameraState.OrthoWidth, LastVisibleCameraState.OrthoWidth)
		|| CameraState.bIsOrthogonal != LastVisibleCameraState.bIsOrthogonal;
}

void UWorld::CacheVisibleCameraState()
{
	if (!ActiveCamera)
	{
		bHasVisibleCameraState = false;
		LastVisibleCamera = nullptr;
		return;
	}

	LastVisibleCamera = ActiveCamera;
	LastVisibleCameraPos = ActiveCamera->GetWorldLocation();
	LastVisibleCameraForward = ActiveCamera->GetForwardVector();
	LastVisibleCameraState = ActiveCamera->GetCameraState();
	bHasVisibleCameraState = true;
}

void UWorld::RemoveVisibleProxy(FPrimitiveSceneProxy* Proxy, uint32 Index)
{
	//if (Index != UINT32_MAX)
	//{
	//	// swap-pop
	//	FPrimitiveSceneProxy* Last = VisibleProxies.back();
	//	VisibleProxies[Index] = Last;
	//	VisibleProxies.pop_back();

	//	if (Last != Proxy)
	//		Last->VisibleListIndex = Index;

	//	Proxy->bInVisibleSet = false;
	//	Proxy->VisibleListIndex = UINT32_MAX;
	//	delete Proxy;
	//}
}

void UWorld::UpdateVisibleProxies()
{
	TArray<FPrimitiveSceneProxy*>& VisibleProxies = Scene.GetVisibleProxiesMutable();

	if (!ActiveCamera)
	{
		for (FPrimitiveSceneProxy* Proxy : VisibleProxies)
		{
			if (!Proxy)
			{
				continue;
			}

			Proxy->bInVisibleSet = false;
			Proxy->VisibleListIndex = UINT32_MAX;
		}

		VisibleProxies.clear();
		bHasVisibleCameraState = false;
		LastVisibleCamera = nullptr;
		return;
	}

	if (!NeedsVisibleProxyRebuild())
	{
		return;
	}

	for (FPrimitiveSceneProxy* Proxy : VisibleProxies)
	{
		if (!Proxy)
		{
			continue;
		}

		Proxy->bInVisibleSet = false;
		Proxy->VisibleListIndex = UINT32_MAX;
	}

	VisibleProxies.clear();

	// HEAD: capacity 예약으로 재할당 방지
	const uint32 ExpectedProxyCount = Scene.GetProxyCount();
	if (VisibleProxies.capacity() < ExpectedProxyCount)
	{
		VisibleProxies.reserve(ExpectedProxyCount);
	}

	const uint32 ExpectedVisibleProxyCount =
		ExpectedProxyCount + static_cast<uint32>(Scene.GetNeverCullProxies().size());
	if (VisibleProxies.capacity() < ExpectedVisibleProxyCount)
	{
		VisibleProxies.reserve(ExpectedVisibleProxyCount);
	}

	{
		SCOPE_STAT_CAT("FrustumCulling", "1_WorldTick");
		FConvexVolume ConvexVolume = ActiveCamera->GetConvexVolume();
		Partition.QueryFrustumAllProxies(ConvexVolume, VisibleProxies);
	}

	for (uint32 Index = 0; Index < static_cast<uint32>(VisibleProxies.size()); ++Index)
	{
		FPrimitiveSceneProxy* Proxy = VisibleProxies[Index];
		if (!Proxy)
		{
			continue;
		}

		Proxy->bInVisibleSet = true;
		Proxy->VisibleListIndex = Index;
	}

	// NeverCull 프록시 추가 (LOD 갱신은 Collect 단계에서 병합 처리)
	for (FPrimitiveSceneProxy* Proxy : Scene.GetNeverCullProxies())
	{
		if (!Proxy || Proxy->bInVisibleSet)
		{
			continue;
		}

		Proxy->bInVisibleSet = true;
		Proxy->VisibleListIndex = static_cast<uint32>(VisibleProxies.size());
		VisibleProxies.push_back(Proxy);
	}

	CacheVisibleCameraState();
	Scene.MarkVisibleSetClean();
}

void UWorld::GatherVisibleProxiesForCamera(const UCameraComponent* InCamera, TArray<FPrimitiveSceneProxy*>& OutVisibleProxies) const
{
	OutVisibleProxies.clear();
	if (!InCamera)
	{
		return;
	}

	const uint32 ExpectedProxyCount = Scene.GetProxyCount();
	if (OutVisibleProxies.capacity() < ExpectedProxyCount)
	{
		OutVisibleProxies.reserve(ExpectedProxyCount);
	}

	{
		SCOPE_STAT_CAT("FrustumCulling", "1_WorldTick");
		FConvexVolume ConvexVolume = InCamera->GetConvexVolume();
		Partition.QueryFrustumAllProxies(ConvexVolume, OutVisibleProxies);
	}

	// NeverCull 프록시는 frustum 결과와 무관하게 항상 포함
	for (FPrimitiveSceneProxy* Proxy : Scene.GetNeverCullProxies())
	{
		if (!Proxy)
		{
			continue;
		}

		if (std::find(OutVisibleProxies.begin(), OutVisibleProxies.end(), Proxy) == OutVisibleProxies.end())
		{
			OutVisibleProxies.push_back(Proxy);
		}
	}
}

FLODUpdateContext UWorld::PrepareLODContext()
{
	return PrepareLODContextForCamera(ActiveCamera);
}

FLODUpdateContext UWorld::PrepareLODContextForCamera(const UCameraComponent* InCamera)
{
	if (!InCamera) return {};

	const FVector CameraPos = InCamera->GetWorldLocation();
	const FVector CameraForward = InCamera->GetForwardVector();

	const uint32 LODUpdateFrame = VisibleProxyBuildFrame++;
	const uint32 LODUpdateSlice = LODUpdateFrame & (LOD_UPDATE_SLICE_COUNT - 1);
	const bool bShouldStaggerLOD = Scene.GetVisibleProxies().size() >= LOD_STAGGER_MIN_VISIBLE;

	const bool bForceFullLODRefresh =
		!bShouldStaggerLOD
		|| LastLODUpdateCamera != InCamera
		|| !bHasLastFullLODUpdateCameraPos
		|| FVector::DistSquared(CameraPos, LastFullLODUpdateCameraPos) >= LOD_FULL_UPDATE_CAMERA_MOVE_SQ
		|| CameraForward.Dot(LastFullLODUpdateCameraForward) < LOD_FULL_UPDATE_CAMERA_ROTATION_DOT;

	if (bForceFullLODRefresh)
	{
		LastLODUpdateCamera = const_cast<UCameraComponent*>(InCamera);
		LastFullLODUpdateCameraPos = CameraPos;
		LastFullLODUpdateCameraForward = CameraForward;
		bHasLastFullLODUpdateCameraPos = true;
	}

	FLODUpdateContext Ctx;
	Ctx.CameraPos = CameraPos;
	Ctx.LODUpdateFrame = LODUpdateFrame;
	Ctx.LODUpdateSlice = LODUpdateSlice;
	Ctx.bForceFullRefresh = bForceFullLODRefresh;
	Ctx.bValid = true;
	return Ctx;
}

void UWorld::InitWorld()
{
	Partition.Reset(FBoundingBox());
	InvalidateVisibleSet();
	PersistentLevel = UObjectManager::Get().CreateObject<ULevel>(this);
	PersistentLevel->SetWorld(this);
}

void UWorld::BeginPlay()
{
	bHasBegunPlay = true;

	if (PersistentLevel)
	{
		PersistentLevel->BeginPlay();
	}
}

void UWorld::Tick(float DeltaTime, ELevelTick TickType)
{
	{
		SCOPE_STAT_CAT("FlushPrimitive", "1_WorldTick");
		Partition.FlushPrimitive();
	}

	UpdateVisibleProxies();

#if _DEBUG
	DebugDrawQueue.Tick(DeltaTime);
#endif

	TickManager.Tick(this, DeltaTime, TickType);
}

void UWorld::EndPlay()
{
	bHasBegunPlay = false;
	TickManager.Reset();

	if (!PersistentLevel)
	{
		return;
	}

	PersistentLevel->EndPlay();

	// Clear spatial partition while actors/components are still alive.
	// Otherwise Octree teardown can dereference stale primitive pointers during shutdown.
	Partition.Reset(FBoundingBox());

	PersistentLevel->Clear();
	MarkWorldPrimitivePickingBVHDirty();
}
