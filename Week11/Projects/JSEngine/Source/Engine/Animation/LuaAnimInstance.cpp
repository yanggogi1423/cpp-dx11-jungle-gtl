#include "LuaAnimInstance.h"

#include "Animation/AnimLuaProgramAsset.h"
#include "Animation/AnimSequence.h"
#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Asset/SkeletalMesh.h"
#include "Component/SkeletalMeshComponent.h"
#include "Core/ResourceManager.h"
#include "Math/Quat.h"
#include "Runtime/Script/ScriptManager.h"

#include <algorithm>
#include <cmath>

namespace
{
float Clamp01(float Value)
{
    return MathUtil::Clamp(Value, 0.0f, 1.0f);
}

float EaseInOut(float T)
{
    T = Clamp01(T);
    return T * T * (3.0f - 2.0f * T);
}

EAnimLuaBlendMode ParseLuaBlendMode(const FString& Text)
{
    if (Text == "EaseInOut" || Text == "easeInOut" || Text == "Ease")
    {
        return EAnimLuaBlendMode::EaseInOut;
    }

    return EAnimLuaBlendMode::Linear;
}

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

    // Match FBX import convention: when a bind pose is reflected, the signed scale is
    // represented on a stable Y axis so animation and transition blends do not switch axes.
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

        if (!DecomposeWithSignedScaleForBlend(A[BoneIndex], TranslationA, RotationA, ScaleA, PreferredNegativeAxis, &bFixedReflectionA) ||
            !DecomposeWithSignedScaleForBlend(B[BoneIndex], TranslationB, RotationB, ScaleB, PreferredNegativeAxis, &bFixedReflectionB))
        {
            // Do not fall back to matrix lerp or build FQuat from an improper basis.
            // Pick the nearer endpoint so a failed decomposition cannot inject reflected rotation into the blend.
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

void FAnimLuaTransitionRuntime::Start(const FString& InFromState, const FString& InToState, float InDuration, EAnimLuaBlendMode InBlendMode)
{
    bActive = true;
    FromState = InFromState;
    ToState = InToState;
    ElapsedTime = 0.0f;
    Duration = std::max(InDuration, 0.001f);
    BlendMode = InBlendMode;
}

void FAnimLuaTransitionRuntime::Update(float DeltaTime)
{
    if (!bActive)
    {
        return;
    }

    ElapsedTime += DeltaTime;
}

float FAnimLuaTransitionRuntime::GetRawAlpha() const
{
    if (!bActive)
    {
        return 0.0f;
    }

    return MathUtil::Clamp(ElapsedTime / Duration, 0.0f, 1.0f);
}

float FAnimLuaTransitionRuntime::GetBlendAlpha() const
{
    const float T = GetRawAlpha();

    switch (BlendMode)
    {
    case EAnimLuaBlendMode::EaseInOut:
        return EaseInOut(T);

    case EAnimLuaBlendMode::Linear:
    default:
        return T;
    }
}

bool FAnimLuaTransitionRuntime::IsFinished() const
{
    return bActive && ElapsedTime >= Duration;
}

void FAnimLuaTransitionRuntime::Clear()
{
    bActive = false;
    FromState.clear();
    ToState.clear();
    ElapsedTime = 0.0f;
    Duration = 0.0f;
    BlendMode = EAnimLuaBlendMode::Linear;
}

float ULuaAnimInstance::GetFloat(const FString& Name, float DefaultValue) const
{
    auto It = FloatParams.find(Name);
    return It != FloatParams.end() ? It->second : DefaultValue;
}

bool ULuaAnimInstance::GetBool(const FString& Name, bool DefaultValue) const
{
    auto It = BoolParams.find(Name);
    return It != BoolParams.end() ? It->second : DefaultValue;
}

int32 ULuaAnimInstance::GetInt(const FString& Name, int32 DefaultValue) const
{
    auto It = IntParams.find(Name);
    return It != IntParams.end() ? It->second : DefaultValue;
}

void ULuaAnimInstance::InitializeAnimation(USkeletalMeshComponent* InOwningComponent)
{
    UAnimInstance::InitializeAnimation(InOwningComponent);

    if (!LoadLuaProgram())
    {
        return;
    }

    if (!CreateLuaMachineInstance())
    {
        return;
    }

    BuildPlaybackRuntimeFromLuaStates();
}

void ULuaAnimInstance::UninitializeAnimation()
{
    Transition.Clear();

    SequencePlayers.clear();

    FloatParams.clear();
    BoolParams.clear();
    IntParams.clear();

    LuaMachineTable = sol::nil;
    LuaFactoryTable = sol::nil;
    LuaEnv = sol::nil;

    UAnimInstance::UninitializeAnimation();
}

bool ULuaAnimInstance::LoadLuaProgram()
{
    if (LuaAnimProgramAssetPath.empty())
    {
        UE_LOG_ERROR("[LuaAnim] Missing LuaAnimProgramAssetPath");
        return false;
    }

    sol::state* LuaState = FScriptManager::Get().GetGlobalLuaState();
    if (!LuaState)
    {
        UE_LOG_ERROR("[LuaAnim] Global Lua state is not initialized");
        return false;
    }

    FAssetMetaData MetaData;
    FAnimLuaProgramAssetPayload Payload;
    bool bInvalidAssetClass = false;
    const bool bLoaded = FAssetFile::Load(
        LuaAnimProgramAssetPath,
        MetaData,
        [&](FArchive& Ar)
        {
            if (MetaData.ClassName != UAnimLuaProgramAsset::StaticClass()->ClassName)
            {
                bInvalidAssetClass = true;
                return false;
            }

            Payload.Serialize(Ar, MetaData.PayloadVersion);
            return true;
        });

    if (!bLoaded)
    {
        if (bInvalidAssetClass)
        {
            UE_LOG_ERROR(
                "[LuaAnim] Asset is not UAnimLuaProgramAsset: %s | Class=%s",
                LuaAnimProgramAssetPath.c_str(),
                MetaData.ClassName.c_str());
        }
        else
        {
            UE_LOG_ERROR("[LuaAnim] Failed to load Lua anim program asset: %s", LuaAnimProgramAssetPath.c_str());
        }
        return false;
    }

    const FString& Source = Payload.GeneratedLuaSource;
    if (Source.empty())
    {
        UE_LOG_ERROR("[LuaAnim] Generated Lua source is empty: %s", LuaAnimProgramAssetPath.c_str());
        return false;
    }

    LuaEnv = sol::environment(*LuaState, sol::create, LuaState->globals());
    LuaEnv["AnimInstance"] = this;

    sol::protected_function_result Result = LuaState->safe_script(Source, LuaEnv);
    if (!Result.valid())
    {
        sol::error Error = Result;
        UE_LOG_ERROR("[LuaAnim] Failed to execute generated Lua: %s", Error.what());
        return false;
    }

    sol::object ReturnObj = Result;
    if (!ReturnObj.valid() || ReturnObj.get_type() != sol::type::table)
    {
        UE_LOG_ERROR("[LuaAnim] Generated Lua must return Machine table");
        return false;
    }

    LuaFactoryTable = ReturnObj.as<sol::table>();
    return true;
}

bool ULuaAnimInstance::CreateLuaMachineInstance()
{
    if (!LuaFactoryTable.valid())
    {
        return false;
    }

    sol::protected_function NewFunc = LuaFactoryTable["new"];
    if (!NewFunc.valid())
    {
        UE_LOG_ERROR("[LuaAnim] Lua Machine table must have new()");
        return false;
    }

    sol::protected_function_result Result = NewFunc();
    if (!Result.valid())
    {
        sol::error Error = Result;
        UE_LOG_ERROR("[LuaAnim] Machine.new() failed: %s", Error.what());
        return false;
    }

    sol::object MachineObj = Result;
    if (!MachineObj.valid() || MachineObj.get_type() != sol::type::table)
    {
        UE_LOG_ERROR("[LuaAnim] Machine.new() must return machine instance table");
        return false;
    }

    LuaMachineTable = MachineObj.as<sol::table>();
    return true;
}

bool ULuaAnimInstance::BuildPlaybackRuntimeFromLuaStates()
{
    if (!LuaMachineTable.valid())
    {
        return false;
    }

    USkeletalMeshComponent* MeshComponent = GetOwningComponent();
    USkeletalMesh* SkeletalMesh = MeshComponent ? MeshComponent->GetSkeletalMesh() : nullptr;

    if (!SkeletalMesh || !SkeletalMesh->HasValidMeshData())
    {
        UE_LOG_ERROR("[LuaAnim] Missing valid skeletal mesh");
        return false;
    }

    SequencePlayers.clear();

    sol::table States = LuaMachineTable["states"];
    if (!States.valid())
    {
        sol::protected_function GetStatesFunc = LuaMachineTable["getStates"];
        if (GetStatesFunc.valid())
        {
            sol::protected_function_result Result = GetStatesFunc(LuaMachineTable);
            if (Result.valid())
            {
                sol::object Obj = Result;
                if (Obj.is<sol::table>())
                {
                    States = Obj.as<sol::table>();
                }
            }
        }
    }

    if (!States.valid())
    {
        UE_LOG_ERROR("[LuaAnim] Lua machine must expose states table or getStates()");
        return false;
    }

    for (auto& Pair : States)
    {
        sol::object Key = Pair.first;
        sol::object Value = Pair.second;

        if (!Key.is<FString>() || !Value.is<sol::table>())
        {
            continue;
        }

        const FString StateName = Key.as<FString>();
        sol::table StateTable = Value.as<sol::table>();

        const FString AnimationPath = StateTable["animation"].get_or(FString());
        const bool bLoop = StateTable["loop"].get_or(true);
        const float PlayRate = StateTable["playRate"].get_or(1.0f);

        if (AnimationPath.empty())
        {
            UE_LOG_ERROR("[LuaAnim] State has no animation path | State=%s", StateName.c_str());
            continue;
        }

        UAnimSequence* Sequence = FResourceManager::Get().LoadAnimSequence(AnimationPath, SkeletalMesh, 0);
        if (!Sequence)
        {
            UE_LOG_ERROR("[LuaAnim] Failed to load sequence | State=%s | Path=%s", StateName.c_str(), AnimationPath.c_str());
            continue;
        }

        FAnimSequencePlayerRuntime Player;
        Player.DebugName = StateName;
        Player.AnimationPath = AnimationPath;
        Player.Sequence = Sequence;
        Player.bLoop = bLoop;
        Player.bPlaying = true;
        Player.PlayRate = PlayRate;

        SequencePlayers[StateName] = Player;
    }

    const FString InitialState = GetLuaCurrentState();
    if (InitialState.empty() || SequencePlayers.find(InitialState) == SequencePlayers.end())
    {
        UE_LOG_ERROR("[LuaAnim] Invalid initial state: %s", InitialState.c_str());
        return false;
    }

    Transition.Clear();
    CachedCurrentState = InitialState;
    return true;
}

void ULuaAnimInstance::UpdateAnimGraph(float DeltaTime)
{
    if (!LuaMachineTable.valid())
    {
        return;
    }

    CachedCurrentState = GetLuaCurrentState();

    FAnimLuaTransitionRequest Request;
    TickLuaStateMachine(DeltaTime, Request);

    if (!Transition.bActive && Request.bValid)
    {
        StartTransition(Request);
    }

    if (Transition.bActive)
    {
        Transition.Update(DeltaTime);
    }

    UpdateSequencePlayers(DeltaTime);

    if (Transition.bActive)
    {
        FinishTransitionIfNeeded();
    }
}

bool ULuaAnimInstance::TickLuaStateMachine(float DeltaTime, FAnimLuaTransitionRequest& OutRequest)
{
    if (!LuaMachineTable.valid())
    {
        return false;
    }

    sol::protected_function UpdateFunc = LuaMachineTable["update"];
    if (!UpdateFunc.valid())
    {
        return false;
    }

    sol::table Ctx = BuildLuaContext(DeltaTime);

    sol::protected_function_result Result = UpdateFunc(LuaMachineTable, Ctx);
    if (!Result.valid())
    {
        sol::error Error = Result;
        UE_LOG_ERROR("[LuaAnim] machine:update(ctx) failed: %s", Error.what());
        return false;
    }

    sol::object Obj = Result;
    if (!Obj.valid() || Obj.get_type() != sol::type::table)
    {
        return false;
    }

    sol::table Decision = Obj.as<sol::table>();
    const FString Type = Decision["type"].get_or(FString("none"));

    if (Type != "transition")
    {
        return false;
    }

    OutRequest.bValid = true;
    OutRequest.FromState = Decision["from"].get_or(FString());
    OutRequest.ToState = Decision["to"].get_or(FString());
    OutRequest.BlendTime = Decision["blendTime"].get_or(0.2f);
    OutRequest.bResetTime = Decision["resetTime"].get_or(true);
    OutRequest.BlendMode = ParseLuaBlendMode(Decision["blendMode"].get_or(FString("Linear")));

    if (OutRequest.FromState.empty() || OutRequest.ToState.empty())
    {
        OutRequest.bValid = false;
        return false;
    }

    return OutRequest.bValid;
}

bool ULuaAnimInstance::StartTransition(const FAnimLuaTransitionRequest& Request)
{
    if (!Request.bValid || Request.FromState.empty() || Request.ToState.empty())
    {
        return false;
    }

    if (Request.FromState == Request.ToState)
    {
        return false;
    }

    FAnimSequencePlayerRuntime* FromPlayer = FindPlayer(Request.FromState);
    FAnimSequencePlayerRuntime* ToPlayer = FindPlayer(Request.ToState);

    if (!FromPlayer || !ToPlayer)
    {
        UE_LOG_ERROR("[LuaAnim] Invalid transition | From=%s | To=%s", Request.FromState.c_str(), Request.ToState.c_str());
        return false;
    }

    if (Request.bResetTime)
    {
        ToPlayer->Reset();
    }

    Transition.Start(Request.FromState, Request.ToState, Request.BlendTime, Request.BlendMode);
    return true;
}

void ULuaAnimInstance::CompleteLuaTransition(const FString& ToState)
{
    if (!LuaMachineTable.valid())
    {
        return;
    }

    sol::protected_function CompleteFunc = LuaMachineTable["completeTransition"];
    if (!CompleteFunc.valid())
    {
        return;
    }

    sol::protected_function_result Result = CompleteFunc(LuaMachineTable, ToState);
    if (!Result.valid())
    {
        sol::error Error = Result;
        UE_LOG_ERROR("[LuaAnim] completeTransition failed: %s", Error.what());
    }
}

void ULuaAnimInstance::UpdateSequencePlayers(float DeltaTime)
{
    if (Transition.bActive)
    {
        const float Alpha = Transition.GetBlendAlpha();

        if (FAnimSequencePlayerRuntime* FromPlayer = FindPlayer(Transition.FromState))
        {
            AdvanceAndQueueSequencePlayer(*FromPlayer, DeltaTime, 1.0f - Alpha);
        }

        if (FAnimSequencePlayerRuntime* ToPlayer = FindPlayer(Transition.ToState))
        {
            AdvanceAndQueueSequencePlayer(*ToPlayer, DeltaTime, Alpha);
        }

        return;
    }

    if (FAnimSequencePlayerRuntime* CurrentPlayer = FindPlayer(CachedCurrentState))
    {
        AdvanceAndQueueSequencePlayer(*CurrentPlayer, DeltaTime, 1.0f);
    }
}

bool ULuaAnimInstance::AdvanceAndQueueSequencePlayer(FAnimSequencePlayerRuntime& Player, float DeltaTime, float NotifyWeight)
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
    return true;
}

void ULuaAnimInstance::FinishTransitionIfNeeded()
{
    if (!Transition.IsFinished())
    {
        return;
    }

    const FString CompletedState = Transition.ToState;

    CompleteLuaTransition(CompletedState);
    CachedCurrentState = CompletedState;
    Transition.Clear();
}

sol::table ULuaAnimInstance::BuildLuaContext(float DeltaTime) const
{
    sol::state* LuaState = FScriptManager::Get().GetGlobalLuaState();
    sol::table Ctx = LuaState->create_table();

    const FAnimSequencePlayerRuntime* CurrentPlayer = FindPlayer(CachedCurrentState);

    Ctx["DeltaTime"] = DeltaTime;
    Ctx["IsInTransition"] = Transition.bActive;
    Ctx["TransitionFrom"] = Transition.FromState;
    Ctx["TransitionTo"] = Transition.ToState;
    Ctx["TransitionAlpha"] = Transition.GetBlendAlpha();
    Ctx["StateTime"] = CurrentPlayer ? CurrentPlayer->CurrentTime : 0.0f;
    Ctx["StateNormalizedTime"] = CurrentPlayer ? CurrentPlayer->GetNormalizedTime() : 0.0f;

    for (const auto& Pair : FloatParams)
    {
        Ctx[Pair.first] = Pair.second;
    }

    for (const auto& Pair : BoolParams)
    {
        Ctx[Pair.first] = Pair.second;
    }

    for (const auto& Pair : IntParams)
    {
        Ctx[Pair.first] = Pair.second;
    }

    return Ctx;
}

FString ULuaAnimInstance::GetLuaCurrentState() const
{
    if (!LuaMachineTable.valid())
    {
        return FString();
    }

    sol::protected_function GetCurrentStateFunc = LuaMachineTable["getCurrentState"];
    if (GetCurrentStateFunc.valid())
    {
        sol::protected_function_result Result = GetCurrentStateFunc(LuaMachineTable);
        if (Result.valid())
        {
            sol::object Obj = Result;
            if (Obj.is<FString>())
            {
                return Obj.as<FString>();
            }
        }
    }

    sol::object CurrentStateObj = LuaMachineTable["currentState"];
    if (CurrentStateObj.valid() && CurrentStateObj.is<FString>())
    {
        return CurrentStateObj.as<FString>();
    }

    return FString();
}

bool ULuaAnimInstance::EvaluateAnimation(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const
{
    if (!SkeletalMesh)
    {
        return false;
    }

    if (Transition.bActive)
    {
        return EvaluateTransitionPose(SkeletalMesh, OutLocalPose);
    }

    return EvaluateCurrentStatePose(SkeletalMesh, OutLocalPose);
}

bool ULuaAnimInstance::EvaluateCurrentStatePose(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const
{
    const FAnimSequencePlayerRuntime* Player = FindPlayer(CachedCurrentState);
    if (!Player || !Player->Sequence)
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    if (!Player->Evaluate(SkeletalMesh, OutLocalPose))
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    return true;
}

bool ULuaAnimInstance::EvaluateTransitionPose(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const
{
    const FAnimSequencePlayerRuntime* FromPlayer = FindPlayer(Transition.FromState);
    const FAnimSequencePlayerRuntime* ToPlayer = FindPlayer(Transition.ToState);

    if (!FromPlayer || !ToPlayer || !FromPlayer->Sequence || !ToPlayer->Sequence)
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    TArray<FMatrix> FromPose;
    TArray<FMatrix> ToPose;

    const bool bFromOk = FromPlayer->Evaluate(SkeletalMesh, FromPose);
    const bool bToOk = ToPlayer->Evaluate(SkeletalMesh, ToPose);

    if (!bFromOk || !bToOk)
    {
        return UAnimInstance::EvaluateAnimation(SkeletalMesh, OutLocalPose);
    }

    BlendLocalMatrixPose(FromPose, ToPose, SkeletalMesh, Transition.GetBlendAlpha(), OutLocalPose);
    return true;
}

FAnimSequencePlayerRuntime* ULuaAnimInstance::FindPlayer(const FString& StateName)
{
    auto It = SequencePlayers.find(StateName);
    return It != SequencePlayers.end() ? &It->second : nullptr;
}

const FAnimSequencePlayerRuntime* ULuaAnimInstance::FindPlayer(const FString& StateName) const
{
    auto It = SequencePlayers.find(StateName);
    return It != SequencePlayers.end() ? &It->second : nullptr;
}
