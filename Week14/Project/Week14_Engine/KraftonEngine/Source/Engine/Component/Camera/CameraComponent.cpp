#include "Component/Camera/CameraComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Object/Reflection/ObjectFactory.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Core/Types/CollisionTypes.h"
#include "Engine/Runtime/Engine.h"
#include "Materials/MaterialManager.h"
#include "Mesh/Importer/MeshImportOptions.h"
#include "Mesh/MeshManager.h"
#include <cmath>

void UCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	// E.2/3: PC 가 BeginPlay 시점엔 아직 spawn 전 → PlayerCameraManager nullptr.
	// PC 의 BeginPlay 에서 World 의 모든 카메라 컴포넌트를 catch up 등록하므로 안전.
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CM = PC->GetPlayerCameraManager())
			{
				CM->RegisterCamera(this);
			}
		}
	}
}

void UCameraComponent::EndPlay()
{
	Super::EndPlay();
	if (UWorld* World = GetWorldEvenIfPendingKill())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CM = PC->GetPlayerCameraManager())
			{
				CM->UnregisterCamera(this);
			}
		}
	}
}

void UCameraComponent::CreateRenderState()
{
	USceneComponent::CreateRenderState();
	EnsureEditorVisualizationMesh();
}

void UCameraComponent::UpdateWorldMatrix() const
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	if (World && World->GetWorldType() == EWorldType::Editor)
	{
		if (USpringArmComponent* SpringArm = Cast<USpringArmComponent>(GetParent()))
		{
			SpringArm->RefreshSpringArm(0.0f, false);
		}
	}

	USceneComponent::UpdateWorldMatrix();
}

void UCameraComponent::PreGetEditableProperties()
{
	USceneComponent::PreGetEditableProperties();
	EnsureEditorVisualizationMesh();
}

const char* UCameraComponent::GetEditorVisualizationMaterialPath() const
{
	return "Content/Material/Editor/EditorCamera_Blue.uasset";
}

namespace
{
	void ConfigureEditorCameraVisualizationMesh(UStaticMeshComponent* MeshComponent, UStaticMesh* Mesh, const char* MaterialPath)
	{
		if (!MeshComponent || !Mesh)
		{
			return;
		}

		MeshComponent->SetHiddenInComponentTree(true);
		if (!MeshComponent->IsEditorOnlyComponent())
		{
			MeshComponent->SetEditorOnlyComponent(true);
		}

		MeshComponent->SetVisibility(true);
		MeshComponent->SetStaticMesh(Mesh);
		MeshComponent->SetMaterial(0, FMaterialManager::Get().GetOrCreateMaterial(MaterialPath));
		MeshComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 90.0f));
		MeshComponent->SetRelativeScale(FVector(0.01f, 0.01f, 0.01f));
		MeshComponent->SetCastShadow(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->MarkWorldBoundsDirty();
		MeshComponent->MarkRenderStateDirty();
	}

	UStaticMesh* LoadEditorCameraVisualizationMesh(ID3D11Device* Device)
	{
		if (!Device)
		{
			return nullptr;
		}

		if (UStaticMesh* PackageMesh = FMeshManager::LoadStaticMesh("Content/Data/EditorCamera/CameraMesh_StaticMesh.uasset", Device))
		{
			return PackageMesh;
		}

		FImportOptions CameraMeshImportOptions = FImportOptions::Default();
		CameraMeshImportOptions.ForwardAxis = EForwardAxis::Identity;
		CameraMeshImportOptions.WindingOrder = EWindingOrder::Keep;
		return FMeshManager::LoadStaticMesh("Content/Data/EditorCamera/CameraMesh.OBJ", CameraMeshImportOptions, Device);
	}
}

UStaticMeshComponent* UCameraComponent::EnsureEditorVisualizationMesh()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	UWorld* World = OwnerActor->GetWorld();
	if (!World || World->GetWorldType() != EWorldType::Editor)
	{
		return nullptr;
	}

	if (!GEngine)
	{
		return nullptr;
	}

	static UStaticMesh* SharedCameraVizMesh = nullptr;
	if (!SharedCameraVizMesh)
	{
		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
		SharedCameraVizMesh = LoadEditorCameraVisualizationMesh(Device);
	}

	if (!SharedCameraVizMesh)
	{
		return nullptr;
	}

	for (USceneComponent* Child : GetChildren())
	{
		UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Child);
		if (MeshComponent && MeshComponent->IsEditorOnlyComponent())
		{
			ConfigureEditorCameraVisualizationMesh(MeshComponent, SharedCameraVizMesh, GetEditorVisualizationMaterialPath());
			return MeshComponent;
		}
	}

	UStaticMeshComponent* MeshComponent = OwnerActor->AddComponent<UStaticMeshComponent>();
	if (!MeshComponent)
	{
		return nullptr;
	}

	MeshComponent->AttachToComponent(this);
	ConfigureEditorCameraVisualizationMesh(MeshComponent, SharedCameraVizMesh, GetEditorVisualizationMaterialPath());
	return MeshComponent;
}

void UCameraComponent::LookAt(const FVector& Target)
{
	FVector Position = GetWorldLocation();
	FVector Diff = (Target - Position).Normalized();

	constexpr float Rad2Deg = 180.0f / 3.14159265358979f;

	FRotator LookRotation = GetRelativeRotation();
	LookRotation.Pitch = -asinf(Diff.Z) * Rad2Deg;

	if (fabsf(Diff.Z) < 0.999f) {
		LookRotation.Yaw = atan2f(Diff.Y, Diff.X) * Rad2Deg;
	}

	SetRelativeRotation(LookRotation);
}

void UCameraComponent::OnResize(int32 Width, int32 Height)
{
	CameraState.AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
}

void UCameraComponent::SetCameraState(const FCameraState& NewState)
{
	CameraState = NewState;
}

void UCameraComponent::GetCameraView(float /*DeltaTime*/, FMinimalViewInfo& OutPOV) const
{
	UpdateWorldMatrix();
	OutPOV.Location    = GetWorldLocation();
	OutPOV.Rotation    = GetWorldMatrix().ToRotator();
	OutPOV.FOV         = CameraState.FOV;
	OutPOV.AspectRatio = CameraState.AspectRatio;
	OutPOV.OrthoWidth  = CameraState.OrthoWidth;
	OutPOV.NearClip    = CameraState.NearZ;
	OutPOV.FarClip     = CameraState.FarZ;
	OutPOV.bIsOrtho    = CameraState.bIsOrthogonal;
}
