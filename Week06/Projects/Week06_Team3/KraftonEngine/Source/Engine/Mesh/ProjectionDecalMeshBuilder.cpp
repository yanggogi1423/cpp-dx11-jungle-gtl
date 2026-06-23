#include "Mesh/ProjectionDecalMeshBuilder.h"

#include "Components/ProjectionDecalComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"

#include <algorithm>
#include <cmath>

namespace
{
	struct FOrientedBox
	{
		FVector Center;
		FVector Axis[3];
		FVector Extent;
	};

	bool PassProjectionDecalTargetFilter(
		const UProjectionDecalComponent& ProjectionDecalComponent,
		const UStaticMeshComponent* StaticMeshComponent)
	{
		if (!StaticMeshComponent)
		{
			return false;
		}

		if (ProjectionDecalComponent.IsExcludeSameOwnerEnabled()
			&& StaticMeshComponent->GetOwner() == ProjectionDecalComponent.GetOwner())
		{
			return false;
		}

		// Engine-side ReceivesDecal flag is not exposed on UStaticMeshComponent yet.
		// Keep the option for compatibility, but do not reject meshes here.

		return true;
	}

	FOrientedBox MakeProjectionDecalWorldOBB(const UProjectionDecalComponent& ProjectionDecalComponent)
	{
		const FMatrix ProjectionDecalLocalToWorld = ProjectionDecalComponent.GetProjectionDecalLocalToWorldMatrix();

		FOrientedBox Box;
		Box.Center = ProjectionDecalLocalToWorld.TransformPositionWithW(FVector(0.0f, 0.0f, 0.0f));

		FVector AxisVectors[3] =
		{
			ProjectionDecalLocalToWorld.TransformVector(FVector(1.0f, 0.0f, 0.0f)),
			ProjectionDecalLocalToWorld.TransformVector(FVector(0.0f, 1.0f, 0.0f)),
			ProjectionDecalLocalToWorld.TransformVector(FVector(0.0f, 0.0f, 1.0f))
		};

		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const float AxisLength = AxisVectors[AxisIndex].Length();
			Box.Extent.Data[AxisIndex] = AxisLength * 0.5f;
			Box.Axis[AxisIndex] = AxisLength > 0.000001f
				? AxisVectors[AxisIndex] / AxisLength
				: FVector(
					AxisIndex == 0 ? 1.0f : 0.0f,
					AxisIndex == 1 ? 1.0f : 0.0f,
					AxisIndex == 2 ? 1.0f : 0.0f);
		}

		return Box;
	}

	FOrientedBox MakeWorldAABBAsOBB(const FBoundingBox& Bounds)
	{
		FOrientedBox Box;
		Box.Center = (Bounds.Min + Bounds.Max) * 0.5f;
		Box.Extent = (Bounds.Max - Bounds.Min) * 0.5f;
		Box.Axis[0] = FVector(1.0f, 0.0f, 0.0f);
		Box.Axis[1] = FVector(0.0f, 1.0f, 0.0f);
		Box.Axis[2] = FVector(0.0f, 0.0f, 1.0f);
		return Box;
	}

	bool IntersectsOBBvsOBB(const FOrientedBox& A, const FOrientedBox& B)
	{
		float R[3][3] = {};
		float AbsR[3][3] = {};

		for (int32 i = 0; i < 3; ++i)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				R[i][j] = A.Axis[i].Dot(B.Axis[j]);
				AbsR[i][j] = std::abs(R[i][j]) + 0.00001f;
			}
		}

		const FVector Delta = B.Center - A.Center;
		float T[3] =
		{
			Delta.Dot(A.Axis[0]),
			Delta.Dot(A.Axis[1]),
			Delta.Dot(A.Axis[2])
		};

		for (int32 i = 0; i < 3; ++i)
		{
			const float RA = A.Extent.Data[i];
			const float RB =
				B.Extent.X * AbsR[i][0] +
				B.Extent.Y * AbsR[i][1] +
				B.Extent.Z * AbsR[i][2];
			if (std::abs(T[i]) > RA + RB)
			{
				return false;
			}
		}

		for (int32 j = 0; j < 3; ++j)
		{
			const float RA =
				A.Extent.X * AbsR[0][j] +
				A.Extent.Y * AbsR[1][j] +
				A.Extent.Z * AbsR[2][j];
			const float RB = B.Extent.Data[j];
			const float ProjectedT = std::abs(T[0] * R[0][j] + T[1] * R[1][j] + T[2] * R[2][j]);
			if (ProjectedT > RA + RB)
			{
				return false;
			}
		}

		for (int32 i = 0; i < 3; ++i)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				const int32 I1 = (i + 1) % 3;
				const int32 I2 = (i + 2) % 3;
				const int32 J1 = (j + 1) % 3;
				const int32 J2 = (j + 2) % 3;

				const float RA =
					A.Extent.Data[I1] * AbsR[I2][j] +
					A.Extent.Data[I2] * AbsR[I1][j];
				const float RB =
					B.Extent.Data[J1] * AbsR[i][J2] +
					B.Extent.Data[J2] * AbsR[i][J1];
				const float ProjectedT = std::abs(T[I2] * R[I1][j] - T[I1] * R[I2][j]);

				if (ProjectedT > RA + RB)
				{
					return false;
				}
			}
		}

		return true;
	}
}

void FProjectionDecalMeshBuilder::BuildRenderableMesh(
	const UProjectionDecalComponent& ProjectionDecalComponent,
	const UWorld& World,
	FProjectionDecalRenderableMesh& OutMesh)
{
	OutMesh.Clear();

	const FBoundingBox ProjectionDecalWorldAABB = ProjectionDecalComponent.GetProjectionDecalWorldAABB();
	if (!ProjectionDecalWorldAABB.IsValid())
	{
		return;
	}

	const FOrientedBox ProjectionDecalOBB = MakeProjectionDecalWorldOBB(ProjectionDecalComponent);
	const FMatrix WorldToProjectionDecal = ProjectionDecalComponent.GetWorldToProjectionDecalMatrix();
	const float MaxProjectionDecalExtent = (std::max)(ProjectionDecalOBB.Extent.X, (std::max)(ProjectionDecalOBB.Extent.Y, ProjectionDecalOBB.Extent.Z));
	const float ProjectionDecalWorldPushBias = (std::max)(MaxProjectionDecalExtent * 0.002f, 0.0015f);

	FProjectionDecalRenderableSection Section;
	Section.FirstIndex = 0;

	for (AActor* Actor : World.GetActors())
	{
		if (!Actor || !Actor->IsVisible())
		{
			continue;
		}

		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Primitive);
			if (!StaticMeshComponent || !StaticMeshComponent->IsVisible())
			{
				continue;
			}

			if (!PassProjectionDecalTargetFilter(ProjectionDecalComponent, StaticMeshComponent))
			{
				continue;
			}

			const FBoundingBox PrimitiveWorldAABB = StaticMeshComponent->GetWorldBoundingBox();
			if (!PrimitiveWorldAABB.IsValid() || !ProjectionDecalWorldAABB.IsIntersected(PrimitiveWorldAABB))
			{
				continue;
			}

			const FOrientedBox PrimitiveBounds = MakeWorldAABBAsOBB(PrimitiveWorldAABB);
			if (!IntersectsOBBvsOBB(ProjectionDecalOBB, PrimitiveBounds))
			{
				continue;
			}

			UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
			FStaticMesh* MeshAsset = StaticMesh ? StaticMesh->GetStaticMeshAsset() : nullptr;
			if (!MeshAsset || MeshAsset->Vertices.empty() || MeshAsset->Indices.size() < 3)
			{
				continue;
			}

			const FMatrix MeshToWorld = StaticMeshComponent->GetWorldMatrix();
			const uint32 BaseVertexIndex = static_cast<uint32>(OutMesh.Vertices.size());

			OutMesh.Vertices.reserve(OutMesh.Vertices.size() + MeshAsset->Vertices.size());
			OutMesh.Indices.reserve(OutMesh.Indices.size() + MeshAsset->Indices.size());

			for (const FNormalVertex& SourceVertex : MeshAsset->Vertices)
			{
				FVector WorldNormal = MeshToWorld.TransformVector(SourceVertex.normal);
				WorldNormal.Normalize();
				const FVector BaseWorldPosition = MeshToWorld.TransformPositionWithW(SourceVertex.pos);
				const FVector WorldPosition = BaseWorldPosition + (WorldNormal * ProjectionDecalWorldPushBias);
				const FVector ProjectionDecalLocalPosition = WorldToProjectionDecal.TransformPositionWithW(WorldPosition);

				FVector ProjectionDecalLocalNormal = WorldToProjectionDecal.TransformVector(WorldNormal);
				ProjectionDecalLocalNormal.Normalize();

				FProjectionDecalRenderableVertex Vertex = {};
				Vertex.Position = ProjectionDecalLocalPosition;
				Vertex.Normal = ProjectionDecalLocalNormal;
				Vertex.Color = SourceVertex.color;
				Vertex.UV = SourceVertex.tex;
				OutMesh.Vertices.push_back(Vertex);
			}

			for (uint32 Index : MeshAsset->Indices)
			{
				OutMesh.Indices.push_back(BaseVertexIndex + Index);
			}
		}
	}

	Section.IndexCount = static_cast<uint32>(OutMesh.Indices.size());
	if (Section.IndexCount > 0)
	{
		OutMesh.Sections.push_back(Section);
	}
}

