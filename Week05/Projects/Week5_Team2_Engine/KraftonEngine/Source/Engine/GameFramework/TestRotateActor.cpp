#include "GameFramework/TestRotateActor.h"
#include "Component/StaticMeshComponent.h"
#include "Component/RotateComponent.h"
#include "Engine/Runtime/Engine.h"
#include "Mesh/ObjManager.h"

IMPLEMENT_CLASS(ATestRotateActor, AActor)

void ATestRotateActor::InitializeTest(bool bTickInEditorMode)
{
	SetTickInEditor(bTickInEditorMode);

	MeshComponent = AddComponent<UStaticMeshComponent>();
	SetRootComponent(MeshComponent);

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	
	// 기본 큐브 로드 (ObjManager가 .obj에서 .bin으로 자동 변환/캐시 로드함)
	UStaticMesh* CubeMesh = FObjManager::LoadObjStaticMesh("Data/BasicShape/Cube.OBJ", Device);
	if (CubeMesh)
	{
		MeshComponent->SetStaticMesh(CubeMesh);
	}

	RotateComponent = AddComponent<URotateComponent>();
	RotateComponent->SetRotationSpeed(FRotator(0, 90, 0));
}
