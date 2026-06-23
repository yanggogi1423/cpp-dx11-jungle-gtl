#pragma once

#include "Object/FName.h"
#include "Particle/ParticleModule.h"
#include "Render/Resource/Material.h"

#include <algorithm>

struct FTextureAtlasResource;
struct FLightInfo;

UCLASS()
class UParticleModuleRequired : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleRequired, UParticleModule)

	UParticleModuleRequired();
	void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;
	void PostEditProperty(const char* PropertyName) override;

	int32 GetMaxParticles() const { return MaxParticles; }
	float GetEmitterDuration() const { return EmitterDuration; }
	bool IsLooping() const { return bLooping; }
	bool UseLocalSpace() const { return bUseLocalSpace; }
	UMaterialInterface* GetMaterial() const { return Material; }
	int32 GetSubImagesHorizontal() const { return std::max(SubImagesHorizontal, 1); }
	int32 GetSubImagesVertical() const { return std::max(SubImagesVertical, 1); }
	EParticleEmitterRenderMode GetRenderMode() const { return RenderMode; }
	void SetRenderMode(EParticleEmitterRenderMode InRenderMode) { RenderMode = InRenderMode; }

private:
	UPROPERTY(DisplayName = "Material", Category = "Emitter", ReferenceKind = Asset)
	UMaterialInterface* Material = nullptr;

	UPROPERTY(DisplayName = "Max Particles", Min = 1)
	int32 MaxParticles = 128;

	UPROPERTY(DisplayName = "Emitter Duration", Min = 0.0f)
	float EmitterDuration = 1.0f;

	UPROPERTY(DisplayName = "Looping")
	bool bLooping = true;

	UPROPERTY(DisplayName = "Use Local Space")
	bool bUseLocalSpace = false;

	UPROPERTY(DisplayName = "Sub Images Horizontal", Category = "SubUV", Min = 1)
	int32 SubImagesHorizontal = 1;

	UPROPERTY(DisplayName = "Sub Images Vertical", Category = "SubUV", Min = 1)
	int32 SubImagesVertical = 1;

	UPROPERTY(DisplayName = "Emitter Type", Category = "TypeData", NoEdit)
	EParticleEmitterRenderMode RenderMode = EParticleEmitterRenderMode::Sprite;
};

UCLASS()
class UParticleModuleSpawn : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleSpawn, UParticleModule)

	UParticleModuleSpawn();
	int32 ComputeSpawnCount(FParticleEmitterInstance* Owner, float DeltaTime);

private:
	UPROPERTY(DisplayName = "Rate", Min = 0.0f)
	float Rate = 10.0f;
};

UCLASS()
class UParticleModuleBurst : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleBurst, UParticleModule)

	UParticleModuleBurst();
	void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;

private:
	UPROPERTY(DisplayName = "Burst Count", Min = 0)
	int32 BurstCount = 16;

	UPROPERTY(DisplayName = "Burst Time", Min = 0.0f)
	float BurstTime = 0.0f;

	UPROPERTY(DisplayName = "Repeat")
	bool bRepeat = false;

	UPROPERTY(DisplayName = "Repeat Interval", Min = 0.001f)
	float RepeatInterval = 1.0f;
};

UCLASS()
class UParticleModuleLifetime : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleLifetime, UParticleModule)

	UParticleModuleLifetime();
	void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;

private:
	UPROPERTY(DisplayName = "Lifetime Min", Min = 0.01f)
	float LifetimeMin = 1.0f;

	UPROPERTY(DisplayName = "Lifetime Max", Min = 0.01f)
	float LifetimeMax = 1.0f;
};

UCLASS()
class UParticleModuleLocation : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleLocation, UParticleModule)

	UParticleModuleLocation();
	void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;

private:
	UPROPERTY(DisplayName = "Start Location Min")
	FVector StartLocationMin = FVector::ZeroVector;

	UPROPERTY(DisplayName = "Start Location Max")
	FVector StartLocationMax = FVector::ZeroVector;
};

UENUM()
enum class EProceduralParticleShape : uint8
{
	Sphere,
	Box,
	Cone,
};

UCLASS()
class UParticleModuleLocationShape : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleLocationShape, UParticleModule)

	UParticleModuleLocationShape();
	void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;

private:
	UPROPERTY(DisplayName = "Shape")
	EProceduralParticleShape Shape = EProceduralParticleShape::Sphere;

	UPROPERTY(DisplayName = "Surface Only")
	bool bSurfaceOnly = false;

	UPROPERTY(DisplayName = "Sphere Radius", Min = 0.0f)
	float SphereRadius = 50.0f;

	UPROPERTY(DisplayName = "Box Extents")
	FVector BoxExtents = FVector(50.0f, 50.0f, 50.0f);

	UPROPERTY(DisplayName = "Cone Height", Min = 0.0f)
	float ConeHeight = 100.0f;

	UPROPERTY(DisplayName = "Cone Half Angle", Min = 0.0f, Max = 89.0f)
	float ConeHalfAngle = 30.0f;
};

UCLASS()
class UParticleModuleVelocity : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleVelocity, UParticleModule)

	UParticleModuleVelocity();
	void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;

private:
	UPROPERTY(DisplayName = "Start Velocity Min")
	FVector StartVelocityMin = FVector(0.0f, 0.0f, 50.0f);

	UPROPERTY(DisplayName = "Start Velocity Max")
	FVector StartVelocityMax = FVector(0.0f, 0.0f, 100.0f);
};

UCLASS()
class UParticleModuleAcceleration : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleAcceleration, UParticleModule)

	UParticleModuleAcceleration();
	void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;

private:
	UPROPERTY(DisplayName = "Acceleration")
	FVector Acceleration = FVector(0.0f, 0.0f, -98.0f);
};

UCLASS()
class UParticleModuleDrag : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleDrag, UParticleModule)

	UParticleModuleDrag();
	void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;

private:
	UPROPERTY(DisplayName = "Drag Coefficient", Min = 0.0f)
	float DragCoefficient = 0.0f;
};

UCLASS()
class UParticleModuleRotationRate : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleRotationRate, UParticleModule)

	UParticleModuleRotationRate();
	void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;
	void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;

private:
	UPROPERTY(DisplayName = "Start Rotation Rate Min")
	float StartRotationRateMin = -180.0f;

	UPROPERTY(DisplayName = "Start Rotation Rate Max")
	float StartRotationRateMax = 180.0f;
};

UCLASS()
class UParticleModuleColor : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleColor, UParticleModule)

	UParticleModuleColor();
	void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;
	void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;

private:
	UPROPERTY(DisplayName = "Color Over Life")
	FVector ColorOverLife = FVector(255.0f, 255.0f, 255.0f);

	UPROPERTY(DisplayName = "Alpha Over Life", Min = 0.0f, Max = 255.0f)
	float AlphaOverLife = 255.0f;
};

UCLASS()
class UParticleModuleLight : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleLight, UParticleModule)

	UParticleModuleLight();
	bool ShouldCreateLight(const FBaseParticle& Particle, int32 CurrentLightCount) const;
	void BuildLightInfo(const FBaseParticle& Particle, FLightInfo& OutLightInfo) const;

private:
	UPROPERTY(DisplayName = "Light Enabled")
	bool bLightEnabled = true;

	UPROPERTY(DisplayName = "Use Particle Color")
	bool bUseParticleColor = true;

	UPROPERTY(DisplayName = "Use Particle Alpha")
	bool bUseParticleAlpha = true;

	UPROPERTY(DisplayName = "Light Color")
	FColor LightColor = FColor(1.0f, 0.55f, 0.18f, 1.0f);

	UPROPERTY(DisplayName = "Brightness", Min = 0.0f)
	float Brightness = 1.0f;

	UPROPERTY(DisplayName = "Radius", Min = 0.0f)
	float Radius = 250.0f;

	UPROPERTY(DisplayName = "Radius Scale", Min = 0.0f)
	float RadiusScale = 0.0f;

	UPROPERTY(DisplayName = "Falloff", Min = 0.0f)
	float Falloff = 2.0f;

	UPROPERTY(DisplayName = "Spawn Fraction", Min = 0.0f, Max = 1.0f)
	float SpawnFraction = 1.0f;

	UPROPERTY(DisplayName = "Max Lights Per Emitter", Min = 0)
	int32 MaxLightsPerEmitter = 16;
};

UCLASS()
class UParticleModuleSize : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleSize, UParticleModule)

	UParticleModuleSize();
	void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;
	void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;

private:
	UPROPERTY(DisplayName = "Size Over Life")
	FVector SizeOverLife = FVector(1.0f, 1.0f, 1.0f);
};

UENUM()
enum class EParticleCollisionResponse : uint8
{
	Bounce,
	Kill,
	Stop,
	Ignore,
};

UENUM()
enum class EParticleCollisionTraceMode : uint8
{
	Point,
	Sphere,
};

UCLASS()
class UParticleModuleCollision : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleCollision, UParticleModule)

	UParticleModuleCollision();
	void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;
	EParticleCollisionTraceMode GetTraceMode() const { return TraceMode; }
	bool IsUsingParticleSizeAsRadius() const { return bUseParticleSizeAsRadius; }

private:
	UPROPERTY(DisplayName = "Collision Enabled")
	bool bCollisionEnabled = true;

	UPROPERTY(DisplayName = "Trace Mode")
	EParticleCollisionTraceMode TraceMode = EParticleCollisionTraceMode::Point;

	UPROPERTY(DisplayName = "Use Particle Size As Radius")
	bool bUseParticleSizeAsRadius = true;

	UPROPERTY(DisplayName = "Collision Radius", Min = 0.0f)
	float CollisionRadius = 1.0f;

	UPROPERTY(DisplayName = "Response")
	EParticleCollisionResponse Response = EParticleCollisionResponse::Bounce;

	UPROPERTY(DisplayName = "Restitution", Min = 0.0f, Max = 1.0f)
	float Restitution = 0.25f;

	UPROPERTY(DisplayName = "Friction", Min = 0.0f, Max = 1.0f)
	float Friction = 0.0f;

	UPROPERTY(DisplayName = "Max Collisions", Min = 0)
	int32 MaxCollisions = 0;

	UPROPERTY(DisplayName = "Kill On Max Collisions")
	bool bKillWhenMaxCollisionsReached = false;

	UPROPERTY(DisplayName = "Max Collision Distance", Min = 0.0f)
	float MaxCollisionDistance = 0.0f;

	UPROPERTY(DisplayName = "Collision Check Fraction", Min = 0.0f, Max = 1.0f)
	float CollisionCheckFraction = 1.0f;

	UPROPERTY(DisplayName = "Ignore Owner")
	bool bIgnoreOwner = true;

	UPROPERTY(DisplayName = "Generate Events")
	bool bGenerateCollisionEvents = true;
};

UCLASS()
class UParticleModuleEventGenerator : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleEventGenerator, UParticleModule)

	UParticleModuleEventGenerator();
	void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;

private:
	UPROPERTY(DisplayName = "Dispatch Collision Events")
	bool bDispatchCollisionEvents = true;

	UPROPERTY(DisplayName = "Max Collision Events Per Frame", Min = 0)
	int32 MaxCollisionEventsPerFrame = 0;

	UPROPERTY(DisplayName = "Keep Newest Collision Events")
	bool bKeepNewestCollisionEvents = true;
};

UENUM()
enum class EParticleSubUVPlaybackMode : uint8
{
	Life,
	FramesPerSecond,
};

UCLASS()
class USubUVModule : public UParticleModule
{
public:
	GENERATED_BODY(USubUVModule, UParticleModule)

	USubUVModule();
	void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;
	void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;
	void Serialize(FArchive& Ar) override;
	void PostEditProperty(const char* PropertyName) override;

	void SetSubUVName(const FName& InName);
	const FName& GetSubUVName() const { return SubUVName; }
	const FTextureAtlasResource* GetCachedSubUV() const { return CachedSubUV; }
	int32 GetStartFrameIndex() const { return std::max(StartFrameIndex, 0); }
	int32 GetEndFrameIndex() const { return std::max(EndFrameIndex, 0); }

private:
	UPROPERTY(DisplayName = "SubUV")
	FName SubUVName;

	UPROPERTY(DisplayName = "Start Index", Min = 0)
	int32 StartFrameIndex = 0;

	UPROPERTY(DisplayName = "End Index", Min = 0)
	int32 EndFrameIndex = 0;

	UPROPERTY(DisplayName = "Playback Mode")
	EParticleSubUVPlaybackMode PlaybackMode = EParticleSubUVPlaybackMode::Life;

	UPROPERTY(DisplayName = "Frame Rate", Min = 0.0f)
	float FrameRate = 24.0f;

	UPROPERTY(DisplayName = "Loop")
	bool bLoop = false;

	UPROPERTY(DisplayName = "Random Start Frame")
	bool bRandomStartFrame = false;

	FTextureAtlasResource* CachedSubUV = nullptr; // ResourceManager 소유, 참조만
};
