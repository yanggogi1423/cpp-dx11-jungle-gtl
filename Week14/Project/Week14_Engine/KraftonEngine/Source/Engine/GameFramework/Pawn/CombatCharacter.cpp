#include "GameFramework/Pawn/CombatCharacter.h"

#include "Component/Gameplay/CombatCoverAgentComponent.h"
#include "Component/Gameplay/SniperDamageReceiverComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/SoundComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Engine/Runtime/Engine.h"
#include "GameFramework/Actor/DecalActor.h"
#include "GameFramework/Actor/SniperKillCamDirector.h"
#include "GameFramework/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialManager.h"
#include "Texture/Texture2D.h"

#include <algorithm>
#include <cmath>

namespace
{
	const FString CombatGunfireSoundPath = "SFX/CombatAI/npc_gun_fire.mp3";
	constexpr float CombatGunfireVolume = 0.5f;
	constexpr float CombatGunfireMinDistance = 1.0f;
	constexpr float CombatGunfireMaxDistance = 80.0f;
	constexpr const char* DefaultDeathBloodDecalMaterialPath = "Content/Material/Editor/DefaultDecal.uasset";
	constexpr float DeathBloodDecalKillCamDecisionGrace = 0.1f;

	FQuat MakeRotationWithForwardX(const FVector& ForwardX, const FVector& UpHint)
	{
		FVector Forward = ForwardX.Normalized();
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::ForwardVector;
		}

		FVector Up = UpHint.Normalized();
		if (Up.IsNearlyZero() || std::abs(Forward.Dot(Up)) > 0.98f)
		{
			Up = std::abs(Forward.Z) < 0.98f ? FVector::UpVector : FVector::RightVector;
		}

		FVector Right = Up.Cross(Forward).Normalized();
		if (Right.IsNearlyZero())
		{
			Up = std::abs(Forward.Z) < 0.98f ? FVector::UpVector : FVector::RightVector;
			Right = Up.Cross(Forward).Normalized();
		}

		Up = Forward.Cross(Right).Normalized();

		FMatrix RotationMatrix = FMatrix::Identity;
		RotationMatrix.M[0][0] = Forward.X;
		RotationMatrix.M[0][1] = Forward.Y;
		RotationMatrix.M[0][2] = Forward.Z;
		RotationMatrix.M[1][0] = Right.X;
		RotationMatrix.M[1][1] = Right.Y;
		RotationMatrix.M[1][2] = Right.Z;
		RotationMatrix.M[2][0] = Up.X;
		RotationMatrix.M[2][1] = Up.Y;
		RotationMatrix.M[2][2] = Up.Z;
		return RotationMatrix.ToQuat().GetNormalized();
	}
}

ACombatCharacter::ACombatCharacter()
{
	bAutoInputWASD = false;
	bAutoInputMouseLook = false;
	bAutoPossessPlayer = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void ACombatCharacter::BeginPlay()
{
	bDeathBloodDecalSpawned = false;
	PendingDeathBloodDecal = FPendingDeathBloodDecal();

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Capsule->SetCollisionObjectType(ECollisionChannel::Pawn);
		Capsule->SetKinematic(true);
	}

	// PhysicsAsset query bodies are created from the skeletal mesh.
	// Scene/prefab data often stores the mesh itself as WorldStatic/NoCollision
	// because the capsule owns gameplay collision, but precise sniper hit queries
	// search Pawn objects. Keep the mesh object type aligned before query bodies
	// are created so pre-placed allies and spawned enemies behave the same.
	if (USkeletalMeshComponent* Mesh = GetMesh())
	{
		Mesh->SetCollisionObjectType(ECollisionChannel::Pawn);
	}

	if (!SniperDamageReceiverComponent)
	{
		SniperDamageReceiverComponent = GetComponentByClass<USniperDamageReceiverComponent>();
	}
	if (!SniperDamageReceiverComponent)
	{
		SniperDamageReceiverComponent = AddComponent<USniperDamageReceiverComponent>();
	}

	if (!CombatGunfireSoundComponent)
	{
		CombatGunfireSoundComponent = GetComponentByClass<USoundComponent>();
	}
	if (!CombatGunfireSoundComponent)
	{
		CombatGunfireSoundComponent = AddComponent<USoundComponent>();
	}
	ConfigureCombatGunfireSound(CombatGunfireSoundComponent.Get());

	Super::BeginPlay();

	if (bEnablePersistentQueryBodies)
	{
		if (USkeletalMeshComponent* Mesh = GetMesh())
		{
			// Recreate the query bodies so stale filter data from serialized mesh
			// collision settings cannot keep precise hit proxies off the Pawn channel.
			Mesh->DisablePhysicsAssetQueryBodies();
			Mesh->EnablePhysicsAssetQueryBodies();
		}
	}

	if (USniperDamageReceiverComponent* DamageReceiver = GetSniperDamageReceiverComponent())
	{
		if (SniperKilledHandle.IsValid())
		{
			DamageReceiver->OnSniperKilled.Remove(SniperKilledHandle);
			SniperKilledHandle.Reset();
		}
		SniperKilledHandle = DamageReceiver->OnSniperKilled.AddUObject(this, &ACombatCharacter::HandleSniperKilled);
	}
}

void ACombatCharacter::EndPlay()
{
	if (USniperDamageReceiverComponent* DamageReceiver = GetSniperDamageReceiverComponent())
	{
		if (SniperKilledHandle.IsValid())
		{
			DamageReceiver->OnSniperKilled.Remove(SniperKilledHandle);
			SniperKilledHandle.Reset();
		}
	}

	Super::EndPlay();
}

void ACombatCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!PendingDeathBloodDecal.bActive)
	{
		return;
	}

	const bool bHasKillCamRecord =
		PendingDeathBloodDecal.BulletId != 0 &&
		ASniperKillCamDirector::HasBulletRecord(PendingDeathBloodDecal.BulletId);

	if (bHasKillCamRecord)
	{
		PendingDeathBloodDecal.bWaitForKillCamEnd = true;
	}

	if (PendingDeathBloodDecal.bWaitForKillCamEnd)
	{
		if (bHasKillCamRecord)
		{
			return;
		}

		SpawnDeathBloodDecal(PendingDeathBloodDecal.HitInfo);
		PendingDeathBloodDecal = FPendingDeathBloodDecal();
		return;
	}

	PendingDeathBloodDecal.DecisionGraceRemaining -= DeltaTime;
	if (PendingDeathBloodDecal.DecisionGraceRemaining > 0.0f)
	{
		return;
	}

	SpawnDeathBloodDecal(PendingDeathBloodDecal.HitInfo);
	PendingDeathBloodDecal = FPendingDeathBloodDecal();
}

void ACombatCharacter::InitDefaultComponents(const FString& SkeletalMeshFileName, const FString& ScriptFile)
{
	Super::InitDefaultComponents(SkeletalMeshFileName);

	LuaScriptComponent = AddComponent<ULuaScriptComponent>();
	if (!ScriptFile.empty())
	{
		LuaScriptComponent->SetScriptFile(ScriptFile);
	}

	CombatCoverAgentComponent = AddComponent<UCombatCoverAgentComponent>();
	CombatGunfireSoundComponent = AddComponent<USoundComponent>();
	ConfigureCombatGunfireSound(CombatGunfireSoundComponent.Get());
	SniperDamageReceiverComponent = AddComponent<USniperDamageReceiverComponent>();
}

void ACombatCharacter::PostDuplicate()
{
	Super::PostDuplicate();
	LuaScriptComponent = GetComponentByClass<ULuaScriptComponent>();
	CombatCoverAgentComponent = GetComponentByClass<UCombatCoverAgentComponent>();
	CombatGunfireSoundComponent = GetComponentByClass<USoundComponent>();
	SniperDamageReceiverComponent = GetComponentByClass<USniperDamageReceiverComponent>();
}

ULuaScriptComponent* ACombatCharacter::GetLuaScriptComponent() const
{
	if (!LuaScriptComponent)
	{
		LuaScriptComponent = GetComponentByClass<ULuaScriptComponent>();
	}
	return LuaScriptComponent;
}

UCombatCoverAgentComponent* ACombatCharacter::GetCombatCoverAgentComponent() const
{
	if (!CombatCoverAgentComponent)
	{
		CombatCoverAgentComponent = GetComponentByClass<UCombatCoverAgentComponent>();
	}
	return CombatCoverAgentComponent;
}

USoundComponent* ACombatCharacter::GetCombatGunfireSoundComponent() const
{
	if (!CombatGunfireSoundComponent)
	{
		CombatGunfireSoundComponent = GetComponentByClass<USoundComponent>();
	}
	ConfigureCombatGunfireSound(CombatGunfireSoundComponent.Get());
	return CombatGunfireSoundComponent.Get();
}

USniperDamageReceiverComponent* ACombatCharacter::GetSniperDamageReceiverComponent() const
{
	if (!SniperDamageReceiverComponent)
	{
		SniperDamageReceiverComponent = GetComponentByClass<USniperDamageReceiverComponent>();
	}
	return SniperDamageReceiverComponent;
}

void ACombatCharacter::ConfigureCombatGunfireSound(USoundComponent* Sound) const
{
	if (!Sound)
	{
		return;
	}

	Sound->SetSoundPath(CombatGunfireSoundPath);
	Sound->SetVolume(CombatGunfireVolume);
	Sound->SetPitch(1.0f);
	Sound->SetLooping(false);
	Sound->SetPlayOnBeginPlay(false);
	Sound->SetSpatialized(true);
	Sound->Set3DMinMaxDistance(CombatGunfireMinDistance, CombatGunfireMaxDistance);
	if (USceneComponent* Root = GetRootComponent())
	{
		Sound->SetParent(Root);
	}
	Sound->SetRelativeLocation(FVector::ZeroVector);
	Sound->SetRelativeRotation(FRotator::ZeroRotator);
	Sound->SetRelativeScale(FVector::OneVector);
}

void ACombatCharacter::HandleSniperKilled(const FSniperHitInfo& HitInfo)
{
	if (bDeathBloodDecalSpawned)
	{
		return;
	}

	bDeathBloodDecalSpawned = true;
	if (HitInfo.BulletId == 0)
	{
		SpawnDeathBloodDecal(HitInfo);
		return;
	}

	PendingDeathBloodDecal.bActive = true;
	PendingDeathBloodDecal.bWaitForKillCamEnd = ASniperKillCamDirector::HasBulletRecord(HitInfo.BulletId);
	PendingDeathBloodDecal.DecisionGraceRemaining = DeathBloodDecalKillCamDecisionGrace;
	PendingDeathBloodDecal.BulletId = HitInfo.BulletId;
	PendingDeathBloodDecal.HitInfo = HitInfo;
}

void ACombatCharacter::SpawnDeathBloodDecal(const FSniperHitInfo& HitInfo) const
{
	if (!bSpawnDeathBloodDecal)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !GEngine)
	{
		return;
	}

	UMaterial* BaseMaterial = nullptr;
	if (!DeathBloodDecalMaterialPath.empty() && DeathBloodDecalMaterialPath != "None")
	{
		BaseMaterial = FMaterialManager::Get().GetOrCreateMaterial(DeathBloodDecalMaterialPath);
	}
	if (!BaseMaterial)
	{
		BaseMaterial = FMaterialManager::Get().GetOrCreateMaterial(DefaultDeathBloodDecalMaterialPath);
	}
	if (!BaseMaterial)
	{
		return;
	}

	UMaterial* DecalMaterial = BaseMaterial;
	UMaterialInstanceDynamic* DynamicMaterial =
		UMaterialInstanceDynamic::Create(BaseMaterial, const_cast<ACombatCharacter*>(this), "CombatCharacter_BloodDecalMID");
	if (DynamicMaterial)
	{
		if (!DeathBloodDecalTexturePath.empty() && DeathBloodDecalTexturePath != "None")
		{
			if (UTexture2D* BloodTexture = UTexture2D::LoadFromFile(
				DeathBloodDecalTexturePath,
				GEngine->GetRenderer().GetFD3DDevice().GetDevice(),
				ETextureColorSpace::SRGB))
			{
				DynamicMaterial->SetTextureParameterValue("DiffuseTexture", BloodTexture);
			}
		}
		DecalMaterial = DynamicMaterial;
	}

	const FVector TraceAnchor =
		!HitInfo.HitLocation.IsNearlyZero()
		? HitInfo.HitLocation
		: GetActorLocation();
	const float TraceDistance = (std::max)(0.1f, DeathBloodDecalGroundTraceUp + DeathBloodDecalGroundTraceDown);

	FHitResult GroundHit{};
	FVector DecalNormal = FVector::UpVector;
	FVector DecalLocation = GetActorLocation();
	auto TryProjectToGround = [&](const FVector& Anchor) -> bool
	{
		const FVector TraceStart = Anchor + FVector::UpVector * (std::max)(0.0f, DeathBloodDecalGroundTraceUp);
		if (!World->PhysicsRaycastByObjectTypes(
			TraceStart,
			FVector::DownVector,
			TraceDistance,
			GroundHit,
			ObjectTypeBit(ECollisionChannel::WorldStatic),
			this))
		{
			return false;
		}

		DecalLocation = GroundHit.WorldHitLocation;
		if (!GroundHit.WorldNormal.IsNearlyZero())
		{
			DecalNormal = GroundHit.WorldNormal.Normalized();
		}
		return true;
	};

	if (!TryProjectToGround(TraceAnchor))
	{
		TryProjectToGround(GetActorLocation());
	}

	const float DecalDepth = (std::max)(0.01f, DeathBloodDecalDepth);
	const float DecalSize = (std::max)(0.1f, DeathBloodDecalSize);
	const FVector DecalCenter =
		DecalLocation +
		DecalNormal * (DeathBloodDecalSurfaceOffset - DecalDepth * 0.5f);
	const FVector UpHint = GetActorForward().IsNearlyZero() ? FVector::ForwardVector : GetActorForward();

	ADecalActor* DecalActor = World->SpawnActor<ADecalActor>();
	if (!DecalActor)
	{
		return;
	}

	DecalActor->SetFName(FName("DeathBlood_Decal"));
	DecalActor->InitRuntimeDecal(DecalMaterial);
	DecalActor->SetActorLocation(DecalCenter);
	DecalActor->SetActorRotation(MakeRotationWithForwardX(DecalNormal * -1.0f, UpHint).ToRotator());
	DecalActor->SetActorScale(FVector(DecalDepth, DecalSize, DecalSize));
	DecalActor->SetLifetimeSeconds((std::max)(0.1f, DeathBloodDecalLifetime));
}
