#include "Editor/UI/EditorActorSequenceDetails.h"

#include "Animation/ActorSequence.h"
#include "Component/ActorSequenceComponent.h"
#include "Editor/EditorEngine.h"
#include "Editor/UI/EditorMainPanel.h"
#include "GameFramework/AActor.h"
#include "ImGui/imgui.h"
#include <algorithm>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

namespace
{
	static void DrawDetailsSeparator()
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	}

	static void DrawDetailsSectionLabel(const char* Label)
	{
		ImVec2 Pos = ImGui::GetCursorScreenPos();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImU32 Color = ImGui::GetColorU32(ImGuiCol_Text);
		DrawList->AddText(ImVec2(Pos.x + 0.75f, Pos.y), Color, Label);
		ImGui::TextUnformatted(Label);
	}

}

void FEditorActorSequenceDetails::Initialize(UEditorEngine* InEditorEngine, bool* InUndoCaptureFlag)
{
	EditorEngine = InEditorEngine;
	UndoCaptureFlag = InUndoCaptureFlag;
}

void FEditorActorSequenceDetails::MarkEdited(
	UActorSequenceComponent* SequenceComp,
	const char* UndoLabel)
{
	if (!SequenceComp)
	{
		return;
	}

	SequenceComp->MarkSequenceDirty();
	SequenceComp->PostEditProperty("Sequence");

	if (EditorEngine)
	{
		EditorEngine->GetSceneService().MarkDirty();
	}
}

void FEditorActorSequenceDetails::BeginEditUndo(
	UActorSequenceComponent* SequenceComp,
	const char* InUndoLabel)
{
	if (!EditorEngine || !UndoCaptureFlag || *UndoCaptureFlag || !SequenceComp)
	{
		return;
	}

	AActor* Owner = SequenceComp->GetOwner();
	if (!Owner)
	{
		EditorEngine->GetUndoSystem().CaptureSnapshot(InUndoLabel ? InUndoLabel : "Edit Actor Sequence");
		*UndoCaptureFlag = true;
		UndoSequenceComponent = nullptr;
		UndoBeforeActorStates.clear();
		UndoLabel = InUndoLabel ? InUndoLabel : "Edit Actor Sequence";
		return;
	}

	TArray<AActor*> Actors;
	Actors.push_back(Owner);
	UndoBeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(Actors);
	if (!UndoBeforeActorStates.empty())
	{
		*UndoCaptureFlag = true;
		UndoSequenceComponent = SequenceComp;
		UndoLabel = InUndoLabel ? InUndoLabel : "Edit Actor Sequence";
	}
}

void FEditorActorSequenceDetails::CommitEditUndo(
	UActorSequenceComponent* SequenceComp,
	const char* InUndoLabel)
{
	if (!EditorEngine || !UndoCaptureFlag || !*UndoCaptureFlag)
	{
		return;
	}

	if (UndoSequenceComponent == SequenceComp && !UndoBeforeActorStates.empty())
	{
		if (AActor* Owner = SequenceComp ? SequenceComp->GetOwner() : nullptr)
		{
			TArray<AActor*> Actors;
			Actors.push_back(Owner);
			EditorEngine->GetUndoSystem().RecordActorStateChange(
				UndoBeforeActorStates,
				EditorEngine->GetUndoSystem().CaptureActorStates(Actors),
				UndoLabel.empty() ? FString(InUndoLabel ? InUndoLabel : "Edit Actor Sequence") : UndoLabel);
		}
	}

	*UndoCaptureFlag = false;
	UndoSequenceComponent = nullptr;
	UndoBeforeActorStates.clear();
	UndoLabel.clear();
}

void FEditorActorSequenceDetails::Render(UActorSequenceComponent* SequenceComp, float DeltaTime)
{
	(void)DeltaTime;

	UActorSequence* Sequence = SequenceComp ? SequenceComp->GetSequence() : nullptr;
	if (!Sequence)
	{
		return;
	}

	DrawDetailsSeparator();
	DrawDetailsSectionLabel("Actor Sequence");

	if (ImGui::Button("Open Sequencer"))
	{
		EditorEngine->GetMainPanel().OpenActorSequencer(SequenceComp);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("Timeline editor");

	bool bAutoPlay = SequenceComp->IsAutoPlay();
	if (ImGui::Checkbox("Auto Play", &bAutoPlay))
	{
		BeginEditUndo(SequenceComp, "Edit Actor Sequence");
		SequenceComp->SetAutoPlay(bAutoPlay);
		MarkEdited(SequenceComp, "Edit Actor Sequence");
	}

	bool bLooping = SequenceComp->IsLooping();
	if (ImGui::Checkbox("Looping", &bLooping))
	{
		BeginEditUndo(SequenceComp, "Edit Actor Sequence");
		SequenceComp->SetLooping(bLooping);
		MarkEdited(SequenceComp, "Edit Actor Sequence");
	}

	float PlayRate = SequenceComp->GetPlayRate();
	if (ImGui::DragFloat("Play Rate", &PlayRate, 0.01f, 0.001f, 100.0f))
	{
		BeginEditUndo(SequenceComp, "Edit Actor Sequence");
		SequenceComp->SetPlayRate(std::max(0.001f, PlayRate));
		MarkEdited(SequenceComp, "Edit Actor Sequence");
	}

	bool bPauseAtEnd = SequenceComp->ShouldPauseAtEnd();
	if (ImGui::Checkbox("Pause at End", &bPauseAtEnd))
	{
		BeginEditUndo(SequenceComp, "Edit Actor Sequence");
		SequenceComp->SetPauseAtEnd(bPauseAtEnd);
		MarkEdited(SequenceComp, "Edit Actor Sequence");
	}

	float StartOffsetSeconds = SequenceComp->GetStartOffsetSeconds();
	if (ImGui::DragFloat("Start Offset (seconds)", &StartOffsetSeconds, 0.01f, 0.0f, 100000.0f))
	{
		BeginEditUndo(SequenceComp, "Edit Actor Sequence");
		SequenceComp->SetStartOffsetSeconds(std::max(0.0f, StartOffsetSeconds));
		MarkEdited(SequenceComp, "Edit Actor Sequence");
	}

	if ((ImGui::IsItemDeactivatedAfterEdit() || !ImGui::IsAnyItemActive()) && UndoCaptureFlag)
	{
		CommitEditUndo(SequenceComp, "Edit Actor Sequence");
	}
}

