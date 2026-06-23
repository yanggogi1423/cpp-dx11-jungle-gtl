#pragma once
#include "GameFramework/AActor.h"

class ATestRotateActor : public AActor
{
public:
	DECLARE_CLASS(ATestRotateActor, AActor)
	ATestRotateActor() = default;

	// 테스트용 초기화 함수
	void InitializeTest(bool bTickInEditorMode);

protected:
	class UStaticMeshComponent* MeshComponent = nullptr;
	class URotateComponent* RotateComponent = nullptr;
};
