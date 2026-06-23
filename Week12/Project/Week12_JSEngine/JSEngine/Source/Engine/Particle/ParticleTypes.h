#pragma once

#include "Core/CoreMinimal.h"
#include "Core/CollisionTypes.h"

class AActor;
class UParticleSystemComponent;
class UPrimitiveComponent;
struct FParticleEmitterInstance;

UENUM()
enum class EParticleEmitterRenderMode : uint8
{
	Sprite,
	Mesh,
	Beam,
	Ribbon,
};


enum class EParticleSortMode : uint8
{
    None,
    ViewDepth,
    DistanceToView
};

struct FBaseParticle
{
	FVector Location = FVector::ZeroVector;
	FVector OldLocation = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector BaseVelocity = FVector::ZeroVector;
	float RelativeTime = 0.0f;
	float Lifetime = 1.0f;
	FVector Size = FVector(1.0f, 1.0f, 1.0f);
	FColor Color = FColor::White();
	float Rotation = 0.0f;
	float RotationRate = 0.0f;
	uint32 ParticleId = 0;
	uint32 Flags = 0;
	int32 CollisionCount = 0;
	uint32 SubUVIndex = 0;
};

struct FParticleDataContainer
{
	int32 MemBlockSize = 0;
	int32 ParticleDataNumBytes = 0;
	int32 ParticleIndicesNumShorts = 0;
	uint8* ParticleData = nullptr;
	uint16* ParticleIndices = nullptr;

	// Cycle 10d: stride source-of-truth가 instance에서 container로 이전.
	// Allocate(MaxParticles, ParticleStride, Alignment)이 align 후 본 멤버에 저장.
	// 외부는 GetStride()만 사용. instance의 ParticleStride 멤버는 본 cycle에 삭제됨.
	int32 ParticleStride = 0;

	static constexpr int32 DefaultParticleAlignment = 16;

	// Align particle stride so each particle starts at a predictable boundary.
	static int32 AlignSize(int32 Size, int32 Alignment)
	{
		if (Alignment <= 0)
		{
			return Size;
		}
		return ((Size + Alignment - 1) / Alignment) * Alignment;
	}

	bool Allocate(int32 MaxParticles, int32 ParticleStride, int32 Alignment = DefaultParticleAlignment)
	{
		Reset();
		if (MaxParticles <= 0 || ParticleStride <= 0)
		{
			return false;
		}

		const int32 AlignedParticleStride = AlignSize(ParticleStride, Alignment);
		this->ParticleStride = AlignedParticleStride;  // Cycle 10d: source-of-truth 저장 (인자 ParticleStride와 별개의 멤버)
		ParticleDataNumBytes = AlignedParticleStride * MaxParticles;
		ParticleIndicesNumShorts = MaxParticles;
		const int32 ParticleIndicesNumBytes = static_cast<int32>(sizeof(uint16)) * ParticleIndicesNumShorts;
		MemBlockSize = ParticleDataNumBytes + ParticleIndicesNumBytes;

		ParticleData = new uint8[MemBlockSize];
		ParticleIndices = reinterpret_cast<uint16*>(ParticleData + ParticleDataNumBytes);
		return ParticleData != nullptr && ParticleIndices != nullptr;
	}

	int32 GetMemoryBytes() const
	{
		return MemBlockSize;
	}

	// Cycle 10d: stride accessor. instance가 ParticleStorage.GetStride()로 read.
	// Allocate가 align 적용 후 저장한 값 — Allocate 호출 전이면 0.
	int32 GetStride() const
	{
		return ParticleStride;
	}

	// Function : Release owned particle data memory and clear container metadata
	// input : None
	// output : ParticleData is released and size/index fields are reset
	void Reset()
	{
		delete[] ParticleData;
		MemBlockSize = 0;
		ParticleDataNumBytes = 0;
		ParticleIndicesNumShorts = 0;
		ParticleData = nullptr;
		ParticleIndices = nullptr;
		ParticleStride = 0;  // Cycle 10d: source-of-truth 일관성 — Reset 후 GetStride() == 0
	}
};

struct FParticleEventCollideData
{
	UParticleSystemComponent* Component = nullptr;
	FParticleEmitterInstance* EmitterInstance = nullptr;
	int32 EmitterIndex = -1;
	uint32 ParticleId = 0;
	FVector Location = FVector::ZeroVector;
	FVector OldLocation = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	UPrimitiveComponent* HitComponent = nullptr;
	AActor* HitActor = nullptr;
	float Time = 0.0f;
	FHitResult Hit;
};

// Cycle 15a Phase 5 (D11): FParticleEmitterRuntimeView 삭제됨 (사용처 0건, FDynamicEmitterReplayDataBase 가 대체).
