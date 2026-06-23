#pragma once
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Component/SceneComponent.h"

#include <type_traits>

class UWorld;
class ULevel;
class UPrimitiveComponent;

class AActor : public UObject
{
public:
	DECLARE_CLASS(AActor, UObject)
	AActor() = default;
	~AActor() override;
	void DuplicateSubObjects() override;

	virtual void BeginPlay() {}
	virtual void Tick(float DeltaTime);
	virtual void EndPlay() {}

	// 컴포넌트 생성 + Owner 설정 + 등록만 수행. Attach는 별도로 호출할 것.
	template<typename T>
	T* AddComponent() {
		static_assert(std::is_base_of_v<UActorComponent, T>,
			"AddComponent<T>: T must derive from UActorComponent");

		T* Comp = UObjectManager::Get().CreateObject<T>();
		Comp->SetOwner(this);
		OwnedComponents.push_back(Comp);
		bPrimitiveCacheDirty = true;

		if (GetLevel())
		{
			Comp->OnRegister();
		}

		return Comp;
	}

	// FTypeInfo 기반 런타임 컴포넌트 생성
	UActorComponent* AddComponentByClass(const FTypeInfo* Class);

	void RemoveComponent(UActorComponent* Component);

	// 외부에서 생성된 컴포넌트를 등록 (역직렬화 등)
	void RegisterComponent(UActorComponent* Comp);

	void SetRootComponent(USceneComponent* Comp);
	USceneComponent* GetRootComponent() const { return RootComponent; }

	const TArray<UActorComponent*>& GetComponents() const { return OwnedComponents; }

	// Transform — Location
	FVector GetActorLocation() const;
	void SetActorLocation(const FVector& Location);
	void AddActorWorldOffset(const FVector& Delta);

	// Transform — Rotation
	FRotator GetActorRotation() const;
	void SetActorRotation(const FRotator& NewRotation);
	void SetActorRotation(const FVector& EulerRotation);

	// Transform — Scale
	FVector GetActorScale() const;
	void SetActorScale(const FVector& NewScale);

	// Direction
	FVector GetActorForward() const;

	void SetWorld(UWorld* World) { OwningWorld = World; }
	UWorld* GetWorld() const { return OwningWorld; }

	void SetLevel(ULevel* Level) { OwningLevel = Level; }
	ULevel* GetLevel() const { return OwningLevel; }

	bool IsVisible() const { return bVisible; }
	void SetVisible(bool Visible) { bVisible = Visible; }
	
	bool IsTickInEditor() const { return bTickInEditor; }
	void SetTickInEditor(bool TickEnable) { bTickInEditor = TickEnable; }

	virtual void RegisterAllComponents();
	virtual void UnregisterAllComponents();

	const TArray<UPrimitiveComponent*>& GetPrimitiveComponents() const;

protected:
	USceneComponent* RootComponent = nullptr;
	UWorld* OwningWorld = nullptr;
	ULevel* OwningLevel = nullptr;

	FVector PendingActorLocation = FVector(0, 0, 0);
	bool bVisible = true;
	bool bTickInEditor = false;

	TArray<UActorComponent*> OwnedComponents;

	// 렌더링용 캐시
	mutable TArray<UPrimitiveComponent*> PrimitiveCache;
	mutable bool bPrimitiveCacheDirty = true;
};