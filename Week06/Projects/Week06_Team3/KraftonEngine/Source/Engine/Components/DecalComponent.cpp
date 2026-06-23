#include "Components/DecalComponent.h"

#include <cstring>
#include <algorithm>

#include "Collision/RayUtils.h"
#include "Mesh/ObjManager.h"
#include "Materials/MaterialInterface.h"
#include "Object/ObjectFactory.h"
#include "Resource/ResourceManager.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Render/Proxy/DecalSceneProxy.h"
#include "Render/Proxy/FScene.h"
#include "Serialization/Archive.h"
#include "Texture/Texture2D.h"

IMPLEMENT_CLASS(UDecalComponent, UPrimitiveComponent)

namespace
{
	constexpr const char* DecalTextureMaterialPrefix = "Texture:";

	void EnsureFadeCanTick(UDecalComponent* Component)
	{
		if (!Component)
		{
			return;
		}

		if (AActor* OwnerActor = Component->GetOwner())
		{
			OwnerActor->bNeedsTick = true;
		}

		Component->SetComponentTickEnabled(true);
	}

	bool IsDecalTextureMaterialPath(const FString& Path)
	{
		return Path.rfind(DecalTextureMaterialPrefix, 0) == 0;
	}

	FString GetDecalTextureNameFromMaterialPath(const FString& Path)
	{
		return IsDecalTextureMaterialPath(Path)
			? Path.substr(std::strlen(DecalTextureMaterialPrefix))
			: FString();
	}

	FString MakeDecalTextureMaterialPath(const FName& TextureName)
	{
		return FString(DecalTextureMaterialPrefix) + TextureName.ToString();
	}
}

UDecalComponent::UDecalComponent()
{
	bTickEnable = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
   OriginalAlpha = DecalColor.A;
}

float UDecalComponent::GetFadeStartDelay() const { return FadeStartDelay; }
float UDecalComponent::GetFadeDuration() const { return FadeDuration; }
float UDecalComponent::GetFadeInStartDelay() const { return FadeInStartDelay; }
float UDecalComponent::GetFadeInDuration() const { return FadeInDuration; }

void UDecalComponent::SetFadeOut(float StartDelay, float Duration, bool DestroyOwnerAfterFade)
{
	FadeStartDelay = std::max(0.0f, StartDelay);
	FadeDuration = std::max(0.0f, Duration);
	bDestroyOwnerAfterFade = DestroyOwnerAfterFade;

  bIsFadeInActive = false;
	FadeOutTimeElapsed = 0.0f;
	bIsFadeOutActive = true;
   bPendingFadeOutAfterFadeIn = false;
	OriginalAlpha = DecalColor.A;
   SetVisibility(true);
	EnsureFadeCanTick(this);
}

void UDecalComponent::SetFadeIn(float StartDelay, float Duration)
{
	FadeInStartDelay = std::max(0.0f, StartDelay);
	FadeInDuration = std::max(0.0f, Duration);

   bIsFadeOutActive = false;
	FadeInTimeElapsed = 0.0f;
	bIsFadeInActive = true;

   // 목표 알파(OriginalAlpha)는 기존 값을 유지하고, 시작 시점에는 0(투명)으로 설정
	if (OriginalAlpha <= 0.0f)
	{
		OriginalAlpha = (DecalColor.A > 0.0f) ? DecalColor.A : 1.0f;
	}
	DecalColor.A = 0.0f;
	SetVisibility(true);
	EnsureFadeCanTick(this);

	// 첫 프레임부터 즉시 렌더 스레드에 반영되도록Dirty 마킹
	MarkProxyDirty(EDirtyFlag::Transform);
}

void UDecalComponent::RestartFadePreviewSequence()
{
	FadeStartDelay = std::max(0.0f, FadeStartDelay);
	FadeDuration = std::max(0.0f, FadeDuration);
	FadeInStartDelay = std::max(0.0f, FadeInStartDelay);
	FadeInDuration = std::max(0.0f, FadeInDuration);

	const bool bHasFadeInConfig = (FadeInStartDelay > 0.0f) || (FadeInDuration > 0.0f);
	const bool bHasFadeOutConfig = (FadeStartDelay > 0.0f) || (FadeDuration > 0.0f);

	if (bHasFadeInConfig)
	{
		bPendingFadeOutAfterFadeIn = bHasFadeOutConfig;
		SetFadeIn(FadeInStartDelay, FadeInDuration);
	}
	else if (bHasFadeOutConfig)
	{
		SetFadeOut(FadeStartDelay, FadeDuration, false);
	}
	else
	{
		bIsFadeInActive = false;
		bIsFadeOutActive = false;
		bPendingFadeOutAfterFadeIn = false;
		DecalColor.A = OriginalAlpha;
		SetVisibility(true);
		MarkProxyDirty(EDirtyFlag::Transform);
	}
}

void UDecalComponent::SetDecalColor(const FLinearColor& Color)
{
    DecalColor = Color;
  if (!bIsFadeInActive && !bIsFadeOutActive)
	{
		OriginalAlpha = Color.A;
	}
    MarkProxyDirty(EDirtyFlag::Transform);
}

void UDecalComponent::SetDecalSortOrder(int32 InSortOrder)
{
	DecalSortOrder = InSortOrder;
	MarkProxyDirty(EDirtyFlag::Transform);
}

void UDecalComponent::SetDecalMaterial(UMaterialInterface* NewDecalMaterial)
{
	DecalMaterial = NewDecalMaterial;
	DecalTextureName = FName();
	DecalTexture = nullptr;
	DecalMaterialSlot.Path = (DecalMaterial != nullptr) ? DecalMaterial->GetAssetPathFileName() : "None";
	MarkProxyDirty(EDirtyFlag::Material);
}

UMaterialInterface* UDecalComponent::GetDecalMaterial() const
{
	return DecalMaterial;
}

void UDecalComponent::SetDecalTexture(const FName& TextureName)
{
	DecalMaterial = nullptr;
	DecalTextureName = TextureName;
	DecalTexture = FResourceManager::Get().FindTexture(TextureName);
	DecalMaterialSlot.Path = DecalTexture ? MakeDecalTextureMaterialPath(TextureName) : "None";
	MarkProxyDirty(EDirtyFlag::Material);
}

const FTextureResource* UDecalComponent::GetDecalTexture() const
{
	return DecalTexture;
}

bool UDecalComponent::FitSizeToTextureAspect()
{
	uint32 TextureWidth = 0;
	uint32 TextureHeight = 0;

	if (DecalTexture)
	{
		TextureWidth = DecalTexture->Width;
		TextureHeight = DecalTexture->Height;
	}
	else if (DecalMaterial)
	{
		if (UTexture2D* DiffuseTexture = DecalMaterial->GetDiffuseTexture())
		{
			TextureWidth = DiffuseTexture->GetWidth();
			TextureHeight = DiffuseTexture->GetHeight();
		}
	}

	if (TextureWidth == 0 || TextureHeight == 0)
	{
		return false;
	}

	if (DecalSize.Y <= 0.0f)
	{
		DecalSize.Y = 1.0f;
	}

	const FVector WorldScale = GetWorldScale();
	const float SafeWorldScaleY = (WorldScale.Y > 0.001f) ? WorldScale.Y : 1.0f;
	const float SafeWorldScaleZ = (WorldScale.Z > 0.001f) ? WorldScale.Z : 1.0f;
	const float TextureAspectYZ = static_cast<float>(TextureHeight) / static_cast<float>(TextureWidth);
	DecalSize.Z = (DecalSize.Y * SafeWorldScaleY * TextureAspectYZ) / SafeWorldScaleZ;
	MarkProxyDirty(EDirtyFlag::Transform);
	MarkWorldBoundsDirty();
	return true;
}

void UDecalComponent::SetDecalSize(const FVector& InSize)
{
	DecalSize = InSize;
	MarkProxyDirty(EDirtyFlag::Transform);
	MarkWorldBoundsDirty();
}

FMatrix UDecalComponent::GetTransformIncludingDecalSize() const
{
    return FMatrix::MakeScaleMatrix(DecalSize) * GetWorldMatrix();
}

void UDecalComponent::UpdateWorldAABB() const
{
	// 1. DecalSize가 반영된 로컬 Extent(반지름)를 계산합니다.
	// 렌더링에 쓰이는 큐브 메쉬가 크기 1(-0.5 ~ 0.5)이므로 0.5를 곱해줍니다.
	FVector LExt = DecalSize * 0.5f;

	// 2. 부모(UPrimitiveComponent)의 투영 공식을 그대로 사용하여 OBB의 꼭짓점들을 AABB로 변환합니다.
	FMatrix worldMatrix = GetWorldMatrix();

	float NewEx = std::abs(worldMatrix.M[0][0]) * LExt.X + std::abs(worldMatrix.M[1][0]) * LExt.Y + std::abs(worldMatrix.M[2][0]) * LExt.Z;
	float NewEy = std::abs(worldMatrix.M[0][1]) * LExt.X + std::abs(worldMatrix.M[1][1]) * LExt.Y + std::abs(worldMatrix.M[2][1]) * LExt.Z;
	float NewEz = std::abs(worldMatrix.M[0][2]) * LExt.X + std::abs(worldMatrix.M[1][2]) * LExt.Y + std::abs(worldMatrix.M[2][2]) * LExt.Z;

	// 3. 엔진 AABB 변수에 갱신
	FVector WorldCenter = GetWorldLocation();
	WorldAABBMinLocation = WorldCenter - FVector(NewEx, NewEy, NewEz);
	WorldAABBMaxLocation = WorldCenter + FVector(NewEx, NewEy, NewEz);

	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
}

bool UDecalComponent::LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult)
{
	const FMeshData* Data = GetMeshData();
	if (!Data || Data->Indices.empty())
	{
		return false;
	}

	const FMatrix DecalWorldMatrix = GetTransformIncludingDecalSize();
	const FMatrix DecalWorldInverseMatrix = DecalWorldMatrix.GetInverse();
	const bool bHit = FRayUtils::RaycastTriangles(
		Ray,
		DecalWorldMatrix,
		DecalWorldInverseMatrix,
		&Data->Vertices[0].Position,
		sizeof(FVertex),
		Data->Indices,
		OutHitResult);

	if (bHit)
	{
		OutHitResult.HitComponent = this;
	}
	return bHit;
}

FMeshBuffer* UDecalComponent::GetMeshBuffer() const
{
    return &FMeshBufferManager::Get().GetMeshBuffer(EMeshShape::Cube);
}

const FMeshData* UDecalComponent::GetMeshData() const
{
    return &FMeshBufferManager::Get().GetMeshData(EMeshShape::Cube);
}

FPrimitiveSceneProxy* UDecalComponent::CreateSceneProxy()
{
    return new FDecalSceneProxy(this);
}

void UDecalComponent::CreateRenderState()
{
	if (SceneProxy)
	{
		return;
	}

	UPrimitiveComponent::CreateRenderState();
	if (!SceneProxy)
	{
		return;
	}

	FScene* Scene = nullptr;
	if (Owner && Owner->GetWorld())
	{
		Scene = &Owner->GetWorld()->GetScene();
	}

	if (!Scene)
	{
		return;
	}

	ArrowOuterProxy = new FDecalArrowSceneProxy(this, false);
	Scene->RegisterProxy(ArrowOuterProxy);

	ArrowInnerProxy = new FDecalArrowSceneProxy(this, true);
	Scene->RegisterProxy(ArrowInnerProxy);
}

void UDecalComponent::DestroyRenderState()
{
	if (Owner && Owner->GetWorld())
	{
		FScene& Scene = Owner->GetWorld()->GetScene();
		if (ArrowInnerProxy)
		{
			Scene.RemovePrimitive(ArrowInnerProxy);
			ArrowInnerProxy = nullptr;
		}
		if (ArrowOuterProxy)
		{
			Scene.RemovePrimitive(ArrowOuterProxy);
			ArrowOuterProxy = nullptr;
		}
	}

	UPrimitiveComponent::DestroyRenderState();
}

void UDecalComponent::Serialize(FArchive& Ar)
{
    UPrimitiveComponent::Serialize(Ar);

    Ar << FadeStartDelay;
    Ar << FadeDuration;
    Ar << FadeInDuration;
    Ar << FadeInStartDelay;
    Ar << bDestroyOwnerAfterFade;
    Ar << DecalSize;
    Ar << DecalColor;
    Ar << DecalMaterialSlot.Path;
    Ar << DecalMaterialSlot.bUVScroll;
    Ar << DecalSortOrder;

	if (Ar.IsLoading())
	{
		OriginalAlpha = DecalColor.A;
	}
}

void UDecalComponent::PostDuplicate()
{
    UPrimitiveComponent::PostDuplicate();
	OriginalAlpha = DecalColor.A;

	if (IsDecalTextureMaterialPath(DecalMaterialSlot.Path))
	{
		SetDecalTexture(FName(GetDecalTextureNameFromMaterialPath(DecalMaterialSlot.Path)));
	}
	else if (!DecalMaterialSlot.Path.empty() && DecalMaterialSlot.Path != "None")
	{
		DecalMaterial = FObjManager::GetOrLoadMaterial(DecalMaterialSlot.Path);
		DecalTextureName = FName();
		DecalTexture = nullptr;
	}
	else
	{
		DecalMaterial = nullptr;
		DecalTextureName = FName();
		DecalTexture = nullptr;
	}

    MarkRenderStateDirty();
}

void UDecalComponent::BeginPlay()
{
	UPrimitiveComponent::BeginPlay();

	const bool bHasFadeInConfig = (FadeInStartDelay > 0.0f) || (FadeInDuration > 0.0f);
	const bool bHasFadeOutConfig = (FadeStartDelay > 0.0f) || (FadeDuration > 0.0f);

	if (bHasFadeInConfig && bHasFadeOutConfig)
	{
		RestartFadePreviewSequence();
	}
	else if (bHasFadeInConfig)
	{
		SetFadeIn(FadeInStartDelay, FadeInDuration);
	}
	else if (bHasFadeOutConfig)
	{
		SetFadeOut(FadeStartDelay, FadeDuration, false);
	}
}

void UDecalComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Decal Material", EPropertyType::MaterialSlot, &DecalMaterialSlot });
	OutProps.push_back({ "Fade Start Delay", EPropertyType::Float, &FadeStartDelay });
	OutProps.push_back({ "Fade Duration", EPropertyType::Float, &FadeDuration });
	OutProps.push_back({ "Fade In Duration", EPropertyType::Float, &FadeInDuration });
	OutProps.push_back({ "Fade In Start Delay", EPropertyType::Float, &FadeInStartDelay });
    OutProps.push_back({ "Destroy Owner After Fade", EPropertyType::Bool, &bDestroyOwnerAfterFade });
    OutProps.push_back({ "Decal Size", EPropertyType::Vec3, &DecalSize });
    OutProps.push_back({ "Decal Color", EPropertyType::Vec4, &DecalColor });
    OutProps.push_back({ "Decal Sort Order", EPropertyType::Int, &DecalSortOrder });
    
}

void UDecalComponent::PostEditProperty(const char* PropertyName)
{
    UPrimitiveComponent::PostEditProperty(PropertyName);

    if (std::strcmp(PropertyName, "Decal Size") == 0)
    {
        MarkProxyDirty(EDirtyFlag::Transform);
		MarkWorldBoundsDirty();
    }
    else if (std::strcmp(PropertyName, "Decal Color") == 0)
    {
        SetDecalColor(DecalColor);
    }
    else if (std::strcmp(PropertyName, "Decal Sort Order") == 0)
    {
        SetDecalSortOrder(DecalSortOrder);
    }
    else if (std::strcmp(PropertyName, "Decal Material") == 0 || std::strcmp(PropertyName, "Element 0") == 0)
    {
		if (DecalMaterialSlot.Path.empty() || DecalMaterialSlot.Path == "None")
		{
			SetDecalMaterial(nullptr);
		}
		else if (IsDecalTextureMaterialPath(DecalMaterialSlot.Path))
		{
			SetDecalTexture(FName(GetDecalTextureNameFromMaterialPath(DecalMaterialSlot.Path)));
		}
		else
		{
			SetDecalMaterial(FObjManager::GetOrLoadMaterial(DecalMaterialSlot.Path));
		}
    }
	else if (std::strcmp(PropertyName, "Fade Start Delay") == 0
		|| std::strcmp(PropertyName, "Fade Duration") == 0
		|| std::strcmp(PropertyName, "Fade In Duration") == 0
		|| std::strcmp(PropertyName, "Fade In Start Delay") == 0)
	{
       if (Owner && Owner->HasActorBegunPlay())
		{
			RestartFadePreviewSequence();
		}
		else
		{
			FadeStartDelay = std::max(0.0f, FadeStartDelay);
			FadeDuration = std::max(0.0f, FadeDuration);
			FadeInStartDelay = std::max(0.0f, FadeInStartDelay);
			FadeInDuration = std::max(0.0f, FadeInDuration);

			bIsFadeInActive = false;
			bIsFadeOutActive = false;
			bPendingFadeOutAfterFadeIn = false;
			if (OriginalAlpha > 0.0f)
			{
				DecalColor.A = OriginalAlpha;
			}
			MarkProxyDirty(EDirtyFlag::Transform);
		}
	}

}

void UDecalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	bool bColorChanged = false;

	// 1. 페이드 인 (서서히 나타남)
	if (bIsFadeInActive)
	{
		FadeInTimeElapsed += DeltaTime;

		// 시작 지연 시간이 지났을 때부터 알파값 증가
		if (FadeInTimeElapsed >= FadeInStartDelay)
		{
			if (FadeInDuration > 0.0f)
			{
				float Progress = (FadeInTimeElapsed - FadeInStartDelay) / FadeInDuration;
				DecalColor.A = OriginalAlpha * std::clamp(Progress, 0.0f, 1.0f);
			}
			else
			{
				DecalColor.A = OriginalAlpha;
			}

			bColorChanged = true;

			// 완료 체크
			if (FadeInTimeElapsed >= FadeInStartDelay + FadeInDuration)
			{
				bIsFadeInActive = false;
               if (bPendingFadeOutAfterFadeIn && ((FadeStartDelay > 0.0f) || (FadeDuration > 0.0f)))
				{
					SetFadeOut(FadeStartDelay, FadeDuration, false);
				}
				else
				{
					bPendingFadeOutAfterFadeIn = false;
				}
			}
		}
	}

	// 2. 페이드 아웃 (서서히 사라짐)
	if (bIsFadeOutActive)
	{
		FadeOutTimeElapsed += DeltaTime;

		// 시작 지연 시간이 지났을 때부터 알파값 감소
		if (FadeOutTimeElapsed >= FadeStartDelay)
		{
			if (FadeDuration > 0.0f)
			{
				float Progress = (FadeOutTimeElapsed - FadeStartDelay) / FadeDuration;
				DecalColor.A = OriginalAlpha * (1.0f - std::clamp(Progress, 0.0f, 1.0f));
			}
			else
			{
				DecalColor.A = 0.0f;
			}

			bColorChanged = true;

			// 완료 체크
			if (FadeOutTimeElapsed >= FadeStartDelay + FadeDuration)
			{
				bIsFadeOutActive = false;

				// 액터를 삭제하지 않는 대신 가시성과 틱을 꺼서 렌더링/연산 비용 제거
				SetVisibility(false);
				SetComponentTickEnabled(false);
			}
		}
	}

	if (bColorChanged)
	{
		MarkProxyDirty(EDirtyFlag::Transform);
	}

}
