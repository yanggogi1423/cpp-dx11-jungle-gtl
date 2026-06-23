#pragma once
#pragma once

#include "Components/PrimitiveComponent.h"
#include "Core/EngineTypes.h"
#include "Core/PropertyTypes.h"
#include "Core/ResourceTypes.h"
#include "Render/Resource/MeshBufferManager.h"

class UMaterialInterface;
class FPrimitiveSceneProxy;

class UDecalComponent : public UPrimitiveComponent
{
public:
 DECLARE_CLASS(UDecalComponent, UPrimitiveComponent)

	UDecalComponent();
	~UDecalComponent() override = default;

	// 페이드 아웃 관련 데이터
	float FadeStartDelay = 0.0f;
	float FadeDuration = 0.0f;
	float FadeInDuration = 0.0f;
	float FadeInStartDelay = 0.0f;
	bool bDestroyOwnerAfterFade = true;

	float GetFadeStartDelay() const;
	float GetFadeDuration() const;
	float GetFadeInStartDelay() const;
	float GetFadeInDuration() const;

	FVector DecalSize = FVector(1.0f, 1.0f, 1.0f);
	FLinearColor DecalColor = FLinearColor::White();
	int32 DecalSortOrder = 0;
	
	void SetFadeOut(float StartDelay, float Duration, bool DestroyOwnerAfterFade = true);
	void SetFadeIn(float StartDelay, float Duration);
	void SetDecalColor(const FLinearColor& Color);
	void SetDecalSortOrder(int32 InSortOrder);
	int32 GetDecalSortOrder() const { return DecalSortOrder; }
	void SetDecalMaterial(UMaterialInterface* NewDecalMaterial);
	UMaterialInterface* GetDecalMaterial() const;
	void SetDecalTexture(const FName& TextureName);
	const FTextureResource* GetDecalTexture() const;
	bool FitSizeToTextureAspect();

	void SetDecalSize(const FVector& InSize);
	FMatrix GetTransformIncludingDecalSize() const;

	void UpdateWorldAABB() const override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;
	bool SupportsPicking() const override { return false; }
	FMeshBuffer* GetMeshBuffer() const override;
	const FMeshData* GetMeshData() const override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;
	void CreateRenderState() override;
	void DestroyRenderState() override;

	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;
 void BeginPlay() override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

protected:
	UMaterialInterface* DecalMaterial = nullptr;
	FMaterialSlot DecalMaterialSlot;
	FName DecalTextureName;
	FTextureResource* DecalTexture = nullptr;
	FPrimitiveSceneProxy* ArrowOuterProxy = nullptr;
	FPrimitiveSceneProxy* ArrowInnerProxy = nullptr;

private:
	float FadeOutTimeElapsed = 0.0f;
	float FadeInTimeElapsed = 0.0f;
	bool bIsFadeOutActive = false;
	bool bIsFadeInActive = false;
 bool bPendingFadeOutAfterFadeIn = false;
	float OriginalAlpha = 1.0f;

	void RestartFadePreviewSequence();
};
