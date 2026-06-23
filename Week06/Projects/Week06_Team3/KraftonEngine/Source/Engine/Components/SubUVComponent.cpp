#include "SubUVComponent.h"
#include "Object/ObjectFactory.h"

#include <algorithm>
#include <cstring>
#include "Render/Resource/MeshBufferManager.h"
#include "Resource/ResourceManager.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Components/CameraComponent.h"
#include "Render/Proxy/SubUVSceneProxy.h"
#include "Serialization/Archive.h"

IMPLEMENT_CLASS(USubUVComponent, UBillboardComponent)

namespace
{
	int32 ClampSubUVEndFrame(int32 EndFrameIndex, int32 Columns, int32 Rows)
	{
		const int32 TotalFrames = std::max(1, Columns) * std::max(1, Rows);
		return std::max(0, std::min(EndFrameIndex, TotalFrames - 1));
	}
}

USubUVComponent::USubUVComponent()
{
	SetVisibility(false);
	SyncFrameDisplayFromInternal();
}

FPrimitiveSceneProxy* USubUVComponent::CreateSceneProxy()
{
	return new FSubUVSceneProxy(this);
}

void USubUVComponent::Serialize(FArchive& Ar)
{
	// UBillboardComponent::Serialize가 TextureName/Width/Height를 처리한다.
	// SubUV는 자체 ParticleResource를 쓰므로 Billboard의 TextureName은 사용하지 않지만,
	// 직렬화 일관성을 위해 Super 호출은 유지한다 (Width/Height는 공통).
	UBillboardComponent::Serialize(Ar);

	Ar << ParticleName;
	Ar << FrameIndex;
	Ar << Columns;
	Ar << Rows;
	Ar << StartFrameIndex;
	Ar << EndFrameIndex;
	Ar << PlayRate;
	Ar << bLoop;

	if (Ar.IsLoading())
	{
		Columns = std::max(1, Columns);
		Rows = std::max(1, Rows);

		const int32 Total = std::max(1, Columns) * std::max(1, Rows);
		StartFrameIndex = std::max(0, std::min(StartFrameIndex, Total - 1));
		EndFrameIndex = std::max(0, std::min(EndFrameIndex, Total - 1));

		if (StartFrameIndex > EndFrameIndex)
		{
			EndFrameIndex = StartFrameIndex;
		}

		if (FrameIndex < static_cast<uint32>(StartFrameIndex))
		{
			FrameIndex = static_cast<uint32>(StartFrameIndex);
		}
		else if (FrameIndex > static_cast<uint32>(EndFrameIndex))
		{
			FrameIndex = static_cast<uint32>(EndFrameIndex);
		}

		SyncFrameDisplayFromInternal();
	}
}

void USubUVComponent::PostDuplicate()
{
	UBillboardComponent::PostDuplicate();
	// 파티클 리소스 재바인딩
	SetParticle(ParticleName);
}

void USubUVComponent::SetParticle(const FName& InParticleName)
{
	ParticleName = InParticleName;
	CachedParticle = FResourceManager::Get().FindParticle(InParticleName);
}

void USubUVComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	// Billboard의 Texture 프로퍼티는 SubUV에서 의미가 없으므로 의도적으로 스킵.
	// UPrimitiveComponent로 직접 올라가 공통 트랜스폼 등만 가져온 뒤,
	// Width/Height(상속 멤버)와 SubUV 고유 프로퍼티만 노출한다.
	UPrimitiveComponent::GetEditableProperties(OutProps);
	SyncFrameDisplayFromInternal();
		
	OutProps.push_back({ "Particle",    EPropertyType::Name,  &ParticleName });
	OutProps.push_back({ "Width",		EPropertyType::Float, &Width,  0.1f, 100.0f, 0.1f });
	OutProps.push_back({ "Height",		EPropertyType::Float, &Height, 0.1f, 100.0f, 0.1f });
	OutProps.push_back({ "Columns",		EPropertyType::Int,   &Columns });
	OutProps.push_back({ "Rows",		EPropertyType::Int,   &Rows });
	OutProps.push_back({ "Start Frame", EPropertyType::Int,   &StartFrameDisplay });
	OutProps.push_back({ "End Frame",	EPropertyType::Int,   &EndFrameDisplay });
	OutProps.push_back({ "Play Rate",	EPropertyType::Float, &PlayRate, 1.0f, 120.0f, 1.0f });
	OutProps.push_back({ "bLoop",		EPropertyType::Bool,  &bLoop });
}

void USubUVComponent::PostEditProperty(const char* PropertyName)
{
	// SubUV는 GetEditableProperties에서 Billboard의 Texture를 의도적으로 스킵하므로
	// PostEditProperty도 Billboard를 거치지 않고 Primitive로 직접 올라간다.
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Particle") == 0)
	{
		SetParticle(ParticleName);
		// 파티클 교체 시 UV 그리드/텍스처가 바뀌므로 Mesh 단계까지 dirty.
		MarkProxyDirty(EDirtyFlag::Mesh);
	}
	else if (strcmp(PropertyName, "Width") == 0 || strcmp(PropertyName, "Height") == 0)
	{
		MarkProxyDirty(EDirtyFlag::Transform);
		MarkWorldBoundsDirty();
	}
	else if (strcmp(PropertyName, "Columns") == 0 ||
		strcmp(PropertyName, "Rows") == 0 ||
		strcmp(PropertyName, "Start Frame") == 0 ||
		strcmp(PropertyName, "End Frame") == 0)
	{
		Columns = std::max(1, Columns);
		Rows = std::max(1, Rows);
		
		SetStartFrameDisplay(StartFrameDisplay);
		SetEndFrameDisplay(EndFrameDisplay);

		const uint32 MinFrame = static_cast<uint32>(StartFrameIndex);
		const uint32 MaxFrame = static_cast<uint32>(EndFrameIndex);

		if (FrameIndex < MinFrame)
		{
			FrameIndex = MinFrame;
		}
		else if (FrameIndex > MaxFrame)
		{
			FrameIndex = MaxFrame;
		}
		
		MarkProxyDirty(EDirtyFlag::Mesh);
	}
}

void USubUVComponent::UpdateWorldAABB() const
{
	FVector LExt = { 0.01f, 0.5f, 0.5f };

	float NewEx = std::abs(CachedWorldMatrix.M[0][0]) * LExt.X +
		std::abs(CachedWorldMatrix.M[1][0]) * LExt.Y +
		std::abs(CachedWorldMatrix.M[2][0]) * LExt.Z;

	float NewEy = std::abs(CachedWorldMatrix.M[0][1]) * LExt.X +
		std::abs(CachedWorldMatrix.M[1][1]) * LExt.Y +
		std::abs(CachedWorldMatrix.M[2][1]) * LExt.Z;

	float NewEz = std::abs(CachedWorldMatrix.M[0][2]) * LExt.X +
		std::abs(CachedWorldMatrix.M[1][2]) * LExt.Y +
		std::abs(CachedWorldMatrix.M[2][2]) * LExt.Z;

	FVector WorldCenter = GetWorldLocation();

	WorldAABBMinLocation = WorldCenter - FVector(NewEx, NewEy, NewEz);
	WorldAABBMaxLocation = WorldCenter + FVector(NewEx, NewEy, NewEz);
}

FVector USubUVComponent::GetVisualScale() const
{
	// 부모 메시 스케일과 무관하게 파티클 스프라이트 크기를 고정한다.
	return GetRelativeScale();
}

void USubUVComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UBillboardComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CachedParticle) return;
	if (!bLoop && bIsExecute) return; // 단발 재생 완료 후 정지

	TimeAccumulator += DeltaTime;
	const float FrameDuration = 1.0f / PlayRate;
	while (TimeAccumulator >= FrameDuration)
	{
		TimeAccumulator -= FrameDuration;
		const uint32 Start = static_cast<uint32>(StartFrameIndex);
		const uint32 End = static_cast<uint32>(EndFrameIndex);

		if (FrameIndex < Start) FrameIndex = Start;
		else if (FrameIndex > End) FrameIndex = End;

		const uint32 Range = End - Start + 1;

		if (bLoop)
		{
			bIsExecute = false;
			FrameIndex = Start + (FrameIndex - Start + 1) % Range; // 무한 반복
		}
		else
		{
			if (FrameIndex < End)
			{
				FrameIndex++;
			}
			else
			{
				bIsExecute = true;
				TimeAccumulator = 0.0f;
				break;
			}
		}
	}
}

