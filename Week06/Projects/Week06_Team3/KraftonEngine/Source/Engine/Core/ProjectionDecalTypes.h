#pragma once

#include "EngineTypes.h"
#include "Core/CoreTypes.h"
#include "Math/Matrix.h"

class AActor;
class UStaticMeshComponent;
class UStaticMesh;

struct FProjectionDecalCandidate
{
	AActor* OwnerActor = nullptr;
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	UStaticMesh* StaticMesh = nullptr;
	FBoundingBox PrimitiveWorldAABB;
	FMatrix MeshToWorld;
	FMatrix WorldToMesh;
};

struct FProjectionDecalRenderableVertex
{
	FVector Position; // decal local
	FVector Normal;   // decal local
	FVector4 Color;
	FVector2 UV;
};

struct FProjectionDecalRenderableSection
{
	uint32 FirstIndex = 0;
	uint32 IndexCount = 0;
};

struct FProjectionDecalRenderableMesh
{
	TArray<FProjectionDecalRenderableVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FProjectionDecalRenderableSection> Sections;

	void Clear()
	{
		Vertices.clear();
		Indices.clear();
		Sections.clear();
	}

	bool IsEmpty() const
	{
		return Vertices.empty() || Indices.empty();
	}
};


