#include "FontGeometry.h"
#include "Resource/ResourceManager.h"

#include <algorithm>

void FFontGeometry::Create(ID3D11Device* InDevice)
{
	Device = InDevice;
	if (!Device) return;
	Device->AddRef();

	WorldVB.Create(InDevice, 1024, sizeof(FTextureVertex));
	WorldIB.Create(InDevice, 1536);
	ScreenVB.Create(InDevice, 256, sizeof(FTextureVertex));
	ScreenIB.Create(InDevice, 384);

	if (const FFontResource* DefaultFont = FResourceManager::Get().FindFont(FName("Default")))
	{
		if (DefaultFont->Columns > 0 && DefaultFont->Rows > 0)
		{
			BuildCharInfoMap(DefaultFont->Columns, DefaultFont->Rows);
		}
	}
}

void FFontGeometry::Release()
{
	CharInfoMap.clear();
	Clear();
	ClearScreen();

	WorldVB.Release();
	WorldIB.Release();
	ScreenVB.Release();
	ScreenIB.Release();

	if (Device) { Device->Release(); Device = nullptr; }
}

void FFontGeometry::BuildCharInfoMap(uint32 Columns, uint32 Rows)
{
	CharInfoMap.clear();
	CachedColumns = Columns;
	CachedRows = Rows;

	const float CellW = 1.0f / static_cast<float>(Columns);
	const float CellH = 1.0f / static_cast<float>(Rows);

	auto AddChar = [&](uint32 Codepoint, uint32 Slot)
	{
		const uint32 Col = Slot % Columns;
		const uint32 Row = Slot / Columns;
		if (Row >= Rows) return;
		CharInfoMap[Codepoint] = { Col * CellW, Row * CellH, CellW, CellH };
	};

	// ASCII 32(' ') ~ 126('~')
	for (uint32 CP = 32; CP <= 126; ++CP)
		AddChar(CP, CP - 32);

	// 한글 완성형 가(U+AC00) ~ 힣(U+D7A3)
	uint32 Slot = 127;
	for (uint32 CP = 0xAC00; CP <= 0xD7A3; ++CP, ++Slot)
		AddChar(CP, Slot - 32);
}

void FFontGeometry::EnsureCharInfoMap(const FFontResource* Resource)
{
	if (!Resource || Resource->Columns == 0 || Resource->Rows == 0) return;
	if (CachedColumns == Resource->Columns && CachedRows == Resource->Rows) return;
	BuildCharInfoMap(Resource->Columns, Resource->Rows);
}

void FFontGeometry::GetCharUV(uint32 Codepoint, FVector2& OutUVMin, FVector2& OutUVMax) const
{
	const auto It = CharInfoMap.find(Codepoint);
	if (It == CharInfoMap.end())
	{
		OutUVMin = FVector2(0, 0);
		OutUVMax = FVector2(0, 0);
		return;
	}
	const FCharacterInfo& Info = It->second;
	OutUVMin = FVector2(Info.U, Info.V);
	OutUVMax = FVector2(Info.U + Info.Width, Info.V + Info.Height);
}

void FFontGeometry::GetGlyphUV(const FFontResource& Resource, const FFontGlyph& Glyph, FVector2& OutUVMin, FVector2& OutUVMax) const
{
	if (Resource.Common.ScaleW == 0 || Resource.Common.ScaleH == 0)
	{
		OutUVMin = FVector2(0.0f, 0.0f);
		OutUVMax = FVector2(0.0f, 0.0f);
		return;
	}

	const float InvW = 1.0f / static_cast<float>(Resource.Common.ScaleW);
	const float InvH = 1.0f / static_cast<float>(Resource.Common.ScaleH);
	OutUVMin = FVector2(static_cast<float>(Glyph.X) * InvW, static_cast<float>(Glyph.Y) * InvH);
	OutUVMax = FVector2(static_cast<float>(Glyph.X + Glyph.Width) * InvW, static_cast<float>(Glyph.Y + Glyph.Height) * InvH);
}

void FFontGeometry::Clear()
{
	WorldVertices.clear();
	WorldIndices.clear();
	WorldBatches.clear();
}

void FFontGeometry::ClearScreen()
{
	ScreenVertices.clear();
	ScreenIndices.clear();
	ScreenBatches.clear();
}

void FFontGeometry::AppendBatch(TArray<FFontDrawBatch>& Batches, const FFontResource* Font, uint32 FirstIndex, uint32 IndexCount, EDepthStencilState DepthStencil)
{
	if (!Font || IndexCount == 0)
	{
		return;
	}

	if (!Batches.empty())
	{
		FFontDrawBatch& Last = Batches.back();
		if (Last.Font == Font && Last.DepthStencil == DepthStencil && Last.FirstIndex + Last.IndexCount == FirstIndex)
		{
			Last.IndexCount += IndexCount;
			return;
		}
	}

	Batches.push_back({ Font, FirstIndex, IndexCount, DepthStencil });
}

void FFontGeometry::AddWorldText(const FString& Text,
	const FFontResource* Font,
	const FVector4& Color,
	const FVector& WorldPos,
	const FVector& CamRight,
	const FVector& CamUp,
	const FVector& WorldScale,
	float Scale,
	float CharWidth,
	float CharHeight,
	float Spacing,
	float LineSpacing,
	int32 HorizontalAlign,
	int32 VerticalAlign,
	EDepthStencilState DepthStencil)
{
	if (Text.empty()) return;
	if (!Font || !Font->IsLoaded()) return;

	EnsureCharInfoMap(Font);

	const float CharW = std::max(0.001f, CharWidth) * Scale * WorldScale.Y;
	const float CharH = std::max(0.001f, CharHeight) * Scale * WorldScale.Z;
	const float Advance = CharW + std::max(0.0f, Spacing) * Scale * WorldScale.Y;
	const float LineAdvance = CharH + std::max(0.0f, LineSpacing) * Scale * WorldScale.Z;
	const bool bUseGlyphMetrics = Font->HasGlyphMetrics();
	const float MetricLineHeight = std::max(1.0f, static_cast<float>(Font->Common.LineHeight));
	const float MetricScale = CharH / MetricLineHeight;
	const float MetricSpacing = std::max(0.0f, Spacing) * Scale * WorldScale.Y;

	TArray<float> LineWidths;
	LineWidths.reserve(4);
	{
		const uint8* MeasurePtr = reinterpret_cast<const uint8*>(Text.c_str());
		const uint8* const MeasureEnd = MeasurePtr + Text.size();
		float LineWidth = 0.0f;
		uint32 PreviousMeasureCP = 0;
		while (MeasurePtr < MeasureEnd)
		{
			uint32 CP = 0;
			if      (MeasurePtr[0] < 0x80)                                     { CP = MeasurePtr[0]; MeasurePtr += 1; }
			else if ((MeasurePtr[0] & 0xE0) == 0xC0 && MeasurePtr + 1 < MeasureEnd) { CP = ((MeasurePtr[0] & 0x1F) << 6) | (MeasurePtr[1] & 0x3F); MeasurePtr += 2; }
			else if ((MeasurePtr[0] & 0xF0) == 0xE0 && MeasurePtr + 2 < MeasureEnd) { CP = ((MeasurePtr[0] & 0x0F) << 12) | ((MeasurePtr[1] & 0x3F) << 6) | (MeasurePtr[2] & 0x3F); MeasurePtr += 3; }
			else if ((MeasurePtr[0] & 0xF8) == 0xF0 && MeasurePtr + 3 < MeasureEnd) { CP = ((MeasurePtr[0] & 0x07) << 18) | ((MeasurePtr[1] & 0x3F) << 12) | ((MeasurePtr[2] & 0x3F) << 6) | (MeasurePtr[3] & 0x3F); MeasurePtr += 4; }
			else { ++MeasurePtr; continue; }

			if (CP == '\n')
			{
				LineWidths.push_back(LineWidth);
				LineWidth = 0.0f;
				PreviousMeasureCP = 0;
				continue;
			}

			if (bUseGlyphMetrics)
			{
				if (PreviousMeasureCP != 0)
				{
					LineWidth += static_cast<float>(Font->FindKerning(PreviousMeasureCP, CP)) * MetricScale;
				}

				const FFontGlyph* Glyph = Font->FindGlyph(CP);
				if (!Glyph)
				{
					Glyph = Font->FindGlyph('?');
				}
				LineWidth += Glyph ? (Glyph->XAdvance > 0 ? static_cast<float>(Glyph->XAdvance) * MetricScale : CharW) + MetricSpacing : Advance;
				PreviousMeasureCP = CP;
			}
			else
			{
				LineWidth += Advance;
			}
		}
		LineWidths.push_back(LineWidth);
	}

	auto GetLineOffsetX = [HorizontalAlign](float LineWidth) -> float
	{
		if (HorizontalAlign == 1) return -LineWidth * 0.5f;
		if (HorizontalAlign == 2) return -LineWidth;
		return 0.0f;
	};

	const float LineCount = std::max(1.0f, static_cast<float>(LineWidths.size()));
	const float BlockHeight = CharH + (LineCount - 1.0f) * LineAdvance;
	float VerticalOffset = 0.0f;
	if (VerticalAlign == 1) VerticalOffset = BlockHeight * 0.5f;
	else if (VerticalAlign == 2) VerticalOffset = BlockHeight;

	uint32 LineIndex = 0;
	float CursorX = GetLineOffsetX(LineWidths.empty() ? 0.0f : LineWidths[0]);
	float CursorY = VerticalOffset;
	const uint32 Base = static_cast<uint32>(WorldVertices.size());
	const uint32 IdxBase = static_cast<uint32>(WorldIndices.size());
	const size_t CharCount = Text.size();

	WorldVertices.resize(Base + CharCount * 4);
	WorldIndices.resize(IdxBase + CharCount * 6);
	FTextureVertex* pV = WorldVertices.data() + Base;
	uint32* pI = WorldIndices.data() + IdxBase;

	const FVector HalfRight = CamRight * (CharW * 0.5f);
	const FVector HalfUp    = CamUp    * (CharH * 0.5f);

	const uint8* Ptr = reinterpret_cast<const uint8*>(Text.c_str());
	const uint8* const End = Ptr + Text.size();
	uint32 CharIdx = 0;
	uint32 PreviousCP = 0;

	for (size_t i = 0; i < CharCount && Ptr < End; ++i)
	{
		uint32 CP = 0;
		if      (Ptr[0] < 0x80)                             { CP = Ptr[0];                                                                       Ptr += 1; }
		else if ((Ptr[0] & 0xE0) == 0xC0 && Ptr + 1 < End)  { CP = ((Ptr[0] & 0x1F) << 6)  |  (Ptr[1] & 0x3F);                                   Ptr += 2; }
		else if ((Ptr[0] & 0xF0) == 0xE0 && Ptr + 2 < End)  { CP = ((Ptr[0] & 0x0F) << 12) | ((Ptr[1] & 0x3F) << 6)  |  (Ptr[2] & 0x3F);         Ptr += 3; }
		else if ((Ptr[0] & 0xF8) == 0xF0 && Ptr + 3 < End)  { CP = ((Ptr[0] & 0x07) << 18) | ((Ptr[1] & 0x3F) << 12) | ((Ptr[2] & 0x3F) << 6) | (Ptr[3] & 0x3F); Ptr += 4; }
		else                                                  { ++Ptr; continue; }

		if (CP == '\n')
		{
			++LineIndex;
			CursorX = GetLineOffsetX(LineIndex < LineWidths.size() ? LineWidths[LineIndex] : 0.0f);
			CursorY -= LineAdvance;
			PreviousCP = 0;
			continue;
		}

		if (bUseGlyphMetrics)
		{
			if (PreviousCP != 0)
			{
				CursorX += static_cast<float>(Font->FindKerning(PreviousCP, CP)) * MetricScale;
			}

			const FFontGlyph* Glyph = Font->FindGlyph(CP);
			if (!Glyph)
			{
				Glyph = Font->FindGlyph('?');
			}
			if (!Glyph)
			{
				CursorX += Advance;
				PreviousCP = CP;
				continue;
			}

			const float GlyphAdvance = (Glyph->XAdvance > 0 ? static_cast<float>(Glyph->XAdvance) * MetricScale : CharW) + MetricSpacing;
			if (!Glyph->IsDrawable())
			{
				CursorX += GlyphAdvance;
				PreviousCP = CP;
				continue;
			}

			FVector2 UVMin, UVMax;
			GetGlyphUV(*Font, *Glyph, UVMin, UVMax);
			if (UVMin.X == UVMax.X || UVMin.Y == UVMax.Y)
			{
				CursorX += GlyphAdvance;
				PreviousCP = CP;
				continue;
			}

			const float Left = CursorX + static_cast<float>(Glyph->XOffset) * MetricScale;
			const float Top = CursorY - static_cast<float>(Glyph->YOffset) * MetricScale;
			const float Right = Left + static_cast<float>(Glyph->Width) * MetricScale;
			const float Bottom = Top - static_cast<float>(Glyph->Height) * MetricScale;

			const FVector TopLeft = WorldPos + CamRight * Left + CamUp * Top;
			const FVector TopRight = WorldPos + CamRight * Right + CamUp * Top;
			const FVector BottomLeft = WorldPos + CamRight * Left + CamUp * Bottom;
			const FVector BottomRight = WorldPos + CamRight * Right + CamUp * Bottom;

			pV[0] = { TopLeft, { UVMin.X, UVMin.Y }, Color };
			pV[1] = { TopRight, { UVMax.X, UVMin.Y }, Color };
			pV[2] = { BottomLeft, { UVMin.X, UVMax.Y }, Color };
			pV[3] = { BottomRight, { UVMax.X, UVMax.Y }, Color };

			const uint32 Vi = Base + CharIdx * 4;
			pI[0] = Vi;     pI[1] = Vi + 1; pI[2] = Vi + 2;
			pI[3] = Vi + 1; pI[4] = Vi + 3; pI[5] = Vi + 2;

			pV += 4;
			pI += 6;
			++CharIdx;
			CursorX += GlyphAdvance;
			PreviousCP = CP;
			continue;
		}

		FVector2 UVMin, UVMax;
		GetCharUV(CP, UVMin, UVMax);
		if (UVMin.X == UVMax.X || UVMin.Y == UVMax.Y)
		{
			GetCharUV('?', UVMin, UVMax);
			if (UVMin.X == UVMax.X || UVMin.Y == UVMax.Y)
			{
				CursorX += Advance;
				continue;
			}
		}

		const FVector Center = WorldPos + CamRight * CursorX + CamUp * (CursorY - CharH * 0.5f);

		pV[0] = { Center                 + HalfUp, { UVMin.X, UVMin.Y }, Color };
		pV[1] = { Center + HalfRight * 2 + HalfUp, { UVMax.X, UVMin.Y }, Color };
		pV[2] = { Center                 - HalfUp, { UVMin.X, UVMax.Y }, Color };
		pV[3] = { Center + HalfRight * 2 - HalfUp, { UVMax.X, UVMax.Y }, Color };

		const uint32 Vi = Base + CharIdx * 4;
		pI[0] = Vi;     pI[1] = Vi + 1; pI[2] = Vi + 2;
		pI[3] = Vi + 1; pI[4] = Vi + 3; pI[5] = Vi + 2;

		pV += 4;
		pI += 6;
		++CharIdx;
		CursorX += Advance;
		PreviousCP = CP;
	}

	WorldVertices.resize(Base + CharIdx * 4);
	WorldIndices.resize(IdxBase + CharIdx * 6);
	AppendBatch(WorldBatches, Font, IdxBase, CharIdx * 6, DepthStencil);
}

void FFontGeometry::AddScreenText(const FString& Text,
	float ScreenX, float ScreenY,
	float ViewportWidth, float ViewportHeight,
	float Scale,
	const FFontResource* Font,
	const FVector4& Color)
{
	if (Text.empty()) return;
	if (ViewportWidth <= 0.0f || ViewportHeight <= 0.0f) return;

	if (!Font)
	{
		Font = FResourceManager::Get().FindFont(FName("Default"));
	}
	if (!Font || !Font->IsLoaded()) return;

	EnsureCharInfoMap(Font);

	const float CharW = 23.0f * Scale;
	const float CharH = 23.0f * Scale;
	const float LetterSpacing = -0.5f * CharW;
	const bool bUseGlyphMetrics = Font->HasGlyphMetrics();
	const float MetricLineHeight = std::max(1.0f, static_cast<float>(Font->Common.LineHeight));
	const float MetricScale = CharH / MetricLineHeight;

	const uint32 Base = static_cast<uint32>(ScreenVertices.size());
	const uint32 IdxBase = static_cast<uint32>(ScreenIndices.size());
	const size_t CharCount = Text.size();

	ScreenVertices.resize(Base + CharCount * 4);
	ScreenIndices.resize(IdxBase + CharCount * 6);

	FTextureVertex* pV = ScreenVertices.data() + Base;
	uint32* pI = ScreenIndices.data() + IdxBase;

	const uint8* Ptr = reinterpret_cast<const uint8*>(Text.c_str());
	const uint8* const End = Ptr + Text.size();

	uint32 CharIdx = 0;
	float CursorX = ScreenX;
	float CursorY = ScreenY;
	uint32 PreviousCP = 0;

	auto PixelToClipX = [ViewportWidth](float X) -> float
		{
			return (X / ViewportWidth) * 2.0f - 1.0f;
		};

	auto PixelToClipY = [ViewportHeight](float Y) -> float
		{
			return 1.0f - (Y / ViewportHeight) * 2.0f;
		};

	for (size_t i = 0; i < CharCount && Ptr < End; ++i)
	{
		uint32 CP = 0;
		if (Ptr[0] < 0x80) { CP = Ptr[0]; Ptr += 1; }
		else if ((Ptr[0] & 0xE0) == 0xC0 && Ptr + 1 < End) { CP = ((Ptr[0] & 0x1F) << 6) | (Ptr[1] & 0x3F); Ptr += 2; }
		else if ((Ptr[0] & 0xF0) == 0xE0 && Ptr + 2 < End) { CP = ((Ptr[0] & 0x0F) << 12) | ((Ptr[1] & 0x3F) << 6) | (Ptr[2] & 0x3F); Ptr += 3; }
		else if ((Ptr[0] & 0xF8) == 0xF0 && Ptr + 3 < End) { CP = ((Ptr[0] & 0x07) << 18) | ((Ptr[1] & 0x3F) << 12) | ((Ptr[2] & 0x3F) << 6) | (Ptr[3] & 0x3F); Ptr += 4; }
		else { ++Ptr; continue; }

		if (CP == '\n')
		{
			CursorX = ScreenX;
			CursorY += CharH;
			PreviousCP = 0;
			continue;
		}

		if (bUseGlyphMetrics)
		{
			if (PreviousCP != 0)
			{
				CursorX += static_cast<float>(Font->FindKerning(PreviousCP, CP)) * MetricScale;
			}

			const FFontGlyph* Glyph = Font->FindGlyph(CP);
			if (!Glyph)
			{
				Glyph = Font->FindGlyph('?');
			}
			if (!Glyph)
			{
				CursorX += CharW + LetterSpacing;
				PreviousCP = CP;
				continue;
			}

			const float GlyphAdvance = Glyph->XAdvance > 0 ? static_cast<float>(Glyph->XAdvance) * MetricScale : CharW;
			if (!Glyph->IsDrawable())
			{
				CursorX += GlyphAdvance + LetterSpacing;
				PreviousCP = CP;
				continue;
			}

			FVector2 UVMin, UVMax;
			GetGlyphUV(*Font, *Glyph, UVMin, UVMax);
			if (UVMin.X == UVMax.X || UVMin.Y == UVMax.Y)
			{
				CursorX += GlyphAdvance + LetterSpacing;
				PreviousCP = CP;
				continue;
			}

			const float PixelLeft = CursorX + static_cast<float>(Glyph->XOffset) * MetricScale;
			const float PixelRight = PixelLeft + static_cast<float>(Glyph->Width) * MetricScale;
			const float PixelTop = CursorY + static_cast<float>(Glyph->YOffset) * MetricScale;
			const float PixelBottom = PixelTop + static_cast<float>(Glyph->Height) * MetricScale;

			const float Left = PixelToClipX(PixelLeft);
			const float Right = PixelToClipX(PixelRight);
			const float Top = PixelToClipY(PixelTop);
			const float Bottom = PixelToClipY(PixelBottom);

			pV[0] = { FVector(Left,  Top,    0.0f), FVector2(UVMin.X, UVMin.Y), Color };
			pV[1] = { FVector(Right, Top,    0.0f), FVector2(UVMax.X, UVMin.Y), Color };
			pV[2] = { FVector(Left,  Bottom, 0.0f), FVector2(UVMin.X, UVMax.Y), Color };
			pV[3] = { FVector(Right, Bottom, 0.0f), FVector2(UVMax.X, UVMax.Y), Color };

			const uint32 Vi = Base + CharIdx * 4;
			pI[0] = Vi;     pI[1] = Vi + 1; pI[2] = Vi + 2;
			pI[3] = Vi + 1; pI[4] = Vi + 3; pI[5] = Vi + 2;

			pV += 4;
			pI += 6;
			++CharIdx;
			CursorX += GlyphAdvance + LetterSpacing;
			PreviousCP = CP;
			continue;
		}

		FVector2 UVMin, UVMax;
		GetCharUV(CP, UVMin, UVMax);
		if (UVMin.X == UVMax.X || UVMin.Y == UVMax.Y)
		{
			GetCharUV('?', UVMin, UVMax);
			if (UVMin.X == UVMax.X || UVMin.Y == UVMax.Y)
			{
				CursorX += CharW + LetterSpacing;
				continue;
			}
		}

		const float Left = PixelToClipX(CursorX);
		const float Right = PixelToClipX(CursorX + CharW);
		const float Top = PixelToClipY(CursorY);
		const float Bottom = PixelToClipY(CursorY + CharH);

		pV[0] = { FVector(Left,  Top,    0.0f), FVector2(UVMin.X, UVMin.Y), Color };
		pV[1] = { FVector(Right, Top,    0.0f), FVector2(UVMax.X, UVMin.Y), Color };
		pV[2] = { FVector(Left,  Bottom, 0.0f), FVector2(UVMin.X, UVMax.Y), Color };
		pV[3] = { FVector(Right, Bottom, 0.0f), FVector2(UVMax.X, UVMax.Y), Color };

		const uint32 Vi = Base + CharIdx * 4;
		pI[0] = Vi;     pI[1] = Vi + 1; pI[2] = Vi + 2;
		pI[3] = Vi + 1; pI[4] = Vi + 3; pI[5] = Vi + 2;

		pV += 4;
		pI += 6;
		++CharIdx;
		CursorX += CharW + LetterSpacing;
		PreviousCP = CP;
	}

	ScreenVertices.resize(Base + CharIdx * 4);
	ScreenIndices.resize(IdxBase + CharIdx * 6);
	AppendBatch(ScreenBatches, Font, IdxBase, CharIdx * 6, EDepthStencilState::NoDepth);
}

bool FFontGeometry::UploadWorldBuffers(ID3D11DeviceContext* Context)
{
	if (WorldVertices.empty()) return false;

	const uint32 VertCount = static_cast<uint32>(WorldVertices.size());
	const uint32 IdxCount  = static_cast<uint32>(WorldIndices.size());

	WorldVB.EnsureCapacity(Device, VertCount);
	WorldIB.EnsureCapacity(Device, IdxCount);
	if (!WorldVB.Update(Context, WorldVertices.data(), VertCount)) return false;
	if (!WorldIB.Update(Context, WorldIndices.data(), IdxCount)) return false;
	return true;
}

bool FFontGeometry::UploadScreenBuffers(ID3D11DeviceContext* Context)
{
	if (ScreenVertices.empty()) return false;

	const uint32 VertCount = static_cast<uint32>(ScreenVertices.size());
	const uint32 IdxCount  = static_cast<uint32>(ScreenIndices.size());

	ScreenVB.EnsureCapacity(Device, VertCount);
	ScreenIB.EnsureCapacity(Device, IdxCount);
	if (!ScreenVB.Update(Context, ScreenVertices.data(), VertCount)) return false;
	if (!ScreenIB.Update(Context, ScreenIndices.data(), IdxCount)) return false;
	return true;
}
