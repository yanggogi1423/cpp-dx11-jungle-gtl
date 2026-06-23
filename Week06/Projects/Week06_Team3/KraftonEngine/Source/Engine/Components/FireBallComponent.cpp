#include "FireBallComponent.h"

#include "GameFramework/AActor.h"
#include "Object/ObjectFactory.h"
#include "Render/Proxy/FScene.h"
#include "Serialization/Archive.h"
#include "GameFramework/World.h"

#include <cstring>

IMPLEMENT_CLASS(UFireBallComponent, UPrimitiveComponent)

UFireBallComponent::UFireBallComponent()
{
	SyncLocalExtents();
}

void UFireBallComponent::CreateRenderState()
{
	// 이 컴포넌트는 primitive proxy를 만들지 않고 씬 효과로만 등록합니다. 따라서 프록시 생성 없이 바로 씬에 등록합니다.
	RegisterToScene();
}

void UFireBallComponent::DestroyRenderState()
{
	UnregisterFromScene();
	UPrimitiveComponent::DestroyRenderState();
}

void UFireBallComponent::UpdateWorldAABB() const
{
	const FVector Center = GetWorldLocation();
	const FVector Extent(Radius, Radius, Radius);
	WorldAABBMinLocation = Center - Extent;
	WorldAABBMaxLocation = Center + Extent;
	bWorldAABBDirty = false;
	bHasValidWorldAABB = (Radius > 0.0f);
}

void UFireBallComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);
	Ar << Intensity;
	Ar << Radius;
	Ar << RadiusFallOff;
	Ar << Color.R;
	Ar << Color.G;
	Ar << Color.B;
	Ar << Color.A;

	if (Ar.IsLoading())
	{
		SyncLocalExtents();
	}
}

void UFireBallComponent::SetIntensity(float InIntensity)
{
	Intensity = (InIntensity < 0.0f) ? 0.0f : InIntensity;
}

void UFireBallComponent::SetRadius(float InRadius)
{
	Radius = (InRadius < 0.0f) ? 0.0f : InRadius;
	SyncLocalExtents();
	MarkWorldBoundsDirty();
}

void UFireBallComponent::SetRadiusFallOff(float InRadiusFallOff)
{
	RadiusFallOff = (InRadiusFallOff < 0.01f) ? 0.01f : InRadiusFallOff;
}

void UFireBallComponent::SetColor(const FLinearColor& InColor)
{
	Color = InColor;
}

void UFireBallComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Intensity", EPropertyType::Float, &Intensity, 0.0f, 16.0f, 0.05f });
	OutProps.push_back({ "Radius", EPropertyType::Float, &Radius, 0.0f, 100.0f, 0.1f });
	OutProps.push_back({ "Radius FallOff", EPropertyType::Float, &RadiusFallOff, 0.01f, 16.0f, 0.05f });
	OutProps.push_back({ "Color", EPropertyType::Vec4, &Color, 0.0f, 1.0f, 0.01f });
}

void UFireBallComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "Intensity") == 0 && Intensity < 0.0f)
	{
		SetIntensity(Intensity);
	}
	else if (std::strcmp(PropertyName, "Radius") == 0 && Radius < 0.0f)
	{
		SetRadius(Radius);
	}
	else if (std::strcmp(PropertyName, "Radius FallOff") == 0)
	{
		SetRadiusFallOff(RadiusFallOff);
	}

	if (std::strcmp(PropertyName, "Radius") == 0)
	{
		SetRadius(Radius);
	}
}

bool UFireBallComponent::IsSceneEffectActive() const
{
	if (!IsActive() || !IsVisible() || Radius <= 0.0f || Intensity <= 0.0f)
	{
		return false;
	}

	AActor* OwnerActor = GetOwner();
	return OwnerActor && OwnerActor->IsVisible();
}

void UFireBallComponent::WriteSceneEffectConstants(FSceneEffectConstants& OutConstants, uint32 SlotIndex) const
{
	if (SlotIndex >= ECBSlot::MaxLocalTintEffects)
	{
		return;
	}

	FLocalTintEffectConstants& Entry = OutConstants.LocalTints[SlotIndex];
	Entry.PositionRadius = FVector4(GetWorldLocation(), Radius);
	Entry.Color = Color.ToVector4();
	Entry.Params = FVector4(Intensity, RadiusFallOff, 0.0f, 0.0f);
}

void UFireBallComponent::RegisterToScene()
{
	if (Owner)
	{
		if (UWorld* World = Owner->GetWorld())
		{
			World->GetScene().RegisterSceneEffectSource(this);
		}
	}
}

void UFireBallComponent::UnregisterFromScene()
{
	if (Owner)
	{
		if (UWorld* World = Owner->GetWorld())
		{
			World->GetScene().UnregisterSceneEffectSource(this);
		}
	}
}

void UFireBallComponent::SyncLocalExtents()
{
	LocalExtents = FVector(Radius, Radius, Radius);
}
