#pragma once

#include "Components/PrimitiveComponent.h"
#include "Core/EngineTypes.h"
#include "Core/ProjectionDecalTypes.h"
#include "Core/PropertyTypes.h"
#include "Core/ResourceTypes.h"

class UMaterialInterface;
class FPrimitiveSceneProxy;
class FArchive;

class UProjectionDecalComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(UProjectionDecalComponent, UPrimitiveComponent)

	UProjectionDecalComponent();
	~UProjectionDecalComponent() override = default;

	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;
	void BeginPlay() override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	void OnTransformDirty() override;
	void CreateRenderState() override;
	void DestroyRenderState() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	void SetProjectionDecalSize(const FVector& InSize);
	const FVector& GetProjectionDecalSize() const { return ProjectionDecalSize; }

	void SetProjectionDecalMaterial(UMaterialInterface* InMaterial);
	UMaterialInterface* GetProjectionDecalMaterial() const { return ProjectionDecalMaterial; }
	void SetProjectionDecalTexture(const FName& TextureName);
	const FTextureResource* GetProjectionDecalTexture() const { return ProjectionDecalTexture; }
	bool IsProjectionDecalUVScrollEnabled() const { return ProjectionDecalMaterialSlot.bUVScroll != 0; }
	void SetProjectionDecalColor(const FLinearColor& Color);
	const FLinearColor& GetProjectionDecalColor() const { return ProjectionDecalColor; }
	bool FitSizeToTextureAspect();
	void SetFadeOut(float StartDelay, float Duration, bool DestroyOwnerAfterFade = true);
	void SetFadeIn(float StartDelay, float Duration);

	void SetSortOrder(int32 InSortOrder);
	int32 GetSortOrder() const { return SortOrder; }

	void SetReceivesDecalOnly(bool bEnable) { bReceivesDecalOnly = bEnable; MarkProjectionDecalDirty(); }
	bool IsReceivesDecalOnlyEnabled() const { return bReceivesDecalOnly; }
	void SetExcludeSameOwner(bool bEnable) { bExcludeSameOwner = bEnable; MarkProjectionDecalDirty(); }
	bool IsExcludeSameOwnerEnabled() const { return bExcludeSameOwner; }
	void SetLooseTriangleAccept(bool bEnable) { bLooseTriangleAccept = bEnable; MarkProjectionDecalDirty(); }
	bool IsLooseTriangleAcceptEnabled() const { return bLooseTriangleAccept; }

	void MarkProjectionDecalDirty();
	void EnsureProjectionDecalMeshBuilt();
	const FProjectionDecalRenderableMesh& GetRenderableMesh() const { return RenderableMesh; }

	FMatrix GetProjectionDecalLocalToWorldMatrix() const;
	FMatrix GetWorldToProjectionDecalMatrix() const;
	void GetProjectionDecalBoxCorners(FVector(&OutCorners)[8]) const;
	FBoundingBox GetProjectionDecalWorldAABB() const;

	void UpdateWorldAABB() const override;
	bool SupportsOutline() const override { return false; }
	bool SupportsPicking() const override { return false; }

	FPrimitiveSceneProxy* CreateSceneProxy() override;

private:
	void BuildProjectionDecalMesh();
	void ReloadMaterialFromPath();
	void RestartFadePreviewSequence();

private:
	FVector ProjectionDecalSize = FVector(1.0f, 1.0f, 1.0f);
	FLinearColor ProjectionDecalColor = FLinearColor::White();
	float FadeStartDelay = 0.0f;
	float FadeDuration = 0.0f;
	float FadeInDuration = 0.0f;
	float FadeInStartDelay = 0.0f;
	bool bDestroyOwnerAfterFade = true;
	UMaterialInterface* ProjectionDecalMaterial = nullptr;
	FName ProjectionDecalTextureName;
	FTextureResource* ProjectionDecalTexture = nullptr;
	FMaterialSlot ProjectionDecalMaterialSlot;
	FString ProjectionDecalMaterialPath = "None";
	FProjectionDecalRenderableMesh RenderableMesh;
	FPrimitiveSceneProxy* ArrowOuterProxy = nullptr;
	FPrimitiveSceneProxy* ArrowInnerProxy = nullptr;
	int32 SortOrder = 0;
	bool bProjectionDecalDirty = true;
	bool bReceivesDecalOnly = true;
	bool bExcludeSameOwner = false;
	bool bLooseTriangleAccept = true;
	float FadeOutTimeElapsed = 0.0f;
	float FadeInTimeElapsed = 0.0f;
	bool bIsFadeOutActive = false;
	bool bIsFadeInActive = false;
	bool bPendingFadeOutAfterFadeIn = false;
	float OriginalAlpha = 1.0f;
};

