#pragma once

#include "Editor/Selection/PickingTypes.h"
#include "Collision/PickingTuning.h"
#include "Collision/RayUtils.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Component/PrimitiveComponent.h"
#include "Render/Pipeline/PrimitiveProxy.h"
#include "Render/Pipeline/IPrimitiveSpatialQuery.h"
#include "Render/Pipeline/WorldRenderProxy.h"
#include "Object/Object.h"

#include <cfloat>
#include <algorithm>

class UWorld;
class AActor;
class UPrimitiveComponent;

class FPickingService
{
public:
	static uint32 MakeObjectPickingId(const UObject* Object)
	{
		return Object ? Object->GetUUID() : 0u;
	}

	static UObject* ResolveObjectFromPickingId(uint32 PickingId)
	{
		if (PickingId == 0u)
		{
			return nullptr;
		}

		return UObjectManager::Get().FindByUUID(PickingId);
	}

	static UObject* ResolveObjectFromPickingIdInWorld(uint32 PickingId, UWorld* World)
	{
		if (PickingId == 0u || !World)
		{
			return nullptr;
		}

		// 같은 UUID를 가진 객체가 여러 월드에 존재할 수 있으므로 (PIE 시 월드 복제), 지정된 월드 내에서 먼저 찾기
		for (AActor* Actor : World->GetActors())
		{
			if (Actor && Actor->GetUUID() == PickingId)
			{
				return Actor;
			}
			
			// 컴포넌트 레벨에서도 확인이 필요한 경우 (Gizmo 등)
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (Comp && Comp->GetUUID() == PickingId)
				{
					return Comp;
				}
			}
		}

		// 월드 내에 없으면 전역 검색 (fallback)
		return ResolveObjectFromPickingId(PickingId);
	}

	static bool PickGizmo(UPrimitiveComponent* Gizmo, const FRay& Ray, EPickingMode Mode, FHitResult& OutHit, uint32* OutPickingId = nullptr)
	{
		if (!Gizmo)
		{
			if (OutPickingId) *OutPickingId = 0u;
			return false;
		}

		if (OutPickingId)
		{
			*OutPickingId = MakeObjectPickingId(Gizmo);
		}

		if (Mode == EPickingMode::IDBuffer)
		{
			if (OutPickingId) *OutPickingId = 0u;
			return false;
		}

		return FRayUtils::RaycastComponent(Gizmo, Ray, OutHit);
	}

	static AActor* PickActor(UWorld* World, const FRay& Ray, EPickingMode Mode, float& OutClosestDistance, uint32* OutPickingId = nullptr)
	{
		if (!World)
		{
			if (OutPickingId) *OutPickingId = 0u;
			return nullptr;
		}

		if (Mode == EPickingMode::IDBuffer)
		{
			OutClosestDistance = FLT_MAX;
			if (OutPickingId) *OutPickingId = 0u;
			return nullptr;
		}

		return PickActorByRay(World, Ray, OutClosestDistance, OutPickingId);
	}

private:
	struct FTemporalRayNearTHint
	{
		bool bValid = false;
		FRay Ray = {};
		float ClosestDistance = FLT_MAX;
		uint32 LastHitComponentId = 0u;
	};

	static AActor* PickActorByRay(UWorld* World, const FRay& Ray, float& OutClosestDistance, uint32* OutPickingId)
	{
		thread_local FTemporalRayNearTHint TemporalHint;
		ULevel* ActiveLevel = World->GetActiveLevel();
		if (ActiveLevel)
		{
			FWorldRenderProxy& RenderProxy = ActiveLevel->GetRenderProxy();
			// Exclude spatial index rebuild/warmup from algorithm timing window.
			if (RenderProxy.IsSpatialIndexDirtyForQueries())
			{
				RenderProxy.WarmupSpatialIndices();
			}
			RenderProxy.PrepareRayPickingCachesForQuery();
		}
		
		bool bHasComparableHint = false;
		float HintNearT = FLT_MAX;
		if (TemporalHint.bValid)
		{
			const float DirSimilarity = Ray.Direction.Dot(TemporalHint.Ray.Direction);
			const FVector OriginDelta = Ray.Origin - TemporalHint.Ray.Origin;
			const float OriginDeltaSq = OriginDelta.Dot(OriginDelta);
			if (DirSimilarity > 0.9995f && OriginDeltaSq < 1.0f)
			{
				bHasComparableHint = true;
				HintNearT = TemporalHint.ClosestDistance * 1.5f + 2.0f;
			}
		}

		AActor* BestActor = nullptr;
		OutClosestDistance = FLT_MAX;
		if (OutPickingId) *OutPickingId = 0u;
		FHitResult HitResult{};
		thread_local TArray<FRayQueryCandidate> RayCandidates;
		thread_local int32 RayCandidateReserveHint = 0;
		RayCandidates.clear();
		if (RayCandidateReserveHint > 0)
		{
			RayCandidates.reserve(static_cast<size_t>(RayCandidateReserveHint));
		}

		auto ExecuteBroadQuery = [&](float MaxNearT)
		{
			RayCandidates.clear();
			if (ActiveLevel)
			{
				ActiveLevel->GetRenderProxy().QueryByRayWithNearT(Ray, RayCandidates, MaxNearT);
				if (static_cast<int32>(RayCandidates.size()) > RayCandidateReserveHint)
				{
					RayCandidateReserveHint = static_cast<int32>(RayCandidates.size());
				}
			}
		};
		bool bUsedNearTHint = false;
		{
			if (bHasComparableHint && TemporalHint.LastHitComponentId != 0u)
			{
				if (UPrimitiveComponent* LastComp = Cast<UPrimitiveComponent>(ResolveObjectFromPickingId(TemporalHint.LastHitComponentId)))
				{
					if (AActor* LastActor = LastComp->GetOwner())
					{
						if (LastActor->GetRootComponent())
						{
							HitResult = {};
							if (LastComp->LineTraceComponent(Ray, HitResult, HintNearT) &&
								HitResult.Distance < OutClosestDistance)
							{
								OutClosestDistance = HitResult.Distance;
								BestActor = LastActor;
								if (OutPickingId) *OutPickingId = MakeObjectPickingId(BestActor);
								TemporalHint.Ray = Ray;
								TemporalHint.ClosestDistance = OutClosestDistance;
								TemporalHint.bValid = true;
								TemporalHint.LastHitComponentId = MakeObjectPickingId(LastComp);
								return BestActor;
							}
						}
					}
				}
			}
		}

		bool bFastProbeHit = false;
		{
			// FastProbe는 temporal hint가 유효할 때만 의미가 있다.
			// 힌트가 없으면 전역 closest query가 되어 오히려 병목이 될 수 있다.
			if (ActiveLevel && bHasComparableHint)
			{
				FRayQueryCandidate FastCandidate{};
				const float ProbeMaxNearT = HintNearT;
				bUsedNearTHint = true;

				if (ActiveLevel->GetRenderProxy().QueryClosestByRayWithNearT(Ray, FastCandidate, ProbeMaxNearT))
				{
					FPrimitiveProxy* ProbeProxy = FastCandidate.Proxy;
					if (ProbeProxy)
					{
						if (UPrimitiveComponent* ProbeComp = ProbeProxy->GetOwner())
						{
							if (AActor* ProbeActor = ProbeComp->GetOwner())
							{
								if (ProbeActor->GetRootComponent())
								{
									HitResult = {};
									if (ProbeComp->LineTraceComponent(Ray, HitResult, OutClosestDistance) && HitResult.Distance < OutClosestDistance)
									{
										OutClosestDistance = HitResult.Distance;
										BestActor = ProbeActor;
										bFastProbeHit = true;
									}
								}
							}
						}
					}
				}
			}
		}

		if (bFastProbeHit)
		{
			if (OutPickingId && BestActor)
			{
				*OutPickingId = MakeObjectPickingId(BestActor);
			}
			TemporalHint.Ray = Ray;
			TemporalHint.ClosestDistance = OutClosestDistance;
			TemporalHint.bValid = (BestActor != nullptr) && (OutClosestDistance < FLT_MAX);
			TemporalHint.LastHitComponentId = (HitResult.HitComponent != nullptr) ? MakeObjectPickingId(HitResult.HitComponent) : 0u;
			return BestActor;
		}

		if (bHasComparableHint)
		{
			ExecuteBroadQuery(HintNearT);
			bUsedNearTHint = true;
		}

		if (!bUsedNearTHint)
		{
			ExecuteBroadQuery(FLT_MAX);
		}

		auto ExecuteNarrowQuery = [&]()
		{
			const float NearTEpsilon = FPickingTuning::NearTEpsilon();
			const auto NearTMinHeapCompare = [](const FRayQueryCandidate& L, const FRayQueryCandidate& R)
			{
				return L.NearT > R.NearT;
			};

			if (RayCandidates.size() > 1)
			{
				std::make_heap(RayCandidates.begin(), RayCandidates.end(), NearTMinHeapCompare);
			}

			while (!RayCandidates.empty())
			{
				if (RayCandidates.size() > 1)
				{
					std::pop_heap(RayCandidates.begin(), RayCandidates.end(), NearTMinHeapCompare);
				}

				const FRayQueryCandidate Candidate = RayCandidates.back();
				RayCandidates.pop_back();

				if (Candidate.NearT >= OutClosestDistance - NearTEpsilon)
				{
					break;
				}

				FPrimitiveProxy* Proxy = Candidate.Proxy;
				if (!Proxy)
				{
					continue;
				}

				UPrimitiveComponent* PrimitiveComp = Proxy->GetOwner();
				if (!PrimitiveComp)
				{
					continue;
				}

				AActor* Actor = PrimitiveComp->GetOwner();
				if (!Actor || !Actor->GetRootComponent())
				{
					continue;
				}

				HitResult = {};
				const bool bTriangleHit = PrimitiveComp->LineTraceComponent(Ray, HitResult, OutClosestDistance);
				if (bTriangleHit && HitResult.Distance < OutClosestDistance)
				{
					OutClosestDistance = HitResult.Distance;
					BestActor = Actor;
				}
			}
		};

		ExecuteNarrowQuery();
		if (!BestActor && bUsedNearTHint)
		{
			ExecuteBroadQuery(FLT_MAX);
			ExecuteNarrowQuery();
		}

		if (OutPickingId && BestActor)
		{
			*OutPickingId = MakeObjectPickingId(BestActor);
		}

		TemporalHint.Ray = Ray;
		TemporalHint.ClosestDistance = OutClosestDistance;
		TemporalHint.bValid = (BestActor != nullptr) && (OutClosestDistance < FLT_MAX);
		TemporalHint.LastHitComponentId = (HitResult.HitComponent != nullptr) ? MakeObjectPickingId(HitResult.HitComponent) : 0u;

		return BestActor;
	}
};
