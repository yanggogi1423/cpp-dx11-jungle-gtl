#include "Diagnostics/CameraEditorMeshDiagnostics.h"

#include "Component/Camera/CameraComponent.h"
#include "Component/Camera/CineCameraComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Materials/Material.h"
#include "Object/GarbageCollection.h"

#include <cmath>

namespace
{
	constexpr float RotationTolerance = 0.05f;
	constexpr float ScaleTolerance = 0.001f;
	constexpr float ColorTolerance = 0.001f;
	constexpr const char* ExpectedCameraMaterialPath = "Content/Material/Editor/EditorCamera_Blue.uasset";
	constexpr const char* ExpectedCineCameraMaterialPath = "Content/Material/Editor/EditorCineCamera_Black.uasset";
	constexpr const char* ExpectedMaterialShaderPath = "Shaders/Geometry/UberLit.hlsl";
	const FVector4 ExpectedCameraColor(0.0f, 0.12f, 1.0f, 1.0f);
	const FVector4 ExpectedCineCameraColor(0.02f, 0.02f, 0.025f, 1.0f);

	struct FCameraEditorMeshSelfTestContext
	{
		FCameraEditorMeshSelfTestResult Result;

		void Check(bool bCondition, const char* Message)
		{
			++Result.ChecksRun;
			if (bCondition)
			{
				return;
			}

			Result.bPassed = false;
			if (!Result.Message.empty())
			{
				Result.Message += "\n";
			}
			Result.Message += Message ? Message : "unknown failure";
		}
	};

	UWorld* CreateDiagnosticsEditorWorld()
	{
		UWorld* World = UObjectManager::Get().CreateObject<UWorld>();
		if (!World)
		{
			return nullptr;
		}

		World->SetWorldType(EWorldType::Editor);
		World->InitWorld();
		return World;
	}

	int32 CountEditorOnlyStaticMeshChildren(const UCameraComponent* Camera)
	{
		if (!Camera)
		{
			return 0;
		}

		int32 Count = 0;
		for (USceneComponent* Child : Camera->GetChildren())
		{
			if (UStaticMeshComponent* MeshChild = Cast<UStaticMeshComponent>(Child))
			{
				if (MeshChild->IsEditorOnlyComponent())
				{
					++Count;
				}
			}
		}
		return Count;
	}

	bool NearlyEqual(float A, float B, float Tolerance)
	{
		return std::fabs(A - B) <= Tolerance;
	}

	bool NearlyEqualColor(const FVector4& A, const FVector4& B)
	{
		return NearlyEqual(A.X, B.X, ColorTolerance)
			&& NearlyEqual(A.Y, B.Y, ColorTolerance)
			&& NearlyEqual(A.Z, B.Z, ColorTolerance)
			&& NearlyEqual(A.W, B.W, ColorTolerance);
	}

	void ValidateCameraMeshComponent(
		FCameraEditorMeshSelfTestContext& Context,
		UCameraComponent* Camera,
		UStaticMeshComponent* MeshComponent,
		const char* ExpectedMaterialPath,
		const FVector4& ExpectedColor)
	{
		Context.Check(MeshComponent != nullptr, "Camera editor visualization mesh should be created.");
		if (!MeshComponent)
		{
			return;
		}

		Context.Check(MeshComponent->GetParent() == Camera, "Camera editor visualization mesh should be attached to the camera component.");
		Context.Check(MeshComponent->IsEditorOnlyComponent(), "Camera editor visualization mesh should be editor-only.");
		Context.Check(MeshComponent->IsHiddenInComponentTree(), "Camera editor visualization mesh should be hidden from the component tree.");
		Context.Check(MeshComponent->IsVisible(), "Camera editor visualization mesh should be visible in the viewport.");
		Context.Check(MeshComponent->GetStaticMesh() != nullptr, "Camera editor visualization mesh should have a static mesh asset.");
		Context.Check(MeshComponent->GetMaterial(0) != nullptr, "Camera editor visualization mesh should have the editor camera material.");
		Context.Check(
			MeshComponent->GetMaterialPath(0) == ExpectedMaterialPath,
			"Camera editor visualization mesh should use the expected editor camera material path.");

		UMaterial* Material = MeshComponent->GetMaterial(0);
		if (Material)
		{
			Context.Check(
				Material->GetShaderPathForSerialize() == ExpectedMaterialShaderPath,
				"Camera editor visualization material should use the Week13 static mesh UberLit shader.");
			Context.Check(
				Material->IsTwoSided(),
				"Camera editor visualization material should keep the Week13 no-cull rasterizer behavior.");

			FVector4 SectionColor;
			Context.Check(
				Material->GetVector4Parameter("SectionColor", SectionColor) && NearlyEqualColor(SectionColor, ExpectedColor),
				"Camera editor visualization material should keep the Week13 SectionColor.");

			float HasNormalMap = 1.0f;
			Context.Check(
				Material->GetScalarParameter("HasNormalMap", HasNormalMap) && NearlyEqual(HasNormalMap, 0.0f, ColorTolerance),
				"Camera editor visualization material should not request a missing normal map.");
		}

		Context.Check(!MeshComponent->GetCastShadow(), "Camera editor visualization mesh should not cast shadows.");
		Context.Check(
			MeshComponent->GetCollisionEnabled() == ECollisionEnabled::NoCollision,
			"Camera editor visualization mesh should not participate in collision.");

		const FRotator Rotation = MeshComponent->GetRelativeRotation();
		Context.Check(
			NearlyEqual(Rotation.Roll, 90.0f, RotationTolerance),
			"Camera editor visualization mesh should keep the Week13 roll=90 orientation.");

		const FVector Scale = MeshComponent->GetRelativeScale();
		Context.Check(
			NearlyEqual(Scale.X, 0.01f, ScaleTolerance)
				&& NearlyEqual(Scale.Y, 0.01f, ScaleTolerance)
				&& NearlyEqual(Scale.Z, 0.01f, ScaleTolerance),
			"Camera editor visualization mesh should keep the Week13 0.01 uniform scale.");
	}
}

FCameraEditorMeshSelfTestResult FCameraEditorMeshDiagnostics::RunSelfTest()
{
	FScopedGarbageCollectionBlocker GCBlocker;
	FCameraEditorMeshSelfTestContext Context;
	Context.Result.bPassed = true;

	UWorld* World = CreateDiagnosticsEditorWorld();
	Context.Check(World != nullptr, "Camera mesh self-test should create an editor world.");
	if (!World)
	{
		return Context.Result;
	}

	AActor* Actor = World->SpawnActor<AActor>();
	Context.Check(Actor != nullptr, "Camera mesh self-test should spawn a diagnostics actor.");
	if (!Actor)
	{
		return Context.Result;
	}

	Actor->SetFName(FName("CameraEditorMeshSelfTest_Actor"));
	UCameraComponent* Camera = Actor->AddComponent<UCameraComponent>();
	Context.Check(Camera != nullptr, "Camera mesh self-test should add a camera component.");
	if (!Camera)
	{
		return Context.Result;
	}

	Camera->SetFName(FName("CameraEditorMeshSelfTest_Camera"));
	Actor->SetRootComponent(Camera);

	UStaticMeshComponent* MeshComponent = Camera->EnsureEditorVisualizationMesh();
	ValidateCameraMeshComponent(Context, Camera, MeshComponent, ExpectedCameraMaterialPath, ExpectedCameraColor);

	UStaticMeshComponent* ReusedMeshComponent = Camera->EnsureEditorVisualizationMesh();
	Context.Check(ReusedMeshComponent == MeshComponent, "Camera mesh self-test should repair/reuse the existing editor mesh child.");
	Context.Check(
		CountEditorOnlyStaticMeshChildren(Camera) == 1,
		"Camera mesh self-test should not create duplicate editor mesh children.");

	AActor* CineActor = World->SpawnActor<AActor>();
	Context.Check(CineActor != nullptr, "Camera mesh self-test should spawn a cine diagnostics actor.");
	if (CineActor)
	{
		CineActor->SetFName(FName("CameraEditorMeshSelfTest_CineActor"));
		UCineCameraComponent* CineCamera = CineActor->AddComponent<UCineCameraComponent>();
		Context.Check(CineCamera != nullptr, "Camera mesh self-test should add a cine camera component.");
		if (CineCamera)
		{
			CineCamera->SetFName(FName("CameraEditorMeshSelfTest_CineCamera"));
			CineActor->SetRootComponent(CineCamera);
			UStaticMeshComponent* CineMeshComponent = CineCamera->EnsureEditorVisualizationMesh();
			ValidateCameraMeshComponent(
				Context,
				CineCamera,
				CineMeshComponent,
				ExpectedCineCameraMaterialPath,
				ExpectedCineCameraColor);
		}
	}

	if (Context.Result.bPassed && Context.Result.Message.empty())
	{
		Context.Result.Message = "Camera editor visualization mesh self-test passed.";
	}
	return Context.Result;
}
