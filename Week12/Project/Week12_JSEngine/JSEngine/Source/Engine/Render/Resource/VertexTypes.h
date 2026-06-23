#pragma once

#include "Core/CoreMinimal.h"

/*
	Vertex 구조체들을 정의하는 Header입니다.
	추후에 다양한 Vertex 구조체들을 추가할 수 있습니다.
*/

struct FVertex
{ 
	FVector Position;
	FColor	Color;
	int		SubID;
};

struct FNormalVertex
{
	FVector		Position;
	FColor		Color;
	FVector		Normal;
	FVector2	UVs;	//	TexCoord
    FVector4	Tangent;
};

struct FSkeletalMeshVertex
{
    FVector Position;
    FColor Color;
    FVector Normal;
    FVector2 UVs;
    FVector4 Tangent;

    // Bone influence (핵심)
    // TODO: invalid bone 표현이나 256개 이상의 bone 표현을 위해 int32로 변경을 고려
    uint8 BoneIndices[4];
    float BoneWeights[4];
};

struct FOverlayVertex
{
	float X, Y;
};

// Position + TexCoord 범용 버텍스 (FontBatcher, SubUVBatcher 등 텍스처 기반 배처 공용)
struct FTextureVertex
{
	FVector  Position;
	FVector2 TexCoord;
	FColor Color;
};

struct FMeshData
{
	TArray<FVertex> Vertices;
	TArray<uint32> Indices;
};

// Sprite Particle Quad의 Per-Vertex입니다. Slot 0 stream.
struct FSpriteParticleVertex
{
	FVector  Position;   // 12
	FVector2 TexCoord;   //  8
};                       // 20 bytes

// Sprite Particle의 Per-Instance입니다. Slot 1 stream.
// 한 emitter의 모든 활성 파티클을 이 struct 배열로 채워 FInstanceBuffer에 올립니다.
struct FSpriteParticleInstanceData
{
	FVector  Position;     // 12 (offset 0)
	FVector2 Size;         //  8 (offset 12)
	FColor   Color;        // 16 (offset 20) — RGBA float
	float    Rotation;     //  4 (offset 36) — radians
	uint32   SubUVIndex;   //  4 (offset 40)
};                         // 44 bytes
static_assert(sizeof(FSpriteParticleInstanceData) == 44,
	"SpriteParticleLayout slot 1 offsets depend on this struct being tightly packed at 44 bytes");

// Mesh Particle의 Per-Instance입니다. Slot 1 stream (Cycle 11, 옵션 B).
// per-vertex는 Slot 0의 FNormalVertex (mesh asset 그대로 사용).
// InstanceRotation은 FVector Euler 3축 (옵션 B) — VS에서 RotationZYX 행렬로 합성.
struct FMeshParticleInstanceData
{
	FVector  InstancePosition;   // 12 (offset 0)
	FVector  InstanceRotation;   // 12 (offset 12) — Euler radians (옵션 B 3축)
	FVector  InstanceScale;      // 12 (offset 24)
	uint32   _Padding0;          //  4 (offset 36)
	FColor   InstanceColor;      // 16 (offset 40) — RGBA float
};                               // 56 bytes
static_assert(sizeof(FMeshParticleInstanceData) == 56,
	"MeshParticleLayout slot 1 offsets depend on this struct being tightly packed at 56 bytes");