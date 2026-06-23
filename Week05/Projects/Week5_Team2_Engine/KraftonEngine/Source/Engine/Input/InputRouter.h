#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Input/InputTypes.h"
#include "UI/SWindow.h"

#include <functional>

class FInputRouter
{
public:
    using FRectProvider = std::function<bool(FRect&)>;

    void SetOwnerWindow(HWND InOwnerWindow);
    void SetImGuiCaptureState(bool bCaptureMouse, bool bCaptureKeyboard);

    void ClearTargets();
    void RegisterTarget(
        FViewport* InViewport,
        FViewportClient* InClient,
        EInputRouteDomain InDomain,
        FRectProvider InRectProvider);

    void Tick();

private:
    struct FTargetEntry
    {
        FViewport* Viewport = nullptr;
        FViewportClient* Client = nullptr;
        EInputRouteDomain Domain = EInputRouteDomain::Editor;
        FRectProvider RectProvider;
    };

    static bool IsPointInRect(const POINT& Point, const FRect& Rect);
    FTargetEntry* FindEntryByViewport(FViewport* InViewport, FRect& OutRect);
    const FTargetEntry* FindEntryByViewport(FViewport* InViewport, FRect& OutRect) const;

private:
    TArray<FTargetEntry> Targets;
    HWND OwnerWindow = nullptr;

    FViewport* HoveredViewport = nullptr;
    FViewport* FocusedViewport = nullptr;
    FViewport* CapturedViewport = nullptr;
    FViewport* RelativeViewport = nullptr;

    bool bImGuiCaptureMouse = false;
    bool bImGuiCaptureKeyboard = false;
};
