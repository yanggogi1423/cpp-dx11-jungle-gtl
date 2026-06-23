#pragma once
#include "Object/Object.h"
#include "Render/Pipeline/WorldRenderProxy.h"
#include <memory>

class AActor;
class UWorld;

class ULevel : public UObject
{
public:
	DECLARE_CLASS(ULevel, UObject)
	ULevel();
	ULevel(const ULevel&);		// unique_ptr 필드로 인해 복사 생성자 자동 생성 불가
	~ULevel() override;
	void DuplicateSubObjects() override;

	void SetWorld(UWorld* InWorld) { OwningWorld = InWorld; }
	UWorld* GetWorld() const { return OwningWorld; }

	void AddActor(AActor* Actor);
	void RemoveActor(AActor* Actor);
	const TArray<AActor*>& GetActors() const { return Actors; }

	FWorldRenderProxy& GetRenderProxy() { return *RenderProxy; }

	// Lifecycle
	void BeginPlay();
	void Tick(float DeltaTime);
	void EndPlay();

private:
	UWorld* OwningWorld = nullptr;
	TArray<AActor*> Actors;
	std::unique_ptr<FWorldRenderProxy> RenderProxy;

	bool bHasBegunPlay = false;
};
