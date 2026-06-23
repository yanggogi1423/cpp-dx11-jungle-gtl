#pragma once

#include "Component/ActorComponent.h"
#include "Component/Gameplay/SniperTypes.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Component/Gameplay/SniperDamageReceiverComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FSniperDamageReceiverEventSignature, const FSniperHitInfo&);

class UCombatCoverAgentComponent;

UCLASS()
class USniperDamageReceiverComponent : public UActorComponent
{
public:
	GENERATED_BODY()

	USniperDamageReceiverComponent();
	~USniperDamageReceiverComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;

	UFUNCTION(Pure, Category="Sniper|Damage")
	float GetMaxHP() const;
	UFUNCTION(Pure, Category="Sniper|Damage")
	float GetCurrentHP() const;
	UFUNCTION(Pure, Category="Sniper|Damage")
	bool IsFriendly() const { return bIsFriendly; }
	UFUNCTION(Pure, Category="Sniper|Damage")
	bool HasArmor() const { return bHasArmor; }
	UFUNCTION(Pure, Category="Sniper|Damage")
	float GetArmorStrength() const { return ArmorStrength; }
	UFUNCTION(Pure, Category="Sniper|Damage")
	bool AllowsRicochet() const { return bAllowRicochet; }
	UFUNCTION(Pure, Category="Sniper|Damage")
	bool CanRagdoll() const { return bCanRagdoll; }
	UFUNCTION(Pure, Category="Sniper|Damage")
	bool IsDead() const;
	UFUNCTION(Pure, Category="Sniper|Damage")
	bool CanReceiveSniperHit() const;
	UFUNCTION(Pure, Category="Sniper|Damage")
	FSniperHitInfo ResolveSniperHit(const FSniperHitInfo& HitInfo) const;
	UFUNCTION(Callable, Category="Sniper|Damage")
	void ResetHealth();
	UFUNCTION(Callable, Category="Sniper|Damage")
	bool ApplySniperHit(const FSniperHitInfo& HitInfo);
	UFUNCTION(Callable, Category="Sniper|Damage")
	bool ApplyResolvedSniperHit(const FSniperHitInfo& HitInfo);

	FSniperDamageReceiverEventSignature OnSniperDamaged;
	FSniperDamageReceiverEventSignature OnSniperKilled;

private:
	FSniperHitInfo BuildResolvedHitInfo(const FSniperHitInfo& HitInfo) const;
	UCombatCoverAgentComponent* ResolveCombatCoverAgentComponent() const;
	bool IsFriendlyTarget() const;
	float GetDamageMultiplierForHitRegion(ESniperHitRegion HitRegion) const;

private:
	UPROPERTY(Edit, Save, Category="Sniper|Damage", DisplayName="Max HP", Min=1.0f, Max=10000.0f, Speed=1.0f)
	float MaxHP = 100.0f;

	UPROPERTY(Edit, Save, Category="Sniper|Damage", DisplayName="Current HP", Min=0.0f, Max=10000.0f, Speed=1.0f)
	float CurrentHP = 100.0f;

	UPROPERTY(Edit, Save, Category="Sniper|Damage", DisplayName="Friendly Target")
	bool bIsFriendly = false;

	UPROPERTY(Edit, Save, Category="Sniper|Damage", DisplayName="Has Armor")
	bool bHasArmor = false;

	UPROPERTY(Edit, Save, Category="Sniper|Damage", DisplayName="Armor Strength", Min=0.1f, Max=20.0f, Speed=0.1f)
	float ArmorStrength = 2.5f;

	UPROPERTY(Edit, Save, Category="Sniper|Damage", DisplayName="Allow Ricochet")
	bool bAllowRicochet = true;

	UPROPERTY(Edit, Save, Category="Sniper|Damage", DisplayName="Can Ragdoll")
	bool bCanRagdoll = true;

	UPROPERTY(Edit, Save, Category="Sniper|Damage|Region", DisplayName="Head Damage Multiplier", Min=0.0f, Max=10.0f, Speed=0.05f)
	float SniperHeadDamageMultiplier = 2.0f;

	UPROPERTY(Edit, Save, Category="Sniper|Damage|Region", DisplayName="Torso Damage Multiplier", Min=0.0f, Max=10.0f, Speed=0.05f)
	float SniperTorsoDamageMultiplier = 1.0f;

	UPROPERTY(Edit, Save, Category="Sniper|Damage|Region", DisplayName="Arm Damage Multiplier", Min=0.0f, Max=10.0f, Speed=0.05f)
	float SniperArmDamageMultiplier = 0.65f;

	UPROPERTY(Edit, Save, Category="Sniper|Damage|Region", DisplayName="Leg Damage Multiplier", Min=0.0f, Max=10.0f, Speed=0.05f)
	float SniperLegDamageMultiplier = 0.75f;

	bool bIsDead = false;
	mutable TWeakObjectPtr<UCombatCoverAgentComponent> CombatCoverAgentComponent = nullptr;
};
