#include "PrimitiveProxy.h"
#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"
#include "Render/Resource/ShaderManager.h"
#include "Engine/Profiling/Stats.h"

FPrimitiveProxy::FPrimitiveProxy(UPrimitiveComponent* InOwner)
	: Owner(InOwner)
	, bIsDirty(true)
{
	RefreshOcclusionCache();
}

void FPrimitiveProxy::SubmitRenderCommand(FViewContext& View)
{
	if (IsDirty())
	{
		UpdateProxy();
		RefreshOcclusionCache();
		bIsDirty = false;
	}

	if (Owner)
	{
		if (AActor* ActorOwner = Owner->GetOwner())
		{
			CachedCommand.PickingId = ActorOwner->GetUUID();
		}
		else
		{
			CachedCommand.PickingId = Owner->GetUUID();
		}
	}

	View.AddCommand(ERenderPass::Opaque, CachedCommand);

	if (bSelected)
	{
		if (Owner->SupportsOutline())
		{
			// Selection Mask
			View.AddCommand(ERenderPass::SelectionMask, CachedCommand);
		}

		if (View.GetShowFlags().bBoundingVolume)
		{
			FAABBEntry Entry = {};
			FBoundingBox Box = Owner->GetWorldBoundingBox();
			Entry.AABB.Min = Box.Min;
			Entry.AABB.Max = Box.Max;
			Entry.AABB.Color = FColor::White();
			View.AddAABBEntry(std::move(Entry));
		}
	}
}

uint32 FPrimitiveProxy::GetId() const
{
	return Owner ? Owner->GetUUID() : 0;
}

FBoundingBox FPrimitiveProxy::GetAABB() const
{
	return Owner ? Owner->GetWorldBoundingBox() : FBoundingBox();
}

void FPrimitiveProxy::RefreshOcclusionCache()
{
	if (Owner)
	{
		SCOPE_STAT_ACCUM("Proxy.RefreshOcclusionCache");
		FBoundingBox Box = Owner->GetWorldBoundingBox();
		CachedAABBMin = Box.Min;
		CachedAABBMax = Box.Max;
		CachedProxyId = Owner->GetUUID();
	}
}

FDefaultPrimitiveProxy::FDefaultPrimitiveProxy(UPrimitiveComponent* InOwner)
	: FPrimitiveProxy(InOwner)
{
}

void FDefaultPrimitiveProxy::UpdateProxy()
{
	FMeshBuffer* Buffer = Owner->GetMeshBuffer();
	if (!Buffer || !Buffer->IsValid())
	{
		CachedCommand = {};
		return;
	}

	CachedCommand = {};
	CachedCommand.PerObjectConstants = FPerObjectConstants::FromWorldMatrix(Owner->GetWorldMatrix());
	CachedCommand.Shader = FShaderManager::Get().GetShader(EShaderType::Primitive);
	CachedCommand.MeshBuffer = Buffer;
	if (AActor* ActorOwner = Owner->GetOwner())
	{
		CachedCommand.PickingId = ActorOwner->GetUUID();
	}
	else
	{
		CachedCommand.PickingId = Owner->GetUUID();
	}
}
