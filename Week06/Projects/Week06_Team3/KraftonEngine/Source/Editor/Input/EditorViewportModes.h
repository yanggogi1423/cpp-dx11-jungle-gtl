#pragma once

#include "Core/CoreTypes.h"
#include "Editor/Input/EditorGizmoTool.h"
#include "Editor/Input/EditorSelectionTool.h"
#include "Editor/Input/EditorViewportTools.h"

#include <windows.h>
#include <memory>

class FEditorViewportClient;

enum class EEditorViewportModeType : uint8
{
	Select,
	Translate,
	Rotate,
	Scale
};

inline const char* GetEditorViewportModeDisplayName(EEditorViewportModeType InModeType)
{
	switch (InModeType)
	{
	case EEditorViewportModeType::Select: return "Select";
	case EEditorViewportModeType::Translate: return "Translate";
	case EEditorViewportModeType::Rotate: return "Rotate";
	case EEditorViewportModeType::Scale: return "Scale";
	default: return "Unknown";
	}
}

class IEditorViewportMode
{
public:
	virtual ~IEditorViewportMode() = default;
	virtual EEditorViewportModeType GetType() const = 0;
	virtual void OnActivated() {}
	virtual bool HandleGizmoInput(float DeltaTime) = 0;
	virtual bool HandleSelectionInput(float DeltaTime) = 0;
	virtual bool GetSelectionMarquee(POINT& OutStart, POINT& OutCurrent, bool& bOutAdditive) const
	{
		OutStart = { 0, 0 };
		OutCurrent = { 0, 0 };
		bOutAdditive = false;
		return false;
	}
};

class FEditorTransformMode final : public IEditorViewportMode
{
public:
	FEditorTransformMode(FEditorViewportClient* InOwner, EEditorViewportModeType InModeType);
	EEditorViewportModeType GetType() const override { return ModeType; }
	void OnActivated() override;
	bool HandleGizmoInput(float DeltaTime) override;
	bool HandleSelectionInput(float DeltaTime) override;
	bool GetSelectionMarquee(POINT& OutStart, POINT& OutCurrent, bool& bOutAdditive) const override;

private:
	FEditorViewportClient* Owner = nullptr;
	EEditorViewportModeType ModeType = EEditorViewportModeType::Select;
	std::unique_ptr<FEditorGizmoTool> GizmoTool;
	std::unique_ptr<FEditorSelectionTool> SelectionTool;
};
