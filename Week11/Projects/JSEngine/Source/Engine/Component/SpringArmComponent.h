#pragma once

#include "Component/SceneComponent.h"

UCLASS()
class USpringArmComponent : public USceneComponent
{
	GENERATED_BODY(USpringArmComponent, USceneComponent)
public:
	USpringArmComponent();

	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	void UpdateWorldMatrix() const override;
	FVector GetSocketLocalLocation() const;
	void UpdateSocketChildren();

	float GetTargetArmLength() const { return TargetArmLength; }
	void SetTargetArmLength(float InTargetArmLength);

	const FVector& GetSocketOffset() const { return SocketOffset; }
	void SetSocketOffset(const FVector& InSocketOffset);

	bool IsCameraLagEnabled() const { return bEnableCameraLag; }
	void SetCameraLagEnabled(bool bEnabled);

	float GetCameraLagSpeed() const { return CameraLagSpeed; }
	void SetCameraLagSpeed(float InCameraLagSpeed);

protected:
	void TickComponent(float DeltaTime) override;

private:
	FTransform CalculateDesiredSocketTransform() const;
	void ResetCameraLag();

private:
	UPROPERTY(EditAnywhere, Category = "Spring Arm", DisplayName = "Target Arm Length")
	float TargetArmLength = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Spring Arm", DisplayName = "Socket Offset")
	FVector SocketOffset = FVector(0.0f, 0.0f, 0.25f);

	UPROPERTY(EditAnywhere, Category = "Spring Arm", DisplayName = "Enable Camera Lag")
	bool bEnableCameraLag = false;

	UPROPERTY(EditAnywhere, Category = "Spring Arm", DisplayName = "Camera Lag Speed")
	float CameraLagSpeed = 10.0f;

	mutable FVector LagLocation = FVector::ZeroVector;
	mutable bool bLagLocationInitialized = false;
};
