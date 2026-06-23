#include "OcclusionManager.h"
#include <algorithm>
#include <iostream>
#include <cmath>

#include "Render/Pipeline/ViewContext.h"
#include "Render/Pipeline/PrimitiveProxy.h"
#include "Component/PrimitiveComponent.h"
#include "Core/EngineTypes.h"
#include "Object/Object.h"
#include "Engine/Profiling/Stats.h"
#include "Viewport/Viewport.h"

void FViewportOcclusionState::Release()
{
	if (HZBSRV) { HZBSRV->Release(); HZBSRV = nullptr; }
	for (auto UAV : HZBMipsUAV) { if (UAV) UAV->Release(); }
	HZBMipsUAV.clear();
	for (auto SRV : HZBMipsSRV) { if (SRV) SRV->Release(); }
	HZBMipsSRV.clear();
	if (HZBTexture) { HZBTexture->Release(); HZBTexture = nullptr; }

	if (ReadbackBuffers[0]) { ReadbackBuffers[0]->Release(); ReadbackBuffers[0] = nullptr; }
	if (ReadbackBuffers[1]) { ReadbackBuffers[1]->Release(); ReadbackBuffers[1] = nullptr; }
	ReadbackCapacity = 0;

	HZBWidth = 0;
	HZBHeight = 0;
	HZBMipCount = 0;
}

FOcclusionManager& FOcclusionManager::Get()
{
	static FOcclusionManager Instance;
	return Instance;
}

void FOcclusionManager::Initialize(ID3D11Device* InDevice)
{
	if (!HZBBuildCS.Create(InDevice, L"Shaders/HZBBuild.hlsl", "CSMain"))
	{
		std::cerr << "Failed to create HZBBuild CS" << std::endl;
	}

	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.ByteWidth = 32; // (sizeof(uint32) * 6 + 15) & ~15 = 32
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	InDevice->CreateBuffer(&cbDesc, nullptr, &HZBConstantBuffer);

	// Phase 2
	if (!OcclusionTestCS.Create(InDevice, L"Shaders/OcclusionTest.hlsl", "CSMain"))
	{
		std::cerr << "Failed to create OcclusionTest CS" << std::endl;
	}

	D3D11_BUFFER_DESC otCbDesc = {};
	otCbDesc.ByteWidth = (sizeof(FMatrix) + 16 + 16 + 15) & ~15;
	otCbDesc.Usage = D3D11_USAGE_DYNAMIC;
	otCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	otCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	InDevice->CreateBuffer(&otCbDesc, nullptr, &OcclusionTestCB);

	D3D11_SAMPLER_DESC sampDesc = {};
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	InDevice->CreateSamplerState(&sampDesc, &PointClampSampler);
}

void FOcclusionManager::Release()
{
	HZBBuildCS.Release();
	if (HZBConstantBuffer) { HZBConstantBuffer->Release(); HZBConstantBuffer = nullptr; }

	if (PointClampSampler) { PointClampSampler->Release(); PointClampSampler = nullptr; }

	// Phase 2
	OcclusionTestCS.Release();
	if (OcclusionTestCB) { OcclusionTestCB->Release(); OcclusionTestCB = nullptr; }
	if (ProxyBuffer) { ProxyBuffer->Release(); ProxyBuffer = nullptr; }
	if (ProxySRV) { ProxySRV->Release(); ProxySRV = nullptr; }
	if (VisibilityBuffer) { VisibilityBuffer->Release(); VisibilityBuffer = nullptr; }
	if (VisibilityUAV) { VisibilityUAV->Release(); VisibilityUAV = nullptr; }
	ProxyBufferCapacity = 0;

	for (auto& Pair : ViewportStates)
	{
		Pair.second.Release();
	}
	ViewportStates.clear();
}

void FOcclusionManager::CreateHZBTexture(ID3D11Device* InDevice, uint32 Width, uint32 Height, FViewportOcclusionState& OutState)
{
	auto NextPowerOfTwo = [](uint32 v) -> uint32 {
		if (v == 0) return 1;
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	};

	uint32 targetWidth = std::max(1u, NextPowerOfTwo(Width / 2));
	uint32 targetHeight = std::max(1u, NextPowerOfTwo(Height / 2));

	if (OutState.HZBWidth == targetWidth && OutState.HZBHeight == targetHeight && OutState.HZBTexture)
		return;

	if (OutState.HZBSRV) { OutState.HZBSRV->Release(); OutState.HZBSRV = nullptr; }
	for (auto UAV : OutState.HZBMipsUAV) { if (UAV) UAV->Release(); }
	OutState.HZBMipsUAV.clear();
	for (auto SRV : OutState.HZBMipsSRV) { if (SRV) SRV->Release(); }
	OutState.HZBMipsSRV.clear();
	if (OutState.HZBTexture) { OutState.HZBTexture->Release(); OutState.HZBTexture = nullptr; }

	OutState.HZBWidth = targetWidth;
	OutState.HZBHeight = targetHeight;

	OutState.HZBMipCount = static_cast<uint32>(std::floor(std::log2(std::max(OutState.HZBWidth, OutState.HZBHeight)))) + 1;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = OutState.HZBWidth;
	texDesc.Height = OutState.HZBHeight;
	texDesc.MipLevels = OutState.HZBMipCount;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	HRESULT hr = InDevice->CreateTexture2D(&texDesc, nullptr, &OutState.HZBTexture);
	if (FAILED(hr)) return;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = OutState.HZBMipCount;
	srvDesc.Texture2D.MostDetailedMip = 0;
	InDevice->CreateShaderResourceView(OutState.HZBTexture, &srvDesc, &OutState.HZBSRV);

	for (uint32 i = 0; i < OutState.HZBMipCount; ++i)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = i;
		ID3D11UnorderedAccessView* uav = nullptr;
		InDevice->CreateUnorderedAccessView(OutState.HZBTexture, &uavDesc, &uav);
		OutState.HZBMipsUAV.push_back(uav);

		D3D11_SHADER_RESOURCE_VIEW_DESC mipSrvDesc = {};
		mipSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		mipSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		mipSrvDesc.Texture2D.MipLevels = 1;
		mipSrvDesc.Texture2D.MostDetailedMip = i;
		ID3D11ShaderResourceView* mipSrv = nullptr;
		InDevice->CreateShaderResourceView(OutState.HZBTexture, &mipSrvDesc, &mipSrv);
		OutState.HZBMipsSRV.push_back(mipSrv);
	}
}

void FOcclusionManager::BuildHZB(ID3D11DeviceContext* InContext, const FViewContext& InView)
{
	SCOPE_STAT("Render.BuildHZB");
	
	const FViewport* Viewport = InView.GetViewport();
	if (!Viewport) return;

	ID3D11ShaderResourceView* InDepthSRV = InView.GetViewportDepthSRV();
	if (!InDepthSRV) return;

	uint32 Width = static_cast<uint32>(InView.GetViewportWidth());
	uint32 Height = static_cast<uint32>(InView.GetViewportHeight());

	ID3D11Device* device = nullptr;
	InContext->GetDevice(&device);
	if (!device) return;

	FViewportOcclusionState& State = ViewportStates[Viewport];
	CreateHZBTexture(device, Width, Height, State);
	device->Release();

	if (!State.HZBTexture) return;

	HZBBuildCS.Bind(InContext);

	struct HZBConstants
	{
		uint32 SrcRes[2];
		uint32 DstRes[2];
		uint32 NumMips;
		uint32 Padding;
	};

	uint32 currSrcWidth = Width;
	uint32 currSrcHeight = Height;
	uint32 currDstWidth = State.HZBWidth;
	uint32 currDstHeight = State.HZBHeight;

	for (uint32 i = 0; i < State.HZBMipCount; )
	{
		uint32 mipsToBuild = std::min(4u, State.HZBMipCount - i);

		HZBConstants consts;
		consts.SrcRes[0] = currSrcWidth;
		consts.SrcRes[1] = currSrcHeight;
		consts.DstRes[0] = currDstWidth;
		consts.DstRes[1] = currDstHeight;
		consts.NumMips = mipsToBuild;
		consts.Padding = 0;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(InContext->Map(HZBConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			memcpy(mapped.pData, &consts, sizeof(consts));
			InContext->Unmap(HZBConstantBuffer, 0);
		}

		InContext->CSSetConstantBuffers(0, 1, &HZBConstantBuffer);

		ID3D11ShaderResourceView* srcSRV = (i == 0) ? InDepthSRV : State.HZBMipsSRV[i - 1];
		InContext->CSSetShaderResources(0, 1, &srcSRV);
		
		ID3D11UnorderedAccessView* uavs[4] = { nullptr, nullptr, nullptr, nullptr };
		for (uint32 j = 0; j < mipsToBuild; ++j)
		{
			uavs[j] = State.HZBMipsUAV[i + j];
		}
		InContext->CSSetUnorderedAccessViews(0, 4, uavs, nullptr);

		uint32 groupX = (currDstWidth + 15) / 16;
		uint32 groupY = (currDstHeight + 15) / 16;
		HZBBuildCS.Dispatch(InContext, groupX, groupY, 1);

		ID3D11UnorderedAccessView* nullUAVs[4] = { nullptr, nullptr, nullptr, nullptr };
		InContext->CSSetUnorderedAccessViews(0, 4, nullUAVs, nullptr);
		ID3D11ShaderResourceView* nullSRV = nullptr;
		InContext->CSSetShaderResources(0, 1, &nullSRV);

		// Update resolutions for next pass if any
		for (uint32 j = 0; j < mipsToBuild; ++j)
		{
			currSrcWidth = currDstWidth;
			currSrcHeight = currDstHeight;
			currDstWidth = std::max(1u, currDstWidth / 2);
			currDstHeight = std::max(1u, currDstHeight / 2);
		}
		i += mipsToBuild;
	}

	HZBBuildCS.Unbind(InContext);
}

void FOcclusionManager::UpdateGPUProxies(ID3D11DeviceContext* InContext, const TArray<FPrimitiveProxy*>& InProxies, FViewportOcclusionState& OutState)
{
	SCOPE_STAT("Occlusion.UpdateGPUProxies");
	if (InProxies.empty()) return;

	ID3D11Device* device = nullptr;
	InContext->GetDevice(&device);

	if (ProxyBufferCapacity < (uint32)InProxies.size())
	{
		if (ProxyBuffer) ProxyBuffer->Release();
		if (ProxySRV) ProxySRV->Release();
		if (VisibilityBuffer) VisibilityBuffer->Release();
		if (VisibilityUAV) VisibilityUAV->Release();

		ProxyBufferCapacity = (uint32)InProxies.size() + 64;

		D3D11_BUFFER_DESC bufDesc = {};
		bufDesc.ByteWidth = sizeof(FProxyAABB) * ProxyBufferCapacity;
		bufDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufDesc.StructureByteStride = sizeof(FProxyAABB);
		device->CreateBuffer(&bufDesc, nullptr, &ProxyBuffer);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = ProxyBufferCapacity;
		device->CreateShaderResourceView(ProxyBuffer, &srvDesc, &ProxySRV);

		D3D11_BUFFER_DESC visDesc = {};
		visDesc.ByteWidth = sizeof(uint32) * ProxyBufferCapacity;
		visDesc.Usage = D3D11_USAGE_DEFAULT;
		visDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		visDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		visDesc.StructureByteStride = sizeof(uint32);
		device->CreateBuffer(&visDesc, nullptr, &VisibilityBuffer);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = ProxyBufferCapacity;
		device->CreateUnorderedAccessView(VisibilityBuffer, &uavDesc, &VisibilityUAV);
	}

	if (OutState.ReadbackCapacity < ProxyBufferCapacity)
	{
		if (OutState.ReadbackBuffers[0]) OutState.ReadbackBuffers[0]->Release();
		if (OutState.ReadbackBuffers[1]) OutState.ReadbackBuffers[1]->Release();

		OutState.ReadbackCapacity = ProxyBufferCapacity;

		D3D11_BUFFER_DESC rbDesc = {};
		rbDesc.ByteWidth = sizeof(uint32) * OutState.ReadbackCapacity;
		rbDesc.Usage = D3D11_USAGE_STAGING;
		rbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		device->CreateBuffer(&rbDesc, nullptr, &OutState.ReadbackBuffers[0]);
		device->CreateBuffer(&rbDesc, nullptr, &OutState.ReadbackBuffers[1]);

		OutState.bReadbackReady[0] = false;
		OutState.bReadbackReady[1] = false;
	}

	if (device) device->Release();

	const uint32 ProxyCount = static_cast<uint32>(InProxies.size());

	D3D11_MAPPED_SUBRESOURCE MappedResource = {};
	if (FAILED(InContext->Map(ProxyBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource)))
		return;

	// __restrict를 사용하여 컴파일러에게 MappedResource.pData가 이 함수 내에서 다른 포인터로 변경되지 않는다고 알려줌
	FProxyAABB* __restrict MappedProxyBuffer = static_cast<FProxyAABB*>(MappedResource.pData);

	TArray<uint32>& CurrentFrameProxyIds = OutState.ReadbackProxyIds[OutState.ReadbackIndex];
	CurrentFrameProxyIds.resize(ProxyCount);

	constexpr uint32 PrefetchDistance = 8;
	for (uint32 Index = 0; Index < ProxyCount; ++Index)
	{
		if (Index + PrefetchDistance < ProxyCount)
		{
			_mm_prefetch((const char*)InProxies[Index + PrefetchDistance], _MM_HINT_T0);
		}

		FPrimitiveProxy* Proxy = InProxies[Index];
		
		/** SIMD 사용하기 이전 코드
		MappedProxyBuffer[Index].Min = Proxy->CachedAABBMin;
		MappedProxyBuffer[Index].Id  = Proxy->CachedProxyId;
		MappedProxyBuffer[Index].Max = Proxy->CachedAABBMax;
		CurrentFrameProxyIds[Index]  = Proxy->CachedProxyId;
		*/
		
		// SSE를 사용하여 16바이트(float4) 단위로 데이터를 묶어 기록 (Min.xyz + Id)                            
		__m128 vMinId = _mm_setr_ps(Proxy->CachedAABBMin.X, Proxy->CachedAABBMin.Y, Proxy->CachedAABBMin.Z,
										*(float*)&Proxy->CachedProxyId);                                                                             
		// SSE를 사용하여 16바이트(float4) 단위로 데이터를 묶어 기록 (Max.xyz + Padding)                       
		__m128 vMaxPad = _mm_setr_ps(Proxy->CachedAABBMax.X, Proxy->CachedAABBMax.Y, Proxy->CachedAABBMax.Z,   
		                             0.0f);                                                                                                       
		_mm_store_ps((float*)&MappedProxyBuffer[Index], vMinId);                                               
		_mm_store_ps((float*)&MappedProxyBuffer[Index] + 4, vMaxPad);   
		
		CurrentFrameProxyIds[Index]  = Proxy->CachedProxyId; 
	}

	InContext->Unmap(ProxyBuffer, 0);
}

void FOcclusionManager::ExecuteOcclusionTest(ID3D11DeviceContext* InContext, const FViewContext& InView, const TArray<FPrimitiveProxy*>& InProxies)
{
	SCOPE_STAT("Occlusion");
	
	const FViewport* Viewport = InView.GetViewport();
	if (!Viewport) return;

	FViewportOcclusionState& State = ViewportStates[Viewport];

	if (InProxies.empty() || !State.HZBTexture) return;

	// Process readback from PREVIOUS frame (N-1 or N-2)
	uint32 prevIndex = (State.ReadbackIndex + 1) % 2;
	if (State.bReadbackReady[prevIndex])
	{
		SCOPE_STAT("Occlusion.ReadbackMap");
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(InContext->Map(State.ReadbackBuffers[prevIndex], 0, D3D11_MAP_READ, 0, &mapped)))
		{
			uint32* data = static_cast<uint32*>(mapped.pData);
			const auto& ids = State.ReadbackProxyIds[prevIndex];
			for (uint32 i = 0; i < ids.size(); ++i)
			{
				uint32 ProxyId = ids[i];
				if (ProxyId >= State.VisibilityArray.size())
				{
					State.VisibilityArray.resize(ProxyId + 1, 1);
				}
				State.VisibilityArray[ProxyId] = (data[i] != 0) ? 1 : 0;
			}
			InContext->Unmap(State.ReadbackBuffers[prevIndex], 0);
		}
	}

	UpdateGPUProxies(InContext, InProxies, State);

	OcclusionTestCS.Bind(InContext);

	struct PassConstants
	{
		FMatrix ViewProjection;
		uint32 ProxyCount;
		uint32 HZBMipCount;
		float HZBSize[2];
		float ViewportSize[2];
	};

	PassConstants consts;
	// Use previous frame's ViewProjection if available, otherwise use current
	FMatrix currentVP = (InView.GetView() * InView.GetProj());
	if (State.bHasPrevViewProjection)
	{
		consts.ViewProjection = State.PrevViewProjection.GetTransposed();
	}
	else
	{
		consts.ViewProjection = currentVP.GetTransposed();
	}

	consts.ProxyCount = (uint32)InProxies.size();
	consts.HZBMipCount = State.HZBMipCount;
	consts.HZBSize[0] = (float)State.HZBWidth;
	consts.HZBSize[1] = (float)State.HZBHeight;
	consts.ViewportSize[0] = (float)InView.GetViewportWidth();
	consts.ViewportSize[1] = (float)InView.GetViewportHeight();

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (SUCCEEDED(InContext->Map(OcclusionTestCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		memcpy(mapped.pData, &consts, sizeof(consts));
		InContext->Unmap(OcclusionTestCB, 0);
	}

	InContext->CSSetConstantBuffers(0, 1, &OcclusionTestCB);
	InContext->CSSetShaderResources(0, 1, &State.HZBSRV);
	InContext->CSSetShaderResources(1, 1, &ProxySRV);
	InContext->CSSetSamplers(0, 1, &PointClampSampler);
	InContext->CSSetUnorderedAccessViews(0, 1, &VisibilityUAV, nullptr);

	uint32 groupX = (consts.ProxyCount + 63) / 64;
	OcclusionTestCS.Dispatch(InContext, groupX, 1, 1);

	ID3D11UnorderedAccessView* nullUAV = nullptr;
	InContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
	InContext->CSSetShaderResources(0, 2, nullSRVs);

	InContext->CopyResource(State.ReadbackBuffers[State.ReadbackIndex], VisibilityBuffer);
	State.bReadbackReady[State.ReadbackIndex] = true;
	State.ReadbackIndex = (State.ReadbackIndex + 1) % 2;

	OcclusionTestCS.Unbind(InContext);

	// Store current ViewProjection for next frame
	State.PrevViewProjection = currentVP;
	State.bHasPrevViewProjection = true;
}

bool FOcclusionManager::IsVisible(const FViewport* Viewport, uint32 ProxyId) const
{
	auto itState = ViewportStates.find(Viewport);
	if (itState != ViewportStates.end())
	{
		const auto& Array = itState->second.VisibilityArray;
		if (ProxyId < Array.size())
		{
			return Array[ProxyId] != 0;
		}
	}
	return true; // Default to visible if no result yet
}

ID3D11ShaderResourceView* FOcclusionManager::GetHZBSRV(const FViewport* Viewport) const
{
	auto it = ViewportStates.find(Viewport);
	if (it != ViewportStates.end())
	{
		return it->second.HZBSRV;
	}
	return nullptr;
}

uint32 FOcclusionManager::GetHZBMipCount(const FViewport* Viewport) const
{
	auto it = ViewportStates.find(Viewport);
	if (it != ViewportStates.end())
	{
		return it->second.HZBMipCount;
	}
	return 0;
}

void FOcclusionManager::ReleaseViewportState(const FViewport* Viewport)
{
	if (!Viewport) return;

	auto it = ViewportStates.find(Viewport);
	if (it != ViewportStates.end())
	{
		it->second.Release();
		ViewportStates.erase(it);
	}
}
