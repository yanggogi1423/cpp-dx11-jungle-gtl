#pragma once

#include "Object/Object.h"
#include "Particle/ParticleModules.h"
#include "Particle/ParticleModuleTypeData.h"
#include "Particle/ParticleRendererProperties.h"
#include "ParticleCompiledData.h"

UCLASS()
class UParticleLODLevel : public UObject
{
public:
	GENERATED_BODY(UParticleLODLevel, UObject)
    ~UParticleLODLevel() override;
    void PostDuplicate(UObject* Original) override;
	UParticleModuleRequired* EnsureRequiredModule();
    UParticleModuleSpawn* EnsureSpawnModule();
	UParticleModuleTypeDataBase* EnsureTypeDataModule(EParticleEmitterRenderMode RenderMode = EParticleEmitterRenderMode::Sprite);
	void SetTypeDataModule(UParticleModuleTypeDataBase* InTypeDataModule);

	template <typename T>
    T* AddModule()
    {
        static_assert(std::is_base_of_v<UParticleModule, T>, "T must derive from UParticleModule");

        T* NewModule = UObjectManager::Get().CreateObject<T>();
        if (!NewModule)
            return nullptr;

        Modules.push_back(NewModule);
        CacheModuleLists();
        return NewModule;
    }

    void RemoveModule(UParticleModule* Module);
    void ClearModules();
    bool Validate(TArray<FString>* OutErrors = nullptr) const;
	void CacheModuleLists();

	int32 GetLevel() const { return Level; }
	bool IsEnabled() const { return bEnabled; }
	float GetDistanceThreshold() const { return DistanceThreshold; }
	UParticleModuleRequired* GetRequiredModule() const { return RequiredModule; }
	UParticleModuleSpawn* GetSpawnModule() const { return SpawnModule; }
	const TArray<UParticleModule*>& GetModules() const { return Modules; }
	const TArray<UParticleModule*>& GetSpawnModules() const { return SpawnModules; }
	const TArray<UParticleModule*>& GetUpdateModules() const { return UpdateModules; }
	UParticleModuleTypeDataBase* GetTypeDataModule() const { return TypeDataModule; }

	UParticleRendererProperties* EnsureRendererProperties(EParticleEmitterRenderMode RenderMode = EParticleEmitterRenderMode::Sprite);
    void SetRendererProperties(UParticleRendererProperties* InRendererProperties);
    UParticleRendererProperties* GetRendererProperties() const { return RendererProperties; }

	// Resolve effective render mode through RendererProperties. Deprecated TypeData is normalized before runtime use.
    EParticleEmitterRenderMode GetEffectiveRenderMode() const;
    UParticleRendererProperties* GetEffectiveRendererProperties() const;
	UPROPERTY(DisplayName = "Level")
	int32 Level = 0;

	UPROPERTY(DisplayName = "Enabled")
	bool bEnabled = true;

	UPROPERTY(DisplayName = "Distance Threshold", Min = 0.0f)
	float DistanceThreshold = 100.0f;

	UPROPERTY(DisplayName = "Required Module")
	UParticleModuleRequired* RequiredModule = nullptr;

	UPROPERTY(DisplayName = "Modules")
	TArray<UParticleModule*> Modules;

	// Deprecated Cascade-style TypeData slot. RendererProperties is the canonical render policy.
	UPROPERTY(DisplayName = "TypeData Module")
	UParticleModuleTypeDataBase* TypeDataModule = nullptr;

	UPROPERTY(DisplayName = "Renderer Properties")
    UParticleRendererProperties* RendererProperties = nullptr;

private:
	UParticleModuleSpawn* SpawnModule = nullptr;
	TArray<UParticleModule*> SpawnModules;
	TArray<UParticleModule*> UpdateModules;
};

UCLASS()
class UParticleEmitter : public UObject
{
public:
	GENERATED_BODY(UParticleEmitter, UObject)

	~UParticleEmitter() override;
    void PostDuplicate(UObject* Original) override;
	UParticleLODLevel* AddLODLevel(int32 Level, float DistanceThreshold);
    void RemoveLODLevel(int32 Index);
    void ClearLODLevels();
    void RefreshLODLevelIndices();
    bool Validate(TArray<FString>* OutErrors = nullptr) const;

	void CacheEmitterModuleInfo();
	UParticleLODLevel* GetLODLevel(int32 Index) const;
	int32 SelectLODLevel(float Distance) const;

	const TArray<UParticleLODLevel*>& GetLODLevels() const { return LODLevels; }
	int32 GetParticleSize() const { return ParticleSize; }
	int32 GetMaxActiveParticleCount() const { return MaxActiveParticles; }
    uint32 GetCompiledRevision() const { return CompiledRevision; }

	UPROPERTY(DisplayName = "LOD Levels")
	TArray<UParticleLODLevel*> LODLevels;

	const FCompiledParticleLODData* GetCompiledLODData(int32 Index) const;
    const FCompiledParticleLODData* SelectCompiledLODData(float Distance) const;

private:
    TArray<FCompiledParticleLODData> CompiledLODData;
    uint32 CompiledRevision = 0;

	int32 ParticleSize = sizeof(FBaseParticle);
	int32 MaxActiveParticles = 128;
};

UCLASS()
class UParticleSystem : public UObject
{
public:
	GENERATED_BODY(UParticleSystem, UObject)
    ~UParticleSystem() override;
    void PostDuplicate(UObject* Original) override;

	const TArray<UParticleEmitter*>& GetEmitters() const { return Emitters; }
    UParticleEmitter* AddEmitter();
    void RemoveEmitter(int32 Index);
    void ClearEmitters();
	void CacheEmitterModuleInfo();
    bool Validate(TArray<FString>* OutErrors = nullptr) const;
	void SetAssetPath(const FString& InAssetPath);
	const FString& GetAssetPath() const { return AssetPath; }
    static UParticleSystem* CreateDefaultSpriteSystem();
    // Detail panel 검증용 기본 mesh emitter system.
    // CreateDefaultSpriteSystem과 동일 구조 + sprite renderer 대신 mesh renderer.
    static UParticleSystem* CreateDefaultMeshSystem();
    // Detail panel 검증용 기본 ribbon emitter system.
    // CreateDefaultSpriteSystem과 동일 구조 + sprite renderer 대신 ribbon renderer.
    static UParticleSystem* CreateDefaultRibbonSystem();
    // Cycle 13a/13b: detail panel 검증용 기본 beam emitter system.
    // Sprite/Mesh/Ribbon 패턴 답습 + beam renderer properties + Source/Target/Noise 모듈.
    static UParticleSystem* CreateDefaultBeamSystem();

	UPROPERTY(DisplayName = "Asset Path")
	FString AssetPath;

	UPROPERTY(DisplayName = "Update Time FPS", Min = 0.0f)
	float UpdateTimeFPS = 60.0f;

	UPROPERTY(DisplayName = "LODDistances")
	TArray<float> LODDistances = { 100.0f };

	UPROPERTY(DisplayName = "Emitters")
    TArray<UParticleEmitter*> Emitters;
};
