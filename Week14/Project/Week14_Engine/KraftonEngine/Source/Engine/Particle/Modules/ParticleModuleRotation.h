#pragma once

#include "Particle/ParticleModule.h"
#include "Engine/Particle/Distributions/DistributionFloat.h"
#include "Engine/Particle/Distributions/DistributionFloatConstant.h"
#include "Math/MathUtils.h"

#include "Source/Engine/Particle/Modules/ParticleModuleRotation.generated.h"

// =============================================================================
// UParticleModuleRotation
//   Initial sprite/mesh rotation module. The authored distribution is in degrees
//   for editor usability; runtime particle storage keeps Rotation in radians.
// =============================================================================
UCLASS()
class UParticleModuleRotation : public UParticleModule
{
public:
	GENERATED_BODY()

	UParticleModuleRotation()
	{
		auto* DefaultRotation = UObjectManager::Get().CreateObject<UDistributionFloatConstant>(this);
		if (DefaultRotation)
		{
			DefaultRotation->Constant = 0.0f;
			StartRotationDistribution = DefaultRotation;
		}
	}

	EModuleCategory GetCategory() const override { return EModuleCategory::Rotation; }
	const char*     GetDisplayName() const override { return "Initial Rotation"; }

	void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	           float SpawnTime, FBaseParticle* Particle) override
	{
		(void)Owner;
		(void)ModuleOffset;

		if (!Particle)
		{
			return;
		}

		const float RotationDegrees = StartRotationDistribution
			? StartRotationDistribution->GetValue(SpawnTime, nullptr)
			: 0.0f;
		const float RotationRadians = RotationDegrees * FMath::DegToRad;

		Particle->Rotation = RotationRadians;
		Particle->BaseRotation = RotationRadians;
	}

	// Evaluated with SpawnTime: emitter-loop seconds at which the particle is spawned.
	UPROPERTY(Edit, Save, Instanced, Category="Rotation", DisplayName="Start Rotation Distribution", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* StartRotationDistribution = nullptr;
};
