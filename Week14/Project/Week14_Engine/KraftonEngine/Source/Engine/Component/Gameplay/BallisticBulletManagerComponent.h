#pragma once

#include "Component/ActorComponent.h"
#include "Component/Gameplay/SniperTypes.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Component/Gameplay/BallisticBulletManagerComponent.generated.h"

class UBillboardComponent;
class UMaterial;
class USkeletalMeshComponent;
class USniperWeaponComponent;
class FPhysicsAssetInstance;

enum class ESniperPreciseHitQueryMode : uint8
{
	None = 0,
	CharacterQueryBody,
	LiveRagdollBody,
	PosePhysicsAssetFallback,
};

enum class ESniperPreciseHitRejectReason : uint8
{
	None = 0,
	NoSkeletalMesh,
	NoPhysicsScene,
	QueryBodySyncFailed,
	PreciseMissAfterBroadHit,
	BroadPreciseDistanceExceeded,
	PoseFallbackNoHit,
};

struct FSniperPreciseHitQueryDiagnostics
{
	ESniperPreciseHitQueryMode QueryMode = ESniperPreciseHitQueryMode::None;
	ESniperPreciseHitRejectReason RejectReason = ESniperPreciseHitRejectReason::None;
	FName BroadHitBoneName = FName::None;
	FName PreciseHitBoneName = FName::None;
	FVector BroadHitLocation = FVector::ZeroVector;
	FVector PreciseHitLocation = FVector::ZeroVector;
	float BroadToPreciseDistance = 0.0f;
	float BroadToPreciseTravelDelta = 0.0f;
	bool bAccepted = false;
	bool bSyncAttempted = false;
	bool bSyncSucceeded = false;
};

UCLASS()
class UBallisticBulletManagerComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UBallisticBulletManagerComponent();
	~UBallisticBulletManagerComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;

	UFUNCTION(Callable, Category="Sniper|Bullet")
	bool SpawnBullet(const FBallisticBullet& Bullet);
	UFUNCTION(Callable, Category="Sniper|Bullet")
	void ResetBullets();
	UFUNCTION(Pure, Category="Sniper|Bullet")
	int32 GetAliveBulletCount() const { return static_cast<int32>(ActiveBullets.size()); }
	UFUNCTION(Pure, Category="Sniper|Bullet")
	const TArray<FBallisticBullet>& GetActiveBullets() const { return ActiveBullets; }
	UFUNCTION(Pure, Category="Sniper|Bullet")
	bool GetBulletSnapshotById(int32 BulletId, FBulletCinematicSnapshot& OutSnapshot) const;
	UFUNCTION(Pure, Category="Sniper|Bullet")
	FBulletCinematicSnapshot GetLatestBulletSnapshot() const;
	UFUNCTION(Pure, Category="Sniper|Bullet")
	USniperWeaponComponent* GetWeaponComponent() const { return WeaponComponent.Get(); }
	UFUNCTION(Pure, Category="Sniper|Wind")
	bool IsWindEnabled() const;
	UFUNCTION(Callable, Category="Sniper|Wind")
	void SetWindEnabled(bool bInEnableWind);
	UFUNCTION(Pure, Category="Sniper|Wind")
	FVector GetWindAcceleration() const;
	UFUNCTION(Callable, Category="Sniper|Wind")
	void SetWindAcceleration(const FVector& InWindAcceleration);

	FBulletSpawnedEventSignature OnBulletSpawned;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void UpdateBullets(float DeltaTime);
	void UpdateSingleBullet(FBallisticBullet& Bullet, const FVector& WorldGravity, const FVector& AppliedWindAcceleration, float DeltaTime, float LifetimeDeltaTime, class UWorld* World);
	void UpdateImpactVisuals(float DeltaTime);
	void SyncBulletVisuals();
	void HideAllBulletVisuals();
	UBillboardComponent* GetOrCreateBulletHeadVisual(int32 VisualIndex);
	UBillboardComponent* GetOrCreateBulletTracerVisual(int32 VisualIndex);
	UBillboardComponent* GetOrCreateImpactVisual(int32 VisualIndex);
	UMaterial* ResolveBulletHeadVisualMaterial();
	UMaterial* ResolveBulletTracerVisualMaterial();
	UMaterial* ResolveImpactVisualMaterial();
	UMaterial* ResolveBulletImpactDecalMaterial();
	void SpawnImpactVisual(const FVector& ImpactLocation);
	void SpawnBulletImpactDecal(const FBallisticBullet& Bullet, const struct FHitResult& Hit, class UWorld* World);
	bool ShouldSpawnBulletImpactDecal(const struct FHitResult& Hit) const;
	FVector4 PickBulletImpactDecalAtlasRect(const FBallisticBullet& Bullet, const struct FHitResult& Hit) const;
	bool QueryBulletHit(const FBallisticBullet& Bullet, class UWorld* World, struct FHitResult& OutHit) const;
	bool ShouldRunPreciseCharacterHitQuery(const struct FHitResult& BroadHit) const;
	bool ShouldRunPosePhysicsAssetHitQuery(const struct FHitResult& BroadHit) const;
	bool EnsurePreciseHitQueryBodies(USkeletalMeshComponent* SkeletalMeshComponent, bool& bOutCreatedTemporaryBodies) const;
	bool QueryTaggedFloorBoundsHit(const FBallisticBullet& Bullet, class UWorld* World, struct FHitResult& OutHit) const;
	bool IsFloorHit(const struct FHitResult& Hit) const;
	float ResolveBroadToPreciseDistanceThreshold(ESniperPreciseHitQueryMode QueryMode) const;
	bool QueryCharacterQueryBodyHit(const FBallisticBullet& Bullet, class UWorld* World, const struct FHitResult& BroadHit, struct FHitResult& OutPreciseHit, FSniperPreciseHitQueryDiagnostics* OutDiagnostics = nullptr) const;
	bool QueryPreciseCharacterHit(const FBallisticBullet& Bullet, class UWorld* World, const struct FHitResult& BroadHit, struct FHitResult& OutPreciseHit, FSniperPreciseHitQueryDiagnostics* OutDiagnostics = nullptr) const;
	bool QueryPosePhysicsAssetCharacterHit(const FBallisticBullet& Bullet, const struct FHitResult& BroadHit, struct FHitResult& OutPreciseHit, FSniperPreciseHitQueryDiagnostics* OutDiagnostics = nullptr) const;
	void HandleBulletHit(FBallisticBullet& Bullet, const struct FHitResult& Hit, class UWorld* World);
	FSniperHitInfo BuildSniperHitInfo(const FBallisticBullet& Bullet, const struct FHitResult& Hit) const;
	USkeletalMeshComponent* ResolveHitSkeletalMeshComponent(const struct FHitResult& Hit) const;
	bool ResolveHitBodyCenterMetrics(const struct FHitResult& Hit, const FName& HitBoneName, FVector& OutBodyCenter, float& OutDistance) const;
	bool ShouldNotifyKillCamForHit(const FSniperHitInfo& HitInfo) const;
	FName ResolvePreciseHitBoneName(const struct FHitResult& Hit, bool* bOutUsedFallback = nullptr) const;
	FString NormalizeBoneNameForHitClassification(const FName& BoneName) const;
	bool IsAuxiliaryBoneNameNormalized(const FString& BoneName) const;
	ESniperHitRegion ClassifyHitRegionNormalized(const FString& BoneName) const;
	ESniperHitRegion ClassifyHitRegion(const FName& BoneName) const;
	bool IsHeadshotBoneNameNormalized(const FString& BoneName) const;
	bool IsHeadshotBoneName(const FName& BoneName) const;
	FBulletCinematicSnapshot BuildBulletSnapshot(const FBallisticBullet& Bullet) const;
	void CompactDeadBullets();
	void ResolveWeaponComponent();

	UPROPERTY(Edit, Save, Category="Sniper|Simulation")
	bool bEnableBallisticSubsteps = true;
	UPROPERTY(Edit, Save, Category="Sniper|Simulation")
	int32 MaxBallisticSubsteps = 2;
	UPROPERTY(Edit, Save, Category="Sniper|Simulation")
	float MaxBallisticSubstepDeltaTime = 1.0f / 120.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Simulation", DisplayName="Crosswind Influence Multiplier", Min=0.0f, Max=5.0f, Speed=0.05f)
	float CrosswindInfluenceMultiplier = 1.85f;
	UPROPERTY(Edit, Save, Category="Sniper|Hit")
	bool bEnablePreciseCharacterHitQuery = true;
	UPROPERTY(Edit, Save, Category="Sniper|Hit")
	float MaxPreciseCharacterHitDistance = 0.25f;
	UPROPERTY(Edit, Save, Category="Sniper|Hit")
	float MaxQueryBodyBroadToPreciseDistance = 0.12f;
	UPROPERTY(Edit, Save, Category="Sniper|Hit")
	float MaxRagdollBroadToPreciseDistance = 0.25f;
	UPROPERTY(Edit, Save, Category="Sniper|Hit")
	float MaxPoseFallbackBroadToPreciseDistance = 0.20f;
	UPROPERTY(Edit, Save, Category="Sniper|Hit")
	bool bRejectBroadHitWhenQueryBodyPreciseMisses = false;
	UPROPERTY(Edit, Save, Category="Sniper|Hit")
	bool bAllowPoseFallbackWhenQueryBodySyncFails = false;
	UPROPERTY(Edit, Save, Category="Sniper|Hit")
	bool bLogPreciseCharacterHitDiagnostics = true;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	bool bEnableKillCamBodyCenterDistanceFilter = true;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam", DisplayName="Max KillCam Body Center Distance")
	float MaxKillCamBodyCenterDistance = 0.18f;
	UPROPERTY(Edit, Save, Category="Sniper|Visual")
	bool bEnableBulletVisuals = true;
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Bullet Head Visual Material", AssetType="Material")
	FSoftObjectPtr BulletHeadVisualMaterialPath = "Content/Material/Particle/ParticleSprite.uasset";
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Bullet Tracer Visual Material", AssetType="Material")
	FSoftObjectPtr BulletTracerVisualMaterialPath = "Content/Material/Particle/ParticleSprite.uasset";
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Bullet Head Visual Scale Multiplier")
	float BulletHeadVisualScaleMultiplier = 1.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Bullet Tracer Width Multiplier")
	float BulletTracerWidthMultiplier = 1.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Bullet Tracer Length Multiplier")
	float BulletTracerLengthMultiplier = 1.0f;
	UPROPERTY(Edit, Save, Category="Sniper|Visual")
	bool bEnableImpactVisuals = true;
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Impact Visual Material", AssetType="Material")
	FSoftObjectPtr ImpactVisualMaterialPath = "Content/Material/Particle/ParticleSprite.uasset";
	UPROPERTY(Edit, Save, Category="Sniper|Visual")
	float ImpactVisualScale = 0.075f;
	UPROPERTY(Edit, Save, Category="Sniper|Visual")
	float ImpactVisualLifetime = 0.08f;
	UPROPERTY(Edit, Save, Category="Sniper|Visual")
	bool bEnableBulletImpactDecals = true;
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Bullet Impact Decal Material", AssetType="Material")
	FSoftObjectPtr BulletImpactDecalMaterialPath = "Content/Material/Editor/DefaultDecal.uasset";
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Bullet Impact Decal Size")
	float BulletImpactDecalSize = 0.22f;
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Bullet Impact Decal Depth")
	float BulletImpactDecalDepth = 0.08f;
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Bullet Impact Decal Surface Offset")
	float BulletImpactDecalSurfaceOffset = 0.01f;
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Bullet Impact Decal Atlas Columns")
	int32 BulletImpactDecalAtlasColumns = 8;
	UPROPERTY(Edit, Save, Category="Sniper|Visual", DisplayName="Bullet Impact Decal Atlas Rows")
	int32 BulletImpactDecalAtlasRows = 8;
	UPROPERTY(Edit, Save, Category="Sniper|Visual")
	bool bDrawDebugBallistics = false;
	UPROPERTY(Edit, Save, Category="Sniper|Visual")
	bool bDrawDebugImpactMarker = false;

	TWeakObjectPtr<USniperWeaponComponent> WeaponComponent;
	TArray<TWeakObjectPtr<UBillboardComponent>> BulletHeadVisualPool;
	TArray<TWeakObjectPtr<UBillboardComponent>> BulletTracerVisualPool;
	TArray<TWeakObjectPtr<UBillboardComponent>> ImpactVisualPool;
	TArray<float> ImpactVisualRemainingTimes;
	TWeakObjectPtr<UMaterial> BulletHeadVisualMaterial;
	TWeakObjectPtr<UMaterial> BulletTracerVisualMaterial;
	TWeakObjectPtr<UMaterial> ImpactVisualMaterial;
	TWeakObjectPtr<UMaterial> BulletImpactDecalMaterial;
	TArray<FBallisticBullet> ActiveBullets;
	int32 NextBulletId = 1;
};
