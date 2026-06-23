#pragma once

#include "Core/ProjectionDecalTypes.h"

class UProjectionDecalComponent;
class UWorld;

class FProjectionDecalMeshBuilder
{
public:
	static void BuildRenderableMesh(const UProjectionDecalComponent& ProjectionDecalComponent, const UWorld& World, FProjectionDecalRenderableMesh& OutMesh);
};


