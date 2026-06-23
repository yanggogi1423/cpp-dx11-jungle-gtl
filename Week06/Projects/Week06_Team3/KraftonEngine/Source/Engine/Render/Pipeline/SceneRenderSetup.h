#pragma once

#include "Render/Pipeline/RenderBus.h"
#include "GameFramework/World.h"

inline void PopulateScenePostProcessConstants(const UWorld* World, FRenderBus& Bus)
{
	if (!World)
	{
		Bus.SetSceneEffectConstants({});
		Bus.SetFogPostProcessConstants({});
		return;
	}

	const FScene& Scene = World->GetScene();
	Bus.SetSceneEffectConstants(Scene.GetSceneEffectConstants());
	Bus.SetFogPostProcessConstants(Scene.GetFogPostProcessConstants());
}
