#pragma once
#include "UI/EditorWidget.h"
#include "Object/GarbageCollection.h"
#include "Editor/UI/EditorDocumentTabManager.h"

#include <memory>

class UObject;
class IEditorPreviewViewportClient;

class FAssetEditorWidget : public FEditorWidget
{
public:
	virtual ~FAssetEditorWidget();

	virtual bool CanEdit(UObject* Object) const = 0;

	virtual void Open(UObject* Object);
	virtual void Close();
	virtual void Tick(float DeltaTime) {}

	virtual void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const {}
	virtual void AddReferencedObjects(FReferenceCollector& Collector) { Collector.AddReferencedObject(EditedObject); }

	virtual bool AllowsMultipleInstances() const { return false; }
	virtual bool IsEditingObject(UObject* Object) const { return IsOpen() && EditedObject == Object; }
	virtual bool SupportsDocumentTabs() const { return GetDocumentTabKind() != EEditorDocumentTabKind::Unsupported; }
	virtual void RenderDocument(float DeltaTime);
	virtual FString GetDocumentTitle() const;
	virtual FString GetDocumentPayloadId() const;
	virtual EEditorDocumentTabKind GetDocumentTabKind() const { return EEditorDocumentTabKind::Unsupported; }
	FEditorDocumentTabId GetDocumentTabId() const;

	UObject* GetEditedObject() const { return EditedObject; }
	bool IsOpen() const { return bOpen; }
	void RequestFocus() { bFocusRequested = true; }

protected:
	void MarkDirty() { bDirty = true; }
	void ClearDirty() { bDirty = false; }
	bool IsDirty() const { return bDirty; }
	std::shared_ptr<bool> GetEditorLifetimeToken() const { return EditorLifetimeToken; }
	TArray<uint8> CaptureSerializedObjectSnapshot(UObject* Object) const;
	bool RecordSerializedObjectEdit(
		UObject* Object,
		const TArray<uint8>& BeforeSnapshot,
		const FString& Label);
	virtual void OnSerializedObjectEditRestored(UObject* Object) { (void)Object; }
	bool ConsumeFocusRequest()
	{
		const bool bWasRequested = bFocusRequested;
		bFocusRequested = false;
		return bWasRequested;
	}

protected:
	UObject* EditedObject = nullptr;
	bool bOpen = false;
	bool bDirty = false;
	bool bFocusRequested = false;
	std::shared_ptr<bool> EditorLifetimeToken = std::make_shared<bool>(true);
};
