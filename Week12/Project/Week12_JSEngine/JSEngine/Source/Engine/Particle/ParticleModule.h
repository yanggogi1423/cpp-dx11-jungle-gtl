#pragma once

#include "Asset/CurveFloatAsset.h"
#include "Core/Containers/Map.h"
#include "Object/Object.h"
#include "Particle/ParticleTypes.h"

struct FParticleEmitterInstance;

enum class EParticleDistributionRuntimeKind : int32
{
	FloatConstant = 0,
	FloatConstantCurve = 1,
	FloatUniform = 2,
	FloatUniformCurve = 3,
};

struct FParticleDistributionRuntimeData
{
	int32 Kind = 0;
	bool bVector = false;
	float StoredMaxFloat = 0.0f;
	FVector StoredMaxVector = FVector::ZeroVector;
	TMap<FString, FFloatCurve> Curves;
};

UCLASS()
class UParticleModule : public UObject
{
public:
	GENERATED_BODY(UParticleModule, UObject)

	// Function : Apply module behavior when a particle is spawned
	// input : Owner, Particle, SpawnTime
	// Owner : emitter instance that owns the particle
	// Particle : particle being initialized
	// SpawnTime : relative spawn time within this tick
	// output : Default implementation has no effect
	virtual void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) {}

	// Function : Apply module behavior during emitter update
	// input : Owner, DeltaTime
	// Owner : emitter instance that owns active particles
	// DeltaTime : elapsed time for this simulation step
	// output : Default implementation has no effect
	virtual void Update(FParticleEmitterInstance* Owner, float DeltaTime) {}

	bool IsEnabled() const { return bEnabled; }
	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }
	bool IsSpawnModule() const { return bSpawnModule; }
	bool IsUpdateModule() const { return bUpdateModule; }

	void PostDuplicate(UObject* Original) override;
	void Serialize(FArchive& Ar) override;

	void SetDistributionRuntimeData(const FString& PropertyName, const FParticleDistributionRuntimeData& Data);
	const FParticleDistributionRuntimeData* FindDistributionRuntimeData(const FString& PropertyName) const;
	float EvaluateFloatDistribution(const char* PropertyName, float ConstantValue, float UniformMaxValue, float Time) const;
	FVector EvaluateVectorDistribution(const char* PropertyName, const FVector& ConstantValue, const FVector& UniformMaxValue, float Time) const;

protected:
	UPROPERTY(DisplayName = "Enabled")
	bool bEnabled = true;

	UPROPERTY(DisplayName = "Spawn Module")
	bool bSpawnModule = false;

	UPROPERTY(DisplayName = "Update Module")
	bool bUpdateModule = false;

private:
	TMap<FString, FParticleDistributionRuntimeData> DistributionRuntimeData;
};
