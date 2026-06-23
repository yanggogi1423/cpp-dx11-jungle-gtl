#pragma once

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSequence.h"
#include "Component/SkinnedMeshComponent.h"
#include "Engine/Animation/Notify.h"

class UAnimationAsset;
class UAnimInstance;
class UAnimLuaProgramAsset;
class UAnimSequence;
class UAnimSingleNodeInstance;
class USkeletalMeshComponent;

UENUM()
enum class EAnimationMode : int32
{
    None,
    AnimationSingleNode,
    AnimationBlueprint,
    AnimationLua,
    AnimationStateMachine
};

/**
 * @brief Unreal Engine 스타일에서는 skinned mesh가 skeleton을 이용하는 mesh를 표현하고,
 *        skeletal mesh는 실제로 actor에 붙어서 애니메이션을 붙일 수 있는 component로 사용되고 있으므로
 *        USkeletalMeshComponent 또한 해당 방식대로 우선은 얇게 유지.
 *        핵심 로직들은 대부분 USkinnedMeshComponent로 옮겼습니다.
 */
UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
    GENERATED_BODY(USkeletalMeshComponent, USkinnedMeshComponent)
public:
    USkeletalMeshComponent() = default;
    ~USkeletalMeshComponent() override = default;

    void Serialize(FArchive& Ar) override;
    void BeginPlay() override;
    void TickComponent(float DeltaTime) override;
    void PostDuplicate(UObject* Original) override;
    void PostEditProperty(const char* PropertyName) override;

    EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SkeletalMesh; }

    void ResetToBindPose();

    void SetBoneLocalTransform(int32 BoneIndex, const FMatrix& NewLocalTransform);
    const FMatrix& GetBoneLocalTransform(int32 BoneIndex) const;

    FMatrix GetBoneGlobalTransform(int32 BoneIndex) const;
    void SetBoneGlobalTransform(int32 BoneIndex, const FMatrix& NewGlobalTransform);

public:
    // Single Node Play
    void PlayAnimation(UAnimationAsset* AnimToPlay, bool bLooping);
    void StopAnimation();
    bool ApplyAnimationLocalPose(const TArray<FMatrix>& AnimationLocalPose);

public:
    void SetAnimationMode(EAnimationMode InAnimationMode, bool bForceInitAnimInstance = true);
    void SetAnimation(UAnimationAsset* AnimToPlay);
    void Play(bool bLooping);

public:
    void SetLuaAnimProgramAssetPath(const FString& InAssetPath);
    const FString& GetLuaAnimProgramAssetPath() const { return LuaAnimProgramAssetPath; }
    void SetAnimationStateMachineAssetPath(const FString& InAssetPath);
    const FString& GetAnimationStateMachineAssetPath() const { return AnimationStateMachinePath; }
    UAnimInstance* GetAnimInstance() const { return AnimInstance; }

protected:
    void ClearAnimScriptInstance();
    bool InitializeAnimScriptInstance();

    void TickAnimation(float DeltaTime);
    void RefreshBoneTransformsFromAnimation();
    void ConditionallyDispatchQueuedAnimEvents();
    bool LoadConfiguredAnimation(bool bStartPlayback);
    void RefreshAnimationState(bool bStartPlayback);

protected:
    UPROPERTY(EditAnywhere, Category = "Animation", DisplayName = "Enable Animation")
    bool bEnableAnimation = true;
    bool bNeedsQueuedAnimEventsDispatched = false;

    UPROPERTY(EditAnywhere, Category = "Animation", DisplayName = "Animation")
    UAnimSequence* AnimationSequence = nullptr;
    FString AnimationSequencePath;

    UPROPERTY(EditAnywhere, Category = "Animation", DisplayName = "Loop Animation")
    bool bLoopAnimation = false;
    UPROPERTY(EditAnywhere, Category = "Animation", DisplayName = "Auto Play Animation")
    bool bAutoPlayAnimation = false;

    UPROPERTY(EditAnywhere, Category = "Animation", DisplayName = "Animation Mode")
    EAnimationMode AnimationMode = EAnimationMode::None;
    UAnimInstance* AnimInstance = nullptr;

    UPROPERTY(EditAnywhere, Category = "Animation", DisplayName = "Lua Anim Program")
    UAnimLuaProgramAsset* LuaAnimProgramAsset = nullptr;
    FString LuaAnimProgramAssetPath;

    FString AnimationStateMachinePath;
};
