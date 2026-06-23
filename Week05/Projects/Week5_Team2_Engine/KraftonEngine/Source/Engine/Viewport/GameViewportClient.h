#pragma once

#include "Engine/Input/InputTypes.h"
#include "Object/Object.h"
#include "Viewport/ViewportClient.h"

class AActor;
class FGameViewportController;
class FViewport;
class UCameraComponent;
class UStaticMeshComponent;

class UGameViewportClient : public UObject, public FViewportClient
{
	friend class FGameViewportController;

public:
	DECLARE_CLASS(UGameViewportClient, UObject)

	UGameViewportClient() = default;
	~UGameViewportClient() override;

	void Draw(FViewport* InViewport, float DeltaTime) override;
	bool InputKey(int32 Key, bool bPressed) override { return false; }
	bool ProcessInput(FViewportInputContext& Context) override;
	bool WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const override;

	void SetViewport(FViewport* InViewport) { Viewport = InViewport; }
	FViewport* GetViewport() const { return Viewport; }

	void OnBeginPIE();
	void OnEndPIE();
	void SyncPlayerViewToEditorViewport();
	bool HandleGlobalInput(float DeltaTime);
	bool HandlePlayerInput(float DeltaTime);
	bool HandleGizmoInput(float DeltaTime);

private:
	void EnsureController();
	void EnsurePIEPlayer();
	void ReleasePIEPlayer();

private:
	FViewport* Viewport = nullptr;
	bool bHasInputContext = false;
	FViewportInputContext InputContext;
	float DispatchDeltaTime = 0.0f;
	FGameViewportController* Controller = nullptr;

	AActor* PIEPlayerActor = nullptr;
	uint32 PIEPlayerActorUUID = 0u;
	UStaticMeshComponent* PIEPlayerMesh = nullptr;
	UCameraComponent* PIEPlayerCamera = nullptr;
	float PIECameraBoomLength = 6.0f;
	float PIECameraPitch = -20.0f;
	float PIECameraYaw = 0.0f;
	bool bPIEInputArmed = false;
};
