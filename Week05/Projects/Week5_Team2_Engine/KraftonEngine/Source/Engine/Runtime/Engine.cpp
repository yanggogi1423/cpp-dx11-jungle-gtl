#include "Engine/Runtime/Engine.h"

#include "Platform/Paths.h"
#include "Profiling/Stats.h"
#include "Engine/Input/InputRouter.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Resource/ResourceManager.h"
#include "Render/Pipeline/DefaultRenderPipeline.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Mesh/ObjManager.h"
#include "GameFramework/World.h"

DEFINE_CLASS(UEngine, UObject)

UEngine* GEngine = nullptr;

UEngine::~UEngine() = default;

void UEngine::Init(FWindowsWindow* InWindow)
{
	Window = InWindow;

	// 싱글턴 초기화 순서 보장
	FNamePool::Get();
	FObjectFactory::Get();

	InputRouter = std::make_unique<FInputRouter>();
	InputRouter->SetOwnerWindow(Window->GetHWND());
	Renderer.Create(Window->GetHWND());

	ID3D11Device* Device = Renderer.GetFD3DDevice().GetDevice();
	FMeshBufferManager::Get().Initialize(Device);
	FResourceManager::Get().LoadFromFile(FPaths::ToUtf8(FPaths::ResourceFilePath()), Device);

	SetRenderPipeline(std::make_unique<FDefaultRenderPipeline>(this, Renderer));
}

void UEngine::Shutdown()
{
	InputRouter.reset();
	RenderPipeline.reset();
	FResourceManager::Get().ReleaseGPUResources();
	FMeshBufferManager::Get().Release();
	Renderer.Release();
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

void UEngine::Tick(float DeltaTime)
{
	SCOPE_STAT("Frame");
	DispatchInput();
	{
		SCOPE_STAT("Frame.WorldTick");
		WorldTick(DeltaTime);
	}
	{
		SCOPE_STAT("Frame.Render");
		Render(DeltaTime);
	}
}

void UEngine::SetImGuiInputCapture(bool bCaptureMouse, bool bCaptureKeyboard)
{
	if (!InputRouter)
	{
		return;
	}

	InputRouter->SetImGuiCaptureState(bCaptureMouse, bCaptureKeyboard);
}

void UEngine::ClearInputTargets()
{
	if (!InputRouter)
	{
		return;
	}

	InputRouter->ClearTargets();
}

void UEngine::RegisterInputTarget(
	FViewport* InViewport,
	FViewportClient* InClient,
	EInputRouteDomain InDomain,
	const std::function<bool(FRect&)>& InRectProvider)
{
	if (!InputRouter)
	{
		return;
	}

	FViewportClient* RoutedClient = ResolveInputTargetClient(InViewport, InClient);
	if (!RoutedClient)
	{
		return;
	}

	InputRouter->RegisterTarget(InViewport, RoutedClient, InDomain, InRectProvider);
}

FViewportClient* UEngine::ResolveInputTargetClient(FViewport* InViewport, FViewportClient* InClient) const
{
	(void)InViewport;
	return InClient;
}

void UEngine::DispatchInput()
{
	if (!InputRouter)
	{
		return;
	}

	InputRouter->Tick();
}

void UEngine::Render(float DeltaTime)
{
	if (RenderPipeline)
	{
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
}

void UEngine::WorldTick(float DeltaTime)
{
	const FWorldContext* Context = GetWorldContextFromHandle(ActiveWorldHandle);
	if (Context == nullptr) return;
	
	UWorld* World = GetWorld();
	
	// Editor Tick
	if (Context->WorldType == EWorldType::Editor)
	{
		ULevel* ActiveLevel = World->GetActiveLevel();
		{
			for (auto Actor : ActiveLevel->GetActors())
			{
				if (Actor->IsTickInEditor())
					Actor->Tick(DeltaTime);
			}
		}
		ULevel* PersistentLevel = World->GetPersistentLevel();
		{
			for (auto Actor : PersistentLevel->GetActors())
			{
				if (Actor->IsTickInEditor())
					Actor->Tick(DeltaTime);
			}
		}
	}
	// PIE Tick
	else if (Context->WorldType == EWorldType::PIE)
	{
		ULevel* ActiveLevel = World->GetActiveLevel();
		{
			for (AActor* Actor : ActiveLevel->GetActors())
			{
				Actor->Tick(DeltaTime);
			}
		}
		ULevel* PersistentLevel = World->GetPersistentLevel();
		{
			for (AActor* Actor : PersistentLevel->GetActors())
			{
				Actor->Tick(DeltaTime);
			}
		}
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
			it->World->EndPlay();
			UObjectManager::Get().DestroyObject(it->World);
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
