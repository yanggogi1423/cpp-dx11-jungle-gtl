#include "ActorSequenceEditorWidget.h"

#include "Animation/ActorSequence.h"
#include "Component/ActorSequenceComponent.h"
#include "Component/ActorComponent.h"
#include "Component/Primitive/TextRenderComponent.h"
#include "Core/Types/PropertyTypes.h"
#include "Editor/EditorEngine.h"
#include "Editor/Undo/EditorUndoSystem.h"
#include "FloatCurve/FloatCurveAsset.h"
#include "GameFramework/AActor.h"
#include "ImGui/imgui.h"
#include "Object/Object.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

namespace
{
	constexpr float MinSequenceDuration = 0.001f;
	constexpr float CurveKeyHitRadius = 7.0f;
	constexpr float CurveTangentHandleLength = 44.0f;

	class FScopedActorSequenceUndo
	{
	public:
		FScopedActorSequenceUndo(UEditorEngine* InEditor, UActorSequenceComponent* InSequenceComp, const char* InLabel)
			: Editor(InEditor)
			, SequenceComp(InSequenceComp)
			, Label(InLabel ? InLabel : "Edit Actor Sequence")
		{
			AActor* Owner = ResolveOwner();
			if (Editor && Owner)
			{
				BeforeStates = Editor->GetUndoSystem().CaptureActorStates(TArray<AActor*>{ Owner });
			}
		}

		~FScopedActorSequenceUndo()
		{
			AActor* Owner = ResolveOwner();
			if (!Editor || !Owner || BeforeStates.empty())
			{
				return;
			}

			Editor->GetUndoSystem().RecordActorStateChange(
				BeforeStates,
				Editor->GetUndoSystem().CaptureActorStates(TArray<AActor*>{ Owner }),
				Label);
		}

	private:
		AActor* ResolveOwner() const
		{
			return IsValid(SequenceComp) ? SequenceComp->GetOwner() : nullptr;
		}

		UEditorEngine* Editor = nullptr;
		UActorSequenceComponent* SequenceComp = nullptr;
		FString Label;
		TArray<FEditorSerializedActorState> BeforeStates;
	};

#define ACTOR_SEQUENCE_UNDO_JOIN_IMPL(A, B) A##B
#define ACTOR_SEQUENCE_UNDO_JOIN(A, B) ACTOR_SEQUENCE_UNDO_JOIN_IMPL(A, B)
#define ACTOR_SEQUENCE_UNDO_SCOPE(EditorPtr, SequencePtr, LabelText) \
	FScopedActorSequenceUndo ACTOR_SEQUENCE_UNDO_JOIN(ActorSequenceUndoScope, __LINE__)(EditorPtr, SequencePtr, LabelText)

	FString MakeTargetLabel(AActor* Owner, UObject* Object)
	{
		if (!IsValid(Object))
		{
			return "Missing Target";
		}

		if (Object == Owner)
		{
			return "Owner (" + Owner->GetFName().ToString() + ")";
		}

		if (UActorComponent* Component = Cast<UActorComponent>(Object))
		{
			FString Label = Component->GetFName().ToString();
			if (Label.empty() && Component->GetClass())
			{
				Label = Component->GetClass()->GetName();
			}
			return Label;
		}

		return Object->GetName();
	}

	ImVec2 ActorSequenceCurveToScreen(
		float Time,
		float Value,
		float ViewMinTime,
		float ViewMaxTime,
		float ViewMinValue,
		float ViewMaxValue,
		const ImVec2& Min,
		const ImVec2& Max)
	{
		const float Width = (std::max)(1.0f, Max.x - Min.x);
		const float Height = (std::max)(1.0f, Max.y - Min.y);
		const float TimeSpan = (std::max)(0.001f, ViewMaxTime - ViewMinTime);
		const float ValueSpan = (std::max)(0.001f, ViewMaxValue - ViewMinValue);
		const float X = (Time - ViewMinTime) / TimeSpan;
		const float Y = (Value - ViewMinValue) / ValueSpan;
		return ImVec2(Min.x + X * Width, Max.y - Y * Height);
	}

	void ActorSequenceScreenToCurve(
		const ImVec2& Position,
		float ViewMinTime,
		float ViewMaxTime,
		float ViewMinValue,
		float ViewMaxValue,
		const ImVec2& Min,
		const ImVec2& Max,
		float& OutTime,
		float& OutValue)
	{
		const float Width = (std::max)(1.0f, Max.x - Min.x);
		const float Height = (std::max)(1.0f, Max.y - Min.y);
		const float TimeSpan = (std::max)(0.001f, ViewMaxTime - ViewMinTime);
		const float ValueSpan = (std::max)(0.001f, ViewMaxValue - ViewMinValue);
		const float NormalizedX = (Position.x - Min.x) / Width;
		const float NormalizedY = (Max.y - Position.y) / Height;
		OutTime = ViewMinTime + NormalizedX * TimeSpan;
		OutValue = ViewMinValue + NormalizedY * ValueSpan;
	}

	bool ActorSequenceIsPointNear(const ImVec2& A, const ImVec2& B, float Radius)
	{
		const float DX = A.x - B.x;
		const float DY = A.y - B.y;
		return DX * DX + DY * DY <= Radius * Radius;
	}

	ImVec2 ActorSequenceTangentHandlePosition(
		const FCurveKey& Key,
		bool bArrive,
		float ViewMinTime,
		float ViewMaxTime,
		float ViewMinValue,
		float ViewMaxValue,
		const ImVec2& Min,
		const ImVec2& Max)
	{
		const float Tangent = bArrive ? Key.ArriveTangent : Key.LeaveTangent;
		const float Direction = bArrive ? -1.0f : 1.0f;
		const float Width = (std::max)(1.0f, Max.x - Min.x);
		const float Height = (std::max)(1.0f, Max.y - Min.y);
		const float TimeSpan = (std::max)(0.001f, ViewMaxTime - ViewMinTime);
		const float ValueSpan = (std::max)(0.001f, ViewMaxValue - ViewMinValue);
		ImVec2 DirectionVector(Direction * Width / TimeSpan, -Direction * Tangent * Height / ValueSpan);
		const float Length = std::sqrt(DirectionVector.x * DirectionVector.x + DirectionVector.y * DirectionVector.y);
		if (Length > 0.0001f)
		{
			DirectionVector.x /= Length;
			DirectionVector.y /= Length;
		}
		else
		{
			DirectionVector = ImVec2(Direction, 0.0f);
		}

		const ImVec2 KeyPos = ActorSequenceCurveToScreen(
			Key.Time,
			Key.Value,
			ViewMinTime,
			ViewMaxTime,
			ViewMinValue,
			ViewMaxValue,
			Min,
			Max);
		return ImVec2(
			KeyPos.x + DirectionVector.x * CurveTangentHandleLength,
			KeyPos.y + DirectionVector.y * CurveTangentHandleLength);
	}

	int32 SortCurveAndFindKey(FFloatCurve& Curve, float Time, float Value)
	{
		Curve.SortKeys();
		int32 BestIndex = -1;
		float BestDistance = 100000.0f;
		for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Curve.Keys.size()); ++KeyIndex)
		{
			const float TimeDelta = Curve.Keys[KeyIndex].Time - Time;
			const float ValueDelta = Curve.Keys[KeyIndex].Value - Value;
			const float Distance = TimeDelta * TimeDelta + ValueDelta * ValueDelta * 0.0001f;
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				BestIndex = KeyIndex;
			}
		}
		return BestIndex;
	}

	UFloatCurveAsset* EnsureActorSequenceEditableInlineCurve(UActorSequence* Sequence, FActorSequenceChannel& Channel)
	{
		if (!Sequence)
		{
			return Channel.Playback.Curve;
		}

		UFloatCurveAsset* SourceCurve = Channel.Playback.Curve;
		if (!SourceCurve)
		{
			SourceCurve = Sequence->CreateInlineCurve();
			Channel.Playback.Curve = SourceCurve;
			Channel.Playback.CurveAssetPath.clear();
			return SourceCurve;
		}

		if (!Channel.Playback.CurveAssetPath.empty() || SourceCurve->GetOuter() != Sequence)
		{
			UFloatCurveAsset* InlineCurve = Sequence->CreateInlineCurve();
			if (!InlineCurve)
			{
				return SourceCurve;
			}

			InlineCurve->GetCurve() = SourceCurve->GetCurve();
			Channel.Playback.Curve = InlineCurve;
			Channel.Playback.CurveAssetPath.clear();
			return InlineCurve;
		}

		return SourceCurve;
	}

	UObject* ResolveBindingObject(AActor* Owner, const FSequenceObjectBinding& Binding)
	{
		if (!IsValid(Owner))
		{
			return nullptr;
		}

		if (Binding.TargetType == EActorSequenceBindingTarget::OwnerActor)
		{
			if (Binding.TargetObjectName.empty() || Binding.TargetObjectName == Owner->GetFName().ToString())
			{
				return Owner;
			}
			return nullptr;
		}

		for (UActorComponent* Component : Owner->GetComponents())
		{
			if (!IsValid(Component))
			{
				continue;
			}

			if (!Binding.TargetComponentGuid.empty()
				&& Component->GetPersistentGuid() == Binding.TargetComponentGuid)
			{
				return Component;
			}
		}

		for (UActorComponent* Component : Owner->GetComponents())
		{
			if (IsValid(Component) && Component->GetFName().ToString() == Binding.TargetObjectName)
			{
				return Component;
			}
		}
		return nullptr;
	}

	const FProperty* ResolveTrackProperty(
		UObject* Object,
		const FActorSequenceTrack& Track,
		const FActorSequenceChannel& Channel)
	{
		if (!IsValid(Object) || !Object->GetClass())
		{
			return nullptr;
		}

		TArray<const FProperty*> Properties;
		Object->GetClass()->GetPropertyRefs(Properties);
		for (const FProperty* Property : Properties)
		{
			if (!Property || !Property->Name || Track.PropertyName != Property->Name)
			{
				continue;
			}

			float TestValue = 0.0f;
			if (Property->ReadScalarChannelValue(Object, Channel.ChannelName, TestValue))
			{
				return Property;
			}
		}
		return nullptr;
	}

	void CollectAnimatableScalarProperties(UObject* Object, TArray<const FProperty*>& OutProps)
	{
		OutProps.clear();
		if (!IsValid(Object) || !Object->GetClass())
		{
			return;
		}

		TArray<const FProperty*> Properties;
		Object->GetClass()->GetPropertyRefs(Properties);
		for (const FProperty* Property : Properties)
		{
			if (Property && Property->IsSequencerScalar())
			{
				OutProps.push_back(Property);
			}
		}
	}

	bool HasAnimatableScalarProperties(UObject* Object)
	{
		TArray<const FProperty*> Properties;
		CollectAnimatableScalarProperties(Object, Properties);
		return !Properties.empty();
	}

	void CollectTrackTargets(AActor* Owner, UActorSequenceComponent* SequenceComp, TArray<UObject*>& OutTargets)
	{
		OutTargets.clear();
		if (!IsValid(Owner))
		{
			return;
		}

		if (HasAnimatableScalarProperties(Owner))
		{
			OutTargets.push_back(Owner);
		}

		for (UActorComponent* Component : Owner->GetComponents())
		{
			if (!IsValid(Component) || Component == SequenceComp)
			{
				continue;
			}

			if (HasAnimatableScalarProperties(Component))
			{
				OutTargets.push_back(Component);
			}
		}
	}

	void GetChannelNames(const FProperty& Property, TArray<const char*>& OutChannels)
	{
		OutChannels.clear();
		switch (Property.GetType())
		{
		case EPropertyType::Vec3:
			OutChannels = { "x", "y", "z" };
			break;
		case EPropertyType::Rotator:
			OutChannels = { "pitch", "yaw", "roll" };
			break;
		case EPropertyType::Vec4:
			OutChannels = { "x", "y", "z", "w" };
			break;
		case EPropertyType::Color4:
			OutChannels = { "r", "g", "b", "a" };
			break;
		default:
			OutChannels = { "Value" };
			break;
		}
	}

	bool IsTextOpacityPresetTarget(UObject* Target)
	{
		return IsValid(Target)
			&& Target->IsA<UTextRenderComponent>();
	}

	UFloatCurveAsset* CreateTwoKeyInlineCurve(UActorSequence* Sequence, float StartValue, float EndValue, float Duration)
	{
		if (!Sequence)
		{
			return nullptr;
		}

		UFloatCurveAsset* Curve = Sequence->CreateInlineCurve();
		if (!Curve)
		{
			return nullptr;
		}

		FFloatCurve& FloatCurve = Curve->GetCurve();
		FloatCurve.Reset();
		FloatCurve.AddKey(0.0f, StartValue);
		FloatCurve.AddKey((std::max)(Duration, MinSequenceDuration), EndValue);
		FloatCurve.SortKeys();
		FloatCurve.AutoSetTangents();
		return Curve;
	}

	float SequenceTimeToCurveTime(const FActorSequenceSection& Section, const FActorSequenceChannel& Channel, float SequenceTime)
	{
		const float LocalTime = (std::max)(0.0f, SequenceTime - Section.StartTime) * (std::max)(0.0f, Section.PlayRate);
		if (Channel.Playback.TimeMappingMode == ECurveTimeMappingMode::NormalizedTime)
		{
			return Section.Duration > MinSequenceDuration ? LocalTime / Section.Duration : 0.0f;
		}
		return LocalTime;
	}

	float CurveTimeToSequenceTime(const FActorSequenceSection& Section, const FActorSequenceChannel& Channel, float CurveTime)
	{
		const float Rate = (std::max)(0.001f, Section.PlayRate);
		if (Channel.Playback.TimeMappingMode == ECurveTimeMappingMode::NormalizedTime)
		{
			return Section.StartTime + (CurveTime * (std::max)(MinSequenceDuration, Section.Duration)) / Rate;
		}
		return Section.StartTime + CurveTime / Rate;
	}

	bool AddKeyAtCurrentValue(
		UActorSequenceComponent* SequenceComp,
		FActorSequenceBinding& Binding,
		FActorSequenceTrack& Track,
		FActorSequenceSection& Section,
		FActorSequenceChannel& Channel,
		float SequenceTime)
	{
		if (!IsValid(SequenceComp))
		{
			return false;
		}

		AActor* Owner = SequenceComp->GetOwner();
		UObject* TargetObject = ResolveBindingObject(Owner, Binding.Binding);
		const FProperty* Property = ResolveTrackProperty(TargetObject, Track, Channel);
		if (!Property)
		{
			return false;
		}

		float CurrentValue = 0.0f;
		if (!Property->ReadScalarChannelValue(TargetObject, Channel.ChannelName, CurrentValue))
		{
			return false;
		}

		UActorSequence* Sequence = SequenceComp->GetSequence();
		if (!Sequence)
		{
			return false;
		}

		UFloatCurveAsset* Curve = EnsureActorSequenceEditableInlineCurve(Sequence, Channel);
		if (!Curve)
		{
			return false;
		}

		FFloatCurve& FloatCurve = Curve->GetCurve();
		const float CurveTime = SequenceTimeToCurveTime(Section, Channel, SequenceTime);
		bool bUpdatedExisting = false;
		for (FCurveKey& Key : FloatCurve.Keys)
		{
			if (std::fabs(Key.Time - CurveTime) <= 0.0001f)
			{
				Key.Value = CurrentValue;
				bUpdatedExisting = true;
				break;
			}
		}

		if (!bUpdatedExisting)
		{
			FCurveKey Key;
			Key.Time = CurveTime;
			Key.Value = CurrentValue;
			Key.InterpMode = ECurveInterpMode::Cubic;
			Key.TangentMode = ECurveTangentMode::Auto;
			FloatCurve.Keys.push_back(Key);
		}

		FloatCurve.SortKeys();
		FloatCurve.AutoSetTangents();
		return true;
	}
}

bool FActorSequenceEditorWidget::CanEdit(UObject* Object) const
{
	if (!IsValid(Object))
	{
		return false;
	}

	if (UActorSequence* Sequence = Cast<UActorSequence>(Object))
	{
		return IsValid(Cast<UActorSequenceComponent>(Sequence->GetOuter()));
	}

	if (UActorSequenceComponent* SequenceComp = Cast<UActorSequenceComponent>(Object))
	{
		return IsValid(SequenceComp->GetSequence());
	}

	return false;
}

void FActorSequenceEditorWidget::Open(UObject* Object)
{
	UActorSequenceComponent* DirectSequenceComp = Cast<UActorSequenceComponent>(Object);
	UObject* ObjectToEdit = DirectSequenceComp ? static_cast<UObject*>(DirectSequenceComp->GetSequence()) : Object;
	FAssetEditorWidget::Open(ObjectToEdit);
	SequenceComponent = DirectSequenceComp ? DirectSequenceComp : ResolveSequenceComponent();
	char Buffer[64] = {};
	std::snprintf(
		Buffer,
		sizeof(Buffer),
		"%p",
		static_cast<const void*>(SequenceComponent.Get() ? SequenceComponent.Get() : ObjectToEdit));
	DocumentPayloadId = Buffer;
	SelectedBindingIndex = -1;
	SelectedTrackIndex = -1;
	SelectedSectionIndex = -1;
	SelectedChannelIndex = -1;
	SelectedKeyIndex = -1;
	bDraggingTimelineKey = false;
	DraggingKeyBindingIndex = -1;
	DraggingKeyTrackIndex = -1;
	DraggingKeySectionIndex = -1;
	DraggingKeyChannelIndex = -1;
	DraggingKeyIndex = -1;
	bDraggingTimelineSection = false;
	bDraggingPlaybackRange = false;
	bDraggingCurveKey = false;
	bSuppressTimelineScrubUntilMouseUp = false;
	DraggingSectionBindingIndex = -1;
	DraggingSectionTrackIndex = -1;
	DraggingSectionSectionIndex = -1;
	DraggingSectionChannelIndex = -1;
	DraggingSectionEdge = 0;
	DraggingPlaybackRangeEdge = 0;
	DraggingCurveKeyIndex = -1;
	DraggingCurveTangentHandle = 0;
	CurveViewBindingIndex = -1;
	CurveViewTrackIndex = -1;
	CurveViewSectionIndex = -1;
	CurveViewChannelIndex = -1;
	CurveViewMinTime = 0.0f;
	CurveViewMaxTime = 1.0f;
	CurveViewMinValue = -1.0f;
	CurveViewMaxValue = 1.0f;
	PendingTrackTarget = nullptr;
	PendingTrackPropertyName.clear();
	PendingTrackChannelIndex = 0;
	PendingTrackStartTime = 0.0f;
	PendingTrackDuration = 1.0f;
	PendingTrackCurveAssetPath[0] = '\0';

	if (UActorSequence* Sequence = Cast<UActorSequence>(EditedObject))
	{
		ViewStartTime = Sequence->GetStartTime();
		ViewEndTime = (std::max)(Sequence->GetStartTime() + 1.0f, Sequence->GetEndTime());
		PendingTrackDuration = (std::max)(1.0f, Sequence->GetDuration());
	}
}

void FActorSequenceEditorWidget::Close()
{
	if (UActorSequenceComponent* SequenceComp = SequenceComponent.Get())
	{
		SequenceComp->PreviewStop();
	}
	ClearSequenceDragUndo();
	SequenceComponent = nullptr;
	DocumentPayloadId.clear();
	FAssetEditorWidget::Close();
}

void FActorSequenceEditorWidget::Tick(float DeltaTime)
{
	(void)DeltaTime;
	if (!ResolveSequenceComponent())
	{
		Close();
	}
}

void FActorSequenceEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;
	const FString WindowTitle = GetDocumentTitle() + "###ActorSequenceDock_" + GetDocumentPayloadId();
	bool bWindowOpen = IsOpen();
	if (!bRenderingDocument)
	{
		ImGui::SetNextWindowSize(ImVec2(1280.0f, 540.0f), ImGuiCond_Once);
		if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen))
		{
			ImGui::End();
			if (!bWindowOpen)
			{
				Close();
			}
			return;
		}
	}

	if (ConsumeFocusRequest())
	{
		ImGui::SetWindowFocus();
	}

	UActorSequenceComponent* SequenceComp = ResolveSequenceComponent();
	UActorSequence* Sequence = Cast<UActorSequence>(EditedObject);
	if (!SequenceComp || !Sequence)
	{
		ImGui::TextDisabled("Actor Sequence target is no longer valid.");
		if (!bRenderingDocument)
		{
			ImGui::End();
			if (!bWindowOpen)
			{
				Close();
			}
		}
		return;
	}

	RenderToolbar(SequenceComp);
	ImGui::Separator();

	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	const float LeftWidth = (std::min)(320.0f, Avail.x * 0.34f);
	if (ImGui::BeginChild("ActorSequenceTrackList", ImVec2(LeftWidth, 0.0f), true))
	{
		RenderTrackList(SequenceComp);
	}
	ImGui::EndChild();

	ImGui::SameLine();
	if (ImGui::BeginChild("ActorSequenceTimeline", ImVec2(0.0f, 0.0f), true))
	{
		RenderTimeline(SequenceComp);
		ImGui::Separator();
		RenderKeyTable(SequenceComp);
	}
	ImGui::EndChild();

	if (!bRenderingDocument)
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			Close();
		}
	}
}

void FActorSequenceEditorWidget::RenderDocument(float DeltaTime)
{
	bRenderingDocument = true;
	Render(DeltaTime);
	bRenderingDocument = false;
}

void FActorSequenceEditorWidget::AddReferencedObjects(FReferenceCollector& Collector)
{
	FAssetEditorWidget::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(SequenceComponent.Get(), "ActorSequenceEditor.SequenceComponent");
}

FString FActorSequenceEditorWidget::GetDocumentTitle() const
{
	const UActorSequenceComponent* SequenceComp = SequenceComponent.Get();
	const AActor* Owner = SequenceComp ? SequenceComp->GetOwner() : nullptr;
	if (Owner)
	{
		return "Sequencer: " + Owner->GetFName().ToString();
	}
	return "Actor Sequencer";
}

FString FActorSequenceEditorWidget::GetDocumentPayloadId() const
{
	return !DocumentPayloadId.empty() ? DocumentPayloadId : FAssetEditorWidget::GetDocumentPayloadId();
}

UActorSequenceComponent* FActorSequenceEditorWidget::ResolveSequenceComponent() const
{
	UActorSequence* Sequence = Cast<UActorSequence>(EditedObject);
	if (!IsValid(Sequence))
	{
		return nullptr;
	}

	UActorSequenceComponent* Component = Cast<UActorSequenceComponent>(Sequence->GetOuter());
	if (!IsValid(Component) || !IsValid(Component->GetOwner()))
	{
		return nullptr;
	}
	return Component;
}

void FActorSequenceEditorWidget::BeginSequenceDragUndo(UActorSequenceComponent* SequenceComp, const FString& Label)
{
	if (!EditorEngine || !IsValid(SequenceComp) || !IsValid(SequenceComp->GetOwner()))
	{
		return;
	}

	if (!PendingSequenceDragUndoStates.empty())
	{
		return;
	}

	PendingSequenceDragUndoLabel = Label.empty() ? FString("Edit Actor Sequence") : Label;
	PendingSequenceDragUndoStates = EditorEngine->GetUndoSystem().CaptureActorStates(
		TArray<AActor*>{ SequenceComp->GetOwner() });
}

void FActorSequenceEditorWidget::EndSequenceDragUndo(UActorSequenceComponent* SequenceComp)
{
	if (!EditorEngine || PendingSequenceDragUndoStates.empty())
	{
		ClearSequenceDragUndo();
		return;
	}

	AActor* Owner = IsValid(SequenceComp) ? SequenceComp->GetOwner() : nullptr;
	if (!Owner)
	{
		ClearSequenceDragUndo();
		return;
	}

	EditorEngine->GetUndoSystem().RecordActorStateChange(
		PendingSequenceDragUndoStates,
		EditorEngine->GetUndoSystem().CaptureActorStates(TArray<AActor*>{ Owner }),
		PendingSequenceDragUndoLabel);
	ClearSequenceDragUndo();
}

void FActorSequenceEditorWidget::ClearSequenceDragUndo()
{
	PendingSequenceDragUndoStates.clear();
	PendingSequenceDragUndoLabel.clear();
}

void FActorSequenceEditorWidget::RenderToolbar(UActorSequenceComponent* SequenceComp)
{
	UActorSequence* Sequence = SequenceComp ? SequenceComp->GetSequence() : nullptr;
	if (!Sequence)
	{
		return;
	}

	if (ConsumeFocusRequest())
	{
		ImGui::SetKeyboardFocusHere();
	}

	if (ImGui::Button("Play"))
	{
		SequenceComp->PreviewPlay();
	}
	ImGui::SameLine();
	if (ImGui::Button("Pause"))
	{
		SequenceComp->PreviewPause();
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop"))
	{
		SequenceComp->PreviewStop();
	}
	ImGui::SameLine();
	if (ImGui::Button("Fit"))
	{
		ViewStartTime = Sequence->GetStartTime();
		ViewEndTime = (std::max)(Sequence->GetStartTime() + 1.0f, Sequence->GetEndTime());
	}
	ImGui::SameLine();
	if (ImGui::Button("Add Track"))
	{
		ResetAddTrackState(SequenceComp);
		ImGui::OpenPopup("ActorSequenceAddTrackPopup");
	}
	DrawAddTrackPopup(SequenceComp);

	float PreviewTime = SequenceComp->GetPreviewTime();
	const float PlaybackStart = Sequence->GetStartTime();
	const float PlaybackEnd = (std::max)(PlaybackStart + MinSequenceDuration, Sequence->GetEndTime());
	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::SliderFloat("Time", &PreviewTime, PlaybackStart, PlaybackEnd, "%.3f"))
	{
		SequenceComp->SetPreviewTime(PreviewTime);
	}

	ImGui::SameLine();
	float SequenceStartTime = Sequence->GetStartTime();
	ImGui::SetNextItemWidth(110.0f);
	if (ImGui::DragFloat("Start", &SequenceStartTime, 0.01f, 0.0f, 600.0f, "%.3f"))
	{
		ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Playback Range");
		const float OldEndTime = Sequence->GetEndTime();
		Sequence->SetPlaybackRange(SequenceStartTime, OldEndTime);
		SequenceComp->CommitSequenceEditsForSerialization();
		MarkDirty();
	}

	ImGui::SameLine();
	float SequenceDuration = Sequence->GetDuration();
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::DragFloat("Duration", &SequenceDuration, 0.01f, MinSequenceDuration, 600.0f, "%.3f"))
	{
		ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Duration");
		Sequence->SetDuration(SequenceDuration);
		SequenceComp->CommitSequenceEditsForSerialization();
		MarkDirty();
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(86.0f);
	ImGui::DragFloat("View Start", &ViewStartTime, 0.05f, -60.0f, 600.0f, "%.2f");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(86.0f);
	ImGui::DragFloat("View End", &ViewEndTime, 0.05f, -60.0f, 600.0f, "%.2f");
	if (ViewEndTime <= ViewStartTime + 0.1f)
	{
		ViewEndTime = ViewStartTime + 0.1f;
	}
}

void FActorSequenceEditorWidget::ResetAddTrackState(UActorSequenceComponent* SequenceComp)
{
	PendingTrackTarget = nullptr;
	PendingTrackPropertyName.clear();
	PendingTrackChannelIndex = 0;
	PendingTrackStartTime = 0.0f;
	PendingTrackDuration = 1.0f;
	PendingTrackCurveAssetPath[0] = '\0';

	UActorSequence* Sequence = SequenceComp ? SequenceComp->GetSequence() : nullptr;
	AActor* Owner = SequenceComp ? SequenceComp->GetOwner() : nullptr;
	if (Sequence)
	{
		PendingTrackDuration = (std::max)(1.0f, Sequence->GetDuration());
	}

	TArray<UObject*> Targets;
	CollectTrackTargets(Owner, SequenceComp, Targets);
	if (Targets.empty())
	{
		return;
	}

	PendingTrackTarget = Targets.front();
	TArray<const FProperty*> Properties;
	CollectAnimatableScalarProperties(Targets.front(), Properties);
	if (!Properties.empty() && Properties.front() && Properties.front()->Name)
	{
		PendingTrackPropertyName = Properties.front()->Name;
	}
}

void FActorSequenceEditorWidget::DrawAddTrackPopup(UActorSequenceComponent* SequenceComp)
{
	UActorSequence* Sequence = SequenceComp ? SequenceComp->GetSequence() : nullptr;
	AActor* Owner = SequenceComp ? SequenceComp->GetOwner() : nullptr;
	if (!Sequence || !Owner)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
	if (!ImGui::BeginPopup("ActorSequenceAddTrackPopup"))
	{
		return;
	}

	ImGui::TextUnformatted("Add Float Track");
	ImGui::Separator();

	TArray<UObject*> Targets;
	CollectTrackTargets(Owner, SequenceComp, Targets);
	UObject* CurrentTarget = PendingTrackTarget.Get();
	bool bCurrentTargetFound = false;
	for (UObject* Target : Targets)
	{
		if (Target == CurrentTarget)
		{
			bCurrentTargetFound = true;
			break;
		}
	}

	if (!bCurrentTargetFound)
	{
		CurrentTarget = Targets.empty() ? nullptr : Targets.front();
		PendingTrackTarget = CurrentTarget;
		PendingTrackPropertyName.clear();
		PendingTrackChannelIndex = 0;
	}

	if (Targets.empty())
	{
		ImGui::TextDisabled("No animatable scalar properties on this actor.");
	}
	else
	{
		const FString TargetPreview = MakeTargetLabel(Owner, CurrentTarget);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("Target", TargetPreview.c_str()))
		{
			for (UObject* Target : Targets)
			{
				const bool bSelected = Target == CurrentTarget;
				const FString Label = MakeTargetLabel(Owner, Target);
				if (ImGui::Selectable(Label.c_str(), bSelected))
				{
					CurrentTarget = Target;
					PendingTrackTarget = Target;
					PendingTrackPropertyName.clear();
					PendingTrackChannelIndex = 0;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		TArray<const FProperty*> Properties;
		CollectAnimatableScalarProperties(CurrentTarget, Properties);
		const FProperty* CurrentProperty = nullptr;
		for (const FProperty* Property : Properties)
		{
			if (Property && Property->Name && PendingTrackPropertyName == Property->Name)
			{
				CurrentProperty = Property;
				break;
			}
		}
		if (!CurrentProperty && !Properties.empty())
		{
			CurrentProperty = Properties.front();
			PendingTrackPropertyName = CurrentProperty && CurrentProperty->Name ? CurrentProperty->Name : FString();
			PendingTrackChannelIndex = 0;
		}

		const char* PropertyPreview = CurrentProperty
			? (CurrentProperty->DisplayName ? CurrentProperty->DisplayName : CurrentProperty->Name)
			: "None";
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("Property", PropertyPreview))
		{
			for (const FProperty* Property : Properties)
			{
				if (!Property || !Property->Name)
				{
					continue;
				}

				const bool bSelected = CurrentProperty == Property;
				const char* Label = Property->DisplayName ? Property->DisplayName : Property->Name;
				if (ImGui::Selectable(Label, bSelected))
				{
					CurrentProperty = Property;
					PendingTrackPropertyName = Property->Name;
					PendingTrackChannelIndex = 0;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		TArray<const char*> Channels;
		if (CurrentProperty)
		{
			GetChannelNames(*CurrentProperty, Channels);
		}
		if (PendingTrackChannelIndex < 0
			|| PendingTrackChannelIndex >= static_cast<int32>(Channels.size()))
		{
			PendingTrackChannelIndex = 0;
		}

		const char* ChannelPreview = !Channels.empty() ? Channels[PendingTrackChannelIndex] : "Value";
		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::BeginCombo("Channel", ChannelPreview))
		{
			for (int32 ChannelIndex = 0; ChannelIndex < static_cast<int32>(Channels.size()); ++ChannelIndex)
			{
				const bool bSelected = ChannelIndex == PendingTrackChannelIndex;
				if (ImGui::Selectable(Channels[ChannelIndex], bSelected))
				{
					PendingTrackChannelIndex = ChannelIndex;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		float CurrentValue = 0.0f;
		const bool bCanReadCurrentValue = CurrentTarget && CurrentProperty
			&& CurrentProperty->ReadScalarChannelValue(CurrentTarget, ChannelPreview, CurrentValue);
		if (bCanReadCurrentValue)
		{
			ImGui::Text("Current Value: %.4f", CurrentValue);
		}
		else
		{
			ImGui::TextDisabled("Current Value: unavailable");
		}

		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragFloat("Start", &PendingTrackStartTime, 0.01f, 0.0f, 600.0f, "%.3f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragFloat("Length", &PendingTrackDuration, 0.01f, 0.001f, 600.0f, "%.3f");
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputText("Curve Asset Path", PendingTrackCurveAssetPath, sizeof(PendingTrackCurveAssetPath));

		const bool bCanAddTrack = CurrentTarget && CurrentProperty && !Channels.empty() && bCanReadCurrentValue;
		if (!bCanAddTrack)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("Add Track"))
		{
			const FString CurveAssetPath = PendingTrackCurveAssetPath;
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Add Actor Sequence Track");

			if (SequenceComp->AddFloatTrack(
				CurrentTarget,
				CurrentProperty->Name,
				ChannelPreview,
				(std::max)(0.0f, PendingTrackStartTime),
				(std::max)(0.001f, PendingTrackDuration),
				CurveAssetPath))
			{
				SequenceComp->CommitSequenceEditsForSerialization();
				SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
				MarkDirty();

				TArray<FActorSequenceBinding>& Bindings = Sequence->GetBindings();
				for (int32 BindingIndex = static_cast<int32>(Bindings.size()) - 1; BindingIndex >= 0; --BindingIndex)
				{
					FActorSequenceBinding& Binding = Bindings[BindingIndex];
					if (ResolveBindingObject(Owner, Binding.Binding) != CurrentTarget)
					{
						continue;
					}

					for (int32 TrackIndex = static_cast<int32>(Binding.Tracks.size()) - 1; TrackIndex >= 0; --TrackIndex)
					{
						FActorSequenceTrack& Track = Binding.Tracks[TrackIndex];
						if (Track.PropertyName != CurrentProperty->Name)
						{
							continue;
						}

						for (int32 SectionIndex = static_cast<int32>(Track.Sections.size()) - 1; SectionIndex >= 0; --SectionIndex)
						{
							FActorSequenceSection& Section = Track.Sections[SectionIndex];
							for (int32 ChannelIndex = static_cast<int32>(Section.Channels.size()) - 1; ChannelIndex >= 0; --ChannelIndex)
							{
								if (Section.Channels[ChannelIndex].ChannelName == ChannelPreview)
								{
									SelectedBindingIndex = BindingIndex;
									SelectedTrackIndex = TrackIndex;
									SelectedSectionIndex = SectionIndex;
									SelectedChannelIndex = ChannelIndex;
									SelectedKeyIndex = -1;
									ImGui::CloseCurrentPopup();
									ImGui::EndPopup();
									return;
								}
							}
						}
					}
				}

				ImGui::CloseCurrentPopup();
			}
		}
		if (!bCanAddTrack)
		{
			ImGui::EndDisabled();
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}

		const bool bCanAddTextOpacityPreset = bCanAddTrack
			&& IsTextOpacityPresetTarget(CurrentTarget);
		if (bCanAddTextOpacityPreset)
		{
			auto AddTextOpacityPreset = [&](const char* Label, float StartValue, float EndValue) -> bool
			{
				if (!ImGui::Button(Label))
				{
					return false;
				}

				const float Start = (std::max)(0.0f, PendingTrackStartTime);
				const float Duration = (std::max)(MinSequenceDuration, PendingTrackDuration);
				ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Add Text Opacity Preset");
				UFloatCurveAsset* Curve = CreateTwoKeyInlineCurve(Sequence, StartValue, EndValue, Duration);
				if (!Curve)
				{
					return false;
				}

				if (!Sequence->AddFloatTrack(CurrentTarget, "Opacity", "Value", Start, Duration, Curve, FString()))
				{
					return false;
				}

				SequenceComp->CommitSequenceEditsForSerialization();
				SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
				MarkDirty();
				ImGui::CloseCurrentPopup();
				return true;
			};

			ImGui::Separator();
			ImGui::TextUnformatted("Text Preset");
			if (AddTextOpacityPreset("Fade In", 0.0f, 1.0f))
			{
				ImGui::EndPopup();
				return;
			}
			ImGui::SameLine();
			if (AddTextOpacityPreset("Fade Out", 1.0f, 0.0f))
			{
				ImGui::EndPopup();
				return;
			}
		}
	}

	ImGui::EndPopup();
}

void FActorSequenceEditorWidget::RenderTrackList(UActorSequenceComponent* SequenceComp)
{
	UActorSequence* Sequence = SequenceComp ? SequenceComp->GetSequence() : nullptr;
	AActor* Owner = SequenceComp ? SequenceComp->GetOwner() : nullptr;
	if (!Sequence || !Owner)
	{
		return;
	}

	ImGui::TextUnformatted("Tracks");
	ImGui::Separator();

	TArray<FActorSequenceBinding>& Bindings = Sequence->GetBindings();
	if (Bindings.empty())
	{
		ImGui::TextDisabled("No tracks. Use Add Track in the toolbar.");
		return;
	}

	for (int32 BindingIndex = 0; BindingIndex < static_cast<int32>(Bindings.size()); ++BindingIndex)
	{
		FActorSequenceBinding& Binding = Bindings[BindingIndex];
		UObject* Target = ResolveBindingObject(Owner, Binding.Binding);
		const FString TargetLabel = MakeTargetLabel(Owner, Target);

		if (ImGui::TreeNodeEx(TargetLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(Binding.Tracks.size()); ++TrackIndex)
			{
				FActorSequenceTrack& Track = Binding.Tracks[TrackIndex];
				for (int32 SectionIndex = 0; SectionIndex < static_cast<int32>(Track.Sections.size()); ++SectionIndex)
				{
					FActorSequenceSection& Section = Track.Sections[SectionIndex];
					for (int32 ChannelIndex = 0; ChannelIndex < static_cast<int32>(Section.Channels.size()); ++ChannelIndex)
					{
						FActorSequenceChannel& Channel = Section.Channels[ChannelIndex];
						const bool bSelected =
							SelectedBindingIndex == BindingIndex &&
							SelectedTrackIndex == TrackIndex &&
							SelectedSectionIndex == SectionIndex &&
							SelectedChannelIndex == ChannelIndex;
						const FString Label = Track.PropertyName + "." + Channel.ChannelName;
						if (ImGui::Selectable(Label.c_str(), bSelected))
						{
							SelectedBindingIndex = BindingIndex;
							SelectedTrackIndex = TrackIndex;
							SelectedSectionIndex = SectionIndex;
							SelectedChannelIndex = ChannelIndex;
							SelectedKeyIndex = -1;
						}
					}
				}
			}
			ImGui::TreePop();
		}
	}
}

void FActorSequenceEditorWidget::RenderTimeline(UActorSequenceComponent* SequenceComp)
{
	UActorSequence* Sequence = SequenceComp ? SequenceComp->GetSequence() : nullptr;
	if (!Sequence)
	{
		return;
	}

	const ImVec2 CanvasSize((std::max)(320.0f, ImGui::GetContentRegionAvail().x), 220.0f);
	const ImVec2 CanvasMin = ImGui::GetCursorScreenPos();
	const ImVec2 CanvasMax(CanvasMin.x + CanvasSize.x, CanvasMin.y + CanvasSize.y);
	const float ViewRange = (std::max)(0.1f, ViewEndTime - ViewStartTime);
	auto TimeToX = [&](float Time)
	{
		return CanvasMin.x + ((Time - ViewStartTime) / ViewRange) * CanvasSize.x;
	};
	auto XToTime = [&](float X)
	{
		const float Alpha = (X - CanvasMin.x) / (std::max)(1.0f, CanvasSize.x);
		return ViewStartTime + Alpha * ViewRange;
	};

	struct FTimelineKeyHit
	{
		int32 BindingIndex = -1;
		int32 TrackIndex = -1;
		int32 SectionIndex = -1;
		int32 ChannelIndex = -1;
		int32 KeyIndex = -1;
		float DistanceSq = 100000.0f;
	};

	auto FindKeyHit = [&](const ImVec2& Mouse, FTimelineKeyHit& OutHit)
	{
		if (Mouse.x < CanvasMin.x || Mouse.x > CanvasMax.x || Mouse.y < CanvasMin.y || Mouse.y > CanvasMax.y)
		{
			return false;
		}

		TArray<FActorSequenceBinding>& Bindings = Sequence->GetBindings();
		float RowY = CanvasMin.y + 28.0f;
		int32 DisplayIndex = 0;
		for (int32 BindingIndex = 0; BindingIndex < static_cast<int32>(Bindings.size()); ++BindingIndex)
		{
			FActorSequenceBinding& Binding = Bindings[BindingIndex];
			for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(Binding.Tracks.size()); ++TrackIndex)
			{
				FActorSequenceTrack& Track = Binding.Tracks[TrackIndex];
				for (int32 SectionIndex = 0; SectionIndex < static_cast<int32>(Track.Sections.size()); ++SectionIndex)
				{
					FActorSequenceSection& Section = Track.Sections[SectionIndex];
					for (int32 ChannelIndex = 0; ChannelIndex < static_cast<int32>(Section.Channels.size()); ++ChannelIndex)
					{
						FActorSequenceChannel& Channel = Section.Channels[ChannelIndex];
						const float RowMinY = RowY + static_cast<float>(DisplayIndex) * 24.0f;
						const float RowMaxY = RowMinY + 18.0f;
						++DisplayIndex;
						if (RowMinY > CanvasMax.y - 10.0f || !Channel.Playback.Curve)
						{
							continue;
						}

						const float KeyY = (RowMinY + RowMaxY) * 0.5f;
						const TArray<FCurveKey>& Keys = Channel.Playback.Curve->GetCurve().Keys;
						for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Keys.size()); ++KeyIndex)
						{
							const float KeySequenceTime = CurveTimeToSequenceTime(Section, Channel, Keys[KeyIndex].Time);
							const float KeyX = TimeToX(KeySequenceTime);
							const float Dx = Mouse.x - KeyX;
							const float Dy = Mouse.y - KeyY;
							const float DistanceSq = Dx * Dx + Dy * Dy;
							if (DistanceSq <= 64.0f && DistanceSq < OutHit.DistanceSq)
							{
								OutHit.BindingIndex = BindingIndex;
								OutHit.TrackIndex = TrackIndex;
								OutHit.SectionIndex = SectionIndex;
								OutHit.ChannelIndex = ChannelIndex;
								OutHit.KeyIndex = KeyIndex;
								OutHit.DistanceSq = DistanceSq;
							}
						}
					}
				}
			}
		}
		return OutHit.KeyIndex >= 0;
	};

	struct FTimelineSectionHit
	{
		int32 BindingIndex = -1;
		int32 TrackIndex = -1;
		int32 SectionIndex = -1;
		int32 ChannelIndex = -1;
		int32 Edge = 0; // 0: body, 1: start, 2: end
	};

	auto FindSectionHit = [&](const ImVec2& Mouse, FTimelineSectionHit& OutHit)
	{
		if (Mouse.x < CanvasMin.x || Mouse.x > CanvasMax.x || Mouse.y < CanvasMin.y || Mouse.y > CanvasMax.y)
		{
			return false;
		}

		TArray<FActorSequenceBinding>& Bindings = Sequence->GetBindings();
		float RowY = CanvasMin.y + 28.0f;
		int32 DisplayIndex = 0;
		for (int32 BindingIndex = 0; BindingIndex < static_cast<int32>(Bindings.size()); ++BindingIndex)
		{
			FActorSequenceBinding& Binding = Bindings[BindingIndex];
			for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(Binding.Tracks.size()); ++TrackIndex)
			{
				FActorSequenceTrack& Track = Binding.Tracks[TrackIndex];
				for (int32 SectionIndex = 0; SectionIndex < static_cast<int32>(Track.Sections.size()); ++SectionIndex)
				{
					FActorSequenceSection& Section = Track.Sections[SectionIndex];
					for (int32 ChannelIndex = 0; ChannelIndex < static_cast<int32>(Section.Channels.size()); ++ChannelIndex)
					{
						const float RowMinY = RowY + static_cast<float>(DisplayIndex) * 24.0f;
						const float RowMaxY = RowMinY + 18.0f;
						++DisplayIndex;
						if (RowMinY > CanvasMax.y - 10.0f || Mouse.y < RowMinY || Mouse.y > RowMaxY)
						{
							continue;
						}

						const float StartX = TimeToX(Section.StartTime);
						const float EndX = TimeToX(Section.StartTime + Section.Duration);
						const float MinX = (std::min)(StartX, EndX);
						const float MaxX = (std::max)(StartX, EndX);
						if (Mouse.x < MinX - 4.0f || Mouse.x > MaxX + 4.0f)
						{
							continue;
						}

						OutHit.BindingIndex = BindingIndex;
						OutHit.TrackIndex = TrackIndex;
						OutHit.SectionIndex = SectionIndex;
						OutHit.ChannelIndex = ChannelIndex;
						if (std::fabs(Mouse.x - StartX) <= 6.0f)
						{
							OutHit.Edge = 1;
						}
						else if (std::fabs(Mouse.x - EndX) <= 6.0f)
						{
							OutHit.Edge = 2;
						}
						else if (Mouse.x >= MinX && Mouse.x <= MaxX)
						{
							OutHit.Edge = 0;
						}
						else
						{
							continue;
						}
						return true;
					}
				}
			}
		}
		return false;
	};

	auto FindPlaybackRangeHit = [&](const ImVec2& Mouse)
	{
		if (Mouse.x < CanvasMin.x || Mouse.x > CanvasMax.x
			|| Mouse.y < CanvasMin.y + 14.0f || Mouse.y > CanvasMin.y + 30.0f)
		{
			return 0;
		}

		const float PlaybackStartX = TimeToX(Sequence->GetStartTime());
		const float PlaybackEndX = TimeToX(Sequence->GetEndTime());
		if (std::fabs(Mouse.x - PlaybackStartX) <= 7.0f)
		{
			return 1;
		}
		if (std::fabs(Mouse.x - PlaybackEndX) <= 7.0f)
		{
			return 2;
		}
		return 0;
	};

	auto ResetTimelineKeyDrag = [&]()
	{
		bDraggingTimelineKey = false;
		DraggingKeyBindingIndex = -1;
		DraggingKeyTrackIndex = -1;
		DraggingKeySectionIndex = -1;
		DraggingKeyChannelIndex = -1;
		DraggingKeyIndex = -1;
	};

	auto ResetTimelineSectionDrag = [&]()
	{
		bDraggingTimelineSection = false;
		DraggingSectionBindingIndex = -1;
		DraggingSectionTrackIndex = -1;
		DraggingSectionSectionIndex = -1;
		DraggingSectionChannelIndex = -1;
		DraggingSectionEdge = 0;
	};

	auto ResetPlaybackRangeDrag = [&]()
	{
		bDraggingPlaybackRange = false;
		DraggingPlaybackRangeEdge = 0;
	};

	auto ResetTimelineDrags = [&]()
	{
		ResetTimelineKeyDrag();
		ResetTimelineSectionDrag();
		ResetPlaybackRangeDrag();
	};

	auto GetDraggingKey = [&](FActorSequenceSection*& OutSection, FActorSequenceChannel*& OutChannel, FCurveKey*& OutKey)
	{
		OutSection = nullptr;
		OutChannel = nullptr;
		OutKey = nullptr;

		TArray<FActorSequenceBinding>& Bindings = Sequence->GetBindings();
		if (DraggingKeyBindingIndex < 0 || DraggingKeyBindingIndex >= static_cast<int32>(Bindings.size()))
		{
			return false;
		}
		FActorSequenceBinding& Binding = Bindings[DraggingKeyBindingIndex];
		if (DraggingKeyTrackIndex < 0 || DraggingKeyTrackIndex >= static_cast<int32>(Binding.Tracks.size()))
		{
			return false;
		}
		FActorSequenceTrack& Track = Binding.Tracks[DraggingKeyTrackIndex];
		if (DraggingKeySectionIndex < 0 || DraggingKeySectionIndex >= static_cast<int32>(Track.Sections.size()))
		{
			return false;
		}
		FActorSequenceSection& Section = Track.Sections[DraggingKeySectionIndex];
		if (DraggingKeyChannelIndex < 0 || DraggingKeyChannelIndex >= static_cast<int32>(Section.Channels.size()))
		{
			return false;
		}
		FActorSequenceChannel& Channel = Section.Channels[DraggingKeyChannelIndex];
		UFloatCurveAsset* EditableCurve = EnsureActorSequenceEditableInlineCurve(Sequence, Channel);
		if (!EditableCurve)
		{
			return false;
		}
		FFloatCurve& Curve = EditableCurve->GetCurve();
		if (DraggingKeyIndex < 0 || DraggingKeyIndex >= static_cast<int32>(Curve.Keys.size()))
		{
			return false;
		}

		OutSection = &Section;
		OutChannel = &Channel;
		OutKey = &Curve.Keys[DraggingKeyIndex];
		return true;
	};

	auto GetDraggingSection = [&](FActorSequenceSection*& OutSection)
	{
		OutSection = nullptr;

		TArray<FActorSequenceBinding>& Bindings = Sequence->GetBindings();
		if (DraggingSectionBindingIndex < 0 || DraggingSectionBindingIndex >= static_cast<int32>(Bindings.size()))
		{
			return false;
		}
		FActorSequenceBinding& Binding = Bindings[DraggingSectionBindingIndex];
		if (DraggingSectionTrackIndex < 0 || DraggingSectionTrackIndex >= static_cast<int32>(Binding.Tracks.size()))
		{
			return false;
		}
		FActorSequenceTrack& Track = Binding.Tracks[DraggingSectionTrackIndex];
		if (DraggingSectionSectionIndex < 0 || DraggingSectionSectionIndex >= static_cast<int32>(Track.Sections.size()))
		{
			return false;
		}
		OutSection = &Track.Sections[DraggingSectionSectionIndex];
		return OutSection != nullptr;
	};

	auto UpdateDraggingKeyTime = [&]()
	{
		FActorSequenceSection* Section = nullptr;
		FActorSequenceChannel* Channel = nullptr;
		FCurveKey* Key = nullptr;
		if (!GetDraggingKey(Section, Channel, Key))
		{
			ResetTimelineKeyDrag();
			return false;
		}

		const float SectionEnd = Section->StartTime + (std::max)(MinSequenceDuration, Section->Duration);
		const float NewSequenceTime = (std::clamp)(XToTime(ImGui::GetMousePos().x), Section->StartTime, SectionEnd);
		Key->Time = (std::max)(0.0f, SequenceTimeToCurveTime(*Section, *Channel, NewSequenceTime));
		if (Channel->Playback.Curve)
		{
			Channel->Playback.Curve->GetCurve().AutoSetTangents();
		}
		SequenceComp->SetPreviewTime((std::clamp)(NewSequenceTime, Sequence->GetStartTime(), Sequence->GetEndTime()));
		SequenceComp->CommitSequenceEditsForSerialization();
		MarkDirty();
		return true;
	};

	auto UpdateDraggingSectionTime = [&]()
	{
		FActorSequenceSection* Section = nullptr;
		if (!GetDraggingSection(Section) || DraggingSectionEdge == 0)
		{
			ResetTimelineSectionDrag();
			return false;
		}

		constexpr float MinSectionDuration = 0.001f;
		const float MouseTime = (std::clamp)(XToTime(ImGui::GetMousePos().x), 0.0f, 600.0f);
		const float CurrentEnd = Section->StartTime + (std::max)(MinSectionDuration, Section->Duration);
		if (DraggingSectionEdge == 1)
		{
			const float NewStart = (std::clamp)(MouseTime, 0.0f, CurrentEnd - MinSectionDuration);
			Section->StartTime = NewStart;
			Section->Duration = (std::max)(MinSectionDuration, CurrentEnd - NewStart);
			SequenceComp->SetPreviewTime(NewStart);
		}
		else
		{
			const float NewEnd = (std::max)(Section->StartTime + MinSectionDuration, MouseTime);
			Section->Duration = (std::max)(MinSectionDuration, NewEnd - Section->StartTime);
			SequenceComp->SetPreviewTime(NewEnd);
		}

		Sequence->SetDuration((std::max)(Sequence->GetDuration(), Section->StartTime + Section->Duration - Sequence->GetStartTime()));
		SequenceComp->CommitSequenceEditsForSerialization();
		MarkDirty();
		return true;
	};

	auto UpdateDraggingPlaybackRange = [&]()
	{
		if (DraggingPlaybackRangeEdge == 0)
		{
			ResetPlaybackRangeDrag();
			return false;
		}

		constexpr float MinPlaybackDuration = 0.001f;
		const float MouseTime = (std::clamp)(XToTime(ImGui::GetMousePos().x), 0.0f, 600.0f);
		const float CurrentStart = Sequence->GetStartTime();
		const float CurrentEnd = Sequence->GetEndTime();
		if (DraggingPlaybackRangeEdge == 1)
		{
			const float NewStart = (std::clamp)(MouseTime, 0.0f, CurrentEnd - MinPlaybackDuration);
			Sequence->SetPlaybackRange(NewStart, CurrentEnd);
		}
		else
		{
			const float NewEnd = (std::max)(CurrentStart + MinPlaybackDuration, MouseTime);
			Sequence->SetPlaybackRange(CurrentStart, NewEnd);
		}

		SequenceComp->SetPreviewTime((std::clamp)(SequenceComp->GetPreviewTime(), Sequence->GetStartTime(), Sequence->GetEndTime()));
		SequenceComp->CommitSequenceEditsForSerialization();
		MarkDirty();
		return true;
	};

	auto ResolveSelectedChannel = [&](
		FActorSequenceBinding*& OutBinding,
		FActorSequenceTrack*& OutTrack,
		FActorSequenceSection*& OutSection,
		FActorSequenceChannel*& OutChannel)
	{
		OutBinding = nullptr;
		OutTrack = nullptr;
		OutSection = nullptr;
		OutChannel = nullptr;

		TArray<FActorSequenceBinding>& Bindings = Sequence->GetBindings();
		if (SelectedBindingIndex < 0 || SelectedBindingIndex >= static_cast<int32>(Bindings.size()))
		{
			return false;
		}

		FActorSequenceBinding& Binding = Bindings[SelectedBindingIndex];
		if (SelectedTrackIndex < 0 || SelectedTrackIndex >= static_cast<int32>(Binding.Tracks.size()))
		{
			return false;
		}

		FActorSequenceTrack& Track = Binding.Tracks[SelectedTrackIndex];
		if (SelectedSectionIndex < 0 || SelectedSectionIndex >= static_cast<int32>(Track.Sections.size()))
		{
			return false;
		}

		FActorSequenceSection& Section = Track.Sections[SelectedSectionIndex];
		if (SelectedChannelIndex < 0 || SelectedChannelIndex >= static_cast<int32>(Section.Channels.size()))
		{
			return false;
		}

		OutBinding = &Binding;
		OutTrack = &Track;
		OutSection = &Section;
		OutChannel = &Section.Channels[SelectedChannelIndex];
		return true;
	};

	auto FitViewToSelectedSection = [&]()
	{
		FActorSequenceBinding* Binding = nullptr;
		FActorSequenceTrack* Track = nullptr;
		FActorSequenceSection* Section = nullptr;
		FActorSequenceChannel* Channel = nullptr;
		if (!ResolveSelectedChannel(Binding, Track, Section, Channel))
		{
			return false;
		}

		const float Start = Section->StartTime;
		const float End = Section->StartTime + (std::max)(MinSequenceDuration, Section->Duration);
		const float Padding = (std::max)(0.1f, (End - Start) * 0.15f);
		ViewStartTime = (std::max)(0.0f, Start - Padding);
		ViewEndTime = End + Padding;
		return true;
	};

	auto AddKeyToSelectedChannel = [&]()
	{
		FActorSequenceBinding* Binding = nullptr;
		FActorSequenceTrack* Track = nullptr;
		FActorSequenceSection* Section = nullptr;
		FActorSequenceChannel* Channel = nullptr;
		if (!ResolveSelectedChannel(Binding, Track, Section, Channel))
		{
			return false;
		}

		ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Add Actor Sequence Key");
		if (!AddKeyAtCurrentValue(
			SequenceComp,
			*Binding,
			*Track,
			*Section,
			*Channel,
			SequenceComp->GetPreviewTime()))
		{
			return false;
		}

		SequenceComp->CommitSequenceEditsForSerialization();
		MarkDirty();
		if (Channel->Playback.Curve)
		{
			const float NewCurveTime = SequenceTimeToCurveTime(*Section, *Channel, SequenceComp->GetPreviewTime());
			const TArray<FCurveKey>& Keys = Channel->Playback.Curve->GetCurve().Keys;
			int32 ClosestKeyIndex = -1;
			float ClosestDistance = 100000.0f;
			for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Keys.size()); ++KeyIndex)
			{
				const float Distance = std::fabs(Keys[KeyIndex].Time - NewCurveTime);
				if (Distance < ClosestDistance)
				{
					ClosestDistance = Distance;
					ClosestKeyIndex = KeyIndex;
				}
			}
			SelectedKeyIndex = ClosestKeyIndex;
		}
		return true;
	};

	auto DeleteSelectedKey = [&]()
	{
		FActorSequenceBinding* Binding = nullptr;
		FActorSequenceTrack* Track = nullptr;
		FActorSequenceSection* Section = nullptr;
		FActorSequenceChannel* Channel = nullptr;
		if (!ResolveSelectedChannel(Binding, Track, Section, Channel)
			|| !Channel)
		{
			return false;
		}

		UFloatCurveAsset* EditableCurve = EnsureActorSequenceEditableInlineCurve(Sequence, *Channel);
		if (!EditableCurve)
		{
			return false;
		}

		FFloatCurve& Curve = EditableCurve->GetCurve();
		if (SelectedKeyIndex < 0 || SelectedKeyIndex >= static_cast<int32>(Curve.Keys.size()))
		{
			return false;
		}

		ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Delete Actor Sequence Key");
		Curve.Keys.erase(Curve.Keys.begin() + SelectedKeyIndex);
		Curve.AutoSetTangents();
		SelectedKeyIndex = -1;
		SequenceComp->CommitSequenceEditsForSerialization();
		MarkDirty();
		return true;
	};

	auto DeleteSelectedChannel = [&]()
	{
		TArray<FActorSequenceBinding>& Bindings = Sequence->GetBindings();
		if (SelectedBindingIndex < 0 || SelectedBindingIndex >= static_cast<int32>(Bindings.size()))
		{
			return false;
		}
		FActorSequenceBinding& Binding = Bindings[SelectedBindingIndex];
		if (SelectedTrackIndex < 0 || SelectedTrackIndex >= static_cast<int32>(Binding.Tracks.size()))
		{
			return false;
		}
		FActorSequenceTrack& Track = Binding.Tracks[SelectedTrackIndex];
		if (SelectedSectionIndex < 0 || SelectedSectionIndex >= static_cast<int32>(Track.Sections.size()))
		{
			return false;
		}
		FActorSequenceSection& Section = Track.Sections[SelectedSectionIndex];
		if (SelectedChannelIndex < 0 || SelectedChannelIndex >= static_cast<int32>(Section.Channels.size()))
		{
			return false;
		}

		ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Delete Actor Sequence Track");
		Section.Channels.erase(Section.Channels.begin() + SelectedChannelIndex);
		if (Section.Channels.empty())
		{
			Track.Sections.erase(Track.Sections.begin() + SelectedSectionIndex);
		}
		if (Track.Sections.empty())
		{
			Binding.Tracks.erase(Binding.Tracks.begin() + SelectedTrackIndex);
		}
		if (Binding.Tracks.empty())
		{
			Bindings.erase(Bindings.begin() + SelectedBindingIndex);
		}

		SelectedBindingIndex = -1;
		SelectedTrackIndex = -1;
		SelectedSectionIndex = -1;
		SelectedChannelIndex = -1;
		SelectedKeyIndex = -1;
		ResetTimelineDrags();
		SequenceComp->CommitSequenceEditsForSerialization();
		MarkDirty();
		return true;
	};

	ImGui::InvisibleButton("##ActorSequenceTimelineCanvas", CanvasSize);
	const bool bCanvasActive = ImGui::IsItemActive();
	const bool bCanvasHovered = ImGui::IsItemHovered();
	const bool bCanvasActivated = ImGui::IsItemActivated();
	const bool bMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		FTimelineKeyHit KeyHit;
		FTimelineSectionHit SectionHit;
		if (FindKeyHit(ImGui::GetMousePos(), KeyHit))
		{
			SelectedBindingIndex = KeyHit.BindingIndex;
			SelectedTrackIndex = KeyHit.TrackIndex;
			SelectedSectionIndex = KeyHit.SectionIndex;
			SelectedChannelIndex = KeyHit.ChannelIndex;
			SelectedKeyIndex = KeyHit.KeyIndex;
		}
		else if (FindSectionHit(ImGui::GetMousePos(), SectionHit))
		{
			SelectedBindingIndex = SectionHit.BindingIndex;
			SelectedTrackIndex = SectionHit.TrackIndex;
			SelectedSectionIndex = SectionHit.SectionIndex;
			SelectedChannelIndex = SectionHit.ChannelIndex;
			SelectedKeyIndex = -1;
		}
		ImGui::OpenPopup("ActorSequenceTimelineContext");
	}
	if (ImGui::BeginPopup("ActorSequenceTimelineContext"))
	{
		FActorSequenceBinding* Binding = nullptr;
		FActorSequenceTrack* Track = nullptr;
		FActorSequenceSection* Section = nullptr;
		FActorSequenceChannel* Channel = nullptr;
		const bool bHasSelectedChannel = ResolveSelectedChannel(Binding, Track, Section, Channel);
		const bool bHasSelectedKey =
			bHasSelectedChannel &&
			Channel &&
			Channel->Playback.Curve &&
			SelectedKeyIndex >= 0 &&
			SelectedKeyIndex < static_cast<int32>(Channel->Playback.Curve->GetCurve().Keys.size());

		if (!bHasSelectedChannel)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::MenuItem("Add Key At Current Time"))
		{
			AddKeyToSelectedChannel();
		}
		if (ImGui::MenuItem("Fit View To Section"))
		{
			FitViewToSelectedSection();
		}
		if (!bHasSelectedChannel)
		{
			ImGui::EndDisabled();
		}

		if (!bHasSelectedKey)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::MenuItem("Delete Key"))
		{
			DeleteSelectedKey();
		}
		if (!bHasSelectedKey)
		{
			ImGui::EndDisabled();
		}

		if (!bHasSelectedChannel)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::MenuItem("Delete Track"))
		{
			DeleteSelectedChannel();
		}
		if (!bHasSelectedChannel)
		{
			ImGui::EndDisabled();
		}

		if (ImGui::MenuItem("Fit View To Playback Range"))
		{
			ViewStartTime = Sequence->GetStartTime();
			ViewEndTime = (std::max)(Sequence->GetStartTime() + 1.0f, Sequence->GetEndTime());
		}

		ImGui::EndPopup();
	}
	if ((bCanvasHovered && FindPlaybackRangeHit(ImGui::GetMousePos()) != 0) || bDraggingPlaybackRange)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}
	if (bCanvasActivated)
	{
		const int32 PlaybackRangeEdge = FindPlaybackRangeHit(ImGui::GetMousePos());
		if (PlaybackRangeEdge != 0)
		{
			ResetTimelineKeyDrag();
			ResetTimelineSectionDrag();
			bSuppressTimelineScrubUntilMouseUp = true;
			DraggingPlaybackRangeEdge = PlaybackRangeEdge;
			bDraggingPlaybackRange = true;
			BeginSequenceDragUndo(SequenceComp, "Resize Actor Sequence Playback Range");
		}
		else
		{
			FTimelineKeyHit Hit;
			if (FindKeyHit(ImGui::GetMousePos(), Hit))
			{
				ResetTimelineSectionDrag();
				ResetPlaybackRangeDrag();
				bSuppressTimelineScrubUntilMouseUp = true;
				SelectedBindingIndex = Hit.BindingIndex;
				SelectedTrackIndex = Hit.TrackIndex;
				SelectedSectionIndex = Hit.SectionIndex;
				SelectedChannelIndex = Hit.ChannelIndex;
				SelectedKeyIndex = Hit.KeyIndex;
				DraggingKeyBindingIndex = Hit.BindingIndex;
				DraggingKeyTrackIndex = Hit.TrackIndex;
				DraggingKeySectionIndex = Hit.SectionIndex;
				DraggingKeyChannelIndex = Hit.ChannelIndex;
				DraggingKeyIndex = Hit.KeyIndex;
				bDraggingTimelineKey = true;
				BeginSequenceDragUndo(SequenceComp, "Move Actor Sequence Key");
			}
			else
			{
				FTimelineSectionHit SectionHit;
				if (FindSectionHit(ImGui::GetMousePos(), SectionHit))
				{
					ResetTimelineKeyDrag();
					ResetPlaybackRangeDrag();
					bSuppressTimelineScrubUntilMouseUp = true;
					SelectedBindingIndex = SectionHit.BindingIndex;
					SelectedTrackIndex = SectionHit.TrackIndex;
					SelectedSectionIndex = SectionHit.SectionIndex;
					SelectedChannelIndex = SectionHit.ChannelIndex;
					SelectedKeyIndex = -1;
					if (SectionHit.Edge != 0)
					{
						DraggingSectionBindingIndex = SectionHit.BindingIndex;
						DraggingSectionTrackIndex = SectionHit.TrackIndex;
						DraggingSectionSectionIndex = SectionHit.SectionIndex;
						DraggingSectionChannelIndex = SectionHit.ChannelIndex;
						DraggingSectionEdge = SectionHit.Edge;
						bDraggingTimelineSection = true;
						BeginSequenceDragUndo(SequenceComp, "Resize Actor Sequence Section");
					}
				}
				else
				{
					ResetTimelineDrags();
					bSuppressTimelineScrubUntilMouseUp = false;
					const float ScrubTime = (std::clamp)(XToTime(ImGui::GetMousePos().x), Sequence->GetStartTime(), Sequence->GetEndTime());
					SequenceComp->SetPreviewTime(ScrubTime);
				}
			}
		}
	}
	if (bCanvasActive && bMouseDown)
	{
		if (bDraggingPlaybackRange)
		{
			UpdateDraggingPlaybackRange();
		}
		else if (bDraggingTimelineKey)
		{
			UpdateDraggingKeyTime();
		}
		else if (bDraggingTimelineSection)
		{
			UpdateDraggingSectionTime();
		}
		else if (!bSuppressTimelineScrubUntilMouseUp)
		{
			const float ScrubTime = (std::clamp)(XToTime(ImGui::GetMousePos().x), Sequence->GetStartTime(), Sequence->GetEndTime());
			SequenceComp->SetPreviewTime(ScrubTime);
		}
	}
	if (bDraggingPlaybackRange && !bMouseDown)
	{
		SequenceComp->CommitSequenceEditsForSerialization();
		MarkDirty();
		EndSequenceDragUndo(SequenceComp);
		ResetPlaybackRangeDrag();
	}
	if (bDraggingTimelineKey && !bMouseDown)
	{
		FActorSequenceSection* Section = nullptr;
		FActorSequenceChannel* Channel = nullptr;
		FCurveKey* Key = nullptr;
		if (GetDraggingKey(Section, Channel, Key) && Channel->Playback.Curve)
		{
			const float MovedCurveTime = Key->Time;
			FFloatCurve& Curve = Channel->Playback.Curve->GetCurve();
			Curve.SortKeys();
			Curve.AutoSetTangents();
			int32 ClosestKeyIndex = -1;
			float ClosestDistance = 100000.0f;
			for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Curve.Keys.size()); ++KeyIndex)
			{
				const float Distance = std::fabs(Curve.Keys[KeyIndex].Time - MovedCurveTime);
				if (Distance < ClosestDistance)
				{
					ClosestDistance = Distance;
					ClosestKeyIndex = KeyIndex;
				}
			}
			SelectedKeyIndex = ClosestKeyIndex;
			SequenceComp->CommitSequenceEditsForSerialization();
			MarkDirty();
		}
		EndSequenceDragUndo(SequenceComp);
		ResetTimelineKeyDrag();
	}
	if (bDraggingTimelineSection && !bMouseDown)
	{
		FActorSequenceSection* Section = nullptr;
		if (GetDraggingSection(Section))
		{
			Sequence->SetDuration((std::max)(Sequence->GetDuration(), Section->StartTime + Section->Duration - Sequence->GetStartTime()));
			SequenceComp->CommitSequenceEditsForSerialization();
			MarkDirty();
		}
		EndSequenceDragUndo(SequenceComp);
		ResetTimelineSectionDrag();
	}
	if (!bMouseDown)
	{
		bSuppressTimelineScrubUntilMouseUp = false;
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(CanvasMin, CanvasMax, IM_COL32(22, 23, 27, 255), 4.0f);
	DrawList->AddRect(CanvasMin, CanvasMax, IM_COL32(82, 86, 96, 255), 4.0f);

	for (int32 GridIndex = 0; GridIndex <= 10; ++GridIndex)
	{
		const float Alpha = static_cast<float>(GridIndex) / 10.0f;
		const float X = CanvasMin.x + CanvasSize.x * Alpha;
		DrawList->AddLine(ImVec2(X, CanvasMin.y), ImVec2(X, CanvasMax.y), IM_COL32(46, 48, 56, 255));
		const float Time = ViewStartTime + ViewRange * Alpha;
		char Buffer[32] = {};
		std::snprintf(Buffer, sizeof(Buffer), "%.2f", Time);
		DrawList->AddText(ImVec2(X + 4.0f, CanvasMin.y + 4.0f), IM_COL32(150, 154, 166, 255), Buffer);
	}

	const float PlaybackStartX = TimeToX(Sequence->GetStartTime());
	const float PlaybackEndX = TimeToX(Sequence->GetEndTime());
	const float RangeBarY = CanvasMin.y + 23.0f;
	const float RangeStartX = (std::clamp)(PlaybackStartX, CanvasMin.x, CanvasMax.x);
	const float RangeEndX = (std::clamp)(PlaybackEndX, CanvasMin.x, CanvasMax.x);
	DrawList->AddLine(
		ImVec2(CanvasMin.x + 4.0f, RangeBarY),
		ImVec2(CanvasMax.x - 4.0f, RangeBarY),
		IM_COL32(78, 82, 94, 255),
		2.0f);
	DrawList->AddLine(
		ImVec2(RangeStartX, RangeBarY),
		ImVec2(RangeEndX, RangeBarY),
		IM_COL32(118, 184, 255, 255),
		4.0f);
	auto DrawPlaybackHandle = [&](float X, bool bActive)
	{
		if (X < CanvasMin.x - 1.0f || X > CanvasMax.x + 1.0f)
		{
			return;
		}

		const ImU32 Color = bActive ? IM_COL32(255, 206, 92, 255) : IM_COL32(151, 205, 255, 255);
		DrawList->AddRectFilled(
			ImVec2(X - 4.0f, RangeBarY - 8.0f),
			ImVec2(X + 4.0f, RangeBarY + 8.0f),
			Color,
			2.0f);
		DrawList->AddRect(
			ImVec2(X - 4.0f, RangeBarY - 8.0f),
			ImVec2(X + 4.0f, RangeBarY + 8.0f),
			IM_COL32(20, 22, 26, 210),
			2.0f);
	};
	DrawPlaybackHandle(PlaybackStartX, bDraggingPlaybackRange && DraggingPlaybackRangeEdge == 1);
	DrawPlaybackHandle(PlaybackEndX, bDraggingPlaybackRange && DraggingPlaybackRangeEdge == 2);

	const float PreviewX = TimeToX(SequenceComp->GetPreviewTime());
	if (PreviewX >= CanvasMin.x && PreviewX <= CanvasMax.x)
	{
		DrawList->AddLine(ImVec2(PreviewX, CanvasMin.y), ImVec2(PreviewX, CanvasMax.y), IM_COL32(255, 206, 92, 255), 2.0f);
	}

	TArray<FActorSequenceBinding>& Bindings = Sequence->GetBindings();
	float RowY = CanvasMin.y + 28.0f;
	int32 DisplayIndex = 0;
	for (int32 BindingIndex = 0; BindingIndex < static_cast<int32>(Bindings.size()); ++BindingIndex)
	{
		FActorSequenceBinding& Binding = Bindings[BindingIndex];
		for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(Binding.Tracks.size()); ++TrackIndex)
		{
			FActorSequenceTrack& Track = Binding.Tracks[TrackIndex];
			for (int32 SectionIndex = 0; SectionIndex < static_cast<int32>(Track.Sections.size()); ++SectionIndex)
			{
				FActorSequenceSection& Section = Track.Sections[SectionIndex];
				for (int32 ChannelIndex = 0; ChannelIndex < static_cast<int32>(Section.Channels.size()); ++ChannelIndex)
				{
					FActorSequenceChannel& Channel = Section.Channels[ChannelIndex];
					const int32 DisplayRowIndex = DisplayIndex++;
					const float RowMinY = RowY + static_cast<float>(DisplayRowIndex) * 24.0f;
					const float RowMaxY = RowMinY + 18.0f;
					if (RowMinY > CanvasMax.y - 10.0f)
					{
						continue;
					}

					const bool bSelected =
						SelectedBindingIndex == BindingIndex &&
						SelectedTrackIndex == TrackIndex &&
						SelectedSectionIndex == SectionIndex &&
						SelectedChannelIndex == ChannelIndex;
					const ImU32 SectionColor = bSelected ? IM_COL32(90, 164, 255, 235) : IM_COL32(84, 116, 156, 220);
					const float SectionStartX = TimeToX(Section.StartTime);
					const float SectionEndX = TimeToX(Section.StartTime + Section.Duration);
					const ImVec2 SectionMin(SectionStartX, RowMinY);
					const ImVec2 SectionMax(SectionEndX, RowMaxY);
					DrawList->AddRectFilled(
						SectionMin,
						SectionMax,
						SectionColor,
						3.0f);
					DrawList->AddRect(
						SectionMin,
						SectionMax,
						bSelected ? IM_COL32(255, 255, 255, 230) : IM_COL32(130, 150, 180, 110),
						3.0f,
						0,
						bSelected ? 1.8f : 1.0f);

					const float HandleWidth = 4.0f;
					const ImU32 HandleColor = bSelected ? IM_COL32(255, 206, 92, 255) : IM_COL32(190, 200, 220, 150);
					DrawList->AddRectFilled(
						ImVec2(SectionStartX, RowMinY + 2.0f),
						ImVec2(SectionStartX + HandleWidth, RowMaxY - 2.0f),
						HandleColor,
						1.5f);
					DrawList->AddRectFilled(
						ImVec2(SectionEndX - HandleWidth, RowMinY + 2.0f),
						ImVec2(SectionEndX, RowMaxY - 2.0f),
						HandleColor,
						1.5f);

					if (Channel.Playback.Curve)
					{
						const TArray<FCurveKey>& Keys = Channel.Playback.Curve->GetCurve().Keys;
						for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Keys.size()); ++KeyIndex)
						{
							const FCurveKey& Key = Keys[KeyIndex];
							const float KeySequenceTime = CurveTimeToSequenceTime(Section, Channel, Key.Time);
							const float X = TimeToX(KeySequenceTime);
							if (X >= CanvasMin.x && X <= CanvasMax.x)
							{
								const bool bSelectedKey = bSelected && SelectedKeyIndex == KeyIndex;
								const ImVec2 KeyCenter(X, (RowMinY + RowMaxY) * 0.5f);
								DrawList->AddCircleFilled(
									KeyCenter,
									bSelectedKey ? 5.5f : 4.0f,
									bSelectedKey ? IM_COL32(255, 255, 255, 255) : IM_COL32(252, 232, 138, 255));
								if (bSelectedKey)
								{
									DrawList->AddCircle(KeyCenter, 7.0f, IM_COL32(255, 206, 92, 255), 16, 1.5f);
								}
							}
						}
					}
				}
			}
		}
	}
}

void FActorSequenceEditorWidget::FitCurveView(const FFloatCurve& Curve)
{
	if (Curve.Keys.empty())
	{
		CurveViewMinTime = 0.0f;
		CurveViewMaxTime = 1.0f;
		CurveViewMinValue = -1.0f;
		CurveViewMaxValue = 1.0f;
		return;
	}

	CurveViewMinTime = Curve.Keys.front().Time;
	CurveViewMaxTime = Curve.Keys.front().Time;
	CurveViewMinValue = Curve.Keys.front().Value;
	CurveViewMaxValue = Curve.Keys.front().Value;
	for (const FCurveKey& Key : Curve.Keys)
	{
		CurveViewMinTime = (std::min)(CurveViewMinTime, Key.Time);
		CurveViewMaxTime = (std::max)(CurveViewMaxTime, Key.Time);
		CurveViewMinValue = (std::min)(CurveViewMinValue, Key.Value);
		CurveViewMaxValue = (std::max)(CurveViewMaxValue, Key.Value);
	}

	if (CurveViewMaxTime <= CurveViewMinTime + 0.001f)
	{
		CurveViewMinTime -= 0.5f;
		CurveViewMaxTime += 0.5f;
	}
	else
	{
		const float TimePadding = (CurveViewMaxTime - CurveViewMinTime) * 0.12f;
		CurveViewMinTime -= TimePadding;
		CurveViewMaxTime += TimePadding;
	}
	CurveViewMinTime = (std::max)(0.0f, CurveViewMinTime);

	if (CurveViewMaxValue <= CurveViewMinValue + 0.001f)
	{
		CurveViewMinValue -= 0.5f;
		CurveViewMaxValue += 0.5f;
	}
	else
	{
		const float ValuePadding = (CurveViewMaxValue - CurveViewMinValue) * 0.18f;
		CurveViewMinValue -= ValuePadding;
		CurveViewMaxValue += ValuePadding;
	}
}

void FActorSequenceEditorWidget::RenderCurveCanvas(
	UActorSequenceComponent* SequenceComp,
	FActorSequenceSection& Section,
	FActorSequenceChannel& Channel)
{
	UFloatCurveAsset* CurveAsset = Channel.Playback.Curve;
	if (!CurveAsset)
	{
		ImGui::TextDisabled("No curve is loaded for the selected channel.");
		return;
	}

	FFloatCurve& Curve = CurveAsset->GetCurve();
	if (CurveViewBindingIndex != SelectedBindingIndex
		|| CurveViewTrackIndex != SelectedTrackIndex
		|| CurveViewSectionIndex != SelectedSectionIndex
		|| CurveViewChannelIndex != SelectedChannelIndex)
	{
		CurveViewBindingIndex = SelectedBindingIndex;
		CurveViewTrackIndex = SelectedTrackIndex;
		CurveViewSectionIndex = SelectedSectionIndex;
		CurveViewChannelIndex = SelectedChannelIndex;
		bDraggingCurveKey = false;
		DraggingCurveKeyIndex = -1;
		DraggingCurveTangentHandle = 0;
		FitCurveView(Curve);
	}

	if (ImGui::Button("Fit Curve"))
	{
		FitCurveView(Curve);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("Double-click: add key  |  Drag key/tangent: edit");

	const ImVec2 CanvasSize((std::max)(320.0f, ImGui::GetContentRegionAvail().x), 190.0f);
	const ImVec2 CanvasMin = ImGui::GetCursorScreenPos();
	const ImVec2 CanvasMax(CanvasMin.x + CanvasSize.x, CanvasMin.y + CanvasSize.y);
	ImGui::InvisibleButton("##ActorSequenceCurveCanvas", CanvasSize);
	const bool bCanvasHovered = ImGui::IsItemHovered();
	const bool bCanvasActive = ImGui::IsItemActive();
	const ImVec2 MousePos = ImGui::GetMousePos();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	DrawList->AddRectFilled(CanvasMin, CanvasMax, IM_COL32(19, 20, 24, 255), 4.0f);
	DrawList->AddRect(CanvasMin, CanvasMax, IM_COL32(78, 82, 92, 255), 4.0f);
	for (int32 GridIndex = 1; GridIndex < 8; ++GridIndex)
	{
		const float Alpha = static_cast<float>(GridIndex) / 8.0f;
		const ImU32 GridColor = GridIndex == 4 ? IM_COL32(70, 74, 84, 255) : IM_COL32(43, 46, 54, 255);
		const float X = CanvasMin.x + CanvasSize.x * Alpha;
		const float Y = CanvasMin.y + CanvasSize.y * Alpha;
		DrawList->AddLine(ImVec2(X, CanvasMin.y), ImVec2(X, CanvasMax.y), GridColor);
		DrawList->AddLine(ImVec2(CanvasMin.x, Y), ImVec2(CanvasMax.x, Y), GridColor);
	}

	char MinLabel[64] = {};
	char MaxLabel[64] = {};
	std::snprintf(MinLabel, sizeof(MinLabel), "t %.2f  v %.2f", CurveViewMinTime, CurveViewMinValue);
	std::snprintf(MaxLabel, sizeof(MaxLabel), "t %.2f  v %.2f", CurveViewMaxTime, CurveViewMaxValue);
	DrawList->AddText(ImVec2(CanvasMin.x + 6.0f, CanvasMax.y - 18.0f), IM_COL32(142, 148, 160, 255), MinLabel);
	DrawList->AddText(ImVec2(CanvasMax.x - ImGui::CalcTextSize(MaxLabel).x - 6.0f, CanvasMin.y + 5.0f), IM_COL32(142, 148, 160, 255), MaxLabel);

	if (!Curve.IsEmpty())
	{
		constexpr int32 SampleCount = 160;
		ImVec2 Previous = ActorSequenceCurveToScreen(
			CurveViewMinTime,
			Curve.Evaluate(CurveViewMinTime),
			CurveViewMinTime,
			CurveViewMaxTime,
			CurveViewMinValue,
			CurveViewMaxValue,
			CanvasMin,
			CanvasMax);
		for (int32 SampleIndex = 1; SampleIndex < SampleCount; ++SampleIndex)
		{
			const float Alpha = static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
			const float SampleTime = CurveViewMinTime + (CurveViewMaxTime - CurveViewMinTime) * Alpha;
			const ImVec2 Current = ActorSequenceCurveToScreen(
				SampleTime,
				Curve.Evaluate(SampleTime),
				CurveViewMinTime,
				CurveViewMaxTime,
				CurveViewMinValue,
				CurveViewMaxValue,
				CanvasMin,
				CanvasMax);
			if (std::isfinite(Previous.x) && std::isfinite(Previous.y)
				&& std::isfinite(Current.x) && std::isfinite(Current.y))
			{
				DrawList->AddLine(Previous, Current, IM_COL32(118, 210, 148, 255), 2.0f);
			}
			Previous = Current;
		}
	}

	int32 HoveredKeyIndex = -1;
	float HoveredDistanceSq = 100000.0f;
	for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Curve.Keys.size()); ++KeyIndex)
	{
		const FCurveKey& Key = Curve.Keys[KeyIndex];
		const ImVec2 KeyPos = ActorSequenceCurveToScreen(
			Key.Time,
			Key.Value,
			CurveViewMinTime,
			CurveViewMaxTime,
			CurveViewMinValue,
			CurveViewMaxValue,
			CanvasMin,
			CanvasMax);
		const float DX = MousePos.x - KeyPos.x;
		const float DY = MousePos.y - KeyPos.y;
		const float DistanceSq = DX * DX + DY * DY;
		if (bCanvasHovered && DistanceSq <= CurveKeyHitRadius * CurveKeyHitRadius && DistanceSq < HoveredDistanceSq)
		{
			HoveredDistanceSq = DistanceSq;
			HoveredKeyIndex = KeyIndex;
		}

		const bool bSelected = SelectedKeyIndex == KeyIndex;
		DrawList->AddCircleFilled(
			KeyPos,
			bSelected ? 5.5f : 4.2f,
			bSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 218, 118, 255));
		if (bSelected)
		{
			DrawList->AddCircle(KeyPos, 7.5f, IM_COL32(255, 206, 92, 255), 20, 1.5f);
		}
	}

	int32 HoveredTangentHandle = 0;
	const bool bHasSelectedKey =
		SelectedKeyIndex >= 0 && SelectedKeyIndex < static_cast<int32>(Curve.Keys.size());
	if (bHasSelectedKey)
	{
		const FCurveKey& Key = Curve.Keys[SelectedKeyIndex];
		const ImVec2 KeyPos = ActorSequenceCurveToScreen(
			Key.Time,
			Key.Value,
			CurveViewMinTime,
			CurveViewMaxTime,
			CurveViewMinValue,
			CurveViewMaxValue,
			CanvasMin,
			CanvasMax);
		const bool bShowArrive = SelectedKeyIndex > 0
			&& Curve.Keys[SelectedKeyIndex - 1].InterpMode == ECurveInterpMode::Cubic;
		const bool bShowLeave = SelectedKeyIndex + 1 < static_cast<int32>(Curve.Keys.size())
			&& Key.InterpMode == ECurveInterpMode::Cubic;
		auto DrawTangentHandle = [&](bool bArrive, int32 HandleId)
		{
			const ImVec2 HandlePos = ActorSequenceTangentHandlePosition(
				Key,
				bArrive,
				CurveViewMinTime,
				CurveViewMaxTime,
				CurveViewMinValue,
				CurveViewMaxValue,
				CanvasMin,
				CanvasMax);
			DrawList->AddLine(KeyPos, HandlePos, IM_COL32(104, 162, 255, 190), 1.5f);
			DrawList->AddCircleFilled(HandlePos, 4.5f, IM_COL32(104, 162, 255, 255));
			DrawList->AddCircle(HandlePos, 4.5f, IM_COL32(18, 24, 34, 230));
			if (bCanvasHovered && ActorSequenceIsPointNear(MousePos, HandlePos, 7.0f))
			{
				HoveredTangentHandle = HandleId;
			}
		};
		if (bShowArrive) DrawTangentHandle(true, 1);
		if (bShowLeave) DrawTangentHandle(false, 2);
	}

	if (HoveredKeyIndex >= 0 || HoveredTangentHandle != 0 || bDraggingCurveKey || DraggingCurveTangentHandle != 0)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}

	auto CommitCurveCanvasEdit = [&]()
	{
		Curve.AutoSetTangents();
		SequenceComp->CommitSequenceEditsForSerialization();
		SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
		MarkDirty();
	};

	if (bCanvasHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		float NewTime = 0.0f;
		float NewValue = 0.0f;
		ActorSequenceScreenToCurve(MousePos, CurveViewMinTime, CurveViewMaxTime, CurveViewMinValue, CurveViewMaxValue, CanvasMin, CanvasMax, NewTime, NewValue);
		ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Add Actor Sequence Curve Key");
		Curve.AddKey((std::max)(0.0f, NewTime), NewValue, ECurveInterpMode::Cubic);
		SelectedKeyIndex = SortCurveAndFindKey(Curve, (std::max)(0.0f, NewTime), NewValue);
		CommitCurveCanvasEdit();
	}
	else if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		if (HoveredTangentHandle != 0 && bHasSelectedKey)
		{
			DraggingCurveTangentHandle = HoveredTangentHandle;
			bDraggingCurveKey = false;
			DraggingCurveKeyIndex = -1;
			BeginSequenceDragUndo(SequenceComp, "Edit Actor Sequence Curve Tangent");
		}
		else if (HoveredKeyIndex >= 0)
		{
			SelectedKeyIndex = HoveredKeyIndex;
			DraggingCurveKeyIndex = HoveredKeyIndex;
			bDraggingCurveKey = true;
			DraggingCurveTangentHandle = 0;
			BeginSequenceDragUndo(SequenceComp, "Move Actor Sequence Curve Key");
		}
		else
		{
			SelectedKeyIndex = -1;
			bDraggingCurveKey = false;
			DraggingCurveKeyIndex = -1;
			DraggingCurveTangentHandle = 0;
		}
	}

	if (bCanvasActive && bDraggingCurveKey
		&& DraggingCurveKeyIndex >= 0
		&& DraggingCurveKeyIndex < static_cast<int32>(Curve.Keys.size())
		&& ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		float NewTime = 0.0f;
		float NewValue = 0.0f;
		ActorSequenceScreenToCurve(MousePos, CurveViewMinTime, CurveViewMaxTime, CurveViewMinValue, CurveViewMaxValue, CanvasMin, CanvasMax, NewTime, NewValue);
		FCurveKey& Key = Curve.Keys[DraggingCurveKeyIndex];
		Key.Time = (std::max)(0.0f, NewTime);
		Key.Value = NewValue;
		CommitCurveCanvasEdit();
	}

	if (bCanvasActive && DraggingCurveTangentHandle != 0 && bHasSelectedKey && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		FCurveKey& Key = Curve.Keys[SelectedKeyIndex];
		float MouseTime = 0.0f;
		float MouseValue = 0.0f;
		ActorSequenceScreenToCurve(MousePos, CurveViewMinTime, CurveViewMaxTime, CurveViewMinValue, CurveViewMaxValue, CanvasMin, CanvasMax, MouseTime, MouseValue);
		float NewTangent = 0.0f;
		if (DraggingCurveTangentHandle == 1)
		{
			const float DeltaTime = Key.Time - MouseTime;
			NewTangent = std::fabs(DeltaTime) > 0.001f ? (Key.Value - MouseValue) / DeltaTime : Key.ArriveTangent;
		}
		else
		{
			const float DeltaTime = MouseTime - Key.Time;
			NewTangent = std::fabs(DeltaTime) > 0.001f ? (MouseValue - Key.Value) / DeltaTime : Key.LeaveTangent;
		}

		if (Key.TangentMode == ECurveTangentMode::Auto)
		{
			Key.TangentMode = ECurveTangentMode::User;
		}
		if (Key.TangentMode == ECurveTangentMode::Break)
		{
			if (DraggingCurveTangentHandle == 1) Key.ArriveTangent = NewTangent;
			else Key.LeaveTangent = NewTangent;
		}
		else
		{
			Key.ArriveTangent = NewTangent;
			Key.LeaveTangent = NewTangent;
		}
		SequenceComp->CommitSequenceEditsForSerialization();
		SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
		MarkDirty();
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		const bool bWasDraggingCurve = bDraggingCurveKey || DraggingCurveTangentHandle != 0;
		if (bDraggingCurveKey
			&& DraggingCurveKeyIndex >= 0
			&& DraggingCurveKeyIndex < static_cast<int32>(Curve.Keys.size()))
		{
			const float FinalTime = Curve.Keys[DraggingCurveKeyIndex].Time;
			const float FinalValue = Curve.Keys[DraggingCurveKeyIndex].Value;
			SelectedKeyIndex = SortCurveAndFindKey(Curve, FinalTime, FinalValue);
			Curve.AutoSetTangents();
			SequenceComp->CommitSequenceEditsForSerialization();
			MarkDirty();
		}
		if (bWasDraggingCurve)
		{
			EndSequenceDragUndo(SequenceComp);
		}
		bDraggingCurveKey = false;
		DraggingCurveKeyIndex = -1;
		DraggingCurveTangentHandle = 0;
	}

	if (HoveredKeyIndex >= 0)
	{
		const FCurveKey& HoveredKey = Curve.Keys[HoveredKeyIndex];
		ImGui::SetTooltip("Key %d\nTime %.3f\nValue %.4f", HoveredKeyIndex, HoveredKey.Time, HoveredKey.Value);
	}

	if (ImGui::BeginPopupContextItem("ActorSequenceCurveCanvasContext"))
	{
		if (HoveredKeyIndex < 0)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::MenuItem("Delete Key"))
		{
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Delete Actor Sequence Curve Key");
			Curve.Keys.erase(Curve.Keys.begin() + HoveredKeyIndex);
			if (SelectedKeyIndex == HoveredKeyIndex) SelectedKeyIndex = -1;
			else if (SelectedKeyIndex > HoveredKeyIndex) --SelectedKeyIndex;
			CommitCurveCanvasEdit();
		}
		if (HoveredKeyIndex < 0)
		{
			ImGui::EndDisabled();
		}

		if (ImGui::MenuItem("Add Key Here"))
		{
			float NewTime = 0.0f;
			float NewValue = 0.0f;
			ActorSequenceScreenToCurve(MousePos, CurveViewMinTime, CurveViewMaxTime, CurveViewMinValue, CurveViewMaxValue, CanvasMin, CanvasMax, NewTime, NewValue);
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Add Actor Sequence Curve Key");
			Curve.AddKey((std::max)(0.0f, NewTime), NewValue, ECurveInterpMode::Cubic);
			SelectedKeyIndex = SortCurveAndFindKey(Curve, (std::max)(0.0f, NewTime), NewValue);
			CommitCurveCanvasEdit();
		}
		if (ImGui::MenuItem("Fit Curve"))
		{
			FitCurveView(Curve);
		}
		ImGui::EndPopup();
	}

	(void)Section;
}

void FActorSequenceEditorWidget::RenderKeyTable(UActorSequenceComponent* SequenceComp)
{
	UActorSequence* Sequence = SequenceComp ? SequenceComp->GetSequence() : nullptr;
	if (!Sequence)
	{
		return;
	}

	TArray<FActorSequenceBinding>& Bindings = Sequence->GetBindings();
	if (SelectedBindingIndex < 0 || SelectedBindingIndex >= static_cast<int32>(Bindings.size()))
	{
		ImGui::TextDisabled("Select a track to edit keys.");
		return;
	}

	FActorSequenceBinding& Binding = Bindings[SelectedBindingIndex];
	if (SelectedTrackIndex < 0 || SelectedTrackIndex >= static_cast<int32>(Binding.Tracks.size()))
	{
		ImGui::TextDisabled("Select a track to edit keys.");
		return;
	}

	FActorSequenceTrack& Track = Binding.Tracks[SelectedTrackIndex];
	if (SelectedSectionIndex < 0 || SelectedSectionIndex >= static_cast<int32>(Track.Sections.size()))
	{
		ImGui::TextDisabled("Select a track to edit keys.");
		return;
	}

	FActorSequenceSection& Section = Track.Sections[SelectedSectionIndex];
	if (SelectedChannelIndex < 0 || SelectedChannelIndex >= static_cast<int32>(Section.Channels.size()))
	{
		ImGui::TextDisabled("Select a track to edit keys.");
		return;
	}

	FActorSequenceChannel& Channel = Section.Channels[SelectedChannelIndex];
	ImGui::Text("Selected: %s.%s", Track.PropertyName.c_str(), Channel.ChannelName.c_str());

	const char* ApplyModeLabels[] = { "Absolute", "Additive" };
	int32 ApplyModeIndex = Channel.Playback.ApplyMode == ECurveApplyMode::Additive ? 1 : 0;
	ImGui::SetNextItemWidth(150.0f);
	if (ImGui::BeginCombo("Apply Mode", ApplyModeLabels[ApplyModeIndex]))
	{
		for (int32 ModeIndex = 0; ModeIndex < 2; ++ModeIndex)
		{
			const bool bSelected = ApplyModeIndex == ModeIndex;
			if (ImGui::Selectable(ApplyModeLabels[ModeIndex], bSelected))
			{
				ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Apply Mode");
				Channel.Playback.ApplyMode = ModeIndex == 1
					? ECurveApplyMode::Additive
					: ECurveApplyMode::Absolute;
				SequenceComp->CommitSequenceEditsForSerialization();
				MarkDirty();
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	const char* TimeMappingLabels[] = { "Seconds", "Normalized" };
	int32 TimeMappingIndex = Channel.Playback.TimeMappingMode == ECurveTimeMappingMode::NormalizedTime ? 1 : 0;
	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::BeginCombo("Time Mapping", TimeMappingLabels[TimeMappingIndex]))
	{
		for (int32 ModeIndex = 0; ModeIndex < 2; ++ModeIndex)
		{
			const bool bSelected = TimeMappingIndex == ModeIndex;
			if (ImGui::Selectable(TimeMappingLabels[ModeIndex], bSelected))
			{
				ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Time Mapping");
				Channel.Playback.TimeMappingMode = ModeIndex == 1
					? ECurveTimeMappingMode::NormalizedTime
					: ECurveTimeMappingMode::Seconds;
				SequenceComp->CommitSequenceEditsForSerialization();
				MarkDirty();
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	if (ImGui::Button("Add Key At Current Value"))
	{
		ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Add Actor Sequence Key");
		if (AddKeyAtCurrentValue(
			SequenceComp,
			Binding,
			Track,
			Section,
			Channel,
			SequenceComp->GetPreviewTime()))
		{
			SequenceComp->CommitSequenceEditsForSerialization();
			MarkDirty();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Remove Track"))
	{
		ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Remove Actor Sequence Track");
		Section.Channels.erase(Section.Channels.begin() + SelectedChannelIndex);
		if (Section.Channels.empty())
		{
			Track.Sections.erase(Track.Sections.begin() + SelectedSectionIndex);
		}
		if (Track.Sections.empty())
		{
			Binding.Tracks.erase(Binding.Tracks.begin() + SelectedTrackIndex);
		}
		if (Binding.Tracks.empty())
		{
			Bindings.erase(Bindings.begin() + SelectedBindingIndex);
		}
		SelectedBindingIndex = -1;
		SelectedTrackIndex = -1;
		SelectedSectionIndex = -1;
		SelectedChannelIndex = -1;
		SelectedKeyIndex = -1;
		SequenceComp->CommitSequenceEditsForSerialization();
		MarkDirty();
		return;
	}

	const bool bUsesExternalCurve =
		!Channel.Playback.CurveAssetPath.empty()
		|| (Channel.Playback.Curve && Channel.Playback.Curve->GetOuter() != Sequence);
	if (bUsesExternalCurve)
	{
		ImGui::Separator();
		const FString ExternalCurveLabel = !Channel.Playback.CurveAssetPath.empty()
			? Channel.Playback.CurveAssetPath
			: (Channel.Playback.Curve ? Channel.Playback.Curve->GetSourcePath() : FString());
		ImGui::TextWrapped("External curve: %s", ExternalCurveLabel.empty() ? "(object reference)" : ExternalCurveLabel.c_str());
		ImGui::TextDisabled("Convert to an inline copy before editing keys so changes are saved with this Actor Sequence.");
		if (ImGui::Button("Convert To Inline Copy"))
		{
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Convert Actor Sequence Curve To Inline");
			if (EnsureActorSequenceEditableInlineCurve(Sequence, Channel))
			{
				SequenceComp->CommitSequenceEditsForSerialization();
				SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
				MarkDirty();
			}
		}
		if (!Channel.Playback.CurveAssetPath.empty()
			|| (Channel.Playback.Curve && Channel.Playback.Curve->GetOuter() != Sequence))
		{
			return;
		}
	}

	UFloatCurveAsset* Curve = Channel.Playback.Curve;
	if (!Curve)
	{
		ImGui::TextDisabled("No inline curve is loaded for this channel.");
		return;
	}

	FFloatCurve& FloatCurve = Curve->GetCurve();
	if (SelectedKeyIndex >= static_cast<int32>(FloatCurve.Keys.size()))
	{
		SelectedKeyIndex = -1;
	}

	RenderCurveCanvas(SequenceComp, Section, Channel);
	ImGui::Separator();

	if (ImGui::BeginTable("ActorSequencerKeys", 4,
		ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
	{
		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
		ImGui::TableSetupColumn("Curve Time");
		ImGui::TableSetupColumn("Value");
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.0f);

		for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(FloatCurve.Keys.size()); ++KeyIndex)
		{
			FCurveKey& Key = FloatCurve.Keys[KeyIndex];
			const bool bSelectedKey = SelectedKeyIndex == KeyIndex;
			ImGui::PushID(KeyIndex);
			ImGui::TableNextRow();
			if (bSelectedKey)
			{
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(66, 88, 116, 190));
			}
			ImGui::TableSetColumnIndex(0);
			char KeyLabel[32] = {};
			std::snprintf(KeyLabel, sizeof(KeyLabel), "%d", KeyIndex);
			if (ImGui::Selectable(KeyLabel, bSelectedKey, ImGuiSelectableFlags_SpanAllColumns))
			{
				SelectedKeyIndex = KeyIndex;
			}

			ImGui::TableSetColumnIndex(1);
			float KeyTime = Key.Time;
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::DragFloat("##KeyTime", &KeyTime, 0.01f, 0.0f, 600.0f, "%.3f"))
			{
				ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Key");
				const float NewKeyTime = (std::max)(0.0f, KeyTime);
				Key.Time = NewKeyTime;
				FloatCurve.SortKeys();
				FloatCurve.AutoSetTangents();
				int32 ClosestKeyIndex = -1;
				float ClosestDistance = 100000.0f;
				for (int32 CandidateIndex = 0; CandidateIndex < static_cast<int32>(FloatCurve.Keys.size()); ++CandidateIndex)
				{
					const float Distance = std::fabs(FloatCurve.Keys[CandidateIndex].Time - NewKeyTime);
					if (Distance < ClosestDistance)
					{
						ClosestDistance = Distance;
						ClosestKeyIndex = CandidateIndex;
					}
				}
				SelectedKeyIndex = ClosestKeyIndex;
				SequenceComp->CommitSequenceEditsForSerialization();
				MarkDirty();
				ImGui::PopID();
				ImGui::EndTable();
				return;
			}

			ImGui::TableSetColumnIndex(2);
			float KeyValue = Key.Value;
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::DragFloat("##KeyValue", &KeyValue, 0.01f, -100000.0f, 100000.0f, "%.4f"))
			{
				ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Key");
				Key.Value = KeyValue;
				FloatCurve.AutoSetTangents();
				SequenceComp->CommitSequenceEditsForSerialization();
				MarkDirty();
			}

			ImGui::TableSetColumnIndex(3);
			if (ImGui::Button("Delete"))
			{
				ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Delete Actor Sequence Key");
				FloatCurve.Keys.erase(FloatCurve.Keys.begin() + KeyIndex);
				if (SelectedKeyIndex == KeyIndex)
				{
					SelectedKeyIndex = -1;
				}
				else if (SelectedKeyIndex > KeyIndex)
				{
					--SelectedKeyIndex;
				}
				FloatCurve.AutoSetTangents();
				SequenceComp->CommitSequenceEditsForSerialization();
				MarkDirty();
				ImGui::PopID();
				ImGui::EndTable();
				return;
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::Separator();
	if (SelectedKeyIndex >= 0 && SelectedKeyIndex < static_cast<int32>(FloatCurve.Keys.size()))
	{
		FCurveKey& Key = FloatCurve.Keys[SelectedKeyIndex];
		ImGui::Text("Selected Key: %d", SelectedKeyIndex);

		const char* InterpModeLabels[] = { "Constant", "Linear", "Cubic" };
		int32 InterpModeIndex = static_cast<int32>(Key.InterpMode);
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::Combo("Interpolation Mode", &InterpModeIndex, InterpModeLabels, IM_ARRAYSIZE(InterpModeLabels)))
		{
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Key Interpolation");
			Key.InterpMode = static_cast<ECurveInterpMode>(InterpModeIndex);
			if (Key.InterpMode == ECurveInterpMode::Cubic)
			{
				Key.TangentMode = ECurveTangentMode::Auto;
				FloatCurve.AutoSetTangents();
			}
			SequenceComp->CommitSequenceEditsForSerialization();
			SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
			MarkDirty();
		}

		const char* TangentModeLabels[] = { "Auto", "User", "Break" };
		int32 TangentModeIndex = static_cast<int32>(Key.TangentMode);
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::Combo("Tangent Mode", &TangentModeIndex, TangentModeLabels, IM_ARRAYSIZE(TangentModeLabels)))
		{
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Key Tangent Mode");
			Key.TangentMode = static_cast<ECurveTangentMode>(TangentModeIndex);
			if (Key.TangentMode == ECurveTangentMode::Auto)
			{
				FloatCurve.AutoSetTangents();
			}
			SequenceComp->CommitSequenceEditsForSerialization();
			SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
			MarkDirty();
		}

		float KeyTime = Key.Time;
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::DragFloat("Time", &KeyTime, 0.01f, 0.0f, 600.0f, "%.3f"))
		{
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Key Time");
			Key.Time = (std::max)(0.0f, KeyTime);
			const float NewTime = Key.Time;
			const float NewValue = Key.Value;
			FloatCurve.SortKeys();
			FloatCurve.AutoSetTangents();
			SelectedKeyIndex = SortCurveAndFindKey(FloatCurve, NewTime, NewValue);
			SequenceComp->CommitSequenceEditsForSerialization();
			SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
			MarkDirty();
			return;
		}

		float KeyValue = Key.Value;
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::DragFloat("Value", &KeyValue, 0.01f, -100000.0f, 100000.0f, "%.4f"))
		{
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Key Value");
			Key.Value = KeyValue;
			FloatCurve.AutoSetTangents();
			SequenceComp->CommitSequenceEditsForSerialization();
			SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
			MarkDirty();
		}

		const bool bAutoTangent = Key.TangentMode == ECurveTangentMode::Auto;
		if (bAutoTangent)
		{
			ImGui::BeginDisabled();
		}
		float ArriveTangent = Key.ArriveTangent;
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::DragFloat("Arrive Tangent", &ArriveTangent, 0.01f, -100000.0f, 100000.0f, "%.4f"))
		{
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Key Tangent");
			Key.ArriveTangent = ArriveTangent;
			SequenceComp->CommitSequenceEditsForSerialization();
			SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
			MarkDirty();
		}
		float LeaveTangent = Key.LeaveTangent;
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::DragFloat("Leave Tangent", &LeaveTangent, 0.01f, -100000.0f, 100000.0f, "%.4f"))
		{
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Edit Actor Sequence Key Tangent");
			Key.LeaveTangent = LeaveTangent;
			SequenceComp->CommitSequenceEditsForSerialization();
			SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
			MarkDirty();
		}
		if (bAutoTangent)
		{
			ImGui::EndDisabled();
		}

		if (ImGui::Button("Auto Tangents"))
		{
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Auto Actor Sequence Tangents");
			FloatCurve.AutoSetTangents();
			SequenceComp->CommitSequenceEditsForSerialization();
			SequenceComp->SetPreviewTime(SequenceComp->GetPreviewTime());
			MarkDirty();
		}
		ImGui::SameLine();
		if (ImGui::Button("Sort Keys"))
		{
			ACTOR_SEQUENCE_UNDO_SCOPE(EditorEngine, SequenceComp, "Sort Actor Sequence Keys");
			const float SelectedTime = Key.Time;
			const float SelectedValue = Key.Value;
			FloatCurve.SortKeys();
			SelectedKeyIndex = SortCurveAndFindKey(FloatCurve, SelectedTime, SelectedValue);
			SequenceComp->CommitSequenceEditsForSerialization();
			MarkDirty();
		}
	}
	else
	{
		ImGui::TextDisabled("No key selected.");
	}
}
