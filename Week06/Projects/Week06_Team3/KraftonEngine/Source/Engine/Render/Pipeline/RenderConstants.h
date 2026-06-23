#pragma once
#include "Render/Types/RenderTypes.h"
#include "Render/Resource/Buffer.h"
#include "Render/Device/D3DDevice.h"
#include "Core/EngineTypes.h"
#include "Core/ResourceTypes.h"

#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Render/Fog/FogRenderTypes.h"
#include <cstddef>
#include <cstring>

class FShader;

/*
	GPU Constant Buffer 구조체, Batcher Entry, 섹션별 드로우 정보 등
	렌더링에 필요한 데이터 타입을 정의합니다.
*/

// HLSL Common.hlsl과 1:1 대응하는 CB 슬롯 정의
namespace ECBSlot
{
	constexpr uint32 Frame = 0;			// b0: View/Projection/Wireframe
	constexpr uint32 PerObject = 1;		// b1: Model/Color
	constexpr uint32 Gizmo = 2;			// b2: Gizmo state
	constexpr uint32 PostProcess = 3;	// b3: PostProcess effect params
	constexpr uint32 Material = 4;		// b4: Material properties (UVScroll 등)
	constexpr uint32 SceneEffect = 5;	// b5: scene-wide special effects
	constexpr uint32 Fog = 6;			// b6: fog post-process params
	constexpr uint32 Picking = 7;		// b7: ID picking


	constexpr uint32 PostProcess_FXAA = 9;    // b9: FXAA effect params
	constexpr uint32 MaxLocalTintEffects = 8;
	constexpr uint32 MaxFogComponents = FogRendering::MaxFogComponents;
}

//PerObject
struct FPerObjectConstants
{
	FMatrix Model;
	FVector4 Color;

	// 기본 PerObject: WorldMatrix + White
	static FPerObjectConstants FromWorldMatrix(const FMatrix& WorldMatrix)
	{
		return { WorldMatrix, FVector4(1.0f, 1.0f, 1.0f, 1.0f) };
	}
};

struct FGPUFloat4x4
{
	float M[4][4] = {};

	FGPUFloat4x4() = default;
	FGPUFloat4x4(const FMatrix& InMatrix)
	{
		std::memcpy(M, InMatrix.M, sizeof(M));
	}

	FGPUFloat4x4& operator=(const FMatrix& InMatrix)
	{
		std::memcpy(M, InMatrix.M, sizeof(M));
		return *this;
	}
};

struct FFrameConstants
{
	FGPUFloat4x4 View;
	FGPUFloat4x4 Projection;
	float bIsWireframe;
	FVector WireframeColor;
	float Time;
	float NearPlane;
	float FarPlane;
	float _pad0;
	FVector CameraPosition;
	float _pad1;
	FGPUFloat4x4 InverseView;
	FGPUFloat4x4 InverseProjection;
	FGPUFloat4x4 InverseViewProjection;
	float InvDeviceZToWorldZTransform2;
	float InvDeviceZToWorldZTransform3;
	float _framePad2[2];
};

static_assert(sizeof(FGPUFloat4x4) == 64, "FGPUFloat4x4 must match HLSL float4x4 size.");
static_assert(offsetof(FFrameConstants, InverseView) == 176, "FFrameConstants::InverseView offset must match HLSL cbuffer layout.");
static_assert(offsetof(FFrameConstants, InverseProjection) == 240, "FFrameConstants::InverseProjection offset must match HLSL cbuffer layout.");
static_assert(offsetof(FFrameConstants, InverseViewProjection) == 304, "FFrameConstants::InverseViewProjection offset must match HLSL cbuffer layout.");
static_assert(offsetof(FFrameConstants, InvDeviceZToWorldZTransform2) == 368, "FFrameConstants::InvDeviceZToWorldZTransform2 offset must match HLSL cbuffer layout.");
static_assert(sizeof(FFrameConstants) == 384, "FFrameConstants size must match HLSL cbuffer layout.");

struct FLocalTintEffectConstants
{
	FVector4 PositionRadius = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4 Color = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4 Params = FVector4(0.0f, 1.0f, 0.0f, 0.0f);
};

struct FSceneEffectConstants
{
	FLocalTintEffectConstants LocalTints[ECBSlot::MaxLocalTintEffects];
	uint32 LocalTintCount = 0;
	float _pad[3] = {};
};

struct FMaterialConstants
{
	uint32 bIsUVScroll;
	float _pad[3];
	FVector4 SectionColor = { 1.0f, 1.0f, 1.0f, 1.0f };
};

struct FPickingConstants
{
	uint32 PickingId = 0;
	float _pad[3] = { 0.0f, 0.0f, 0.0f };
};

struct FGizmoConstants
{
	FVector4 ColorTint;
	uint32 bIsInnerGizmo;
	uint32 bClicking;
	uint32 SelectedAxis;
	float HoveredAxisOpacity;
	uint32 AxisMask;       // 비트 0=X, 1=Y, 2=Z — 1이면 표시, 0이면 숨김. 0x7=전부 표시
	uint32 bOverrideAxisColor;
	uint32 _pad[2];
};

// PostProcess Outline CB (b3) — HLSL OutlinePostProcessCB와 1:1 대응
struct FOutlinePostProcessConstants
{
	FVector4 OutlineColor = FVector4(1.0f, 0.5f, 0.0f, 1.0f);
	float OutlineThickness = 1.0f;
	float OutlineFalloff = 1.6f;
	float bOutputLumaToAlpha = 1.0f;
	float OutputAlpha = 1.0f;
};

struct FFXAAConstants
{
	FVector2 TexelSize = FVector2(0.0f, 0.0f);
	float EdgeThreshold;
	float EdgeThresholdMin;
  int32 SearchSteps = 3;
	float _pad[3] = { 0.0f, 0.0f, 0.0f };
};

struct FAABBConstants
{
	FVector Min;
	float Padding0;

	FVector Max;
	float Padding1;

	FColor Color;
};

struct FOBBConstants
{
	FBoundingBox LocalBox;
	FMatrix Transform;
	FColor Color;
};

struct FGridConstants
{
	float GridSpacing;
	int32 GridHalfLineCount;
	float Padding0[2];
};

struct FFontConstants
{
	FString Text;							// 렌더링할 문자열 (프레임 내 유효)
	const FFontResource* Font = nullptr;
	float Scale = 1.0f;

	uint32 bScreenSpace = 0;	// true면 스크린 공간에서 렌더링, false면 월드 공간
	FVector2 ScreenPosition = FVector2(0.0f, 0.0f);		// 스크린 공간에서의 위치 (bScreenSpace가 true일 때 사용)
};

struct FSubUVConstants
{
	const FParticleResource* Particle = nullptr;
	uint32 FrameIndex = 0;
	uint32 Columns = 1;
	uint32 Rows = 1;
	float Width = 1.0f;
	float Height = 1.0f;
};

struct FBillboardConstants
{
	const FTextureResource* Texture = nullptr;
	float Width  = 1.0f;
	float Height = 1.0f;
};

// ============================================================
// Batcher Entry — 각 Batcher가 필요한 데이터만 담는 경량 구조체
// ============================================================

struct FFontEntry
{
	FPerObjectConstants PerObject;
	FFontConstants Font;
	bool bSelected = false;
};

struct FSubUVEntry
{
	FPerObjectConstants PerObject;
	FSubUVConstants SubUV;
  bool bSelected = false;
};

struct FBillboardEntry
{
	FPerObjectConstants PerObject;
	FBillboardConstants Billboard;
  bool bSelected = false;
};

struct FAABBEntry
{
	FAABBConstants AABB;
};

struct FOBBEntry
{
	FOBBConstants OBB;
};

struct FGridEntry
{
	FGridConstants Grid;
};

struct FDebugLineEntry
{
	FVector Start;
	FVector End;
	FColor  Color;
};

// 스크린 공간 텍스트 — Overlay Stats 등에서 사용
struct FOverlayStatLine
{
	FString Text;
	FVector2 ScreenPosition = FVector2(0.0f, 0.0f);
};

// ============================================================
// 타입별 CB 바인딩 디스크립터 — GPU CB에 업로드할 데이터를 인라인 보관
// ============================================================
struct FConstantBufferBinding
{
	FConstantBuffer* Buffer = nullptr;	// 업데이트할 CB (nullptr이면 미사용)
	uint32 Size = 0;					// 업로드할 바이트 수
	uint32 Slot = 0;					// VS/PS CB 슬롯

	static constexpr size_t kMaxDataSize = 64;
	alignas(16) uint8 Data[kMaxDataSize] = {};

	// Buffer/Size/Slot
	template<typename T>
	T& Bind(FConstantBuffer* InBuffer, uint32 InSlot)
	{
		static_assert(sizeof(T) <= kMaxDataSize, "CB data exceeds inline buffer size");
		Buffer = InBuffer;
		Size = sizeof(T);
		Slot = InSlot;
		return *reinterpret_cast<T*>(Data);
	}

	template<typename T>
	T& As()
	{
		static_assert(sizeof(T) <= kMaxDataSize, "CB data exceeds inline buffer size");
		return *reinterpret_cast<T*>(Data);
	}

	template<typename T>
	const T& As() const
	{
		static_assert(sizeof(T) <= kMaxDataSize, "CB data exceeds inline buffer size");
		return *reinterpret_cast<const T*>(Data);
	}
};

// 섹션별 드로우 정보 — 머티리얼(텍스처)이 다른 구간을 분리 드로우
struct FMeshSectionDraw
{
	ID3D11ShaderResourceView* DiffuseSRV = nullptr;
	FVector4 DiffuseColor = { 1.0f, 0.0f, 1.0f, 1.0f };		// 기본 마젠타 색
	uint32 FirstIndex = 0;
	uint32 IndexCount = 0;
	bool bIsUVScroll = false;
};

