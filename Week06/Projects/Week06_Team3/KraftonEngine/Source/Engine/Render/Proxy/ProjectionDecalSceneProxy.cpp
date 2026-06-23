#include "Render/Proxy/ProjectionDecalSceneProxy.h"

#include "Components/ProjectionDecalComponent.h"
#include "Materials/MaterialInterface.h"
#include "Render/Pipeline/RenderBus.h"
#include "Render/Resource/ConstantBufferPool.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Types/VertexTypes.h"
#include "Runtime/Engine.h"
#include "Texture/Texture2D.h"

#include <cstdint>

namespace
{
	constexpr float DecalArrowScreenScale = 0.12f;
	constexpr float DecalArrowMinScale = 0.01f;
	constexpr float DecalArrowInnerOpacity = 0.35f;
	const FVector4 DecalArrowColorTint(0.31f, 0.31f, 0.78f, 1.0f);

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

	FMatrix BuildUnitRotationMatrix(const UProjectionDecalComponent* ProjectionDecalComp)
	{
		FMatrix Rotation = ProjectionDecalComp->GetWorldMatrix();

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

FProjectionDecalSceneProxy::FProjectionDecalSceneProxy(UProjectionDecalComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	bSupportsOutline = false;
	bShowAABB = false;
	// Thin projected Projection Decals are unstable in occlusion tests and can toggle
	// visibility when camera/viewport changes.
	bSkipGPUOcclusion = true;
}

UProjectionDecalComponent* FProjectionDecalSceneProxy::GetProjectionDecalComponent() const
{
	return static_cast<UProjectionDecalComponent*>(Owner);
}

void FProjectionDecalSceneProxy::UpdateTransform()
{
	UProjectionDecalComponent* ProjectionDecal = GetProjectionDecalComponent();
	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(ProjectionDecal->GetProjectionDecalLocalToWorldMatrix());
	PerObjectConstants.Color = ProjectionDecal->GetProjectionDecalColor().ToVector4();
	CachedWorldPos = PerObjectConstants.Model.GetLocation();
	CachedBounds = ProjectionDecal->GetWorldBoundingBox();
	MarkPerObjectCBDirty();
}

void FProjectionDecalSceneProxy::UpdateMaterial()
{
	UProjectionDecalComponent* ProjectionDecal = GetProjectionDecalComponent();
	UpdateSortKey();
	if (ProjectionDecal)
	{
		MaterialSortKey = static_cast<uint32>(ProjectionDecal->GetSortOrder() - INT32_MIN);
	}
}

void FProjectionDecalSceneProxy::UpdateMesh()
{
	UProjectionDecalComponent* ProjectionDecal = GetProjectionDecalComponent();
	ProjectionDecal->EnsureProjectionDecalMeshBuilt();
	const FProjectionDecalRenderableMesh& SrcMesh = ProjectionDecal->GetRenderableMesh();

	MeshBuffer = nullptr;
	SectionDraws.clear();

	Shader = FShaderManager::Get().GetShader(EShaderType::ProjectionDecal);
	Pass = ERenderPass::ProjectionDecal;

	if (SrcMesh.IsEmpty())
	{
		UpdateSortKey();
		return;
	}

	TMeshData<FVertexPNCT> MeshData;
	MeshData.Vertices.reserve(SrcMesh.Vertices.size());
	MeshData.Indices = SrcMesh.Indices;

	for (const FProjectionDecalRenderableVertex& Src : SrcMesh.Vertices)
	{
		FVertexPNCT Dst = {};
		Dst.Position = Src.Position;
		Dst.Normal = Src.Normal;
		Dst.Color = Src.Color;
		Dst.UV = Src.UV;
		MeshData.Vertices.push_back(Dst);
	}

	OwnedMeshBuffer.Create(GEngine->GetRenderer().GetFD3DDevice().GetDevice(), MeshData);
	MeshBuffer = &OwnedMeshBuffer;

	for (const FProjectionDecalRenderableSection& SrcSection : SrcMesh.Sections)
	{
		FMeshSectionDraw Draw = {};
		Draw.FirstIndex = SrcSection.FirstIndex;
		Draw.IndexCount = SrcSection.IndexCount;

		if (UMaterialInterface* Material = ProjectionDecal->GetProjectionDecalMaterial())
		{
			if (UTexture2D* Diffuse = Material->GetDiffuseTexture())
			{
				Draw.DiffuseSRV = Diffuse->GetSRV();
			}
			Draw.DiffuseColor = Material->GetDiffuseColor();
		}
		else if (const FTextureResource* Texture = ProjectionDecal->GetProjectionDecalTexture())
		{
			Draw.DiffuseSRV = Texture->SRV;
			Draw.DiffuseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		}
		Draw.bIsUVScroll = ProjectionDecal->IsProjectionDecalUVScrollEnabled() ? 1 : 0;

		SectionDraws.push_back(Draw);
	}

	UpdateSortKey();
	MaterialSortKey = static_cast<uint32>(ProjectionDecal->GetSortOrder() - INT32_MIN);
}

void FProjectionDecalSceneProxy::CollectEntries(FRenderBus& Bus)
{
	FPrimitiveSceneProxy::CollectEntries(Bus);

	if (!bVisible || !bSelected)
	{
		return;
	}

	FOBBEntry Entry;
	Entry.OBB.LocalBox = FBoundingBox(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f));
	Entry.OBB.Transform = PerObjectConstants.Model;
	Entry.OBB.Color = FColor::Green();
	Bus.AddOBBEntry(std::move(Entry));
}

FProjectionDecalArrowSceneProxy::FProjectionDecalArrowSceneProxy(UProjectionDecalComponent* InComponent, bool bInner)
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

void FProjectionDecalArrowSceneProxy::UpdateMesh()
{
	MeshBuffer = &FMeshBufferManager::Get().GetMeshBuffer(EMeshShape::TransGizmo);
	Shader = FShaderManager::Get().GetShader(EShaderType::Gizmo);

	SectionDraws.clear();

	FMeshSectionDraw Draw = {};
	Draw.IndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
	SectionDraws.push_back(Draw);

	UpdateSortKey();
}

void FProjectionDecalArrowSceneProxy::UpdatePerViewport(const FRenderBus& Bus)
{
	UProjectionDecalComponent* ProjectionDecalComp = GetProjectionDecalComponent();
	FPrimitiveSceneProxy* ProjectionDecalProxy = ProjectionDecalComp ? ProjectionDecalComp->GetSceneProxy() : nullptr;
	if (!ProjectionDecalComp
		|| !ProjectionDecalProxy
		|| !ProjectionDecalComp->IsVisible()
		|| !ProjectionDecalProxy->bVisible
		|| !ProjectionDecalProxy->bSelected
		|| !ProjectionDecalProxy->bInVisibleSet
		|| !Bus.GetShowFlags().bGizmo
		|| !Bus.GetShowFlags().bDecal)
	{
		bVisible = false;
		return;
	}

	bVisible = true;

	const FVector WorldLocation = ProjectionDecalComp->GetWorldLocation();
	const float PerViewScale = ComputeDecalArrowScale(Bus, WorldLocation);
	const FMatrix RotationMatrix = BuildUnitRotationMatrix(ProjectionDecalComp);

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

UProjectionDecalComponent* FProjectionDecalArrowSceneProxy::GetProjectionDecalComponent() const
{
	return static_cast<UProjectionDecalComponent*>(Owner);
}

