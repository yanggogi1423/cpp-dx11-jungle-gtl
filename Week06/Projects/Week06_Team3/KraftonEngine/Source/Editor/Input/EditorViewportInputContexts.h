#pragma once

#include "Core/CoreTypes.h"

class FEditorViewportClient;

enum class EEditorInputContextPriority : int32
{
	Navigation = 100,
	Selection = 200,
	Gizmo = 300,
	Command = 400
};

class IEditorViewportInputContext
{
public:
	virtual ~IEditorViewportInputContext() = default;
	virtual int32 GetPriority() const = 0;
	virtual bool HandleInput(float DeltaTime) = 0;
};

class FEditorViewportCommandContext final : public IEditorViewportInputContext
{
public:
	explicit FEditorViewportCommandContext(FEditorViewportClient* InOwner) : Owner(InOwner) {}
	int32 GetPriority() const override { return static_cast<int32>(EEditorInputContextPriority::Command); }
	bool HandleInput(float DeltaTime) override;

private:
	FEditorViewportClient* Owner = nullptr;
};

class FEditorViewportGizmoContext final : public IEditorViewportInputContext
{
public:
	explicit FEditorViewportGizmoContext(FEditorViewportClient* InOwner) : Owner(InOwner) {}
	int32 GetPriority() const override { return static_cast<int32>(EEditorInputContextPriority::Gizmo); }
	bool HandleInput(float DeltaTime) override;

private:
	FEditorViewportClient* Owner = nullptr;
};

class FEditorViewportSelectionContext final : public IEditorViewportInputContext
{
public:
	explicit FEditorViewportSelectionContext(FEditorViewportClient* InOwner) : Owner(InOwner) {}
	int32 GetPriority() const override { return static_cast<int32>(EEditorInputContextPriority::Selection); }
	bool HandleInput(float DeltaTime) override;

private:
	FEditorViewportClient* Owner = nullptr;
};

class FEditorViewportNavigationContext final : public IEditorViewportInputContext
{
public:
	explicit FEditorViewportNavigationContext(FEditorViewportClient* InOwner) : Owner(InOwner) {}
	int32 GetPriority() const override { return static_cast<int32>(EEditorInputContextPriority::Navigation); }
	bool HandleInput(float DeltaTime) override;

private:
	FEditorViewportClient* Owner = nullptr;
};
