#pragma once

#include "Core/CoreTypes.h"

enum class EViewMode : int32
{
	Lit_Gouraud,
	Lit_Lambert,
	Lit_BlinnPhong,
	Unlit,
	Heatmap,
	BoneWeightHeatmap,
	Wireframe,
	Normal,
	Depth,
	IdBuffer,
	Count
};

enum class ELightCullMode : int32
{
	None,       // iterate all lights
	Clustered,  // clustered light culling
	Tiled,		// Tiled light culling
};

enum class EToneMappingMode : int32
{
	Linear,
	Reinhard,
	ACES,
	Hable,
	Count
};

struct FShowFlags
{
	bool bPrimitives = true;
	bool bSkeletalMesh = true;
	bool bParticleSystem = true;
	bool bGrid = true;
	bool bAxis = true;
	bool bGizmo = true;
	bool bBillboardText = false;
	bool bBoundingVolume = false;
	bool bCollision = false;
	bool bBVHBoundingVolume = false;
	bool bEnableLOD = true;
	bool bDecals = true;
	bool bFog = true;
	bool bShadow = true;
	bool bBloom = false;
	float BloomThreshold = 1.0f;
	float BloomKnee = 0.2f;
	float BloomIntensity = 0.6f;
	int32 BloomBlurIterations = 2;
	bool bToneMapping = true;
	EToneMappingMode ToneMappingMode = EToneMappingMode::ACES;
	float Exposure = 1.0f;
	float HableWhitePoint = 11.2f;
	bool bGammaCorrection = false;
	float GammaValue = 2.2f;
};
