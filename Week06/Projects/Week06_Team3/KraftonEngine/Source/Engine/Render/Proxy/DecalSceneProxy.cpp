#include "Render/Proxy/DecalSceneProxy.h"
//#include "Render/Pipeline/RenderBus.h"
//#include "Components/DecalComponent.h"
//#include "Materials/MaterialInterface.h"
//#include "Render/Resource/ConstantBufferPool.h"
//#include "Render/Resource/MeshBufferManager.h"
//#include "Render/Resource/ShaderManager.h"
//#include "Texture/Texture2D.h"
#include "Render/Proxy/DecalGeometryChecker.h"
#include "Render/Pipeline/RenderBus.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInterface.h"
#include "Render/Resource/ConstantBufferPool.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Render/Resource/ShaderManager.h"
#include "Texture/Texture2D.h"
#include "GameFramework/World.h"

namespace
{
	constexpr float DecalArrowScreenScale = 0.12f;
	constexpr float DecalArrowMinScale = 0.01f;
	constexpr float DecalArrowInnerOpacity = 0.35f;
	const FVector4 DecalArrowColorTint(0.31f, 0.31f, 0.78f, 1.0f);

    struct FDecalObjectConstants
    {
        FGPUFloat4x4 InverseDecalModel;
    };

	float ComputeDecalArrowScale(const FRenderBus& Bus, const FVector& WorldLocation)
	{
		if (Bus.IsOrtho())
		{
			const float Scale = Bus.GetOrthoWidth() * DecalArrowScreenScale;
			return (Scale < DecalArrowMinScale) ? DecalArrowMinScale : Scale;
		}

		const float Distance = FVector::Distance(Bus.GetCameraPosition(), WorldLocation);
		const float Scale = Distance * DecalArrowScreenScale;
		return (Scale < DecalArrowMinScale) ? DecalArrowMinScale : Scale;
	}

	FMatrix BuildUnitRotationMatrix(const UDecalComponent* DecalComp)
	{
		FMatrix Rotation = DecalComp->GetWorldMatrix();

		for (int32 Row = 0; Row < 3; ++Row)
		{
			FVector Axis(Rotation.M[Row][0], Rotation.M[Row][1], Rotation.M[Row][2]);
			if (Axis.Dot(Axis) > 1e-8f)
			{
				Axis.Normalize();
			}

			Rotation.M[Row][0] = Axis.X;
			Rotation.M[Row][1] = Axis.Y;
			Rotation.M[Row][2] = Axis.Z;
			Rotation.M[Row][3] = 0.0f;
		}

		Rotation.M[3][0] = 0.0f;
		Rotation.M[3][1] = 0.0f;
		Rotation.M[3][2] = 0.0f;
		Rotation.M[3][3] = 1.0f;
		return Rotation;
	}
}

FDecalSceneProxy::FDecalSceneProxy(UDecalComponent* InComponent)
    : FPrimitiveSceneProxy(InComponent)
{
	bShowAABB = false;
	bSupportsOutline = false;
	bPerViewportUpdate = true; // OBB-Frustum 컬링을 위해 매 프레임 UpdatePerViewport 호출
	// Single viewport에서만 활성화되는 GPU occlusion에 의해
	// 선택 볼륨(Decal/SpotLight) 가이드가 사라지지 않도록 제외한다.
	bSkipGPUOcclusion = true;
}

void FDecalSceneProxy::UpdateMesh()
{
    MeshBuffer = Owner->GetMeshBuffer();
	Shader = FShaderManager::Get().GetShader(EShaderType::Decal);
	Pass = ERenderPass::Decal;
    RebuildSectionDraw();
}

void FDecalSceneProxy::UpdateMaterial()
{
    RebuildSectionDraw();
}

void FDecalSceneProxy::UpdateTransform()
{
    FPrimitiveSceneProxy::UpdateTransform();
    if (UDecalComponent* DecalComp = GetDecalComponent())
    {
        PerObjectConstants.Model = DecalComp->GetTransformIncludingDecalSize();
        CachedWorldPos = PerObjectConstants.Model.GetLocation();
        PerObjectConstants.Color = DecalComp->DecalColor.ToVector4();
        SortOrder = DecalComp->GetDecalSortOrder();

        auto& DecalCB = ExtraCB.Bind<FDecalObjectConstants>(
            FConstantBufferPool::Get().GetBuffer(ECBSlot::Gizmo, sizeof(FDecalObjectConstants)),
            ECBSlot::Gizmo);
        DecalCB.InverseDecalModel = PerObjectConstants.Model.GetInverse();

        MarkPerObjectCBDirty();
    }
}

void FDecalSceneProxy::CollectEntries(FRenderBus& Bus)
{
	// 1. 부모 클래스의 기본 수집 로직 호출 (필수)
	FPrimitiveSceneProxy::CollectEntries(Bus);

	if (!bVisible)
	{
		return;
	}
	
	if (bSelected)
	{
		FOBBEntry Entry;

		// 크기가 1인 로컬 단위 육면체 정의 (-0.5 ~ 0.5)
		// 데칼의 투영 범위를 나타냅니다.
		Entry.OBB.LocalBox = FBoundingBox(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f));

		// UpdateTransform()에서 캐싱해둔 최신 데칼 월드 변환 행렬 (스케일, 회전, 위치 모두 포함)
		Entry.OBB.Transform = PerObjectConstants.Model;

		// 엔진에 구현된 FColor::Green()을 사용하여 깔끔한 초록색 지정
		Entry.OBB.Color = FColor::Green();

		// 렌더 버스에 OBB 엔트리를 추가하여 LineBatcher가 그리도록 전달!
		Bus.AddOBBEntry(std::move(Entry));
	}

}

void FDecalSceneProxy::UpdatePerViewport(const FRenderBus& Bus)
{
	UDecalComponent* DecalComp = GetDecalComponent();

	// 컴포넌트 유효성 및 가시성 체크
	if (!DecalComp || !DecalComp->IsVisible())
	{
		bVisible = false;
		return;
	}

	//// OBB-Frustum SAT 컬링
	//// Decal의 OBB (단위 큐브를 DecalSize + 회전 + 위치로 변환한 행렬)를
	//// 절두체 6개 평면에 대해 8코너 테스트 - 어떤 평면에서 모든 코너가 외부면 false
	//const FMatrix OBBTransform = DecalComp->GetTransformIncludingDecalSize();
	//bVisible = Bus.GetConvexVolume().IntersectOBB(OBBTransform);
	
	//// ---- Stage 0: OBB-Frustum SAT 컬링 ----
	//// Decal OBB의 8코너를 6개 절두체 평면에 대해 테스트
	//// 어떤 평면에서 모든 코너가 외부면 → 절두체 밖 → 스킵
	//const FMatrix OBBTransform = DecalComp->GetTransformIncludingDecalSize();
	//if (!Bus.GetConvexVolume().IntersectOBB(OBBTransform))
	//{
	//	bVisible = false;
	//	return;
	//}

	//// ---- Stage 1~5: 지오메트리 교차 판별 ----
	//// BVH Broad/Narrow Phase → 로컬 변환 → Coarse AABB → SAT
	//// 씬에 지오메트리가 하나도 없으면 Draw Call 생략
	//UWorld* World = DecalComp->GetWorld();
	//if (World)
	//{
	//	FDecalGeometryChecker Checker;
	//	bVisible = Checker.HasOverlappingGeometry(DecalComp, *World);
	//}
	//else
	//{
	//	// World 접근 불가 시 Stage 0 통과 결과만 사용 (Draw Call 허용)
	//	bVisible = true;
	//}

	// ---- Stage 0: OBB-Frustum SAT 컬링 ----
	// 절두체 밖이면 선택 상태여도 렌더링 대상 제외
	const FMatrix OBBTransform = DecalComp->GetTransformIncludingDecalSize();
	if (!Bus.GetConvexVolume().IntersectOBB(OBBTransform))
	{
		bVisible = false;
		return;
	}

	// ---- Stage 1~5: 지오메트리 교차 판별 ----
	// BVH Broad/Narrow Phase → 로컬 변환 → Coarse AABB → SAT
	bool bHasGeometry = true;
	UWorld* World = DecalComp->GetWorld();
	if (World)
	{
		FDecalGeometryChecker Checker;
       int32 OverlappingObjectCount = 0;
		bHasGeometry = Checker.HasOverlappingGeometry(DecalComp, *World, &OverlappingObjectCount);
        LastOverlappingObjectCount = (OverlappingObjectCount > 0) ? static_cast<uint32>(OverlappingObjectCount) : 0;
	}
	else
	{
		LastOverlappingObjectCount = 0;
	}

	// 지오메트리 상태가 바뀐 경우에만 SectionDraws 재구성
	// - 교차 O → SectionDraws 복원 (실제 데칼 프로젝션 활성화)
	// - 교차 X → SectionDraws 비움 (프로젝션 억제, OBB 박스만 남김)
	if (bHasGeometry != bDecalProjectionVisible)
	{
		bDecalProjectionVisible = bHasGeometry;
		if (bDecalProjectionVisible)
			RebuildSectionDraw();
		else
			SectionDraws.clear();
	}

	// 선택 상태이면 OBB 박스를 위해 CollectEntries가 호출되도록 bVisible = true 유지
	bVisible = bSelected || bDecalProjectionVisible;
}

UDecalComponent* FDecalSceneProxy::GetDecalComponent() const
{
    return static_cast<UDecalComponent*>(Owner);
}

void FDecalSceneProxy::RebuildSectionDraw()
{
    SectionDraws.clear();

	FMeshSectionDraw Draw = {};
	if (UDecalComponent* DecalComp = GetDecalComponent())
	{
		if (const FTextureResource* Texture = DecalComp->GetDecalTexture())
		{
			Draw.DiffuseSRV = Texture->SRV;
			Draw.DiffuseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else if (UMaterialInterface* Material = DecalComp->GetDecalMaterial())
		{
			if (UTexture2D* DiffuseTexture = Material->GetDiffuseTexture())
			{
				Draw.DiffuseSRV = DiffuseTexture->GetSRV();
			}
			Draw.DiffuseColor = Material->GetDiffuseColor();
		}
		else
		{
			Draw.DiffuseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		}
    }

    if (MeshBuffer && Draw.DiffuseSRV)
    {
        Draw.IndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
    }

    SectionDraws.push_back(Draw);
    UpdateSortKey();
}

FDecalArrowSceneProxy::FDecalArrowSceneProxy(UDecalComponent* InComponent, bool bInner)
	: FPrimitiveSceneProxy(InComponent)
	, bIsInner(bInner)
{
	bPerViewportUpdate = true;
	bNeverCull = true;
	bSkipGPUOcclusion = true;
	bShowAABB = false;
	bSupportsOutline = false;
	Pass = bInner ? ERenderPass::GizmoInner : ERenderPass::GizmoOuter;
}

void FDecalArrowSceneProxy::UpdateMesh()
{
	MeshBuffer = &FMeshBufferManager::Get().GetMeshBuffer(EMeshShape::TransGizmo);
	Shader = FShaderManager::Get().GetShader(EShaderType::Gizmo);

	SectionDraws.clear();

	FMeshSectionDraw Draw = {};
	Draw.IndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
	SectionDraws.push_back(Draw);

	UpdateSortKey();
}

void FDecalArrowSceneProxy::UpdatePerViewport(const FRenderBus& Bus)
{
	UDecalComponent* DecalComp = GetDecalComponent();
	FPrimitiveSceneProxy* DecalProxy = DecalComp ? DecalComp->GetSceneProxy() : nullptr;
	if (!DecalComp
		|| !DecalProxy
		|| !DecalComp->IsVisible()
		|| !DecalProxy->bVisible
		|| !DecalProxy->bSelected
		|| !DecalProxy->bInVisibleSet
		|| !Bus.GetShowFlags().bGizmo
		|| !Bus.GetShowFlags().bDecal)
	{
		bVisible = false;
		return;
	}

	bVisible = true;

	const FVector WorldLocation = DecalComp->GetWorldLocation();
	const float PerViewScale = ComputeDecalArrowScale(Bus, WorldLocation);
	const FMatrix RotationMatrix = BuildUnitRotationMatrix(DecalComp);

	PerObjectConstants = FPerObjectConstants{
		FMatrix::MakeScaleMatrix(FVector(PerViewScale, PerViewScale, PerViewScale))
			* RotationMatrix
			* FMatrix::MakeTranslationMatrix(WorldLocation),
		DecalArrowColorTint
	};
	CachedWorldPos = WorldLocation;
	MarkPerObjectCBDirty();

	auto& G = ExtraCB.Bind<FGizmoConstants>(
		FConstantBufferPool::Get().GetBuffer(ECBSlot::Gizmo, sizeof(FGizmoConstants)),
		ECBSlot::Gizmo);
	G.ColorTint = DecalArrowColorTint;
	G.bIsInnerGizmo = bIsInner ? 1u : 0u;
	G.bClicking = 0u;
	G.SelectedAxis = 0xFFFFFFFFu;
	G.HoveredAxisOpacity = DecalArrowInnerOpacity;
	G.AxisMask = 0x1u;
	G.bOverrideAxisColor = 1u;
}

UDecalComponent* FDecalArrowSceneProxy::GetDecalComponent() const
{
	return static_cast<UDecalComponent*>(Owner);
}
