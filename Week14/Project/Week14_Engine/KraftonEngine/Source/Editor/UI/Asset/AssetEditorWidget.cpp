#include "AssetEditorWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/Undo/EditorUndoSystem.h"
#include "Object/Object.h"
#include "Serialization/MemoryArchive.h"

#include <cstdio>

FAssetEditorWidget::~FAssetEditorWidget()
{
	if (EditorLifetimeToken)
	{
		*EditorLifetimeToken = false;
	}
}

void FAssetEditorWidget::Open(UObject* Object)
{
	if (!CanEdit(Object))
	{
		return;
	}

	EditedObject = Object;
	bOpen = true;
	RequestFocus();
	ClearDirty();
}

void FAssetEditorWidget::Close()
{
	if (EditorLifetimeToken)
	{
		*EditorLifetimeToken = false;
	}
	EditorLifetimeToken = std::make_shared<bool>(true);
	EditedObject = nullptr;
	bOpen = false;
	bFocusRequested = false;
	ClearDirty();
}

void FAssetEditorWidget::RenderDocument(float DeltaTime)
{
	Render(DeltaTime);
}

FString FAssetEditorWidget::GetDocumentTitle() const
{
	return EditedObject ? EditedObject->GetName() : FString("Untitled");
}

FString FAssetEditorWidget::GetDocumentPayloadId() const
{
	char Buffer[32] = {};
	std::snprintf(Buffer, sizeof(Buffer), "%p", static_cast<const void*>(EditedObject));
	return FString(Buffer);
}

FEditorDocumentTabId FAssetEditorWidget::GetDocumentTabId() const
{
	FEditorDocumentTabId Id;
	Id.Kind = GetDocumentTabKind();
	Id.PayloadId = GetDocumentPayloadId();
	return Id;
}

TArray<uint8> FAssetEditorWidget::CaptureSerializedObjectSnapshot(UObject* Object) const
{
	TArray<uint8> Snapshot;
	if (!IsValid(Object))
	{
		return Snapshot;
	}

	FMemoryArchive Saver(true);
	Object->Serialize(Saver);
	Snapshot = Saver.GetBuffer();
	return Snapshot;
}

bool FAssetEditorWidget::RecordSerializedObjectEdit(
	UObject* Object,
	const TArray<uint8>& BeforeSnapshot,
	const FString& Label)
{
	if (!IsValid(Object))
	{
		return false;
	}

	const TArray<uint8> AfterSnapshot = CaptureSerializedObjectSnapshot(Object);
	if (BeforeSnapshot.empty() || AfterSnapshot.empty() || BeforeSnapshot == AfterSnapshot)
	{
		return false;
	}

	if (!EditorEngine)
	{
		MarkDirty();
		return false;
	}

	std::shared_ptr<bool> UndoLifetime = GetEditorLifetimeToken();
	FEditorUndoSystem& UndoSystem = EditorEngine->GetUndoSystem();
	UndoSystem.BeginTransaction(Label);
	UndoSystem.AddCommand(std::make_unique<FLambdaEditorUndoCommand>(
		Label,
		BeforeSnapshot,
		AfterSnapshot,
		[this, Object, UndoLifetime](FEditorUndoContext&, const TArray<uint8>& Snapshot)
		{
			if (!UndoLifetime || !*UndoLifetime)
			{
				return false;
			}
			if (!IsOpen() || !IsEditingObject(Object) || !IsValid(Object) || Snapshot.empty())
			{
				return false;
			}

			FMemoryArchive Loader(Snapshot, false);
			Object->Serialize(Loader);
			OnSerializedObjectEditRestored(Object);
			MarkDirty();
			return true;
		}));
	const bool bRecorded = UndoSystem.EndTransaction();
	MarkDirty();
	return bRecorded;
}
