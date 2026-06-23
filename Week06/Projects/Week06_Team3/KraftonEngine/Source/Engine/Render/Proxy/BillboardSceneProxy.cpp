#include "Render/Proxy/BillboardSceneProxy.h"
#include "Components/BillboardComponent.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Pipeline/RenderBus.h"
#include "Render/Pipeline/RenderConstants.h"
#include <algorithm>

// ============================================================
// FBillboardSceneProxy
// ============================================================
FBillboardSceneProxy::FBillboardSceneProxy(UBillboardComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	bPerViewportUpdate = true;
	bShowAABB = false;
	// Billboard는 per-viewport 회전 특성상 GPU occlusion에서 false negative가 발생할 수 있다.
	// Frustum culling은 유지하고, occlusion만 제외한다.
	bSkipGPUOcclusion = true;
	// 텍스처가 세팅돼 있으면 SubUV batcher 경로를 사용한다 (1x1 atlas로 단일 텍스처 렌더링).
	// 텍스처가 없으면 기존 Primitive 셰이더 경로 유지.
	bBatcherRendered = (InComponent && InComponent->GetTexture() != nullptr);
}

UBillboardComponent* FBillboardSceneProxy::GetBillboardComponent() const
{
	return static_cast<UBillboardComponent*>(Owner);
}

// ============================================================
// UpdateMesh — Quad 메시 캐싱 + 텍스처 유무에 따라 batcher/Primitive 경로 결정
// ============================================================
void FBillboardSceneProxy::UpdateMesh()
{
	MeshBuffer = Owner->GetMeshBuffer();

	UBillboardComponent* Comp = GetBillboardComponent();
	const bool bHasTexture = (Comp && Comp->GetTexture() != nullptr);
	bBatcherRendered = bHasTexture;

	if (bHasTexture)
	{
		// BillboardBatcher 가 자체 셰이더를 사용 — Shader 는 SelectionMask 아웃라인용으로
		// Primitive 를 캐싱해 둔다 (Quad 메시는 FVertex 레이아웃).
		Shader = FShaderManager::Get().GetShader(EShaderType::Primitive);
		Pass = ERenderPass::Billboard;

		// ID picking에서 텍스처 alpha 테스트를 수행할 수 있도록 섹션 텍스처 정보를 함께 유지한다.
		SectionDraws.clear();
		FMeshSectionDraw Draw = {};
		Draw.DiffuseSRV = Comp->GetTexture() ? Comp->GetTexture()->SRV : nullptr;
		Draw.FirstIndex = 0;
		Draw.IndexCount = MeshBuffer ? MeshBuffer->GetIndexBuffer().GetIndexCount() : 0;
		SectionDraws.push_back(Draw);
	}
	else
	{
		Shader = FShaderManager::Get().GetShader(EShaderType::Primitive);
		Pass = ERenderPass::Opaque;
		SectionDraws.clear();
	}
	UpdateSortKey();
}

// ============================================================
// CollectEntries — 텍스처가 있을 때만 Billboard batcher에 엔트리 제출
// ============================================================
void FBillboardSceneProxy::CollectEntries(FRenderBus& Bus)
{
	UBillboardComponent* Comp = GetBillboardComponent();
	if (!Comp) return;

	const FTextureResource* Texture = Comp->GetTexture();
	if (!Texture || !Texture->IsLoaded()) return;

	FBillboardEntry Entry = {};
	Entry.PerObject = PerObjectConstants;
	Entry.Billboard.Texture = Texture;
	Entry.Billboard.Width   = Comp->GetWidth();
	Entry.Billboard.Height  = Comp->GetHeight();
    Entry.bSelected = bSelected;
	Bus.AddBillboardEntry(std::move(Entry));
}

// ============================================================
// UpdatePerViewport — 뷰포트 카메라 기반 빌보드 행렬 갱신
// ============================================================
void FBillboardSceneProxy::UpdatePerViewport(const FRenderBus& Bus)
{
	UBillboardComponent* Comp = GetBillboardComponent();
	bVisible = Comp->IsVisible();
	if (!bVisible) return;

	// 컬러 패스(BillboardBatcher)와 ID/Selection 패스의 축 기준을 완전히 동일하게 맞춘다.
	// 기존 WorldUp x Forward 재구성은 카메라 right 축 부호가 달라져 스프라이트가 반전될 수 있다.
	const FVector CameraForward = (Bus.GetCameraForward() * -1.0f).Normalized();
	const FVector CameraRight = Bus.GetCameraRight().Normalized();
	const FVector CameraUp = Bus.GetCameraUp().Normalized();

	FMatrix BillboardMatrix;
	BillboardMatrix.SetAxes(CameraForward, CameraRight, CameraUp);
	BillboardMatrix = BillboardMatrix * FMatrix::MakeTranslationMatrix(Comp->GetWorldLocation());

	// 주의: 컬러 렌더(BillboardBatcher)는 Width/Height를 반영해 쿼드를 만들지만,
	// ID/Selection 경로는 Proxy의 PerObject 행렬을 사용한다.
	// 여기서 같은 스케일을 반영하지 않으면 "보이는 이미지보다 큰 영역"이 피킹된다.
	const float WidthScale = (std::max)(0.0001f, Comp->GetWidth() * 0.5f);
	const float HeightScale = (std::max)(0.0001f, Comp->GetHeight() * 0.5f);
	const FMatrix SpriteSizeScale = FMatrix::MakeScaleMatrix(FVector(1.0f, WidthScale, HeightScale));
	BillboardMatrix = SpriteSizeScale * BillboardMatrix;

	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(BillboardMatrix);
	MarkPerObjectCBDirty();
}
