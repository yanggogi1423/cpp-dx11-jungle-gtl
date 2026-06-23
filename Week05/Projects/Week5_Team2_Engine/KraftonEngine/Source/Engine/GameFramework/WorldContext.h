#pragma once
#include "Core/CoreTypes.h"
#include "Object/FName.h"
#include "World.h"

enum class EWorldType : uint32
{
    Editor,			// Editor mode — no BeginPlay
	EditorPreview,	// Actor preview mode - NOT IMPLEMENTED
    PIE,			// Play In Editor
    Game,			// Game mode — BeginPlay/Tick active
};

struct FWorldContext
{
    EWorldType WorldType = EWorldType::Editor;
    UWorld* World = nullptr;
    FString ContextName;
    FName ContextHandle;
	
	FWorldContext Duplicate()
	{
		FWorldContext Duplicated;
		Duplicated.WorldType = WorldType;
		Duplicated.World = World->Duplicate();
		Duplicated.ContextName = ContextName + "_PIE";
		Duplicated.ContextHandle = FName(Duplicated.ContextName);
		return Duplicated;
	}
};
