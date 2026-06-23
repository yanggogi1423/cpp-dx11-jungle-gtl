#include "StaticMeshProxy.h"
#include "Component/StaticMeshComponent.h"
#include "GameFramework/AActor.h"
#include "Mesh/StaticMeshAsset.h"
#include "Mesh/StaticMesh.h"
#include "Render/Resource/ShaderManager.h"
#include "Materials/Material.h"
#include "Texture/Texture2D.h"

FStaticMeshProxy::FStaticMeshProxy(UStaticMeshComponent* InOwner)
	: FPrimitiveProxy(InOwner)
{
}

void FStaticMeshProxy::UpdateProxy()
{
	UStaticMeshComponent* SMC = static_cast<UStaticMeshComponent*>(Owner);
	if (!SMC) return;

	FMeshBuffer* Buffer = SMC->GetMeshBuffer();
	if (!Buffer || !Buffer->IsValid())
	{
		CachedCommand = {};
		return;
	}

	CachedCommand = {};
	CachedCommand.PerObjectConstants = FPerObjectConstants::FromWorldMatrix(SMC->GetWorldMatrix());
	CachedCommand.Shader = FShaderManager::Get().GetShader(EShaderType::StaticMesh);
	CachedCommand.MeshBuffer = Buffer;
	if (AActor* ActorOwner = SMC->GetOwner())
	{
		CachedCommand.PickingId = ActorOwner->GetUUID();
	}
	else
	{
		CachedCommand.PickingId = SMC->GetUUID();
	}

	UStaticMesh* StaticMesh = SMC->GetStaticMesh();
	if (StaticMesh && StaticMesh->GetStaticMeshAsset())
	{
		const auto& Sections = StaticMesh->GetStaticMeshAsset()->Sections;
		const auto& Slots = StaticMesh->GetStaticMaterials();

		for (const FStaticMeshSection& Section : Sections)
		{
			FMeshSectionDraw Draw;
			Draw.FirstIndex = Section.FirstIndex;
			Draw.IndexCount = Section.NumTriangles * 3;

			for (int32 i = 0; i < (int32)Slots.size(); ++i)
			{
				const int32 MatIdx = Section.MaterialIndex;
				if (MatIdx >= 0 && MatIdx < (int32)SMC->OverrideMaterials.size() && SMC->OverrideMaterials[MatIdx])
				{
					auto& Mat = SMC->OverrideMaterials[MatIdx];
					if (Mat->DiffuseTexture)
						Draw.DiffuseSRV = Mat->DiffuseTexture->GetSRV();
					Draw.DiffuseColor = Mat->DiffuseColor;
				}
				break;
			}
			CachedCommand.SectionDraws.push_back(Draw);
		}
	}
}
