#pragma once

#include "Component/ActorComponent.h"

#include "Source/Engine/Component/Gameplay/KillCamRailRigComponent.generated.h"

UCLASS()
class UKillCamRailRigComponent : public UActorComponent
{
public:
	GENERATED_BODY()

	UFUNCTION(Callable, Category="KillCam|Rail")
	void ResetToDefaultRailPose();

	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail", DisplayName="Forward Offset", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float ForwardOffset = -2.2f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail", DisplayName="Side Offset", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float SideOffset = 0.18f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail", DisplayName="Up Offset", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float UpOffset = 0.22f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail", DisplayName="Look Ahead", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float LookAhead = 0.75f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail", DisplayName="Look Side Offset", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float LookSideOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail", DisplayName="Look Up Offset", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float LookUpOffset = 0.0f;
	UPROPERTY(Edit, Save, Category="KillCam|Rail", DisplayName="Look At Bullet Visual")
	bool bLookAtBulletVisual = true;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail Timing", DisplayName="Camera Rail Alpha Override", Min=-1.0f, Max=1.0f, Speed=0.01f)
	float CameraRailAlphaOverride = -1.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail Timing", DisplayName="Camera Rail Alpha Scale", Min=-4.0f, Max=4.0f, Speed=0.01f)
	float CameraRailAlphaScale = 1.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail Timing", DisplayName="Camera Rail Alpha Offset", Min=-1.0f, Max=1.0f, Speed=0.01f)
	float CameraRailAlphaOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail Timing", DisplayName="Camera Rail Alpha Ease", Min=0.0f, Max=1.0f, Speed=0.01f)
	float CameraRailAlphaEase = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail Timing", DisplayName="Camera Rail Alpha Power", Min=0.05f, Max=8.0f, Speed=0.01f)
	float CameraRailAlphaPower = 1.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail Timing", DisplayName="Look Rail Alpha Override", Min=-1.0f, Max=1.0f, Speed=0.01f)
	float LookRailAlphaOverride = -1.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail Timing", DisplayName="Look Rail Alpha Scale", Min=-4.0f, Max=4.0f, Speed=0.01f)
	float LookRailAlphaScale = 1.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail Timing", DisplayName="Look Rail Alpha Offset", Min=-1.0f, Max=1.0f, Speed=0.01f)
	float LookRailAlphaOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail Timing", DisplayName="Look Rail Alpha Ease", Min=0.0f, Max=1.0f, Speed=0.01f)
	float LookRailAlphaEase = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Rail Timing", DisplayName="Look Rail Alpha Power", Min=0.05f, Max=8.0f, Speed=0.01f)
	float LookRailAlphaPower = 1.0f;
	UPROPERTY(Edit, Save, Category="KillCam|Rail Timing", DisplayName="Allow Rail Extrapolation")
	bool bAllowRailExtrapolation = true;
	UPROPERTY(Edit, Save, Category="KillCam|Rail Timing", DisplayName="Clamp Authored Rail Alpha")
	bool bClampAuthoredRailAlpha = true;
	UPROPERTY(Edit, Save, Category="KillCam|Rail Timing", DisplayName="Rail Alpha Clamp Min", Min=-4.0f, Max=4.0f, Speed=0.01f)
	float RailAlphaClampMin = -0.35f;
	UPROPERTY(Edit, Save, Category="KillCam|Rail Timing", DisplayName="Rail Alpha Clamp Max", Min=-4.0f, Max=4.0f, Speed=0.01f)
	float RailAlphaClampMax = 1.35f;

	UPROPERTY(Edit, Save, Category="KillCam|Distance", DisplayName="Scale Offsets By Shot Distance")
	bool bScaleOffsetsByShotDistance = false;
	UPROPERTY(Edit, Save, Category="KillCam|Distance", DisplayName="Reference Distance", Min=0.01f, Max=100000.0f, Speed=0.1f)
	float ReferenceDistance = 50.0f;
	UPROPERTY(Edit, Save, Category="KillCam|Distance", DisplayName="Min Distance Scale", Min=0.01f, Max=100.0f, Speed=0.01f)
	float MinDistanceScale = 0.5f;
	UPROPERTY(Edit, Save, Category="KillCam|Distance", DisplayName="Max Distance Scale", Min=0.01f, Max=100.0f, Speed=0.01f)
	float MaxDistanceScale = 2.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Distance", DisplayName="Linear Offset Distance Blend", Min=0.0f, Max=1.0f, Speed=0.01f)
	float LinearOffsetDistanceScaleBlend = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Distance", DisplayName="Orbit Radius Distance Blend", Min=0.0f, Max=1.0f, Speed=0.01f)
	float OrbitRadiusDistanceScaleBlend = 0.0f;

	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Camera", DisplayName="FOV", Min=0.1f, Max=3.0f, Speed=0.01f)
	float FOV = 0.72f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Camera", DisplayName="Roll", Min=-180.0f, Max=180.0f, Speed=0.5f)
	float Roll = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Camera", DisplayName="Lag Speed", Min=0.0f, Max=60.0f, Speed=0.1f)
	float CameraLagSpeed = 12.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Camera", DisplayName="Look Lag Speed", Min=0.0f, Max=60.0f, Speed=0.1f)
	float LookLagSpeed = 10.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Camera", DisplayName="Shake Amplitude", Min=0.0f, Max=1.0f, Speed=0.001f)
	float CameraShakeAmplitude = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Camera", DisplayName="Shake Frequency", Min=0.0f, Max=60.0f, Speed=0.1f)
	float CameraShakeFrequency = 10.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Orbit", DisplayName="Orbit Blend", Min=0.0f, Max=1.0f, Speed=0.01f)
	float OrbitBlend = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Orbit", DisplayName="Orbit Yaw", Min=-180.0f, Max=180.0f, Speed=0.5f)
	float OrbitYaw = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Orbit", DisplayName="Orbit Pitch", Min=-89.0f, Max=89.0f, Speed=0.5f)
	float OrbitPitch = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Orbit", DisplayName="Orbit Radius", Min=0.0f, Max=50.0f, Speed=0.05f)
	float OrbitRadius = 2.2f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Orbit", DisplayName="Orbit Pivot Forward Offset", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float OrbitPivotForwardOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Orbit", DisplayName="Orbit Pivot Side Offset", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float OrbitPivotSideOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Orbit", DisplayName="Orbit Pivot Up Offset", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float OrbitPivotUpOffset = 0.0f;

	UPROPERTY(Edit, Save, Animatable, Category="KillCam|DOF", DisplayName="Focus Range", Min=0.0f, Max=100.0f, Speed=0.05f)
	float DOFFocusRange = 1.25f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|DOF", DisplayName="Blur Radius", Min=0.0f, Max=20.0f, Speed=0.05f)
	float DOFBlurRadius = 4.0f;

	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Forward Offset", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float BulletForwardOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Side Offset", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float BulletSideOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Up Offset", Min=-20.0f, Max=20.0f, Speed=0.05f)
	float BulletUpOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Scale Multiplier", Min=0.0f, Max=20.0f, Speed=0.05f)
	float BulletScaleMultiplier = 1.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Scale X Multiplier", Min=0.0f, Max=20.0f, Speed=0.05f)
	float BulletScaleXMultiplier = 1.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Scale Y Multiplier", Min=0.0f, Max=20.0f, Speed=0.05f)
	float BulletScaleYMultiplier = 1.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Scale Z Multiplier", Min=0.0f, Max=20.0f, Speed=0.05f)
	float BulletScaleZMultiplier = 1.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Pitch Offset", Min=-180.0f, Max=180.0f, Speed=0.5f)
	float BulletPitchOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Yaw Offset", Min=-180.0f, Max=180.0f, Speed=0.5f)
	float BulletYawOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Roll Offset", Min=-180.0f, Max=180.0f, Speed=0.5f)
	float BulletRollOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Spin Revolutions", Min=-200.0f, Max=200.0f, Speed=0.05f)
	float BulletSpinRevolutions = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Spin Phase", Min=-360.0f, Max=360.0f, Speed=0.5f)
	float BulletSpinPhase = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Rail Alpha Override", Min=-1.0f, Max=1.0f, Speed=0.01f)
	float BulletRailAlphaOverride = -1.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Rail Alpha Scale", Min=-4.0f, Max=4.0f, Speed=0.01f)
	float BulletRailAlphaScale = 1.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Rail Alpha Offset", Min=-1.0f, Max=1.0f, Speed=0.01f)
	float BulletRailAlphaOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Rail Alpha Ease", Min=0.0f, Max=1.0f, Speed=0.01f)
	float BulletRailAlphaEase = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|Bullet", DisplayName="Bullet Rail Alpha Power", Min=0.05f, Max=8.0f, Speed=0.01f)
	float BulletRailAlphaPower = 1.0f;

	UPROPERTY(Edit, Save, Category="KillCam|ShockWave", DisplayName="Enable ShockWave")
	bool bEnableShockWave = false;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|ShockWave", DisplayName="ShockWave Forward Offset", Min=-20.0f, Max=20.0f, Speed=0.02f)
	float ShockWaveForwardOffset = -0.34f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|ShockWave", DisplayName="ShockWave Side Offset", Min=-20.0f, Max=20.0f, Speed=0.02f)
	float ShockWaveSideOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|ShockWave", DisplayName="ShockWave Up Offset", Min=-20.0f, Max=20.0f, Speed=0.02f)
	float ShockWaveUpOffset = 0.0f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|ShockWave", DisplayName="ShockWave Radius", Min=0.0f, Max=2.0f, Speed=0.005f)
	float ShockWaveRadius = 0.10f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|ShockWave", DisplayName="ShockWave Start Radius Boost", Min=0.0f, Max=2.0f, Speed=0.005f)
	float ShockWaveStartRadiusBoost = 0.12f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|ShockWave", DisplayName="ShockWave Width", Min=0.001f, Max=1.0f, Speed=0.002f)
	float ShockWaveWidth = 0.035f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|ShockWave", DisplayName="ShockWave Strength", Min=0.0f, Max=0.25f, Speed=0.001f)
	float ShockWaveStrength = 0.012f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|ShockWave", DisplayName="ShockWave Start Strength Boost", Min=0.0f, Max=0.25f, Speed=0.001f)
	float ShockWaveStartStrengthBoost = 0.025f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|ShockWave", DisplayName="ShockWave Falloff", Min=0.01f, Max=8.0f, Speed=0.02f)
	float ShockWaveFalloff = 1.6f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|ShockWave", DisplayName="ShockWave Directional Stretch", Min=0.0f, Max=6.0f, Speed=0.02f)
	float ShockWaveDirectionalStretch = 2.8f;
	UPROPERTY(Edit, Save, Animatable, Category="KillCam|ShockWave", DisplayName="ShockWave Decay", Min=0.01f, Max=20.0f, Speed=0.05f)
	float ShockWaveDecay = 5.5f;
};
