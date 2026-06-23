#pragma once

#include "Component/ActorComponent.h"
#include "Component/Gameplay/SniperTypes.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Component/Gameplay/SniperWeaponComponent.generated.h"

class UBallisticBulletManagerComponent;

UCLASS()
class USniperWeaponComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	USniperWeaponComponent();
	~USniperWeaponComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;

	UFUNCTION(Pure, Category="Sniper|Weapon")
	ESniperAmmoType GetCurrentAmmoType() const { return CurrentAmmoType; }
	UFUNCTION(Callable, Category="Sniper|Weapon")
	bool SetCurrentAmmoType(ESniperAmmoType InAmmoType);
	UFUNCTION(Pure, Category="Sniper|Weapon")
	bool IsZeroingEnabled() const { return bEnableZeroing; }
	UFUNCTION(Callable, Category="Sniper|Weapon")
	void SetZeroingEnabled(bool bInEnableZeroing) { bEnableZeroing = bInEnableZeroing; }
	UFUNCTION(Pure, Category="Sniper|Weapon")
	float GetZeroRangeMeters() const { return ZeroRangeMeters; }
	UFUNCTION(Callable, Category="Sniper|Weapon")
	void SetZeroRangeMeters(float InZeroRangeMeters);
	const FAmmoBallisticData* GetCurrentAmmoData() const;
	const FAmmoBallisticData* GetAmmoData(ESniperAmmoType InAmmoType) const;
	UFUNCTION(Pure, Category="Sniper|Weapon")
	bool CanFire() const;
	UFUNCTION(Callable, Category="Sniper|Weapon")
	bool RequestFire(const FVector& MuzzlePosition, const FVector& ShotDirection, bool bWasScopedShot, AActor* Shooter);
	UFUNCTION(Callable, Category="Sniper|Weapon")
	bool RequestReload();
	UFUNCTION(Callable, Category="Sniper|Weapon")
	void CancelReload();
	UFUNCTION(Pure, Category="Sniper|Weapon")
	bool IsReloading() const { return bIsReloading; }
	UFUNCTION(Pure, Category="Sniper|Weapon")
	float GetReloadRemaining() const { return ReloadRemaining; }
	UFUNCTION(Pure, Category="Sniper|Weapon")
	float GetReloadDuration() const { return ReloadDuration; }
	UFUNCTION(Pure, Category="Sniper|Weapon")
	float GetReloadProgress() const;
	UFUNCTION(Pure, Category="Sniper|Weapon")
	int32 GetAmmoInMagazine() const { return AmmoInMagazine; }
	UFUNCTION(Pure, Category="Sniper|Weapon")
	int32 GetMagazineCapacity() const { return MagazineCapacity; }
	UFUNCTION(Callable, Category="Sniper|Weapon")
	void NotifySniperHit(const FSniperHitInfo& HitInfo);
	UFUNCTION(Pure, Category="Sniper|Weapon")
	float GetFireCooldownRemaining() const { return FireCooldownRemaining; }
	UFUNCTION(Pure, Category="Sniper|Weapon")
	UBallisticBulletManagerComponent* GetBulletManagerComponent() const { return BulletManagerComponent.Get(); }

	FSniperHitEventSignature OnSniperHit;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	FVector BuildZeroedShotDirection(const FVector& ShotDirection, const FAmmoBallisticData& AmmoData) const;
	void NormalizeMagazineState();
	void CompleteReload();
	void InitializeDefaultAmmoData();
	void PlayWeaponSFX(const FString& SoundPath, float VolumeScale) const;
	void ResolveBulletManagerComponent();

	UPROPERTY(Edit, Save, Category="Sniper|Weapon")
	bool bEnableZeroing = true;
	UPROPERTY(Edit, Save, Category="Sniper|Weapon")
	float ZeroRangeMeters = 200.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Reload", Min=1, Max=100, Speed=1)
	int32 MagazineCapacity = 5;
	UPROPERTY(Edit, Save, Category="Sniper|Reload", Min=0, Max=100, Speed=1)
	int32 AmmoInMagazine = 5;
	UPROPERTY(Edit, Save, Category="Sniper|Reload", Min=0.0f, Max=10.0f, Speed=0.1f)
	float ReloadDuration = 3.528f;
	UPROPERTY(Edit, Save, Category="Sniper|Audio", DisplayName="Fire SFX Path")
	FString FireSFXPath = "SFX/Sniper/Shoot1.wav";
	UPROPERTY(Edit, Save, Category="Sniper|Audio", DisplayName="Reload SFX Path")
	FString ReloadSFXPath = "SFX/Sniper/rifle_reload.mp3";
	UPROPERTY(Edit, Save, Category="Sniper|Audio", DisplayName="Empty Trigger SFX Path")
	FString EmptyTriggerSFXPath = "SFX/Sniper/Empty_Gun_Trigger.mp3";
	UPROPERTY(Edit, Save, Category="Sniper|Audio", DisplayName="Fire SFX Volume", Min=0.0f, Max=1.0f, Speed=0.01f)
	float FireSFXVolume = 1.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Audio", DisplayName="Reload SFX Volume", Min=0.0f, Max=1.0f, Speed=0.01f)
	float ReloadSFXVolume = 1.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Audio", DisplayName="Empty Trigger SFX Volume", Min=0.0f, Max=1.0f, Speed=0.01f)
	float EmptyTriggerSFXVolume = 1.0f;
	ESniperAmmoType CurrentAmmoType = ESniperAmmoType::Normal;
	float FireCooldownRemaining = 0.0f;
	float ReloadRemaining = 0.0f;
	bool bIsReloading = false;
	TArray<FAmmoBallisticData> AmmoBallisticTable;
	TWeakObjectPtr<UBallisticBulletManagerComponent> BulletManagerComponent;
};
