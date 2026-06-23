#include "SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationStateMachine.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimStateMachineInstance.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Object/ObjectFactory.h"
#include "Animation/LuaAnimInstance.h"
#include "Animation/AnimLuaProgramAsset.h"

#include <cstring>


void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
    USkinnedMeshComponent::Serialize(Ar);

    Ar << "Enable Animation" << bEnableAnimation;

    int32 AnimationModeValue = static_cast<int32>(AnimationMode);
    Ar << "Animation Mode" << AnimationModeValue;
    if (Ar.IsLoading())
    {
        AnimationMode = static_cast<EAnimationMode>(AnimationModeValue);
    }

    if (Ar.IsLoading() && Ar.IsJson() && Ar.HasKey("Lua Anim Program Path") && !Ar.HasKey("Lua Anim Program Asset Path"))
    {
        Ar << "Lua Anim Program Path" << LuaAnimProgramAssetPath;
    }
    else
    {
        if (!Ar.IsLoading() && LuaAnimProgramAsset)
        {
            LuaAnimProgramAssetPath = LuaAnimProgramAsset->GetAssetPathFileName();
        }
        Ar << "Lua Anim Program Asset Path" << LuaAnimProgramAssetPath;
    }
    Ar << "AnimationStateMachineAsset" << AnimationStateMachinePath;
    Ar << "AnimationAsset" << AnimationSequencePath;
    Ar << "LoopAnimation" << bLoopAnimation;
    Ar << "AutoPlayAnimation" << bAutoPlayAnimation;

    if (Ar.IsLoading())
    {
        ClearAnimScriptInstance();
        if (bEnableAnimation)
        {
            InitializeAnimScriptInstance();
            if (AnimationMode == EAnimationMode::AnimationSingleNode)
            {
                LoadConfiguredAnimation(bAutoPlayAnimation);
            }
            else if (AnimationMode == EAnimationMode::AnimationStateMachine)
            {
                ResetToBindPose();
            }
            else
            {
                RefreshBoneTransformsFromAnimation();
            }
        }
    }
}

void USkeletalMeshComponent::BeginPlay()
{
    USkinnedMeshComponent::BeginPlay();

    if (!bEnableAnimation)
    {
        return;
    }

    if (!AnimInstance && AnimationMode != EAnimationMode::None)
    {
        InitializeAnimScriptInstance();
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode && bAutoPlayAnimation)
    {
        LoadConfiguredAnimation(true);
    }
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    USkinnedMeshComponent::TickComponent(DeltaTime);

    TickAnimation(DeltaTime);
    RefreshBoneTransformsFromAnimation();

    EnsureSkinningUpdated();

    ConditionallyDispatchQueuedAnimEvents();
}

void USkeletalMeshComponent::PostDuplicate(UObject* Original)
{
    USkinnedMeshComponent::PostDuplicate(Original);

    USkeletalMeshComponent* SourceComponent = Cast<USkeletalMeshComponent>(Original);
    if (!SourceComponent)
    {
        return;
    }

    AnimationSequence = SourceComponent->AnimationSequence;
    AnimationSequencePath = SourceComponent->AnimationSequencePath;
    LuaAnimProgramAssetPath = SourceComponent->LuaAnimProgramAssetPath;
    LuaAnimProgramAsset = SourceComponent->LuaAnimProgramAsset;
    AnimationStateMachinePath = SourceComponent->AnimationStateMachinePath;
    bNeedsQueuedAnimEventsDispatched = false;

    ClearAnimScriptInstance();

    if (!bEnableAnimation || AnimationMode == EAnimationMode::None)
    {
        return;
    }

    InitializeAnimScriptInstance();

    if (AnimationMode == EAnimationMode::AnimationSingleNode)
    {
        LoadConfiguredAnimation(bAutoPlayAnimation);
    }
    else if (AnimationMode == EAnimationMode::AnimationStateMachine)
    {
        ResetToBindPose();
    }
    else
    {
        RefreshBoneTransformsFromAnimation();
    }
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
    USkinnedMeshComponent::PostEditProperty(PropertyName);

    if (std::strcmp(PropertyName, "bEnableAnimation") == 0 ||
        std::strcmp(PropertyName, "AnimationMode") == 0 ||
        std::strcmp(PropertyName, "LuaAnimProgramAssetPath") == 0 ||
        std::strcmp(PropertyName, "LuaAnimProgramAsset") == 0 ||
        std::strcmp(PropertyName, "AnimationStateMachinePath") == 0 ||
        std::strcmp(PropertyName, "SkeletalMesh") == 0)
    {
        if (std::strcmp(PropertyName, "LuaAnimProgramAsset") == 0)
        {
            LuaAnimProgramAssetPath = LuaAnimProgramAsset
                ? LuaAnimProgramAsset->GetAssetPathFileName()
                : FString();
        }

        ClearAnimScriptInstance();

        if (bEnableAnimation)
        {
            InitializeAnimScriptInstance();
            if (AnimationMode == EAnimationMode::AnimationSingleNode)
            {
                LoadConfiguredAnimation(bAutoPlayAnimation);
            }
            else
            {
                RefreshBoneTransformsFromAnimation();
            }
        }
    }
    else if (std::strcmp(PropertyName, "AnimationSequence") == 0 ||
             std::strcmp(PropertyName, "bLoopAnimation") == 0 ||
             std::strcmp(PropertyName, "bAutoPlayAnimation") == 0)
    {
        RefreshAnimationState(bAutoPlayAnimation);
    }
}

void USkeletalMeshComponent::ResetToBindPose()
{
    InitializePoseFromBindPose();
    MarkSkinningDirty();
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FMatrix& NewLocalTransform)
{
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(CurrentLocalPose.size()))
    {
        return;
    }

    CurrentLocalPose[BoneIndex] = NewLocalTransform;
    UpdateCurrentGlobalPose();
    MarkSkinningDirty();
}

const FMatrix& USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
    // fallback은 identity
    static const FMatrix Identity = FMatrix::Identity;

    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(CurrentLocalPose.size()))
    {
        return Identity;
    }

    return CurrentLocalPose[BoneIndex];
}

FMatrix USkeletalMeshComponent::GetBoneGlobalTransform(int32 BoneIndex) const
{
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(CurrentGlobalPose.size()))
    {
        return FMatrix::Identity;
    }

    return CurrentGlobalPose[BoneIndex] * GetWorldMatrix();
}

void USkeletalMeshComponent::SetBoneGlobalTransform(int32 BoneIndex, const FMatrix& NewGlobalTransform)
{
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(CurrentLocalPose.size()))
    {
        return;
    }

    if (!SkeletalMesh)
    {
        return;
    }

    const TArray<FBoneInfo>& Bones = SkeletalMesh->GetBones();
    if (BoneIndex >= static_cast<int32>(Bones.size()))
    {
        return;
    }

    int32 ParentIndex = Bones[BoneIndex].ParentIndex;

    FMatrix ParentGlobalTransform;
    if (ParentIndex >= 0)
    {
        ParentGlobalTransform = CurrentGlobalPose[ParentIndex] * GetWorldMatrix();
    }
    else
    {
        ParentGlobalTransform = GetWorldMatrix();
    }

    // Local = Global * ParentGlobal.Inverse
    FMatrix NewLocalTransform = NewGlobalTransform * ParentGlobalTransform.GetInverse();
    SetBoneLocalTransform(BoneIndex, NewLocalTransform);
}

void USkeletalMeshComponent::TickAnimation(float DeltaTime)
{
    if (!bEnableAnimation)
    {
        return;
    }

    if (!AnimInstance || !SkeletalMesh)
    {
        return;
    }

    AnimInstance->UpdateAnimation(DeltaTime);
    bNeedsQueuedAnimEventsDispatched = true;
}

void USkeletalMeshComponent::ConditionallyDispatchQueuedAnimEvents()
{
    if (!bNeedsQueuedAnimEventsDispatched)
    {
        return;
    }

    bNeedsQueuedAnimEventsDispatched = false;

    if (AnimInstance)
    {
        AnimInstance->DispatchQueuedAnimEvents();
    }
}

void USkeletalMeshComponent::RefreshBoneTransformsFromAnimation()
{
    if (!AnimInstance || !SkeletalMesh)
    {
        return;
    }

    TArray<FMatrix> EvaluatedLocalPose;
    if (AnimInstance->EvaluateAnimation(SkeletalMesh, EvaluatedLocalPose))
    {
        ApplyAnimationLocalPose(EvaluatedLocalPose);
    }
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* AnimToPlay, bool bLooping)
{
    if (!bEnableAnimation)
    {
        return;
    }

    SetAnimationMode(EAnimationMode::AnimationSingleNode);
    SetAnimation(AnimToPlay);
    Play(bLooping);
}

void USkeletalMeshComponent::SetAnimationMode(EAnimationMode InAnimationMode, bool bForceInitAnimInstance)
{
    if (!bEnableAnimation)
    {
        return;
    }

    const bool bNeedChange = AnimationMode != InAnimationMode;

    if (bNeedChange)
    {
        AnimationMode = InAnimationMode;
        ClearAnimScriptInstance();
    }

    const bool bShouldForceBlueprintReinit =
        AnimationMode == EAnimationMode::AnimationBlueprint && bForceInitAnimInstance;
    const bool bNeedsInstance = AnimationMode != EAnimationMode::None && !AnimInstance;

    if (SkeletalMesh && (bNeedChange || bShouldForceBlueprintReinit || bNeedsInstance))
    {
        InitializeAnimScriptInstance();
    }
}

void USkeletalMeshComponent::ClearAnimScriptInstance()
{
    if (AnimInstance)
    {
        AnimInstance->UninitializeAnimation();
        UObjectManager::Get().DestroyObject(AnimInstance);
        AnimInstance = nullptr;
    }
}

bool USkeletalMeshComponent::InitializeAnimScriptInstance()
{
    if (!SkeletalMesh || !SkeletalMesh->HasValidMeshData())
    {
        return false;
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode)
    {
        UAnimSingleNodeInstance* SingleNode =
            UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>();

        if (!SingleNode)
        {
            return false;
        }

        SingleNode->InitializeAnimation(this);

        AnimInstance = SingleNode;
        return true;
    }

	if (AnimationMode == EAnimationMode::AnimationLua)
    {
        if (LuaAnimProgramAssetPath.empty() && LuaAnimProgramAsset)
        {
            LuaAnimProgramAssetPath = LuaAnimProgramAsset->GetAssetPathFileName();
        }

        if (LuaAnimProgramAssetPath.empty())
        {
            return false;
        }

        ULuaAnimInstance* LuaInstance =
            UObjectManager::Get().CreateObject<ULuaAnimInstance>();

        if (!LuaInstance)
        {
            return false;
        }

        LuaInstance->SetLuaAnimProgramAssetPath(LuaAnimProgramAssetPath);
        LuaInstance->InitializeAnimation(this);

        AnimInstance = LuaInstance;
        return true;
    }

    if (AnimationMode == EAnimationMode::AnimationBlueprint)
    {
        // 나중에 AnimBlueprint/StateMachine 기반 인스턴스를 만들 자리.
        // 지금은 비워두거나 기본 UAnimInstance 생성.
        UAnimInstance* NewInstance =
            UObjectManager::Get().CreateObject<UAnimInstance>();

        if (!NewInstance)
        {
            return false;
        }

        NewInstance->InitializeAnimation(this);

        AnimInstance = NewInstance;
        return true;
    }

    if (AnimationMode == EAnimationMode::AnimationStateMachine)
    {
        UAnimStateMachineInstance* StateMachineInstance =
            UObjectManager::Get().CreateObject<UAnimStateMachineInstance>();

        if (!StateMachineInstance)
        {
            return false;
        }

        UAnimationStateMachine* StateMachine = AnimationStateMachinePath.empty()
            ? nullptr
            : FResourceManager::Get().LoadAnimationStateMachine(AnimationStateMachinePath);

        StateMachineInstance->SetStateMachine(StateMachine, AnimationStateMachinePath);
        StateMachineInstance->InitializeAnimation(this);

        AnimInstance = StateMachineInstance;
        return true;
    }

    return false;
}

void USkeletalMeshComponent::Play(bool bLooping)
{
    if (!bEnableAnimation)
    {
        return;
    }

    UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance);
    if (!SingleNode)
    {
        return;
    }

    SingleNode->SetLooping(bLooping);
    SingleNode->SetPlaying(true);
}

void USkeletalMeshComponent::StopAnimation()
{
    UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance);
    if (!SingleNode)
    {
        return;
    }

    SingleNode->StopAnim();
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset* AnimToPlay)
{
    if (!bEnableAnimation)
    {
        return;
    }

    UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance);
    if (!SingleNode)
    {
        SetAnimationMode(EAnimationMode::AnimationSingleNode);
        SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance);
        if (!SingleNode)
        {
            return;
        }
    }

    AnimationSequence = Cast<UAnimSequence>(AnimToPlay);
    AnimationSequencePath = AnimationSequence ? AnimationSequence->GetAssetPathFileName() : FString();

    SingleNode->SetAnimationAsset(AnimToPlay);
    SingleNode->SetPlaying(false);
}

bool USkeletalMeshComponent::LoadConfiguredAnimation(bool bStartPlayback)
{
    if (AnimationSequencePath.empty())
    {
        StopAnimation();
        return false;
    }

    if (!SkeletalMesh || !SkeletalMesh->HasValidMeshData())
    {
        StopAnimation();
        return false;
    }

    UAnimSequence* LoadedAnimation = FResourceManager::Get().LoadAnimSequence(AnimationSequencePath, SkeletalMesh, 0);

    if (!LoadedAnimation)
    {
        StopAnimation();
        return false;
    }

	AnimationSequence = LoadedAnimation;
	AnimationSequencePath = LoadedAnimation->GetAssetPathFileName();

    SetAnimationMode(EAnimationMode::AnimationSingleNode);
    SetAnimation(LoadedAnimation);

    if (bStartPlayback)
    {
        Play(bLoopAnimation);
    }

    return true;
}

void USkeletalMeshComponent::RefreshAnimationState(bool bStartPlayback)
{
	if (AnimationSequence)
	{
		AnimationSequencePath = AnimationSequence->GetAssetPathFileName();

		if (bStartPlayback)
		{
			PlayAnimation(AnimationSequence, bLoopAnimation);
		}
		else
		{
            SetAnimationMode(EAnimationMode::AnimationSingleNode);
            SetAnimation(AnimationSequence);
		}

		MarkSkinningDirty();
		return;
	}

	AnimationSequencePath.clear();
	AnimationSequence = nullptr;
	MarkSkinningDirty();
}

//AnimationLocalPose == UAnimSequence::EvaluateLocalPose()가 만든 결과
//현재 애니메이션 Pose의 bone local transform
bool USkeletalMeshComponent::ApplyAnimationLocalPose(const TArray<FMatrix>& AnimationLocalPose)
{
    if (!SkeletalMesh)
        return false;

    const int32 BoneCount = static_cast<int32>(SkeletalMesh->GetBones().size());
    if (AnimationLocalPose.size() != BoneCount)
        return false;

    CurrentLocalPose = AnimationLocalPose;
    UpdateCurrentGlobalPose();
    MarkSkinningDirty();
    return true;
}

void USkeletalMeshComponent::SetLuaAnimProgramAssetPath(const FString& InAssetPath)
{
    if (LuaAnimProgramAssetPath == InAssetPath && AnimationMode == EAnimationMode::AnimationLua)
    {
        return;
    }

    LuaAnimProgramAssetPath = InAssetPath;
    LuaAnimProgramAsset = nullptr;
    SetAnimationMode(EAnimationMode::AnimationLua);
}

void USkeletalMeshComponent::SetAnimationStateMachineAssetPath(const FString& InAssetPath)
{
    AnimationStateMachinePath = FPaths::Normalize(InAssetPath);

    UAnimStateMachineInstance* StateMachineInstance = Cast<UAnimStateMachineInstance>(AnimInstance);
    if (StateMachineInstance)
    {
        UAnimationStateMachine* StateMachine = AnimationStateMachinePath.empty()
            ? nullptr
            : FResourceManager::Get().LoadAnimationStateMachine(AnimationStateMachinePath);
        StateMachineInstance->SetStateMachine(StateMachine, AnimationStateMachinePath);
    }
}
