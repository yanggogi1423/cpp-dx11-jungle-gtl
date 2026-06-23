#pragma once

#include "Component/Gameplay/SniperTypes.h"
#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Actor/SniperKillCamDirector.generated.h"

class APlayerCameraManager;
class AActor;
class UBallisticBulletManagerComponent;
class UCameraComponent;
class UActorSequenceComponent;
class UKillCamRailRigComponent;
class UStaticMeshComponent;
class UWorld;

UENUM()
enum class ESniperKillCamCameraMode : uint8
{
	SidePassToTail = 0,
	TailFollow = 1,
	SideFollow = 2
};

UCLASS()
class ASniperKillCamDirector : public AActor
{
public:
	GENERATED_BODY()

	ASniperKillCamDirector();

	void InitDefaultComponents();
	void BeginPlay() override;
	void EndPlay() override;
	void Tick(float DeltaTime) override;

	UFUNCTION(Callable, Category="Sniper|KillCam")
	bool StartForBulletId(int32 BulletId, float Duration = 10.0f, int32 CameraMode = 0);
	UFUNCTION(Callable, Category="Sniper|KillCam")
	void StopKillCam();
	UFUNCTION(Pure, Category="Sniper|KillCam")
	bool IsPlaying() const { return bPlaying; }
	UFUNCTION(Pure, Category="Sniper|KillCam")
	int32 GetActiveBulletId() const { return ActiveBulletId; }
	UFUNCTION(Callable, Category="Sniper|KillCam")
	bool SetRailRigScalar(const FString& PropertyName, float Value);
	UFUNCTION(Pure, Category="Sniper|KillCam")
	float GetRailRigScalar(const FString& PropertyName, float DefaultValue = 0.0f) const;
	UFUNCTION(Callable, Category="Sniper|KillCam")
	bool SetKillCamScalar(const FString& PropertyName, float Value);
	UFUNCTION(Pure, Category="Sniper|KillCam")
	float GetKillCamScalar(const FString& PropertyName, float DefaultValue = 0.0f) const;
	UFUNCTION(Callable, Category="Sniper|KillCam")
	bool SetKillCamString(const FString& PropertyName, const FString& Value);
	UFUNCTION(Pure, Category="Sniper|KillCam")
	FString GetKillCamString(const FString& PropertyName, const FString& DefaultValue = "") const;
	UFUNCTION(Callable, Category="Sniper|KillCam")
	bool SetKillCamVector(const FString& PropertyName, const FVector& Value);
	UFUNCTION(Pure, Category="Sniper|KillCam")
	FVector GetKillCamVector(const FString& PropertyName, const FVector& DefaultValue = FVector::ZeroVector) const;
	UFUNCTION(Callable, Category="Sniper|KillCam")
	bool SetKillCamRotator(const FString& PropertyName, const FRotator& Value);
	UFUNCTION(Pure, Category="Sniper|KillCam")
	FRotator GetKillCamRotator(const FString& PropertyName, const FRotator& DefaultValue = FRotator()) const;

	static void NotifyBulletSpawned(UBallisticBulletManagerComponent* Manager, const FBulletCinematicSnapshot& Snapshot);
	static void NotifyBulletHit(const FSniperHitInfo& HitInfo);
	static void NotifyBulletFloorHit(const FBulletCinematicSnapshot& Snapshot);
	static bool GetHitSnapshotForBulletId(int32 BulletId, FBulletCinematicSnapshot& OutSnapshot);
	static bool ConsumeFloorHitForBulletId(int32 BulletId, FBulletCinematicSnapshot& OutSnapshot);
	static bool CheckFloorHitInWorld(UWorld* World, int32 BulletId, FBulletCinematicSnapshot& OutSnapshot);
	static bool HasBulletRecord(int32 BulletId);
	static bool IsBulletPendingOrActive(int32 BulletId);
	static int32 ConsumePendingBulletId();
	static void ClearPendingBullets();
	static ASniperKillCamDirector* EnsureDirectorForWorld(UWorld* World);
	static ASniperKillCamDirector* FindDirectorForWorld(UWorld* World);
	static bool StartForBulletIdInWorld(UWorld* World, int32 BulletId, float Duration, int32 CameraMode);
	static void StopInWorld(UWorld* World);
	static bool IsPlayingInWorld(UWorld* World);
	static bool SetRailRigScalarInWorld(UWorld* World, const FString& PropertyName, float Value);
	static float GetRailRigScalarInWorld(UWorld* World, const FString& PropertyName, float DefaultValue);
	static bool SetKillCamScalarInWorld(UWorld* World, const FString& PropertyName, float Value);
	static float GetKillCamScalarInWorld(UWorld* World, const FString& PropertyName, float DefaultValue);
	static bool SetKillCamStringInWorld(UWorld* World, const FString& PropertyName, const FString& Value);
	static FString GetKillCamStringInWorld(UWorld* World, const FString& PropertyName, const FString& DefaultValue);
	static bool SetKillCamVectorInWorld(UWorld* World, const FString& PropertyName, const FVector& Value);
	static FVector GetKillCamVectorInWorld(UWorld* World, const FString& PropertyName, const FVector& DefaultValue);
	static bool SetKillCamRotatorInWorld(UWorld* World, const FString& PropertyName, const FRotator& Value);
	static FRotator GetKillCamRotatorInWorld(UWorld* World, const FString& PropertyName, const FRotator& DefaultValue);

private:
	void EnsureCameraComponent();
	void EnsureBulletVisualComponent();
	void DestroyBulletVisualActor();
	UKillCamRailRigComponent* ResolveRailRigComponent();
	UActorSequenceComponent* ResolveRailSequenceComponent();
	void ScrubRailSequence(float RailAlpha);
	APlayerCameraManager* ResolveCameraManager() const;
	bool ResolveBulletSnapshot(FBulletCinematicSnapshot& OutSnapshot) const;
	FBulletCinematicSnapshot BuildPlaybackSnapshot(float Alpha, bool bClampAlpha = true) const;
	FBulletCinematicSnapshot ResolveSnapshotAtRailAlpha(
		float RailAlpha,
		const FBulletCinematicSnapshot& FallbackSnapshot,
		const UKillCamRailRigComponent* Rig) const;
	FBulletCinematicSnapshot BuildBulletVisualCollisionSnapshot(
		const FBulletCinematicSnapshot& FallbackSnapshot,
		float RailAlpha);
	void UpdateCameraFromSnapshot(const FBulletCinematicSnapshot& Snapshot, float DeltaTime, float RailAlpha);
	void UpdateBulletVisualFromSnapshot(const FBulletCinematicSnapshot& Snapshot, float RailAlpha);
	void UpdateShockWaveFromSnapshot(const FBulletCinematicSnapshot& Snapshot, float RailAlpha);
	void ClearShockWave();
	void SetBulletVisualVisible(bool bVisible);
	void RestorePreviousCamera();
	bool CheckFloorHitNow(FBulletCinematicSnapshot& OutSnapshot);

	FVector ComputeCameraOffset(const FVector& Direction, float Alpha) const;
	FVector ComputeOrbitOffset(const FVector& Direction, float YawDegrees, float PitchDegrees, float Radius) const;
	float ComputeDistanceScale(const UKillCamRailRigComponent* Rig) const;
	FVector ComputeSideVector(const FVector& Direction) const;

	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float DefaultDuration = 10.0f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float StartForwardDistance = 1.25f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float StartSideDistance = 1.0f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float StartUpDistance = 0.35f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float FollowBackDistance = 2.2f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float FollowSideDistance = 0.18f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float FollowUpDistance = 0.22f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float LookAheadDistance = 0.75f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float CameraLagSpeed = 12.0f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float TransitionToTailTime = 2.5f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float KillCamFOV = 0.72f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float DOFFocusRange = 1.25f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam")
	float DOFBlurRadius = 4.0f;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam|Rail Rig")
	bool bUseRailRigComponent = true;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam|Rail Rig")
	bool bAutoCreateRailRigComponent = true;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam|Rail Rig")
	bool bScrubRailSequenceByRailAlpha = true;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam|Rail Rig")
	bool bAutoCreateRailSequenceComponent = true;
	UPROPERTY(Edit, Save, Category="Sniper|KillCam|Bullet Visual", AssetType="Prefab")
	FString CinematicBulletPrefabPath = "";
	UPROPERTY(Edit, Save, Category="Sniper|KillCam|Bullet Visual", AssetType="StaticMesh")
	FString CinematicBulletMeshPath = "Content/Data/SniperBullet/bullet.fbx";
	UPROPERTY(Edit, Save, Category="Sniper|KillCam|Bullet Visual", AssetType="Material")
	FString CinematicBulletMaterialPath = "Content/Material/Auto/Barrett M82 Magazine and Bullet.uasset";
	UPROPERTY(Edit, Save, Category="Sniper|KillCam|Bullet Visual")
	FVector CinematicBulletScale = FVector(0.0025f, 0.0025f, 0.0025f);
	UPROPERTY(Edit, Save, Category="Sniper|KillCam|Bullet Visual")
	FRotator CinematicBulletRotationOffset = FRotator();

	TWeakObjectPtr<UCameraComponent> CinematicCamera;
	TWeakObjectPtr<UKillCamRailRigComponent> RailRigComponent;
	TWeakObjectPtr<UActorSequenceComponent> RailSequenceComponent;
	TWeakObjectPtr<AActor> CinematicBulletActor;
	FVector CinematicBulletPrefabBaseScale = FVector::OneVector;
	TWeakObjectPtr<UStaticMeshComponent> CinematicBulletVisual;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	TWeakObjectPtr<UCameraComponent> PreviousActiveCamera;
	TWeakObjectPtr<UBallisticBulletManagerComponent> ActiveBulletManager;
	int32 ActiveBulletId = 0;
	int32 ShockWaveHandle = 0;
	float Elapsed = 0.0f;
	float Duration = 0.0f;
	ESniperKillCamCameraMode ActiveCameraMode = ESniperKillCamCameraMode::SidePassToTail;
	FBulletCinematicSnapshot StartSnapshot;
	FBulletCinematicSnapshot HitSnapshot;
	FBulletCinematicSnapshot LastSnapshot;
	FBulletCinematicSnapshot LastVisualCollisionSnapshot;
	bool bHasHitSnapshot = false;
	bool bHasLastVisualCollisionSnapshot = false;
	bool bPlaying = false;
};
