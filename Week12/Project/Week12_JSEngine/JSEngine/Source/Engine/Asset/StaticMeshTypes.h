#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Geometry/AABB.h"
#include "Render/Resource/VertexTypes.h"
#include "Render/Resource/Material.h"

struct FArchive;

struct FStaticMeshSection
{
	uint32 StartIndex = 0;
	uint32 IndexCount = 0;
	int32 MaterialSlotIndex = -1;
};

struct FStaticMeshMaterialSlot
{
	FString SlotName;
	UMaterialInterface* Material = nullptr;
};

struct FStaticMeshRenderData
{
	ID3D11Buffer* VertexBuffer = nullptr;
	ID3D11Buffer* IndexBuffer = nullptr;
};

// Cooked
struct FStaticMesh
{
	FString PathFileName;
	TArray<FNormalVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FStaticMeshSection> Sections;
	TArray<FStaticMeshMaterialSlot> Slots;
	FAABB LocalBounds;

	FStaticMeshRenderData RenderData;

	void Serialize(FArchive& Ar, int32 PayloadVersion);
};

FArchive& operator<<(FArchive& Ar, FStaticMeshSection& Section);
FArchive& operator<<(FArchive& Ar, FStaticMeshMaterialSlot& Slot);
FArchive& operator<<(FArchive& Ar, FNormalVertex& Vertex);
FArchive& operator<<(FArchive& Ar, FAABB& AABB);
