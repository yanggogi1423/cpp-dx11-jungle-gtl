#pragma once

#include <memory>
#include "Core/CoreTypes.h"
#include "Core/RayTypes.h"
#include "Editor/Input/EditorGizmoTool.h"
#include "Editor/Input/EditorSelectionTool.h"
#include "Editor/Input/EditorViewportTools.h"

class FEditorViewportClient;

enum class EEditorViewportModeType : uint8
{
	Select
};

class IEditorViewportMode
{
public:
	virtual ~IEditorViewportMode() = default;
	virtual EEditorViewportModeType GetType() const = 0;
	virtual bool HandleGizmoInput(float DeltaTime) = 0;
	virtual bool HandleSelectionInput(float DeltaTime) = 0;
	virtual bool HasPendingIdPickRequest() const = 0;
	virtual void GetPendingIdPickCoord(uint32& OutX, uint32& OutY) const = 0;
	virtual bool HasPendingIdPickReadback() const = 0;
	virtual uint32 GetPendingIdPickReadbackRequestId() const = 0;
	virtual void BeginPendingIdPickReadback(uint32 InRequestId) = 0;
	virtual void CancelPendingIdPickReadback() = 0;
	virtual void SetIdPickResult(uint32 InId) = 0;
	virtual void ResetIdPickingState() = 0;
	virtual void ResetInputState() = 0;
};

class FEditorSelectMode final : public IEditorViewportMode
{
public:
	explicit FEditorSelectMode(FEditorViewportClient* InOwner);
	EEditorViewportModeType GetType() const override { return EEditorViewportModeType::Select; }

	bool HandleGizmoInput(float DeltaTime) override;
	bool HandleSelectionInput(float DeltaTime) override;
	bool HasPendingIdPickRequest() const override;
	void GetPendingIdPickCoord(uint32& OutX, uint32& OutY) const override;
	bool HasPendingIdPickReadback() const override;
	uint32 GetPendingIdPickReadbackRequestId() const override;
	void BeginPendingIdPickReadback(uint32 InRequestId) override;
	void CancelPendingIdPickReadback() override;
	void SetIdPickResult(uint32 InId) override;
	void ResetIdPickingState() override;
	void ResetInputState() override;

private:
	FEditorViewportClient* Owner = nullptr;
	std::unique_ptr<IEditorViewportTool> GizmoTool;
	std::unique_ptr<IEditorViewportTool> SelectionTool;
};
