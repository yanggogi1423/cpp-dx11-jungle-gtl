#include "TextRenderComponent.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Resource/ResourceManager.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Proxy/TextRenderSceneProxy.h"
#include "Serialization/Archive.h"

namespace
{
bool DecodeUTF8Codepoint(const uint8*& Ptr, const uint8* End, uint32& OutCodepoint)
{
	while (Ptr < End)
	{
		if (Ptr[0] < 0x80)
		{
			OutCodepoint = Ptr[0];
			Ptr += 1;
			return true;
		}
		if ((Ptr[0] & 0xE0) == 0xC0 && Ptr + 1 < End)
		{
			OutCodepoint = ((Ptr[0] & 0x1F) << 6) | (Ptr[1] & 0x3F);
			Ptr += 2;
			return true;
		}
		if ((Ptr[0] & 0xF0) == 0xE0 && Ptr + 2 < End)
		{
			OutCodepoint = ((Ptr[0] & 0x0F) << 12) | ((Ptr[1] & 0x3F) << 6) | (Ptr[2] & 0x3F);
			Ptr += 3;
			return true;
		}
		if ((Ptr[0] & 0xF8) == 0xF0 && Ptr + 3 < End)
		{
			OutCodepoint = ((Ptr[0] & 0x07) << 18) |
				((Ptr[1] & 0x3F) << 12) |
				((Ptr[2] & 0x3F) << 6) |
				(Ptr[3] & 0x3F);
			Ptr += 4;
			return true;
		}
		++Ptr;
	}

	return false;
}

float GetTextLineOffset(ETextHAlign Align, float LineWidth)
{
	switch (Align)
	{
	case ETextHAlign::Center:
		return -LineWidth * 0.5f;
	case ETextHAlign::Right:
		return -LineWidth;
	default:
		return 0.0f;
	}
}

void ExpandTextBounds(FTextRenderLayoutBounds& Bounds, float MinY, float MaxY, float MinZ, float MaxZ)
{
	if (!Bounds.bValid)
	{
		Bounds.MinY = MinY;
		Bounds.MaxY = MaxY;
		Bounds.MinZ = MinZ;
		Bounds.MaxZ = MaxZ;
		Bounds.bValid = true;
		return;
	}

	Bounds.MinY = std::min(Bounds.MinY, MinY);
	Bounds.MaxY = std::max(Bounds.MaxY, MaxY);
	Bounds.MinZ = std::min(Bounds.MinZ, MinZ);
	Bounds.MaxZ = std::max(Bounds.MaxZ, MaxZ);
}
}

FPrimitiveSceneProxy* UTextRenderComponent::CreateSceneProxy()
{
	return new FTextRenderSceneProxy(this);
}

void UTextRenderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	if (bBillboard)
	{
		UBillboardComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
		return;
	}

	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	GetWorldMatrix();
	UpdateWorldAABB();
}

void UTextRenderComponent::SetFont(const FName& InFontName)
{
	FontName = InFontName;
	CachedFont = FResourceManager::Get().FindFont(FontName);
	MarkProxyDirty(EDirtyFlag::Mesh);
}

void UTextRenderComponent::SetText(const FString& InText)
{
	if (Text == InText)
	{
		return;
	}

	Text = InText;
	MarkProxyDirty(EDirtyFlag::Mesh);
	MarkProxyDirty(EDirtyFlag::Transform);
	MarkWorldBoundsDirty();
}

void UTextRenderComponent::SetColor(const FVector4& InColor)
{
	Color = InColor;
	MarkProxyDirty(EDirtyFlag::Material);
}

void UTextRenderComponent::SetOpacity(float InOpacity)
{
	const float ClampedOpacity = std::clamp(InOpacity, 0.0f, 1.0f);
	if (Opacity == ClampedOpacity)
	{
		return;
	}

	Opacity = ClampedOpacity;
	MarkProxyDirty(EDirtyFlag::Material);
}

void UTextRenderComponent::SetFontSize(float InSize)
{
	const float ClampedSize = std::max(0.1f, InSize);
	if (FontSize == ClampedSize)
	{
		return;
	}

	FontSize = ClampedSize;
	MarkProxyDirty(EDirtyFlag::Mesh);
	MarkProxyDirty(EDirtyFlag::Transform);
	MarkWorldBoundsDirty();
}

void UTextRenderComponent::SetLetterSpacing(float InSpacing)
{
	const float ClampedSpacing = std::max(0.0f, InSpacing);
	if (Spacing == ClampedSpacing)
	{
		return;
	}

	Spacing = ClampedSpacing;
	MarkProxyDirty(EDirtyFlag::Mesh);
	MarkProxyDirty(EDirtyFlag::Transform);
	MarkWorldBoundsDirty();
}

void UTextRenderComponent::SetLineSpacing(float InSpacing)
{
	const float ClampedSpacing = std::max(0.0f, InSpacing);
	if (LineSpacing == ClampedSpacing)
	{
		return;
	}

	LineSpacing = ClampedSpacing;
	MarkProxyDirty(EDirtyFlag::Mesh);
	MarkProxyDirty(EDirtyFlag::Transform);
	MarkWorldBoundsDirty();
}

void UTextRenderComponent::SetRenderSpace(ETextRenderSpace InSpace)
{
	if (RenderSpace == InSpace)
	{
		return;
	}

	RenderSpace = InSpace;
	MarkProxyDirty(EDirtyFlag::Mesh);
}

void UTextRenderComponent::SetTextBillboardEnabled(bool bEnable)
{
	if (bBillboard == bEnable)
	{
		return;
	}

	bBillboard = bEnable;
	MarkProxyDirty(EDirtyFlag::Transform);
	MarkWorldBoundsDirty();
}

void UTextRenderComponent::SetDepthTestEnabled(bool bEnable)
{
	if (bDepthTest == bEnable)
	{
		return;
	}

	bDepthTest = bEnable;
	MarkProxyDirty(EDirtyFlag::Material);
}

void UTextRenderComponent::SetScreenPosition(float X, float Y)
{
	if (ScreenX == X && ScreenY == Y)
	{
		return;
	}

	ScreenX = X;
	ScreenY = Y;
	MarkProxyDirty(EDirtyFlag::Transform);
}

void UTextRenderComponent::SetHorizontalAlignment(ETextHAlign InAlign)
{
	if (HAlign == InAlign)
	{
		return;
	}

	HAlign = InAlign;
	MarkProxyDirty(EDirtyFlag::Mesh);
	MarkProxyDirty(EDirtyFlag::Transform);
	MarkWorldBoundsDirty();
}

void UTextRenderComponent::SetVerticalAlignment(ETextVAlign InAlign)
{
	if (VAlign == InAlign)
	{
		return;
	}

	VAlign = InAlign;
	MarkProxyDirty(EDirtyFlag::Mesh);
	MarkProxyDirty(EDirtyFlag::Transform);
	MarkWorldBoundsDirty();
}

FTextRenderLayoutBounds UTextRenderComponent::MeasureLocalLayoutBounds() const
{
	return MeasureLocalLayoutBounds(
		Text,
		CachedFont,
		FontSize,
		CharWidth,
		CharHeight,
		Spacing,
		LineSpacing,
		HAlign,
		VAlign);
}

FTextRenderLayoutBounds UTextRenderComponent::MeasureLocalLayoutBounds(
	const FString& InText,
	const FFontResource* InFont,
	float InFontSize,
	float InCharWidth,
	float InCharHeight,
	float InSpacing,
	float InLineSpacing,
	ETextHAlign InHAlign,
	ETextVAlign InVAlign)
{
	FTextRenderLayoutBounds Bounds;
	if (InText.empty())
	{
		return Bounds;
	}

	const float FontScale = std::max(0.1f, InFontSize);
	const float CharW = std::max(0.001f, InCharWidth) * FontScale;
	const float CharH = std::max(0.001f, InCharHeight) * FontScale;
	const float MetricSpacing = std::max(0.0f, InSpacing) * FontScale;
	const float Advance = CharW + MetricSpacing;
	const float LineAdvance = CharH + std::max(0.0f, InLineSpacing) * FontScale;
	const bool bUseGlyphMetrics = InFont && InFont->HasGlyphMetrics();
	const float MetricLineHeight = bUseGlyphMetrics
		? std::max(1.0f, static_cast<float>(InFont->Common.LineHeight))
		: 1.0f;
	const float MetricScale = CharH / MetricLineHeight;

	TArray<float> LineWidths;
	LineWidths.reserve(4);

	const uint8* MeasurePtr = reinterpret_cast<const uint8*>(InText.c_str());
	const uint8* const MeasureEnd = MeasurePtr + InText.size();
	float LineWidth = 0.0f;
	uint32 PreviousMeasureCP = 0;
	while (MeasurePtr < MeasureEnd)
	{
		uint32 CP = 0;
		if (!DecodeUTF8Codepoint(MeasurePtr, MeasureEnd, CP))
		{
			break;
		}

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
				LineWidth += static_cast<float>(InFont->FindKerning(PreviousMeasureCP, CP)) * MetricScale;
			}

			const FFontGlyph* Glyph = InFont->FindGlyph(CP);
			if (!Glyph)
			{
				Glyph = InFont->FindGlyph('?');
			}
			LineWidth += Glyph
				? (Glyph->XAdvance > 0 ? static_cast<float>(Glyph->XAdvance) * MetricScale : CharW) + MetricSpacing
				: Advance;
			PreviousMeasureCP = CP;
		}
		else
		{
			LineWidth += Advance;
		}
	}
	LineWidths.push_back(LineWidth);

	const float LineCount = std::max(1.0f, static_cast<float>(LineWidths.size()));
	const float BlockHeight = CharH + (LineCount - 1.0f) * LineAdvance;
	float VerticalOffset = 0.0f;
	if (InVAlign == ETextVAlign::Center)
	{
		VerticalOffset = BlockHeight * 0.5f;
	}
	else if (InVAlign == ETextVAlign::Bottom)
	{
		VerticalOffset = BlockHeight;
	}

	for (size_t LineIndex = 0; LineIndex < LineWidths.size(); ++LineIndex)
	{
		const float LogicalWidth = std::max(LineWidths[LineIndex], CharW);
		const float LineLeft = GetTextLineOffset(InHAlign, LineWidths[LineIndex]);
		const float LineTop = VerticalOffset - static_cast<float>(LineIndex) * LineAdvance;
		ExpandTextBounds(Bounds, LineLeft, LineLeft + LogicalWidth, LineTop - CharH, LineTop);
	}

	const uint8* Ptr = reinterpret_cast<const uint8*>(InText.c_str());
	const uint8* const End = Ptr + InText.size();
	size_t LineIndex = 0;
	float CursorX = GetTextLineOffset(InHAlign, LineWidths.empty() ? 0.0f : LineWidths[0]);
	float CursorY = VerticalOffset;
	uint32 PreviousCP = 0;

	while (Ptr < End)
	{
		uint32 CP = 0;
		if (!DecodeUTF8Codepoint(Ptr, End, CP))
		{
			break;
		}

		if (CP == '\n')
		{
			++LineIndex;
			CursorX = GetTextLineOffset(InHAlign, LineIndex < LineWidths.size() ? LineWidths[LineIndex] : 0.0f);
			CursorY -= LineAdvance;
			PreviousCP = 0;
			continue;
		}

		if (bUseGlyphMetrics)
		{
			if (PreviousCP != 0)
			{
				CursorX += static_cast<float>(InFont->FindKerning(PreviousCP, CP)) * MetricScale;
			}

			const FFontGlyph* Glyph = InFont->FindGlyph(CP);
			if (!Glyph)
			{
				Glyph = InFont->FindGlyph('?');
			}
			if (!Glyph)
			{
				CursorX += Advance;
				PreviousCP = CP;
				continue;
			}

			const float GlyphAdvance = (Glyph->XAdvance > 0 ? static_cast<float>(Glyph->XAdvance) * MetricScale : CharW) + MetricSpacing;
			if (Glyph->IsDrawable())
			{
				const float Left = CursorX + static_cast<float>(Glyph->XOffset) * MetricScale;
				const float Top = CursorY - static_cast<float>(Glyph->YOffset) * MetricScale;
				const float Right = Left + static_cast<float>(Glyph->Width) * MetricScale;
				const float Bottom = Top - static_cast<float>(Glyph->Height) * MetricScale;
				ExpandTextBounds(Bounds, Left, Right, Bottom, Top);
			}

			CursorX += GlyphAdvance;
			PreviousCP = CP;
			continue;
		}

		ExpandTextBounds(Bounds, CursorX, CursorX + CharW, CursorY - CharH, CursorY);
		CursorX += Advance;
		PreviousCP = CP;
	}

	return Bounds;
}

void UTextRenderComponent::UpdateWorldAABB() const
{
	// 빌보드는 어느 방향에서든 보이므로 view-independent 구형 바운드 사용
	const FTextRenderLayoutBounds Bounds = MeasureLocalLayoutBounds();
	if (!Bounds.bValid)
	{
		FVector WorldCenter = GetWorldLocation();
		WorldAABBMinLocation = WorldCenter;
		WorldAABBMaxLocation = WorldCenter;
		return;
	}

	const float MaxLocalY = std::max(std::abs(Bounds.MinY), std::abs(Bounds.MaxY));
	const float MaxLocalZ = std::max(std::abs(Bounds.MinZ), std::abs(Bounds.MaxZ));
	float MaxExtent = std::sqrt(MaxLocalY * MaxLocalY + MaxLocalZ * MaxLocalZ);

	FVector WorldScale = GetWorldScale();
	float ScaledMax = MaxExtent * std::max({ WorldScale.X, WorldScale.Y, WorldScale.Z });

	FVector WorldCenter = GetWorldLocation();
	FVector Extent(ScaledMax, ScaledMax, ScaledMax);

	WorldAABBMinLocation = WorldCenter - Extent;
	WorldAABBMaxLocation = WorldCenter + Extent;
}

bool UTextRenderComponent::LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult)
{
	// Ray 방향으로 빌보드 행렬을 계산 (CachedWorldMatrix는 active 카메라 기준이라 다른 뷰포트에서 틀림)
	if (!MeasureLocalLayoutBounds().bValid)
	{
		return false;
	}

	FMatrix PerRayBillboard = bBillboard ? ComputeBillboardMatrix(Ray.Direction) : GetWorldMatrix();
	FMatrix OutlineWorldMatrix = CalculateOutlineMatrix(PerRayBillboard);
	FMatrix InvWorldMatrix = OutlineWorldMatrix.GetInverse();

	FRay LocalRay;
	LocalRay.Origin = InvWorldMatrix.TransformPositionWithW(Ray.Origin);
	LocalRay.Direction = InvWorldMatrix.TransformVector(Ray.Direction).Normalized();


	if (std::abs(LocalRay.Direction.X) < 0.00111f) return false;

	float t = -LocalRay.Origin.X / LocalRay.Direction.X;

	if (t < 0.0f) return false;

	FVector LocalHitPos = LocalRay.Origin + LocalRay.Direction * t;

	if (LocalHitPos.Y >= -0.5f && LocalHitPos.Y <= 0.5f &&
		LocalHitPos.Z >= -0.5f && LocalHitPos.Z <= 0.5f)
	{
		FVector WorldHitPos = OutlineWorldMatrix.TransformPositionWithW(LocalHitPos);
		OutHitResult.Distance = (WorldHitPos - Ray.Origin).Length();
		OutHitResult.HitComponent = this;
		return true;
	}

	return false;
}

void UTextRenderComponent::PostDuplicate()
{
	UBillboardComponent::PostDuplicate();
	// 폰트 리소스 재바인딩 — 직렬화된 FontName 기준으로 ResourceManager에서 다시 lookup
	SetFont(FontName);
}

FString UTextRenderComponent::GetOwnerUUIDToString() const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return FName::None.ToString();
	}
	return std::to_string(OwnerActor->GetUUID());
}

FString UTextRenderComponent::GetOwnerNameToString() const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return FName::None.ToString();
	}

	FName Name = OwnerActor->GetFName();
	if (Name.IsValid())
	{
		return Name.ToString();
	}
	return FName::None.ToString();
}

UTextRenderComponent::UTextRenderComponent()
{
	SetFont(FontName);
}

bool UTextRenderComponent::ShouldExposeProperty(const FProperty& Property) const
{
	if (Property.OwnerClassName && strcmp(Property.OwnerClassName, "UBillboardComponent") == 0)
	{
		return false;
	}
	return USceneComponent::ShouldExposeProperty(Property);
}

void UTextRenderComponent::PostEditProperty(const char* PropertyName)
{
	// TextRender는 Billboard의 property를 숨기므로 post-edit도 SceneComponent로 직접 올린다.
	USceneComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "FontName") == 0 || strcmp(PropertyName, "Font") == 0)
	{
		SetFont(FontName);
		MarkProxyDirty(EDirtyFlag::Mesh);
	}
	else if (strcmp(PropertyName, "Text") == 0)
	{
		// CachedText 갱신을 위해 Mesh dirty 필요 + 바운드/아웃라인 행렬도 변함
		MarkProxyDirty(EDirtyFlag::Mesh);
		MarkProxyDirty(EDirtyFlag::Transform);
		MarkWorldBoundsDirty();
	}
	else if (strcmp(PropertyName, "FontSize") == 0 || strcmp(PropertyName, "Font Size") == 0 ||
		strcmp(PropertyName, "Spacing") == 0 || strcmp(PropertyName, "Letter Spacing") == 0 ||
		strcmp(PropertyName, "LineSpacing") == 0 || strcmp(PropertyName, "Line Spacing") == 0 ||
		strcmp(PropertyName, "CharWidth") == 0 || strcmp(PropertyName, "Char Width") == 0 ||
		strcmp(PropertyName, "CharHeight") == 0 || strcmp(PropertyName, "Char Height") == 0 ||
		strcmp(PropertyName, "HAlign") == 0 || strcmp(PropertyName, "Horizontal Align") == 0 ||
		strcmp(PropertyName, "VAlign") == 0 || strcmp(PropertyName, "Vertical Align") == 0 ||
		strcmp(PropertyName, "bBillboard") == 0 || strcmp(PropertyName, "Billboard") == 0)
	{
		MarkProxyDirty(EDirtyFlag::Mesh);
		MarkProxyDirty(EDirtyFlag::Transform);
		MarkWorldBoundsDirty();
	}
	else if (strcmp(PropertyName, "Color") == 0 || strcmp(PropertyName, "Opacity") == 0 ||
		strcmp(PropertyName, "bDepthTest") == 0 || strcmp(PropertyName, "Depth Test") == 0)
	{
		MarkProxyDirty(EDirtyFlag::Material);
	}
	else if (strcmp(PropertyName, "Visible") == 0)
	{
		MarkRenderVisibilityDirty();
	}
}


FMatrix UTextRenderComponent::CalculateOutlineMatrix() const
{
	const FTextRenderLayoutBounds Bounds = MeasureLocalLayoutBounds();
	if (!Bounds.bValid)
	{
		return FMatrix::Identity;
	}

	const float TotalLocalWidth = std::max(0.001f, Bounds.GetWidth());
	const float TotalLocalHeight = std::max(0.001f, Bounds.GetHeight());

	FMatrix ScaleMatrix = FMatrix::MakeScaleMatrix(FVector(1.0f, TotalLocalWidth, TotalLocalHeight));
	FMatrix TransMatrix = FMatrix::MakeTranslationMatrix(FVector(0.0f, Bounds.GetCenterY(), Bounds.GetCenterZ()));

	return (ScaleMatrix * TransMatrix) * CachedWorldMatrix;
}

FMatrix UTextRenderComponent::CalculateOutlineMatrix(const FMatrix& BillboardWorldMatrix) const
{
	const FTextRenderLayoutBounds Bounds = MeasureLocalLayoutBounds();
	if (!Bounds.bValid)
	{
		return FMatrix::Identity;
	}

	const float TotalLocalWidth = std::max(0.001f, Bounds.GetWidth());
	const float TotalLocalHeight = std::max(0.001f, Bounds.GetHeight());

	FMatrix ScaleMatrix = FMatrix::MakeScaleMatrix(FVector(1.0f, TotalLocalWidth, TotalLocalHeight));
	FMatrix TransMatrix = FMatrix::MakeTranslationMatrix(FVector(0.0f, Bounds.GetCenterY(), Bounds.GetCenterZ()));

	return (ScaleMatrix * TransMatrix) * BillboardWorldMatrix;
}

int32 UTextRenderComponent::GetUTF8Length(const FString& str) const {
	int32 count = 0;
	for (size_t i = 0; i < str.length(); ++i) {
		// UTF-8의 첫 바이트가 10xxxxxx 이 아니면 새로운 글자의 시작임
		if ((str[i] & 0xC0) != 0x80) count++;
	}
	return count;
}
