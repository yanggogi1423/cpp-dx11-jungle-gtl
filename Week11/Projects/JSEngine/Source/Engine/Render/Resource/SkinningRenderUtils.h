#pragma once

#include <d3d11.h>
#include "Core/PlatformTime.h"
#include "Render/Scene/RenderCommand.h"
#include "Runtime/Stats/SkinningRuntimeStats.h"

inline void BindSkinningConstantsIfNeeded(
	ID3D11DeviceContext* DeviceContext,
	FRenderResources* Resources,
	const FRenderCommand& Cmd)
{
	if (Cmd.VertexFactoryType != EVertexFactoryType::SkeletalMesh)
	{
		return;
	}

	FSkeletalSkinningConstants Data = {};
	Data.bUseGPUSkinning = Cmd.bUseGPUSkinning ? 1u : 0u;
	Data.BoneMatrixOffset = 0;

	if (Cmd.bUseGPUSkinning && Cmd.SkinningMatrices && Cmd.SkinningMatrixCount > 0)
	{
		const double UploadStartSeconds = FPlatformTime::Seconds();
		Data.BoneCount = Cmd.SkinningMatrixCount;

		Resources->SkinningMatrixBuffer.Update(DeviceContext, Cmd.SkinningMatrices->data(), Cmd.SkinningMatrixCount);

		ID3D11ShaderResourceView* SkinningSRV = Resources->SkinningMatrixBuffer.GetSRV();
		DeviceContext->VSSetShaderResources(16, 1, &SkinningSRV);

		FSkinningRuntimeStatCollector::RecordGPUMatrixUpload(
			(FPlatformTime::Seconds() - UploadStartSeconds) * 1000.0,
			Cmd.SkinningMatrixCount);
	}
	else
	{
		Data.BoneCount = 0;

		ID3D11ShaderResourceView* NullSRV = nullptr;
		DeviceContext->VSSetShaderResources(16, 1, &NullSRV);
	}

	Resources->SkinningConstantBuffer.Update(DeviceContext, &Data, sizeof(Data));
	ID3D11Buffer* Buffer = Resources->SkinningConstantBuffer.GetBuffer();
	DeviceContext->VSSetConstantBuffers(5, 1, &Buffer);
}
