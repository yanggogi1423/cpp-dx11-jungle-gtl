#pragma once

#include "Core/Delegate.h"
#include "Component/Gameplay/SniperTypes.h"
#include "GameFramework/Pawn/Character.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Object/Ptr/WeakObjectPtr.h"

class UCombatCoverAgentComponent;
class ULuaScriptComponent;
class USoundComponent;
class USniperDamageReceiverComponent;

#include "Source/Engine/GameFramework/Pawn/CombatCharacter.generated.h"

UCLASS()
class ACombatCharacter : public ACharacter
{
public:
	GENERATED_BODY()

	ACombatCharacter();
	~ACombatCharacter() override = default;
	void BeginPlay() override;
	void EndPlay() override;
	void Tick(float DeltaTime) override;

	void InitDefaultComponents(const FString& SkeletalMeshFileName, const FString& ScriptFile);

	void InitDefaultComponents(const FString& SkeletalMeshFileName) override
	{
		InitDefaultComponents(SkeletalMeshFileName, FString());
	}

	void PostDuplicate() override;

	ULuaScriptComponent* GetLuaScriptComponent() const;
	UCombatCoverAgentComponent* GetCombatCoverAgentComponent() const;
	USoundComponent* GetCombatGunfireSoundComponent() const;
	USniperDamageReceiverComponent* GetSniperDamageReceiverComponent() const;

protected:
	UPROPERTY(Edit, Save, Category="Sniper|Hit Query", DisplayName="Enable Persistent Query Bodies")
	bool bEnablePersistentQueryBodies = true;

	UPROPERTY(Edit, Save, Category="Sniper|Death Decal", DisplayName="Spawn Death Blood Decal")
	bool bSpawnDeathBloodDecal = true;

	UPROPERTY(Edit, Save, Category="Sniper|Death Decal", DisplayName="Blood Decal Material Path", AssetType="Material")
	FSoftObjectPtr DeathBloodDecalMaterialPath = "Content/Material/Editor/DefaultDecal.uasset";

	UPROPERTY(Edit, Save, Category="Sniper|Death Decal", DisplayName="Blood Decal Texture Path", AssetType="Texture")
	FSoftObjectPtr DeathBloodDecalTexturePath = "Content/Texture/blood_decal.png";

	UPROPERTY(Edit, Save, Category="Sniper|Death Decal", DisplayName="Blood Decal Lifetime", Min=0.1f, Max=120.0f, Speed=0.5f)
	float DeathBloodDecalLifetime = 30.0f;

	UPROPERTY(Edit, Save, Category="Sniper|Death Decal", DisplayName="Blood Decal Size", Min=0.1f, Max=10.0f, Speed=0.05f)
	float DeathBloodDecalSize = 1.6f;

	UPROPERTY(Edit, Save, Category="Sniper|Death Decal", DisplayName="Blood Decal Depth", Min=0.01f, Max=5.0f, Speed=0.01f)
	float DeathBloodDecalDepth = 0.2f;

	UPROPERTY(Edit, Save, Category="Sniper|Death Decal", DisplayName="Blood Decal Surface Offset", Min=0.0f, Max=1.0f, Speed=0.005f)
	float DeathBloodDecalSurfaceOffset = 0.01f;

	UPROPERTY(Edit, Save, Category="Sniper|Death Decal", DisplayName="Blood Decal Ground Trace Up", Min=0.0f, Max=20.0f, Speed=0.05f)
	float DeathBloodDecalGroundTraceUp = 1.5f;

	UPROPERTY(Edit, Save, Category="Sniper|Death Decal", DisplayName="Blood Decal Ground Trace Down", Min=0.1f, Max=50.0f, Speed=0.1f)
	float DeathBloodDecalGroundTraceDown = 8.0f;

	void ConfigureCombatGunfireSound(USoundComponent* Sound) const;
	void HandleSniperKilled(const FSniperHitInfo& HitInfo);
	void SpawnDeathBloodDecal(const FSniperHitInfo& HitInfo) const;

	struct FPendingDeathBloodDecal
	{
		bool bActive = false;
		bool bWaitForKillCamEnd = false;
		float DecisionGraceRemaining = 0.0f;
		int32 BulletId = 0;
		FSniperHitInfo HitInfo;
	};

	mutable TWeakObjectPtr<ULuaScriptComponent> LuaScriptComponent = nullptr;
	mutable TWeakObjectPtr<UCombatCoverAgentComponent> CombatCoverAgentComponent = nullptr;
	mutable TWeakObjectPtr<USoundComponent> CombatGunfireSoundComponent = nullptr;
	mutable TWeakObjectPtr<USniperDamageReceiverComponent> SniperDamageReceiverComponent = nullptr;
	FDelegateHandle SniperKilledHandle;
	bool bDeathBloodDecalSpawned = false;
	FPendingDeathBloodDecal PendingDeathBloodDecal;
};
