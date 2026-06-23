#pragma once

#include "Particle/ParticleModule.h"
#include "Particle/ParticleTypes.h"

struct FParticleEmitterInstance;
class UParticleSystemComponent;

// Deprecated Cascade-style TypeData. New runtime/render policy lives in UParticleRendererProperties.
// Kept as an internal bridge while editor/runtime call sites finish moving to RendererProperties.
UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleTypeDataBase, UParticleModule)

	// FBaseParticle 뒤에 type별로 요구하는 추가 payload byte 수.
	// 기본 0 (Sprite는 추가 payload 없음). Mesh/Ribbon/Beam이 override.
	virtual int32 RequiredPayloadBytes() const { return 0; }

	virtual EParticleEmitterRenderMode GetRenderMode() const { return RenderMode; }
	void SetRenderMode(EParticleEmitterRenderMode InRenderMode) { RenderMode = InRenderMode; }

	// emitter runtime instance 생성 hook. 기본은 base FParticleEmitterInstance 반환.
	// Mesh/Ribbon/Beam은 파생 instance를 반환하도록 override.
	virtual FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const;

private:
	UPROPERTY(DisplayName = "Render Mode", NoEdit)
	EParticleEmitterRenderMode RenderMode = EParticleEmitterRenderMode::Sprite;
};

// Deprecated sprite TypeData. New assets should use UParticleSpriteRendererProperties.
UCLASS()
class USpriteTypeData : public UParticleModuleTypeDataBase
{
public:
	GENERATED_BODY(USpriteTypeData, UParticleModuleTypeDataBase)

	int32 RequiredPayloadBytes() const override { return 0; }
	EParticleEmitterRenderMode GetRenderMode() const override { return EParticleEmitterRenderMode::Sprite; }
	FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const override;
};
