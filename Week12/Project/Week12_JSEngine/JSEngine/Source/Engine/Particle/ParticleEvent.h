#pragma once

#include "GameFramework/AActor.h"
#include "Particle/ParticleTypes.h"

class UParticleSystemComponent;

DECLARE_DELEGATE(FOnParticleEventCollide, const FParticleEventCollideData&);

UCLASS(Placeable, DisplayName = "Particle Event Manager", Category = "Effects")
class AParticleEventManager : public AActor
{
public:
	GENERATED_BODY(AParticleEventManager, AActor)

	~AParticleEventManager() override;

	void BindToParticleSystemComponent(UParticleSystemComponent* InComponent);
	void UnbindParticleSystemComponent();
	UParticleSystemComponent* GetBoundParticleSystemComponent() const { return BoundComponent; }

	void PushCollisionEvent(const FParticleEventCollideData& EventData);
	void DispatchCollisionEvents(const TArray<FParticleEventCollideData>& EventDataList);
	void DispatchEvents();
	const TArray<FParticleEventCollideData>& GetCollisionEvents() const { return CollisionEvents; }
	void ClearEvents() { CollisionEvents.clear(); }

	void InitDefaultComponents() override;

	FOnParticleEventCollide OnParticleCollide;

private:
	void HandleParticleCollide(const FParticleEventCollideData& EventData);

	TArray<FParticleEventCollideData> CollisionEvents;
	UParticleSystemComponent* BoundComponent = nullptr;
	uint64 BoundCollisionDelegateId = 0;
};
