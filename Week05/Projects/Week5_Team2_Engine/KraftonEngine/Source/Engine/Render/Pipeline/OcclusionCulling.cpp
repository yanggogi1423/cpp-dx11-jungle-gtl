#include "OcclusionCulling.h"
#include "ViewContext.h"
#include "OcclusionManager.h"
#include "PrimitiveProxy.h"
#include "Component/BillboardComponent.h"
#include "Component/PrimitiveComponent.h"
#include <algorithm>

void OcclusionCulling::ApplyOcclusionCulling(FViewContext& Context)
{
	if (!Context.GetShowFlags().bOcclusionCulling)
	{
		return;
	}

	auto& Proxies = Context.GetCandidateProxiesMutable();
	FOcclusionManager::Get().AddTotalCandidates((uint32)Proxies.size());

	uint32 initialSize = (uint32)Proxies.size();

	// Remove occluded proxies based on last known results from FOcclusionManager
	Proxies.erase(std::remove_if(Proxies.begin(), Proxies.end(),
		[&Context](FPrimitiveProxy* Proxy) {
			if (!Proxy)
			{
				return true;
			}

			UPrimitiveComponent* Owner = Proxy->GetOwner();
			if (Owner && Owner->IsA<UBillboardComponent>() && Owner->IsVisualizationComponent())
			{
				// Keep editor visualization billboard always visible; avoid occlusion by gizmo/handles.
				return false;
			}

			return !FOcclusionManager::Get().IsVisible(Context.GetViewport(), Proxy->CachedProxyId);
		}), Proxies.end());

	uint32 finalSize = (uint32)Proxies.size();
	FOcclusionManager::Get().AddOccluded(initialSize - finalSize);
}
