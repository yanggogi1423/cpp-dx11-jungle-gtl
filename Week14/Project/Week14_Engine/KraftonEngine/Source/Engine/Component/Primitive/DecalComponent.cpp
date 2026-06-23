#include "DecalComponent.h"
#include "Component/Primitive/BillboardComponent.h"
#include "Materials/MaterialManager.h"
#include "Collision/Math/OBB.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Profiling/Stats/Stats.h"
#include "Render/Scene/FScene.h"
#include "Render/Proxy/DecalSceneProxy.h"
#include "Resource/ResourceManager.h"
#include "Mesh/MeshManager.h"
#include "Engine/Runtime/Engine.h"
#include "Texture/Texture2D.h"
#include "Materials/Material.h"
#include "Object/GarbageCollection.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
	void AddDecalBoxLines(FScene& Scene, const FVector& Center, const FVector& Forward, const FVector& Right, const FVector& Up, const FVector& Extent, const FColor& Color)
	{
		FVector Corners[8];
		for (int32 i = 0; i < 8; ++i)
		{
			const FVector LocalOffset(
				(i & 1) ? Extent.X : -Extent.X,
				(i & 2) ? Extent.Y : -Extent.Y,
				(i & 4) ? Extent.Z : -Extent.Z);
			Corners[i] = Center + Forward * LocalOffset.X + Right * LocalOffset.Y + Up * LocalOffset.Z;
		}

		Scene.AddDebugLine(Corners[0], Corners[1], Color);
		Scene.AddDebugLine(Corners[1], Corners[3], Color);
		Scene.AddDebugLine(Corners[3], Corners[2], Color);
		Scene.AddDebugLine(Corners[2], Corners[0], Color);
		Scene.AddDebugLine(Corners[4], Corners[5], Color);
		Scene.AddDebugLine(Corners[5], Corners[7], Color);
		Scene.AddDebugLine(Corners[7], Corners[6], Color);
		Scene.AddDebugLine(Corners[6], Corners[4], Color);
		Scene.AddDebugLine(Corners[0], Corners[4], Color);
		Scene.AddDebugLine(Corners[1], Corners[5], Color);
		Scene.AddDebugLine(Corners[2], Corners[6], Color);
		Scene.AddDebugLine(Corners[3], Corners[7], Color);
	}

	void AddDecalFrontArrow(FScene& Scene, const FVector& Center, const FVector& Forward, const FVector& Right, const FVector& Up, float Length, const FColor& ShaftColor, const FColor& HeadColor)
	{
		const FVector Dir = Forward.Normalized();
		if (Dir.Length() <= 0.001f || Length <= 0.001f)
		{
			return;
		}

		const float HeadLength = Length * 0.22f;
		const float HeadRadius = Length * 0.08f;
		const FVector Start = Center;
		const FVector Tip = Center + Dir * Length;
		const FVector HeadBase = Tip - Dir * HeadLength;
		constexpr int32 RingSegments = 12;

		Scene.AddDebugLine(Start, Tip, ShaftColor);

		FVector Previous = HeadBase + Right.Normalized() * HeadRadius;
		for (int32 i = 1; i <= RingSegments; ++i)
		{
			const float Angle = (static_cast<float>(i) / static_cast<float>(RingSegments)) * 2.0f * 3.1415926535f;
			const FVector RingOffset = Right.Normalized() * std::cos(Angle) * HeadRadius + Up.Normalized() * std::sin(Angle) * HeadRadius;
			const FVector Point = HeadBase + RingOffset;
			Scene.AddDebugLine(Previous, Point, HeadColor);
			Scene.AddDebugLine(Tip, Point, HeadColor);
			Previous = Point;
		}
	}
}

UDecalComponent::UDecalComponent()
{
	bCastShadow = false;
	bCastShadowAsTwoSided = false;
}

void UDecalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	if (TickType == ELevelTick::LEVELTICK_All)
	{
		HandleFade(DeltaTime);
	}

	UpdateReceivers();
}

FPrimitiveSceneProxy* UDecalComponent::CreateSceneProxy()
{
	return new FDecalSceneProxy(this);
}

void UDecalComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "MaterialSlot") == 0 || strcmp(PropertyName, "Material") == 0)
	{
		if (MaterialSlot == "None" || MaterialSlot.empty())
		{
			SetMaterial(nullptr);
		}
		else
		{
			UMaterial* LoadedMat = FMaterialManager::Get().GetOrCreateMaterial(MaterialSlot);
			if (LoadedMat)
			{
				SetMaterial(LoadedMat);
			}
		}
		MarkRenderStateDirty();
	}
	if (strcmp(PropertyName, "Color") == 0)
	{
		MarkProxyDirty(EDirtyFlag::Material);
	}
	if (strcmp(PropertyName, "AtlasRect") == 0)
	{
		MarkProxyDirty(EDirtyFlag::Material);
	}
}

void UDecalComponent::PostDuplicate()
{
	UPrimitiveComponent::PostDuplicate();

	if (!MaterialSlot.empty() && MaterialSlot != "None")
	{
		UMaterial* LoadedMat = FMaterialManager::Get().GetOrCreateMaterial(MaterialSlot);
		if (LoadedMat)
		{
			SetMaterial(LoadedMat);
		}
	}
	MarkProxyDirty(EDirtyFlag::Material);
}

void UDecalComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	const FMatrix& World = GetWorldMatrix();
	const FVector Center = World.GetLocation();
	const FVector Scale = World.GetScale();
	const FVector Extent(
		std::abs(Scale.X) * 0.5f,
		std::abs(Scale.Y) * 0.5f,
		std::abs(Scale.Z) * 0.5f);

	FVector Forward = GetForwardVector().Normalized();
	FVector Right = GetRightVector().Normalized();
	FVector Up = GetUpVector().Normalized();

	const FColor BoxColor(0, 220, 255);
	const FColor FrontColor(255, 190, 40);
	const FColor HeadColor(255, 90, 40);
	AddDecalBoxLines(Scene, Center, Forward, Right, Up, Extent, BoxColor);
	AddDecalFrontArrow(Scene, Center, Forward, Right, Up, std::max(0.75f, Extent.X * 1.5f), FrontColor, HeadColor);
}

FVector4 UDecalComponent::GetColor() const
{
	FVector4 OutColor = Color;
	OutColor.A *= Clamp(FadeOpacity, 0, 1);
	return OutColor;
}

void UDecalComponent::SetAtlasRect(FVector4 InAtlasRect)
{
	AtlasRect = InAtlasRect;
	MarkProxyDirty(EDirtyFlag::Material);
}

void UDecalComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	UPrimitiveComponent::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(Material, "UDecalComponent::Material");
}

TArray<UStaticMeshComponent*> UDecalComponent::GetReceivers() const
{
	TArray<UStaticMeshComponent*> ValidReceivers;
	ValidReceivers.reserve(Receivers.size());
	for (const TWeakObjectPtr<UStaticMeshComponent>& Receiver : Receivers)
	{
		if (UStaticMeshComponent* Component = Receiver.Get())
		{
			ValidReceivers.push_back(Component);
		}
	}
	return ValidReceivers;
}

void UDecalComponent::SetMaterial(UMaterial* InMaterial)
{
	Material = InMaterial;
	if (Material)
	{
		MaterialSlot = Material->GetAssetPathFileName();
	}
	else
	{
		MaterialSlot = "None";
	}
	MarkProxyDirty(EDirtyFlag::Material);
}

void UDecalComponent::UpdateDecalVolumeFromTransform()
{
	ConvexVolume.UpdateAsOBB(GetWorldMatrix());
}

void UDecalComponent::OnTransformDirty()
{
	UPrimitiveComponent::OnTransformDirty();
	UpdateReceivers();
	MarkProxyDirty(EDirtyFlag::Material);
}

bool UDecalComponent::ShouldReceivePrimitive(UPrimitiveComponent* PrimitiveComp) const
{
	return PrimitiveComp && PrimitiveComp != this && PrimitiveComp->GetOwner() != GetOwner();
}

void UDecalComponent::HandleFade(float DeltaTime)
{
	if (FadeInDuration <= 0.0f && FadeOutDuration <= 0.0f)
	{
		if (FadeOpacity != 1.0f)
		{
			FadeOpacity = 1.0f;
			MarkProxyDirty(EDirtyFlag::Material);
		}
		return;
	}

	FadeTimer += DeltaTime;

	float Alpha = 1.0f;

	if (FadeInDuration > 0.0f)
	{
		const float InStart = FadeInDelay;
		const float InEnd = FadeInDelay + FadeInDuration;
		if (FadeTimer < InStart)
		{
			Alpha = 0.0f;
		}
		else if (FadeTimer < InEnd)
		{
			Alpha = (FadeTimer - InStart) / FadeInDuration;
		}
	}

	if (FadeOutDuration > 0.0f)
	{
		const float OutStart = FadeOutDelay;
		const float OutEnd = FadeOutDelay + FadeOutDuration;
		if (FadeTimer > OutEnd)
		{
			Alpha = 0.0f;
		}
		else if (FadeTimer > OutStart)
		{
			Alpha = std::min(Alpha, 1.0f - (FadeTimer - OutStart) / FadeOutDuration);
		}
	}

	if (std::abs(FadeOpacity - Alpha) > 0.0001f)
	{
		FadeOpacity = Alpha;
		MarkProxyDirty(EDirtyFlag::Material);
	}
}

void UDecalComponent::UpdateReceivers()
{
	SCOPE_STAT_CAT("UpdateDecalReceivers", "6_Decal");

	UpdateDecalVolumeFromTransform();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<UPrimitiveComponent*> OverlappingPrimitives;
	World->GetPartition().QueryFrustumAllPrimitive(ConvexVolume, OverlappingPrimitives);

	TArray<TWeakObjectPtr<UStaticMeshComponent>> NewReceivers;

	FOBB DecalOBB;
	DecalOBB.UpdateAsOBB(GetWorldMatrix());

	for (UPrimitiveComponent* PrimitiveComp : OverlappingPrimitives)
	{
		if (!ShouldReceivePrimitive(PrimitiveComp))
		{
			continue;
		}

		UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(PrimitiveComp);
		if (!StaticMeshComp || !StaticMeshComp->GetStaticMesh())
		{
			continue;
		}

		const FBoundingBox ReceiverBounds = StaticMeshComp->GetWorldBoundingBox();
		if (!ReceiverBounds.IsValid())
		{
			continue;
		}

		if (!DecalOBB.IntersectOBBAABB(ReceiverBounds))
		{
			continue;
		}

		NewReceivers.push_back(StaticMeshComp);
	}

	bool bReceiversChanged = NewReceivers.size() != Receivers.size();
	if (!bReceiversChanged)
	{
		for (size_t Index = 0; Index < NewReceivers.size(); ++Index)
		{
			if (NewReceivers[Index].Get() != Receivers[Index].Get())
			{
				bReceiversChanged = true;
				break;
			}
		}
	}

	if (bReceiversChanged)
	{
		Receivers = std::move(NewReceivers);
		MarkProxyDirty(EDirtyFlag::Mesh);
	}
}

UBillboardComponent* UDecalComponent::EnsureEditorBillboard()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	auto ConfigureBillboard = [](UBillboardComponent* Billboard)
	{
		if (!Billboard)
		{
			return;
		}

		Billboard->SetBillboardEnabled(true);
		Billboard->SetAbsoluteScale(true);
		Billboard->SetEditorOnlyComponent(true);
		Billboard->SetHiddenInComponentTree(true);
		if (auto Material = FMaterialManager::Get().GetOrCreateMaterial("Content/Material/Editor/Decal.uasset"))
		{
			Billboard->SetMaterial(Material);
		}
	};

	for (USceneComponent* Child : GetChildren())
	{
		UBillboardComponent* Billboard = Cast<UBillboardComponent>(Child);
		if (Billboard && Billboard->IsEditorOnlyComponent())
		{
			// 에디터 아이콘 빌보드는 부모 스케일과 컴포넌트 트리 기본 표시에서 분리한다.
			ConfigureBillboard(Billboard);
			return Billboard;
		}
	}

	UBillboardComponent* Billboard = OwnerActor->AddComponent<UBillboardComponent>();
	if (Billboard)
	{
		Billboard->AttachToComponent(this);
		// 에디터 아이콘 빌보드는 부모 스케일과 컴포넌트 트리 기본 표시에서 분리한다.
		ConfigureBillboard(Billboard);
	}

	return Billboard;
}
