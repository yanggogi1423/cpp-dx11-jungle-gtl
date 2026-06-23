#include "Asset/SkeletalMeshTypes.h"

#include "Serialization/Archive.h"

void FSkeletalMesh::Serialize(FArchive& Ar, int32 PayloadVersion)
{
	(void)PayloadVersion;
	Ar << "PathFileName" << PathFileName;
	Ar << "Vertices" << Vertices;
	Ar << "Indices" << Indices;
	Ar << "Bones" << Bones;
	Ar << "Sockets" << Sockets;
	Ar << "Sections" << Sections;
	Ar << "MaterialSlots" << MaterialSlots;
	Ar << "LocalBounds" << LocalBounds;
}

FArchive& operator<<(FArchive& Ar, FSkeletalMeshVertex& Vertex)
{
	Ar << "Position" << Vertex.Position;
	Ar << "Color" << Vertex.Color;
	Ar << "Normal" << Vertex.Normal;
	Ar << "UVs" << Vertex.UVs;
	Ar << "Tangent" << Vertex.Tangent;

	for (int32 Index = 0; Index < 4; ++Index)
	{
		int32 BoneIndex = Vertex.BoneIndices[Index];
		Ar << "BoneIndex" << BoneIndex;
		if (Ar.IsLoading())
		{
			Vertex.BoneIndices[Index] = static_cast<uint8>(BoneIndex);
		}
	}

	for (int32 Index = 0; Index < 4; ++Index)
	{
		Ar << "BoneWeight" << Vertex.BoneWeights[Index];
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FBoneInfo& BoneInfo)
{
	Ar << "Name" << BoneInfo.Name;
	Ar << "ParentIndex" << BoneInfo.ParentIndex;
	Ar << "LocalBindTransform" << BoneInfo.LocalBindTransform;
	Ar << "GlobalBindTransform" << BoneInfo.GlobalBindTransform;
	Ar << "InverseBindPose" << BoneInfo.InverseBindPose;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FSkeletalMeshSocket& Socket)
{
	Ar << "Name" << Socket.Name;
	Ar << "BoneIndex" << Socket.BoneIndex;
	Ar << "RelativeLocation" << Socket.RelativeLocation;
	Ar << "RelativeRotation" << Socket.RelativeRotation;
	Ar << "RelativeScale" << Socket.RelativeScale;
	return Ar;
}
