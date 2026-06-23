#pragma once

#include "Core/CoreTypes.h"
#include "Object/FName.h"

// COM 인터페이스 전방 선언 (d3d11.h 없이 포인터 사용 가능)
struct ID3D11ShaderResourceView;

// Font/Particle/Texture 공통 텍스처 리소스.
// ResourceManager가 소유하며, 컴포넌트는 포인터로 참조만 합니다.
// Font는 Columns × Rows 그리드 정보를 리소스에 보관하고, SubUV는 컴포넌트 인스턴스에서 그리드를 편집합니다.
struct FTextureAtlasResource
{
	FName Name;
	FString Path;							// Asset 상대 경로
	ID3D11ShaderResourceView* SRV = nullptr; // GPU에 로드된 텍스처 SRV
	uint64 TrackedMemoryBytes = 0;
	uint32 Width = 0;						// 원본 텍스처 가로 픽셀 수
	uint32 Height = 0;						// 원본 텍스처 세로 픽셀 수

	uint32 Columns = 1;						// 아틀라스 가로 프레임(셀) 수
	uint32 Rows    = 1;						// 아틀라스 세로 프레임(셀) 수

	bool IsLoaded() const { return SRV != nullptr; }
};

// 의미론적 별칭 — 타입은 동일하지만 용도를 명시합니다.
using FFontResource     = FTextureAtlasResource;
using FParticleResource = FTextureAtlasResource;
using FTextureResource  = FTextureAtlasResource;	// 단일 정적 텍스처 (Columns=Rows=1)
