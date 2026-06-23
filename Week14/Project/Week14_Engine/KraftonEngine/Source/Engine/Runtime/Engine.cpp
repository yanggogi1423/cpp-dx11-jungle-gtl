#include "Engine/Runtime/Engine.h"

#include "Platform/Paths.h"
#include "Core/Logging/Log.h"
#include "Core/Logging/Notification.h"
#include "Engine/Platform/DirectoryWatcher.h"
#include "Profiling/Stats/Stats.h"
#include "Profiling/Stats/ParticleStats.h"
#include "Profiling/Stats/ClothCollisionStats.h"
#include "Profiling/StartupProfiler.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Platform/WindowsWindow.h"
#include "Resource/ResourceManager.h"
#include "Render/Pipeline/DefaultRenderPipeline.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Mesh/MeshManager.h"
#include "Texture/Texture2D.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "Viewport/GameViewportClient.h"
#include "Core/TickFunction.h"
#include "Lua/LuaScriptManager.h"
#include "UI/UIManager.h"
#include "Audio/AudioManager.h"
#include "Object/GarbageCollection.h"
#include "LuaBlueprint/LuaBlueprintManager.h"
#include "FloatCurve/FloatCurveManager.h"
#include "CameraShake/CameraShakeManager.h"
#include "Particle/ParticleSystemManager.h"
#include "Animation/Graph/AnimGraphManager.h"
#include "Animation/Skeleton/SkeletonManager.h"
#include "Animation/AnimationManager.h"
#include "Materials/MaterialManager.h"
#include "Physics/PhysicsAssetManager.h"
#include "Serialization/SceneSaveManager.h"

UEngine* GEngine = nullptr;

namespace
{
	std::filesystem::path WithSceneExtension(std::filesystem::path Path)
	{
		if (!Path.has_extension())
		{
			Path += FSceneSaveManager::SceneExtension;
		}
		return Path;
	}

	void AddScenePathCandidate(TArray<std::filesystem::path>& OutCandidates, std::filesystem::path Candidate)
	{
		Candidate = Candidate.lexically_normal();
		for (const std::filesystem::path& Existing : OutCandidates)
		{
			if (Existing == Candidate)
			{
				return;
			}
		}
		OutCandidates.push_back(Candidate);
	}

	ELevelTick ToLevelTickType(EWorldType WorldType)
	{
		switch (WorldType)
		{
		case EWorldType::Editor:
		case EWorldType::EditorPreview:
			return ELevelTick::LEVELTICK_ViewportsOnly;
		case EWorldType::PIE:
		case EWorldType::Game:
			return ELevelTick::LEVELTICK_All;
		default:
			return ELevelTick::LEVELTICK_TimeOnly;
		}
	}
}

void UEngine::AddReferencedObjects(FReferenceCollector& Collector)
{
	UObject::AddReferencedObjects(Collector);
	for (FWorldContext& Context : WorldList)
	{
		Collector.AddReferencedObject(Context.World, "WorldContext.World");
	}
	Collector.AddReferencedObject(GameViewportClient, "GameViewportClient");
}


void UEngine::Init(FWindowsWindow* InWindow)
{
	AddToRoot();
	Window = InWindow;

	// 싱글턴 초기화 순서 보장
	FNamePool::Get();
	FObjectFactory::Get();

	InputSystem::Get().SetOwnerWindow(Window->GetHWND());

	{
		SCOPE_STARTUP_STAT("Renderer::Create");
		Renderer.Create(Window->GetHWND());
	}

	ID3D11Device* Device = Renderer.GetFD3DDevice().GetDevice();

	{
		SCOPE_STARTUP_STAT("MeshBufferManager::Init");
		FMeshBufferManager::Get().Initialize(Device);
	}

	{
		SCOPE_STARTUP_STAT("ResourceManager::LoadFromFile");
		FResourceManager::Get().LoadFromFile(FPaths::ToUtf8(FPaths::ResourceFilePath()), Device);
	}

	{
		SCOPE_STARTUP_STAT("RenderPipeline::Create");
		SetRenderPipeline(std::make_unique<FDefaultRenderPipeline>(this, Renderer));
	}

	UUIManager::Get().Initialize(Device);

	FLogManager::Get().Initialize();
	FDirectoryWatcher::Get().Initialize();
	FLuaScriptManager::Initialize();
	FAudioManager::Get().Initialize();
}

void UEngine::Shutdown()
{
	while (!WorldList.empty())
	{
		DestroyWorldContext(WorldList.back().ContextHandle);
	}

	// UI 가 Lua callback (FWidgetClickEventListener::Callback 의 sol::protected_function 등)
	// 을 보유하므로, 위젯 destroy 시점에 lua_State 가 살아있어야 deref 가 안전.
	// 따라서 UIManager → LuaScriptManager 순서.
	UUIManager::Get().Shutdown();
	FLuaScriptManager::Shutdown();
	FAudioManager::Get().Shutdown();
	FDirectoryWatcher::Get().Shutdown();
	FLogManager::Get().Shutdown();

	// Any render pass/UI/viewport can leave resources bound on the immediate context.
	// Detach before tearing down GPU resource owners so COM refcounts and debug-layer state
	// do not keep stale bindings alive until FD3DDevice::Release().
	Renderer.GetFD3DDevice().ReleaseImmediateContextBindings(false);
	RenderPipeline.reset();
	Renderer.GetFD3DDevice().ReleaseImmediateContextBindings(false);
	FResourceManager::Get().ReleaseGPUResources();
	UTexture2D::ReleaseAllGPU();
	FMeshManager::ReleaseAllGPU();
	FMaterialManager::Get().Release();

	// PhysicsAssetManager is also an FGCObject root. If this cache is left alive until
	// process teardown, loaded PhysicsAsset data stays referenced and CRT reports it
	// as a leak, especially after opening/simulating in the Physics Asset editor.
	FPhysicsAssetManager::Get().ClearCache();

	FAnimationManager::Get().ClearCache();
	FSkeletonManager::Get().ClearCache();
	FAnimGraphManager::Get().ClearCache();
	FLuaBlueprintManager::Get().ClearCache();
	FParticleSystemManager::Get().ClearCache();
	FCameraShakeManager::Get().ClearCache();
	FFloatCurveManager::Get().ClearCache();

	FMeshBufferManager::Get().Release();
	Renderer.Release();
	RemoveFromRoot();
}

FString UEngine::ResolveRuntimeScenePath(const FString& InNameOrPath) const
{
	if (InNameOrPath.empty())
	{
		return {};
	}

	const std::filesystem::path Input(FPaths::ToWide(InNameOrPath));
	TArray<std::filesystem::path> Candidates;

	if (Input.is_absolute())
	{
		AddScenePathCandidate(Candidates, Input);
		AddScenePathCandidate(Candidates, WithSceneExtension(Input));
	}
	else
	{
		const std::filesystem::path RootDir(FPaths::RootDir());
		const std::filesystem::path SceneDir(FSceneSaveManager::GetSceneDirectory());

		AddScenePathCandidate(Candidates, RootDir / Input);
		AddScenePathCandidate(Candidates, WithSceneExtension(RootDir / Input));

		AddScenePathCandidate(Candidates, SceneDir / Input);
		AddScenePathCandidate(Candidates, WithSceneExtension(SceneDir / Input));

		// Lua/gameplay code often passes "Content/Scene/Foo.Scene". If that exact
		// root-relative path is unavailable, never build
		// "Content/Scene/Content/Scene/Foo.Scene"; fall back to the scene stem.
		AddScenePathCandidate(Candidates, SceneDir / Input.filename());
		AddScenePathCandidate(Candidates, WithSceneExtension(SceneDir / Input.filename()));
	}

	for (const std::filesystem::path& Candidate : Candidates)
	{
		if (std::filesystem::exists(Candidate))
		{
			return FPaths::ToUtf8(Candidate.wstring());
		}
	}

	return Candidates.empty() ? InNameOrPath : FPaths::ToUtf8(Candidates.front().wstring());
}

bool UEngine::DoesRuntimeSceneExist(const FString& InNameOrPath) const
{
	const FString Resolved = ResolveRuntimeScenePath(InNameOrPath);
	return !Resolved.empty() && std::filesystem::exists(std::filesystem::path(FPaths::ToWide(Resolved)));
}

void UEngine::BeginPlay()
{
	FWorldContext* Context = GetWorldContextFromHandle(ActiveWorldHandle);
	if (Context && Context->World)
	{
		if (Context->WorldType == EWorldType::Game || Context->WorldType == EWorldType::PIE)
		{
			Context->World->BeginPlay();
		}
	}
}

void UEngine::TickFrameStart(float DeltaTime)
{
	FDirectoryWatcher::Get().ProcessChanges();
	FNotificationManager::Get().Tick(DeltaTime);
	InputSystem::Get().Tick();
	UUIManager::Get().BeginInputFrame();
}

void UEngine::TickFrameBody(float DeltaTime)
{
	FAudioManager::Get().Tick();
	WorldTick(DeltaTime);
	Render(DeltaTime);
}

void UEngine::ProcessActiveWorldPlayerInput(const FInputSystemSnapshot& Snapshot, float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || World->IsPaused())
	{
		return;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return;
	}

	PlayerController->ProcessPlayerInput(Snapshot, DeltaTime);
}

void UEngine::Tick(float DeltaTime)
{
	TickFrameStart(DeltaTime);
	TickFrameBody(DeltaTime);
}

void UEngine::Render(float DeltaTime)
{
	if (RenderPipeline)
	{
		SCOPE_STAT_CAT("UEngine::Render", "2_Render");
		RenderPipeline->Execute(DeltaTime, Renderer);
	}
}

void UEngine::SetRenderPipeline(std::unique_ptr<IRenderPipeline> InPipeline)
{
	RenderPipeline = std::move(InPipeline);
}

void UEngine::OnWindowResized(uint32 Width, uint32 Height)
{
	if (Width == 0 || Height == 0)
	{
		return;
	}

	Renderer.GetFD3DDevice().OnResizeViewport(Width, Height);
	Renderer.ResetRenderStateCache();
}

void UEngine::WorldTick(float DeltaTime)
{
	SCOPE_STAT_CAT("UEngine::WorldTick", "1_WorldTick");

	// 파티클 stat은 GT tick 중(PSC TickComponent / emitter Spawn·Update) 누적되므로,
	// 월드 tick 루프 진입 전 여기서 프레임 카운터를 리셋한다. (Peak은 유지)
	PARTICLE_STATS_RESET();
	CLOTH_COLLISION_STATS_RESET();

	// PIE 활성 시 Editor 월드는 sleep (UE 동작과 동일).
	// culling/octree/visibility 갱신을 건너뛰어 50k+ 환경에서 비용 2배를 방지.
	bool bHasPIEWorld = false;
	for (const FWorldContext& Ctx : WorldList)
	{
		if (Ctx.WorldType == EWorldType::PIE && Ctx.World)
		{
			bHasPIEWorld = true;
			break;
		}
	}

	// 월드 타입별 Tick 라우팅:
	// - Editor: bTickInEditor 액터만 TickManager 대상
	// - PIE/Game: BeginPlay 이후 bNeedsTick 액터만 TickManager 대상
	// - 기타:   시간 갱신만 유지
	for (FWorldContext& Ctx : WorldList)
	{
		UWorld* World = Ctx.World;
		if (!World) continue;

		// PIE 활성 시 Editor 월드는 완전히 skip
		if (bHasPIEWorld && Ctx.WorldType == EWorldType::Editor)
		{
			continue;
		}

		const ELevelTick TickType = ToLevelTickType(Ctx.WorldType);

		// 월드 단위 업데이트 (FlushPrimitive / VisibleProxies / DebugDraw /s TickManager)
		World->Tick(DeltaTime, TickType);
	}
}

UWorld* UEngine::GetWorld() const
{
	const FWorldContext* Context = GetWorldContextFromHandle(ActiveWorldHandle);
	return Context ? Context->World : nullptr;
}

FWorldContext& UEngine::CreateWorldContext(EWorldType Type, const FName& Handle, const FString& Name)
{
	FWorldContext Context;
	Context.WorldType = Type;
	Context.ContextHandle = Handle;
	Context.ContextName = Name.empty() ? Handle.ToString() : Name;
	Context.World = UObjectManager::Get().CreateObject<UWorld>();
	WorldList.push_back(Context);
	return WorldList.back();
}

void UEngine::DestroyWorldContext(const FName& Handle)
{
	for (auto it = WorldList.begin(); it != WorldList.end(); ++it)
	{
		if (it->ContextHandle == Handle)
		{
			if (ActiveWorldHandle == Handle)
			{
				ActiveWorldHandle = FName::None;
				for (const FWorldContext& Ctx : WorldList)
				{
					if (Ctx.ContextHandle != Handle)
					{
						ActiveWorldHandle = Ctx.ContextHandle;
						break;
					}
				}
			}

			if (it->World)
			{
				it->World->RouteWorldDestroyed();
				UObjectManager::Get().DestroyObject(it->World);
			}
			WorldList.erase(it);
			return;
		}
	}
}

FWorldContext* UEngine::GetWorldContextFromHandle(const FName& Handle)
{
	for (FWorldContext& Ctx : WorldList)
	{
		if (Ctx.ContextHandle == Handle)
		{
			return &Ctx;
		}
	}
	return nullptr;
}

const FWorldContext* UEngine::GetWorldContextFromHandle(const FName& Handle) const
{
	for (const FWorldContext& Ctx : WorldList)
	{
		if (Ctx.ContextHandle == Handle)
		{
			return &Ctx;
		}
	}
	return nullptr;
}

FWorldContext* UEngine::GetWorldContextFromWorld(const UWorld* World)
{
	for (FWorldContext& Ctx : WorldList)
	{
		if (Ctx.World == World)
		{
			return &Ctx;
		}
	}
	return nullptr;
}

void UEngine::SetActiveWorld(const FName& Handle)
{
	ActiveWorldHandle = Handle;
}
