#pragma once

//	Windows API Include
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>

//	D3D API Include
#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>

#pragma comment(lib, "dxgi")
#include "Core/CoreTypes.h"

//	Mesh Shape Enum — MeshBufferManager 조회용 (순수 기하 형상)
enum class EMeshShape
{
	Cube,
	Sphere,
	Plane,
	Quad,
	SpriteQuad,		// FTextureVertex 기반 UV 쿼드 (Billboard 전용)
	TransGizmo,
	RotGizmo,
	ScaleGizmo,
};

enum class ERenderPass : uint32
{
	Opaque,
	Font,			// DEPRECATED: TextRenderComponent → FontBatcher 경유
	SubUV,			// DEPRECATED: SubUVComponent     → SubUVBatcher 경유
	Translucent,
	SelectionMask,
	Editor,
	Grid,
	PostProcess,
	Billboard,		// BillboardComponent → FBillboardProxy 직접 드로우
	VisualizationBillboard, // Editor visualization billboard (e.g. Empty Actor icon)
	GizmoOuter,
	GizmoInner,
	OverlayFont,	// DEPRECATED
	MAX
};
