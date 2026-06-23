#include "GizmoComponent.h"
#include "Object/ObjectFactory.h"
#include "GameFramework/AActor.h"
#include "Math/Quat.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Resource/ConstantBufferPool.h"
#include "Collision/RayUtils.h"
#include "Render/Pipeline/PrimitiveProxy.h"
#include "GameFramework/World.h"
#include "Render/Pipeline/WorldRenderProxy.h"

#include <cmath>

class FGizmoProxy : public FPrimitiveProxy
{
public:
	FGizmoProxy(UGizmoComponent* InOwner) : FPrimitiveProxy(InOwner) {}

	void UpdateProxy() override
	{
	}

	void SubmitRenderCommand(FViewContext& View) override
	{
		if (IsDirty())
		{
			UpdateProxy();
			bIsDirty = false;
		}

		UGizmoComponent* Gizmo = static_cast<UGizmoComponent*>(Owner);
		if (!View.GetShowFlags().bGizmo || !Gizmo->IsVisible()) return;

		Gizmo->UpdateAxisMask(View.GetViewportType());

		FMeshBuffer* GizmoMesh = Gizmo->GetMeshBuffer();
		const FVector CameraPos = View.GetView().GetInverseFast().GetLocation();
		float PerViewScale = Gizmo->ComputeScreenSpaceScale(CameraPos, View.IsOrtho(), View.GetOrthoWidth());

		FMatrix WorldMatrix = FMatrix::MakeScaleMatrix(FVector(PerViewScale, PerViewScale, PerViewScale))
			* Gizmo->GetRelativeQuat().ToMatrix()
			* FMatrix::MakeTranslationMatrix(Gizmo->GetWorldLocation());

		auto CreateGizmoCmd = [&](bool bInner) {
			FRenderCommand Cmd = {};
			Cmd.Shader = FShaderManager::Get().GetShader(EShaderType::Gizmo);
			Cmd.MeshBuffer = GizmoMesh;
			Cmd.PerObjectConstants = FPerObjectConstants{ WorldMatrix };
			Cmd.PickingId = Gizmo->GetUUID();

			auto& G = Cmd.ExtraCB.Bind<FGizmoConstants>(
				FConstantBufferPool::Get().GetBuffer(ECBSlot::Gizmo, sizeof(FGizmoConstants)), ECBSlot::Gizmo);
			G.ColorTint = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			G.bIsInnerGizmo = bInner ? 1 : 0;
			G.bClicking = Gizmo->IsHolding() ? 1 : 0;
			G.SelectedAxis = Gizmo->GetSelectedAxis() >= 0 ? (uint32)Gizmo->GetSelectedAxis() : 0xffffffffu;
			G.HoveredAxisOpacity = 0.7f;
			G.AxisMask = Gizmo->GetAxisMask();

			return Cmd;
		};

		View.AddCommand(ERenderPass::GizmoOuter, CreateGizmoCmd(false));
		View.AddCommand(ERenderPass::GizmoInner, CreateGizmoCmd(true));
	}
};

IMPLEMENT_CLASS(UGizmoComponent, UPrimitiveComponent)

#include <cmath>
UGizmoComponent::UGizmoComponent()
{
	MeshData = &FMeshBufferManager::Get().GetMeshData(EMeshShape::TransGizmo);
	LocalExtents = FVector(1.5f, 1.5f, 1.5f);
}

FPrimitiveProxy* UGizmoComponent::CreateProxy()
{
	return new FGizmoProxy(this);
}

UWorld* UGizmoComponent::GetWorld() const
{
	if (ExplicitWorld) return ExplicitWorld;
	return UPrimitiveComponent::GetWorld();
}

bool UGizmoComponent::IntersectRayAxis(const FRay& Ray, FVector AxisEnd, float& OutRayT)
{
	FVector AxisStart = GetWorldLocation();
	FVector RayOrigin = Ray.Origin;
	FVector RayDirection = Ray.Direction;

	FVector AxisVector = AxisEnd - AxisStart;
	FVector DiffOrigin = RayOrigin - AxisStart;

	float RayDirDotRayDir = RayDirection.X * RayDirection.X + RayDirection.Y * RayDirection.Y + RayDirection.Z * RayDirection.Z;
	float RayDirDotAxis = RayDirection.X * AxisVector.X + RayDirection.Y * AxisVector.Y + RayDirection.Z * AxisVector.Z;
	float AxisDotAxis = AxisVector.X * AxisVector.X + AxisVector.Y * AxisVector.Y + AxisVector.Z * AxisVector.Z;
	float RayDirDotDiff = RayDirection.X * DiffOrigin.X + RayDirection.Y * DiffOrigin.Y + RayDirection.Z * DiffOrigin.Z;
	float AxisDotDiff = AxisVector.X * DiffOrigin.X + AxisVector.Y * DiffOrigin.Y + AxisVector.Z * DiffOrigin.Z;

	float Denominator = (RayDirDotRayDir * AxisDotAxis) - (RayDirDotAxis * RayDirDotAxis);

	float RayT;
	float AxisS;

	if (Denominator < 1e-6f)
	{
		RayT = 0.0f;
		AxisS = (AxisDotAxis > 0.0f) ? (AxisDotDiff / AxisDotAxis) : 0.0f;
	}
	else
	{
		RayT = (RayDirDotAxis * AxisDotDiff - AxisDotAxis * RayDirDotDiff) / Denominator;
		AxisS = (RayDirDotRayDir * AxisDotDiff - RayDirDotAxis * RayDirDotDiff) / Denominator;
	}

	if (RayT < 0.0f) RayT = 0.0f;

	if (AxisS < 0.0f) AxisS = 0.0f;
	else if (AxisS > 1.0f) AxisS = 1.0f;

	FVector ClosestPointOnRay = RayOrigin + (RayDirection * RayT);
	FVector ClosestPointOnAxis = AxisStart + (AxisVector * AxisS);

	FVector DistanceVector = ClosestPointOnRay - ClosestPointOnAxis;
	float DistanceSquared = (DistanceVector.X * DistanceVector.X) +
		(DistanceVector.Y * DistanceVector.Y) +
		(DistanceVector.Z * DistanceVector.Z);

	float ClickThresholdSquared = Radius * Radius;

	if (DistanceSquared < ClickThresholdSquared)
	{
		OutRayT = RayT;
		return true;
	}

	return false;
}

void UGizmoComponent::HandleDrag(float DragAmount)
{
	float AppliedDragAmount = DragAmount;
	auto CalcStepCount = [](float Value) -> int32
	{
		if (Value >= 0.0f)
		{
			return static_cast<int32>(std::floor(Value));
		}
		return static_cast<int32>(std::ceil(Value));
	};
	switch (CurMode)
	{
	case EGizmoMode::Translate:
		if (bTranslateSnapEnabled)
		{
			const float Step = TranslateSnapValue > 0.0001f ? TranslateSnapValue : 0.0001f;
			TranslateSnapAccumulator += DragAmount;
			const int32 StepCount = CalcStepCount(TranslateSnapAccumulator / Step);
			if (StepCount == 0)
			{
				return;
			}
			AppliedDragAmount = Step * static_cast<float>(StepCount);
			TranslateSnapAccumulator -= AppliedDragAmount;
		}
		break;
	case EGizmoMode::Rotate:
		if (bRotateSnapEnabled)
		{
			const float Step = (RotateSnapValueDegrees > 0.0001f ? RotateSnapValueDegrees : 0.0001f) * DEG_TO_RAD;
			RotateSnapAccumulator += DragAmount;
			const int32 StepCount = CalcStepCount(RotateSnapAccumulator / Step);
			if (StepCount == 0)
			{
				return;
			}
			AppliedDragAmount = Step * static_cast<float>(StepCount);
			RotateSnapAccumulator -= AppliedDragAmount;
		}
		break;
	case EGizmoMode::Scale:
		if (bScaleSnapEnabled)
		{
			const float Step = ScaleSnapValue > 0.0001f ? ScaleSnapValue : 0.0001f;
			ScaleSnapAccumulator += DragAmount;
			const int32 StepCount = CalcStepCount(ScaleSnapAccumulator / Step);
			if (StepCount == 0)
			{
				return;
			}
			AppliedDragAmount = Step * static_cast<float>(StepCount);
			ScaleSnapAccumulator -= AppliedDragAmount;
		}
		break;
	default:
		break;
	}

	switch (CurMode)
	{
	case EGizmoMode::Translate:
		TranslateTarget(AppliedDragAmount);
		break;
	case EGizmoMode::Rotate:
		RotateTarget(AppliedDragAmount);
		break;
	case EGizmoMode::Scale:
		ScaleTarget(AppliedDragAmount);
		break;
	default:
		break;
	}

	UpdateGizmoTransform();
}

void UGizmoComponent::TranslateTarget(float DragAmount)
{
	if (!TargetActor || !TargetActor->GetRootComponent()) return;

	FVector ConstrainedDelta = GetVectorForAxis(SelectedAxis) * DragAmount;

	AddWorldOffset(ConstrainedDelta);

	if (AllSelectedActors)
	{
		for (AActor* Actor : *AllSelectedActors)
		{
			if (!Actor || !Actor->GetRootComponent())
			{
				continue;
			}

			Actor->AddActorWorldOffset(ConstrainedDelta);
			Actor->GetRootComponent()->MarkTransformDirty();
		}
	}
	else
	{
		TargetActor->AddActorWorldOffset(ConstrainedDelta);
		TargetActor->GetRootComponent()->MarkTransformDirty();
	}
}

void UGizmoComponent::RotateTarget(float DragAmount)
{
	if (!TargetActor || !TargetActor->GetRootComponent()) return;

	FVector RotationAxis = GetVectorForAxis(SelectedAxis);
	FQuat DeltaQuat = FQuat::FromAxisAngle(RotationAxis, DragAmount);

	const float DeltaDeg = DragAmount * RAD_TO_DEG;

	auto ApplyRotation = [&](AActor* Actor)
		{
			if (!Actor || !Actor->GetRootComponent()) return;
			USceneComponent* Root = Actor->GetRootComponent();
			const FQuat& CurQuat = Root->GetRelativeQuat();
			FQuat NewQuat = bIsWorldSpace ? (DeltaQuat * CurQuat) : (CurQuat * DeltaQuat);

			FRotator EulerHint = Root->GetCachedEditRotator();
			if (bIsWorldSpace)
			{
				switch (SelectedAxis)
				{
				case 0: EulerHint.Roll  += DeltaDeg; break;
				case 1: EulerHint.Pitch += DeltaDeg; break;
				case 2: EulerHint.Yaw   += DeltaDeg; break;
				}
			}
			else
			{
				switch (SelectedAxis)
				{
				case 0: EulerHint.Roll  += DeltaDeg; break;
				case 1: EulerHint.Pitch += DeltaDeg; break;
				case 2: EulerHint.Yaw   += DeltaDeg; break;
				}
			}
			Root->SetRelativeRotationWithEulerHint(NewQuat, EulerHint);

			// 다중 선택 회전 시에는 각 액터를 개별 로컬 원점이 아니라
			// 현재 Gizmo(PrimarySelection) 피벗을 기준으로 함께 공전시킨다.
			const FVector Pivot = GetWorldLocation();
			const FVector Offset = Root->GetWorldLocation() - Pivot;
			Root->SetWorldLocation(Pivot + DeltaQuat.RotateVector(Offset));
			Root->MarkTransformDirty();
		};

	if (AllSelectedActors)
	{
		for (AActor* Actor : *AllSelectedActors)
		{
			ApplyRotation(Actor);
		}
	}
	else
	{
		ApplyRotation(TargetActor);
	}
}

void UGizmoComponent::ScaleTarget(float DragAmount)
{
	if (!TargetActor || !TargetActor->GetRootComponent()) return;

	float ScaleDelta = DragAmount * ScaleSensitivity;

	auto ApplyScale = [&](AActor* Actor)
		{
			if (!Actor || !Actor->GetRootComponent()) return;
			FVector NewScale = Actor->GetActorScale();
			switch (SelectedAxis)
			{
			case 0: NewScale.X += ScaleDelta; break;
			case 1: NewScale.Y += ScaleDelta; break;
			case 2: NewScale.Z += ScaleDelta; break;
			}
			Actor->SetActorScale(NewScale);
			Actor->GetRootComponent()->MarkTransformDirty();
		};

	if (AllSelectedActors)
	{
		for (AActor* Actor : *AllSelectedActors)
		{
			ApplyScale(Actor);
		}
	}
	else
	{
		ApplyScale(TargetActor);
	}
}

void UGizmoComponent::SetTargetLocation(FVector NewLocation)
{
	if (!TargetActor) return;

	TargetActor->SetActorLocation(NewLocation);
	UpdateGizmoTransform();
}

void UGizmoComponent::SetTargetRotation(FRotator NewRotation)
{
	if (!TargetActor) return;

	TargetActor->SetActorRotation(NewRotation);
	UpdateGizmoTransform();
}

void UGizmoComponent::SetTargetScale(FVector NewScale)
{
	if (!TargetActor) return;

	FVector SafeScale = NewScale;
	if (SafeScale.X < 0.001f) SafeScale.X = 0.001f;
	if (SafeScale.Y < 0.001f) SafeScale.Y = 0.001f;
	if (SafeScale.Z < 0.001f) SafeScale.Z = 0.001f;

	TargetActor->SetActorScale(SafeScale);
}

bool UGizmoComponent::LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult, float InClosestT)
{
	if (!MeshData || MeshData->Indices.empty()) return false;

	bool bHit = FRayUtils::RaycastTriangles(
		Ray, CachedWorldMatrix,
		&MeshData->Vertices[0].Position,
		sizeof(FVertex),
		MeshData->Indices,
		OutHitResult);

	if (bHit)
	{
		OutHitResult.HitComponent = this;
	}

	UpdateHoveredAxis(OutHitResult.FaceIndex);

	return OutHitResult.bHit;
}


FVector UGizmoComponent::GetVectorForAxis(int32 Axis)
{
	switch (Axis)
	{
	case 0:
		return GetForwardVector();
	case 1:
		return GetRightVector();
	case 2:
		return GetUpVector();
	default:
		return FVector(0.f, 0.f, 0.f);
	}
}

void UGizmoComponent::SetTarget(AActor* NewTarget)
{
	if (!NewTarget || !NewTarget->GetRootComponent())
	{
		return;
	}

	TargetActor = NewTarget;

	SetWorldLocation(TargetActor->GetActorLocation());
	UpdateGizmoTransform();
	SetVisibility(true);
}

void UGizmoComponent::UpdateLinearDrag(const FRay& Ray)
{
	FVector AxisVector = GetVectorForAxis(SelectedAxis);

	FVector PlaneNormal = AxisVector.Cross(Ray.Direction);
	FVector ProjectDir = PlaneNormal.Cross(AxisVector);

	float Denom = Ray.Direction.Dot(ProjectDir);
	if (std::abs(Denom) < 1e-6f) return;

	float DistanceToPlane = (GetWorldLocation() - Ray.Origin).Dot(ProjectDir) / Denom;
	FVector CurrentIntersectionLocation = Ray.Origin + (Ray.Direction * DistanceToPlane);

	if (bIsFirstFrameOfDrag)
	{
		LastIntersectionLocation = CurrentIntersectionLocation;
		bIsFirstFrameOfDrag = false;
		return;
	}

	FVector FullDelta = CurrentIntersectionLocation - LastIntersectionLocation;

	float DragAmount = FullDelta.Dot(AxisVector);

	HandleDrag(DragAmount);

	LastIntersectionLocation = CurrentIntersectionLocation;
}

void UGizmoComponent::UpdateAngularDrag(const FRay& Ray)
{
	FVector AxisVector = GetVectorForAxis(SelectedAxis);
	FVector PlaneNormal = AxisVector;

	float Denom = Ray.Direction.Dot(PlaneNormal);
	if (std::abs(Denom) < 1e-6f) return;

	float DistanceToPlane = (GetWorldLocation() - Ray.Origin).Dot(PlaneNormal) / Denom;
	FVector CurrentIntersectionLocation = Ray.Origin + (Ray.Direction * DistanceToPlane);

	if (bIsFirstFrameOfDrag)
	{
		LastIntersectionLocation = CurrentIntersectionLocation;
		bIsFirstFrameOfDrag = false;
		return;
	}

	FVector CenterToLast = (LastIntersectionLocation - GetWorldLocation()).Normalized();
	FVector CenterToCurrent = (CurrentIntersectionLocation - GetWorldLocation()).Normalized();

	float DotProduct = Clamp(CenterToLast.Dot(CenterToCurrent), -1.0f, 1.0f);
	float AngleRadians = std::acos(DotProduct);

	FVector CrossProduct = CenterToLast.Cross(CenterToCurrent);
	float Sign = (CrossProduct.Dot(AxisVector) >= 0.0f) ? 1.0f : -1.0f;

	float DeltaAngle = Sign * AngleRadians;

	HandleDrag(DeltaAngle);

	LastIntersectionLocation = CurrentIntersectionLocation;
}

void UGizmoComponent::UpdateHoveredAxis(int Index)
{
	int32 OldAxis = SelectedAxis;
	if (Index < 0)
	{
		if (IsHolding() == false) SelectedAxis = -1;
	}
	else
	{
		if (IsHolding() == false)
		{
			uint32 VertexIndex = MeshData->Indices[Index];
			uint32 HitAxis = MeshData->Vertices[VertexIndex].SubID;

			if (AxisMask & (1u << HitAxis))
			{
				SelectedAxis = HitAxis;
			}
			else
			{
				SelectedAxis = -1;
			}
		}
	}

	if (OldAxis != SelectedAxis)
	{
		MarkRenderStateDirty();
	}
}

void UGizmoComponent::UpdateDrag(const FRay& Ray)
{
	if (IsHolding() == false || IsActive() == false)
	{
		return;
	}

	if (SelectedAxis == -1 || TargetActor == nullptr)
	{
		return;
	}

	if (CurMode == EGizmoMode::Rotate)
	{
		UpdateAngularDrag(Ray);
	}
	else
	{
		UpdateLinearDrag(Ray);
	}
}

void UGizmoComponent::DragEnd()
{
	bIsFirstFrameOfDrag = true;
	TranslateSnapAccumulator = 0.0f;
	RotateSnapAccumulator = 0.0f;
	ScaleSnapAccumulator = 0.0f;
	SetHolding(false);
	SetPressedOnHandle(false);
}

void UGizmoComponent::SetTranslateSnapEnabled(bool bEnabled)
{
	bTranslateSnapEnabled = bEnabled;
	if (!bTranslateSnapEnabled)
	{
		TranslateSnapAccumulator = 0.0f;
	}
}

void UGizmoComponent::SetTranslateSnapValue(float InValue)
{
	const float NextValue = InValue > 0.0001f ? InValue : 0.0001f;
	if (std::fabs(TranslateSnapValue - NextValue) > 0.0001f)
	{
		TranslateSnapValue = NextValue;
		TranslateSnapAccumulator = 0.0f;
	}
}

void UGizmoComponent::SetRotateSnapEnabled(bool bEnabled)
{
	bRotateSnapEnabled = bEnabled;
	if (!bRotateSnapEnabled)
	{
		RotateSnapAccumulator = 0.0f;
	}
}

void UGizmoComponent::SetRotateSnapValueDegrees(float InDegrees)
{
	const float NextValue = InDegrees > 0.0001f ? InDegrees : 0.0001f;
	if (std::fabs(RotateSnapValueDegrees - NextValue) > 0.0001f)
	{
		RotateSnapValueDegrees = NextValue;
		RotateSnapAccumulator = 0.0f;
	}
}

void UGizmoComponent::SetScaleSnapEnabled(bool bEnabled)
{
	bScaleSnapEnabled = bEnabled;
	if (!bScaleSnapEnabled)
	{
		ScaleSnapAccumulator = 0.0f;
	}
}

void UGizmoComponent::SetScaleSnapValue(float InValue)
{
	const float NextValue = InValue > 0.0001f ? InValue : 0.0001f;
	if (std::fabs(ScaleSnapValue - NextValue) > 0.0001f)
	{
		ScaleSnapValue = NextValue;
		ScaleSnapAccumulator = 0.0f;
	}
}

void UGizmoComponent::SetNextMode()
{
	EGizmoMode NextMode = static_cast<EGizmoMode>((static_cast<int>(CurMode) + 1) % EGizmoMode::End);
	UpdateGizmoMode(NextMode);
}

void UGizmoComponent::SetHolding(bool bHold)
{
	if (bIsHolding != bHold)
	{
		bIsHolding = bHold;
		MarkRenderStateDirty();
	}
}

void UGizmoComponent::UpdateGizmoMode(EGizmoMode NewMode)
{
	CurMode = NewMode;
	UpdateGizmoTransform();
	MarkRenderStateDirty();
}

void UGizmoComponent::UpdateGizmoTransform()
{
	if (!TargetActor || !TargetActor->GetRootComponent()) return;

	SetWorldLocation(TargetActor->GetActorLocation());

	FRotator ActorRot = TargetActor->GetActorRotation();

	switch (CurMode)
	{
	case EGizmoMode::Scale:
		SetRelativeRotation(ActorRot);
		MeshData = &FMeshBufferManager::Get().GetMeshData(EMeshShape::ScaleGizmo);
		break;

	case EGizmoMode::Rotate:
		SetRelativeRotation(bIsWorldSpace ? FRotator() : ActorRot);
		MeshData = &FMeshBufferManager::Get().GetMeshData(EMeshShape::RotGizmo);
		break;

	case EGizmoMode::Translate:
		SetRelativeRotation(bIsWorldSpace ? FRotator() : ActorRot);
		MeshData = &FMeshBufferManager::Get().GetMeshData(EMeshShape::TransGizmo);
		break;
	}
	MarkRenderStateDirty();
}

float UGizmoComponent::ComputeScreenSpaceScale(const FVector& CameraLocation, bool bIsOrtho, float OrthoWidth)
{
	float NewScale;
	if (bIsOrtho)
	{
		NewScale = OrthoWidth * GizmoScreenScale;
	}
	else
	{
		float Distance = FVector::Distance(CameraLocation, GetWorldLocation());
		NewScale = Distance * GizmoScreenScale;
	}
	return (NewScale < 0.01f) ? 0.01f : NewScale;
}

void UGizmoComponent::ApplyScreenSpaceScaling(const FVector& CameraLocation, bool bIsOrtho, float OrthoWidth)
{
	float NewScale = ComputeScreenSpaceScale(CameraLocation, bIsOrtho, OrthoWidth);
	SetRelativeScale(FVector(NewScale, NewScale, NewScale));
	MarkRenderStateDirty();
}

void UGizmoComponent::SetWorldSpace(bool bWorldSpace)
{
	bIsWorldSpace = bWorldSpace;
	UpdateGizmoTransform();
}

void UGizmoComponent::ToggleWorldSpace()
{
	bIsWorldSpace = !bIsWorldSpace;
	UpdateGizmoTransform();
}


void UGizmoComponent::UpdateAxisMask(ELevelViewportType ViewportType)
{
	constexpr uint32 AllAxes = 0x7;
	uint32 ViewAxis = AllAxes;

	switch (ViewportType)
	{
	case ELevelViewportType::Top:
	case ELevelViewportType::Bottom:
		ViewAxis = 0x4; break;
	case ELevelViewportType::Front:
	case ELevelViewportType::Back:
		ViewAxis = 0x1; break;
	case ELevelViewportType::Left:
	case ELevelViewportType::Right:
		ViewAxis = 0x2; break;
	default: break;
	}

	uint32 NewMask;
	if (ViewAxis == AllAxes)
	{
		NewMask = AllAxes;
	}
	else if (CurMode == EGizmoMode::Rotate)
	{
		NewMask = ViewAxis;
	}
	else
	{
		NewMask = AllAxes & ~ViewAxis;
	}

	if (AxisMask != NewMask)
	{
		AxisMask = NewMask;
		MarkRenderStateDirty();
	}
}

void UGizmoComponent::Deactivate()
{
	TargetActor = nullptr;
	AllSelectedActors = nullptr;
	SetVisibility(false);
	SelectedAxis = -1;
	MarkRenderStateDirty();
}

FMeshBuffer* UGizmoComponent::GetMeshBuffer() const
{
	EMeshShape Shape = EMeshShape::TransGizmo;
	switch (CurMode)
	{
	case EGizmoMode::Translate:
		break;
	case EGizmoMode::Rotate:
		Shape = EMeshShape::RotGizmo;
		break;
	case EGizmoMode::Scale:
		Shape = EMeshShape::ScaleGizmo;
		break;
	}
	return &FMeshBufferManager::Get().GetMeshBuffer(Shape);
}
