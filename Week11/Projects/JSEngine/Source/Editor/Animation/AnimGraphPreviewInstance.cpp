#include "Editor/Animation/AnimGraphPreviewInstance.h"

#include "Animation/AnimSequence.h"
#include "Asset/SkeletalMesh.h"
#include "Component/SkeletalMeshComponent.h"
#include "Core/ResourceManager.h"
#include "Math/Quat.h"
#include "Math/Utils.h"

#include <algorithm>
#include <cmath>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

namespace
{
float Clamp01(float Value)
{
    return MathUtil::Clamp(Value, 0.0f, 1.0f);
}

float GetBasisDeterminant(
    const FVector& XAxis,
    const FVector& YAxis,
    const FVector& ZAxis)
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

    // Match runtime and FBX import: reflected bind poses use one stable signed-scale
    // axis so preview blending does not squash when pose A/B choose different axes.
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
    bool* OutFixedReflection = nullptr,
    float Tolerance = 1.e-8f)
{
    if (OutFixedReflection)
    {
        *OutFixedReflection = false;
    }

    // Keep this in sync with ULuaAnimInstance until the blend utility is extracted.
    // Animation pose matrices can legally contain reflection through signed scale.
    // FMatrix::Decompose() extracts axis lengths as positive scale, leaving that reflection
    // in the rotation basis. Building FQuat from an improper rotation makes transition
    // blending flip bones, so keep one scale axis signed and force the rotation basis to det +1.
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

    const float Determinant = GetBasisDeterminant(XAxis, YAxis, ZAxis);
    if (Determinant < 0.0f)
    {
        if (OutFixedReflection)
        {
            *OutFixedReflection = true;
        }

        ApplyNegativeScaleAxis(PreferredNegativeAxis, XAxis, YAxis, ZAxis, OutScale);
    }

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

    const float ClampedAlpha = Clamp01(Alpha);

    OutPose.resize(A.size());

    for (size_t BoneIndex = 0; BoneIndex < A.size(); ++BoneIndex)
    {
        FVector TranslationA;
        FVector TranslationB;

        FQuat RotationA = FQuat::Identity;
        FQuat RotationB = FQuat::Identity;

        FVector ScaleA;
        FVector ScaleB;

        bool bFixedReflectionA = false;
        bool bFixedReflectionB = false;
        const ESignedScaleAxis PreferredNegativeAxis =
            GetPreferredNegativeScaleAxisFromBindPose(SkeletalMesh, BoneIndex);

        if (!DecomposeWithSignedScaleForBlend(
                A[BoneIndex],
                TranslationA,
                RotationA,
                ScaleA,
                PreferredNegativeAxis,
                &bFixedReflectionA) ||
            !DecomposeWithSignedScaleForBlend(
                B[BoneIndex],
                TranslationB,
                RotationB,
                ScaleB,
                PreferredNegativeAxis,
                &bFixedReflectionB))
        {
            // Keep the same failure policy as ULuaAnimInstance:
            // never fall back to raw matrix lerp, because improper rotation bases can flip bones.
            OutPose[BoneIndex] = ClampedAlpha >= 0.5f ? B[BoneIndex] : A[BoneIndex];
            continue;
        }

        const FVector BlendedTranslation =
            FVector::Lerp(TranslationA, TranslationB, ClampedAlpha);

        const FQuat BlendedRotation =
            FQuat::Slerp(RotationA, RotationB, ClampedAlpha);

        const FVector BlendedScale =
            FVector::Lerp(ScaleA, ScaleB, ClampedAlpha);

        OutPose[BoneIndex] =
            FMatrix::MakeTRS(
                BlendedTranslation,
                BlendedRotation.ToMatrix(),
                BlendedScale);
    }
}
} // namespace

ULuaAnimGraphPreviewInstance::ULuaAnimGraphPreviewInstance()
{
    bPlaying = true;
    bDispatchPreviewNotifies = false;
}

bool ULuaAnimGraphPreviewInstance::SetPreviewState(
    const FLuaAnimGraphPreviewClipDesc& StateDesc)
{
    FAnimSequencePlayerRuntime NewPlayer;
    if (!BuildPlayerFromClip(StateDesc, NewPlayer))
    {
        ClearPreview();
        return false;
    }

    StatePlayer = NewPlayer;

    FromPlayer = FAnimSequencePlayerRuntime();
    ToPlayer = FAnimSequencePlayerRuntime();

    Transition.Clear();

    PreviewMode = ELuaAnimGraphPreviewMode::State;
    bPlaying = true;

    ActiveNotifyStates.clear();
    ResetNotifyQueue();

    return true;
}

bool ULuaAnimGraphPreviewInstance::SetPreviewTransition(
    const FLuaAnimGraphPreviewTransitionDesc& TransitionDesc)
{
    FAnimSequencePlayerRuntime NewFromPlayer;
    FAnimSequencePlayerRuntime NewToPlayer;

    if (!BuildPlayerFromClip(TransitionDesc.From, NewFromPlayer) ||
        !BuildPlayerFromClip(TransitionDesc.To, NewToPlayer))
    {
        ClearPreview();
        return false;
    }

    FromPlayer = NewFromPlayer;
    ToPlayer = NewToPlayer;

    if (TransitionDesc.bResetTime)
    {
        ToPlayer.Reset();
    }

    StatePlayer = FAnimSequencePlayerRuntime();

    Transition.Start(
        FromPlayer.DebugName,
        ToPlayer.DebugName,
        TransitionDesc.BlendTime,
        TransitionDesc.BlendMode);

    PreviewMode = ELuaAnimGraphPreviewMode::Transition;
    bPlaying = true;

    ActiveNotifyStates.clear();
    ResetNotifyQueue();

    return true;
}

void ULuaAnimGraphPreviewInstance::ClearPreview()
{
    PreviewMode = ELuaAnimGraphPreviewMode::None;

    StatePlayer = FAnimSequencePlayerRuntime();
    FromPlayer = FAnimSequencePlayerRuntime();
    ToPlayer = FAnimSequencePlayerRuntime();

    Transition.Clear();

    bPlaying = false;

    ActiveNotifyStates.clear();
    ResetNotifyQueue();
}

bool ULuaAnimGraphPreviewInstance::BuildPlayerFromClip(
    const FLuaAnimGraphPreviewClipDesc& Clip,
    FAnimSequencePlayerRuntime& OutPlayer) const
{
    if (Clip.AnimationPath.empty())
    {
        return false;
    }

    USkeletalMeshComponent* MeshComponent = GetOwningComponent();
    USkeletalMesh* SkeletalMesh =
        MeshComponent ? MeshComponent->GetSkeletalMesh() : nullptr;

    if (!SkeletalMesh || !SkeletalMesh->HasValidMeshData())
    {
        return false;
    }

    UAnimSequence* Sequence =
        FResourceManager::Get().LoadAnimSequence(
            Clip.AnimationPath,
            SkeletalMesh,
            0);

    if (!Sequence)
    {
        return false;
    }

    OutPlayer = FAnimSequencePlayerRuntime();
    OutPlayer.DebugName = Clip.DebugName.empty() ? Clip.AnimationPath : Clip.DebugName;
    OutPlayer.AnimationPath = Clip.AnimationPath;
    OutPlayer.Sequence = Sequence;
    OutPlayer.bLoop = Clip.bLoop;
    OutPlayer.bPlaying = true;
    OutPlayer.PlayRate = Clip.PlayRate;
    OutPlayer.Reset();

    return true;
}

void ULuaAnimGraphPreviewInstance::ResetPreview()
{
    switch (PreviewMode)
    {
    case ELuaAnimGraphPreviewMode::State:
        StatePlayer.Reset();
        bPlaying = true;
        break;

    case ELuaAnimGraphPreviewMode::Transition:
        FromPlayer.Reset();
        ToPlayer.Reset();
        Transition.ElapsedTime = 0.0f;
        Transition.bActive = FromPlayer.Sequence != nullptr && ToPlayer.Sequence != nullptr;
        bPlaying = Transition.bActive;
        break;

    case ELuaAnimGraphPreviewMode::None:
    default:
        break;
    }
}

float ULuaAnimGraphPreviewInstance::GetTransitionRawAlpha() const
{
    return Transition.GetRawAlpha();
}

float ULuaAnimGraphPreviewInstance::GetTransitionBlendAlpha() const
{
    return Transition.GetBlendAlpha();
}

float ULuaAnimGraphPreviewInstance::GetCurrentTime() const
{
    return GetCurrentStateTime();
}

float ULuaAnimGraphPreviewInstance::GetCurrentNormalizedTime() const
{
    return GetCurrentStateNormalizedTime();
}

float ULuaAnimGraphPreviewInstance::GetCurrentStateTime() const
{
    switch (PreviewMode)
    {
    case ELuaAnimGraphPreviewMode::State:
        return StatePlayer.CurrentTime;

    case ELuaAnimGraphPreviewMode::Transition:
        return FromPlayer.CurrentTime;

    case ELuaAnimGraphPreviewMode::None:
    default:
        return 0.0f;
    }
}

float ULuaAnimGraphPreviewInstance::GetCurrentStateNormalizedTime() const
{
    switch (PreviewMode)
    {
    case ELuaAnimGraphPreviewMode::State:
        return StatePlayer.GetNormalizedTime();

    case ELuaAnimGraphPreviewMode::Transition:
        return FromPlayer.GetNormalizedTime();

    case ELuaAnimGraphPreviewMode::None:
    default:
        return 0.0f;
    }
}

void ULuaAnimGraphPreviewInstance::UpdateAnimGraph(float DeltaTime)
{
    if (!bPlaying)
    {
        return;
    }

    switch (PreviewMode)
    {
    case ELuaAnimGraphPreviewMode::State:
        UpdateStatePreview(DeltaTime);
        break;

    case ELuaAnimGraphPreviewMode::Transition:
        UpdateTransitionPreview(DeltaTime);
        break;

    case ELuaAnimGraphPreviewMode::None:
    default:
        break;
    }
}

void ULuaAnimGraphPreviewInstance::UpdateStatePreview(float DeltaTime)
{
    if (!StatePlayer.Sequence)
    {
        return;
    }

    AdvanceAndQueueSequencePlayer(StatePlayer, DeltaTime, 1.0f);
}

void ULuaAnimGraphPreviewInstance::UpdateTransitionPreview(float DeltaTime)
{
    if (!FromPlayer.Sequence || !ToPlayer.Sequence)
    {
        return;
    }

    if (!Transition.bActive)
    {
        return;
    }

    Transition.Update(DeltaTime);

    const float Alpha = Transition.GetBlendAlpha();

    AdvanceAndQueueSequencePlayer(FromPlayer, DeltaTime, 1.0f - Alpha);
    AdvanceAndQueueSequencePlayer(ToPlayer, DeltaTime, Alpha);

    if (Transition.IsFinished())
    {
        // Editor preview keeps the final blended result visible instead of asking Lua to complete.
        // Runtime ULuaAnimInstance completes through completeTransition() and clears Transition.
        bPlaying = false;
    }
}

bool ULuaAnimGraphPreviewInstance::AdvanceAndQueueSequencePlayer(
    FAnimSequencePlayerRuntime& Player,
    float DeltaTime,
    float NotifyWeight)
{
    const FAnimSequenceAdvanceResult Result = Player.Advance(DeltaTime);
    if (!Result.bAdvanced)
    {
        return false;
    }

    if (bDispatchPreviewNotifies)
    {
        QueueSequenceNotifies(
            Player.Sequence,
            Result.PreviousTime,
            Result.CurrentTime,
            Player.bLoop,
            Result.bLooped,
            Player.bReverse,
            NotifyWeight,
            DeltaTime);
    }

    return true;
}

bool ULuaAnimGraphPreviewInstance::EvaluateAnimation(
    const USkeletalMesh* SkeletalMesh,
    TArray<FMatrix>& OutLocalPose) const
{
    if (!SkeletalMesh)
    {
        return false;
    }

    switch (PreviewMode)
    {
    case ELuaAnimGraphPreviewMode::State:
        return EvaluateStatePreview(SkeletalMesh, OutLocalPose);

    case ELuaAnimGraphPreviewMode::Transition:
        return EvaluateTransitionPreview(SkeletalMesh, OutLocalPose);

    case ELuaAnimGraphPreviewMode::None:
    default:
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }
}

bool ULuaAnimGraphPreviewInstance::EvaluateStatePreview(
    const USkeletalMesh* SkeletalMesh,
    TArray<FMatrix>& OutLocalPose) const
{
    if (!StatePlayer.Sequence)
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    if (!StatePlayer.Evaluate(SkeletalMesh, OutLocalPose))
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    return true;
}

bool ULuaAnimGraphPreviewInstance::EvaluateTransitionPreview(
    const USkeletalMesh* SkeletalMesh,
    TArray<FMatrix>& OutLocalPose) const
{
    if (!FromPlayer.Sequence || !ToPlayer.Sequence)
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    TArray<FMatrix> FromPose;
    TArray<FMatrix> ToPose;

    const bool bFromOk = FromPlayer.Evaluate(SkeletalMesh, FromPose);
    const bool bToOk = ToPlayer.Evaluate(SkeletalMesh, ToPose);

    if (!bFromOk || !bToOk)
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    BlendLocalMatrixPose(
        FromPose,
        ToPose,
        SkeletalMesh,
        Transition.GetBlendAlpha(),
        OutLocalPose);

    return true;
}
