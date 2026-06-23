#pragma once

#include "PrimitiveComponent.h"
#include "Render/Fog/FogRenderTypes.h"

// 일단 과제 명세에 맞게 PrimitiveComponent를 상속합니다.
class UExponentialHeightFogComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(UExponentialHeightFogComponent, UPrimitiveComponent);

	UExponentialHeightFogComponent();
	~UExponentialHeightFogComponent() override = default;

	void CreateRenderState() override;
	void DestroyRenderState() override;
	FPrimitiveSceneProxy* CreateSceneProxy() override { return nullptr; }
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override { return false; }
	bool SupportsOutline() const override { return false; }

	void SetFogDensity(float InValue) { FogDensity = InValue; }
	void SetFogHeightFalloff(float InValue) { FogHeightFalloff = InValue; }
	void SetFogHeight(float InValue) { FogHeight = InValue; }
	void SetFogColor(const FLinearColor& InValue) { FogInscatteringColor = InValue; }
	void SetStartDistance(float InValue) { StartDistance = InValue; }
	void SetFogCutoffDistance(float InValue) { FogCutoffDistance = InValue; }
	void SetFogMaxOpacity(float InValue) { FogMaxOpacity = InValue; }

	float GetFogDensity() const { return FogDensity; }
	float GetFogHeightFalloff() const { return FogHeightFalloff; }
	float GetFogHeight() const { return FogHeight; }
	const FLinearColor& GetFogColor() const { return FogInscatteringColor; }
	float GetStartDistance() const { return StartDistance; }
	float GetFogCutoffDistance() const { return FogCutoffDistance; }
	float GetFogMaxOpacity() const { return FogMaxOpacity; }
	bool IsFogActive() const;
	FFogUniformParameters BuildFogUniformParameters() const;

	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

private:
	void RegisterToScene();
	void UnregisterFromScene();
	void SanitizeProperties();

	float FogDensity = 0.05f;		// 기본 밀도
	float FogHeightFalloff = 0.2f;	// 안개 감쇠 계수(0이면 높이에 따른 감쇠 없음, 1이면 완전 지수 감쇠)
	float FogHeight = 0.0f;			// 안개 기준 높이(이 높이보다 낮은 곳은 더 짙은 안개)
	float StartDistance = 0.0f;		// 안개 시작 거리
	float FogCutoffDistance = 0.0f;	// 이 거리 너머로는 안개 미적용
	float FogMaxOpacity = 1.0f;		// 최대 불투명도

	FLinearColor FogInscatteringColor = FLinearColor(0.447f, 0.537f, 0.647f, 1.0f);
};