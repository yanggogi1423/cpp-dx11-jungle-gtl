#include "Editor/Viewport/EditorPickingService.h"

#include "Camera/ViewportCamera.h"
#include "Component/PrimitiveComponent.h"
#include "Core/CollisionTypes.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewport/FSceneViewport.h"
#include "EditorEngine.h"
#include "GameFramework/World.h"
#include "Render/Renderer/Renderer.h"

#include <algorithm>
#include <cmath>

bool FEditorPickingService::ResolveActorForSelection(
	UWorld* World,
	const FViewportCamera* Camera,
	FSceneViewport* Viewport,
	UEditorEngine* Editor,
	float LocalX,
	float LocalY,
	AActor*& OutActor)
{
	OutActor = nullptr;
	const FEditorSettings& EditorSettings = FEditorSettings::Get();
	if (EditorSettings.PickingMode == EEditorPickingMode::IdBuffer &&
		PickActorByIdAtViewportLocalPoint(Viewport, Editor, LocalX, LocalY, OutActor) &&
		OutActor)
	{
		return true;
	}

	// return PickActorByRayAtViewportLocalPoint(World, Camera, Viewport, LocalX, LocalY, OutActor);
	return false;
}

bool FEditorPickingService::PickActorByIdAtViewportLocalPoint(
	FSceneViewport* Viewport,
	UEditorEngine* Editor,
	float LocalX,
	float LocalY,
	AActor*& OutActor)
{
	OutActor = nullptr;
	if (!Viewport || !Editor)
	{
		return false;
	}

	ID3D11DeviceContext* Context = Editor->GetRenderer().GetFD3DDevice().GetDeviceContext();
	if (!Context)
	{
		return false;
	}

	const int32 BaseX = static_cast<int32>(std::round(LocalX));
	const int32 BaseY = static_cast<int32>(std::round(LocalY));
	static constexpr POINT SampleOffsets[] =
	{
		{ 0, 0 },
		{ 1, 0 },
		{ -1, 0 },
		{ 0, 1 },
		{ 0, -1 }
	};

	for (const POINT& Offset : SampleOffsets)
	{
		uint32 PickId = 0;
		const int32 SampleX = BaseX + static_cast<int32>(Offset.x);
		const int32 SampleY = BaseY + static_cast<int32>(Offset.y);
		const uint32 X = static_cast<uint32>(std::max<int32>(0, SampleX));
		const uint32 Y = static_cast<uint32>(std::max<int32>(0, SampleY));
		if (!Viewport->ReadEditorIdPickAt(X, Y, Context, PickId) || PickId == 0)
		{
			continue;
		}

		OutActor = Viewport->GetEditorIdPickActor(PickId);
		return OutActor != nullptr;
	}

	return true;
}

bool FEditorPickingService::PickActorByRayAtViewportLocalPoint(
	UWorld* World,
	const FViewportCamera* Camera,
	FSceneViewport* Viewport,
	float LocalX,
	float LocalY,
	AActor*& OutActor)
{
	OutActor = nullptr;
	if (!World || !Camera || !Viewport)
	{
		return false;
	}

	const FViewportRect& Rect = Viewport->GetRect();
	if (Rect.Width <= 0 || Rect.Height <= 0)
	{
		return false;
	}

	static constexpr float SelectionTraceDistance = 1000000.0f;
	const FRay Ray = Camera->DeprojectScreenToWorld(LocalX, LocalY, static_cast<float>(Rect.Width), static_cast<float>(Rect.Height));
	FHitResult HitResult{};
	if (World->LineTraceSingle(Ray.Origin, Ray.Origin + Ray.Direction * SelectionTraceDistance, HitResult))
	{
		UPrimitiveComponent* PrimitiveComp = HitResult.HitComponent;
		AActor* Actor = PrimitiveComp ? PrimitiveComp->GetOwner() : nullptr;
		if (Actor && Actor->GetRootComponent())
		{
			OutActor = Actor;
		}
	}

	return true;
}
