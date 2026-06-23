#pragma once

#include "Math/Vector.h"
#include "Object/Object.h"
#include "Viewport/ViewportClient.h"

class AActor;
class FViewport;
class UCameraComponent;
class UStaticMeshComponent;

class UGameViewportClient : public UObject, public FViewportClient
{
public:
	DECLARE_CLASS(UGameViewportClient, UObject)

	UGameViewportClient() = default;
	~UGameViewportClient() override = default;

	void Draw(FViewport* InViewport, float DeltaTime) override {}
	bool InputKey(int32 Key, bool bPressed) override { return false; }
	bool ProcessInput(FViewportInputContext& Context) override;
	bool WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const override;

	void SetViewport(FViewport* InViewport) { Viewport = InViewport; }
	FViewport* GetViewport() const { return Viewport; }

	void SetDrivingCamera(UCameraComponent* InCamera) { DrivingCamera = InCamera; }
	UCameraComponent* GetDrivingCamera() const { return DrivingCamera; }
	void SetPIEPossessedInputEnabled(bool bEnabled) { bPIEPossessedInputEnabled = bEnabled; }

	void OnBeginPIE();
	void OnEndPIE();

private:
	void UpdatePIEInputArmedState(const FViewportInputContext& Context, bool& bOutConsumedByArmToggle);
	FVector BuildMoveInput(const FViewportInputContext& Context, bool bKeyboardBlocked) const;
	bool ApplyMoveInput(const FVector& MoveInput, float DeltaTime);
	bool ApplyLookInput(const FViewportInputContext& Context, bool bMouseBlocked);
	void UpdatePlayerCameraFromOrbitState();
	void EnsurePIEPlayer();
	void ReleasePIEPlayer();
	void SyncPlayerViewToEditorViewport();

private:
	FViewport* Viewport = nullptr;
	UCameraComponent* DrivingCamera = nullptr;

	AActor* PIEPlayerActor = nullptr;
	uint32 PIEPlayerActorUUID = 0u;
	UStaticMeshComponent* PIEPlayerMesh = nullptr;
	UCameraComponent* PIEPlayerCamera = nullptr;
	float PIECameraBoomLength = 6.0f;
	float PIECameraPitch = -20.0f;
	float PIECameraYaw = 0.0f;
	bool bPIEInputArmed = false;
	bool bPIEPossessedInputEnabled = false;
};

