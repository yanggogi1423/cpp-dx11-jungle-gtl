#include "AnimNode_SequencePlayer.h"

#include "Animation/AnimInstance.h"
#include "Animation/Sequence/AnimSequenceBase.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
#include "Animation/AnimExtractContext.h"
#include "Animation/PoseContext.h"
#include "Object/Object.h"   // Cast<>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kAnimRangeEpsilon = 1.0e-4f;

	void AppendRootMotion(FTransform& InOut, const FTransform& Delta)
	{
		InOut.Location = InOut.Location + Delta.Location;
		InOut.Rotation = (Delta.Rotation * InOut.Rotation).GetNormalized();
	}

	void ResetToPlaybackRangeStart(FAnimNode_SequencePlayer& Player)
	{
		const float RangeStart = Player.GetEffectiveStartTime();
		const float RangeEnd   = Player.GetEffectiveEndTime();
		Player.LocalTime = (Player.PlayRate < 0.0f && RangeEnd > RangeStart + kAnimRangeEpsilon)
			? RangeEnd
			: RangeStart;
		Player.LastRootMotionDelta = FTransform();
	}
}

void FAnimNode_SequencePlayer::Initialize(const FAnimationInitializeContext& /*Context*/)
{
	ResetToPlaybackRangeStart(*this);
}

void FAnimNode_SequencePlayer::OnBecomeRelevant(const FAnimationInitializeContext& /*Context*/)
{
	// State 진입 / 노드 처음 활성될 때 지정된 playback range 시작점으로 reset.
	ResetToPlaybackRangeStart(*this);
}

void FAnimNode_SequencePlayer::Update(const FAnimationUpdateContext& Context)
{
	if (!IsValid(Sequence))
	{
		Sequence = nullptr;
		LocalTime = 0.0f;
		LastRootMotionDelta = FTransform();
		return;
	}

	const float Length = Sequence->GetPlayLength();
	if (Length <= 0.0f)
	{
		LocalTime = 0.0f;
		LastRootMotionDelta = FTransform();
		return;
	}

	const float RangeStart  = GetEffectiveStartTime();
	const float RangeEnd    = GetEffectiveEndTime();
	const float RangeLength = RangeEnd - RangeStart;
	if (RangeLength <= kAnimRangeEpsilon)
	{
		LocalTime = RangeStart;
		LastRootMotionDelta = FTransform();
		return;
	}

	const float PreviousTime = std::clamp(LocalTime, RangeStart, RangeEnd);
	const float RawTime      = PreviousTime + Context.DeltaSeconds * PlayRate;
	bool bWrappedForward = false;
	bool bWrappedBackward = false;

	if (bLooping)
	{
		bWrappedForward  = PlayRate >  0.0f && RawTime >= RangeEnd;
		bWrappedBackward = PlayRate <  0.0f && RawTime <  RangeStart;

		float Offset = std::fmod(RawTime - RangeStart, RangeLength);
		if (Offset < 0.0f) Offset += RangeLength;
		LocalTime = RangeStart + Offset;
	}
	else
	{
		LocalTime = std::clamp(RawTime, RangeStart, RangeEnd);
	}

	// Notify dispatch — AddAnimNotifies 의 wrap 처리는 전체 sequence 기준이므로,
	// partial range loop 는 [Prev, RangeEnd) + [RangeStart, Cur) 두 구간으로 나눠 보낸다.
	if (IsValid(Context.AnimInstance) && Context.FinalBlendWeight > ZERO_ANIMWEIGHT_THRESH)
	{
		if (PlayRate >= 0.0f)
		{
			if (bLooping && bWrappedForward)
			{
				Context.AnimInstance->AddAnimNotifies(PreviousTime, RangeEnd, Sequence);
				Context.AnimInstance->AddAnimNotifies(RangeStart, LocalTime, Sequence);
			}
			else
			{
				Context.AnimInstance->AddAnimNotifies(PreviousTime, LocalTime, Sequence);
			}
		}
	}

	// Root motion delta 계산 — playback range loop 도 전체 sequence wrap 이 아니라 지정 구간 경계에서 wrap.
	LastRootMotionDelta = FTransform();
	if (UAnimSequence* Seq = Cast<UAnimSequence>(Sequence))
	{
		if (Seq->GetEnableRootMotion())
		{
			if (bLooping && bWrappedForward)
			{
				LastRootMotionDelta = Seq->ExtractRootMotion(PreviousTime, RangeEnd, false);
				AppendRootMotion(LastRootMotionDelta, Seq->ExtractRootMotion(RangeStart, LocalTime, false));
			}
			else if (bLooping && bWrappedBackward)
			{
				LastRootMotionDelta = Seq->ExtractRootMotion(PreviousTime, RangeStart, false);
				AppendRootMotion(LastRootMotionDelta, Seq->ExtractRootMotion(RangeEnd, LocalTime, false));
			}
			else
			{
				LastRootMotionDelta = Seq->ExtractRootMotion(PreviousTime, LocalTime, false);
			}
		}
	}
}

void FAnimNode_SequencePlayer::Evaluate(FPoseContext& Output)
{
	if (!IsValid(Sequence))
	{
		Sequence = nullptr;
		Output.ResetToRefPose();
		return;
	}
	FAnimExtractContext Ctx;
	Ctx.CurrentTime = std::clamp(LocalTime, GetEffectiveStartTime(), GetEffectiveEndTime());
	Ctx.bLooping    = bLooping;
	Sequence->GetBonePose(Output, Ctx);
}


void FAnimNode_SequencePlayer::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Sequence, "SequencePlayer.Sequence");
}

float FAnimNode_SequencePlayer::GetEffectiveStartTime() const
{
	if (!IsValid(Sequence)) return 0.0f;
	const float Length = std::max(0.0f, Sequence->GetPlayLength());
	return std::clamp(StartTime, 0.0f, Length);
}

float FAnimNode_SequencePlayer::GetEffectiveEndTime() const
{
	if (!IsValid(Sequence)) return 0.0f;
	const float Length = std::max(0.0f, Sequence->GetPlayLength());
	const float RangeStart = GetEffectiveStartTime();
	if (EndTime <= RangeStart + kAnimRangeEpsilon)
	{
		return Length;
	}
	return std::clamp(EndTime, RangeStart, Length);
}

float FAnimNode_SequencePlayer::GetEffectivePlayLength() const
{
	return std::max(0.0f, GetEffectiveEndTime() - GetEffectiveStartTime());
}

float FAnimNode_SequencePlayer::GetElapsedTimeInRange() const
{
	return std::clamp(LocalTime, GetEffectiveStartTime(), GetEffectiveEndTime()) - GetEffectiveStartTime();
}

float FAnimNode_SequencePlayer::GetRemainingTimeInRange() const
{
	return std::max(0.0f, GetEffectivePlayLength() - GetElapsedTimeInRange());
}
