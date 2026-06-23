#include "Component/Gameplay/SniperDamageReceiverComponent.h"

#include "Component/Gameplay/CombatCoverAgentComponent.h"
#include "GameFramework/AActor.h"
#include "Math/MathUtils.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace
{
	constexpr float SniperRicochetAlignmentThreshold = 0.45f;
	constexpr float SniperArmorPenetrationSpeedScale = 350.0f;
	constexpr float SniperBlockedDamageMultiplier = 0.15f;
	constexpr float SniperPenetratedDamageMultiplier = 0.75f;

	FString NormalizeTeamToken(FString Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char Character)
		{
			return static_cast<char>(std::tolower(Character));
		});
		return Text;
	}

	bool MatchesAnyTeamToken(const FString& Text, std::initializer_list<const char*> Tokens)
	{
		const FString NormalizedText = NormalizeTeamToken(Text);
		for (const char* Token : Tokens)
		{
			if (NormalizedText == Token)
			{
				return true;
			}
		}
		return false;
	}

	bool HasAnyTag(const TArray<FName>& Tags, std::initializer_list<const char*> Tokens)
	{
		for (const FName& Tag : Tags)
		{
			if (MatchesAnyTeamToken(Tag.ToString(), Tokens))
			{
				return true;
			}
		}
		return false;
	}

	bool HasEnemyTag(const TArray<FName>& Tags)
	{
		return HasAnyTag(Tags, { "enemy", "hostile", "opfor" });
	}

	bool HasFriendlyTag(const TArray<FName>& Tags)
	{
		return HasAnyTag(Tags, { "ally", "friendly", "player", "bravo" });
	}
}

USniperDamageReceiverComponent::USniperDamageReceiverComponent()
{
	bTickEnable = false;
}

void USniperDamageReceiverComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	ResetHealth();
}

void USniperDamageReceiverComponent::EndPlay()
{
	CombatCoverAgentComponent = nullptr;
	UActorComponent::EndPlay();
}

float USniperDamageReceiverComponent::GetMaxHP() const
{
	if (UCombatCoverAgentComponent* CombatAgent = ResolveCombatCoverAgentComponent())
	{
		return (std::max)(CombatAgent->GetMaxHealth(), 1.0f);
	}

	return MaxHP;
}

float USniperDamageReceiverComponent::GetCurrentHP() const
{
	if (UCombatCoverAgentComponent* CombatAgent = ResolveCombatCoverAgentComponent())
	{
		return FMath::Clamp(CombatAgent->GetHealth(), 0.0f, GetMaxHP());
	}

	return CurrentHP;
}

bool USniperDamageReceiverComponent::IsDead() const
{
	if (UCombatCoverAgentComponent* CombatAgent = ResolveCombatCoverAgentComponent())
	{
		return !CombatAgent->IsAlive();
	}

	return bIsDead;
}

bool USniperDamageReceiverComponent::CanReceiveSniperHit() const
{
	if (UCombatCoverAgentComponent* CombatAgent = ResolveCombatCoverAgentComponent())
	{
		return CombatAgent->IsAlive() && CombatAgent->GetHealth() > 0.0f;
	}

	return !bIsDead && CurrentHP > 0.0f;
}

FSniperHitInfo USniperDamageReceiverComponent::ResolveSniperHit(const FSniperHitInfo& HitInfo) const
{
	return BuildResolvedHitInfo(HitInfo);
}

void USniperDamageReceiverComponent::ResetHealth()
{
	if (MaxHP < 1.0f)
	{
		MaxHP = 1.0f;
	}

	if (UCombatCoverAgentComponent* CombatAgent = ResolveCombatCoverAgentComponent())
	{
		MaxHP = (std::max)(CombatAgent->GetMaxHealth(), 1.0f);
		CurrentHP = FMath::Clamp(CombatAgent->GetHealth(), 0.0f, MaxHP);
		bIsDead = !CombatAgent->IsAlive() || CurrentHP <= 0.0f;
		return;
	}

	CurrentHP = FMath::Clamp(CurrentHP, 0.0f, MaxHP);
	if (CurrentHP <= 0.0f || CurrentHP > MaxHP)
	{
		CurrentHP = MaxHP;
	}

	bIsDead = false;
}

bool USniperDamageReceiverComponent::ApplySniperHit(const FSniperHitInfo& HitInfo)
{
	return ApplyResolvedSniperHit(BuildResolvedHitInfo(HitInfo));
}

bool USniperDamageReceiverComponent::ApplyResolvedSniperHit(const FSniperHitInfo& HitInfo)
{
	FSniperHitInfo ResolvedHitInfo = HitInfo;
	const float AppliedDamage = HitInfo.Damage < 0.0f ? 0.0f : HitInfo.Damage;
	ResolvedHitInfo.Damage = AppliedDamage;
	ResolvedHitInfo.bFriendlyTarget = IsFriendlyTarget();

	if (UCombatCoverAgentComponent* CombatAgent = ResolveCombatCoverAgentComponent())
	{
		const float SafeMaxHealth = (std::max)(CombatAgent->GetMaxHealth(), 1.0f);
		const float PreviousHealth = FMath::Clamp(CombatAgent->GetHealth(), 0.0f, SafeMaxHealth);
		if (!CombatAgent->IsAlive() || PreviousHealth <= 0.0f)
		{
			MaxHP = SafeMaxHealth;
			CurrentHP = PreviousHealth;
			bIsDead = true;
			ResolvedHitInfo.TargetMaxHP = SafeMaxHealth;
			ResolvedHitInfo.TargetCurrentHP = PreviousHealth;
			ResolvedHitInfo.bKilled = true;
			return false;
		}

		CombatAgent->ApplyDamage(AppliedDamage);

		const float CurrentHealth = FMath::Clamp(CombatAgent->GetHealth(), 0.0f, SafeMaxHealth);
		MaxHP = SafeMaxHealth;
		CurrentHP = CurrentHealth;
		bIsDead = !CombatAgent->IsAlive() || CurrentHealth <= 0.0f;
		ResolvedHitInfo.TargetMaxHP = SafeMaxHealth;
		ResolvedHitInfo.TargetCurrentHP = CurrentHealth;
		ResolvedHitInfo.bKilled = PreviousHealth > 0.0f && CurrentHealth <= 0.0f;
		CombatAgent->RecordSniperHit(ResolvedHitInfo);

		OnSniperDamaged.Broadcast(ResolvedHitInfo);

		if (ResolvedHitInfo.bKilled)
		{
			OnSniperKilled.Broadcast(ResolvedHitInfo);
		}

		return true;
	}

	if (!CanReceiveSniperHit())
	{
		return false;
	}

	CurrentHP = FMath::Clamp(CurrentHP - AppliedDamage, 0.0f, MaxHP);
	ResolvedHitInfo.TargetMaxHP = MaxHP;
	ResolvedHitInfo.TargetCurrentHP = CurrentHP;
	ResolvedHitInfo.bKilled = CurrentHP <= 0.0f;
	OnSniperDamaged.Broadcast(ResolvedHitInfo);

	if (ResolvedHitInfo.bKilled)
	{
		bIsDead = true;
		OnSniperKilled.Broadcast(ResolvedHitInfo);
	}

	return true;
}

UCombatCoverAgentComponent* USniperDamageReceiverComponent::ResolveCombatCoverAgentComponent() const
{
	if (UCombatCoverAgentComponent* CombatAgent = CombatCoverAgentComponent.Get())
	{
		return CombatAgent;
	}

	AActor* OwnerActor = GetOwner();
	CombatCoverAgentComponent = OwnerActor ? OwnerActor->GetComponentByClass<UCombatCoverAgentComponent>() : nullptr;
	return CombatCoverAgentComponent.Get();
}

bool USniperDamageReceiverComponent::IsFriendlyTarget() const
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor != nullptr)
	{
		const TArray<FName> OwnerTags = OwnerActor->GetTags();
		if (HasEnemyTag(OwnerTags))
		{
			return false;
		}
		if (HasFriendlyTag(OwnerTags))
		{
			return true;
		}
	}

	const TArray<FName> ComponentTags = GetTags();
	if (HasEnemyTag(ComponentTags))
	{
		return false;
	}
	if (HasFriendlyTag(ComponentTags))
	{
		return true;
	}

	if (UCombatCoverAgentComponent* CombatAgent = ResolveCombatCoverAgentComponent())
	{
		const FString& TeamTag = CombatAgent->GetTeamTag();
		if (MatchesAnyTeamToken(TeamTag, { "enemy", "hostile", "opfor" }))
		{
			return false;
		}
		if (MatchesAnyTeamToken(TeamTag, { "ally", "friendly", "player", "bravo" }))
		{
			return true;
		}
	}

	return bIsFriendly;
}

float USniperDamageReceiverComponent::GetDamageMultiplierForHitRegion(ESniperHitRegion HitRegion) const
{
	switch (HitRegion)
	{
	case ESniperHitRegion::Head:
		return SniperHeadDamageMultiplier;
	case ESniperHitRegion::Torso:
		return SniperTorsoDamageMultiplier;
	case ESniperHitRegion::Arm:
		return SniperArmDamageMultiplier;
	case ESniperHitRegion::Leg:
		return SniperLegDamageMultiplier;
	case ESniperHitRegion::Unknown:
	default:
		return 1.0f;
	}
}

FSniperHitInfo USniperDamageReceiverComponent::BuildResolvedHitInfo(const FSniperHitInfo& HitInfo) const
{
	FSniperHitInfo ResolvedHitInfo = HitInfo;

	const FVector SurfaceNormal = HitInfo.HitNormal.IsNearlyZero()
		? FVector::UpVector
		: HitInfo.HitNormal.Normalized();
	const FVector IncomingDirection = HitInfo.ShotDirection.IsNearlyZero()
		? FVector::ForwardVector
		: HitInfo.ShotDirection.Normalized();
	const FVector ReverseIncomingDirection = IncomingDirection * -1.0f;
	const float SurfaceAlignment = FMath::Clamp(ReverseIncomingDirection.Dot(SurfaceNormal), 0.0f, 1.0f);
	const float RegionDamageMultiplier = (std::max)(GetDamageMultiplierForHitRegion(HitInfo.HitRegion), 0.0f);

	ResolvedHitInfo.HitOutcome = ESniperHitOutcome::Normal;
	ResolvedHitInfo.RegionDamageMultiplier = RegionDamageMultiplier;
	ResolvedHitInfo.HitScoreMultiplier = RegionDamageMultiplier;
	if (ResolvedHitInfo.HitScoreValue <= 0)
	{
		ResolvedHitInfo.HitScoreValue = GetDefaultSniperHitScoreValue(ResolvedHitInfo.HitRegion);
	}
	ResolvedHitInfo.HitScoreValue = static_cast<int32>((std::max)(0.0f, ResolvedHitInfo.HitScoreValue * RegionDamageMultiplier) + 0.5f);
	if (ResolvedHitInfo.HitBodyName.empty() && ResolvedHitInfo.HitBoneName.IsValid() && ResolvedHitInfo.HitBoneName != FName::None)
	{
		ResolvedHitInfo.HitBodyName = ResolvedHitInfo.HitBoneName.ToString();
	}
	if (ResolvedHitInfo.HitRegionName.empty())
	{
		ResolvedHitInfo.HitRegionName = GetSniperHitRegionName(ResolvedHitInfo.HitRegion);
	}
	if (ResolvedHitInfo.HitRegionDisplayName.empty())
	{
		ResolvedHitInfo.HitRegionDisplayName = GetSniperHitRegionDisplayName(ResolvedHitInfo.HitRegion);
	}
	ResolvedHitInfo.bShouldRagdoll = HitInfo.bShouldRagdoll && bCanRagdoll;

	if (bHasArmor)
	{
		const bool bRicochet = bAllowRicochet && SurfaceAlignment <= SniperRicochetAlignmentThreshold;
		const float PenetrationThreshold = ArmorStrength * SniperArmorPenetrationSpeedScale;
		const bool bCanPenetrateArmor = HitInfo.bIsArmorPiercing && HitInfo.ImpactSpeed >= PenetrationThreshold;

		if (bRicochet)
		{
			ResolvedHitInfo.HitOutcome = ESniperHitOutcome::Ricochet;
			ResolvedHitInfo.Damage = 0.0f;
			ResolvedHitInfo.bShouldRagdoll = false;
			ResolvedHitInfo.RagdollImpulseStrength = 0.0f;
		}
		else if (bCanPenetrateArmor)
		{
			ResolvedHitInfo.HitOutcome = ESniperHitOutcome::Penetrated;
			ResolvedHitInfo.Damage = HitInfo.Damage * SniperPenetratedDamageMultiplier;
		}
		else
		{
			ResolvedHitInfo.HitOutcome = ESniperHitOutcome::Blocked;
			ResolvedHitInfo.Damage = HitInfo.Damage * SniperBlockedDamageMultiplier;
			ResolvedHitInfo.bShouldRagdoll = false;
			ResolvedHitInfo.RagdollImpulseStrength *= 0.35f;
		}
	}

	ResolvedHitInfo.Damage *= RegionDamageMultiplier;

	return ResolvedHitInfo;
}
