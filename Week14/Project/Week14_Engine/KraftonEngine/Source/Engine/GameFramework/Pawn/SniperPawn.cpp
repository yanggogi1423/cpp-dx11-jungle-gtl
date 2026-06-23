#include "GameFramework/Pawn/SniperPawn.h"

#include "Animation/Sequence/AnimSequenceBase.h"
#include "Audio/AudioManager.h"
#include "Component/Input/ActionComponent.h"
#include "Component/Camera/CameraComponent.h"
#include "Component/Gameplay/BallisticBulletManagerComponent.h"
#include "Component/Gameplay/SniperWeaponComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/Actor/BoxActor.h"
#include "GameFramework/Actor/SniperKillCamDirector.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/World.h"
#include "Input/InputSystem.h"
#include "Math/MathUtils.h"
#include "Profiling/Time/Timer.h"
#include "Runtime/Engine.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>

namespace
{
	float ClampSniperPitch(float Value, float MinPitch, float MaxPitch)
	{
		if (MinPitch > MaxPitch)
		{
			std::swap(MinPitch, MaxPitch);
		}

		return std::clamp(Value, MinPitch, MaxPitch);
	}

	float ExponentialInterpTo(float Current, float Target, float DeltaTime, float Speed)
	{
		if (DeltaTime <= 0.0f || Speed <= 0.0f)
		{
			return Current;
		}

		const float Alpha = 1.0f - std::exp(-Speed * DeltaTime);
		return Current + (Target - Current) * Alpha;
	}

	float ComputeScopeAlpha(const FScopeState& ScopeState)
	{
		const float ScopeRange = ScopeState.NormalFOV - ScopeState.ScopedFOV;
		if (std::abs(ScopeRange) <= FMath::Epsilon)
		{
			return 0.0f;
		}

		return FMath::Clamp((ScopeState.NormalFOV - ScopeState.CurrentFOV) / ScopeRange, 0.0f, 1.0f);
	}

	float RandomRange(float MinValue, float MaxValue)
	{
		const float Alpha = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
		return MinValue + (MaxValue - MinValue) * Alpha;
	}

	float GetSniperInputTimeScale()
	{
		if (!GEngine || !GEngine->GetTimer())
		{
			return 1.0f;
		}

		return FMath::Clamp(GEngine->GetTimer()->GetTimeDilation(), 0.01f, 1.0f);
	}

	bool IsSniperKillCamPlaying(const AActor* Actor)
	{
		return Actor && ASniperKillCamDirector::IsPlayingInWorld(Actor->GetWorld());
	}

	constexpr const char* SniperMovementWallActorNames[] =
	{
		"Wall_0",
		"Wall_1",
		"Wall_2",
		"Wall_3"
	};

	bool IsSniperMovementWallName(const FString& ActorName)
	{
		for (const char* WallName : SniperMovementWallActorNames)
		{
			if (ActorName == WallName)
			{
				return true;
			}
		}

		return false;
	}
}

ASniperPawn::ASniperPawn()
{
	bNeedsTick = true;
	bUseControllerRotationPitch = true;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;
}

void ASniperPawn::EnsureWeaponVisualComponents()
{
	CacheComponentReferences();
	InitDefaultComponents();
	CacheComponentReferences();
	SyncWeaponVisualComponent();
}

void ASniperPawn::BeginPlay()
{
	EnsureWeaponVisualComponents();
	SyncSniperRuntimeState();
	ConfigureSniperMovementCapsule();
	ConfigureSniperMovementWalls();

	APawn::BeginPlay();
}

void ASniperPawn::EndPlay()
{
	if (APlayerController* PC = GetController())
	{
		if (APlayerCameraManager* CameraManager = PC->GetPlayerCameraManager())
		{
			CameraManager->SetScopeZoomEnabled(false);
			CameraManager->ClearScopeLens();
		}
	}

	AActor::EndPlay();
}

void ASniperPawn::PostDuplicate()
{
	APawn::PostDuplicate();
	EnsureWeaponVisualComponents();
}

void ASniperPawn::OnPostLoad(FArchive& Ar)
{
	APawn::OnPostLoad(Ar);
	EnsureWeaponVisualComponents();
}

void ASniperPawn::PreGetEditableProperties()
{
	EnsureWeaponVisualComponents();
	APawn::PreGetEditableProperties();
}

void ASniperPawn::SetupInputComponent()
{
	APawn::SetupInputComponent();

	if (!InputComponent)
	{
		return;
	}

	InputComponent->AddMouseAxisMapping("SniperTurn", EInputAxisSourceType::MouseX, 1.0f);
	InputComponent->AddMouseAxisMapping("SniperLookUp", EInputAxisSourceType::MouseY, 1.0f);
	InputComponent->AddMouseAxisMapping("SniperScopeZoom", EInputAxisSourceType::MouseWheel, 1.0f);
	InputComponent->AddAxisMapping("SniperMoveForward", 'W', 1.0f);
	InputComponent->AddAxisMapping("SniperMoveForward", 'S', -1.0f);
	InputComponent->AddAxisMapping("SniperMoveRight", 'D', 1.0f);
	InputComponent->AddAxisMapping("SniperMoveRight", 'A', -1.0f);
	InputComponent->AddGamepadAxisMapping("SniperGamepadTurn", EInputAxisSourceType::GamepadLeftStickX, 1.0f);
	InputComponent->AddGamepadAxisMapping("SniperGamepadLookUp", EInputAxisSourceType::GamepadLeftStickY, 1.0f);
	InputComponent->AddGamepadAxisMapping("SniperMoveForward", EInputAxisSourceType::GamepadRightStickY, 1.0f);
	InputComponent->AddGamepadAxisMapping("SniperMoveRight", EInputAxisSourceType::GamepadRightStickX, 1.0f);
	InputComponent->AddGamepadAxisMapping("SniperGamepadScope", EInputAxisSourceType::GamepadLeftTrigger, 1.0f);
	InputComponent->AddGamepadAxisMapping("SniperGamepadFire", EInputAxisSourceType::GamepadRightTrigger, 1.0f);
	InputComponent->AddActionMapping("SniperFire", "LeftMouseButton");
	InputComponent->AddActionMapping("SniperScope", "RightMouseButton");
	InputComponent->AddActionMapping("SniperHoldBreath", "Shift");
	InputComponent->AddActionMapping("SniperHoldBreath", "LeftShift");
	InputComponent->AddActionMapping("SniperHoldBreath", "RightShift");
	InputComponent->AddActionMapping("SniperSwitchAmmoNormal", "1");
	InputComponent->AddActionMapping("SniperSwitchAmmoAntiMaterial", "2");
	InputComponent->AddActionMapping("SniperReload", "R");
	InputComponent->AddGamepadActionMapping("SniperGamepadHoldBreath", EGamepadButton::LeftShoulder);
	InputComponent->AddGamepadActionMapping("SniperGamepadSwitchAmmoNormal", EGamepadButton::DPadLeft);
	InputComponent->AddGamepadActionMapping("SniperGamepadSwitchAmmoAntiMaterial", EGamepadButton::DPadRight);
	InputComponent->AddGamepadActionMapping("SniperGamepadZoomIn", EGamepadButton::DPadUp);
	InputComponent->AddGamepadActionMapping("SniperGamepadZoomOut", EGamepadButton::DPadDown);
	InputComponent->AddGamepadActionMapping("SniperReload", EGamepadButton::FaceLeft);

	InputComponent->BindAxis("SniperTurn", [this](float Value)
	{
		HandleTurnInput(Value);
	});

	InputComponent->BindAxis("SniperLookUp", [this](float Value)
	{
		HandleLookUpInput(Value);
	});

	InputComponent->BindAxis("SniperGamepadTurn", [this](float Value)
	{
		HandleGamepadTurnInput(Value);
	});

	InputComponent->BindAxis("SniperGamepadLookUp", [this](float Value)
	{
		HandleGamepadLookUpInput(Value);
	});

	InputComponent->BindAxis("SniperMoveForward", [this](float Value)
	{
		HandleMoveForwardInput(Value);
	});

	InputComponent->BindAxis("SniperMoveRight", [this](float Value)
	{
		HandleMoveRightInput(Value);
	});

	InputComponent->BindAxis("SniperScopeZoom", [this](float Value)
	{
		HandleScopeZoomAxis(Value);
	});

	InputComponent->BindAxis("SniperGamepadScope", [this](float Value)
	{
		HandleGamepadScopeAxis(Value);
	});

	InputComponent->BindAxis("SniperGamepadFire", [this](float Value)
	{
		HandleGamepadFireAxis(Value);
	});

	InputComponent->BindAction("SniperScope", EInputEvent::Pressed, [this]()
	{
		HandleScopePressed();
	});

	InputComponent->BindAction("SniperFire", EInputEvent::Pressed, [this]()
	{
		HandleFirePressed();
	});

	InputComponent->BindAction("SniperHoldBreath", EInputEvent::Pressed, [this]()
	{
		HandleHoldBreathPressed();
	});

	InputComponent->BindAction("SniperHoldBreath", EInputEvent::Released, [this]()
	{
		HandleHoldBreathReleased();
	});

	InputComponent->BindAction("SniperGamepadHoldBreath", EInputEvent::Pressed, [this]()
	{
		HandleGamepadHoldBreathPressed();
	});

	InputComponent->BindAction("SniperGamepadHoldBreath", EInputEvent::Released, [this]()
	{
		HandleGamepadHoldBreathReleased();
	});

	InputComponent->BindAction("SniperSwitchAmmoNormal", EInputEvent::Pressed, [this]()
	{
		HandleSwitchAmmoNormalPressed();
	});

	InputComponent->BindAction("SniperSwitchAmmoAntiMaterial", EInputEvent::Pressed, [this]()
	{
		HandleSwitchAmmoAntiMaterialPressed();
	});

	InputComponent->BindAction("SniperGamepadSwitchAmmoNormal", EInputEvent::Pressed, [this]()
	{
		HandleSwitchAmmoNormalPressed();
	});

	InputComponent->BindAction("SniperGamepadSwitchAmmoAntiMaterial", EInputEvent::Pressed, [this]()
	{
		HandleSwitchAmmoAntiMaterialPressed();
	});

	InputComponent->BindAction("SniperGamepadZoomIn", EInputEvent::Pressed, [this]()
	{
		HandleScopeZoomInPressed();
	});

	InputComponent->BindAction("SniperGamepadZoomOut", EInputEvent::Pressed, [this]()
	{
		HandleScopeZoomOutPressed();
	});

	InputComponent->BindAction("SniperReload", EInputEvent::Pressed, [this]()
	{
		HandleReloadPressed();
	});

	InputComponent->BindAction("SniperScope", EInputEvent::Released, [this]()
	{
		HandleScopeReleased();
	});
}

void ASniperPawn::ProcessPlayerInput(const FInputSystemSnapshot& Snapshot, float DeltaTime)
{
	CachedInputDeltaTime = DeltaTime > 0.0f ? DeltaTime : (1.0f / 60.0f);
	APawn::ProcessPlayerInput(Snapshot, DeltaTime);
}

void ASniperPawn::Tick(float DeltaTime)
{
	APawn::Tick(DeltaTime);

	if (IsSniperKillCamPlaying(this))
	{
		ForceScopeReleased();
	}

	UpdateScopeState(DeltaTime);
	UpdateHoldBreathState(DeltaTime);
	UpdateAimSwayState(DeltaTime);
	UpdateRecoilState(DeltaTime);
	UpdateWeaponHandsReloadAnimation();
	UpdateBulletFlightSlomo(DeltaTime);
	ApplySniperMovement(DeltaTime);
	ApplySniperControlRotation();

	InputState.MouseDeltaX = 0.0f;
	InputState.MouseDeltaY = 0.0f;
}

void ASniperPawn::InitDefaultComponents()
{
	bool bCreatedWeaponHandsMesh = false;
	bool bCreatedWeaponVisual = false;

	if (!GetRootComponent())
	{
		SniperRoot = AddComponent<USceneComponent>();
		SetRootComponent(SniperRoot.Get());
	}
	else
	{
		SniperRoot = GetRootComponent();
	}

	if (!Camera)
	{
		Camera = AddComponent<UCameraComponent>();
		if (Camera)
		{
			Camera->AttachToComponent(GetRootComponent());
		}
	}

	if (!WeaponHandsMeshComponent && Camera)
	{
		WeaponHandsMeshComponent = AddComponent<USkeletalMeshComponent>();
		if (WeaponHandsMeshComponent)
		{
			WeaponHandsMeshComponent->AttachToComponent(Camera.Get());
			bCreatedWeaponHandsMesh = true;
		}
	}

	if (!WeaponVisualComponent)
	{
		if (WeaponHandsMeshComponent)
		{
			WeaponVisualComponent = AddComponent<UStaticMeshComponent>();
			if (WeaponVisualComponent)
			{
				WeaponVisualComponent->AttachToComponent(WeaponHandsMeshComponent.Get(), WeaponVisualSocketName);
				bCreatedWeaponVisual = true;
			}
		}
	}

	if (bCreatedWeaponHandsMesh && WeaponHandsMeshComponent)
	{
		WeaponHandsMeshComponent->SetRelativeLocation(WeaponHandsRelativeLocation);
		WeaponHandsMeshComponent->SetRelativeRotation(WeaponHandsRelativeRotation);
		WeaponHandsMeshComponent->SetRelativeScale(WeaponHandsRelativeScale);
	}

	if (bCreatedWeaponVisual && WeaponVisualComponent)
	{
		WeaponVisualComponent->SetAbsoluteScale(true);
		WeaponVisualComponent->SetRelativeLocation(WeaponVisualRelativeLocation);
		WeaponVisualComponent->SetRelativeRotation(WeaponVisualRelativeRotation);
		WeaponVisualComponent->SetRelativeScale(WeaponVisualRelativeScale);
	}

	if (!WeaponComponent)
	{
		WeaponComponent = AddComponent<USniperWeaponComponent>();
	}

	if (!BulletManagerComponent)
	{
		BulletManagerComponent = AddComponent<UBallisticBulletManagerComponent>();
	}

	if (!ActionComponent)
	{
		ActionComponent = AddComponent<UActionComponent>();
	}
}

void ASniperPawn::CacheComponentReferences()
{
	SniperRoot = GetRootComponent();
	Camera = GetComponentByClass<UCameraComponent>();
	WeaponHandsMeshComponent = nullptr;
	WeaponVisualComponent = nullptr;
	if (Camera)
	{
		for (USceneComponent* Child : Camera->GetChildren())
		{
			if (!WeaponHandsMeshComponent)
			{
				if (USkeletalMeshComponent* SkeletalMeshChild = Cast<USkeletalMeshComponent>(Child))
				{
					WeaponHandsMeshComponent = SkeletalMeshChild;
				}
			}
		}
	}
	if (WeaponHandsMeshComponent)
	{
		for (USceneComponent* Child : WeaponHandsMeshComponent->GetChildren())
		{
			if (UStaticMeshComponent* StaticMeshChild = Cast<UStaticMeshComponent>(Child))
			{
				WeaponVisualComponent = StaticMeshChild;
				break;
			}
		}
	}
	WeaponComponent = GetComponentByClass<USniperWeaponComponent>();
	BulletManagerComponent = GetComponentByClass<UBallisticBulletManagerComponent>();
	ActionComponent = GetComponentByClass<UActionComponent>();
}

void ASniperPawn::CacheInputSensitivityBases()
{
	if (bInputSensitivityBaseInitialized)
	{
		return;
	}

	BaseMouseSensitivity = (std::max)(MouseSensitivity, 0.0001f);
	BaseGamepadLookSensitivity = (std::max)(GamepadLookSensitivity, 0.0001f);
	bInputSensitivityBaseInitialized = true;
}

void ASniperPawn::SyncSniperRuntimeState()
{
	InputState = FSniperInputState{};
	CachedInputDeltaTime = 1.0f / 60.0f;
	bMouseScopeInputHeld = false;
	bGamepadScopeInputHeld = false;
	bKeyboardHoldBreathInputHeld = false;
	bGamepadHoldBreathInputHeld = false;
	bGamepadFireTriggerHeld = false;
	bBulletFlightSlomoActive = false;
	bWasWeaponReloading = false;
	bWeaponHandsReloadAnimationActive = false;
	SniperMoveForwardInput = 0.0f;
	SniperMoveRightInput = 0.0f;
	FootstepCooldownRemaining = 0.0f;
	bUseControllerRotationPitch = true;
	bUseControllerRotationYaw = true;
	CacheInputSensitivityBases();

	if (Camera)
	{
		ScopeState.NormalFOV = Camera->GetFOV();
	}

	ScopeState.MinZoomMagnification = (std::max)(ScopeState.MinZoomMagnification, 1.0f);
	ScopeState.MaxZoomMagnification = (std::max)(ScopeState.MaxZoomMagnification, ScopeState.MinZoomMagnification);
	ScopeState.ZoomStep = (std::max)(ScopeState.ZoomStep, 0.1f);
	ScopeState.ScopedSensitivity = (std::max)(ScopeState.ScopedSensitivity, 0.01f);
	ScopeState.MaxZoomScopedSensitivity = (std::max)(ScopeState.MaxZoomScopedSensitivity, 0.01f);
	GamepadLookSensitivity = (std::max)(GamepadLookSensitivity, 0.0f);
	GamepadTriggerPressThreshold = FMath::Clamp(GamepadTriggerPressThreshold, 0.01f, 1.0f);
	ScopeState.DefaultZoomMagnification = ClampScopeZoomMagnification(ScopeState.DefaultZoomMagnification);
	ScopeState.CurrentZoomMagnification = ScopeState.DefaultZoomMagnification;
	ScopeState.TargetZoomMagnification = ScopeState.DefaultZoomMagnification;
	ScopeState.ScopedFOV = ComputeScopedFOVForMagnification(ScopeState.CurrentZoomMagnification);

	ScopeState.bIsScoped = false;
	ScopeState.TargetFOV = ScopeState.NormalFOV;
	ScopeState.CurrentFOV = ScopeState.NormalFOV;
	ScopeState.CurrentSensitivity = ScopeState.NormalSensitivity;
	AimSwayState.Time = 0.0f;
	AimSwayState.CurrentSwayPitch = 0.0f;
	AimSwayState.CurrentSwayYaw = 0.0f;
	AimSwayState.BreathMultiplier = 1.0f;
	AimSwayState.HoldBreathGauge = AimSwayState.MaxHoldBreathGauge;
	AimSwayState.HoldBreathCooldownRemaining = 0.0f;
	AimSwayState.bForcedRecovery = false;
	AimSwayState.bRequireHoldBreathRelease = false;
	AimSwayState.bWasHoldBreathActive = false;
	RecoilState = FRecoilState{};

	if (Camera)
	{
		Camera->SetFOV(ScopeState.NormalFOV);
	}

	if (APlayerController* PC = GetController())
	{
		if (APlayerCameraManager* CameraManager = PC->GetPlayerCameraManager())
		{
			CameraManager->SetScopeLensProfile(
				ScopeLensRadius,
				ScopeLensOuterBlurRadius,
				ScopeState.CurrentFOV,
				ScopeLensFeather,
				ScopeLensEdgeBlurRadius,
				ScopeLensIntensity,
				ScopeState.CurrentSensitivity,
				ScopeLensBlendTime,
				0.5f,
				0.5f,
				ScopeLensCenterOffsetX,
				ScopeLensCenterOffsetY);
			CameraManager->SetScopeZoomEnabled(false);
		}
	}

	FRotator Control = GetControlRotation();
	Control.Pitch = ClampSniperPitch(Control.Pitch, MinCameraPitch, MaxCameraPitch);
	Control.Roll = 0.0f;
	SetControlRotation(Control);
	ApplySniperControlRotation();
}

void ASniperPawn::SyncWeaponVisualComponent()
{
	USkeletalMeshComponent* WeaponHandsMesh = WeaponHandsMeshComponent.Get();
	UStaticMeshComponent* WeaponVisual = WeaponVisualComponent.Get();
	UCameraComponent* SniperCamera = Camera.Get();
	if (!WeaponHandsMesh || !WeaponVisual || !SniperCamera)
	{
		return;
	}

	const FString HandsMeshPath = WeaponHandsMeshPath.ToString();
	const bool bHasHandsMeshPath = !HandsMeshPath.empty() && HandsMeshPath != "None";
	if (WeaponHandsMesh->GetParent() != SniperCamera)
	{
		WeaponHandsMesh->AttachToComponent(SniperCamera);
	}

	WeaponHandsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponHandsMesh->SetGenerateOverlapEvents(false);
	WeaponHandsMesh->SetCastShadow(false);

	const bool bEnableHandsAndWeapon = bEnableWeaponVisual && bEnableWeaponHandsMesh && bHasHandsMeshPath;
	if (bEnableHandsAndWeapon)
	{
		WeaponHandsMesh->SetSkeletalMeshByPath(HandsMeshPath);
		PlayWeaponHandsIdleAnimation();
	}
	else
	{
		WeaponHandsMesh->SetSkeletalMeshByPath("None");
		WeaponHandsMesh->SetVisibility(false);
		WeaponVisual->ClearStaticMesh();
		WeaponVisual->SetVisibility(false);
		return;
	}

	if (WeaponVisual->GetParent() != WeaponHandsMesh || WeaponVisual->GetAttachSocketName() != WeaponVisualSocketName)
	{
		WeaponVisual->AttachToComponent(WeaponHandsMesh, WeaponVisualSocketName);
	}

	WeaponVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponVisual->SetGenerateOverlapEvents(false);
	WeaponVisual->SetCastShadow(false);
	WeaponVisual->SetAbsoluteScale(true);

	const FString MeshPath = WeaponVisualMeshPath.ToString();
	const bool bHasMeshPath = !MeshPath.empty() && MeshPath != "None";
	if (bEnableWeaponVisual && bHasMeshPath)
	{
		WeaponVisual->SetStaticMeshByPath(MeshPath);
	}
	else
	{
		WeaponVisual->ClearStaticMesh();
	}

	UpdateWeaponVisualScopeVisibility();
}

bool ASniperPawn::PlayWeaponHandsAnimation(const FString& AnimationPath, bool bLooping)
{
	USkeletalMeshComponent* WeaponHandsMesh = WeaponHandsMeshComponent.Get();
	if (!WeaponHandsMesh || AnimationPath.empty() || AnimationPath == "None")
	{
		return false;
	}

	return WeaponHandsMesh->PlayAnimationByPath(AnimationPath, bLooping);
}

bool ASniperPawn::PlayWeaponHandsAnimationSyncedToDuration(const FString& AnimationPath, float TargetDuration)
{
	USkeletalMeshComponent* WeaponHandsMesh = WeaponHandsMeshComponent.Get();
	if (!PlayWeaponHandsAnimation(AnimationPath, false))
	{
		return false;
	}

	WeaponHandsMesh->SetPlayRate(1.0f);

	if (TargetDuration <= 0.0f)
	{
		return true;
	}

	const UAnimSequenceBase* Animation = WeaponHandsMesh->GetAnimation();
	if (!Animation)
	{
		return true;
	}

	const float AnimationLength = Animation->GetPlayLength();
	if (AnimationLength <= 0.0f)
	{
		return true;
	}

	WeaponHandsMesh->SetPlayRate(AnimationLength / TargetDuration);
	return true;
}

void ASniperPawn::PlayWeaponHandsIdleAnimation()
{
	if (USkeletalMeshComponent* WeaponHandsMesh = WeaponHandsMeshComponent.Get())
	{
		WeaponHandsMesh->SetPlayRate(1.0f);
	}
	PlayWeaponHandsAnimation(WeaponHandsIdleAnimationPath, true);
}

void ASniperPawn::UpdateWeaponHandsReloadAnimation()
{
	const bool bReloading = IsReloading();
	if (bReloading && !bWasWeaponReloading)
	{
		const USniperWeaponComponent* SniperWeapon = WeaponComponent.Get();
		const float ReloadDuration = SniperWeapon ? SniperWeapon->GetReloadDuration() : GetReloadRemaining();
		bWeaponHandsReloadAnimationActive = PlayWeaponHandsAnimationSyncedToDuration(WeaponHandsReloadAnimationPath, ReloadDuration);
	}
	else if (!bReloading && bWasWeaponReloading)
	{
		if (bWeaponHandsReloadAnimationActive)
		{
			PlayWeaponHandsIdleAnimation();
		}
		bWeaponHandsReloadAnimationActive = false;
	}

	bWasWeaponReloading = bReloading;
}

void ASniperPawn::UpdateWeaponVisualScopeVisibility()
{
	USkeletalMeshComponent* WeaponHandsMesh = WeaponHandsMeshComponent.Get();
	UStaticMeshComponent* WeaponVisual = WeaponVisualComponent.Get();

	const FString HandsMeshPath = WeaponHandsMeshPath.ToString();
	const FString MeshPath = WeaponVisualMeshPath.ToString();
	const bool bHasHandsMeshPath = !HandsMeshPath.empty() && HandsMeshPath != "None";
	const bool bHasWeaponMeshPath = !MeshPath.empty() && MeshPath != "None";
	const bool bShowWeaponHands = bEnableWeaponVisual && bEnableWeaponHandsMesh && bHasHandsMeshPath && !ScopeState.bIsScoped;
	const bool bShowWeaponVisual = bShowWeaponHands && bHasWeaponMeshPath;

	if (WeaponHandsMesh)
	{
		WeaponHandsMesh->SetVisibility(bShowWeaponHands);
	}
	if (WeaponVisual)
	{
		WeaponVisual->SetVisibility(bShowWeaponVisual);
	}
}

void ASniperPawn::UpdateScopeState(float DeltaTime)
{
	if (!CanEnterScope())
	{
		InputState.bScopeHeld = false;
		InputState.bHoldBreathHeld = false;
	}

	ScopeState.bIsScoped = InputState.bScopeHeld && CanEnterScope();
	ScopeState.TargetZoomMagnification = ClampScopeZoomMagnification(ScopeState.TargetZoomMagnification);
	ScopeState.CurrentZoomMagnification = ScopeState.TargetZoomMagnification;
	ScopeState.ScopedFOV = ComputeScopedFOVForMagnification(ScopeState.CurrentZoomMagnification);
	const float ScopedSensitivityForCurrentZoom =
		ComputeScopedSensitivityForMagnification(ScopeState.CurrentZoomMagnification);
	ScopeState.TargetFOV = ScopeState.bIsScoped ? ScopeState.ScopedFOV : ScopeState.NormalFOV;
	ScopeState.CurrentFOV = ExponentialInterpTo(
		ScopeState.CurrentFOV,
		ScopeState.TargetFOV,
		DeltaTime,
		ScopeState.ScopeBlendSpeed);

	const float ScopeAlpha = ComputeScopeAlpha(ScopeState);

	ScopeState.CurrentSensitivity = FMath::Lerp(
		ScopeState.NormalSensitivity,
		ScopedSensitivityForCurrentZoom,
		ScopeAlpha);

	if (Camera)
	{
		Camera->SetFOV(ScopeState.NormalFOV);
	}

	if (APlayerController* PC = GetController())
	{
		if (APlayerCameraManager* CameraManager = PC->GetPlayerCameraManager())
		{
			CameraManager->SetScopeLensProfile(
				ScopeLensRadius,
				ScopeLensOuterBlurRadius,
				ScopeState.CurrentFOV,
				ScopeLensFeather,
				ScopeLensEdgeBlurRadius,
				ScopeLensIntensity,
				ScopeState.CurrentSensitivity,
				ScopeLensBlendTime,
				0.5f,
				0.5f,
				ScopeLensCenterOffsetX,
				ScopeLensCenterOffsetY);
			CameraManager->SetScopeZoomEnabled(ScopeState.bIsScoped);
		}
	}

	UpdateWeaponVisualScopeVisibility();
}

void ASniperPawn::UpdateHoldBreathState(float DeltaTime)
{
	if (!InputState.bHoldBreathHeld)
	{
		AimSwayState.bRequireHoldBreathRelease = false;
	}

	if (AimSwayState.HoldBreathCooldownRemaining > 0.0f)
	{
		AimSwayState.HoldBreathCooldownRemaining = (std::max)(
			0.0f,
			AimSwayState.HoldBreathCooldownRemaining - DeltaTime);
	}

	const bool bCanHoldBreath =
		InputState.bHoldBreathHeld &&
		ScopeState.bIsScoped &&
		!AimSwayState.bForcedRecovery &&
		!AimSwayState.bRequireHoldBreathRelease &&
		AimSwayState.HoldBreathCooldownRemaining <= 0.0f &&
		AimSwayState.HoldBreathGauge > 0.0f;

	if (AimSwayState.bWasHoldBreathActive && !bCanHoldBreath && !AimSwayState.bForcedRecovery)
	{
		AimSwayState.HoldBreathCooldownRemaining = (std::max)(HoldBreathReentryDelay, 0.0f);
	}

	if (bCanHoldBreath)
	{
		AimSwayState.HoldBreathGauge = FMath::Clamp(
			AimSwayState.HoldBreathGauge - AimSwayState.HoldBreathConsumeSpeed * DeltaTime,
			0.0f,
			AimSwayState.MaxHoldBreathGauge);

		if (AimSwayState.HoldBreathGauge <= 0.0f)
		{
			AimSwayState.bForcedRecovery = true;
			AimSwayState.bRequireHoldBreathRelease = true;
		}
	}
	else
	{
		AimSwayState.HoldBreathGauge = FMath::Clamp(
			AimSwayState.HoldBreathGauge + AimSwayState.HoldBreathRecoverSpeed * DeltaTime,
			0.0f,
			AimSwayState.MaxHoldBreathGauge);
	}

	float TargetBreathMultiplier = 1.0f;
	if (AimSwayState.bForcedRecovery)
	{
		const float RecoveryRatio = AimSwayState.MaxHoldBreathGauge > 0.0f
			? FMath::Clamp(AimSwayState.HoldBreathGauge / AimSwayState.MaxHoldBreathGauge, 0.0f, 1.0f)
			: 1.0f;
		TargetBreathMultiplier = FMath::Lerp(ExhaustedSwayMultiplier, 1.0f, RecoveryRatio);
	}
	else if (bCanHoldBreath)
	{
		TargetBreathMultiplier = HoldBreathSwayMultiplier;
	}

	if (HoldBreathSwayBlendSpeed <= 0.0f)
	{
		AimSwayState.BreathMultiplier = TargetBreathMultiplier;
	}
	else
	{
		AimSwayState.BreathMultiplier = ExponentialInterpTo(
			AimSwayState.BreathMultiplier,
			TargetBreathMultiplier,
			DeltaTime,
			HoldBreathSwayBlendSpeed);
	}

	if (AimSwayState.bForcedRecovery &&
		AimSwayState.HoldBreathGauge >= AimSwayState.MaxHoldBreathGauge - 0.001f &&
		std::abs(AimSwayState.BreathMultiplier - 1.0f) <= 0.05f)
	{
		AimSwayState.bForcedRecovery = false;
	}

	AimSwayState.bWasHoldBreathActive = bCanHoldBreath;
}

void ASniperPawn::UpdateAimSwayState(float DeltaTime)
{
	AimSwayState.Time += DeltaTime;

	const float ScopeAlpha = ComputeScopeAlpha(ScopeState);
	const float BaseAmplitude = FMath::Lerp(
		AimSwayState.BaseSwayAmount * FMath::RadToDeg,
		AimSwayState.ScopedSwayAmount * FMath::RadToDeg,
		ScopeAlpha);
	const float SwayAmplitude = BaseAmplitude * AimSwayState.BreathMultiplier;

	AimSwayState.CurrentSwayPitch = std::sin(AimSwayState.Time * SwayPitchFrequency) * SwayAmplitude;
	AimSwayState.CurrentSwayYaw = std::cos(AimSwayState.Time * SwayYawFrequency) * SwayAmplitude * 0.85f;
}

void ASniperPawn::UpdateRecoilState(float DeltaTime)
{
	RecoilState.CurrentRecoilPitch = ExponentialInterpTo(
		RecoilState.CurrentRecoilPitch,
		0.0f,
		DeltaTime,
		RecoilState.RecoilRecoverSpeed);
	RecoilState.CurrentRecoilYaw = ExponentialInterpTo(
		RecoilState.CurrentRecoilYaw,
		0.0f,
		DeltaTime,
		RecoilState.RecoilRecoverSpeed);
}

void ASniperPawn::ApplySniperMovement(float DeltaTime)
{
	if (!bEnableWASDMovement || DeltaTime <= 0.0f || SniperMoveSpeed <= 0.0f || IsSniperKillCamPlaying(this))
	{
		return;
	}

	FVector MoveInput(SniperMoveForwardInput, SniperMoveRightInput, 0.0f);
	if (MoveInput.IsNearlyZero())
	{
		FootstepCooldownRemaining = 0.0f;
		return;
	}

	if (MoveInput.Length() > 1.0f)
	{
		MoveInput.Normalize();
	}

	const FRotator YawOnly(0.0f, GetControlRotation().Yaw, 0.0f);
	FVector Forward = YawOnly.GetForwardVector();
	FVector Right = YawOnly.GetRightVector();
	Forward.Z = 0.0f;
	Right.Z = 0.0f;
	Forward.Normalize();
	Right.Normalize();

	const FVector MoveDirection = (Forward * MoveInput.X + Right * MoveInput.Y).Normalized();
	if (MoveDirection.IsNearlyZero())
	{
		return;
	}

	const FVector MoveDelta = MoveDirection * SniperMoveSpeed * DeltaTime;
	if (!TryMoveSniperWithCollision(MoveDelta))
	{
		return;
	}

	FootstepCooldownRemaining -= DeltaTime;
	if (FootstepCooldownRemaining <= 0.0f)
	{
		PlaySniperFootstep();
		FootstepCooldownRemaining = (std::max)(FootstepInterval, 0.05f);
	}
}

void ASniperPawn::UpdateBulletFlightSlomo(float DeltaTime)
{
	UActionComponent* SniperAction = ActionComponent.Get();
	UBallisticBulletManagerComponent* BulletManager = BulletManagerComponent.Get();
	if (!SniperAction)
	{
		bBulletFlightSlomoActive = false;
		return;
	}

	if (!bEnableBulletFlightSlomo || IsSniperKillCamPlaying(this) || !BulletManager)
	{
		if (bBulletFlightSlomoActive)
		{
			SniperAction->StopSlomo();
			bBulletFlightSlomoActive = false;
		}
		return;
	}

	if (BulletManager->GetAliveBulletCount() > 0)
	{
		const float RefreshDuration = (std::max)(BulletFlightSlomoDuration, (std::max)(DeltaTime * 2.0f, 0.05f));
		SniperAction->Slomo(RefreshDuration, BulletFlightSlomoTimeDilation);
		bBulletFlightSlomoActive = true;
		return;
	}

	if (bBulletFlightSlomoActive)
	{
		SniperAction->StopSlomo();
		bBulletFlightSlomoActive = false;
	}
}

void ASniperPawn::ApplySniperControlRotation()
{
	USceneComponent* Root = GetRootComponent();
	if (!Root)
	{
		return;
	}

	FRotator AppliedRotation = Root->GetRelativeRotation();
	const FRotator EffectiveRotation = BuildEffectiveAimRotation();

	if (bUseControllerRotationYaw)
	{
		AppliedRotation.Yaw = EffectiveRotation.Yaw;
	}
	if (bUseControllerRotationPitch)
	{
		AppliedRotation.Pitch = EffectiveRotation.Pitch;
	}
	if (bUseControllerRotationRoll)
	{
		AppliedRotation.Roll = EffectiveRotation.Roll;
	}

	Root->SetRelativeRotation(AppliedRotation);
}

void ASniperPawn::ConfigureSniperMovementCapsule()
{
	UCapsuleComponent* Capsule = GetSniperMovementCapsule();
	if (!Capsule)
	{
		return;
	}

	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Capsule->SetCollisionObjectType(ECollisionChannel::Pawn);
	Capsule->SetCollisionResponseToChannel(ECollisionChannel::WorldStatic, ECollisionResponse::Block);
	Capsule->SetCollisionResponseToChannel(ECollisionChannel::WorldDynamic, ECollisionResponse::Block);
	Capsule->SetKinematic(true);
	Capsule->SetSimulatePhysics(false);
}

void ASniperPawn::ConfigureSniperMovementWalls()
{
	for (const char* WallName : SniperMovementWallActorNames)
	{
		UBoxComponent* WallBox = FindSniperMovementWallBox(WallName);
		if (!WallBox)
		{
			continue;
		}

		WallBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		WallBox->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
		WallBox->SetCollisionResponseToAllChannels(ECollisionResponse::Block);
		WallBox->SetCollisionResponseToChannel(ECollisionChannel::Pawn, ECollisionResponse::Block);
		WallBox->SetCollisionResponseToChannel(ECollisionChannel::Projectile, ECollisionResponse::Ignore);
		WallBox->SetKinematic(true);
		WallBox->SetSimulatePhysics(false);
	}
}

bool ASniperPawn::TryMoveSniperWithCollision(const FVector& MoveDelta)
{
	if (MoveDelta.IsNearlyZero())
	{
		return false;
	}

	const FVector CurrentLocation = GetActorLocation();
	FVector MinMovementLocation;
	FVector MaxMovementLocation;
	if (TryGetSniperMovementBounds(MinMovementLocation, MaxMovementLocation))
	{
		FVector DesiredLocation = CurrentLocation + MoveDelta;
		DesiredLocation.X = FMath::Clamp(DesiredLocation.X, MinMovementLocation.X, MaxMovementLocation.X);
		DesiredLocation.Y = FMath::Clamp(DesiredLocation.Y, MinMovementLocation.Y, MaxMovementLocation.Y);

		if (FVector::DistSquared(CurrentLocation, DesiredLocation) <= 1.0e-6f)
		{
			return false;
		}

		SetActorLocation(DesiredLocation);
		return true;
	}

	FHitResult FullHit;
	if (!TrySweepSniperMovement(MoveDelta, FullHit) || !IsSniperMovementWallActor(FullHit.HitActor))
	{
		SetActorLocation(CurrentLocation + MoveDelta);
		return true;
	}

	bool bMoved = false;

	const FVector XOnlyDelta(MoveDelta.X, 0.0f, 0.0f);
	if (!XOnlyDelta.IsNearlyZero())
	{
		bMoved = TryApplySniperMovementDelta(XOnlyDelta) || bMoved;
	}

	const FVector YOnlyDelta(0.0f, MoveDelta.Y, 0.0f);
	if (!YOnlyDelta.IsNearlyZero())
	{
		bMoved = TryApplySniperMovementDelta(YOnlyDelta) || bMoved;
	}

	if (bMoved)
	{
		return FVector::DistSquared(CurrentLocation, GetActorLocation()) > 1.0e-6f;
	}

	const float MoveLength = MoveDelta.Length();
	if (MoveLength <= FMath::Epsilon)
	{
		return false;
	}

	const float SafeDistance = (std::max)(0.0f, FullHit.Distance - SniperMovementSweepPullbackDistance);
	if (SafeDistance <= FMath::Epsilon)
	{
		return false;
	}

	SetActorLocation(CurrentLocation + MoveDelta.Normalized() * (std::min)(SafeDistance, MoveLength));
	return true;
}

bool ASniperPawn::TryApplySniperMovementDelta(const FVector& MoveDelta)
{
	if (MoveDelta.IsNearlyZero())
	{
		return false;
	}

	const FVector CurrentLocation = GetActorLocation();
	FHitResult Hit;
	if (!TrySweepSniperMovement(MoveDelta, Hit) || !IsSniperMovementWallActor(Hit.HitActor))
	{
		SetActorLocation(CurrentLocation + MoveDelta);
		return true;
	}

	const float MoveLength = MoveDelta.Length();
	if (MoveLength <= FMath::Epsilon)
	{
		return false;
	}

	const float SafeDistance = (std::max)(0.0f, Hit.Distance - SniperMovementSweepPullbackDistance);
	if (SafeDistance <= FMath::Epsilon)
	{
		return false;
	}

	SetActorLocation(CurrentLocation + MoveDelta.Normalized() * (std::min)(SafeDistance, MoveLength));
	return true;
}

bool ASniperPawn::TrySweepSniperMovement(const FVector& MoveDelta, FHitResult& OutHit) const
{
	OutHit = FHitResult{};
	if (MoveDelta.IsNearlyZero())
	{
		return false;
	}

	const UCapsuleComponent* Capsule = GetSniperMovementCapsule();
	const UWorld* World = GetWorld();
	if (!Capsule || !World)
	{
		return false;
	}

	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	if (Radius <= 0.0f || HalfHeight <= 0.0f)
	{
		return false;
	}

	const FVector Start = Capsule->GetWorldLocation();
	const FVector End = Start + MoveDelta;
	const FQuat Rotation = Capsule->GetWorldMatrix().ToQuat();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	return World->PhysicsSweep(Start, End, Rotation, Shape, OutHit, ECollisionChannel::Pawn, this);
}

bool ASniperPawn::TryGetSniperMovementBounds(FVector& OutMinLocation, FVector& OutMaxLocation) const
{
	if (bUseExplicitSniperMovementBounds)
	{
		OutMinLocation = SniperMovementMinLocation;
		OutMaxLocation = SniperMovementMaxLocation;
		if (OutMinLocation.X > OutMaxLocation.X)
		{
			std::swap(OutMinLocation.X, OutMaxLocation.X);
		}
		if (OutMinLocation.Y > OutMaxLocation.Y)
		{
			std::swap(OutMinLocation.Y, OutMaxLocation.Y);
		}
		return true;
	}

	const UCapsuleComponent* Capsule = GetSniperMovementCapsule();
	if (!Capsule)
	{
		return false;
	}

	const FVector ActorLocation = GetActorLocation();
	const FVector CapsuleCenter = Capsule->GetWorldLocation();
	const FVector ActorToCapsuleCenter = CapsuleCenter - ActorLocation;
	const float CapsuleRadius = (std::max)(Capsule->GetScaledCapsuleRadius(), 0.0f);

	float MinX = -FLT_MAX;
	float MaxX = FLT_MAX;
	float MinY = -FLT_MAX;
	float MaxY = FLT_MAX;
	bool bHasMinX = false;
	bool bHasMaxX = false;
	bool bHasMinY = false;
	bool bHasMaxY = false;

	for (const char* WallName : SniperMovementWallActorNames)
	{
		const UBoxComponent* WallBox = FindSniperMovementWallBox(WallName);
		if (!WallBox)
		{
			continue;
		}

		const FBoundingBox WallBounds = WallBox->GetWorldBoundingBox();
		if (!WallBounds.IsValid())
		{
			continue;
		}

		const FVector WallExtent = WallBounds.GetExtent();
		const bool bUseXBoundary = WallExtent.Y >= WallExtent.X;
		if (bUseXBoundary)
		{
			if (CapsuleCenter.X < WallBounds.Min.X)
			{
				MaxX = (std::min)(MaxX, WallBounds.Min.X - CapsuleRadius);
				bHasMaxX = true;
			}
			else if (CapsuleCenter.X > WallBounds.Max.X)
			{
				MinX = (std::max)(MinX, WallBounds.Max.X + CapsuleRadius);
				bHasMinX = true;
			}
		}
		else
		{
			if (CapsuleCenter.Y < WallBounds.Min.Y)
			{
				MaxY = (std::min)(MaxY, WallBounds.Min.Y - CapsuleRadius);
				bHasMaxY = true;
			}
			else if (CapsuleCenter.Y > WallBounds.Max.Y)
			{
				MinY = (std::max)(MinY, WallBounds.Max.Y + CapsuleRadius);
				bHasMinY = true;
			}
		}
	}

	if (!bHasMinX || !bHasMaxX || !bHasMinY || !bHasMaxY || MinX > MaxX || MinY > MaxY)
	{
		return false;
	}

	OutMinLocation = FVector(MinX - ActorToCapsuleCenter.X, MinY - ActorToCapsuleCenter.Y, ActorLocation.Z);
	OutMaxLocation = FVector(MaxX - ActorToCapsuleCenter.X, MaxY - ActorToCapsuleCenter.Y, ActorLocation.Z);
	return true;
}

bool ASniperPawn::IsSniperMovementWallActor(const AActor* Actor) const
{
	return Actor && IsSniperMovementWallName(Actor->GetName());
}

UBoxComponent* ASniperPawn::FindSniperMovementWallBox(const FString& ActorName) const
{
	UWorld* World = GetWorld();
	if (!World || ActorName.empty())
	{
		return nullptr;
	}

	for (AActor* Actor : World->GetActors())
	{
		if (!Actor || Actor->GetName() != ActorName)
		{
			continue;
		}

		ABoxActor* BoxActor = Cast<ABoxActor>(Actor);
		UBoxComponent* BoxComponent = BoxActor ? BoxActor->GetBoxComponent() : Actor->GetComponentByClass<UBoxComponent>();
		return BoxComponent;
	}

	return nullptr;
}

UCapsuleComponent* ASniperPawn::GetSniperMovementCapsule() const
{
	if (UCapsuleComponent* RootCapsule = Cast<UCapsuleComponent>(GetRootComponent()))
	{
		return RootCapsule;
	}

	return GetComponentByClass<UCapsuleComponent>();
}

void ASniperPawn::PlaySniperFootstep()
{
	if (FootstepSFXPath.empty() || FootstepVolume <= 0.0f)
	{
		return;
	}

	FAudioManager::Get().PlaySFX(FootstepSFXPath, FMath::Clamp(FootstepVolume, 0.0f, 1.0f));
}

float ASniperPawn::GetScopeBlendAlpha() const
{
	return ComputeScopeAlpha(ScopeState);
}

float ASniperPawn::GetMouseSensitivityMultiplier() const
{
	const float SafeBaseSensitivity = BaseMouseSensitivity > 0.0001f ? BaseMouseSensitivity : 0.0001f;
	return MouseSensitivity / SafeBaseSensitivity;
}

void ASniperPawn::SetMouseSensitivityMultiplier(float Multiplier)
{
	CacheInputSensitivityBases();
	const float ClampedMultiplier = FMath::Clamp(Multiplier, 0.1f, 5.0f);
	MouseSensitivity = BaseMouseSensitivity * ClampedMultiplier;
}

float ASniperPawn::GetGamepadLookSensitivityMultiplier() const
{
	const float SafeBaseSensitivity = BaseGamepadLookSensitivity > 0.0001f ? BaseGamepadLookSensitivity : 0.0001f;
	return GamepadLookSensitivity / SafeBaseSensitivity;
}

void ASniperPawn::SetGamepadLookSensitivityMultiplier(float Multiplier)
{
	CacheInputSensitivityBases();
	const float ClampedMultiplier = FMath::Clamp(Multiplier, 0.1f, 5.0f);
	GamepadLookSensitivity = BaseGamepadLookSensitivity * ClampedMultiplier;
}

void ASniperPawn::SetRightClickZoomToggleMode(bool bToggleMode)
{
	if (bRightClickZoomToggleMode == bToggleMode)
	{
		return;
	}

	bRightClickZoomToggleMode = bToggleMode;
	if (!bRightClickZoomToggleMode && bMouseScopeInputHeld)
	{
		ForceScopeReleased();
	}
}

bool ASniperPawn::IsReloading() const
{
	const USniperWeaponComponent* SniperWeapon = WeaponComponent.Get();
	return SniperWeapon && SniperWeapon->IsReloading();
}

float ASniperPawn::GetReloadRemaining() const
{
	const USniperWeaponComponent* SniperWeapon = WeaponComponent.Get();
	return SniperWeapon ? SniperWeapon->GetReloadRemaining() : 0.0f;
}

float ASniperPawn::GetReloadProgress() const
{
	const USniperWeaponComponent* SniperWeapon = WeaponComponent.Get();
	return SniperWeapon ? SniperWeapon->GetReloadProgress() : 0.0f;
}

void ASniperPawn::ForceScopeReleased()
{
	bMouseScopeInputHeld = false;
	bGamepadScopeInputHeld = false;
	bKeyboardHoldBreathInputHeld = false;
	bGamepadHoldBreathInputHeld = false;
	bGamepadFireTriggerHeld = false;
	InputState.bScopeHeld = false;
	InputState.bHoldBreathHeld = false;
	ScopeState.bIsScoped = false;
	ScopeState.TargetFOV = ScopeState.NormalFOV;
	ScopeState.CurrentFOV = ScopeState.NormalFOV;
	ScopeState.CurrentSensitivity = ScopeState.NormalSensitivity;
	AimSwayState.BreathMultiplier = 1.0f;
	AimSwayState.bRequireHoldBreathRelease = false;

	if (Camera)
	{
		Camera->SetFOV(ScopeState.NormalFOV);
	}

	if (APlayerController* PC = GetController())
	{
		if (APlayerCameraManager* CameraManager = PC->GetPlayerCameraManager())
		{
			CameraManager->SetScopeZoomEnabled(false);
			CameraManager->ClearScopeLens();
		}
	}

	UpdateWeaponVisualScopeVisibility();
}

bool ASniperPawn::IsHoldBreathActive() const
{
	return ScopeState.bIsScoped
		&& !AimSwayState.bForcedRecovery
		&& !AimSwayState.bRequireHoldBreathRelease
		&& AimSwayState.HoldBreathCooldownRemaining <= 0.0f
		&& InputState.bHoldBreathHeld
		&& AimSwayState.HoldBreathGauge > 0.0f
		&& AimSwayState.BreathMultiplier < 1.0f;
}

FRotator ASniperPawn::BuildEffectiveAimRotation() const
{
	FRotator EffectiveRotation = GetControlRotation();
	EffectiveRotation.Pitch += AimSwayState.CurrentSwayPitch + RecoilState.CurrentRecoilPitch;
	EffectiveRotation.Yaw += AimSwayState.CurrentSwayYaw + RecoilState.CurrentRecoilYaw;
	EffectiveRotation.Pitch = ClampSniperPitch(EffectiveRotation.Pitch, MinCameraPitch, MaxCameraPitch);
	EffectiveRotation.Roll = 0.0f;
	return EffectiveRotation;
}

bool ASniperPawn::CanEnterScope() const
{
	const UWorld* World = GetWorld();
	if (World && World->IsPaused())
	{
		return false;
	}

	return !IsSniperKillCamPlaying(this) && !IsReloading();
}

float ASniperPawn::ClampScopeZoomMagnification(float Magnification) const
{
	float MinZoomMagnification = ScopeState.MinZoomMagnification;
	float MaxZoomMagnification = ScopeState.MaxZoomMagnification;
	if (MinZoomMagnification > MaxZoomMagnification)
	{
		std::swap(MinZoomMagnification, MaxZoomMagnification);
	}

	MinZoomMagnification = (std::max)(MinZoomMagnification, 1.0f);
	MaxZoomMagnification = (std::max)(MaxZoomMagnification, MinZoomMagnification);
	return FMath::Clamp(Magnification, MinZoomMagnification, MaxZoomMagnification);
}

float ASniperPawn::ComputeScopedFOVForMagnification(float Magnification) const
{
	const float SafeMagnification = (std::max)(ClampScopeZoomMagnification(Magnification), 1.0f);
	const float HalfBaseScopedFOV = ScopeState.NormalFOV * 0.5f;
	const float ZoomedHalfFOVTangent = std::tan(HalfBaseScopedFOV) / SafeMagnification;
	const float ComputedScopedFOV = std::atan(ZoomedHalfFOVTangent) * 2.0f;
	return FMath::Clamp(ComputedScopedFOV, 0.01f, ScopeState.NormalFOV);
}

float ASniperPawn::ComputeScopedSensitivityForMagnification(float Magnification) const
{
	const float SafeMinZoomMagnification = (std::max)(ScopeState.MinZoomMagnification, 1.0f);
	const float SafeMaxZoomMagnification = (std::max)(ScopeState.MaxZoomMagnification, SafeMinZoomMagnification);
	const float SafeMagnification = ClampScopeZoomMagnification(Magnification);
	const float ZoomRange = SafeMaxZoomMagnification - SafeMinZoomMagnification;
	const float ZoomAlpha = ZoomRange > FMath::Epsilon
		? FMath::Clamp((SafeMagnification - SafeMinZoomMagnification) / ZoomRange, 0.0f, 1.0f)
		: 0.0f;

	const float MinZoomScopedSensitivity = (std::max)(ScopeState.ScopedSensitivity, 0.01f);
	const float MaxZoomScopedSensitivity = (std::max)(ScopeState.MaxZoomScopedSensitivity, 0.01f);
	const float ComputedScopedSensitivity = FMath::Lerp(
		MinZoomScopedSensitivity,
		MaxZoomScopedSensitivity,
		ZoomAlpha);

	return FMath::Clamp(
		ComputedScopedSensitivity,
		0.01f,
		(std::max)(ScopeState.NormalSensitivity, 0.01f));
}

void ASniperPawn::AdjustScopeZoomStep(int32 StepDelta)
{
	if (StepDelta == 0)
	{
		return;
	}

	const float SafeZoomStep = (std::max)(ScopeState.ZoomStep, 0.1f);
	const float NewZoomMagnification =
		ScopeState.TargetZoomMagnification + static_cast<float>(StepDelta) * SafeZoomStep;
	ScopeState.TargetZoomMagnification = ClampScopeZoomMagnification(NewZoomMagnification);
}

void ASniperPawn::HandleTurnInput(float Value)
{
	InputState.MouseDeltaX = Value;
	if (std::abs(Value) <= 0.0001f)
	{
		return;
	}

	FRotator Control = GetControlRotation();
	Control.Yaw += Value * MouseSensitivity * ScopeState.CurrentSensitivity * GetSniperInputTimeScale();
	SetControlRotation(Control);
	ApplySniperControlRotation();
}

void ASniperPawn::HandleGamepadTurnInput(float Value)
{
	if (std::abs(Value) <= 0.0001f)
	{
		return;
	}

	FRotator Control = GetControlRotation();
	Control.Yaw += Value * GamepadLookSensitivity * CachedInputDeltaTime * ScopeState.CurrentSensitivity;
	SetControlRotation(Control);
	ApplySniperControlRotation();
}

void ASniperPawn::HandleLookUpInput(float Value)
{
	InputState.MouseDeltaY = Value;
	if (std::abs(Value) <= 0.0001f)
	{
		return;
	}

	const float Direction = bInvertMouseY ? -1.0f : 1.0f;
	FRotator Control = GetControlRotation();
	Control.Pitch += Value * MouseSensitivity * ScopeState.CurrentSensitivity * Direction * GetSniperInputTimeScale();
	Control.Pitch = ClampSniperPitch(Control.Pitch, MinCameraPitch, MaxCameraPitch);
	SetControlRotation(Control);
	ApplySniperControlRotation();
}

void ASniperPawn::HandleGamepadLookUpInput(float Value)
{
	if (std::abs(Value) <= 0.0001f)
	{
		return;
	}

	const float Direction = bInvertMouseY ? 1.0f : -1.0f;
	FRotator Control = GetControlRotation();
	Control.Pitch += Value * GamepadLookSensitivity * CachedInputDeltaTime * ScopeState.CurrentSensitivity * Direction;
	Control.Pitch = ClampSniperPitch(Control.Pitch, MinCameraPitch, MaxCameraPitch);
	SetControlRotation(Control);
	ApplySniperControlRotation();
}

void ASniperPawn::HandleMoveForwardInput(float Value)
{
	SniperMoveForwardInput = Value;
}

void ASniperPawn::HandleMoveRightInput(float Value)
{
	SniperMoveRightInput = Value;
}

void ASniperPawn::HandleScopeZoomAxis(float Value)
{
	if (!ScopeState.bIsScoped || IsReloading() || std::abs(Value) <= 0.0001f)
	{
		return;
	}

	AdjustScopeZoomStep(Value > 0.0f ? +1 : -1);
}

void ASniperPawn::HandleGamepadScopeAxis(float Value)
{
	const bool bHeld = Value >= GamepadTriggerPressThreshold;
	if (IsSniperKillCamPlaying(this))
	{
		bGamepadScopeInputHeld = false;
		RefreshScopeHeldState();
		return;
	}

	if (bGamepadScopeInputHeld == bHeld)
	{
		return;
	}

	bGamepadScopeInputHeld = bHeld;
	RefreshScopeHeldState();
}

void ASniperPawn::HandleGamepadFireAxis(float Value)
{
	const bool bPressed = Value >= GamepadTriggerPressThreshold;
	if (bGamepadFireTriggerHeld == bPressed)
	{
		return;
	}

	bGamepadFireTriggerHeld = bPressed;
	if (bPressed)
	{
		HandleFirePressed();
	}
}

void ASniperPawn::HandleScopePressed()
{
	if (!CanEnterScope())
	{
		bMouseScopeInputHeld = false;
		RefreshScopeHeldState();
		return;
	}

	bMouseScopeInputHeld = bRightClickZoomToggleMode ? !bMouseScopeInputHeld : true;
	RefreshScopeHeldState();
}

void ASniperPawn::HandleScopeReleased()
{
	if (bRightClickZoomToggleMode)
	{
		return;
	}

	bMouseScopeInputHeld = false;
	RefreshScopeHeldState();
}

void ASniperPawn::HandleHoldBreathPressed()
{
	if (IsSniperKillCamPlaying(this))
	{
		bKeyboardHoldBreathInputHeld = false;
		RefreshHoldBreathHeldState();
		return;
	}

	bKeyboardHoldBreathInputHeld = true;
	RefreshHoldBreathHeldState();
}

void ASniperPawn::HandleHoldBreathReleased()
{
	bKeyboardHoldBreathInputHeld = false;
	RefreshHoldBreathHeldState();
}

void ASniperPawn::HandleGamepadHoldBreathPressed()
{
	if (IsSniperKillCamPlaying(this))
	{
		bGamepadHoldBreathInputHeld = false;
		RefreshHoldBreathHeldState();
		return;
	}

	bGamepadHoldBreathInputHeld = true;
	RefreshHoldBreathHeldState();
}

void ASniperPawn::HandleGamepadHoldBreathReleased()
{
	bGamepadHoldBreathInputHeld = false;
	RefreshHoldBreathHeldState();
}

void ASniperPawn::HandleSwitchAmmoNormalPressed()
{
	InputState.bSwitchAmmoPressed = true;
	if (USniperWeaponComponent* SniperWeapon = WeaponComponent.Get())
	{
		SniperWeapon->SetCurrentAmmoType(ESniperAmmoType::Normal);
	}
	InputState.bSwitchAmmoPressed = false;
}

void ASniperPawn::HandleSwitchAmmoAntiMaterialPressed()
{
	InputState.bSwitchAmmoPressed = true;
	if (USniperWeaponComponent* SniperWeapon = WeaponComponent.Get())
	{
		SniperWeapon->SetCurrentAmmoType(ESniperAmmoType::AntiMaterial);
	}
	InputState.bSwitchAmmoPressed = false;
}

void ASniperPawn::HandleFirePressed()
{
	InputState.bFirePressed = true;
	FireCurrentRound();
}

void ASniperPawn::HandleReloadPressed()
{
	if (IsSniperKillCamPlaying(this))
	{
		InputState.bReloadPressed = false;
		return;
	}

	InputState.bReloadPressed = true;
	if (USniperWeaponComponent* SniperWeapon = WeaponComponent.Get())
	{
		if (SniperWeapon->RequestReload())
		{
			ForceScopeReleased();
		}
	}
	InputState.bReloadPressed = false;
}

void ASniperPawn::HandleScopeZoomInPressed()
{
	if (!ScopeState.bIsScoped)
	{
		return;
	}

	AdjustScopeZoomStep(+1);
}

void ASniperPawn::HandleScopeZoomOutPressed()
{
	if (!ScopeState.bIsScoped)
	{
		return;
	}

	AdjustScopeZoomStep(-1);
}

void ASniperPawn::RefreshScopeHeldState()
{
	InputState.bScopeHeld = bMouseScopeInputHeld || bGamepadScopeInputHeld;
}

void ASniperPawn::RefreshHoldBreathHeldState()
{
	InputState.bHoldBreathHeld = bKeyboardHoldBreathInputHeld || bGamepadHoldBreathInputHeld;
}

void ASniperPawn::ApplyFireRecoil()
{
	USniperWeaponComponent* SniperWeapon = WeaponComponent.Get();
	if (!SniperWeapon)
	{
		return;
	}

	const FAmmoBallisticData* AmmoData = SniperWeapon->GetCurrentAmmoData();
	if (!AmmoData)
	{
		return;
	}

	RecoilState.LastShotRecoilPitch = AmmoData->RecoilPitch;
	RecoilState.LastShotRecoilYaw = RandomRange(-AmmoData->RecoilYawRandomRange, AmmoData->RecoilYawRandomRange);
	RecoilState.CurrentRecoilPitch += RecoilState.LastShotRecoilPitch;
	RecoilState.CurrentRecoilYaw += RecoilState.LastShotRecoilYaw;
}

bool ASniperPawn::FireCurrentRound()
{
	USniperWeaponComponent* SniperWeapon = WeaponComponent.Get();
	UCameraComponent* SniperCamera = Camera.Get();
	if (!SniperWeapon || !SniperCamera)
	{
		return false;
	}

	const FVector ShotDirection = BuildEffectiveAimRotation().GetForwardVector().Normalized();
	if (ShotDirection.IsNearlyZero())
	{
		return false;
	}

	const FVector MuzzlePosition = SniperCamera->GetWorldLocation() + ShotDirection * 5.0f;
	const bool bFired = SniperWeapon->RequestFire(
		MuzzlePosition,
		ShotDirection,
		InputState.bScopeHeld,
		this);

	if (bFired)
	{
		ApplyFireRecoil();

		if (bEnableBulletFlightSlomo && !IsSniperKillCamPlaying(this))
		{
			if (UActionComponent* SniperAction = ActionComponent.Get())
			{
				const float InitialSlomoDuration = (std::max)(BulletFlightSlomoDuration, 0.05f);
				SniperAction->Slomo(InitialSlomoDuration, BulletFlightSlomoTimeDilation);
				bBulletFlightSlomoActive = true;
			}
		}
	}

	InputState.bFirePressed = false;
	return bFired;
}
