#include "Particle/ParticleSystemComponent.h"

#include "Camera/ViewportCamera.h"
#include "Core/ResourceManager.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Particle/ParticleDynamicData.h"
#include "Particle/ParticleRendererProperties.h"
#include "Particle/ParticleSystem.h"
#include "Render/Scene/RenderBus.h"

#include <algorithm>
#include <cstring>

UParticleSystemComponent::UParticleSystemComponent()
{
	SetEnableCull(false);
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	ClearEmitterInstances();
	ReleaseOwnedTransientTemplate();
}

// Function : Set particle system template and recreate emitter instances when it changes
// input : InTemplate
// InTemplate : particle system asset to simulate on this component
// output : Template is updated, asset path mirrored, and emitter instances match the new template
void UParticleSystemComponent::SetTemplate(UParticleSystem* InTemplate)
{
	SetTemplate(InTemplate, false);
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* InTemplate, bool bTakeTransientOwnership)
{
	if (Template == InTemplate && !EmitterInstances.empty())
	{
		TemplateAssetPath.SetPath((InTemplate && !bTakeTransientOwnership) ? InTemplate->GetAssetPath() : FString());
		if (bTakeTransientOwnership)
		{
			OwnedTransientTemplate = InTemplate;
		}
		return;
	}

	UParticleSystem* PreviousOwnedTemplate = OwnedTransientTemplate;
	const bool bDestroyPreviousOwnedTemplate = PreviousOwnedTemplate && PreviousOwnedTemplate != InTemplate;

	Template = InTemplate;
	OwnedTransientTemplate = bTakeTransientOwnership ? InTemplate : nullptr;
	TemplateAssetPath.SetPath((InTemplate && !bTakeTransientOwnership) ? InTemplate->GetAssetPath() : FString());
	RecreateEmitterInstances();

	if (bDestroyPreviousOwnedTemplate)
	{
		UObjectManager::Get().DestroyObject(PreviousOwnedTemplate);
	}
}

void UParticleSystemComponent::ReleaseOwnedTransientTemplate()
{
	if (!OwnedTransientTemplate)
	{
		return;
	}

	UParticleSystem* TemplateToDestroy = OwnedTransientTemplate;
	if (Template == TemplateToDestroy)
	{
		Template = nullptr;
		TemplateAssetPath.SetPath(FString());
	}
	OwnedTransientTemplate = nullptr;
	UObjectManager::Get().DestroyObject(TemplateToDestroy);
}

void UParticleSystemComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	const bool bTemplatePathChanged = PropertyName &&
		(std::strcmp(PropertyName, "TemplateAssetPath") == 0 || std::strcmp(PropertyName, "Template") == 0);
	if (bTemplatePathChanged)
	{
		const FString RequestedPath = TemplateAssetPath.GetPath();
		UParticleSystem* Resolved = RequestedPath.empty()
			? nullptr
			: FResourceManager::Get().LoadParticleSystem(RequestedPath);
		SetTemplate(Resolved);
		return;
	}

	if (Template && EmitterInstances.empty())
	{
		RecreateEmitterInstances();
	}
}

void UParticleSystemComponent::RefreshTemplateRuntime(bool bRestartSimulation)
{
    if (!Template)
    {
        ClearEmitterInstances();
        return;
    }
    Template->CacheEmitterModuleInfo();

	const TArray<UParticleEmitter*>& Emitters = Template->GetEmitters();
    if (bRestartSimulation || EmitterInstances.size() != Emitters.size())
    {
        RecreateEmitterInstances();
        return;
    }
    const float Distance = ComputeEmitterLODDistance();

	for (int32 Index = 0; Index < static_cast<int32>(Emitters.size()); Index++)
    {
        UParticleEmitter* Emitter = Emitters[Index];
        FParticleEmitterInstance* Instance = GetEmitterInstance(Index);
		const FCompiledParticleLODData* NewLOD = Emitter ?
			Emitter->SelectCompiledLODData(Distance) : nullptr;

		if (!Emitter || !Instance || Instance->GetTemplate() != Emitter ||
            !Instance->CanRebindCompiledLOD(NewLOD))
        {
            RecreateEmitterInstances();
            return;
        }
    }

    for (FParticleEmitterInstance* Instance : EmitterInstances)
    {
        if (Instance)
        {
            Instance->RebindCompiledLOD(Distance);
        }
    }
}

void UParticleSystemComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	if (Ar.IsLoading())
	{
		const FString RequestedPath = TemplateAssetPath.GetPath();
		UParticleSystem* Resolved = RequestedPath.empty()
			? nullptr
			: FResourceManager::Get().LoadParticleSystem(RequestedPath);
		SetTemplate(Resolved);
	}
}

// Function : Rebuild emitter instances from current particle system template
// input : None
// output : Existing instances are cleared and one runtime instance is created per template emitter
void UParticleSystemComponent::RecreateEmitterInstances()
{
	ClearEmitterInstances();
	UpdateTimeAccumulator = 0.0f;
	if (!Template)
	{
		return;
	}

	const TArray<UParticleEmitter*>& Emitters = Template->GetEmitters();
	EmitterInstances.reserve(Emitters.size());
	for (int32 Index = 0; Index < static_cast<int32>(Emitters.size()); ++Index)
	{
		UParticleEmitter* EmitterAsset = Emitters[Index];
		// RendererProperties가 캐싱/마이그레이션되어 있도록 보장. Init 내부에서도 다시 호출되지만 idempotent.
		// 명시 호출 이유: LOD0 조회를 Instance 생성 전에 해야 하므로, asset 측 캐시가 stale이면 silent fallback 위험.
		if (EmitterAsset)
		{
			EmitterAsset->CacheEmitterModuleInfo();
		}
        const FCompiledParticleLODData* CompiledLOD = EmitterAsset ?
			EmitterAsset->SelectCompiledLODData(0.0f) : nullptr;

        UParticleRendererProperties* RendererProperties = CompiledLOD ?
			CompiledLOD->RendererProperties : nullptr;
		// RendererProperties가 있으면 render type별 derived instance 생성 hook 사용. 없으면 sprite-style base instance.
		FParticleEmitterInstance* Instance = RendererProperties
			? RendererProperties->CreateInstance(this, Index)
			: new FParticleEmitterInstance();
		Instance->Init(EmitterAsset, this, Index);
		EmitterInstances.push_back(Instance);
	}
}

// Function : Delete all runtime emitter instances and pending particle events
// input : None
// output : EmitterInstances and PendingCollisionEvents become empty
void UParticleSystemComponent::ClearEmitterInstances()
{
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		delete Instance;
	}
	EmitterInstances.clear();
	PendingCollisionEvents.clear();
}

// Function : Compute distance from this particle component to the active camera
// input : None
// output : Distance to active camera, or 0 when no focused world or active camera exists
float UParticleSystemComponent::ComputeEmitterLODDistance() const
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* World = OwnerActor ? OwnerActor->GetFocusedWorld() : nullptr;
	if (!World || !World->GetActiveCamera())
	{
		return 0.0f;
	}

	return FVector::Dist(GetWorldLocation(), World->GetActiveCamera()->GetLocation());
}

// Function : Add particle collision event to component queue
// input : EventData
// EventData : collision event generated by a particle module
// output : EventData is stored until DispatchQueuedParticleEvents is called
void UParticleSystemComponent::QueueCollisionEvent(const FParticleEventCollideData& EventData)
{
	PendingCollisionEvents.push_back(EventData);
}

// Function : Broadcast queued particle collision events and clear the queue
// input : None
// output : OnParticleCollide is broadcast for each queued event and the queue becomes empty
void UParticleSystemComponent::DispatchQueuedParticleEvents()
{
	if (PendingCollisionEvents.empty())
	{
		return;
	}

	for (const FParticleEventCollideData& EventData : PendingCollisionEvents)
	{
		OnParticleCollide.Broadcast(EventData);
	}
	PendingCollisionEvents.clear();
}

// Function : Recalculate world bounds from component location and active particle locations
// input : None
// output : WorldAABB contains the component bounds expanded by active particle positions
void UParticleSystemComponent::UpdateWorldAABB() const
{
	WorldAABB.Reset();
	const FVector ComponentLocation = GetWorldLocation();
	WorldAABB.Expand(ComponentLocation - FVector(100.0f, 100.0f, 100.0f));
	WorldAABB.Expand(ComponentLocation + FVector(100.0f, 100.0f, 100.0f));

	for (const FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance)
		{
			continue;
		}

		for (int32 ParticleIndex = 0; ParticleIndex < Instance->GetActiveParticleCount(); ++ParticleIndex)
		{
			const FBaseParticle* Particle = Instance->GetParticle(ParticleIndex);
			if (Particle)
			{
				WorldAABB.Expand(Instance->ResolveParticleLocationForRender(Particle->Location));
			}
		}
	}
}

// Function : Test particle system component against a ray
// input : Ray, OutHitResult
// Ray : world ray used for picking or collision query
// OutHitResult : hit result reset by this query
// output : false because particle raycast is not implemented yet
bool UParticleSystemComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
	(void)Ray;
	OutHitResult.Reset();
	return false;
}

int32 UParticleSystemComponent::GetTotalActiveParticleCount() const
{
	int32 TotalCount = 0;
	for (const FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (Instance)
		{
			TotalCount += Instance->GetActiveParticleCount();
		}
	}
	return TotalCount;
}

int32 UParticleSystemComponent::GetEmitterInstanceCount() const
{
	return static_cast<int32>(EmitterInstances.size());
}

FParticleEmitterInstance* UParticleSystemComponent::GetEmitterInstance(int32 Index)
{
	if (Index >= 0 && Index < GetEmitterInstanceCount())
	{
		return EmitterInstances[Index];
	}
	return nullptr;
}

const FParticleEmitterInstance* UParticleSystemComponent::GetEmitterInstance(int32 Index) const
{
	if (Index >= 0 && Index < GetEmitterInstanceCount())
	{
		return EmitterInstances[Index];
	}
	return nullptr;
}

// Function : Tick every emitter instance and mark spatial bounds dirty
// input : DeltaTime
// DeltaTime : elapsed time for this component update
// output : Emitter simulations advance and the spatial index is notified for bounds refresh
void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	TickPreview(DeltaTime, true);
}

void UParticleSystemComponent::TickPreview(float DeltaTime, bool bAllowSpawning)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	auto TickEmitterInstances = [&](float StepDeltaTime)
	{
		for (FParticleEmitterInstance* Instance : EmitterInstances)
		{
			if (Instance)
			{
				Instance->Tick(StepDeltaTime, bAllowSpawning);
			}
		}
	};

	float UpdateFPS = Template ? Template->UpdateTimeFPS : 0.0f;
	if (UpdateFPS <= 0.0f)
	{
		TickEmitterInstances(DeltaTime);
		NotifySpatialIndexDirty();
		return;
	}

	UpdateFPS = std::max(1.0f, UpdateFPS);
	const float FixedStep = 1.0f / UpdateFPS;
	constexpr int32 MaxStepsPerTick = 8;
	UpdateTimeAccumulator = std::min(UpdateTimeAccumulator + DeltaTime, FixedStep * static_cast<float>(MaxStepsPerTick));

	int32 StepCount = 0;
	while (UpdateTimeAccumulator >= FixedStep && StepCount < MaxStepsPerTick)
	{
		TickEmitterInstances(FixedStep);
		UpdateTimeAccumulator -= FixedStep;
		++StepCount;
	}

	if (StepCount > 0)
	{
		NotifySpatialIndexDirty();
	}
}

void UParticleSystemComponent::SetEditorPreviewSoloEmitters(const TArray<int32>& InSoloEmitterIndices)
{
	EditorPreviewSoloEmitterIndices = InSoloEmitterIndices;
}

void UParticleSystemComponent::ClearEditorPreviewSoloEmitters()
{
	EditorPreviewSoloEmitterIndices.clear();
}

// Cycle 15a Phase 5 (D5): BuildInstanceData() 삭제됨 — CollectDynamicData() 가 대체.

// Function : Collect DynamicData for all emitters (Cycle 15a Phase 4)
// input : None
// output : array of FDynamicEmitterDataBase* — caller takes ownership (RenderPass deletes at frame end)
//
// 매 frame new (D2). 단일 스레드 + frame-scope life-cycle 안전.
// Component 는 RenderCommand 모름 — instance->CreateDynamicData() dispatch 만.
TArray<FDynamicEmitterDataBase*> UParticleSystemComponent::CollectDynamicData()
{
	TArray<FDynamicEmitterDataBase*> Result;
	Result.reserve(EmitterInstances.size());
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance)
		{
			continue;
		}
		const int32 EmitterIndex = Instance->GetEmitterIndex();
		if (!EditorPreviewSoloEmitterIndices.empty() &&
			std::find(EditorPreviewSoloEmitterIndices.begin(), EditorPreviewSoloEmitterIndices.end(), EmitterIndex) == EditorPreviewSoloEmitterIndices.end())
		{
			continue;
		}
		FDynamicEmitterDataBase* DynData = Instance->CreateDynamicData();
		if (DynData)
		{
			Result.push_back(DynData);
		}
	}
	return Result;
}

// Function : Cache RenderBus camera state for derived BuildInstanceData consumption (Cycle 14, 결정 18 β)
// input : InRenderBus
// InRenderBus : RenderBus belonging to the current render frame
// output : CachedCamera* members are populated and bCachedCameraValid is set true
//
// Builder 가 ParticleSystemComponent->BuildInstanceData() 호출 직전에 호출.
// derived Mesh instance::BuildInstanceData 가 GetOwningComponent() 통해 cache 를 read.
// signature 변경 0건 보장 (옵션 α 회피) — 본 메서드는 BuildInstanceData 와 별도 진입점.
void UParticleSystemComponent::CacheCameraFromRenderBus(const FRenderBus& InRenderBus)
{
	CachedCameraPosition = InRenderBus.GetCameraPosition();
	CachedCameraForward = InRenderBus.GetCameraForward();
	CachedCameraUp = InRenderBus.GetCameraUp();
	CachedCameraRight = InRenderBus.GetCameraRight();
	bCachedCameraValid = true;
}
