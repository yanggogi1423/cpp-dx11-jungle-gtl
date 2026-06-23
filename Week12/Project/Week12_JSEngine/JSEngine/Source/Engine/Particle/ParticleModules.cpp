#include "Particle/ParticleModules.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Core/Random/EngineRandom.h"
#include "Core/ResourceManager.h"
#include "Core/ResourceTypes.h"
#include "GameFramework/World.h"
#include "Math/Quat.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Particle/ParticleSystemComponent.h"
#include "Particle/ParticleUpdateUtils.h"
#include "Render/Scene/RenderCommand.h"

namespace ParticleModuleUtils
{
    constexpr const char* DefaultRequiredSubUVName = "Asset/plasma.png";

    float GetEmitterSpawnDistributionTime(FParticleEmitterInstance* Owner, float SpawnTime)
    {
        const float ClampedSpawnOffset = std::max(SpawnTime, 0.0f);
        return Owner ? std::max(Owner->GetPreviousEmitterTime() + ClampedSpawnOffset, 0.0f) : ClampedSpawnOffset;
    }

// Function : Generate random float inside range
// input : Min, Max
// Min : minimum random value
// Max : maximum random value
// output : Random float between Min and Max
    float RandomRange(float Min, float Max)
    {
        return FEngineRandom::Get().RandomFloat(Min, Max);
    }

// Function : Generate random vector inside per-axis range
// input : Min, Max
// Min : minimum vector value per axis
// Max : maximum vector value per axis
// output : Random vector with each axis sampled between Min and Max
    FVector RandomRangeVector(const FVector& Min, const FVector& Max)
    {
        return FVector(
            RandomRange(Min.X, Max.X),
            RandomRange(Min.Y, Max.Y),
            RandomRange(Min.Z, Max.Z));
    }

    FVector RandomUnitVector()
    {
        FVector Direction;
        do
        {
            Direction = FVector(
                RandomRange(-1.0f, 1.0f),
                RandomRange(-1.0f, 1.0f),
                RandomRange(-1.0f, 1.0f));
        }
        while (Direction.SizeSquared() <= 1.0e-6f || Direction.SizeSquared() > 1.0f);

        return Direction.GetSafeNormal();
    }

    FVector RandomPointInSphere(float Radius, bool bSurfaceOnly)
    {
        const float SafeRadius = std::max(Radius, 0.0f);
        if (SafeRadius <= 0.0f)
        {
            return FVector::ZeroVector;
        }

        const float DistanceScale = bSurfaceOnly ? 1.0f : std::cbrt(RandomRange(0.0f, 1.0f));
        return RandomUnitVector() * (SafeRadius * DistanceScale);
    }

    FVector RandomPointInBox(const FVector& Extents, bool bSurfaceOnly)
    {
        const FVector SafeExtents(
            std::max(Extents.X, 0.0f),
            std::max(Extents.Y, 0.0f),
            std::max(Extents.Z, 0.0f));

        FVector Result(
            RandomRange(-SafeExtents.X, SafeExtents.X),
            RandomRange(-SafeExtents.Y, SafeExtents.Y),
            RandomRange(-SafeExtents.Z, SafeExtents.Z));

        if (bSurfaceOnly)
        {
            const int32 FaceIndex = std::clamp(static_cast<int32>(RandomRange(0.0f, 6.0f)), 0, 5);
            const int32 Axis = FaceIndex / 2;
            const float Sign = (FaceIndex % 2) == 0 ? -1.0f : 1.0f;
            Result[Axis] = SafeExtents[Axis] * Sign;
        }

        return Result;
    }

    FVector RandomPointInCone(float Height, float HalfAngleDegrees, bool bSurfaceOnly)
    {
        const float SafeHeight = std::max(Height, 0.0f);
        if (SafeHeight <= 0.0f)
        {
            return FVector::ZeroVector;
        }

        constexpr float DegreesToRadians = 3.14159265358979323846f / 180.0f;
        const float SafeHalfAngle = std::clamp(HalfAngleDegrees, 0.0f, 89.0f) * DegreesToRadians;
        const float ConeRadiusAtEnd = SafeHeight * std::tan(SafeHalfAngle);
        const float HeightScale = bSurfaceOnly ? std::sqrt(RandomRange(0.0f, 1.0f)) : std::cbrt(RandomRange(0.0f, 1.0f));
        const float X = SafeHeight * HeightScale;
        const float RadiusAtX = ConeRadiusAtEnd * HeightScale;
        const float RadialDistance = bSurfaceOnly ? RadiusAtX : RadiusAtX * std::sqrt(RandomRange(0.0f, 1.0f));
        const float Angle = RandomRange(0.0f, 2.0f * 3.14159265358979323846f);

        return FVector(
            X,
            std::cos(Angle) * RadialDistance,
            std::sin(Angle) * RadialDistance);
    }

    FVector RandomPointInShape(EProceduralParticleShape Shape, float SphereRadius, const FVector& BoxExtents, float ConeHeight, float ConeHalfAngle, bool bSurfaceOnly)
    {
        switch (Shape)
        {
        case EProceduralParticleShape::Box:
            return RandomPointInBox(BoxExtents, bSurfaceOnly);
        case EProceduralParticleShape::Cone:
            return RandomPointInCone(ConeHeight, ConeHalfAngle, bSurfaceOnly);
        case EProceduralParticleShape::Sphere:
        default:
            return RandomPointInSphere(SphereRadius, bSurfaceOnly);
        }
    }

    uint32 HashParticleIdToFrameOffset(uint32 ParticleId, uint32 FrameCount)
    {
        if (FrameCount == 0)
        {
            return 0;
        }

        return (ParticleId * 2654435761u) % FrameCount;
    }
}


UParticleModuleRequired::UParticleModuleRequired()
{
    bSpawnModule = true;
}

// Function : Apply required default particle values at spawn time
// input : Owner, Particle, SpawnTime
// Owner : emitter instance that owns the particle
// Particle : particle being initialized
// SpawnTime : relative spawn time within this tick
// output : Particle time, lifetime, size, and color receive required defaults
void UParticleModuleRequired::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    (void)Owner;
    (void)SpawnTime;
    Particle.RelativeTime = 0.0f;
    Particle.Lifetime = std::max(Particle.Lifetime, 0.01f);
    Particle.Size = FVector(1.0f, 1.0f, 1.0f);
    Particle.Color = FColor::White();
}

void UParticleModuleRequired::PostEditProperty(const char* PropertyName)
{
    UParticleModule::PostEditProperty(PropertyName);
    (void)PropertyName;
}

UParticleModuleSpawn::UParticleModuleSpawn()
{
    bSpawnModule = false;
}

// Function : Compute number of particles to spawn for this tick
// input : Owner, DeltaTime
// Owner : emitter instance that stores fractional spawn remainder
// DeltaTime : elapsed time for this simulation step
// output : Integer spawn count and updated Owner SpawnFraction remainder
int32 UParticleModuleSpawn::ComputeSpawnCount(FParticleEmitterInstance* Owner, float DeltaTime)
{
    if (!Owner || DeltaTime <= 0.0f)
    {
        return 0;
    }

    const float EvaluatedRate = EvaluateFloatDistribution("Rate", Rate, Rate, Owner->GetEmitterTime());
    if (EvaluatedRate <= 0.0f)
    {
        return 0;
    }

	return Owner->ConsumeSpawnCount(EvaluatedRate, DeltaTime);
}

UParticleModuleLifetime::UParticleModuleLifetime()
{
    bSpawnModule = true;
}

// Function : Assign randomized lifetime to spawned particle
// input : Owner, Particle, SpawnTime
// Owner : emitter instance that owns the particle
// Particle : particle receiving lifetime value
// SpawnTime : relative spawn time within this tick
// output : Particle lifetime is set between LifetimeMin and LifetimeMax
void UParticleModuleLifetime::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    const float DistributionTime = ParticleModuleUtils::GetEmitterSpawnDistributionTime(Owner, SpawnTime);
    Particle.Lifetime = std::max(EvaluateFloatDistribution("LifetimeMin", LifetimeMin, LifetimeMax, DistributionTime), 0.01f);
}

UParticleModuleLocation::UParticleModuleLocation()
{
    bSpawnModule = true;
}

// Function : Assign initial world location to spawned particle
// input : Owner, Particle, SpawnTime
// Owner : emitter instance used to read component world location
// Particle : particle receiving initial location
// SpawnTime : relative spawn time within this tick
// output : Particle Location and OldLocation are set from component location plus random local offset
void UParticleModuleLocation::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    const float DistributionTime = ParticleModuleUtils::GetEmitterSpawnDistributionTime(Owner, SpawnTime);
    const FVector LocalOffset = EvaluateVectorDistribution("StartLocationMin", StartLocationMin, StartLocationMax, DistributionTime);
    const FVector BaseLocation = (Owner && !Owner->UsesLocalSpace()) ? Owner->GetComponentWorldLocation() : FVector::ZeroVector;
    Particle.Location = BaseLocation + LocalOffset;
    Particle.OldLocation = Particle.Location;
}

namespace ParticleSizeModuleUtils
{
    float SmoothStep01(float Value)
    {
        const float T = std::clamp(Value, 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }

    float Lerp(float A, float B, float T)
    {
        return A + (B - A) * T;
    }

    bool IsStretchTarget(const FVector& TargetSize)
    {
        const float CrossSection = std::max(TargetSize.Y, TargetSize.Z);
        return TargetSize.X > 1.0f && TargetSize.X > CrossSection * 8.0f;
    }

    FVector EvaluateFallbackStretchSize(const FVector& TargetSize, float RelativeTime, float EmitterTime, uint32 ParticleId)
    {
        if (!IsStretchTarget(TargetSize))
        {
            return TargetSize;
        }

        const float CrossSection = std::max(std::max(TargetSize.Y, TargetSize.Z), 0.01f);
        const float StretchAlpha = SmoothStep01((RelativeTime - 0.30f) / 0.48f);
        const float Pulse = 1.0f + std::sin(EmitterTime * 5.0f + static_cast<float>(ParticleId) * 0.37f) * 0.045f * StretchAlpha;

        return FVector(
            Lerp(CrossSection, TargetSize.X, StretchAlpha) * Pulse,
            Lerp(CrossSection, TargetSize.Y, StretchAlpha),
            Lerp(CrossSection, TargetSize.Z, StretchAlpha));
    }
}

UParticleModuleLocationShape::UParticleModuleLocationShape()
{
    bSpawnModule = true;
}

void UParticleModuleLocationShape::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    const float DistributionTime = ParticleModuleUtils::GetEmitterSpawnDistributionTime(Owner, SpawnTime);
    const float EvaluatedSphereRadius = EvaluateFloatDistribution("SphereRadius", SphereRadius, SphereRadius, DistributionTime);
    const FVector EvaluatedBoxExtents = EvaluateVectorDistribution("BoxExtents", BoxExtents, BoxExtents, DistributionTime);
    const float EvaluatedConeHeight = EvaluateFloatDistribution("ConeHeight", ConeHeight, ConeHeight, DistributionTime);
    const float EvaluatedConeHalfAngle = EvaluateFloatDistribution("ConeHalfAngle", ConeHalfAngle, ConeHalfAngle, DistributionTime);
    const FVector LocalOffset = ParticleModuleUtils::RandomPointInShape(
        Shape,
        EvaluatedSphereRadius,
        EvaluatedBoxExtents,
        EvaluatedConeHeight,
        EvaluatedConeHalfAngle,
        bSurfaceOnly);
    const FVector BaseLocation = (Owner && !Owner->UsesLocalSpace()) ? Owner->GetComponentWorldLocation() : FVector::ZeroVector;
    Particle.Location = BaseLocation + LocalOffset;
    Particle.OldLocation = Particle.Location;
}

UParticleModuleVelocity::UParticleModuleVelocity()
{
    bSpawnModule = true;
}

// Function : Assign randomized initial velocity to spawned particle
// input : Owner, Particle, SpawnTime
// Owner : emitter instance that owns the particle
// Particle : particle receiving initial velocity
// SpawnTime : relative spawn time within this tick
// output : Particle Velocity and BaseVelocity are set between StartVelocityMin and StartVelocityMax
void UParticleModuleVelocity::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    const float DistributionTime = ParticleModuleUtils::GetEmitterSpawnDistributionTime(Owner, SpawnTime);
    Particle.Velocity = EvaluateVectorDistribution("StartVelocityMin", StartVelocityMin, StartVelocityMax, DistributionTime);
    Particle.BaseVelocity = Particle.Velocity;
}

UParticleModuleBurst::UParticleModuleBurst()
{
    bUpdateModule = true;
}

void UParticleModuleBurst::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    (void)DeltaTime;
    if (!Owner || BurstCount <= 0)
    {
        return;
    }

    const float EvaluatedBurstTime = std::max(EvaluateFloatDistribution("BurstTime", BurstTime, BurstTime, 0.0f), 0.0f);
    const float EvaluatedRepeatInterval = std::max(EvaluateFloatDistribution("RepeatInterval", RepeatInterval, RepeatInterval, 0.0f), 0.001f);
    const float PreviousTime = Owner->GetPreviousEmitterTime();
    const float CurrentTime = Owner->GetEmitterTime();
    if (CurrentTime < EvaluatedBurstTime)
    {
        return;
    }

    int32 TriggerCount = 0;
    if (bRepeat && EvaluatedRepeatInterval > 0.0f)
    {
        const float ClampedPrevious = std::max(PreviousTime, EvaluatedBurstTime);
        const int32 PreviousTriggerIndex = PreviousTime < EvaluatedBurstTime
            ? -1
            : static_cast<int32>(std::floor((ClampedPrevious - EvaluatedBurstTime) / EvaluatedRepeatInterval));
        const int32 CurrentTriggerIndex = static_cast<int32>(std::floor((CurrentTime - EvaluatedBurstTime) / EvaluatedRepeatInterval));
        TriggerCount = std::max(CurrentTriggerIndex - PreviousTriggerIndex, 0);
    }
    else if (PreviousTime < EvaluatedBurstTime && CurrentTime >= EvaluatedBurstTime)
    {
        TriggerCount = 1;
    }

    if (TriggerCount <= 0)
    {
        return;
    }

    Owner->SpawnParticles(
        BurstCount * TriggerCount,
        0.0f,
        0.0f,
        Owner->UsesLocalSpace() ? FVector::ZeroVector : Owner->GetComponentWorldLocation(),
        FVector::ZeroVector);
}

UParticleModuleAcceleration::UParticleModuleAcceleration()
{
    bUpdateModule = true;
}

void UParticleModuleAcceleration::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    if (!Owner || DeltaTime <= 0.0f)
    {
        return;
    }

    BEGIN_UPDATE_LOOP
    {
        DECLARE_PARTICLE_PTR;
        const FVector EvaluatedAcceleration = EvaluateVectorDistribution(
            "Acceleration",
            Acceleration,
            Acceleration,
            std::clamp(Particle.RelativeTime, 0.0f, 1.0f));
        Particle.Velocity += EvaluatedAcceleration * DeltaTime;
        END_UPDATE_LOOP;
    }
}

UParticleModuleDrag::UParticleModuleDrag()
{
    bUpdateModule = true;
}

void UParticleModuleDrag::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    if (!Owner || DeltaTime <= 0.0f)
    {
        return;
    }

    BEGIN_UPDATE_LOOP
    {
        DECLARE_PARTICLE_PTR;
        const float EvaluatedDrag = std::max(EvaluateFloatDistribution(
            "DragCoefficient",
            DragCoefficient,
            DragCoefficient,
            std::clamp(Particle.RelativeTime, 0.0f, 1.0f)), 0.0f);
        if (EvaluatedDrag > 0.0f)
        {
            const float Damping = std::exp(-EvaluatedDrag * DeltaTime);
            Particle.Velocity *= Damping;
        }
        END_UPDATE_LOOP;
    }
}

UParticleModuleRotationRate::UParticleModuleRotationRate()
{
    bSpawnModule = true;
    bUpdateModule = true;
}

void UParticleModuleRotationRate::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    const float DistributionTime = ParticleModuleUtils::GetEmitterSpawnDistributionTime(Owner, SpawnTime);
    Particle.RotationRate = EvaluateFloatDistribution(
        "StartRotationRateMin",
        StartRotationRateMin,
        StartRotationRateMax,
        DistributionTime);
}

void UParticleModuleRotationRate::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    if (!Owner || DeltaTime <= 0.0f)
    {
        return;
    }

    BEGIN_UPDATE_LOOP
    {
        DECLARE_PARTICLE_PTR;
        Particle.Rotation += Particle.RotationRate * DeltaTime;
        END_UPDATE_LOOP;
    }
}

UParticleModuleColor::UParticleModuleColor()
{
    bSpawnModule = true;
    bUpdateModule = true;

    FParticleDistributionRuntimeData ColorDistribution;
    ColorDistribution.Kind = static_cast<int32>(EParticleDistributionRuntimeKind::FloatConstantCurve);
    ColorDistribution.bVector = true;
    FFloatCurve& RedCurve = ColorDistribution.Curves["X"];
    RedCurve.Keys.push_back({ 0.0f, 255.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    RedCurve.Keys.push_back({ 1.0f, 255.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    FFloatCurve& GreenCurve = ColorDistribution.Curves["Y"];
    GreenCurve.Keys.push_back({ 0.0f, 255.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    GreenCurve.Keys.push_back({ 1.0f, 255.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    FFloatCurve& BlueCurve = ColorDistribution.Curves["Z"];
    BlueCurve.Keys.push_back({ 0.0f, 255.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    BlueCurve.Keys.push_back({ 1.0f, 255.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    SetDistributionRuntimeData("ColorOverLife", ColorDistribution);

    FParticleDistributionRuntimeData AlphaDistribution;
    AlphaDistribution.Kind = static_cast<int32>(EParticleDistributionRuntimeKind::FloatConstantCurve);
    AlphaDistribution.bVector = false;
    FFloatCurve& AlphaCurve = AlphaDistribution.Curves["Value"];
    AlphaCurve.Keys.push_back({ 0.0f, 255.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    AlphaCurve.Keys.push_back({ 1.0f, 0.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    SetDistributionRuntimeData("AlphaOverLife", AlphaDistribution);
}

// Function : Assign initial color to spawned particle
// input : Owner, Particle, SpawnTime
// Owner : emitter instance that owns the particle
// Particle : particle receiving initial color
// SpawnTime : relative spawn time within this tick
// output : Particle Color is set to StartColor
void UParticleModuleColor::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    (void)Owner;
    (void)SpawnTime;
    const FVector InitialColor = EvaluateVectorDistribution("ColorOverLife", ColorOverLife, ColorOverLife, 0.0f);
    const float InitialAlpha = EvaluateFloatDistribution("AlphaOverLife", AlphaOverLife, AlphaOverLife, 0.0f);
    Particle.Color = FColor(
        std::clamp(InitialColor.X, 0.0f, 255.0f) / 255.0f,
        std::clamp(InitialColor.Y, 0.0f, 255.0f) / 255.0f,
        std::clamp(InitialColor.Z, 0.0f, 255.0f) / 255.0f,
        std::clamp(InitialAlpha, 0.0f, 255.0f) / 255.0f);
}

// Function : Interpolate active particle color over normalized lifetime
// input : Owner, DeltaTime
// Owner : emitter instance that owns active particles
// DeltaTime : elapsed time for this simulation step
// output : Each active particle Color is lerped from StartColor to EndColor
void UParticleModuleColor::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    (void)DeltaTime;
    if (!Owner)
    {
        return;
    }

    BEGIN_UPDATE_LOOP
    {
        DECLARE_PARTICLE_PTR;
        const float LifeTime = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
        const FVector LifeColor = EvaluateVectorDistribution("ColorOverLife", ColorOverLife, ColorOverLife, LifeTime);
        const float LifeAlpha = EvaluateFloatDistribution("AlphaOverLife", AlphaOverLife, AlphaOverLife, LifeTime);
        Particle.Color = FColor(
            std::clamp(LifeColor.X, 0.0f, 255.0f) / 255.0f,
            std::clamp(LifeColor.Y, 0.0f, 255.0f) / 255.0f,
            std::clamp(LifeColor.Z, 0.0f, 255.0f) / 255.0f,
            std::clamp(LifeAlpha, 0.0f, 255.0f) / 255.0f);
        END_UPDATE_LOOP;
    }
}

UParticleModuleLight::UParticleModuleLight()
{
}

bool UParticleModuleLight::ShouldCreateLight(const FBaseParticle& Particle, int32 CurrentLightCount) const
{
    if (!bLightEnabled || MaxLightsPerEmitter <= 0 || CurrentLightCount >= MaxLightsPerEmitter)
    {
        return false;
    }

    const float LifeTime = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
    const float ClampedFraction = std::clamp(EvaluateFloatDistribution("SpawnFraction", SpawnFraction, SpawnFraction, LifeTime), 0.0f, 1.0f);
    if (ClampedFraction >= 1.0f)
    {
        return true;
    }
    if (ClampedFraction <= 0.0f)
    {
        return false;
    }

    const uint32 Hash = Particle.ParticleId * 2654435761u;
    const float Unit = static_cast<float>(Hash & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    return Unit < ClampedFraction;
}

void UParticleModuleLight::BuildLightInfo(const FBaseParticle& Particle, FLightInfo& OutLightInfo) const
{
    OutLightInfo = {};
    const FColor SourceColor = bUseParticleColor ? Particle.Color : LightColor;
    const float AlphaScale = bUseParticleAlpha ? std::clamp(Particle.Color.A, 0.0f, 1.0f) : 1.0f;
    const float LifeTime = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
    const float EvaluatedBrightness = EvaluateFloatDistribution("Brightness", Brightness, Brightness, LifeTime);
    const float EvaluatedRadius = EvaluateFloatDistribution("Radius", Radius, Radius, LifeTime);
    const float EvaluatedRadiusScale = EvaluateFloatDistribution("RadiusScale", RadiusScale, RadiusScale, LifeTime);
    const float EvaluatedFalloff = EvaluateFloatDistribution("Falloff", Falloff, Falloff, LifeTime);
    const float ParticleRadiusScale = std::max(
        std::max(std::abs(Particle.Size.X), std::abs(Particle.Size.Y)),
        std::abs(Particle.Size.Z));

    OutLightInfo.Color = FVector(SourceColor.R, SourceColor.G, SourceColor.B);
    OutLightInfo.Intensity = std::max(EvaluatedBrightness * AlphaScale, 0.0f);
    OutLightInfo.Type = 1;
    OutLightInfo.Radius = std::max(EvaluatedRadius + EvaluatedRadiusScale * ParticleRadiusScale, 0.0f);
    OutLightInfo.InnerAngle = 0.0f;
    OutLightInfo.OuterAngle = 0.0f;
    OutLightInfo.Direction = FVector::ZeroVector;
    OutLightInfo.Falloff = std::max(EvaluatedFalloff, 0.0f);
    OutLightInfo.Position = Particle.Location;
    OutLightInfo.ShadowTextureIndex = InvalidShadowIndex;
}

UParticleModuleSize::UParticleModuleSize()
{
    bSpawnModule = true;
    bUpdateModule = true;

    FParticleDistributionRuntimeData SizeDistribution;
    SizeDistribution.Kind = static_cast<int32>(EParticleDistributionRuntimeKind::FloatConstantCurve);
    SizeDistribution.bVector = true;
    FFloatCurve& XCurve = SizeDistribution.Curves["X"];
    XCurve.Keys.push_back({ 0.0f, 1.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    XCurve.Keys.push_back({ 1.0f, 1.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    FFloatCurve& YCurve = SizeDistribution.Curves["Y"];
    YCurve.Keys.push_back({ 0.0f, 1.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    YCurve.Keys.push_back({ 1.0f, 1.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    FFloatCurve& ZCurve = SizeDistribution.Curves["Z"];
    ZCurve.Keys.push_back({ 0.0f, 1.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    ZCurve.Keys.push_back({ 1.0f, 1.0f, ECurveInterpMode::Cubic, ECurveTangentMode::Auto, 0.0f, 0.0f });
    SetDistributionRuntimeData("SizeOverLife", SizeDistribution);
}

// Function : Assign initial size to spawned particle
// input : Owner, Particle, SpawnTime
// Owner : emitter instance that owns the particle
// Particle : particle receiving initial size
// SpawnTime : relative spawn time within this tick
// output : Particle Size is initialized from SizeOverLife at lifetime start
void UParticleModuleSize::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    (void)Owner;
    (void)SpawnTime;
    if (FindDistributionRuntimeData("SizeOverLife"))
    {
        Particle.Size = EvaluateVectorDistribution("SizeOverLife", SizeOverLife, SizeOverLife, 0.0f);
        return;
    }

    const float EmitterTime = Owner ? Owner->GetEmitterTime() : 0.0f;
    Particle.Size = ParticleSizeModuleUtils::EvaluateFallbackStretchSize(SizeOverLife, 0.0f, EmitterTime, Particle.ParticleId);
}

// Function : Interpolate active particle size over normalized lifetime
// input : Owner, DeltaTime
// Owner : emitter instance that owns active particles
// DeltaTime : elapsed time for this simulation step
// output : Each active particle Size is evaluated from SizeOverLife over normalized lifetime
void UParticleModuleSize::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    (void)DeltaTime;
    if (!Owner)
    {
        return;
    }

    BEGIN_UPDATE_LOOP
    {
        DECLARE_PARTICLE_PTR;
        const float RelativeTime = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
        if (FindDistributionRuntimeData("SizeOverLife"))
        {
            Particle.Size = EvaluateVectorDistribution("SizeOverLife", SizeOverLife, SizeOverLife, RelativeTime);
        }
        else
        {
            Particle.Size = ParticleSizeModuleUtils::EvaluateFallbackStretchSize(
                SizeOverLife,
                RelativeTime,
                Owner->GetEmitterTime(),
                Particle.ParticleId);
        }
        END_UPDATE_LOOP;
    }
}

UParticleModuleCollision::UParticleModuleCollision()
{
    bUpdateModule = true;
}

// Function : Resolve world collision for active particles
// input : Owner, DeltaTime
// Owner : emitter instance that owns active particles and component event queue
// DeltaTime : elapsed time for this simulation step
// output : Colliding particles apply response, collision count updates, and optional collision events are queued
void UParticleModuleCollision::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    (void)DeltaTime;
    if (!bCollisionEnabled || !Owner)
    {
        return;
    }

    if (Owner->UsesLocalSpace())
    {
        return;
    }

    UParticleSystemComponent* Component = Owner->GetOwningComponent();
    AActor* OwnerActor = Component ? Component->GetOwner() : nullptr;
    UWorld* World = OwnerActor ? OwnerActor->GetFocusedWorld() : nullptr;
    if (!Component || !World)
    {
        return;
    }

    const float EvaluatedMaxCollisionDistance = std::max(EvaluateFloatDistribution("MaxCollisionDistance", MaxCollisionDistance, MaxCollisionDistance, 0.0f), 0.0f);
    if (EvaluatedMaxCollisionDistance > 0.0f && Component->ComputeEmitterLODDistance() > EvaluatedMaxCollisionDistance)
    {
        return;
    }

    BEGIN_UPDATE_LOOP
    {
        DECLARE_PARTICLE_PTR;
        const float LifeTime = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
        const float ClampedCheckFraction = std::clamp(EvaluateFloatDistribution("CollisionCheckFraction", CollisionCheckFraction, CollisionCheckFraction, LifeTime), 0.0f, 1.0f);
        const float ClampedRestitution = std::clamp(EvaluateFloatDistribution("Restitution", Restitution, Restitution, LifeTime), 0.0f, 1.0f);
        const float ClampedFriction = std::clamp(EvaluateFloatDistribution("Friction", Friction, Friction, LifeTime), 0.0f, 1.0f);

        if (ClampedCheckFraction <= 0.0f)
        {
            END_UPDATE_LOOP;
            continue;
        }
        if (ClampedCheckFraction < 1.0f)
        {
            constexpr uint32 BucketCount = 100;
            const uint32 Threshold = static_cast<uint32>(ClampedCheckFraction * static_cast<float>(BucketCount));
            if ((Particle.ParticleId % BucketCount) >= Threshold)
            {
                END_UPDATE_LOOP;
                continue;
            }
        }

        if (MaxCollisions > 0 && Particle.CollisionCount >= MaxCollisions)
        {
            if (bKillWhenMaxCollisionsReached)
            {
                Owner->KillParticle(ParticleIndex);
                continue;
            }
            END_UPDATE_LOOP;
            continue;
        }

        FHitResult Hit;
        FCollisionQueryParams QueryParams;
        QueryParams.IgnoredActor = bIgnoreOwner ? OwnerActor : nullptr;
        QueryParams.IgnoredComponent = Component;
        QueryParams.bSimpleCollisionOnly = true;

        bool bHit = false;
        if (TraceMode == EParticleCollisionTraceMode::Sphere)
        {
            float SweepRadius = std::max(0.0f, EvaluateFloatDistribution("CollisionRadius", CollisionRadius, CollisionRadius, LifeTime));
            if (bUseParticleSizeAsRadius)
            {
                SweepRadius = std::max({
                    std::fabs(Particle.Size.X),
                    std::fabs(Particle.Size.Y),
                    std::fabs(Particle.Size.Z) }) * 0.5f;
            }

            if (SweepRadius > 0.0f)
            {
                bHit = World->SweepSingle(
                    Hit,
                    Particle.OldLocation,
                    Particle.Location,
                    FQuat::Identity,
                    FCollisionShape::MakeSphere(SweepRadius),
                    QueryParams);
            }
            else
            {
                bHit = World->LineTraceSingle(Particle.OldLocation, Particle.Location, Hit, QueryParams);
            }
        }
        else
        {
            bHit = World->LineTraceSingle(Particle.OldLocation, Particle.Location, Hit, QueryParams);
        }

        if (!bHit)
        {
            END_UPDATE_LOOP;
            continue;
        }

        ++Particle.CollisionCount;

        EParticleCollisionResponse AppliedResponse = Response;
        if (MaxCollisions > 0 && Particle.CollisionCount >= MaxCollisions && bKillWhenMaxCollisionsReached)
        {
            AppliedResponse = EParticleCollisionResponse::Kill;
        }

        FVector HitNormal = Hit.Normal.GetSafeNormal().IsNearlyZero()
            ? FVector::UpVector
            : Hit.Normal.GetSafeNormal();

        constexpr float ParticleCollisionSkin = 0.01f;

        switch (AppliedResponse)
        {
        case EParticleCollisionResponse::Bounce:
        {
            const FVector Velocity = Particle.Velocity;
            const float NormalSpeed = FVector::DotProduct(Velocity, HitNormal);
            const FVector NormalVelocity = HitNormal * NormalSpeed;
            const FVector TangentVelocity = Velocity - NormalVelocity;

            if (NormalSpeed < 0.0f)
            {
                Particle.Velocity = TangentVelocity * (1.0f - ClampedFriction) - NormalVelocity * ClampedRestitution;
            }
            Particle.Location = Hit.Location + HitNormal * ParticleCollisionSkin;
            break;
        }
        case EParticleCollisionResponse::Stop:
            Particle.Location = Hit.Location + HitNormal * ParticleCollisionSkin;
            Particle.Velocity = FVector::ZeroVector;
            break;
        case EParticleCollisionResponse::Ignore:
            break;
        case EParticleCollisionResponse::Kill:
            Particle.Location = Hit.Location;
            break;
        }

        if (bGenerateCollisionEvents)
        {
            FParticleEventCollideData Event;
            Event.Component = Component;
            Event.EmitterInstance = Owner;
            Event.EmitterIndex = Owner->GetEmitterIndex();
            Event.ParticleId = Particle.ParticleId;
            Event.Location = Particle.Location;
            Event.OldLocation = Particle.OldLocation;
            Event.Velocity = Particle.Velocity;
            Event.Normal = HitNormal;
            Event.HitComponent = Hit.HitComponent;
            Event.HitActor = Hit.HitComponent ? Hit.HitComponent->GetOwner() : nullptr;
            Event.Time = Particle.RelativeTime;
            Event.Hit = Hit;
            Owner->QueueCollisionEvent(Event);
        }

        if (AppliedResponse == EParticleCollisionResponse::Kill)
        {
            Owner->KillParticle(ParticleIndex);
            continue;
        }

        END_UPDATE_LOOP;
    }
}

UParticleModuleEventGenerator::UParticleModuleEventGenerator()
{
    bUpdateModule = true;
}

// Function : Dispatch particle events queued on owning component
// input : Owner, DeltaTime
// Owner : emitter instance used to access the owning component
// DeltaTime : elapsed time for this simulation step
// output : Queued particle events on the component are broadcast and cleared
void UParticleModuleEventGenerator::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    (void)DeltaTime;
    if (!Owner || !bDispatchCollisionEvents)
    {
        return;
    }

    UParticleSystemComponent* Component = Owner->GetOwningComponent();
    if (!Component)
    {
        return;
    }

    TArray<FParticleEventCollideData>& PendingEvents = Component->GetPendingCollisionEvents();
    if (MaxCollisionEventsPerFrame > 0 && static_cast<int32>(PendingEvents.size()) > MaxCollisionEventsPerFrame)
    {
        if (bKeepNewestCollisionEvents)
        {
            PendingEvents.erase(PendingEvents.begin(), PendingEvents.end() - MaxCollisionEventsPerFrame);
        }
        else
        {
            PendingEvents.resize(static_cast<size_t>(MaxCollisionEventsPerFrame));
        }
    }

    Owner->DispatchQueuedParticleEvents();
}

USubUVModule::USubUVModule()
{
    bSpawnModule = true;
    bUpdateModule = true;
    SetSubUVName(FName::None);
}

void USubUVModule::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    (void)Owner;
    (void)SpawnTime;
    const uint32 StartFrame = static_cast<uint32>(GetStartFrameIndex());
    const uint32 EndFrame = static_cast<uint32>(GetEndFrameIndex());
    const uint32 RangeStart = std::min(StartFrame, EndFrame);
    const uint32 RangeEnd = std::max(StartFrame, EndFrame);
    const uint32 RangeFrameCount = RangeEnd - RangeStart + 1;
    const uint32 RandomOffset = bRandomStartFrame
        ? ParticleModuleUtils::HashParticleIdToFrameOffset(Particle.ParticleId, RangeFrameCount)
        : 0;
    Particle.SubUVIndex = RangeStart + RandomOffset;
}

void USubUVModule::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    (void)DeltaTime;
    if (!Owner)
    {
        return;
    }

    uint32 TotalFrames = 0;
    if (CachedSubUV)
    {
        TotalFrames = CachedSubUV->Columns * CachedSubUV->Rows;
    }
    if (TotalFrames == 0)
    {
        const FCompiledParticleLODData* CompiledLOD = Owner->GetCurrentCompiledLODData();
        const UParticleModuleRequired* RequiredModule = CompiledLOD ? CompiledLOD->RequiredModule : nullptr;
        if (!RequiredModule)
        {
            const UParticleLODLevel* LODLevel = Owner->GetCurrentLODLevel();
            RequiredModule = LODLevel ? LODLevel->GetRequiredModule() : nullptr;
        }
        if (RequiredModule)
        {
            TotalFrames = static_cast<uint32>(
                std::max(RequiredModule->GetSubImagesHorizontal(), 1) *
                std::max(RequiredModule->GetSubImagesVertical(), 1));
        }
    }
    if (TotalFrames == 0)
    {
        return;
    }

    const uint32 LastFrame = TotalFrames - 1;
    const uint32 StartFrame = std::min(static_cast<uint32>(GetStartFrameIndex()), LastFrame);
    const uint32 EndFrame = (EndFrameIndex <= 0)
        ? LastFrame
        : std::min(static_cast<uint32>(GetEndFrameIndex()), LastFrame);
    const uint32 RangeStart = std::min(StartFrame, EndFrame);
    const uint32 RangeEnd = std::max(StartFrame, EndFrame);
    const uint32 RangeFrameCount = RangeEnd - RangeStart + 1;

    BEGIN_UPDATE_LOOP
    {
        DECLARE_PARTICLE_PTR;
        const uint32 RandomOffset = bRandomStartFrame
            ? ParticleModuleUtils::HashParticleIdToFrameOffset(Particle.ParticleId, RangeFrameCount)
            : 0;

        uint32 RangeFrameOffset = 0;
        if (PlaybackMode == EParticleSubUVPlaybackMode::FramesPerSecond)
        {
            const float ParticleAge = std::max(Particle.RelativeTime, 0.0f) * std::max(Particle.Lifetime, 0.0f);
            const float EvaluatedFrameRate = std::max(EvaluateFloatDistribution(
                "FrameRate",
                FrameRate,
                FrameRate,
                std::clamp(Particle.RelativeTime, 0.0f, 1.0f)), 0.0f);
            const uint32 AdvancedFrames = static_cast<uint32>(EvaluatedFrameRate * ParticleAge);
            const uint32 FrameWithOffset = AdvancedFrames + RandomOffset;
            RangeFrameOffset = bLoop
                ? (FrameWithOffset % RangeFrameCount)
                : std::min(FrameWithOffset, RangeFrameCount - 1);
        }
        else
        {
            const float Clamped = bLoop
                ? std::fmod(std::max(Particle.RelativeTime, 0.0f), 1.0f)
                : std::clamp(Particle.RelativeTime, 0.0f, 0.9999f);
            const uint32 AdvancedFrames = static_cast<uint32>(Clamped * static_cast<float>(RangeFrameCount));
            const uint32 FrameWithOffset = AdvancedFrames + RandomOffset;
            RangeFrameOffset = bLoop
                ? (FrameWithOffset % RangeFrameCount)
                : std::min(FrameWithOffset, RangeFrameCount - 1);
        }

        Particle.SubUVIndex = RangeStart + RangeFrameOffset;
        END_UPDATE_LOOP;
    }
}

void USubUVModule::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    if (Ar.IsLoading())
    {
        SetSubUVName(SubUVName);
    }
}

void USubUVModule::PostEditProperty(const char* PropertyName)
{
    UParticleModule::PostEditProperty(PropertyName);
    if (PropertyName && strcmp(PropertyName, "SubUVName") == 0)
    {
        SetSubUVName(SubUVName);
    }
}

void USubUVModule::SetSubUVName(const FName& InName)
{
    SubUVName = InName;
    CachedSubUV = FResourceManager::Get().FindSubUVExact(InName);
    if (CachedSubUV && EndFrameIndex <= 0)
    {
        const uint32 TotalFrames = CachedSubUV->Columns * CachedSubUV->Rows;
        if (TotalFrames > 0)
        {
            EndFrameIndex = static_cast<int32>(TotalFrames - 1);
        }
    }
}
