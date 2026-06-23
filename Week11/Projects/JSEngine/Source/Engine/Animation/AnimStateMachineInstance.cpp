#include "Animation/AnimStateMachineInstance.h"

#include "Animation/AnimationStateMachine.h"
#include "Animation/AnimSequence.h"
#include "Animation/StateDatas/StateMachineDefs.h"
#include "Asset/SkeletalMesh.h"
#include "Component/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Math/Quat.h"
#include <cmath>

namespace
{
float GetBasisDeterminant(const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis)
{
    return FVector::DotProduct(FVector::CrossProduct(XAxis, YAxis), ZAxis);
}

enum class ESignedScaleAxis : int32
{
    None = -1,
    X = 0,
    Y = 1,
    Z = 2
};

ESignedScaleAxis ResolveNegativeScaleAxis(ESignedScaleAxis PreferredAxis)
{
    return PreferredAxis == ESignedScaleAxis::None ? ESignedScaleAxis::Y : PreferredAxis;
}

void ApplyNegativeScaleAxis(
    ESignedScaleAxis PreferredAxis,
    FVector& XAxis,
    FVector& YAxis,
    FVector& ZAxis,
    FVector& OutScale)
{
    switch (ResolveNegativeScaleAxis(PreferredAxis))
    {
    case ESignedScaleAxis::X:
        XAxis = -XAxis;
        OutScale.X = -OutScale.X;
        break;
    case ESignedScaleAxis::Y:
        YAxis = -YAxis;
        OutScale.Y = -OutScale.Y;
        break;
    case ESignedScaleAxis::Z:
        ZAxis = -ZAxis;
        OutScale.Z = -OutScale.Z;
        break;
    default:
        break;
    }
}

ESignedScaleAxis GetPreferredNegativeScaleAxisFromBindPose(
    const USkeletalMesh* SkeletalMesh,
    size_t BoneIndex,
    float Tolerance = 1.e-8f)
{
    if (!SkeletalMesh)
    {
        return ESignedScaleAxis::Y;
    }

    const TArray<FBoneInfo>& Bones = SkeletalMesh->GetBones();
    if (BoneIndex >= Bones.size())
    {
        return ESignedScaleAxis::Y;
    }

    const FMatrix& BindTransform = Bones[BoneIndex].LocalBindTransform;
    const FVector XAxis = BindTransform.GetScaledAxis(EAxis::X);
    const FVector YAxis = BindTransform.GetScaledAxis(EAxis::Y);
    const FVector ZAxis = BindTransform.GetScaledAxis(EAxis::Z);
    if (XAxis.Size() <= Tolerance || YAxis.Size() <= Tolerance || ZAxis.Size() <= Tolerance)
    {
        return ESignedScaleAxis::Y;
    }

    // Keep runtime transition blends in sync with FBX import: reflected bind poses use
    // a stable Y signed-scale axis instead of choosing the smallest axis per frame.
    return GetBasisDeterminant(XAxis, YAxis, ZAxis) < 0.0f
        ? ESignedScaleAxis::Y
        : ESignedScaleAxis::None;
}

bool DecomposeWithSignedScaleForBlend(
    const FMatrix& Matrix,
    FVector& OutTranslation,
    FQuat& OutRotation,
    FVector& OutScale,
    ESignedScaleAxis PreferredNegativeAxis,
    float Tolerance = 1.e-8f)
{
    OutTranslation = Matrix.GetTranslation();

    FVector XAxis = Matrix.GetScaledAxis(EAxis::X);
    FVector YAxis = Matrix.GetScaledAxis(EAxis::Y);
    FVector ZAxis = Matrix.GetScaledAxis(EAxis::Z);

    const float ScaleX = XAxis.Size();
    const float ScaleY = YAxis.Size();
    const float ScaleZ = ZAxis.Size();
    if (ScaleX <= Tolerance || ScaleY <= Tolerance || ScaleZ <= Tolerance)
    {
        OutRotation = FQuat::Identity;
        OutScale = FVector::OneVector;
        return false;
    }

    XAxis = XAxis / ScaleX;
    YAxis = YAxis / ScaleY;
    ZAxis = ZAxis / ScaleZ;
    OutScale = FVector(ScaleX, ScaleY, ScaleZ);

    //Rotation에 Reflection이 끼어들어있는지 확인
    //negative Scale이 홀수 개 있으면 basis의 handedness가 뒤집힘
    if (GetBasisDeterminant(XAxis, YAxis, ZAxis) < 0.0f)
    {
        ApplyNegativeScaleAxis(PreferredNegativeAxis, XAxis, YAxis, ZAxis, OutScale);
    }

    //Determinant가 0에 가까우면 차원이 축소되는것 --> 잘못된 회전
    const float RotationDeterminant = GetBasisDeterminant(XAxis, YAxis, ZAxis);
    if (RotationDeterminant <= 0.0f || std::fabs(RotationDeterminant - 1.0f) > 1.e-3f)
    {
        OutRotation = FQuat::Identity;
        return false;
    }

    FMatrix RotationMatrix = FMatrix::Identity;
    RotationMatrix.SetAxes(XAxis, YAxis, ZAxis, FVector::ZeroVector);
    OutRotation = FQuat(RotationMatrix).GetNormalized();
    return true;
}

void BlendLocalMatrixPose(
    const TArray<FMatrix>& A,
    const TArray<FMatrix>& B,
    const USkeletalMesh* SkeletalMesh,
    float Alpha,
    TArray<FMatrix>& OutPose)
{
    if (A.size() != B.size())
    {
        OutPose = Alpha >= 0.5f ? B : A;
        return;
    }

    const float ClampedAlpha = MathUtil::Clamp(Alpha, 0.0f, 1.0f);
    OutPose.resize(A.size());

    for (size_t BoneIndex = 0; BoneIndex < A.size(); ++BoneIndex)
    {
        FVector TranslationA;
        FVector TranslationB;
        FQuat RotationA = FQuat::Identity;
        FQuat RotationB = FQuat::Identity;
        FVector ScaleA;
        FVector ScaleB;
        const ESignedScaleAxis PreferredNegativeAxis =
            GetPreferredNegativeScaleAxisFromBindPose(SkeletalMesh, BoneIndex);

        if (!DecomposeWithSignedScaleForBlend(A[BoneIndex], TranslationA, RotationA, ScaleA, PreferredNegativeAxis) ||
            !DecomposeWithSignedScaleForBlend(B[BoneIndex], TranslationB, RotationB, ScaleB, PreferredNegativeAxis))
        {
            OutPose[BoneIndex] = ClampedAlpha >= 0.5f ? B[BoneIndex] : A[BoneIndex];
            continue;
        }

        const FVector BlendedTranslation = FVector::Lerp(TranslationA, TranslationB, ClampedAlpha);
        const FQuat BlendedRotation = FQuat::Slerp(RotationA, RotationB, ClampedAlpha);
        const FVector BlendedScale = FVector::Lerp(ScaleA, ScaleB, ClampedAlpha);

        OutPose[BoneIndex] = FMatrix::MakeTRS(BlendedTranslation, BlendedRotation.ToMatrix(), BlendedScale);
    }
}
} // namespace

void UAnimStateMachineInstance::SetStateMachine(UAnimationStateMachine* InStateMachine, const FString& InAssetPath)
{
    StateMachine = InStateMachine;
    StateMachinePath = InStateMachine ? FPaths::Normalize(InAssetPath) : FString();
    InitializeStateMachineRuntime();
    RebuildSequencePlayers();
}

void UAnimStateMachineInstance::NativeInitializeAnimation()
{
    if (!StateMachine && !StateMachinePath.empty())
    {
        StateMachine = FResourceManager::Get().LoadAnimationStateMachine(StateMachinePath);
    }

    InitializeStateMachineRuntime();
    RebuildSequencePlayers();
}

void UAnimStateMachineInstance::NativeUninitializeAnimation()
{
    SequencePlayers.clear();
    Runtime.Reset(FName::None);
}

void UAnimStateMachineInstance::UpdateAnimGraph(float DeltaTime)
{
    if (!StateMachine)
    {
        return;
    }

    const bool bWasInTransition = Runtime.Transition.bActive;
    const FName PreviousTransitionFrom = Runtime.Transition.FromState;
    const FName PreviousTransitionTo = Runtime.Transition.ToState;
    if (const FAnimSequencePlayerRuntime* CurrentPlayer = FindPlayer(Runtime.CurrentState))
    {
        Runtime.StateNormalizedTime = CurrentPlayer->GetNormalizedTime();
    }
    else
    {
        Runtime.StateNormalizedTime = 0.0f;
    }

    //Runtime객체 업데이트(실제 전이가 일어남). 즉, Runtime.CurrentState가 바뀔 수 있음.
    //Runtime.StateTime과 Runtime.Trasition.ElapsedTime 두개 다 업데이트 및 시간 초과 확인 후 상태업데이트
    StateMachine->UpdateRuntime(Runtime, Parameters, DeltaTime);

    //이번 프레임에서 Transition에 들어왔으면 둘다 play
    if (!bWasInTransition && Runtime.Transition.bActive)
    {
        if (FAnimSequencePlayerRuntime* FromPlayer = FindPlayer(Runtime.Transition.FromState))
        {
            FromPlayer->bPlaying = true;
        }
        if (FAnimSequencePlayerRuntime* ToPlayer = FindPlayer(Runtime.Transition.ToState))
        {
            const FAnimTransitionDef* TransitionDef =
                FindTransitionDef(Runtime.Transition.FromState, Runtime.Transition.ToState);
            if (!TransitionDef || TransitionDef->bResetTime)
            {
                ToPlayer->Reset();
            }
            ToPlayer->bPlaying = true;
        }
    }

    //상태 전이 중이라면
    if (Runtime.Transition.bActive)
    {
        if (FAnimSequencePlayerRuntime* FromPlayer = FindPlayer(Runtime.Transition.FromState))
        {
            AdvanceAndQueueSequencePlayer(*FromPlayer, DeltaTime, 1.0f - Runtime.Transition.GetAlpha());
        }
        if (FAnimSequencePlayerRuntime* ToPlayer = FindPlayer(Runtime.Transition.ToState))
        {
            AdvanceAndQueueSequencePlayer(*ToPlayer, DeltaTime, Runtime.Transition.GetAlpha());
        }
        return;
    }

    //상태 전이에서 빠져나가면
    if (bWasInTransition && !Runtime.Transition.bActive)
    {
        if (FAnimSequencePlayerRuntime* CompletedToPlayer = FindPlayer(PreviousTransitionTo))
        {
            CompletedToPlayer->bPlaying = true;
        }
        if (FAnimSequencePlayerRuntime* CompletedFromPlayer = FindPlayer(PreviousTransitionFrom))
        {
            CompletedFromPlayer->bPlaying = false;
        }
    }

    if (FAnimSequencePlayerRuntime* CurrentPlayer = FindPlayer(Runtime.CurrentState))
    {
        AdvanceAndQueueSequencePlayer(*CurrentPlayer, DeltaTime, 1.0f);
    }
}

bool UAnimStateMachineInstance::InitializeStateMachineRuntime()
{
    SequencePlayers.clear();

    //StateMachine이 없으면 Runtime Reset하고 실패 return
    if (!StateMachine)
    {
        Runtime.Reset(FName::None);
        return false;
    }

    //InitialState로 초기화
    FString Error;
    const bool bInitialized = StateMachine->InitializeRuntime(Runtime, Error);
    if (!bInitialized)
    {
        UE_LOG_ERROR("[AnimStateMachine] Failed to initialize runtime | Asset=%s | Error=%s",
            StateMachinePath.c_str(),
            Error.c_str());
    }
    return bInitialized;
}

bool UAnimStateMachineInstance::RebuildSequencePlayers()
{
    SequencePlayers.clear();

    if (!StateMachine || !OwningComponent)
    {
        return false;
    }

    const USkeletalMesh* SkeletalMesh = OwningComponent->GetSkeletalMesh();
    if (!SkeletalMesh)
    {
        return false;
    }

    for (const FAnimStateDef& State : StateMachine->States)
    {
        FAnimSequencePlayerRuntime Player;
        Player.DebugName = State.Name.ToString();
        Player.AnimationPath = State.AnimationPath;
        Player.PlayRate = State.PlayRate;
        Player.bLoop = State.bLoop;
        Player.bPlaying = State.Name == Runtime.CurrentState;

        if (!State.AnimationPath.empty())
        {
            Player.Sequence = FResourceManager::Get().LoadAnimSequence(State.AnimationPath, SkeletalMesh, 0);
            if (!Player.Sequence)
            {
                UE_LOG_ERROR("[AnimStateMachine] Failed to load state animation | State=%s | Path=%s",
                    State.Name.ToString().c_str(),
                    State.AnimationPath.c_str());
            }
        }
        else
        {
            UE_LOG_ERROR("[AnimStateMachine] State has empty animation path | State=%s",
                State.Name.ToString().c_str());
        }

        SequencePlayers[State.Name] = Player;
    }

    return true;
}

bool UAnimStateMachineInstance::AdvanceAndQueueSequencePlayer(
    FAnimSequencePlayerRuntime& Player,
    float DeltaTime,
    float NotifyWeight)
{
    const FAnimSequenceAdvanceResult Result = Player.Advance(DeltaTime);
    if (!Result.bAdvanced)
    {
        return false;
    }

    QueueSequenceNotifies(
        Player.Sequence,
        Result.PreviousTime,
        Result.CurrentTime,
        Player.bLoop,
        Result.bLooped,
        Player.bReverse,
        NotifyWeight,
        DeltaTime);

    if (Result.bHitNonLoopBoundary)
    {
        Player.bPlaying = false;
    }

    return true;
}

//Serialize------------------------
void UAnimStateMachineInstance::Serialize(FArchive& Ar)
{
    UAnimInstance::Serialize(Ar);

    Ar << "StateMachineAsset" << StateMachinePath;

    if (Ar.IsLoading())
    {
        StateMachine = StateMachinePath.empty()
            ? nullptr
            : FResourceManager::Get().LoadAnimationStateMachine(StateMachinePath);
    }
}

//--------------Evaluate----------------------
bool UAnimStateMachineInstance::EvaluateAnimation(
    const USkeletalMesh* SkeletalMesh,
    TArray<FMatrix>& OutLocalPose) const
{
    //No StateMachine fallback to bindpose
    if (!StateMachine)
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    //Blending
    if (Runtime.Transition.bActive)
    {
        return EvaluateTransitionPose(SkeletalMesh, OutLocalPose);
    }

    return EvaluateCurrentStatePose(SkeletalMesh, OutLocalPose);
}

bool UAnimStateMachineInstance::EvaluateCurrentStatePose(
    const USkeletalMesh* SkeletalMesh,
    TArray<FMatrix>& OutLocalPose) const
{
    const FAnimSequencePlayerRuntime* Player = FindPlayer(Runtime.CurrentState);
    if (!Player || !Player->Evaluate(SkeletalMesh, OutLocalPose))
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    return true;
}

bool UAnimStateMachineInstance::EvaluateTransitionPose(
    const USkeletalMesh* SkeletalMesh,
    TArray<FMatrix>& OutLocalPose) const
{
    const FAnimSequencePlayerRuntime* FromPlayer = FindPlayer(Runtime.Transition.FromState);
    const FAnimSequencePlayerRuntime* ToPlayer = FindPlayer(Runtime.Transition.ToState);
    if (!FromPlayer || !ToPlayer)
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    TArray<FMatrix> FromPose;
    TArray<FMatrix> ToPose;
    if (!FromPlayer->Evaluate(SkeletalMesh, FromPose) || !ToPlayer->Evaluate(SkeletalMesh, ToPose))
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    BlendLocalMatrixPose(FromPose, ToPose, SkeletalMesh, Runtime.Transition.GetAlpha(), OutLocalPose);
    return true;
}

//---------------------Helper
FAnimSequencePlayerRuntime* UAnimStateMachineInstance::FindPlayer(FName StateName)
{
    auto It = SequencePlayers.find(StateName);
    return It != SequencePlayers.end() ? &It->second : nullptr;
}

const FAnimSequencePlayerRuntime* UAnimStateMachineInstance::FindPlayer(FName StateName) const
{
    auto It = SequencePlayers.find(StateName);
    return It != SequencePlayers.end() ? &It->second : nullptr;
}

const FAnimTransitionDef* UAnimStateMachineInstance::FindTransitionDef(FName FromState, FName ToState) const
{
    if (!StateMachine)
    {
        return nullptr;
    }

    for (const FAnimTransitionDef& Transition : StateMachine->Transitions)
    {
        if (Transition.FromState == FromState && Transition.ToState == ToState)
        {
            return &Transition;
        }
    }

    return nullptr;
}
