#include "StaticMeshComponent.h"

#include <cmath>
#include <cstring>

#include "Collision/CollisionQueryUtils.h"
#include "Core/ResourceManager.h"

UStaticMeshComponent::UStaticMeshComponent()
{
	//	기본 도형은 Cube로 설정
	SetStaticMesh(FResourceManager::Get().LoadStaticMesh("Asset\\Mesh\\Dice\\Dice.obj"));
}

// 프로퍼티 시스템에 노출되지 않은 필드를 직접 복사합니다.
// StaticMeshAsset·OverrideMaterial 은 얕은 복사로 동일한 원본 리소스를 참조하게 합니다.
void UStaticMeshComponent::PostDuplicate(UObject* Original)
{
	UMeshComponent::PostDuplicate(Original);

	const UStaticMeshComponent* Orig = Cast<UStaticMeshComponent>(Original);
	StaticMeshAsset = Orig->StaticMeshAsset;
	bBoundsDirty = true;
	bRenderStateDirty = true;

	Materials = TArray<UMaterialInterface*>(Orig->Materials.size());
	for (int32 i = 0; i < static_cast<int32>(Orig->Materials.size()); ++i)
	{
		if (UMaterialInstance* OrigMatInst = Cast<UMaterialInstance>(Orig->Materials[i]))
		{
			UMaterialInstance* MatInst = UMaterialInstance::Create(OrigMatInst->Parent);
			MatInst->OverridedParams = OrigMatInst->OverridedParams;
			Materials[i] = MatInst;
		}
		else
		{
			Materials[i] = Orig->Materials[i]; // 얕은 복사 — ResourceManager 가 소유
		}
	}
}

void UStaticMeshComponent::Serialize(FArchive& Ar)
{
	UMeshComponent::Serialize(Ar);

	if (Ar.IsLoading())
	{
		const bool bHasMaterialOverrides = Ar.HasKey("Materials");
		TArray<UMaterialInterface*> LoadedMaterials = Materials;
		const FString RequestedPath = StaticMeshAssetPath.GetPath();

		if (!RequestedPath.empty())
		{
			SetStaticMesh(FResourceManager::Get().LoadStaticMesh(RequestedPath));
		}
		else
		{
			SetStaticMesh(nullptr);
		}

		if (bHasMaterialOverrides)
		{
			Materials.clear();
			for (int32 i = 0; i < static_cast<int32>(LoadedMaterials.size()); ++i)
			{
				SetMaterial(i, LoadedMaterials[i]);
			}
			MarkRenderStateDirty();
		}
	}
}

void UStaticMeshComponent::SetStaticMesh(UStaticMesh* InStaticMesh)
{
	if (StaticMeshAsset == InStaticMesh)
	{
		return;
	}

	StaticMeshAsset = InStaticMesh;
	Materials.clear();

	if (StaticMeshAsset != nullptr)
	{
		StaticMeshAssetPath.SetPath(StaticMeshAsset->GetAssetPathFileName());

		const auto& Slots = StaticMeshAsset->GetMaterialSlots();
		const auto& Sections = StaticMeshAsset->GetSections();
		Materials.reserve(Sections.size());

		for (int32 i = 0; i < static_cast<int32>(Sections.size()); ++i)
		{
			Materials.push_back(Slots[Sections[i].MaterialSlotIndex].Material);
		}
	}
	else
	{
		StaticMeshAssetPath.SetPath("");
	}

	MarkBoundsDirty();
	MarkRenderStateDirty();
}

UStaticMesh* UStaticMeshComponent::GetStaticMesh() const
{
	return StaticMeshAsset;
}

bool UStaticMeshComponent::HasValidMesh() const
{
	return StaticMeshAsset != nullptr && StaticMeshAsset->HasValidMeshData();
}


void UStaticMeshComponent::PostEditProperty(const char* PropertyName)
{
	UMeshComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "StaticMeshAssetPath") == 0)
	{
		const FString RequestedPath = StaticMeshAssetPath.GetPath();
		if (RequestedPath.empty())
		{
			SetStaticMesh(nullptr);
			return;
		}

		UStaticMesh* Mesh = FResourceManager::Get().LoadStaticMesh(RequestedPath);

		SetStaticMesh(Mesh);
	}
	else if (std::strcmp(PropertyName, "Materials") == 0)
	{
		for (int32 i = 0; i < static_cast<int32>(Materials.size()); ++i)
		{
			SetMaterial(i, Materials[i]);
		}
		MarkRenderStateDirty();
	}
}

void UStaticMeshComponent::UpdateWorldAABB() const
{
	WorldAABB.Reset();

	if (!HasValidMesh())
	{
		bBoundsDirty = false;
		return;
	}

	const FAABB& LocalBounds = StaticMeshAsset->GetLocalBounds();
	if (!LocalBounds.IsValid())
	{
		bBoundsDirty = false;
		return;
	}

	const FVector LocalCorners[8] = {
		FVector(LocalBounds.Min.X, LocalBounds.Min.Y, LocalBounds.Min.Z),
		FVector(LocalBounds.Max.X, LocalBounds.Min.Y, LocalBounds.Min.Z),
		FVector(LocalBounds.Min.X, LocalBounds.Max.Y, LocalBounds.Min.Z),
		FVector(LocalBounds.Max.X, LocalBounds.Max.Y, LocalBounds.Min.Z),
		FVector(LocalBounds.Min.X, LocalBounds.Min.Y, LocalBounds.Max.Z),
		FVector(LocalBounds.Max.X, LocalBounds.Min.Y, LocalBounds.Max.Z),
		FVector(LocalBounds.Min.X, LocalBounds.Max.Y, LocalBounds.Max.Z),
		FVector(LocalBounds.Max.X, LocalBounds.Max.Y, LocalBounds.Max.Z)
	};

	const FMatrix& WorldMatrix = GetWorldMatrix();

	for (const FVector& Corner : LocalCorners)
	{
		const FVector WorldPos = WorldMatrix.TransformPosition(Corner);
		WorldAABB.Expand(WorldPos);
	}

	bBoundsDirty = false;
}

//	Ray를 Local로 바꿔서 확인
//	모든 Mesh를 World로 바꾸는 것보다 훨씬 빠름
bool UStaticMeshComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
	if (!HasValidMesh())
	{
		return false;
	}

	EnsureBoundsUpdated();

	float BoxT = 0.0f;
	if (!WorldAABB.IntersectRay(Ray, BoxT))
	{
		return false;
	}

	const TArray<FNormalVertex>& Vertices = StaticMeshAsset->GetVertices();
	const TArray<uint32>& Indices = StaticMeshAsset->GetIndices();

	if (Vertices.empty() || Indices.empty())
	{
		return false;
	}

	const FMatrix InvWorld = GetWorldMatrix().GetInverse();

	FRay LocalRay = Ray;
	LocalRay.Origin = InvWorld.TransformPosition(LocalRay.Origin);
	LocalRay.Direction = InvWorld.TransformVector(LocalRay.Direction);
	LocalRay.Direction.NormalizeSafe();

	bool bHit = false;
	float ClosestT = FLT_MAX;
	int32 BestFaceIndex = -1;
	FVector BestLocalNormal = FVector::ZeroVector;

	for (uint32 i = 0; i + 2 < static_cast<uint32>(Indices.size()); i += 3)
	{
		const uint32 I0 = Indices[i];
		const uint32 I1 = Indices[i + 1];
		const uint32 I2 = Indices[i + 2];

		if (I0 >= Vertices.size() || I1 >= Vertices.size() || I2 >= Vertices.size())
		{
			continue;
		}

		const FVector& V0 = Vertices[I0].Position;
		const FVector& V1 = Vertices[I1].Position;
		const FVector& V2 = Vertices[I2].Position;

		float HitT = 0.0f;
		if (IntersectTriangle(LocalRay.Origin, LocalRay.Direction, V0, V1, V2, HitT))
		{
			if (HitT < ClosestT)
			{
				ClosestT = HitT;
				bHit = true;
				BestFaceIndex = static_cast<int32>(i / 3);

				const FVector Edge1 = V1 - V0;
				const FVector Edge2 = V2 - V0;
				BestLocalNormal = FVector::CrossProduct(Edge1, Edge2).GetSafeNormal();
			}
		}
	}

	if (!bHit)
	{
		return false;
	}

	const FVector LocalHitLocation = LocalRay.Origin + LocalRay.Direction * ClosestT;
	const FVector WorldHitLocation = GetWorldMatrix().TransformPosition(LocalHitLocation);
	FVector WorldNormal = GetWorldMatrix().TransformVector(BestLocalNormal);
	WorldNormal.NormalizeSafe();

	OutHitResult.bHit = true;
	OutHitResult.HitComponent = this;
	OutHitResult.Distance = (WorldHitLocation - Ray.Origin).Size();
	OutHitResult.Location = WorldHitLocation;
	OutHitResult.Normal = WorldNormal;
	OutHitResult.FaceIndex = BestFaceIndex;

	return true;
}

bool UStaticMeshComponent::SweepMesh(const FVector& Start, const FVector& End, const FQuat& ShapeWorldRotation,
	const FCollisionShape& Shape, FHitResult& OutHitResult)
{
	(void)ShapeWorldRotation;

	if (!HasValidMesh() || !Shape.IsSphere() || Shape.Radius < 0.0f)
	{
		return false;
	}

	EnsureBoundsUpdated();

	const FVector Delta = End - Start;
	const float SegmentLength = Delta.Size();
	if (SegmentLength <= 1.0e-6f)
	{
		return false;
	}

	const FVector Direction = Delta / SegmentLength;
	const TArray<FNormalVertex>& Vertices = StaticMeshAsset->GetVertices();
	const TArray<uint32>& Indices = StaticMeshAsset->GetIndices();
	if (Vertices.empty() || Indices.empty())
	{
		return false;
	}

	const FMatrix& WorldMatrix = GetWorldMatrix();
	bool bHit = false;
	FHitResult BestHit;

	auto SubmitHit = [&](float Distance, const FVector& Center, const FVector& ImpactPoint,
		const FVector& Normal, int32 FaceIndex)
	{
		FVector HitNormal = Normal.GetSafeNormal();
		if (HitNormal.IsNearlyZero())
		{
			return;
		}

		FHitResult Candidate;
		Candidate.bHit = true;
		Candidate.HitComponent = this;
		Candidate.Distance = Distance;
		Candidate.Location = Center;
		Candidate.ImpactPoint = ImpactPoint;
		Candidate.Normal = HitNormal;
		Candidate.FaceIndex = FaceIndex;

		bool bBetterHit = false;
		if (bHit
			&& std::fabs(Candidate.Distance) <= 1.0e-4f
			&& std::fabs(BestHit.Distance) <= 1.0e-4f)
		{
			const float CandidatePushOut = (Candidate.Location - Start).SizeSquared();
			const float BestPushOut = (BestHit.Location - Start).SizeSquared();
			bBetterHit = CandidatePushOut < BestPushOut - 1.0e-4f;
		}
		else
		{
			bBetterHit = FCollisionQueryUtils::IsBetterSweepHit(Candidate, BestHit, bHit, Direction);
		}

		if (bBetterHit)
		{
			BestHit = Candidate;
			bHit = true;
		}
	};

	for (uint32 i = 0; i + 2 < static_cast<uint32>(Indices.size()); i += 3)
	{
		const uint32 I0 = Indices[i];
		const uint32 I1 = Indices[i + 1];
		const uint32 I2 = Indices[i + 2];
		if (I0 >= Vertices.size() || I1 >= Vertices.size() || I2 >= Vertices.size())
		{
			continue;
		}

		const FVector V0 = WorldMatrix.TransformPosition(Vertices[I0].Position);
		const FVector V1 = WorldMatrix.TransformPosition(Vertices[I1].Position);
		const FVector V2 = WorldMatrix.TransformPosition(Vertices[I2].Position);
		const FVector TriangleNormal = FVector::CrossProduct(V1 - V0, V2 - V0).GetSafeNormal();
		if (TriangleNormal.IsNearlyZero())
		{
			continue;
		}

		const FVector ClosestStartPoint = FCollisionQueryUtils::ClosestPointOnTriangle(Start, V0, V1, V2);
		const FVector StartToTriangle = Start - ClosestStartPoint;
		const float InitialOverlapRadius = std::max(0.0f, Shape.Radius - 1.0e-4f);
		if (StartToTriangle.SizeSquared() < InitialOverlapRadius * InitialOverlapRadius)
		{
			FVector HitNormal = StartToTriangle.GetSafeNormal();
			if (HitNormal.IsNearlyZero())
			{
				HitNormal = FCollisionQueryUtils::ChooseNormalOpposingDirection(TriangleNormal, Direction);
			}

			SubmitHit(
				0.0f,
				ClosestStartPoint + HitNormal * Shape.Radius,
				ClosestStartPoint,
				HitNormal,
				static_cast<int32>(i / 3));
			continue;
		}

		FVector SweepNormal = TriangleNormal;
		float SignedDistance = FVector::DotProduct(Start - V0, SweepNormal);
		if (SignedDistance < 0.0f)
		{
			SweepNormal = -SweepNormal;
			SignedDistance = -SignedDistance;
		}

		const float ApproachSpeed = FVector::DotProduct(Direction, SweepNormal);
		if (ApproachSpeed < -1.0e-6f)
		{
			const float HitDistance = (Shape.Radius - SignedDistance) / ApproachSpeed;
			if (HitDistance >= 0.0f && HitDistance <= SegmentLength)
			{
				const FVector HitCenter = Start + Direction * HitDistance;
				const FVector ImpactPoint = HitCenter - SweepNormal * Shape.Radius;
				if (FCollisionQueryUtils::IsPointInTriangle(ImpactPoint, V0, V1, V2))
				{
					SubmitHit(HitDistance, HitCenter, ImpactPoint, SweepNormal, static_cast<int32>(i / 3));
				}
			}
		}

		const FVector EdgeStarts[3] = { V0, V1, V2 };
		const FVector EdgeEnds[3] = { V1, V2, V0 };
		for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
		{
			float EdgeDistance = 0.0f;
			FVector EdgeNormal = FVector::ZeroVector;
			FVector EdgeClosestPoint = FVector::ZeroVector;
			if (FCollisionQueryUtils::RayCapsuleEdgeSweep(
				Start,
				Direction,
				SegmentLength,
				EdgeStarts[EdgeIndex],
				EdgeEnds[EdgeIndex],
				Shape.Radius,
				EdgeDistance,
				EdgeNormal,
				EdgeClosestPoint))
			{
				const FVector HitCenter = EdgeDistance <= 1.0e-4f
					? EdgeClosestPoint + EdgeNormal.GetSafeNormal() * Shape.Radius
					: Start + Direction * EdgeDistance;
				SubmitHit(
					EdgeDistance,
					HitCenter,
					EdgeClosestPoint,
					EdgeNormal,
					static_cast<int32>(i / 3));
			}
		}

		const FVector VerticesToTest[3] = { V0, V1, V2 };
		for (const FVector& Vertex : VerticesToTest)
		{
			float VertexDistance = 0.0f;
			FVector VertexNormal = FVector::ZeroVector;
			if (FCollisionQueryUtils::RaySphereSweep(
				Start, Direction, SegmentLength, Vertex, Shape.Radius, VertexDistance, VertexNormal))
			{
				const FVector HitCenter = VertexDistance <= 1.0e-4f
					? Vertex + VertexNormal.GetSafeNormal() * Shape.Radius
					: Start + Direction * VertexDistance;
				SubmitHit(
					VertexDistance,
					HitCenter,
					Vertex,
					VertexNormal,
					static_cast<int32>(i / 3));
			}
		}
	}

	if (!bHit)
	{
		return false;
	}

	OutHitResult = BestHit;
	return true;
}

const FAABB& UStaticMeshComponent::GetWorldAABB() const
{
	EnsureBoundsUpdated();
	return WorldAABB;
}

bool UStaticMeshComponent::ConsumeRenderStateDirty()
{
	const bool bWasDirty = bRenderStateDirty;
	bRenderStateDirty = false;
	return bWasDirty;
}

void UStaticMeshComponent::GetMeshData(TArray<FNormalVertex>& OutVertices, TArray<uint32>& OutIndices) const
{
	for (const FNormalVertex& Vertex : StaticMeshAsset->GetVertices())
	{
		OutVertices.push_back(Vertex);
	}

	for (uint32 Index : StaticMeshAsset->GetIndices())
	{
		OutIndices.push_back(Index);
	}
}

void UStaticMeshComponent::MarkBoundsDirty()
{
	bBoundsDirty = true;
}

void UStaticMeshComponent::MarkRenderStateDirty()
{
	bRenderStateDirty = true;
}

void UStaticMeshComponent::EnsureBoundsUpdated() const
{
	if (!bBoundsDirty && !bTransformDirty)
	{
		return;
	}

	if (bTransformDirty)
	{
		(void)GetWorldMatrix();
		return;
	}

	const_cast<UStaticMeshComponent*>(this)->UpdateWorldAABB();
}
