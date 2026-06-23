#pragma once

#include "Particle/ParticleModule.h"
#include "Math/Vector.h"

#include "Source/Engine/Particle/Modules/ParticleModuleBloodSpray.generated.h"

// =============================================================================
// UParticleModuleBloodSpray
//   피격 이펙트처럼 한 방향으로 튀는 sprite 입자에 필요한 최소 기능을 한곳에
//   묶은 모듈이다.
//   - Velocity Cone: ConeAxis 기준 원뿔 안에서 초기 속도를 샘플링한다.
//   - Drag: 짧은 수명 동안 속도를 감쇠시켜 핏방울이 빠르게 죽는 느낌을 만든다.
//   - Initial Rotation / Rotation Rate: 얼룩 sprite 반복감을 줄인다.
//
//   BloodHit 파티클은 보통 SpawnEmitterAtLocation(HitLocation, HitRotation)으로
//   스폰하고, ConeAxisLocal=(1,0,0)을 유지한 채 파티클 액터의 +X가 피가 튈
//   방향을 보게 하는 식으로 사용한다.
// =============================================================================
UCLASS()
class UParticleModuleBloodSpray : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleBloodSpray() = default;

	EModuleCategory GetCategory() const override { return EModuleCategory::Velocity; }
	const char*     GetDisplayName() const override { return "Blood Spray"; }

	void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	           float SpawnTime, FBaseParticle* Particle) override;
	void UpdateParticle(FParticleEmitterInstance* Owner, UParticleLODLevel* SimulationLOD,
	                    uint32 ModuleOffset, float DeltaTime, FBaseParticle* Particle) override;

	// ConeAxisLocal은 기본적으로 emitter local +X다. HitNormal 방향으로 파티클 액터를
	// 회전시켜 스폰하면 이 값을 대부분 바꿀 필요가 없다.
	UPROPERTY(Edit, Save, Category="Blood Spray|Velocity Cone", DisplayName="Cone Axis Local")
	FVector ConeAxisLocal = FVector(1.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Blood Spray|Velocity Cone", DisplayName="Cone Half Angle Degrees", Min=0.0f, Max=180.0f)
	float ConeHalfAngleDegrees = 35.0f;

	UPROPERTY(Edit, Save, Category="Blood Spray|Velocity Cone", DisplayName="Min Speed")
	float MinSpeed = 180.0f;

	UPROPERTY(Edit, Save, Category="Blood Spray|Velocity Cone", DisplayName="Max Speed")
	float MaxSpeed = 450.0f;

	// true면 ConeAxisLocal/velocity를 이미 world space 값으로 보고 처리한다.
	// 보통 BloodHit는 파티클 액터 rotation으로 방향을 맞추므로 false가 맞다.
	UPROPERTY(Edit, Save, Category="Blood Spray|Velocity Cone", DisplayName="In World Space")
	bool bInWorldSpace = false;

	// false면 기존 Velocity를 교체한다. BloodSpray는 Initial Velocity를 대체하는 용도라 false가 기본이다.
	UPROPERTY(Edit, Save, Category="Blood Spray|Velocity Cone", DisplayName="Add To Existing Velocity")
	bool bAddToExistingVelocity = false;

	// 초당 감쇠 계수. 0이면 Drag 없음. 값이 클수록 속도가 빨리 죽는다.
	UPROPERTY(Edit, Save, Category="Blood Spray|Drag", DisplayName="Drag Coefficient", Min=0.0f)
	float DragCoefficient = 7.0f;

	// Sprite rotation은 renderer에서 radians로 소비되지만 에디터 입력은 degrees로 받는다.
	UPROPERTY(Edit, Save, Category="Blood Spray|Rotation", DisplayName="Initial Rotation Min Degrees")
	float InitialRotationMinDegrees = 0.0f;

	UPROPERTY(Edit, Save, Category="Blood Spray|Rotation", DisplayName="Initial Rotation Max Degrees")
	float InitialRotationMaxDegrees = 360.0f;

	UPROPERTY(Edit, Save, Category="Blood Spray|Rotation", DisplayName="Rotation Rate Min Degrees")
	float RotationRateMinDegrees = -720.0f;

	UPROPERTY(Edit, Save, Category="Blood Spray|Rotation", DisplayName="Rotation Rate Max Degrees")
	float RotationRateMaxDegrees = 720.0f;
};
