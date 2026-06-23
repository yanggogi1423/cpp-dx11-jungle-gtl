#pragma once

#include "Editor/Input/EditorViewportTools.h"
#include "Core/RayTypes.h"
#include "Render/Batcher/BatcherBase.h"

class FEditorViewportClient;
class AActor;

class FEditorSelectionTool final : public IEditorViewportTool
{
public:
	explicit FEditorSelectionTool(FEditorViewportClient* InOwner);
	bool HandleInput(float DeltaTime) override;
	bool HasSelectionMarquee() const { return bSelectionMarqueeActive; }
	bool GetSelectionMarquee(POINT& OutStart, POINT& OutCurrent, bool& bOutAdditive) const;

private:
	bool IsBoxSelectionChordDown(bool& bOutAdditive) const;
	void BeginSelectionMarquee(const POINT& InLocalStart, bool bInAdditive);
	void UpdateSelectionMarquee(const POINT& InLocalCurrent);
	void EndSelectionMarquee();
	void ApplySelectionMarquee(bool bAdditive);
	bool TryProjectActorToViewportLocal(AActor* Actor, float& OutX, float& OutY) const;
	void HandleSelectionClick(const FRay& Ray, const POINT& ClickLocal);

	FEditorViewportClient* Owner = nullptr;
	bool bSelectionMarqueeActive = false;
	bool bSelectionMarqueeAdditive = false;
	POINT SelectionMarqueeStartLocal = { 0, 0 };
	POINT SelectionMarqueeCurrentLocal = { 0, 0 };
	bool bHasPendingSelectionPress = false;
	POINT PendingSelectionPressLocal = { 0, 0 };
};
