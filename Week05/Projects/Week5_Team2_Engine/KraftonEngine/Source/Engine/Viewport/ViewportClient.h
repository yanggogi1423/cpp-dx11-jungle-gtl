#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Input/InputTypes.h"
#include <algorithm>

class FViewport;

// UE의 FViewportClient 대응 — 뷰포트 입력/드로잉 인터페이스
class FViewportClient
{
public:
	FViewportClient() = default;
	virtual ~FViewportClient() = default;

	virtual void Draw(FViewport* Viewport, float DeltaTime) {}
	virtual bool InputKey(int32 Key, bool bPressed) { return false; }
	virtual bool InputAxis(float DeltaX, float DeltaY) { return false; }
	virtual bool ProcessInput(FViewportInputContext& Context) { (void)Context; return false; }
	virtual bool WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const
	{
		(void)Context;
		OutRestoreScreenPos = { 0, 0 };
		return false;
	}
};

// Single representative client per viewport.
// Host client can delegate to one active sub-client and optional overlay layers.
class FViewportHostClient : public FViewportClient
{
public:
	void SetActiveSubClient(FViewportClient* InClient) { ActiveSubClient = InClient; }
	FViewportClient* GetActiveSubClient() const { return ActiveSubClient; }

	void AddLayerClient(FViewportClient* InClient)
	{
		if (!InClient)
		{
			return;
		}

		for (FViewportClient* Existing : LayerClients)
		{
			if (Existing == InClient)
			{
				return;
			}
		}

		LayerClients.push_back(InClient);
	}

	void RemoveLayerClient(FViewportClient* InClient)
	{
		LayerClients.erase(
			std::remove(LayerClients.begin(), LayerClients.end(), InClient),
			LayerClients.end());
	}

	void Draw(FViewport* Viewport, float DeltaTime) override
	{
		if (ActiveSubClient)
		{
			ActiveSubClient->Draw(Viewport, DeltaTime);
		}

		for (FViewportClient* Layer : LayerClients)
		{
			if (Layer)
			{
				Layer->Draw(Viewport, DeltaTime);
			}
		}
	}

	bool ProcessInput(FViewportInputContext& Context) override
	{
		if (ActiveSubClient && ActiveSubClient->ProcessInput(Context))
		{
			return true;
		}

		for (FViewportClient* Layer : LayerClients)
		{
			if (Layer && Layer->ProcessInput(Context))
			{
				return true;
			}
		}

		return false;
	}

	bool WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const override
	{
		if (ActiveSubClient && ActiveSubClient->WantsRelativeMouseMode(Context, OutRestoreScreenPos))
		{
			return true;
		}

		for (FViewportClient* Layer : LayerClients)
		{
			if (Layer && Layer->WantsRelativeMouseMode(Context, OutRestoreScreenPos))
			{
				return true;
			}
		}

		OutRestoreScreenPos = { 0, 0 };
		return false;
	}

private:
	FViewportClient* ActiveSubClient = nullptr;
	TArray<FViewportClient*> LayerClients;
};
