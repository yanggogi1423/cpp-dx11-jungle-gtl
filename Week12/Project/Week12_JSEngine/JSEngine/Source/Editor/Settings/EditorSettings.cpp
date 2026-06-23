#include "Editor/Settings/EditorSettings.h"
#include "Component/SkinnedMeshComponent.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <fstream>
#include <filesystem>

namespace EditorKey
{
	// Section
	constexpr const char* Viewport = "Viewport";
	constexpr const char* Paths = "Paths";

	// Splitter / Layout
	constexpr const char* SplitterVRatio      = "SplitterVRatio";
	constexpr const char* SplitterHRatio      = "SplitterHRatio";
	constexpr const char* ActiveViewportCount = "ActiveViewportCount";
	constexpr const char* SingleViewportIndex = "SingleViewportIndex";
	constexpr const char* ViewportLayoutMode  = "ViewportLayoutMode";

	// Viewport
	constexpr const char* CameraSpeed = "CameraSpeed";
	constexpr const char* CameraRotationSpeed = "CameraRotationSpeed";
	constexpr const char* CameraZoomSpeed = "CameraZoomSpeed";
	constexpr const char* CameraMoveSensitivity = "CameraMoveSensitivity";
	constexpr const char* CameraRotateSensitivity = "CameraRotateSensitivity";
	constexpr const char* bEnableCameraSmoothing = "bEnableCameraSmoothing";
	constexpr const char* CameraMoveSmoothSpeed = "CameraMoveSmoothSpeed";
	constexpr const char* CameraRotateSmoothSpeed = "CameraRotateSmoothSpeed";
	constexpr const char* CameraDollySpeedScale = "CameraDollySpeedScale";
	constexpr const char* CameraPanSpeedScale = "CameraPanSpeedScale";
	constexpr const char* InitViewPos = "InitViewPos";
	constexpr const char* InitLookAt = "InitLookAt";

	// View
	constexpr const char* View = "View";
	constexpr const char* ViewMode = "ViewMode";
	constexpr const char* LightCullMode = "LightCullMode";
	constexpr const char* PickingMode = "PickingMode";
	constexpr const char* bPrimitives = "bPrimitives";
	constexpr const char* bSkeletalMesh = "bSkeletalMesh";
	constexpr const char* bParticleSystem = "bParticleSystem";
	constexpr const char* bGrid = "bGrid";
	constexpr const char* bAxis = "bAxis";
	constexpr const char* bGizmo = "bGizmo";
	constexpr const char* bBillboardText = "bBillboardText";
	constexpr const char* bBoundingVolume = "bBoundingVolume";
	constexpr const char* bCollision = "bCollision";
	constexpr const char* bEnableLOD = "bEnableLOD";
	constexpr const char* bBVHBoundingVolume = "bBVHBoundingVolume";
	constexpr const char* bDecals = "bDecals";
	constexpr const char* bFog = "bFog";
    constexpr const char* bShadow = "bShadow";
	constexpr const char* bBloom = "bBloom";
	constexpr const char* BloomThreshold = "BloomThreshold";
	constexpr const char* BloomKnee = "BloomKnee";
	constexpr const char* BloomIntensity = "BloomIntensity";
	constexpr const char* BloomBlurIterations = "BloomBlurIterations";
	constexpr const char* bToneMapping = "bToneMapping";
	constexpr const char* ToneMappingMode = "ToneMappingMode";
	constexpr const char* Exposure = "Exposure";
	constexpr const char* HableWhitePoint = "HableWhitePoint";
	constexpr const char* FXAAEnabled = "FXAAEnabled";
	constexpr const char* FXAAThreshold = "FXAAThreshold"; // Backward compatibility
    constexpr const char* bGammaCorrection = "bGammaCorrection";
	constexpr const char* GammaValue = "GammaValue";
	constexpr const char* ShadowFilterMode = "ShadowFilterMode";
	constexpr const char* SkinningModeOverride = "SkinningModeOverride";

	// Grid
	constexpr const char* Grid = "Grid";
	constexpr const char* GridSpacing = "GridSpacing";
	constexpr const char* GridHalfLineCount = "GridHalfLineCount";

	// Spatial index / BVH maintenance
	constexpr const char* SpatialIndex = "SpatialIndex";
	constexpr const char* BatchRefitMinDirtyCount = "BatchRefitMinDirtyCount";
	constexpr const char* BatchRefitDirtyPercentThreshold = "BatchRefitDirtyPercentThreshold";
	constexpr const char* RotationStructuralChangeThreshold = "RotationStructuralChangeThreshold";
	constexpr const char* RotationDirtyCountThreshold = "RotationDirtyCountThreshold";
	constexpr const char* RotationDirtyPercentThreshold = "RotationDirtyPercentThreshold";

	// Paths
	constexpr const char* DefaultSavePath = "DefaultSavePath";
	constexpr const char* ContentBrowserPath = "ContentBrowserPath";
}

namespace
{
void ApplySkinningModeOverride(int32 Mode)
{
	switch (Mode)
	{
	case 1:
		USkinnedMeshComponent::SetGlobalSkinningModeOverride(ESkinningMode::CPU);
		break;
	case 2:
		USkinnedMeshComponent::SetGlobalSkinningModeOverride(ESkinningMode::GPU);
		break;
	default:
		USkinnedMeshComponent::ClearGlobalSkinningModeOverride();
		break;
	}
}
}

void FEditorSettings::SaveToFile(const FString& Path) const
{
	using namespace json;

	JSON Root = Object();

	// Viewport
	JSON Viewport = Object();
	Viewport[EditorKey::CameraSpeed] = CameraSpeed;
	Viewport[EditorKey::CameraRotationSpeed] = CameraRotationSpeed;
	Viewport[EditorKey::CameraZoomSpeed] = CameraZoomSpeed;
	Viewport[EditorKey::CameraMoveSensitivity] = CameraMoveSensitivity;
	Viewport[EditorKey::CameraRotateSensitivity] = CameraRotateSensitivity;
	Viewport[EditorKey::bEnableCameraSmoothing] = bEnableCameraSmoothing;
	Viewport[EditorKey::CameraMoveSmoothSpeed] = CameraMoveSmoothSpeed;
	Viewport[EditorKey::CameraRotateSmoothSpeed] = CameraRotateSmoothSpeed;
	Viewport[EditorKey::CameraDollySpeedScale] = CameraDollySpeedScale;
	Viewport[EditorKey::CameraPanSpeedScale] = CameraPanSpeedScale;

	JSON InitPos = Array(InitViewPos.X, InitViewPos.Y, InitViewPos.Z);
	Viewport[EditorKey::InitViewPos] = InitPos;

	JSON LookAt = Array(InitLookAt.X, InitLookAt.Y, InitLookAt.Z);
	Viewport[EditorKey::InitLookAt] = LookAt;

	Viewport[EditorKey::SplitterVRatio]      = SplitterVRatio;
	Viewport[EditorKey::SplitterHRatio]      = SplitterHRatio;
	Viewport[EditorKey::ActiveViewportCount] = ActiveViewportCount;
	Viewport[EditorKey::SingleViewportIndex] = SingleViewportIndex;
	Viewport[EditorKey::ViewportLayoutMode]  = ViewportLayoutMode;

	Root[EditorKey::Viewport] = Viewport;

	// View
	JSON ViewObj = Object();
	ViewObj[EditorKey::ViewMode] = static_cast<int32>(ViewMode);
	ViewObj[EditorKey::LightCullMode] = static_cast<int32>(LightCullMode);
	ViewObj[EditorKey::PickingMode] = static_cast<int32>(PickingMode);
	ViewObj[EditorKey::ShadowFilterMode] = static_cast<int32>(ShadowFilterMode);
	ViewObj[EditorKey::SkinningModeOverride] = std::clamp(SkinningModeOverride, 0, 2);
	ViewObj[EditorKey::bPrimitives] = ShowFlags.bPrimitives;
	ViewObj[EditorKey::bSkeletalMesh] = ShowFlags.bSkeletalMesh;
	ViewObj[EditorKey::bParticleSystem] = ShowFlags.bParticleSystem;
	ViewObj[EditorKey::bGrid] = ShowFlags.bGrid;
	ViewObj[EditorKey::bAxis] = ShowFlags.bAxis;
	ViewObj[EditorKey::bGizmo] = ShowFlags.bGizmo;
	ViewObj[EditorKey::bBillboardText] = ShowFlags.bBillboardText;
	ViewObj[EditorKey::bBoundingVolume] = ShowFlags.bBoundingVolume;
	ViewObj[EditorKey::bCollision] = ShowFlags.bCollision;
	ViewObj[EditorKey::bEnableLOD] = ShowFlags.bEnableLOD;
	ViewObj[EditorKey::bBVHBoundingVolume] = ShowFlags.bBVHBoundingVolume;
	ViewObj[EditorKey::bDecals] = ShowFlags.bDecals;
	ViewObj[EditorKey::bFog] = ShowFlags.bFog;
    ViewObj[EditorKey::bShadow] = ShowFlags.bShadow;
	ViewObj[EditorKey::bBloom] = ShowFlags.bBloom;
	ViewObj[EditorKey::BloomThreshold] = ShowFlags.BloomThreshold;
	ViewObj[EditorKey::BloomKnee] = ShowFlags.BloomKnee;
	ViewObj[EditorKey::BloomIntensity] = ShowFlags.BloomIntensity;
	ViewObj[EditorKey::BloomBlurIterations] = ShowFlags.BloomBlurIterations;
	ViewObj[EditorKey::bToneMapping] = ShowFlags.bToneMapping;
	ViewObj[EditorKey::ToneMappingMode] = static_cast<int32>(ShowFlags.ToneMappingMode);
	ViewObj[EditorKey::Exposure] = ShowFlags.Exposure;
	ViewObj[EditorKey::HableWhitePoint] = ShowFlags.HableWhitePoint;
	ViewObj[EditorKey::FXAAEnabled] = bEnableFXAA;
    ViewObj[EditorKey::bGammaCorrection] = ShowFlags.bGammaCorrection;
    ViewObj[EditorKey::GammaValue] = ShowFlags.GammaValue;

	Root[EditorKey::View] = ViewObj;

	// Grid
	JSON GridObj = Object();
	GridObj[EditorKey::GridSpacing] = GridSpacing;
	GridObj[EditorKey::GridHalfLineCount] = GridHalfLineCount;
	Root[EditorKey::Grid] = GridObj;

	// Spatial index / BVH maintenance
	JSON SpatialIndexObj = Object();
	SpatialIndexObj[EditorKey::BatchRefitMinDirtyCount] = SpatialBatchRefitMinDirtyCount;
	SpatialIndexObj[EditorKey::BatchRefitDirtyPercentThreshold] = SpatialBatchRefitDirtyPercentThreshold;
	SpatialIndexObj[EditorKey::RotationStructuralChangeThreshold] = SpatialRotationStructuralChangeThreshold;
	SpatialIndexObj[EditorKey::RotationDirtyCountThreshold] = SpatialRotationDirtyCountThreshold;
	SpatialIndexObj[EditorKey::RotationDirtyPercentThreshold] = SpatialRotationDirtyPercentThreshold;
	Root[EditorKey::SpatialIndex] = SpatialIndexObj;

	// Paths
	JSON PathsObj = Object();
	PathsObj[EditorKey::DefaultSavePath] = DefaultSavePath;
	PathsObj[EditorKey::ContentBrowserPath] = ContentBrowserPath;
	Root[EditorKey::Paths] = PathsObj;

	// Ensure directory exists
	std::filesystem::path FilePath(FPaths::ToWide(Path));
	if (FilePath.has_parent_path())
	{
		std::filesystem::create_directories(FilePath.parent_path());
	}

	std::ofstream File(FilePath);
	if (File.is_open())
	{
		File << Root;
	}
}

void FEditorSettings::LoadFromFile(const FString& Path)
{
	using namespace json;

	std::ifstream File(std::filesystem::path(FPaths::ToWide(Path)));
	if (!File.is_open())
	{
		return;
	}

	FString Content((std::istreambuf_iterator<char>(File)),
		std::istreambuf_iterator<char>());

	JSON Root = JSON::Load(Content);

	// Viewport
	if (Root.hasKey(EditorKey::Viewport))
	{
		JSON Viewport = Root[EditorKey::Viewport];

		if (Viewport.hasKey(EditorKey::CameraSpeed))
			CameraSpeed = static_cast<float>(Viewport[EditorKey::CameraSpeed].ToFloat());
		if (Viewport.hasKey(EditorKey::CameraRotationSpeed))
			CameraRotationSpeed = static_cast<float>(Viewport[EditorKey::CameraRotationSpeed].ToFloat());
		if (Viewport.hasKey(EditorKey::CameraZoomSpeed))
			CameraZoomSpeed = static_cast<float>(Viewport[EditorKey::CameraZoomSpeed].ToFloat());
		if (Viewport.hasKey(EditorKey::CameraMoveSensitivity))
			CameraMoveSensitivity = static_cast<float>(Viewport[EditorKey::CameraMoveSensitivity].ToFloat());
		if (Viewport.hasKey(EditorKey::CameraRotateSensitivity))
			CameraRotateSensitivity = static_cast<float>(Viewport[EditorKey::CameraRotateSensitivity].ToFloat());
		if (Viewport.hasKey(EditorKey::bEnableCameraSmoothing))
			bEnableCameraSmoothing = Viewport[EditorKey::bEnableCameraSmoothing].ToBool();
		if (Viewport.hasKey(EditorKey::CameraMoveSmoothSpeed))
			CameraMoveSmoothSpeed = static_cast<float>(Viewport[EditorKey::CameraMoveSmoothSpeed].ToFloat());
		if (Viewport.hasKey(EditorKey::CameraRotateSmoothSpeed))
			CameraRotateSmoothSpeed = static_cast<float>(Viewport[EditorKey::CameraRotateSmoothSpeed].ToFloat());
		if (Viewport.hasKey(EditorKey::CameraDollySpeedScale))
			CameraDollySpeedScale = std::clamp(static_cast<float>(Viewport[EditorKey::CameraDollySpeedScale].ToFloat()), 0.05f, 5.0f);
		if (Viewport.hasKey(EditorKey::CameraPanSpeedScale))
			CameraPanSpeedScale = std::clamp(static_cast<float>(Viewport[EditorKey::CameraPanSpeedScale].ToFloat()), 0.05f, 10.0f);

		if (Viewport.hasKey(EditorKey::InitViewPos))
		{
			JSON Pos = Viewport[EditorKey::InitViewPos];
			InitViewPos = FVector(
				static_cast<float>(Pos[0].ToFloat()),
				static_cast<float>(Pos[1].ToFloat()),
				static_cast<float>(Pos[2].ToFloat()));
		}

		if (Viewport.hasKey(EditorKey::InitLookAt))
		{
			JSON Look = Viewport[EditorKey::InitLookAt];
			InitLookAt = FVector(
				static_cast<float>(Look[0].ToFloat()),
				static_cast<float>(Look[1].ToFloat()),
				static_cast<float>(Look[2].ToFloat()));
		}

		if (Viewport.hasKey(EditorKey::SplitterVRatio))
			SplitterVRatio = static_cast<float>(Viewport[EditorKey::SplitterVRatio].ToFloat());
		if (Viewport.hasKey(EditorKey::SplitterHRatio))
			SplitterHRatio = static_cast<float>(Viewport[EditorKey::SplitterHRatio].ToFloat());

		if (Viewport.hasKey(EditorKey::ActiveViewportCount))
		{
			const int32 Count = Viewport[EditorKey::ActiveViewportCount].ToInt();
			ActiveViewportCount = (Count == 1) ? 1 : 4;  // 1 또는 4만 유효
		}
		if (Viewport.hasKey(EditorKey::SingleViewportIndex))
		{
			const int32 Idx = Viewport[EditorKey::SingleViewportIndex].ToInt();
			SingleViewportIndex = (Idx >= 0 && Idx < 4) ? Idx : 0;
		}
		if (Viewport.hasKey(EditorKey::ViewportLayoutMode))
		{
			const int32 Mode = Viewport[EditorKey::ViewportLayoutMode].ToInt();
			ViewportLayoutMode = (Mode >= 0 && Mode < 12) ? Mode : ((ActiveViewportCount == 1) ? 0 : 7);
		}
		else
		{
			ViewportLayoutMode = (ActiveViewportCount == 1) ? 0 : 7;
		}
	}

	// View
	if (Root.hasKey(EditorKey::View))
	{
		JSON ViewObj = Root[EditorKey::View];

		if (ViewObj.hasKey(EditorKey::ViewMode))
		{
			int32 Mode = ViewObj[EditorKey::ViewMode].ToInt();
			if (Mode >= 0 && Mode < static_cast<int32>(EViewMode::Count))
				ViewMode = static_cast<EViewMode>(Mode);
		}
		if (ViewObj.hasKey(EditorKey::LightCullMode))
		{
			const int32 Mode = ViewObj[EditorKey::LightCullMode].ToInt();
			switch (static_cast<ELightCullMode>(Mode))
			{
			case ELightCullMode::None:
			case ELightCullMode::Clustered:
			case ELightCullMode::Tiled:
				LightCullMode = static_cast<ELightCullMode>(Mode);
				break;
			default:
				LightCullMode = ELightCullMode::Clustered;
				break;
			}
		}
		if (ViewObj.hasKey(EditorKey::PickingMode))
		{
			int32 Mode = ViewObj[EditorKey::PickingMode].ToInt();
			if (Mode >= 0 && Mode < static_cast<int32>(EEditorPickingMode::Count))
				PickingMode = static_cast<EEditorPickingMode>(Mode);
		}
		if (ViewObj.hasKey(EditorKey::ShadowFilterMode))
		{
			const int32 Mode = ViewObj[EditorKey::ShadowFilterMode].ToInt();
			ShadowFilterMode = Mode == static_cast<int32>(EShadowFilter::VSM) ? EShadowFilter::VSM : EShadowFilter::PCF;
		}
		if (ViewObj.hasKey(EditorKey::SkinningModeOverride))
		{
			SkinningModeOverride = std::clamp(static_cast<int32>(ViewObj[EditorKey::SkinningModeOverride].ToInt()), 0, 2);
			ApplySkinningModeOverride(SkinningModeOverride);
		}
		if (ViewObj.hasKey(EditorKey::bPrimitives))
			ShowFlags.bPrimitives = ViewObj[EditorKey::bPrimitives].ToBool();
		if (ViewObj.hasKey(EditorKey::bSkeletalMesh))
			ShowFlags.bSkeletalMesh = ViewObj[EditorKey::bSkeletalMesh].ToBool();
		if (ViewObj.hasKey(EditorKey::bParticleSystem))
			ShowFlags.bParticleSystem = ViewObj[EditorKey::bParticleSystem].ToBool();
		if (ViewObj.hasKey(EditorKey::bGrid))
			ShowFlags.bGrid = ViewObj[EditorKey::bGrid].ToBool();
		if (ViewObj.hasKey(EditorKey::bAxis))
			ShowFlags.bAxis = ViewObj[EditorKey::bAxis].ToBool();
		if (ViewObj.hasKey(EditorKey::bGizmo))
			ShowFlags.bGizmo = ViewObj[EditorKey::bGizmo].ToBool();
		if (ViewObj.hasKey(EditorKey::bBillboardText))
			ShowFlags.bBillboardText = ViewObj[EditorKey::bBillboardText].ToBool();
		if (ViewObj.hasKey(EditorKey::bBoundingVolume))
			ShowFlags.bBoundingVolume = ViewObj[EditorKey::bBoundingVolume].ToBool();
		if (ViewObj.hasKey(EditorKey::bCollision))
			ShowFlags.bCollision = ViewObj[EditorKey::bCollision].ToBool();
		if (ViewObj.hasKey(EditorKey::bEnableLOD))
			ShowFlags.bEnableLOD = ViewObj[EditorKey::bEnableLOD].ToBool();
		if (ViewObj.hasKey(EditorKey::bBVHBoundingVolume))
            ShowFlags.bBVHBoundingVolume = ViewObj[EditorKey::bBVHBoundingVolume].ToBool();
		if (ViewObj.hasKey(EditorKey::bDecals))
			ShowFlags.bDecals = ViewObj[EditorKey::bDecals].ToBool();
		if (ViewObj.hasKey(EditorKey::bFog))
			ShowFlags.bFog = ViewObj[EditorKey::bFog].ToBool();
        if (ViewObj.hasKey(EditorKey::bShadow))
            ShowFlags.bShadow = ViewObj[EditorKey::bShadow].ToBool(); 
		if (ViewObj.hasKey(EditorKey::bBloom))
			ShowFlags.bBloom = ViewObj[EditorKey::bBloom].ToBool();
		if (ViewObj.hasKey(EditorKey::BloomThreshold))
			ShowFlags.BloomThreshold = std::max(0.0f, static_cast<float>(ViewObj[EditorKey::BloomThreshold].ToFloat()));
		if (ViewObj.hasKey(EditorKey::BloomKnee))
			ShowFlags.BloomKnee = std::max(0.0f, static_cast<float>(ViewObj[EditorKey::BloomKnee].ToFloat()));
		if (ViewObj.hasKey(EditorKey::BloomIntensity))
			ShowFlags.BloomIntensity = std::max(0.0f, static_cast<float>(ViewObj[EditorKey::BloomIntensity].ToFloat()));
		if (ViewObj.hasKey(EditorKey::BloomBlurIterations))
			ShowFlags.BloomBlurIterations = std::clamp<int32>(
				static_cast<int32>(ViewObj[EditorKey::BloomBlurIterations].ToInt()),
				0,
				8);
		if (ViewObj.hasKey(EditorKey::bToneMapping))
			ShowFlags.bToneMapping = ViewObj[EditorKey::bToneMapping].ToBool();
		if (ViewObj.hasKey(EditorKey::ToneMappingMode))
		{
			const int32 Mode = std::clamp<int32>(
				static_cast<int32>(ViewObj[EditorKey::ToneMappingMode].ToInt()),
				0,
				static_cast<int32>(EToneMappingMode::Count) - 1);
			ShowFlags.ToneMappingMode = static_cast<EToneMappingMode>(Mode);
		}
		if (ViewObj.hasKey(EditorKey::Exposure))
			ShowFlags.Exposure = std::max(0.0f, static_cast<float>(ViewObj[EditorKey::Exposure].ToFloat()));
		if (ViewObj.hasKey(EditorKey::HableWhitePoint))
			ShowFlags.HableWhitePoint = std::max(0.001f, static_cast<float>(ViewObj[EditorKey::HableWhitePoint].ToFloat()));
		if (ViewObj.hasKey(EditorKey::bGammaCorrection))
            ShowFlags.bGammaCorrection = ViewObj[EditorKey::bGammaCorrection].ToBool();
		if (ViewObj.hasKey(EditorKey::GammaValue))
            ShowFlags.GammaValue = static_cast<float>(ViewObj[EditorKey::GammaValue].ToFloat());
		if (ViewObj.hasKey(EditorKey::FXAAEnabled))
			bEnableFXAA = ViewObj[EditorKey::FXAAEnabled].ToBool();
		else if (ViewObj.hasKey(EditorKey::FXAAThreshold))
		{
			const float LegacyThreshold = std::clamp(static_cast<float>(ViewObj[EditorKey::FXAAThreshold].ToFloat()), 0.0f, 1.0f);
			bEnableFXAA = (LegacyThreshold > 0.0f);
		}
	}

	// Grid
	if (Root.hasKey(EditorKey::Grid))
	{
		JSON GridObj = Root[EditorKey::Grid];

		if (GridObj.hasKey(EditorKey::GridSpacing))
			GridSpacing = static_cast<float>(GridObj[EditorKey::GridSpacing].ToFloat());
		if (GridObj.hasKey(EditorKey::GridHalfLineCount))
			GridHalfLineCount = GridObj[EditorKey::GridHalfLineCount].ToInt();
	}

	// Spatial index / BVH maintenance
	if (Root.hasKey(EditorKey::SpatialIndex))
	{
		JSON SpatialIndexObj = Root[EditorKey::SpatialIndex];

		if (SpatialIndexObj.hasKey(EditorKey::BatchRefitMinDirtyCount))
		{
			const int32 Value = static_cast<int32>(SpatialIndexObj[EditorKey::BatchRefitMinDirtyCount].ToInt());
			SpatialBatchRefitMinDirtyCount = std::max<int32>(1, Value);
		}
		if (SpatialIndexObj.hasKey(EditorKey::BatchRefitDirtyPercentThreshold))
		{
			const int32 Value =
				static_cast<int32>(SpatialIndexObj[EditorKey::BatchRefitDirtyPercentThreshold].ToInt());
			SpatialBatchRefitDirtyPercentThreshold =
				std::clamp<int32>(Value, 1, 100);
		}
		if (SpatialIndexObj.hasKey(EditorKey::RotationStructuralChangeThreshold))
		{
			const int32 Value =
				static_cast<int32>(SpatialIndexObj[EditorKey::RotationStructuralChangeThreshold].ToInt());
			SpatialRotationStructuralChangeThreshold =
				std::max<int32>(1, Value);
		}
		if (SpatialIndexObj.hasKey(EditorKey::RotationDirtyCountThreshold))
		{
			const int32 Value = static_cast<int32>(SpatialIndexObj[EditorKey::RotationDirtyCountThreshold].ToInt());
			SpatialRotationDirtyCountThreshold =
				std::max<int32>(1, Value);
		}
		if (SpatialIndexObj.hasKey(EditorKey::RotationDirtyPercentThreshold))
		{
			const int32 Value =
				static_cast<int32>(SpatialIndexObj[EditorKey::RotationDirtyPercentThreshold].ToInt());
			SpatialRotationDirtyPercentThreshold =
				std::clamp<int32>(Value, 1, 100);
		}
	}

	// Paths
	if (Root.hasKey(EditorKey::Paths))
	{
		JSON PathsObj = Root[EditorKey::Paths];

		if (PathsObj.hasKey(EditorKey::DefaultSavePath))
			DefaultSavePath = PathsObj[EditorKey::DefaultSavePath].ToString();
		if (PathsObj.hasKey(EditorKey::ContentBrowserPath))
			ContentBrowserPath = PathsObj[EditorKey::ContentBrowserPath].ToString();
	}
}
