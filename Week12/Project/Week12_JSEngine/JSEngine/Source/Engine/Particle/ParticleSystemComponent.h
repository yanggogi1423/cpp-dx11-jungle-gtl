#pragma once

#include "Component/PrimitiveComponent.h"
#include "Object/ObjectPtr.h"
#include "Particle/ParticleEmitterInstance.h"

class FRenderBus;
class UParticleSystem;
struct FDynamicEmitterDataBase;

DECLARE_DELEGATE(FOnParticleCollide, const FParticleEventCollideData&);

UCLASS(SpawnableComponent, DisplayName = "Particle System Component", Category = "Effects")
class UParticleSystemComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY(UParticleSystemComponent, UPrimitiveComponent)

	UParticleSystemComponent();
	~UParticleSystemComponent() override;

	void SetTemplate(UParticleSystem* InTemplate);
	void SetTemplate(UParticleSystem* InTemplate, bool bTakeTransientOwnership);
	void PostEditProperty(const char* PropertyName) override;
	void Serialize(FArchive& Ar) override;
	UParticleSystem* GetTemplate() const { return Template; }
	FString GetTemplateAssetPath() const { return TemplateAssetPath.GetPath(); }
	const TArray<FParticleEmitterInstance*>& GetEmitterInstances() const { return EmitterInstances; } // component가 사용하는 emitter instance들
	TArray<FParticleEventCollideData>& GetPendingCollisionEvents() { return PendingCollisionEvents; }
	const TArray<FParticleEventCollideData>& GetPendingCollisionEvents() const { return PendingCollisionEvents; }

	void RefreshTemplateRuntime(bool bRestartSimulation);
	void RecreateEmitterInstances();
	void ClearEmitterInstances();
	void TickPreview(float DeltaTime, bool bAllowSpawning);
	void SetEditorPreviewSoloEmitters(const TArray<int32>& InSoloEmitterIndices);
	void ClearEditorPreviewSoloEmitters();
	float ComputeEmitterLODDistance() const;
	void QueueCollisionEvent(const FParticleEventCollideData& EventData);
	void DispatchQueuedParticleEvents();
	bool HasPendingCollisionEvents() const { return !PendingCollisionEvents.empty(); }
	void ClearPendingCollisionEvents() { PendingCollisionEvents.clear(); }

	// Cycle 15a Phase 4 (ReplayData/DynamicData, D2 매 frame new): Component 가 모든 emitter 의 DynamicData 를 모아 array 반환.
	// 호출자(Builder)가 ownership 가져감 — RenderCommand 에 매핑 후 RenderPass 가 frame 끝에 delete.
	// RenderCommand 모름 원칙 유지 — Component 는 단지 instance->CreateDynamicData() dispatch hub.
	//
	// Cycle 15a Phase 5 (D5): 기존 BuildInstanceData() 삭제 — CollectDynamicData() 가 대체.
	TArray<FDynamicEmitterDataBase*> CollectDynamicData();

	// Cycle 14 (M1, 결정 18 옵션 β): Builder 가 BuildInstanceData() 호출 직전에 호출.
	// RenderBus 의 camera 4 vector + position 을 Component 멤버에 캐싱 →
	// derived Mesh instance 의 BuildInstanceData 가 alignment 계산에 사용 (PSA_FacingCameraPosition).
	// signature 변경 0건 보장 — 옵션 α (BuildInstanceData 인자 확장) 회피.
	// 첫 frame 또는 RenderBus 부재 frame 에서는 bCachedCameraValid=false → derived 가 PSA_Velocity fallback (위험 12 방어).
	void CacheCameraFromRenderBus(const FRenderBus& InRenderBus);

	// Cycle 14 (M1): cached camera accessor. derived instance 가 read.
	// bCachedCameraValid 가 false 면 다른 4 vector 값은 의미 없음 (zero-init).
	bool IsCachedCameraValid() const { return bCachedCameraValid; }
	const FVector& GetCachedCameraPosition() const { return CachedCameraPosition; }
	const FVector& GetCachedCameraForward() const { return CachedCameraForward; }
	const FVector& GetCachedCameraUp() const { return CachedCameraUp; }
	const FVector& GetCachedCameraRight() const { return CachedCameraRight; }

	EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_ParticleSystem; }
	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	bool SupportsOutline() const override { return false; }

	FOnParticleCollide OnParticleCollide;

	int32 GetTotalActiveParticleCount() const;

	int32 GetEmitterInstanceCount() const;
	FParticleEmitterInstance* GetEmitterInstance(int32 Index);
	const FParticleEmitterInstance* GetEmitterInstance(int32 Index) const;

	// Component-level opacity multiplier — AlphaBlend 모드인 emitter 의 최종 alpha 에 곱해짐.
	// runtime fade-in/out 용. Properties 의 emitter-level Opacity 와 곱연산.
	float GetOpacityMultiplier() const { return OpacityMultiplier; }
	void SetOpacityMultiplier(float InValue) { OpacityMultiplier = InValue; }

protected:
	void TickComponent(float DeltaTime) override;

private:
	void ReleaseOwnedTransientTemplate();

    // Serialized asset path. StaticMeshComponent 패턴 답습 — component 가 path 를 직접 보유하므로
    // UParticleSystem::AssetPath 가 비어있어도 scene save/load 라운드트립이 깨지지 않음.
    UPROPERTY(DisplayName = "Template")
    TSoftObjectPtr<UParticleSystem> TemplateAssetPath;

    // Runtime cache (non-UPROPERTY). TemplateAssetPath 의 path 가 ResourceManager 를 통해 resolve 된 결과.
    // EmitterInstances 재생성/Detail panel 표시 등 모든 런타임 read 는 이 cache 를 사용.
    UParticleSystem* Template = nullptr;

	UParticleSystem* OwnedTransientTemplate = nullptr;

	TArray<FParticleEmitterInstance*> EmitterInstances;
	TArray<FParticleEventCollideData> PendingCollisionEvents;
	TArray<int32> EditorPreviewSoloEmitterIndices;
	float UpdateTimeAccumulator = 0.0f;

	// Cycle 14 (M1, 결정 18 옵션 β): RenderBus → Component → derived instance 캐싱 경로.
	// 첫 frame 또는 외부 호출자 (예: EditorMainPanelDebug) 가 CacheCameraFromRenderBus 미호출 시 bCachedCameraValid=false 유지 →
	// PSA_FacingCameraPosition 모드는 PSA_Velocity 로 fallback (silent bug 위험 12 방어).
	// frame-단위 갱신: Builder 가 매 frame BuildInstanceData 직전 호출하므로 Builder path 는 항상 valid.
	FVector CachedCameraPosition = FVector::ZeroVector;
	FVector CachedCameraForward = FVector::ZeroVector;
	FVector CachedCameraUp = FVector::ZeroVector;
	FVector CachedCameraRight = FVector::ZeroVector;
	bool bCachedCameraValid = false;

	UPROPERTY(DisplayName = "Opacity Multiplier", Category = "Rendering", Min = 0.0f, Max = 1.0f)
	float OpacityMultiplier = 1.0f;
};
