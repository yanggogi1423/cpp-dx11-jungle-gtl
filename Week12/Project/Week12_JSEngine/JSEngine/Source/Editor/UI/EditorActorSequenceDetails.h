#pragma once

#include "Core/CoreMinimal.h"
#include "Editor/Undo/EditorUndoSystem.h"

class UActorSequenceComponent;
class UEditorEngine;

class FEditorActorSequenceDetails
{
public:
	void Initialize(UEditorEngine* InEditorEngine, bool* InUndoCaptureFlag);
	void Render(UActorSequenceComponent* SequenceComp, float DeltaTime);

private:
	void BeginEditUndo(UActorSequenceComponent* SequenceComp, const char* UndoLabel);
	void MarkEdited(UActorSequenceComponent* SequenceComp, const char* UndoLabel);
	void CommitEditUndo(UActorSequenceComponent* SequenceComp, const char* UndoLabel);

private:
	UEditorEngine* EditorEngine = nullptr;
	bool* UndoCaptureFlag = nullptr;
	UActorSequenceComponent* UndoSequenceComponent = nullptr;
	TArray<FEditorSerializedActorState> UndoBeforeActorStates;
	FString UndoLabel;
};
