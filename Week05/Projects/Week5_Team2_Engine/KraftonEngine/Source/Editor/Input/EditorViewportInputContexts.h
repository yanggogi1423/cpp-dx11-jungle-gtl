#pragma once

#include "Engine/Input/InputTypes.h"

class FEditorViewportClient;

enum class EEditorInputContextPriority : int32
{
	Navigation = 100,
	Selection = 200,
	Gizmo = 300,
	ViewportCommand = 400
};

class FViewportCommandContext final : public IInputContext
{
public:
	FViewportCommandContext(FEditorViewportClient* InOwner, float* InDeltaTime);
	int32 GetPriority() const override { return static_cast<int32>(EEditorInputContextPriority::ViewportCommand); }
	bool HandleInput(FViewportInputContext& Context) override;

private:
	FEditorViewportClient* Owner = nullptr;
	float* DeltaTimePtr = nullptr;
};

class FEditorGizmoInputContext final : public IInputContext
{
public:
	FEditorGizmoInputContext(FEditorViewportClient* InOwner, float* InDeltaTime);
	int32 GetPriority() const override { return static_cast<int32>(EEditorInputContextPriority::Gizmo); }
	bool HandleInput(FViewportInputContext& Context) override;

private:
	FEditorViewportClient* Owner = nullptr;
	float* DeltaTimePtr = nullptr;
};

class FEditorSelectionInputContext final : public IInputContext
{
public:
	FEditorSelectionInputContext(FEditorViewportClient* InOwner, float* InDeltaTime);
	int32 GetPriority() const override { return static_cast<int32>(EEditorInputContextPriority::Selection); }
	bool HandleInput(FViewportInputContext& Context) override;

private:
	FEditorViewportClient* Owner = nullptr;
	float* DeltaTimePtr = nullptr;
};

class FEditorNavigationInputContext final : public IInputContext
{
public:
	FEditorNavigationInputContext(FEditorViewportClient* InOwner, float* InDeltaTime);
	int32 GetPriority() const override { return static_cast<int32>(EEditorInputContextPriority::Navigation); }
	bool HandleInput(FViewportInputContext& Context) override;

private:
	FEditorViewportClient* Owner = nullptr;
	float* DeltaTimePtr = nullptr;
};
