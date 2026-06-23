#include "Render/Proxy/TextRenderSceneProxy.h"
#include "Component/Primitive/TextRenderComponent.h"
#include "Render/Types/FrameContext.h"
#include "Render/Shader/ShaderManager.h"
#include "Materials/Material.h"
#include "Object/Reflection/ObjectFactory.h"

#include <algorithm>

// ============================================================
// FTextRenderSceneProxy
// ============================================================
FTextRenderSceneProxy::FTextRenderSceneProxy(UTextRenderComponent* InComponent)
	: FBillboardSceneProxy(static_cast<UBillboardComponent*>(InComponent))
{
}

FTextRenderSceneProxy::~FTextRenderSceneProxy()
{
	if (TextMaterial)
	{
		UObjectManager::Get().DestroyObject(TextMaterial);
		TextMaterial = nullptr;
	}
}

void FTextRenderSceneProxy::UpdateTransform()
{
	FBillboardSceneProxy::UpdateTransform();
	if (UTextRenderComponent* TextComp = GetTextRenderComponent())
	{
		CachedComponentWorldMatrix = TextComp->GetWorldMatrix();
	}
}

void FTextRenderSceneProxy::UpdateMaterial()
{
	UTextRenderComponent* TextComp = GetTextRenderComponent();
	if (!TextComp)
	{
		return;
	}

	CachedColor = TextComp->GetColor();
	CachedColor.W *= TextComp->GetOpacity();
	CachedDepthStencil = TextComp->IsDepthTestEnabled()
		? EDepthStencilState::DepthReadOnly
		: EDepthStencilState::NoDepth;
}

void FTextRenderSceneProxy::UpdateMesh()
{
	// SelectionMask 아웃라인 패스에서 사용할 mesh/shader
	MeshBuffer = GetOwner()->GetMeshBuffer();
	ProxyFlags |= EPrimitiveProxyFlags::FontBatched;

	if (!TextMaterial)
	{
		TextMaterial = UMaterial::CreateTransient(
			ERenderPass::Transparent, EBlendState::AlphaBlend,
			EDepthStencilState::Default, ERasterizerState::SolidBackCull,
			FShaderManager::Get().GetOrCreate(EShaderPath::Primitive));
	}

	SectionDraws.clear();
	if (MeshBuffer && TextMaterial)
	{
		uint32 IdxCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
		SectionDraws.push_back({ TextMaterial, 0, IdxCount });
	}

	// 텍스트/폰트 데이터 캐싱 (UpdatePerViewport에서 Owner 접근 제거)
	UTextRenderComponent* TextComp = GetTextRenderComponent();
	CachedText = TextComp->GetText();
	CachedFontScale = TextComp->GetFontSize();
	CachedFont = TextComp->GetFont();
	UpdateMaterial();
	CachedCharWidth = TextComp->GetCharWidth();
	CachedCharHeight = TextComp->GetCharHeight();
	CachedSpacing = TextComp->GetSpacing();
	CachedLineSpacing = TextComp->GetLineSpacing();
	CachedHorizontalAlign = static_cast<int32>(TextComp->GetHorizontalAlignment());
	CachedVerticalAlign = static_cast<int32>(TextComp->GetVerticalAlignment());
	CachedBillboardEnabled = TextComp->IsTextBillboardEnabled();
	CachedDepthStencil = TextComp->IsDepthTestEnabled()
		? EDepthStencilState::DepthReadOnly
		: EDepthStencilState::NoDepth;
}

UTextRenderComponent* FTextRenderSceneProxy::GetTextRenderComponent() const
{
	return static_cast<UTextRenderComponent*>(GetOwner());
}

// ============================================================
// UpdatePerViewport — 빌보드 행렬 계산 + 아웃라인 행렬 갱신
// ============================================================
void FTextRenderSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	UpdateVisibility();
	if (!bVisible)
	{
		return;
	}

	// 텍스트/폰트 미설정 시 비가시
	if (CachedText.empty() || !CachedFont || !CachedFont->IsLoaded())
	{
		bVisible = false;
		return;
	}

	if (!Frame.RenderOptions.ShowFlags.bBillboardText)
	{
		bVisible = false;
		return;
	}

	// 빌보드 행렬
	if (CachedBillboardEnabled)
	{
		FVector BillboardForward = Frame.CameraForward * -1.0f;
		FMatrix RotMatrix;
		RotMatrix.SetAxes(BillboardForward, Frame.CameraRight * -1.0f, Frame.CameraUp);
		CachedBillboardMatrix = FMatrix::MakeScaleMatrix(CachedScale)
			* RotMatrix * FMatrix::MakeTranslationMatrix(CachedLocation);
		CachedTextRight = Frame.CameraRight;
		CachedTextUp = Frame.CameraUp;
	}
	else
	{
		CachedBillboardMatrix = CachedComponentWorldMatrix;
		CachedTextRight = FVector(
			CachedComponentWorldMatrix.M[1][0],
			CachedComponentWorldMatrix.M[1][1],
			CachedComponentWorldMatrix.M[1][2]).Normalized();
		CachedTextUp = FVector(
			CachedComponentWorldMatrix.M[2][0],
			CachedComponentWorldMatrix.M[2][1],
			CachedComponentWorldMatrix.M[2][2]).Normalized();
	}

	// SelectionMask용 아웃라인 행렬 (캐싱된 CharWidth/CharHeight로 직접 계산)
	const FTextRenderLayoutBounds Bounds = UTextRenderComponent::MeasureLocalLayoutBounds(
		CachedText,
		CachedFont,
		CachedFontScale,
		CachedCharWidth,
		CachedCharHeight,
		CachedSpacing,
		CachedLineSpacing,
		static_cast<ETextHAlign>(CachedHorizontalAlign),
		static_cast<ETextVAlign>(CachedVerticalAlign));

	if (Bounds.bValid)
	{
		float TotalLocalWidth = std::max(0.001f, Bounds.GetWidth());
		float TotalLocalHeight = std::max(0.001f, Bounds.GetHeight());

		FMatrix ScaleMatrix = FMatrix::MakeScaleMatrix(FVector(1.0f, TotalLocalWidth, TotalLocalHeight));
		FMatrix TransMatrix = FMatrix::MakeTranslationMatrix(FVector(0.0f, Bounds.GetCenterY(), Bounds.GetCenterZ()));

		FMatrix OutlineMatrix = (ScaleMatrix * TransMatrix) * CachedBillboardMatrix;
		PerObjectConstants = FPerObjectConstants::FromWorldMatrix(OutlineMatrix);
	}
	else
	{
		PerObjectConstants = FPerObjectConstants::FromWorldMatrix(FMatrix::Identity);
	}
	MarkPerObjectCBDirty();
}
