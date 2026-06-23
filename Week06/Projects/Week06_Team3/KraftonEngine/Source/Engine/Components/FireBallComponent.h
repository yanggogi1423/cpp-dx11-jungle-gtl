#pragma once

#include "Components/PrimitiveComponent.h"
#include "Core/EngineTypes.h"
#include "Components/SceneEffectSource.h"

// 복잡한 lighting 계산 대신, 월드 내 특정 위치 주변에 색을 더하는 간단한 반경 기반 효과용 컴포넌트.
// FSceneEffectConstants에 데이터를 채워 FScene에 전달하는 형태로 구현되어 있습니다.
// 특이사항은 렌더링을 FScene에서 전담하므로 프록시를 생성하지 않습니다.
class FPrimitiveSceneProxy;

class UFireBallComponent : public UPrimitiveComponent, public ISceneEffectSource
{
public:
	DECLARE_CLASS(UFireBallComponent, UPrimitiveComponent)

	UFireBallComponent();
	~UFireBallComponent() override = default;

	// FireBallComponent는 프리미티브 프록시 대신 FScene 효과 소스로만 동작하므로, 프록시 생성 없이 씬에 등록합니다.
	void CreateRenderState() override;
	// 반대로 FScene에서 효과 소스 등록을 해제한 뒤, 부모의 정리 로직을 이어서 호출합니다.
	void DestroyRenderState() override;

	FPrimitiveSceneProxy* CreateSceneProxy() override { return nullptr; };

	// 월드 위치를 중심으로 Radius만큼 확장된 bounds를 계산하는데, 현재 실행하면 bounding box 뷰에서 변경이 적용되지 않습니다.
	void UpdateWorldAABB() const override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override{ return false;}

	// UPrimitiveComponent는 외곽선 렌더링 지원 여부를 질의하므로 오버라이드합니다.
	// 현재는 프록시가 없고 외곽선 대상도 아니므로 사용하지 않으며 false를 반환합니다.
	bool SupportsOutline() const override { return false; }

	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

	// 에디터에서 값이 바뀐 직후 보정.
	void PostEditProperty(const char* PropertyName) override;
	bool IsSceneEffectActive() const override;
	void WriteSceneEffectConstants(FSceneEffectConstants& OutConstants, uint32 SlotIndex) const override;

	float GetIntensity() const { return Intensity; }
	float GetRadius() const { return Radius; }
	float GetRadiusFallOff() const { return RadiusFallOff; }
	const FLinearColor& GetColor() const { return Color; }

	void SetIntensity(float InIntensity);
	void SetRadius(float InRadius);
	void SetRadiusFallOff(float InRadiusFallOff);
	void SetColor(const FLinearColor& InColor);

private:
	void SyncLocalExtents();
	void RegisterToScene();
	void UnregisterFromScene();

	float Intensity = 1.0f;
	float Radius = 4.0f;
	float RadiusFallOff = 2.0f;
	FLinearColor Color = FLinearColor(1.0f, 0.45f, 0.15f, 1.0f);
};
