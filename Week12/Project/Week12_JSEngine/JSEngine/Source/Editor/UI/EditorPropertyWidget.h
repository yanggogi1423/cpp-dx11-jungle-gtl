
#pragma once
#include "Editor/UI/EditorWidget.h"
#include "Editor/UI/EditorActorSequenceDetails.h"
#include "Editor/Undo/EditorUndoSystem.h"
#include "Object/Object.h"

class FSelectionManager;
class UActorComponent;
class AActor;
class UMaterialInterface;
class UStaticMesh;
struct FProperty;
struct FPropertyHandle;
class FDebugDetailsBuilder;
struct FDebugDetailsItem;

class FEditorPropertyWidget : public FEditorWidget
{
public:
	virtual void Render(float DeltaTime) override;
	void Initialize(UEditorEngine* InEditorEngine) override;

	UActorComponent* GetSelectedComponent() const { return SelectedComponent; }
	bool IsActorSelected() const { return bActorSelected; }

	void ResetSelection();

	void OnActorDestroyed(AActor* Actor);

private:
	// 선택 상태 관리
	void UpdateSelectionState(AActor* PrimaryActor);
	void SelectActorForDetails();
	void SelectComponentForDetails(UActorComponent* Component);

	// 헤더 영역
	void RenderDetailsLockBar(AActor* CurrentSelection, AActor* DisplayActor);
	void RenderActorHeaderRegion(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	void RenderMultiSelectionHeader(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors, int32 SelectionCount);
	void RenderSingleSelectionHeader(AActor* PrimaryActor);
	void RenderDetailsContextMenu(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);

	// 컴포넌트 트리
	void RenderComponentTree(AActor* Actor);
	void RenderSceneComponentNode(AActor* Actor, class USceneComponent* Comp);

	// 디테일 패널
	void RenderDetails(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	void RenderActorProperties(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	void RenderActorTags(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	void RenderComponentTags(UActorComponent* Component);
	void RenderComponentProperties();
	void RenderReflectionProperties(UObject* Object);
	void RenderReflectionPropertiesByCategory(UObject* Object, const TArray<const FProperty*>& Properties);
	void RenderReflectionProperty(const FPropertyHandle& Handle);
	void RenderDebugDetails(UObject* Object, AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	void RenderDebugDetailsItem(const FDebugDetailsItem& Item);
	void RenderPropertyWidget(const FPropertyHandle& Handle);
	bool RenderPropertyValueWidget(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget, const char* Label, int32 ArrayIndex = -1);
	bool RenderObjectPtrWidget(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget, const char* Label, int32 ArrayIndex = -1);
	bool RenderSoftObjectPtrWidget(const FProperty& Property, void* ValuePtr, const char* Label);
	bool RenderArrayPropertyWidget(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget);
	bool RenderStructPropertyWidget(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget, const char* Label);
	void RenderSkeletalStateMachinePreview(class USkeletalMeshComponent* Comp);
	void RenderSkeletalBonePoseDebug(class USkeletalMeshComponent* Comp);
	void RenderInterpControlPoints(class UInterpToMovementComponent* Comp);
	void RenderMaterialPreviewTooltip(UMaterialInterface* Material);
	AActor* ResolveActorStateUndoOwner(UObject* Object) const;
	void BeginActorStatePropertyEdit(UObject* Object, const char* Label);
	void EndActorStatePropertyEdit(UObject* Object, const char* Label);
	void BeginReflectedPropertyEdit(UObject* Object, const FProperty& Property, const char* Label);
	void EndReflectedPropertyEdit(UObject* Object, const FProperty& Property, const char* Label);
	void ResetPropertyEditUndoState();

	// 유틸리티
	void AttachAndSelectNewComponent(AActor* PrimaryActor, UActorComponent* NewComp, class USceneComponent* AttachTargetOverride = nullptr);
	bool CanDeleteComponent(AActor* Owner, UActorComponent* Component) const;
	void DeleteSelectedComponent(AActor* Owner);

	// 이름 변경 및 UI 렌더링
	template<typename T>
	void RenderEditableName(const char* Label, T* TargetObject, bool* bFocusNextFrame = nullptr);

	// 멤버 변수
	UActorComponent* SelectedComponent = nullptr;
	AActor* LastSelectedActor = nullptr;
	AActor* LockedDetailsActor = nullptr;
	UStaticMesh* MaterialSlotPreviewMesh = nullptr;
	FEditorActorSequenceDetails ActorSequenceDetails;
	TMap<uint32, int32> SelectedSkeletalBoneByComponent;
	UActorComponent* LastDetailsPerfComponent = nullptr;
	bool bDetailsLocked = false;
	bool bActorSelected   = true; // true: Actor details, false: Component details
	bool bDetailsPerfTraceFrame = false;
	bool bOpenDetailsContextMenu = false;
	bool bPropertyEditUndoCaptured = false;
	bool bPropertyEditUsesActorState = false;
	bool bPropertyEditUsesReflectedProperty = false;
	bool bTransformFieldEditUndoCaptured = false;
	bool bFocusActorNameNextFrame = false;
	bool bFocusComponentNameNextFrame = false;
	float LastDeltaTime = 0.0f;
	char NewActorTagBuffer[128] = "";
	char NewComponentTagBuffer[128] = "";
	UObject* PropertyEditTargetObject = nullptr;
	const FProperty* PropertyEditTargetProperty = nullptr;
	TArray<FEditorSerializedActorState> PropertyEditBeforeActorStates;
	FEditorReflectedPropertyState PropertyEditBeforeReflectedState;
	TArray<FEditorActorTransformState> TransformFieldBeforeActorStates;
	bool bSkeletalBonePoseEditUndoCaptured = false;
	USkeletalMeshComponent* SkeletalBonePoseEditComponent = nullptr;
	int32 SkeletalBonePoseEditBoneIndex = -1;
	FEditorSkeletalBonePoseState SkeletalBonePoseBeforeState;

	// for skeletal mesh bone pose debug
	/**
	 * @brief bind pose를 기준으로 얼마나 움직였는지에 대한 offset
	 */
	struct FSkeletalBonePoseEditState
	{
		uint32 MeshId = 0;
		int32 BoneIndex = -1;
		FVector LocationOffset = FVector::ZeroVector;
		FVector RotationOffset = FVector::ZeroVector;
		FVector ScaleOffset = FVector::OneVector;
	};

	/**
	 * @brief 같은 USkeletalMeshComponent 안에서도 bone마다 UI 편집 상태를 따로 기억하기 위한 캐시
	 */
	TMap<uint32, TMap<int32, FSkeletalBonePoseEditState>> SkeletalBonePoseEditStatesByComponent;

	TMap<uint32, float> StateMachinePreviewBlendTimeByComponent;
};
