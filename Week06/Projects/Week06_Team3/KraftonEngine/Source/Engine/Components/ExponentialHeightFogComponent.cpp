#include "ExponentialHeightFogComponent.h"

#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Render/Proxy/FScene.h"
#include "Serialization/Archive.h"

#include <cstring>

namespace
{
	bool IsFogPropertyName(const char* PropertyName)
	{
		return std::strcmp(PropertyName, "Fog Density") == 0 ||
			std::strcmp(PropertyName, "Fog Height Falloff") == 0 ||
			std::strcmp(PropertyName, "Fog Height") == 0 ||
			std::strcmp(PropertyName, "Fog Color") == 0 ||
			std::strcmp(PropertyName, "Start Distance") == 0 ||
			std::strcmp(PropertyName, "Fog Cutoff Distance") == 0 ||
			std::strcmp(PropertyName, "Fog Max Opacity") == 0;
	}
}

IMPLEMENT_CLASS(UExponentialHeightFogComponent, UPrimitiveComponent)

UExponentialHeightFogComponent::UExponentialHeightFogComponent()
{
	SanitizeProperties();
}

void UExponentialHeightFogComponent::CreateRenderState()
{
	RegisterToScene();
}

void UExponentialHeightFogComponent::DestroyRenderState()
{
	UnregisterFromScene();
	UPrimitiveComponent::DestroyRenderState();
}

void UExponentialHeightFogComponent::SanitizeProperties()
{
	FogDensity = Clamp(FogDensity, 0.0f, 100000.0f);
	FogHeightFalloff = Clamp(FogHeightFalloff, 0.0f, 100000.0f);
	FogHeight = Clamp(FogHeight, -100000.0f, 100000.0f);
	StartDistance = Clamp(StartDistance, 0.0f, 100000.0f);
	FogCutoffDistance = Clamp(FogCutoffDistance, 0.0f, 100000.0f);
	FogMaxOpacity = Clamp(FogMaxOpacity, 0.0f, 1.0f);
}

bool UExponentialHeightFogComponent::IsFogActive() const
{
	if (!IsActive() || !IsVisible() || FogDensity <= 0.0f)
	{
		return false;
	}

	AActor* OwnerActor = GetOwner();
	return OwnerActor && OwnerActor->IsVisible();
}

FFogUniformParameters UExponentialHeightFogComponent::BuildFogUniformParameters() const
{
	FFogUniformParameters Result = {};
	Result.ExponentialFogParameters = FVector4(FogDensity, FogHeightFalloff, 0.0f, StartDistance);
	Result.ExponentialFogColorParameter = FVector4(FogInscatteringColor.R, FogInscatteringColor.G, FogInscatteringColor.B, 1.0f - FogMaxOpacity);
	Result.ExponentialFogParameters3 = FVector4(0.0f, FogHeight, 0.0f, FogCutoffDistance);
	return Result;
}

void UExponentialHeightFogComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	Ar << FogDensity;
	Ar << FogHeightFalloff;
	Ar << FogHeight;
	Ar << StartDistance;
	Ar << FogCutoffDistance;
	Ar << FogMaxOpacity;

	Ar << FogInscatteringColor;

	if (Ar.IsLoading())
	{
		SanitizeProperties();
	}
}

void UExponentialHeightFogComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Fog Density", EPropertyType::Float, &FogDensity, 0.0f, 10.0f, 0.001f });
	OutProps.push_back({ "Fog Height Falloff", EPropertyType::Float, &FogHeightFalloff, 0.0f, 10.0f, 0.001f });
	OutProps.push_back({ "Fog Height", EPropertyType::Float, &FogHeight, -10000.0f, 10000.0f, 1.0f });
	OutProps.push_back({ "Fog Color", EPropertyType::Vec4, &FogInscatteringColor, 0.0f, 1.0f, 0.01f });
	OutProps.push_back({ "Start Distance", EPropertyType::Float, &StartDistance, 0.0f, 10000.0f, 1.0f });
	OutProps.push_back({ "Fog Cutoff Distance", EPropertyType::Float, &FogCutoffDistance, 0.0f, 100000.0f, 1.0f });
	OutProps.push_back({ "Fog Max Opacity", EPropertyType::Float, &FogMaxOpacity, 0.0f, 1.0f, 0.01f });
}

void UExponentialHeightFogComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (IsFogPropertyName(PropertyName))
	{
		SanitizeProperties();
	}
}

void UExponentialHeightFogComponent::RegisterToScene()
{
	if (Owner)
	{
		if (UWorld* World = Owner->GetWorld())
		{
			World->GetScene().RegisterFogComponent(this);
		}
	}
}

void UExponentialHeightFogComponent::UnregisterFromScene()
{
	if (Owner)
	{
		if (UWorld* World = Owner->GetWorld())
		{
			World->GetScene().UnregisterFogComponent(this);
		}
	}
}
