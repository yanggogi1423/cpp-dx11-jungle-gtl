#include "Asset/StaticMeshTypes.h"

#include "Serialization/Archive.h"

void FStaticMesh::Serialize(FArchive& Ar, int32 PayloadVersion)
{
	(void)PayloadVersion;
	Ar << "PathFileName" << PathFileName;
	Ar << "Vertices" << Vertices;
	Ar << "Indices" << Indices;
	Ar << "Sections" << Sections;
	Ar << "Slots" << Slots;
	Ar << "LocalBounds" << LocalBounds;

	if (Ar.IsLoading())
	{
		RenderData = {};
	}
}

FArchive& operator<<(FArchive& Ar, FStaticMeshSection& Section)
{
	Ar << "StartIndex" << Section.StartIndex;
	Ar << "IndexCount" << Section.IndexCount;
	Ar << "MaterialSlotIndex" << Section.MaterialSlotIndex;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FStaticMeshMaterialSlot& Slot)
{
	Ar << "SlotName" << Slot.SlotName;
	if (Ar.IsLoading())
	{
		Slot.Material = nullptr;
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FNormalVertex& Vertex)
{
	Ar << "Position" << Vertex.Position;
	Ar << "Color" << Vertex.Color;
	Ar << "Normal" << Vertex.Normal;
	Ar << "UVs" << Vertex.UVs;
	Ar << "Tangent" << Vertex.Tangent;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAABB& AABB)
{
	Ar << "Min" << AABB.Min;
	Ar << "Max" << AABB.Max;
	return Ar;
}
