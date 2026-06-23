#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/FName.h"

// COM 인터페이스 전방 선언 (d3d11.h 없이 포인터 사용 가능)
struct ID3D11ShaderResourceView;

// Font/Particle 공통 텍스처 아틀라스 리소스.
// ResourceManager가 소유하며, 컴포넌트는 포인터로 참조만 합니다.
// Columns × Rows 그리드 정보를 함께 보유해 UV 계산에 활용합니다.
struct FTextureAtlasResource
{
	FName Name;
	FString Path;							// Asset 상대 경로 (Resource.ini에서 로드)
	ID3D11ShaderResourceView* SRV = nullptr; // GPU에 로드된 텍스처 SRV
	uint64 TrackedMemoryBytes = 0;

	uint32 Columns = 1;						// 아틀라스 가로 프레임(셀) 수
	uint32 Rows    = 1;						// 아틀라스 세로 프레임(셀) 수

	bool IsLoaded() const { return SRV != nullptr; }
};

// 의미론적 별칭 — 타입은 동일하지만 용도를 명시합니다.
struct FFontGlyph
{
	uint32 Id = 0;
	uint32 X = 0;
	uint32 Y = 0;
	uint32 Width = 0;
	uint32 Height = 0;
	int32 XOffset = 0;
	int32 YOffset = 0;
	int32 XAdvance = 0;
	uint32 Page = 0;
	uint32 Channel = 0;

	bool IsDrawable() const { return Width > 0 && Height > 0; }
};

struct FFontCommon
{
	uint32 LineHeight = 0;
	uint32 Base = 0;
	uint32 ScaleW = 0;
	uint32 ScaleH = 0;
	uint32 Pages = 0;
	bool bPacked = false;
	uint32 AlphaChannel = 0;
	uint32 RedChannel = 0;
	uint32 GreenChannel = 0;
	uint32 BlueChannel = 0;
};

struct FFontResource : public FTextureAtlasResource
{
	FString MetadataPath;
	FFontCommon Common;
	TArray<FString> PageFiles;
	TMap<uint32, FFontGlyph> Glyphs;
	TMap<uint64, int32> Kernings;

	static uint64 MakeKerningKey(uint32 First, uint32 Second)
	{
		return (static_cast<uint64>(First) << 32) | static_cast<uint64>(Second);
	}

	const FFontGlyph* FindGlyph(uint32 Codepoint) const
	{
		auto It = Glyphs.find(Codepoint);
		return (It != Glyphs.end()) ? &It->second : nullptr;
	}

	int32 FindKerning(uint32 First, uint32 Second) const
	{
		auto It = Kernings.find(MakeKerningKey(First, Second));
		return (It != Kernings.end()) ? It->second : 0;
	}

	bool HasGlyph(uint32 Codepoint) const
	{
		return Glyphs.find(Codepoint) != Glyphs.end();
	}

	bool HasGlyphMetrics() const
	{
		return Common.ScaleW > 0 && Common.ScaleH > 0 && !Glyphs.empty();
	}
};

using FParticleResource = FTextureAtlasResource;
using FTextureResource  = FTextureAtlasResource;	// 단일 정적 텍스처 (Columns=Rows=1)
