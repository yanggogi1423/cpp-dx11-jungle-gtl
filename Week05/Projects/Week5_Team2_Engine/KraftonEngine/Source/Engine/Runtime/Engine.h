#pragma once

#include "Object/Object.h"
#include "GameFramework/World.h"
#include "GameFramework/WorldContext.h"
#include "Engine/Input/InputRouter.h"
#include "Render/Pipeline/Renderer.h"
#include "Render/Pipeline/IRenderPipeline.h"
#include "Engine/Input/InputTypes.h"

#include <memory>
#include <functional>

#include "Viewport/FLevelViewportLayout.h"

class FWindowsWindow;
class FTimer;
class UGameViewportClient;
class FViewport;
class FViewportClient;
struct FRect;

class UEngine : public UObject
{
public:
	DECLARE_CLASS(UEngine, UObject)

	UEngine() = default;
	~UEngine() override;

	// Lifecycle
	virtual void Init(FWindowsWindow* InWindow);
	virtual void Shutdown();
	virtual void BeginPlay();
	virtual void Tick(float DeltaTime);

	virtual void OnWindowResized(uint32 Width, uint32 Height);

	// World context management
	FWorldContext& CreateWorldContext(EWorldType Type, const FName& Handle, const FString& Name = "");
	void DestroyWorldContext(const FName& Handle);

	// World context lookup
	FWorldContext* GetWorldContextFromHandle(const FName& Handle);
	const FWorldContext* GetWorldContextFromHandle(const FName& Handle) const;
	FWorldContext* GetWorldContextFromWorld(const UWorld* World);

	// Active world
	void SetActiveWorld(const FName& Handle);
	FName GetActiveWorldHandle() const { return ActiveWorldHandle; }

	// Accessors
	FWindowsWindow* GetWindow() const { return Window; }
	UWorld* GetWorld() const;
	const TArray<FWorldContext>& GetWorldList() const { return WorldList; }
	TArray<FWorldContext>& GetWorldList() { return WorldList; }

	void SetTimer(FTimer* InTimer) { Timer = InTimer; }
	FTimer* GetTimer() const { return Timer; }

	FRenderer& GetRenderer() { return Renderer; }
	void ResetRenderPipeline() { if (RenderPipeline) RenderPipeline->Reset(); }

public:
	// 레이아웃에 위임
	const TArray<FEditorViewportClient*>& GetAllViewportClients() const { return ViewportLayout.GetAllViewportClients(); }
	const TArray<FLevelEditorViewportClient*>& GetLevelViewportClients() const { return ViewportLayout.GetLevelViewportClients(); }

	void SetActiveViewport(FLevelEditorViewportClient* InClient) { ViewportLayout.SetActiveViewport(InClient); }
	FLevelEditorViewportClient* GetActiveViewport() const { return ViewportLayout.GetActiveViewport(); }

	void ToggleViewportSplit() { ViewportLayout.ToggleViewportSplit(); }
	bool IsSplitViewport() const { return ViewportLayout.IsSplitViewport(); }

	void RenderViewportUI(float DeltaTime) { ViewportLayout.RenderViewportUI(DeltaTime); }

	bool IsMouseOverViewport() const { return ViewportLayout.IsMouseOverViewport(); }
	
	void SetImGuiInputCapture(bool bCaptureMouse, bool bCaptureKeyboard);
	void ClearInputTargets();
	void RegisterInputTarget(
		FViewport* InViewport,
		FViewportClient* InClient,
		EInputRouteDomain InDomain,
		const std::function<bool(FRect&)>& InRectProvider);

protected:
	virtual FViewportClient* ResolveInputTargetClient(FViewport* InViewport, FViewportClient* InClient) const;
	void DispatchInput();
	void Render(float DeltaTime);
	void SetRenderPipeline(std::unique_ptr<IRenderPipeline> InPipeline);
	void WorldTick(float DeltaTime);

protected:
	FWindowsWindow* Window = nullptr;

	FName ActiveWorldHandle;
	TArray<FWorldContext> WorldList;

	FTimer* Timer = nullptr;

	FLevelViewportLayout ViewportLayout;

	FRenderer Renderer;

private:
	std::unique_ptr<FInputRouter> InputRouter;
	std::unique_ptr<IRenderPipeline> RenderPipeline;
};

extern UEngine* GEngine;
