#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Types/EngineTypes.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"

#include "Source/Engine/GameFramework/Camera/CameraTypes.generated.h"

// ============================================================
// EViewTargetBlendFunction — SetViewTargetWithBlend 의 블렌딩 함수
// UE: EViewTargetBlendFunction
// ============================================================
UENUM()
enum class EViewTargetBlendFunction : uint8
{
	VTBlend_Linear,
	VTBlend_Cubic,
	VTBlend_EaseIn,
	VTBlend_EaseOut,
	VTBlend_EaseInOut,
	VTBlend_PreBlended,
};

// ============================================================
// FViewTargetTransitionParams — view target 전환 파라미터
// UE: FViewTargetTransitionParams
// ============================================================
USTRUCT()
struct FViewTargetTransitionParams
{
	GENERATED_BODY()

	UPROPERTY(Save)
	float BlendTime = 0.0f;

	UPROPERTY(Save, Enum=EViewTargetBlendFunction)
	EViewTargetBlendFunction BlendFunction = EViewTargetBlendFunction::VTBlend_Linear;

	UPROPERTY(Save)
	float BlendExp = 0.0f;

	UPROPERTY(Save)
	bool bLockOutgoing = false;
};

// ============================================================
// ECameraShakePlaySpace — Shake 가 적용되는 좌표 공간
// UE: ECameraShakePlaySpace
// ============================================================
UENUM()
enum class ECameraShakePlaySpace : uint8
{
	CameraLocal,    // 카메라 로컬
	World,          // 월드 좌표
	UserDefined,    // UserPlaySpaceRot 기준
};

// ============================================================
// FCameraShakeUpdateResult — 매 프레임 Shake 결과 (additive)
// UE: FCameraShakeUpdateResult (간소화)
// ============================================================
USTRUCT()
struct FCameraShakeUpdateResult
{
	GENERATED_BODY()

	UPROPERTY(Save)
	FVector Location = FVector(0.0f, 0.0f, 0.0f);   // additive world-space offset

	UPROPERTY(Save)
	FRotator Rotation;                              // additive Pitch/Yaw/Roll (degrees)

	UPROPERTY(Save)
	float FOV = 0.0f;                               // additive radians (FCameraState/FMinimalViewInfo 단위와 통일)
};

USTRUCT()
struct FCameraFadeState
{
	GENERATED_BODY()

	UPROPERTY(Save)
	bool bEnabled = false;

	UPROPERTY(Save)
	float Amount = 0.0f;

	UPROPERTY(Save)
	FLinearColor Color = FLinearColor::Black();

	UPROPERTY(Save)
	bool bFadeAudio = false;
};

USTRUCT()
struct FCameraVignetteState
{
	GENERATED_BODY()

	UPROPERTY(Save)
	bool bEnabled = false;

	UPROPERTY(Save)
	float Intensity = 0.0f;

	UPROPERTY(Save)
	float Radius = 0.75f;

	UPROPERTY(Save)
	float Softness = 0.35f;

	UPROPERTY(Save)
	FLinearColor Color = FLinearColor::Black();
};

USTRUCT()
struct FCameraLetterboxState
{
	GENERATED_BODY()

	UPROPERTY(Save)
	bool bEnabled = false;

	UPROPERTY(Save)
	float Amount = 1.0f;

	UPROPERTY(Save)
	float Thickness = 0.12f;

	UPROPERTY(Save)
	FLinearColor Color = FLinearColor::Black();
};

USTRUCT()
struct FCameraScopeLensState
{
	GENERATED_BODY()

	UPROPERTY(Save)
	bool bEnabled = false;

	UPROPERTY(Save)
	float Radius = 0.42f;

	UPROPERTY(Save)
	float CenterX = 0.5f;

	UPROPERTY(Save)
	float CenterY = 0.5f;

	UPROPERTY(Save)
	float Feather = 0.08f;

	UPROPERTY(Save)
	float OuterBlurRadius = 3.0f;

	UPROPERTY(Save)
	float EdgeBlurRadius = 1.25f;

	UPROPERTY(Save)
	float ZoomFOV = 3.14159265358979f / 8.0f;

	UPROPERTY(Save)
	float Intensity = 1.0f;

	UPROPERTY(Save)
	float CenterOffsetX = 0.0f;

	UPROPERTY(Save)
	float CenterOffsetY = 0.0f;

	UPROPERTY(Save)
	float LookSensitivityScale = 0.275f;

	UPROPERTY(Save)
	float BlendTime = 0.08f;
};

struct FCameraShockWaveState
{
	int32 Handle = 0;
	bool bEnabled = false;
	FVector WorldPosition = FVector::ZeroVector;
	FVector WorldDirection = FVector::ForwardVector;
	float Age = 0.0f;
	float Duration = 0.0f;
	float Radius = 0.12f;
	float Width = 0.035f;
	float Strength = 0.02f;
	float Falloff = 1.5f;
	float DirectionalStretch = 0.0f;
};
