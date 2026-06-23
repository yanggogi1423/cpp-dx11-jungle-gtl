#include "SceneComponent.h"
#include "Object/ObjectFactory.h"

// 소유자와 부모-자식 관계를 초기 상태로 명시적으로 리셋합니다.
// Actor::Duplicate() 에서 DuplicateSubTree 를 통해 올바른 관계가 복원됩니다.
void USceneComponent::PostDuplicate(UObject* Original)
{
    UActorComponent::PostDuplicate(Original);

    SetOwner(nullptr);

    // 트랜스폼 캐시는 새 부모에 붙을 때 다시 계산되도록 Dirty 플래그를 켭니다.
    bTransformDirty = true;

    // 부모-자식 관계는 Actor::PostDuplicate() 에서 DuplicateSubTree 를 통해 복원됩니다.
    ParentComponent = nullptr;
    ChildComponents.clear();
    AttachSocketName = FName::None;
}

void USceneComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);

	if (Ar.IsSaving())
	{
		if (GetParent() != nullptr)
		{
			uint32 ParentUUID = GetParent()->GetUUID();
			Ar << "ParentUUID" << ParentUUID;
		}
	}

	Ar << "Location" << RelativeLocation;
	Ar << "Rotation" << RelativeRotation;
	Ar << "Scale" << RelativeScale3D;
	Ar << "AttachSocket" << AttachSocketName;
}
USceneComponent::USceneComponent()
{
	CachedWorldMatrix = FMatrix::Identity;
	CachedWorldTransform = FTransform::Identity;
	RelativeRotationQuat = FQuat::Identity;
	bTransformDirty = true;
	UpdateWorldMatrix();
}

USceneComponent::~USceneComponent()
{
	if (ParentComponent != nullptr)
	{
		ParentComponent->RemoveChild(this);
		ParentComponent = nullptr;
	}

	for (auto* Child : ChildComponents)
	{
		if (Child)
		{
			Child->ParentComponent = nullptr;
			Child->MarkTransformDirty();
		}
	}
	ChildComponents.clear();
}

void USceneComponent::AttachToComponent(USceneComponent* InParent, const FName& InSocketName)
{
	if (InParent == nullptr || InParent == this)
	{
		return;
	}

	AttachSocketName = InSocketName;
	SetParent(InParent);   // 내부에서 MarkTransformDirty 호출됨
}

void USceneComponent::SetParent(USceneComponent* NewParent)
{
	if (NewParent == ParentComponent || NewParent == this)
	{
		return;
	}

	if (ParentComponent)
	{
		ParentComponent->RemoveChild(this);
	}

	ParentComponent = NewParent;

	if (ParentComponent)
	{
		if (!ParentComponent->ContainsChild(this))
		{
			ParentComponent->ChildComponents.push_back(this);
		}
	}

	MarkTransformDirty();
}

void USceneComponent::AddChild(USceneComponent* NewChild)
{
	if (NewChild == nullptr)
	{
		return;
	}

	NewChild->SetParent(this);
}

void USceneComponent::RemoveChild(USceneComponent* Child)
{
	if (Child == nullptr)
	{
		return;
	}

	auto Iter = std::find(ChildComponents.begin(), ChildComponents.end(), Child);
	if (Iter != ChildComponents.end())
	{
		if ((*Iter)->ParentComponent == this)
		{
			(*Iter)->ParentComponent = nullptr;
			(*Iter)->MarkTransformDirty();
		}

		ChildComponents.erase(Iter);
	}
}

bool USceneComponent::ContainsChild(const USceneComponent* Child) const
{
	if (Child == nullptr)
	{
		return false;
	}

	return std::find(ChildComponents.begin(), ChildComponents.end(), Child) != ChildComponents.end();
}

void USceneComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UActorComponent::GetEditableProperties(OutProps);
}

void USceneComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);
	// 에디터가 RelativeRotation(Euler)을 직접 수정했을 때 쿼터니언 권위 소스를 동기화합니다.
	RelativeRotationQuat = FQuat::MakeFromEuler(RelativeRotation);
	RelativeRotationQuat.Normalize();
	MarkTransformDirty();
}

FRotator USceneComponent::GetRelativeRotator() const
{
	// 쿼터니언 권위 소스에서 직접 변환 — Euler 왕복 없음
	FRotator Rot = RelativeRotationQuat.Rotator();
	Rot.Normalize();
	return Rot;
}

FQuat USceneComponent::GetRelativeQuat() const
{
	// 권위 있는 쿼터니언을 직접 반환 — 짐벌 락 없음
	return RelativeRotationQuat;
}

void USceneComponent::SetRelativeRotationRotator(const FRotator& NewRotation)
{
	FRotator Normalized = NewRotation;
	Normalized.Normalize();

	// 에디터/카메라 용도에서 roll drift 방지
	if (MathUtil::Abs(Normalized.Roll) < 1e-6f)
	{
		Normalized.Roll = 0.0f;
	}

	RelativeRotationQuat = FQuat(Normalized);
	RelativeRotationQuat.Normalize();
	RelativeRotation = RelativeRotationQuat.Euler();
	MarkTransformDirty();
}

void USceneComponent::SetRelativeRotationQuat(const FQuat& NewRotationQuat)
{
	// 쿼터니언을 권위 소스에 직접 저장 — Euler 왕복 변환 없음
	RelativeRotationQuat = NewRotationQuat.GetNormalized();
	RelativeRotation = RelativeRotationQuat.Euler();
	MarkTransformDirty();
}

void USceneComponent::SetRelativeLocation(const FVector& NewLocation)
{
	RelativeLocation = NewLocation;
	MarkTransformDirty();
}

void USceneComponent::SetRelativeRotation(const FVector& NewRotation)
{
	// Euler 입력을 쿼터니언 권위 소스에 저장하고 표시용 캐시도 동기화
	RelativeRotationQuat = FQuat::MakeFromEuler(NewRotation);
	RelativeRotationQuat.Normalize();
	RelativeRotation = RelativeRotationQuat.Euler();
	MarkTransformDirty();
}

void USceneComponent::SetRelativeScale(const FVector& NewScale)
{
	RelativeScale3D = NewScale;
	MarkTransformDirty();
}

void USceneComponent::MarkTransformDirty()
{
	bTransformDirty = true;
    OnTransformDirty();

	for (auto* Child : ChildComponents)
	{
		if (Child)
		{
			Child->MarkTransformDirty();
		}
	}
}

FTransform USceneComponent::GetRelativeTransform() const
{
	// 쿼터니언 권위 소스를 직접 사용 — Euler/Rotator 왕복 없음
	return FTransform(RelativeRotationQuat, RelativeLocation, RelativeScale3D);
}

FMatrix USceneComponent::GetRelativeMatrix() const
{
	return GetRelativeTransform().ToMatrixWithScale();
}

void USceneComponent::UpdateWorldMatrix() const
{
	if (!bTransformDirty)
	{
		return;
	}

	const FTransform RelativeTransform = GetRelativeTransform();

	if (ParentComponent != nullptr)
	{
		// Socket 기반 attach: 부모가 해당 socket을 가지고 있으면 socket world transform 기준.
		// 아니면 (혹은 AttachSocketName이 None/존재하지 않음) 부모의 일반 world transform 기준.
		// 참고: FName::None도 IsValid()는 true이므로 명시적 비교 사용.
		if (AttachSocketName != FName::None && ParentComponent->HasSocket(AttachSocketName))
		{
			CachedWorldTransform = RelativeTransform * ParentComponent->GetSocketTransform(AttachSocketName);
		}
		else
		{
			CachedWorldTransform = RelativeTransform * ParentComponent->GetWorldTransform();
		}
	}
	else
	{
		CachedWorldTransform = RelativeTransform;
	}

	CachedWorldMatrix = CachedWorldTransform.ToMatrixWithScale();
	bTransformDirty = false;
}

const FMatrix& USceneComponent::GetWorldMatrix() const
{
	if (bTransformDirty)
	{
		UpdateWorldMatrix();
	}

	return CachedWorldMatrix;
}

FTransform USceneComponent::GetWorldTransform() const
{
	if (bTransformDirty)
	{
		UpdateWorldMatrix();
	}

	return CachedWorldTransform;
}

void USceneComponent::SetWorldLocation(FVector NewWorldLocation)
{
	if (ParentComponent != nullptr)
	{
		const FTransform ParentWorldInverse = ParentComponent->GetWorldTransform().Inverse();
		const FVector NewRelativeLocation = ParentWorldInverse.TransformPosition(NewWorldLocation);
		SetRelativeLocation(NewRelativeLocation);
	}
	else
	{
		SetRelativeLocation(NewWorldLocation);
	}
}

void USceneComponent::SetWorldRotation(const FVector& NewWorldRotation)
{
	SetWorldRotationQuat(FQuat::MakeFromEuler(NewWorldRotation));
}

void USceneComponent::SetWorldRotationQuat(const FQuat& NewWorldRotationQuat)
{
	FQuat NewRelativeQuat = NewWorldRotationQuat.GetNormalized();
	if (ParentComponent != nullptr)
	{
		const FQuat ParentWorldQuat = ParentComponent->GetWorldTransform().GetRotation().GetNormalized();
		NewRelativeQuat = (NewRelativeQuat * ParentWorldQuat.Inverse()).GetNormalized();
	}

	SetRelativeRotationQuat(NewRelativeQuat);
}

FVector USceneComponent::GetWorldLocation() const
{
	return GetWorldTransform().GetTranslation();
}

FVector USceneComponent::GetWorldRotation() const
{
	return GetWorldQuat().Euler();
}

FVector USceneComponent::GetWorldScale() const
{
	return GetWorldTransform().GetScale3D();
}

FVector USceneComponent::GetForwardVector() const
{
	return GetWorldTransform().GetUnitAxis(EAxis::X);
}

FVector USceneComponent::GetRightVector() const
{
	return GetWorldTransform().GetUnitAxis(EAxis::Y);
}

FVector USceneComponent::GetUpVector() const
{
	return GetWorldTransform().GetUnitAxis(EAxis::Z);
}

void USceneComponent::Move(const FVector& Delta)
{
	SetRelativeLocation(RelativeLocation + Delta);
}

void USceneComponent::MoveLocal(const FVector& Delta)
{
	const FQuat LocalQuat = GetRelativeQuat();

	const FVector LocalOffset =
		LocalQuat.GetAxisX() * Delta.X +
		LocalQuat.GetAxisY() * Delta.Y +
		LocalQuat.GetAxisZ() * Delta.Z;

	SetRelativeLocation(RelativeLocation + LocalOffset);
}

void USceneComponent::AddRelativeRotation(const FVector& DeltaRotation)
{
	const FQuat DeltaQuat = FQuat::MakeFromEuler(DeltaRotation).GetNormalized();
	const FQuat ResultQuat = (GetRelativeQuat() * DeltaQuat).GetNormalized();
	SetRelativeRotationQuat(ResultQuat);
}

void USceneComponent::AddLocalRotation(const FVector& DeltaRotation)
{
	const FQuat DeltaQuat = FQuat::MakeFromEuler(DeltaRotation).GetNormalized();
	const FQuat ResultQuat = (DeltaQuat * GetRelativeQuat()).GetNormalized();
	SetRelativeRotationQuat(ResultQuat);
}

void USceneComponent::AddWorldRotation(const FVector& DeltaRotation)
{
	const FQuat DeltaQuat = FQuat::MakeFromEuler(DeltaRotation).GetNormalized();
	const FQuat ResultQuat = (GetWorldQuat() * DeltaQuat).GetNormalized();
	SetWorldRotationQuat(ResultQuat);
}

void USceneComponent::AddWorldOffset(const FVector& WorldDelta)
{
	if (ParentComponent == nullptr)
	{
		SetRelativeLocation(RelativeLocation + WorldDelta);
		return;
	}

	const FTransform ParentWorldInverse = ParentComponent->GetWorldTransform().Inverse();
	const FVector LocalDelta = ParentWorldInverse.TransformVector(WorldDelta);
	SetRelativeLocation(RelativeLocation + LocalDelta);
}

FQuat USceneComponent::GetWorldQuat() const
{
	return GetWorldTransform().GetRotation().GetNormalized();
}
