#include "BillboardComponent.h"
#include "Collision/RayUtils.h"
#include "Math/Matrix.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Pipeline/PrimitiveProxy.h"
#include "Texture/Texture2D.h"
#include "Core/PropertyTypes.h"
#include "Engine/Runtime/Engine.h"
#include "GameFramework/AActor.h"

class FBillboardProxy : public FPrimitiveProxy
{
public:
	FBillboardProxy(UBillboardComponent* InOwner) : FPrimitiveProxy(InOwner) {}

	void UpdateProxy() override
	{
	}

	void SubmitRenderCommand(FViewContext& View) override
	{
		if (IsDirty())
		{
			UpdateProxy();
			bIsDirty = false;
		}

		UBillboardComponent* Billboard = static_cast<UBillboardComponent*>(Owner);
		if (!Billboard->IsVisible()) return;

		// Billboard proxy bypasses base SubmitRenderCommand path, so keep occlusion bounds in sync here.
		RefreshOcclusionCache();

		FMeshBuffer* Buffer = Billboard->GetMeshBuffer();
		if (!Buffer || !Buffer->IsValid()) return;

		const FMatrix BillboardMatrix = FBillboardProxy::ComputeBillboardMatrix(
			View.GetCameraForward(),
			Billboard->GetWorldScale(),
			Billboard->GetWorldLocation());

		FRenderCommand Cmd        = {};
		Cmd.MeshBuffer            = Buffer;
		Cmd.Shader                = FShaderManager::Get().GetShader(EShaderType::Billboard);
		Cmd.PerObjectConstants    = FPerObjectConstants::FromWorldMatrix(BillboardMatrix);
		Cmd.SpriteSRV             = Billboard->GetSprite() ? Billboard->GetSprite()->GetSRV() : nullptr;
		if (AActor* ActorOwner = Billboard->GetOwner())
		{
			Cmd.PickingId = ActorOwner->GetUUID();
		}
		else
		{
			Cmd.PickingId = Billboard->GetUUID();
		}
		const ERenderPass BillboardPass =
			Billboard->IsVisualizationComponent()
			? ERenderPass::VisualizationBillboard
			: ERenderPass::Billboard;
		View.AddCommand(BillboardPass, Cmd);

		if (bSelected && Billboard->SupportsOutline())
		{
			FRenderCommand MaskCmd    = {};
			MaskCmd.MeshBuffer        = Buffer;
			MaskCmd.Shader            = FShaderManager::Get().GetShader(EShaderType::Primitive);
			MaskCmd.PerObjectConstants = FPerObjectConstants::FromWorldMatrix(BillboardMatrix);
			View.AddCommand(ERenderPass::SelectionMask, MaskCmd);
		}

		if (bSelected && View.GetShowFlags().bBoundingVolume)
		{
			FAABBEntry Entry = {};
			FBoundingBox Box = Billboard->GetWorldBoundingBox();
			Entry.AABB.Min = Box.Min;
			Entry.AABB.Max = Box.Max;
			Entry.AABB.Color = FColor::White();
			View.AddAABBEntry(std::move(Entry));
		}
	}

	static FMatrix ComputeBillboardMatrix(
		const FVector& CameraForward,
		const FVector& WorldScale,
		const FVector& WorldLocation)
	{
		FVector Forward = (CameraForward * -1.0f).Normalized();
		FVector WorldUp = FVector(0.0f, 0.0f, 1.0f);
		if (std::abs(Forward.Dot(WorldUp)) > 0.99f)
			WorldUp = FVector(0.0f, 1.0f, 0.0f);

		const FVector Right = WorldUp.Cross(Forward).Normalized();
		const FVector Up    = Forward.Cross(Right).Normalized();

		FMatrix Rot;
		Rot.SetAxes(Forward, Right, Up);

		return FMatrix::MakeScaleMatrix(WorldScale)
			* Rot
			* FMatrix::MakeTranslationMatrix(WorldLocation);
	}
};

// ============================================================
DEFINE_CLASS(UBillboardComponent, UPrimitiveComponent)

FPrimitiveProxy* UBillboardComponent::CreateProxy()
{
	return new FBillboardProxy(this);
}

FMeshBuffer* UBillboardComponent::GetMeshBuffer() const
{
	return &FMeshBufferManager::Get().GetMeshBuffer(EMeshShape::SpriteQuad);
}

const FMeshData* UBillboardComponent::GetMeshData() const
{
	// SpriteQuad 는 FTextureVertex 기반이므로 FMeshData(FVertex) 접근 불가 — nullptr 반환
	return &FMeshBufferManager::Get().GetMeshData(EMeshShape::Quad);
}

void UBillboardComponent::TickComponent(float DeltaTime)
{
	// if (!GetOwner() || !GetOwner()->GetWorld()) return;
	UpdateWorldAABB();
}

void UBillboardComponent::SetSprite(UTexture2D* NewSprite)
{
	Sprite = NewSprite;
	SpritePath = NewSprite ? NewSprite->GetSourcePath() : "None";
}

// void UBillboardComponent::UpdateWorldAABB() const
// {
// 	// TODO: 아직 계산 검증 안 됨
// 	const float   NewScale    = std::max({ GetWorldScale().X, GetWorldScale().Y, GetWorldScale().Z });
// 	const FVector WorldCenter = GetWorldLocation();
// 	const FVector Extent(NewScale, NewScale, NewScale);
//
// 	WorldAABBMinLocation = WorldCenter - Extent;
// 	WorldAABBMaxLocation = WorldCenter + Extent;
// }
//
// bool UBillboardComponent::LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult, float InClosestT)
// {
// 	// TODO: AABB 대신, 더 정확한 검사 도입
// 	if (true)
// 	{
// 		OutHitResult.HitComponent = this;
// 		OutHitResult.WorldHitLocation = GetWorldLocation();
// 		OutHitResult.WorldNormal = (Ray.Origin - GetWorldLocation()).Normalized();
// 		OutHitResult.Distance = (GetWorldLocation() - Ray.Origin).Length();
// 	}
// 	return true;
// }


void UBillboardComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Sprite", EPropertyType::TextureRef, &SpritePath });
}

void UBillboardComponent::PostEditProperty(const char* PropertyName)
{
	if (strcmp(PropertyName, "Sprite") == 0)
	{
		if (SpritePath.empty() || SpritePath == "None")
		{
			SetSprite(nullptr);
		}
		else
		{
			ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
			UTexture2D* Loaded = UTexture2D::LoadFromFile(SpritePath, Device);
			SetSprite(Loaded);
		}
	}
}
