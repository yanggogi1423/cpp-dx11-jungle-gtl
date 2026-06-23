#pragma once

#include "Editor/Input/EditorViewportInputMapping.h"
#include "Math/Color.h"
#include "Viewport/EditorViewportClient.h"

class AActor;

struct FParticleSystemViewportShowFlags
{
	bool bGrid = true;
	bool bAxis = true;
	bool bBounds = false;
};

class FParticleSystemViewportClient : public FEditorViewportClient
{
public:
	FParticleSystemViewportShowFlags& GetParticleShowFlags() { return ParticleShowFlags; }
	const FParticleSystemViewportShowFlags& GetParticleShowFlags() const { return ParticleShowFlags; }

	static constexpr FColor GetDefaultBackgroundColor() { return FColor(0.34f, 0.35f, 0.34f, 1.0f); }
	const FColor& GetBackgroundColor() const { return BackgroundColor; }
	void SetBackgroundColor(const FColor& InColor) { BackgroundColor = InColor; }
	void ResetBackgroundColor() { BackgroundColor = GetDefaultBackgroundColor(); }

	void SetFocusTargetActor(AActor* InActor) { FocusTargetActor = InActor; }

	bool ProcessInput(FViewportInputContext& Context) override
	{
		const bool bFocusTargetRequested = EditorViewportInputMapping::IsTriggered(
			Context,
			EditorViewportInputMapping::EEditorViewportAction::FocusSelection);
		const bool bHandled = FEditorViewportClient::ProcessInput(Context);
		if (bFocusTargetRequested && FocusTargetActor)
		{
			return FocusActor(FocusTargetActor) || bHandled;
		}

		return bHandled;
	}

private:
	FParticleSystemViewportShowFlags ParticleShowFlags;
	FColor BackgroundColor = GetDefaultBackgroundColor();
	AActor* FocusTargetActor = nullptr;
};
