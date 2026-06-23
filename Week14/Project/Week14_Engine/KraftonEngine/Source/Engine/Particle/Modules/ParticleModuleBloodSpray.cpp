#include "ParticleModuleBloodSpray.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "Component/Particle/ParticleSystemComponent.h"
#include "Math/MathUtils.h"
#include "Particle/ParticleEmitterInstance.h"

namespace
{
	float RandomFloat01()
	{
		return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
	}

	float RandomRange(float InA, float InB)
	{
		const float SafeMin = std::min(InA, InB);
		const float SafeMax = std::max(InA, InB);
		return SafeMin + (SafeMax - SafeMin) * RandomFloat01();
	}

	FVector SafeNormalOrForward(const FVector& InVector)
	{
		if (InVector.IsNearlyZero())
		{
			return FVector::ForwardVector;
		}
		return InVector.Normalized();
	}

	void BuildBasisFromAxis(const FVector& Axis, FVector& OutTangent, FVector& OutBitangent)
	{
		const FVector Helper = (std::fabs(Axis.Z) < 0.99f)
			? FVector::UpVector
			: FVector::RightVector;

		OutTangent = Helper.Cross(Axis).Normalized();
		if (OutTangent.IsNearlyZero())
		{
			OutTangent = FVector::RightVector;
		}

		OutBitangent = Axis.Cross(OutTangent).Normalized();
		if (OutBitangent.IsNearlyZero())
		{
			OutBitangent = FVector::UpVector;
		}
	}

	FVector SampleConeDirection(const FVector& Axis, float ConeHalfAngleDegrees)
	{
		const FVector SafeAxis = SafeNormalOrForward(Axis);
		const float SafeAngleDegrees = std::clamp(ConeHalfAngleDegrees, 0.0f, 180.0f);
		if (SafeAngleDegrees <= 0.001f)
		{
			return SafeAxis;
		}

		const float ConeRadians = SafeAngleDegrees * FMath::DegToRad;
		const float CosMax = std::cos(ConeRadians);
		const float CosTheta = CosMax + (1.0f - CosMax) * RandomFloat01();
		const float SinTheta = std::sqrt(std::max(0.0f, 1.0f - CosTheta * CosTheta));
		const float Phi = RandomFloat01() * FMath::Pi * 2.0f;

		FVector Tangent;
		FVector Bitangent;
		BuildBasisFromAxis(SafeAxis, Tangent, Bitangent);

		const FVector Direction =
			SafeAxis * CosTheta +
			Tangent * (std::cos(Phi) * SinTheta) +
			Bitangent * (std::sin(Phi) * SinTheta);

		return SafeNormalOrForward(Direction);
	}
}

void UParticleModuleBloodSpray::Spawn(
	FParticleEmitterInstance* Owner,
	uint32 ModuleOffset,
	float SpawnTime,
	FBaseParticle* Particle)
{
	(void)ModuleOffset;
	(void)SpawnTime;

	if (!Particle)
	{
		return;
	}

	const FVector ConeDirection = SampleConeDirection(ConeAxisLocal, ConeHalfAngleDegrees);
	const float Speed = RandomRange(MinSpeed, MaxSpeed);
	const FVector AuthoredVelocity = ConeDirection * Speed;

	const EParticleValueSpace SourceSpace = bInWorldSpace
		? EParticleValueSpace::World
		: EParticleValueSpace::Local;
	const FVector SimulationVelocity = Owner
		? Owner->ConvertVectorToSimulation(AuthoredVelocity, SourceSpace)
		: AuthoredVelocity;

	if (bAddToExistingVelocity)
	{
		Particle->Velocity += SimulationVelocity;
	}
	else
	{
		Particle->Velocity = SimulationVelocity;
	}
	Particle->BaseVelocity = Particle->Velocity;

	Particle->Rotation = RandomRange(InitialRotationMinDegrees, InitialRotationMaxDegrees) * FMath::DegToRad;
	Particle->BaseRotation = Particle->Rotation;
	Particle->RotationRate = RandomRange(RotationRateMinDegrees, RotationRateMaxDegrees) * FMath::DegToRad;
	Particle->BaseRotationRate = Particle->RotationRate;
}

void UParticleModuleBloodSpray::UpdateParticle(
	FParticleEmitterInstance* Owner,
	UParticleLODLevel* SimulationLOD,
	uint32 ModuleOffset,
	float DeltaTime,
	FBaseParticle* Particle)
{
	(void)Owner;
	(void)SimulationLOD;
	(void)ModuleOffset;

	if (!Particle || DeltaTime <= 0.0f)
	{
		return;
	}

	if (DragCoefficient > 0.0f)
	{
		const float Damping = std::exp(-DragCoefficient * DeltaTime);
		Particle->Velocity *= Damping;
	}

	Particle->Rotation += Particle->RotationRate * DeltaTime;
}
