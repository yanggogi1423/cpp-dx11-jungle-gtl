#include "Particle/ParticleEmitterInstance.h"

#include <algorithm>

#include "Particle/ParticleDynamicData.h"
#include "Particle/ParticleRendererProperties.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemComponent.h"

FParticleEmitterInstance::~FParticleEmitterInstance()
{
	Reset();
}

// Function : Initialize emitter instance from emitter template and owning component
// input : InTemplate, InComponent, InEmitterIndex
// InTemplate : emitter asset that owns LOD levels and particle modules
// InComponent : particle system component that owns this emitter instance
// InEmitterIndex : index of this emitter inside the particle system
// output : Particle buffers, particle indices, and current LOD state are initialized
void FParticleEmitterInstance::Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent, int32 InEmitterIndex)
{
	Reset();

	SpriteTemplate = InTemplate;
	Component = InComponent;
	EmitterIndex = InEmitterIndex;

	if (SpriteTemplate)
	{
		SpriteTemplate->CacheEmitterModuleInfo();
		CurrentLODLevelIndex = SpriteTemplate->SelectLODLevel(0.0f);
		CurrentLODLevel = SpriteTemplate->GetLODLevel(CurrentLODLevelIndex);
        CurrentCompiledLOD = SpriteTemplate->GetCompiledLODData(CurrentLODLevelIndex);
        if (CurrentCompiledLOD)
        {
            ParticleSize = CurrentCompiledLOD->ParticleSize;
            MaxActiveParticles = std::max(CurrentCompiledLOD->MaxActiveParticles, 1);

			ObservedCompiledRevision = SpriteTemplate->GetCompiledRevision();
            ObservedPayloadSize = CurrentCompiledLOD->PayloadSize;
            ObservedParticleStride = CurrentCompiledLOD->ParticleStride;
            ObservedRenderMode = CurrentCompiledLOD->RenderMode;
        }
        else
        {
            ParticleSize = SpriteTemplate->GetParticleSize();
            MaxActiveParticles = std::max(SpriteTemplate->GetMaxActiveParticleCount(), 1);
        }
	}
	else
	{
		ParticleSize = sizeof(FBaseParticle);
		MaxActiveParticles = 1;
	}

	// RendererProperties owns type-specific payload requirements.
    const int32 PayloadBytes = CurrentCompiledLOD ? CurrentCompiledLOD->PayloadSize : GetRequiredPayloadBytes();
	PayloadOffset = ParticleSize;

	// Cycle 10d: stride source-of-truth = container. Allocate가 (ParticleSize + PayloadBytes)를
	// 받아 align 후 멤버 ParticleStride에 저장하고 단일 블록을 할당한다.
	// 이전 cycle의 redundant `new uint8/uint16` 라인은 silent bug ν 원인이므로 제거됨.
	if (!ParticleStorage.Allocate(MaxActiveParticles, ParticleSize + PayloadBytes))
	{
		MaxActiveParticles = 0;
		return;
	}

	// Allocate는 메모리 placement만 수행 — ParticleIndices 값 초기화 루프 유지 필수
	// (제거 시 첫 Spawn에서 garbage 슬롯 참조 → 즉시 crash).
	for (int32 Index = 0; Index < MaxActiveParticles; ++Index)
	{
		ParticleStorage.ParticleIndices[Index] = static_cast<uint16>(Index);
	}
}

// Function : Release particle instance memory and reset runtime state
// input : None
// output : Particle buffers are released and instance counters return to the default state
void FParticleEmitterInstance::Reset()
{
	ParticleStorage.Reset();
	// Cycle 15a Phase 5: InstanceData / InstancePayloadSize 멤버 삭제됨.
	PayloadOffset = 0;
	ActiveParticles = 0;
	ParticleCounter = 0;
	MaxActiveParticles = 0;
	SpawnFraction = 0.0f;
	EmitterTime = 0.0f;
	PreviousEmitterTime = 0.0f;
	CurrentLODLevelIndex = 0;
	CurrentLODLevel = nullptr;
    CurrentCompiledLOD = nullptr;
    ObservedCompiledRevision = 0;
    ObservedPayloadSize = 0;
    ObservedParticleStride = 0;
    ObservedRenderMode = EParticleEmitterRenderMode::Sprite;
}

// Function : Advance emitter simulation by delta time
// input : DeltaTime
// DeltaTime : elapsed time for this simulation step
// output : New particles are spawned, active particles are updated, and expired particles are removed
void FParticleEmitterInstance::Tick(float DeltaTime, bool bAllowSpawning)
{
	if (!SpriteTemplate || !Component || !ParticleStorage.ParticleData || !ParticleStorage.ParticleIndices || DeltaTime <= 0.0f)
	{
		return;
	}

	PreviousEmitterTime = EmitterTime;
	EmitterTime += DeltaTime;

	SelectLODLevel(Component->ComputeEmitterLODDistance());
	if (!CurrentLODLevel || !CurrentLODLevel->IsEnabled())
	{
		return;
	}

	int32 SpawnCount = 0;
    if (bAllowSpawning)
    {
        UParticleModuleSpawn* SpawnModule = CurrentCompiledLOD
			? CurrentCompiledLOD->SpawnModule : CurrentLODLevel->GetSpawnModule();

        if (SpawnModule)
            SpawnCount = SpawnModule->ComputeSpawnCount(this, DeltaTime);
    }
	const FVector SpawnOrigin = UsesLocalSpace() ? FVector::ZeroVector : Component->GetWorldLocation();
	SpawnParticles(SpawnCount, 0.0f, SpawnCount > 0 ? DeltaTime / static_cast<float>(SpawnCount) : 0.0f,
	               SpawnOrigin, FVector::ZeroVector);

	for (int32 ParticleIndex = 0; ParticleIndex < ActiveParticles; )
	{
		FBaseParticle* Particle = GetParticle(ParticleIndex);
		Particle->RelativeTime += DeltaTime / std::max(Particle->Lifetime, 0.01f);
		if (Particle->RelativeTime >= 1.0f)
		{
			KillParticle(ParticleIndex);
			continue;
		}

		Particle->OldLocation = Particle->Location;
		Particle->Location += Particle->Velocity * DeltaTime;
		++ParticleIndex;
	}
    const TArray<UParticleModule*>& UpdateModules = CurrentCompiledLOD
		? CurrentCompiledLOD->UpdateModules : CurrentLODLevel->GetUpdateModules();
    for (UParticleModule* Module : UpdateModules)
	{
		if (Module && Module->IsEnabled())
		{
			Module->Update(this, DeltaTime);
		}
	}
}

// Function : Select LOD level from current emitter distance
// input : Distance
// Distance : distance from the emitter component to the active camera
// output : CurrentLODLevelIndex and CurrentLODLevel are updated when the selected LOD changes
void FParticleEmitterInstance::SelectLODLevel(float Distance)
{
	if (!SpriteTemplate)
	{
		return;
	}

	const int32 NewLODIndex = SpriteTemplate->SelectLODLevel(Distance);
	if (NewLODIndex == CurrentLODLevelIndex && CurrentLODLevel)
	{
		return;
	}

	CurrentLODLevelIndex = NewLODIndex;
	CurrentLODLevel = SpriteTemplate->GetLODLevel(CurrentLODLevelIndex);
    CurrentCompiledLOD = SpriteTemplate->GetCompiledLODData(CurrentLODLevelIndex);
}

// Function : Spawn particles into available active slots
// input : Count, StartTime, Increment, InitialLocation, InitialVelocity, EventPayload
// Count : number of particles requested for spawn
// StartTime : spawn time assigned to the first particle
// Increment : time offset added between spawned particles
// InitialLocation : base world location before spawn modules modify the particle
// InitialVelocity : base velocity before spawn modules modify the particle
// EventPayload : optional event payload passed from event-driven spawning
// output : Active particle slots are initialized and spawn modules are applied
void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment,
                                              const FVector& InitialLocation, const FVector& InitialVelocity,
                                              FParticleEventInstancePayload* EventPayload)
{
	(void)EventPayload;
	if (!CurrentLODLevel || Count <= 0)
	{
		return;
	}

	for (int32 SpawnIndex = 0; SpawnIndex < Count && ActiveParticles < MaxActiveParticles; ++SpawnIndex)
	{
		const int32 ActiveIndex = ActiveParticles;
		const uint16 SlotIndex = ParticleStorage.ParticleIndices[ActiveIndex];
		FBaseParticle* Particle = reinterpret_cast<FBaseParticle*>(ParticleStorage.ParticleData + SlotIndex * ParticleStorage.GetStride());
		*Particle = FBaseParticle();

		Particle->ParticleId = ++ParticleCounter;
		Particle->Location = InitialLocation;
		Particle->OldLocation = InitialLocation;
		Particle->Velocity = InitialVelocity;
		Particle->BaseVelocity = InitialVelocity;

		const float SpawnTime = StartTime + Increment * static_cast<float>(SpawnIndex);

		const TArray<UParticleModule*>& SpawnModules = CurrentCompiledLOD ?
			CurrentCompiledLOD->SpawnModules : CurrentLODLevel->GetSpawnModules();
		for (UParticleModule* Module : SpawnModules)
		{
			if (Module && Module->IsEnabled())
			{
				Module->Spawn(this, *Particle, SpawnTime);
			}
		}

		++ActiveParticles;
	}
}

// Function : Remove active particle by swapping it with the last active particle
// input : Index
// Index : active particle index to remove
// output : ActiveParticles is decreased and particle index storage remains compact
void FParticleEmitterInstance::KillParticle(int32 Index)
{
	if (Index < 0 || Index >= ActiveParticles)
	{
		return;
	}

	const int32 LastActiveIndex = ActiveParticles - 1;
	std::swap(ParticleStorage.ParticleIndices[Index], ParticleStorage.ParticleIndices[LastActiveIndex]);
	--ActiveParticles;
}

// Cycle 15a Phase 5 (D11): GetRuntimeView() / FParticleEmitterRuntimeView 삭제됨 (호출처 0건 dead code).

// Function : Get mutable particle data by active index
// input : ActiveIndex
// ActiveIndex : active particle index in the compact active list
// output : Pointer to particle data, or nullptr when the index is invalid
FBaseParticle* FParticleEmitterInstance::GetParticle(int32 ActiveIndex)
{
	if (!ParticleStorage.ParticleData || !ParticleStorage.ParticleIndices || ActiveIndex < 0 || ActiveIndex >= ActiveParticles)
	{
		return nullptr;
	}
	return reinterpret_cast<FBaseParticle*>(ParticleStorage.ParticleData + ParticleStorage.ParticleIndices[ActiveIndex] * ParticleStorage.GetStride());
}

// Function : Get read-only particle data by active index
// input : ActiveIndex
// ActiveIndex : active particle index in the compact active list
// output : Const pointer to particle data, or nullptr when the index is invalid
const FBaseParticle* FParticleEmitterInstance::GetParticle(int32 ActiveIndex) const
{
	if (!ParticleStorage.ParticleData || !ParticleStorage.ParticleIndices || ActiveIndex < 0 || ActiveIndex >= ActiveParticles)
	{
		return nullptr;
	}
	return reinterpret_cast<const FBaseParticle*>(ParticleStorage.ParticleData + ParticleStorage.ParticleIndices[ActiveIndex] * ParticleStorage.GetStride());
}


FVector FParticleEmitterInstance::GetComponentWorldLocation() const
{
    if (Component)
        return Component->GetWorldLocation();

    return FVector::ZeroVector;
}

bool FParticleEmitterInstance::UsesLocalSpace() const
{
    const UParticleModuleRequired* RequiredModule = CurrentCompiledLOD
        ? CurrentCompiledLOD->RequiredModule
        : (CurrentLODLevel ? CurrentLODLevel->GetRequiredModule() : nullptr);
    return RequiredModule && RequiredModule->UseLocalSpace();
}

FVector FParticleEmitterInstance::ResolveParticleLocationForRender(const FVector& ParticleLocation) const
{
    if (!UsesLocalSpace() || !Component)
    {
        return ParticleLocation;
    }

    return Component->GetWorldTransform().TransformPosition(ParticleLocation);
}

FVector FParticleEmitterInstance::ResolveParticleVectorForRender(const FVector& ParticleVector) const
{
    if (!UsesLocalSpace() || !Component)
    {
        return ParticleVector;
    }

    return Component->GetWorldTransform().TransformVectorNoScale(ParticleVector);
}

void FParticleEmitterInstance::QueueCollisionEvent(const FParticleEventCollideData& EventData)
{
    if (Component)
        Component->QueueCollisionEvent(EventData);
}

void FParticleEmitterInstance::DispatchQueuedParticleEvents()
{
    if (Component)
        Component->DispatchQueuedParticleEvents();
}

int32 FParticleEmitterInstance::ConsumeSpawnCount(float Rate, float DeltaTime)
{
    if (Rate <= 0.0f || DeltaTime <= 0.0f)
    {
        return 0;
    }

    const float SpawnAmount = Rate * DeltaTime + SpawnFraction;
    const int32 SpawnCount = static_cast<int32>(std::floor(SpawnAmount));
    SpawnFraction = SpawnAmount - static_cast<float>(SpawnCount);
    return SpawnCount;
}

bool FParticleEmitterInstance::CanRebindCompiledLOD(const FCompiledParticleLODData* NewLOD) const
{
    if (!SpriteTemplate || !NewLOD || !CurrentCompiledLOD)
		return false;
    if (ObservedRenderMode != NewLOD->RenderMode)
        return false;
    if (ObservedPayloadSize != NewLOD->PayloadSize)
        return false;
    if (ObservedParticleStride != NewLOD->ParticleStride)
        return false;
    if (MaxActiveParticles != NewLOD->MaxActiveParticles)
        return false;

	return true;
}

void FParticleEmitterInstance::RebindCompiledLOD(float Distance)
{
    if (!SpriteTemplate)
        return;
    CurrentLODLevelIndex = SpriteTemplate->SelectLODLevel(Distance);
    CurrentLODLevel = SpriteTemplate->GetLODLevel(CurrentLODLevelIndex);
    CurrentCompiledLOD = SpriteTemplate->GetCompiledLODData(CurrentLODLevelIndex);

	if (CurrentCompiledLOD)
    {
        ObservedCompiledRevision = SpriteTemplate->GetCompiledRevision();
        ObservedPayloadSize = CurrentCompiledLOD->PayloadSize;
        ObservedParticleStride = CurrentCompiledLOD->ParticleStride;
        ObservedRenderMode = CurrentCompiledLOD->RenderMode;
    }
}

// Function : Query payload byte requirement from current LOD's renderer properties
// input : None
// output : Bytes required by renderer properties beyond FBaseParticle, or 0 when absent
int32 FParticleEmitterInstance::GetRequiredPayloadBytes() const
{
    if (CurrentCompiledLOD)
        return CurrentCompiledLOD->PayloadSize;
    if (CurrentLODLevel)
    {
        if (const UParticleRendererProperties* RendererProperties = CurrentLODLevel->GetEffectiveRendererProperties())
            return RendererProperties->RequiredPayloadBytes();
    }
    return 0;
}

// Cycle 15a Phase 5 (D5): BuildInstanceData() + GetSpriteInstanceData/GetMeshInstanceData/GetBeamVertexData 삭제됨.
// GetRibbonVertexData 만 유지 (D6 — Ribbon 시뮬레이션 무수정 보장 위해).

// Function : Ribbon vertex data getter — base default returns nullptr
// input : OutCount (out-param, always set to 0)
// output : Always nullptr — Ribbon derived instance가 override
//
// Cycle 15a Phase 5: D6 (Ribbon 무수정) 보장 위해 본 virtual + override 유지.
// FDynamicRibbonEmitterData::BuildFromInstance 가 본 메서드 통해 snapshot.
const FRibbonParticleVertex* FParticleEmitterInstance::GetRibbonVertexData(uint32& OutCount) const
{
    OutCount = 0;
    return nullptr;
}

// Function : Create DynamicData (base default — Sprite path + Ribbon placeholder)
// input : None
// output : new FDynamicSpriteEmitterData (Sprite) or FDynamicRibbonEmitterData (Ribbon placeholder)
//
// Cycle 15a (D2): 매 frame new — 호출자가 ownership 가져감 (Phase 4 에서 RenderPass 가 delete).
// 얕은 복사 (D3): ReplayData 의 ParticleData/ParticleIndices 는 instance 소유 메모리를 raw pointer 로 참조만.
//                 단일 스레드 + frame-scope 안전. 멀티스레드 도입 시 deep copy 전환 필요.
//
// Type dispatch:
//   Mesh/Beam derived 가 override — 본 base 함수 호출 안 됨.
//   Sprite/Ribbon 은 derived 없음 — 본 base 함수가 RenderMode 보고 분기 (옵션 C).
//   Ribbon 시뮬레이션 코드 무수정 (D6) — instance 의 GetRibbonVertexData() snapshot 만.
FDynamicEmitterDataBase* FParticleEmitterInstance::CreateDynamicData()
{
    const EParticleEmitterRenderMode RenderMode = CurrentCompiledLOD
        ? CurrentCompiledLOD->RenderMode
        : (CurrentLODLevel ? CurrentLODLevel->GetEffectiveRenderMode() : EParticleEmitterRenderMode::Sprite);

    if (RenderMode == EParticleEmitterRenderMode::Ribbon)
    {
        // Ribbon placeholder path (옵션 C, D6 호환).
        FDynamicRibbonEmitterData* DynData = new FDynamicRibbonEmitterData();
        DynData->EmitterIndex = EmitterIndex;

        FDynamicRibbonEmitterReplayData& Replay = DynData->Source;
        Replay.ActiveParticleCount = ActiveParticles;
        Replay.ParticleStride = ParticleStorage.GetStride();
        Replay.ParticleSize = ParticleSize;
        Replay.PayloadOffset = PayloadOffset;
        Replay.MaxActiveParticles = MaxActiveParticles;
        // TODO(multithread): switch to deep copy when render-thread separation lands
        Replay.ParticleData = ParticleStorage.ParticleData;
        // TODO(multithread): switch to deep copy when render-thread separation lands
        Replay.ParticleIndices = ParticleStorage.ParticleIndices;
        Replay.SortMode = ESortMode::None;
        Replay.Material = nullptr;        // Builder 에서 renderer properties 로부터 추출 후 채움.
        Replay.ParticleTexture = nullptr; // Builder 에서 Material.DiffuseMap 추출 후 채움.

        DynData->BuildFromInstance(*this);
        return DynData;
    }

    // Default Sprite path.
    FDynamicSpriteEmitterData* DynData = new FDynamicSpriteEmitterData();
    DynData->EmitterIndex = EmitterIndex;

    // ReplayData 메타데이터 set (D3: 5 필드 + 얕은 복사 raw 포인터).
    FDynamicSpriteEmitterReplayData& Replay = DynData->Source;
    Replay.ActiveParticleCount = ActiveParticles;
    Replay.ParticleStride = ParticleStorage.GetStride();
    Replay.ParticleSize = ParticleSize;
    Replay.PayloadOffset = PayloadOffset;
    Replay.MaxActiveParticles = MaxActiveParticles;
    // TODO(multithread): switch to deep copy when render-thread separation lands
    Replay.ParticleData = ParticleStorage.ParticleData;
    // TODO(multithread): switch to deep copy when render-thread separation lands
    Replay.ParticleIndices = ParticleStorage.ParticleIndices;
    Replay.SortMode = ESortMode::None;
    Replay.Material = nullptr;        // Builder 에서 RequiredModule->GetMaterial() 후 채움.
    Replay.ParticleTexture = nullptr; // Builder 에서 SubUV/Atlas 추출 후 채움.

    // Sprite instance buffer build (Sprite path 본문 이관).
    DynData->BuildFromInstance(*this);
    return DynData;
}
