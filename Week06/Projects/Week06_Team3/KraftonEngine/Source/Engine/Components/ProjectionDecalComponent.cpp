#include "Components/ProjectionDecalComponent.h"

#include "Materials/MaterialInterface.h"
#include "Mesh/ProjectionDecalMeshBuilder.h"
#include "Mesh/ObjManager.h"
#include "Object/ObjectFactory.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Render/Proxy/FScene.h"
#include "Render/Proxy/ProjectionDecalSceneProxy.h"
#include "Resource/ResourceManager.h"
#include "Serialization/Archive.h"
#include "Texture/Texture2D.h"

#include <algorithm>
#include <cstring>

IMPLEMENT_CLASS(UProjectionDecalComponent, UPrimitiveComponent)

namespace
{
	constexpr const char* DecalTextureMaterialPrefix = "Texture:";

	FVector SanitizeSize(const FVector& InSize)
	{
		return FVector(
			std::max(std::abs(InSize.X), 0.001f),
			std::max(std::abs(InSize.Y), 0.001f),
			std::max(std::abs(InSize.Z), 0.001f));
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

	void EnsureFadeCanTick(UProjectionDecalComponent* Component)
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
}

UProjectionDecalComponent::UProjectionDecalComponent()
{
	bTickEnable = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	OriginalAlpha = ProjectionDecalColor.A;
}

void UProjectionDecalComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	if (Ar.IsSaving())
	{
		if (ProjectionDecalTextureName.IsValid())
		{
			ProjectionDecalMaterialPath = MakeDecalTextureMaterialPath(ProjectionDecalTextureName);
		}
		else
		{
			ProjectionDecalMaterialPath = ProjectionDecalMaterial ? ProjectionDecalMaterial->GetAssetPathFileName() : "None";
		}
		ProjectionDecalMaterialSlot.Path = ProjectionDecalMaterialPath;
	}

	Ar << ProjectionDecalSize;
	Ar << ProjectionDecalColor;
	Ar << FadeStartDelay;
	Ar << FadeDuration;
	Ar << FadeInDuration;
	Ar << FadeInStartDelay;
	Ar << bDestroyOwnerAfterFade;
	Ar << ProjectionDecalMaterialPath;
	Ar << ProjectionDecalMaterialSlot.bUVScroll;
	Ar << SortOrder;
	Ar << bReceivesDecalOnly;
	Ar << bExcludeSameOwner;
	Ar << bLooseTriangleAccept;

	if (Ar.IsLoading())
	{
		ProjectionDecalSize = SanitizeSize(ProjectionDecalSize);
		OriginalAlpha = ProjectionDecalColor.A;
		ProjectionDecalMaterialSlot.Path = ProjectionDecalMaterialPath;
		ReloadMaterialFromPath();
		MarkProjectionDecalDirty();
		MarkWorldBoundsDirty();
	}
}

void UProjectionDecalComponent::PostDuplicate()
{
	UPrimitiveComponent::PostDuplicate();
	OriginalAlpha = ProjectionDecalColor.A;
	ReloadMaterialFromPath();
	MarkProjectionDecalDirty();
	MarkWorldBoundsDirty();
}

void UProjectionDecalComponent::BeginPlay()
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

void UProjectionDecalComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Projection Decal Size", EPropertyType::Vec3, &ProjectionDecalSize });
	OutProps.push_back({ "Projection Decal Material", EPropertyType::MaterialSlot, &ProjectionDecalMaterialSlot });
	OutProps.push_back({ "Fade Start Delay", EPropertyType::Float, &FadeStartDelay });
	OutProps.push_back({ "Fade Duration", EPropertyType::Float, &FadeDuration });
	OutProps.push_back({ "Fade In Duration", EPropertyType::Float, &FadeInDuration });
	OutProps.push_back({ "Fade In Start Delay", EPropertyType::Float, &FadeInStartDelay });
	OutProps.push_back({ "Destroy Owner After Fade", EPropertyType::Bool, &bDestroyOwnerAfterFade });
	OutProps.push_back({ "Projection Decal Color", EPropertyType::Vec4, &ProjectionDecalColor });
	OutProps.push_back({ "Sort Order", EPropertyType::Int, &SortOrder });
	OutProps.push_back({ "Receives Decal Only", EPropertyType::Bool, &bReceivesDecalOnly });
	OutProps.push_back({ "Exclude Same Owner", EPropertyType::Bool, &bExcludeSameOwner });
	OutProps.push_back({ "Loose Triangle Accept", EPropertyType::Bool, &bLooseTriangleAccept });
}

void UProjectionDecalComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "Projection Decal Size") == 0)
	{
		SetProjectionDecalSize(ProjectionDecalSize);
	}
	else if (std::strcmp(PropertyName, "Projection Decal Material") == 0 || std::strcmp(PropertyName, "Element 0") == 0)
	{
		ProjectionDecalMaterialPath = ProjectionDecalMaterialSlot.Path;
		ReloadMaterialFromPath();
		MarkProjectionDecalDirty();
		MarkProxyDirty(EDirtyFlag::Material);
	}
	else if (std::strcmp(PropertyName, "Projection Decal Color") == 0)
	{
		SetProjectionDecalColor(ProjectionDecalColor);
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
				ProjectionDecalColor.A = OriginalAlpha;
			}
			MarkProxyDirty(EDirtyFlag::Transform);
		}
	}
	else if (std::strcmp(PropertyName, "Sort Order") == 0)
	{
		SetSortOrder(SortOrder);
	}
	else if (std::strcmp(PropertyName, "Receives Decal Only") == 0
		|| std::strcmp(PropertyName, "Exclude Same Owner") == 0
		|| std::strcmp(PropertyName, "Loose Triangle Accept") == 0)
	{
		MarkProjectionDecalDirty();
	}
}

void UProjectionDecalComponent::OnTransformDirty()
{
	UPrimitiveComponent::OnTransformDirty();
	MarkProjectionDecalDirty();
}

void UProjectionDecalComponent::SetProjectionDecalSize(const FVector& InSize)
{
	ProjectionDecalSize = SanitizeSize(InSize);
	MarkWorldBoundsDirty();
	MarkProjectionDecalDirty();
}

void UProjectionDecalComponent::SetProjectionDecalMaterial(UMaterialInterface* InMaterial)
{
	ProjectionDecalMaterial = InMaterial;
	ProjectionDecalTextureName = FName();
	ProjectionDecalTexture = nullptr;
	ProjectionDecalMaterialPath = ProjectionDecalMaterial ? ProjectionDecalMaterial->GetAssetPathFileName() : "None";
	ProjectionDecalMaterialSlot.Path = ProjectionDecalMaterialPath;
	MarkProjectionDecalDirty();
	MarkProxyDirty(EDirtyFlag::Material);
}

void UProjectionDecalComponent::SetProjectionDecalTexture(const FName& TextureName)
{
	ProjectionDecalMaterial = nullptr;
	ProjectionDecalTextureName = TextureName;
	ProjectionDecalTexture = FResourceManager::Get().FindTexture(TextureName);
	ProjectionDecalMaterialPath = ProjectionDecalTexture ? MakeDecalTextureMaterialPath(TextureName) : "None";
	ProjectionDecalMaterialSlot.Path = ProjectionDecalMaterialPath;
	MarkProjectionDecalDirty();
	MarkProxyDirty(EDirtyFlag::Material);
}

void UProjectionDecalComponent::SetProjectionDecalColor(const FLinearColor& Color)
{
	ProjectionDecalColor = Color;
	if (!bIsFadeInActive && !bIsFadeOutActive)
	{
		OriginalAlpha = Color.A;
	}
	MarkProxyDirty(EDirtyFlag::Transform);
}

void UProjectionDecalComponent::SetFadeOut(float StartDelay, float Duration, bool DestroyOwnerAfterFade)
{
	FadeStartDelay = std::max(0.0f, StartDelay);
	FadeDuration = std::max(0.0f, Duration);
	bDestroyOwnerAfterFade = DestroyOwnerAfterFade;

	bIsFadeInActive = false;
	FadeOutTimeElapsed = 0.0f;
	bIsFadeOutActive = true;
	bPendingFadeOutAfterFadeIn = false;
	OriginalAlpha = ProjectionDecalColor.A;
	SetVisibility(true);
	EnsureFadeCanTick(this);
}

void UProjectionDecalComponent::SetFadeIn(float StartDelay, float Duration)
{
	FadeInStartDelay = std::max(0.0f, StartDelay);
	FadeInDuration = std::max(0.0f, Duration);

	bIsFadeOutActive = false;
	FadeInTimeElapsed = 0.0f;
	bIsFadeInActive = true;

	if (OriginalAlpha <= 0.0f)
	{
		OriginalAlpha = (ProjectionDecalColor.A > 0.0f) ? ProjectionDecalColor.A : 1.0f;
	}
	ProjectionDecalColor.A = 0.0f;
	SetVisibility(true);
	EnsureFadeCanTick(this);
	MarkProxyDirty(EDirtyFlag::Transform);
}

void UProjectionDecalComponent::RestartFadePreviewSequence()
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
		ProjectionDecalColor.A = OriginalAlpha;
		SetVisibility(true);
		MarkProxyDirty(EDirtyFlag::Transform);
	}
}

bool UProjectionDecalComponent::FitSizeToTextureAspect()
{
	uint32 TextureWidth = 0;
	uint32 TextureHeight = 0;

	if (ProjectionDecalTexture)
	{
		TextureWidth = ProjectionDecalTexture->Width;
		TextureHeight = ProjectionDecalTexture->Height;
	}
	else if (ProjectionDecalMaterial)
	{
		if (UTexture2D* DiffuseTexture = ProjectionDecalMaterial->GetDiffuseTexture())
		{
			TextureWidth = DiffuseTexture->GetWidth();
			TextureHeight = DiffuseTexture->GetHeight();
		}
	}

	if (TextureWidth == 0 || TextureHeight == 0)
	{
		return false;
	}

	if (ProjectionDecalSize.Y <= 0.0f)
	{
		ProjectionDecalSize.Y = 1.0f;
	}

	const FVector WorldScale = GetWorldScale();
	const float SafeWorldScaleY = (WorldScale.Y > 0.001f) ? WorldScale.Y : 1.0f;
	const float SafeWorldScaleZ = (WorldScale.Z > 0.001f) ? WorldScale.Z : 1.0f;
	const float TextureAspectYZ = static_cast<float>(TextureHeight) / static_cast<float>(TextureWidth);
	ProjectionDecalSize.Z = (ProjectionDecalSize.Y * SafeWorldScaleY * TextureAspectYZ) / SafeWorldScaleZ;
	MarkWorldBoundsDirty();
	MarkProjectionDecalDirty();
	return true;
}

void UProjectionDecalComponent::SetSortOrder(int32 InSortOrder)
{
	SortOrder = InSortOrder;
	MarkProxyDirty(EDirtyFlag::Material);
}

void UProjectionDecalComponent::MarkProjectionDecalDirty()
{
	bProjectionDecalDirty = true;
	MarkProxyDirty(EDirtyFlag::Mesh);
}

void UProjectionDecalComponent::EnsureProjectionDecalMeshBuilt()
{
	if (!bProjectionDecalDirty)
	{
		return;
	}

	BuildProjectionDecalMesh();
	bProjectionDecalDirty = false;
}

FMatrix UProjectionDecalComponent::GetProjectionDecalLocalToWorldMatrix() const
{
	return FMatrix::MakeScaleMatrix(ProjectionDecalSize) * GetWorldMatrix();
}

FMatrix UProjectionDecalComponent::GetWorldToProjectionDecalMatrix() const
{
	return GetProjectionDecalLocalToWorldMatrix().GetInverse();
}

void UProjectionDecalComponent::GetProjectionDecalBoxCorners(FVector(&OutCorners)[8]) const
{
	static const FVector UnitCorners[8] =
	{
		FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, -0.5f, -0.5f),
		FVector(0.5f, 0.5f, -0.5f), FVector(-0.5f, 0.5f, -0.5f),
		FVector(-0.5f, -0.5f, 0.5f), FVector(0.5f, -0.5f, 0.5f),
		FVector(0.5f, 0.5f, 0.5f), FVector(-0.5f, 0.5f, 0.5f)
	};

	const FMatrix LocalToWorld = GetProjectionDecalLocalToWorldMatrix();
	for (int32 i = 0; i < 8; ++i)
	{
		OutCorners[i] = LocalToWorld.TransformPositionWithW(UnitCorners[i]);
	}
}

FBoundingBox UProjectionDecalComponent::GetProjectionDecalWorldAABB() const
{
	FVector Corners[8];
	GetProjectionDecalBoxCorners(Corners);

	FBoundingBox Bounds;
	for (const FVector& Corner : Corners)
	{
		Bounds.Expand(Corner);
	}
	return Bounds;
}

void UProjectionDecalComponent::UpdateWorldAABB() const
{
	const FBoundingBox Bounds = GetProjectionDecalWorldAABB();
	WorldAABBMinLocation = Bounds.Min;
	WorldAABBMaxLocation = Bounds.Max;
	bWorldAABBDirty = false;
	bHasValidWorldAABB = Bounds.IsValid();
}

FPrimitiveSceneProxy* UProjectionDecalComponent::CreateSceneProxy()
{
	EnsureProjectionDecalMeshBuilt();
	return new FProjectionDecalSceneProxy(this);
}

void UProjectionDecalComponent::CreateRenderState()
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

	ArrowOuterProxy = new FProjectionDecalArrowSceneProxy(this, false);
	Scene->RegisterProxy(ArrowOuterProxy);

	ArrowInnerProxy = new FProjectionDecalArrowSceneProxy(this, true);
	Scene->RegisterProxy(ArrowInnerProxy);
}

void UProjectionDecalComponent::DestroyRenderState()
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

void UProjectionDecalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	bool bColorChanged = false;

	if (bIsFadeInActive)
	{
		FadeInTimeElapsed += DeltaTime;

		if (FadeInTimeElapsed >= FadeInStartDelay)
		{
			if (FadeInDuration > 0.0f)
			{
				const float Progress = (FadeInTimeElapsed - FadeInStartDelay) / FadeInDuration;
				ProjectionDecalColor.A = OriginalAlpha * std::clamp(Progress, 0.0f, 1.0f);
			}
			else
			{
				ProjectionDecalColor.A = OriginalAlpha;
			}

			bColorChanged = true;

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

	if (bIsFadeOutActive)
	{
		FadeOutTimeElapsed += DeltaTime;

		if (FadeOutTimeElapsed >= FadeStartDelay)
		{
			if (FadeDuration > 0.0f)
			{
				const float Progress = (FadeOutTimeElapsed - FadeStartDelay) / FadeDuration;
				ProjectionDecalColor.A = OriginalAlpha * (1.0f - std::clamp(Progress, 0.0f, 1.0f));
			}
			else
			{
				ProjectionDecalColor.A = 0.0f;
			}

			bColorChanged = true;

			if (FadeOutTimeElapsed >= FadeStartDelay + FadeDuration)
			{
				bIsFadeOutActive = false;
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

void UProjectionDecalComponent::BuildProjectionDecalMesh()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		RenderableMesh.Clear();
		return;
	}

	FProjectionDecalMeshBuilder::BuildRenderableMesh(*this, *World, RenderableMesh);
}

void UProjectionDecalComponent::ReloadMaterialFromPath()
{
	if (ProjectionDecalMaterialPath.empty() || ProjectionDecalMaterialPath == "None")
	{
		ProjectionDecalMaterial = nullptr;
		ProjectionDecalTextureName = FName();
		ProjectionDecalTexture = nullptr;
		return;
	}

	if (IsDecalTextureMaterialPath(ProjectionDecalMaterialPath))
	{
		ProjectionDecalMaterial = nullptr;
		ProjectionDecalTextureName = FName(GetDecalTextureNameFromMaterialPath(ProjectionDecalMaterialPath));
		ProjectionDecalTexture = FResourceManager::Get().FindTexture(ProjectionDecalTextureName);
		return;
	}

	ProjectionDecalMaterial = FObjManager::GetOrLoadMaterial(ProjectionDecalMaterialPath);
	ProjectionDecalTextureName = FName();
	ProjectionDecalTexture = nullptr;
}


