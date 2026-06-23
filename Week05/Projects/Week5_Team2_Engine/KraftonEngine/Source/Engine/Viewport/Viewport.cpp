#include "Viewport/Viewport.h"
#include "Profiling/Stats.h"
#include "Render/Pipeline/OcclusionManager.h"
#include "Viewport/Viewport.h"
#include "Profiling/PlatformTime.h"

FViewport::~FViewport()
{
	ReleaseResources();
}

bool FViewport::Initialize(ID3D11Device* InDevice, uint32 InWidth, uint32 InHeight)
{
	Device = InDevice;
	Width = InWidth;
	Height = InHeight;

	return CreateResources();
}

void FViewport::Release()
{
	FOcclusionManager::Get().ReleaseViewportState(this);
	ReleaseResources();
	Device = nullptr;
	Width = 0;
	Height = 0;
}

void FViewport::Resize(uint32 InWidth, uint32 InHeight)
{
	if (InWidth == 0 || InHeight == 0) return;
	if (InWidth == Width && InHeight == Height) return;

	Width = InWidth;
	Height = InHeight;

	ReleaseResources();
	CreateResources();
}

void FViewport::RequestResize(uint32 InWidth, uint32 InHeight)
{
	if (InWidth == 0 || InHeight == 0) return;
	if (InWidth == Width && InHeight == Height)
	{
		bPendingResize = false;
		return;
	}

	PendingWidth = InWidth;
	PendingHeight = InHeight;
	bPendingResize = true;
}

bool FViewport::ApplyPendingResize()
{
	if (!bPendingResize) return false;

	bPendingResize = false;
	Resize(PendingWidth, PendingHeight);
	return true;
}

void FViewport::BeginRender(ID3D11DeviceContext* Ctx, const float ClearColor[4])
{
	if (!RTV) return;

	const float DefaultColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
	const float* Color = ClearColor ? ClearColor : DefaultColor;
	D3D11_VIEWPORT VPRect = GetViewportRect();

	Ctx->ClearRenderTargetView(RTV, Color);
	Ctx->ClearDepthStencilView(DSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
	Ctx->OMSetRenderTargets(1, &RTV, DSV);
	Ctx->RSSetViewports(1, &VPRect);
}

void FViewport::BeginPickingRender(ID3D11DeviceContext* Ctx)
{
	if (!PickingRTV || !DSV) return;

	const float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	D3D11_VIEWPORT VPRect = GetViewportRect();

	Ctx->ClearRenderTargetView(PickingRTV, ClearColor);
	Ctx->ClearDepthStencilView(DSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
	Ctx->OMSetRenderTargets(1, &PickingRTV, DSV);
	Ctx->RSSetViewports(1, &VPRect);
}

bool FViewport::EnqueuePickingIdReadback(ID3D11DeviceContext* Ctx, uint32 X, uint32 Y, uint32& OutRequestId)
{
	OutRequestId = 0u;
	if (!Ctx || !PickingTexture) return false;
	if (X >= Width || Y >= Height) return false;

	const uint32 Slot = NextPickingReadbackSlot;
	ID3D11Texture2D* SlotTexture = PickingReadbackRing[Slot];
	if (!SlotTexture || bPickingReadbackInFlight[Slot])
	{
		return false;
	}

	Ctx->OMSetRenderTargets(0, nullptr, nullptr);

	D3D11_BOX SrcBox = {};
	SrcBox.left = X;
	SrcBox.top = Y;
	SrcBox.front = 0;
	SrcBox.right = X + 1;
	SrcBox.bottom = Y + 1;
	SrcBox.back = 1;

	Ctx->CopySubresourceRegion(SlotTexture, 0, 0, 0, 0, PickingTexture, 0, &SrcBox);

	uint32 NewRequestId = NextPickingReadbackRequestId++;
	if (NewRequestId == 0u)
	{
		NewRequestId = NextPickingReadbackRequestId++;
	}

	PickingReadbackRequestIds[Slot] = NewRequestId;
	bPickingReadbackInFlight[Slot] = true;
	NextPickingReadbackSlot = (Slot + 1) % PickingReadbackRingSize;
	OutRequestId = NewRequestId;
	return true;
}

bool FViewport::TryReadPickingIdReadback(ID3D11DeviceContext* Ctx, uint32 RequestId, uint32& OutId, bool& bOutReady, uint64* OutFetchCycles)
{
	OutId = 0u;
	bOutReady = false;
	if (OutFetchCycles)
	{
		*OutFetchCycles = 0u;
	}
	if (!Ctx || RequestId == 0u)
	{
		return false;
	}

	for (uint32 Slot = 0; Slot < PickingReadbackRingSize; ++Slot)
	{
		if (!bPickingReadbackInFlight[Slot] || PickingReadbackRequestIds[Slot] != RequestId)
		{
			continue;
		}

		ID3D11Texture2D* SlotTexture = PickingReadbackRing[Slot];
		if (!SlotTexture)
		{
			return false;
		}

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		HRESULT hr = Ctx->Map(SlotTexture, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &Mapped);
		if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
		{
			return true;
		}
		if (FAILED(hr))
		{
			bPickingReadbackInFlight[Slot] = false;
			PickingReadbackRequestIds[Slot] = 0u;
			return false;
		}

		const uint64 FetchStartCycles = FPlatformTime::Cycles64();
		const uint32* Pixel = static_cast<const uint32*>(Mapped.pData);
		OutId = *Pixel;
		Ctx->Unmap(SlotTexture, 0);
		const uint64 FetchEndCycles = FPlatformTime::Cycles64();
		if (OutFetchCycles)
		{
			*OutFetchCycles = (FetchEndCycles - FetchStartCycles);
		}

		bPickingReadbackInFlight[Slot] = false;
		PickingReadbackRequestIds[Slot] = 0u;
		bOutReady = true;
		return true;
	}

	return false;
}

bool FViewport::TryReadPickingIdReadbackBlocking(ID3D11DeviceContext* Ctx, uint32 RequestId, uint32& OutId, uint64* OutWaitCycles, uint64* OutFetchCycles)
{
	OutId = 0u;
	if (OutWaitCycles)
	{
		*OutWaitCycles = 0u;
	}
	if (OutFetchCycles)
	{
		*OutFetchCycles = 0u;
	}
	if (!Ctx || RequestId == 0u)
	{
		return false;
	}

	for (uint32 Slot = 0; Slot < PickingReadbackRingSize; ++Slot)
	{
		if (!bPickingReadbackInFlight[Slot] || PickingReadbackRequestIds[Slot] != RequestId)
		{
			continue;
		}

		ID3D11Texture2D* SlotTexture = PickingReadbackRing[Slot];
		if (!SlotTexture)
		{
			return false;
		}

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		const uint64 WaitStartCycles = FPlatformTime::Cycles64();
		HRESULT hr = Ctx->Map(SlotTexture, 0, D3D11_MAP_READ, 0, &Mapped);
		const uint64 WaitEndCycles = FPlatformTime::Cycles64();
		if (OutWaitCycles)
		{
			*OutWaitCycles = (WaitEndCycles - WaitStartCycles);
		}
		if (FAILED(hr))
		{
			bPickingReadbackInFlight[Slot] = false;
			PickingReadbackRequestIds[Slot] = 0u;
			return false;
		}

		const uint64 FetchStartCycles = FPlatformTime::Cycles64();
		const uint32* Pixel = static_cast<const uint32*>(Mapped.pData);
		OutId = *Pixel;
		Ctx->Unmap(SlotTexture, 0);
		const uint64 FetchEndCycles = FPlatformTime::Cycles64();
		if (OutFetchCycles)
		{
			*OutFetchCycles = (FetchEndCycles - FetchStartCycles);
		}

		bPickingReadbackInFlight[Slot] = false;
		PickingReadbackRequestIds[Slot] = 0u;
		return true;
	}

	return false;
}

bool FViewport::CancelPickingIdReadback(uint32 RequestId)
{
	if (RequestId == 0u)
	{
		return false;
	}

	for (uint32 Slot = 0; Slot < PickingReadbackRingSize; ++Slot)
	{
		if (!bPickingReadbackInFlight[Slot] || PickingReadbackRequestIds[Slot] != RequestId)
		{
			continue;
		}

		bPickingReadbackInFlight[Slot] = false;
		PickingReadbackRequestIds[Slot] = 0u;
		return true;
	}

	return false;
}

bool FViewport::CreateResources()
{
	if (!Device || Width == 0 || Height == 0) return false;

	// ── 렌더 타깃 텍스처 ──
	D3D11_TEXTURE2D_DESC TexDesc = {};
	TexDesc.Width = Width;
	TexDesc.Height = Height;
	TexDesc.MipLevels = 1;
	TexDesc.ArraySize = 1;
	TexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TexDesc.SampleDesc.Count = 1;
	TexDesc.Usage = D3D11_USAGE_DEFAULT;
	TexDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = Device->CreateTexture2D(&TexDesc, nullptr, &RTTexture);
	if (FAILED(hr)) return false;

	hr = Device->CreateRenderTargetView(RTTexture, nullptr, &RTV);
	if (FAILED(hr)) return false;

	hr = Device->CreateShaderResourceView(RTTexture, nullptr, &SRV);
	if (FAILED(hr)) return false;

	// ── Picking ID 렌더 타깃 (R32_UINT) ──
	D3D11_TEXTURE2D_DESC PickingDesc = {};
	PickingDesc.Width = Width;
	PickingDesc.Height = Height;
	PickingDesc.MipLevels = 1;
	PickingDesc.ArraySize = 1;
	PickingDesc.Format = DXGI_FORMAT_R32_UINT;
	PickingDesc.SampleDesc.Count = 1;
	PickingDesc.Usage = D3D11_USAGE_DEFAULT;
	PickingDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

	hr = Device->CreateTexture2D(&PickingDesc, nullptr, &PickingTexture);
	if (FAILED(hr)) return false;

	D3D11_RENDER_TARGET_VIEW_DESC PickingRTVDesc = {};
	PickingRTVDesc.Format = DXGI_FORMAT_R32_UINT;
	PickingRTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	PickingRTVDesc.Texture2D.MipSlice = 0;

	hr = Device->CreateRenderTargetView(PickingTexture, &PickingRTVDesc, &PickingRTV);
	if (FAILED(hr)) return false;

	D3D11_TEXTURE2D_DESC ReadbackDesc = PickingDesc;
	ReadbackDesc.Width = 1;
	ReadbackDesc.Height = 1;
	ReadbackDesc.Usage = D3D11_USAGE_STAGING;
	ReadbackDesc.BindFlags = 0;
	ReadbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	for (uint32 i = 0; i < PickingReadbackRingSize; ++i)
	{
		hr = Device->CreateTexture2D(&ReadbackDesc, nullptr, &PickingReadbackRing[i]);
		if (FAILED(hr)) return false;
		PickingReadbackRequestIds[i] = 0u;
		bPickingReadbackInFlight[i] = false;
	}
	NextPickingReadbackSlot = 0;
	NextPickingReadbackRequestId = 1;

	// ── 뎁스/스텐실 (TYPELESS → DSV + StencilSRV) ──
	D3D11_TEXTURE2D_DESC DepthDesc = {};
	DepthDesc.Width = Width;
	DepthDesc.Height = Height;
	DepthDesc.MipLevels = 1;
	DepthDesc.ArraySize = 1;
	DepthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	DepthDesc.SampleDesc.Count = 1;
	DepthDesc.Usage = D3D11_USAGE_DEFAULT;
	DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	hr = Device->CreateTexture2D(&DepthDesc, nullptr, &DepthTexture);
	if (FAILED(hr)) return false;

	// DSV: D24_UNORM_S8_UINT 로 해석 (기존과 동일한 뎁스/스텐실 동작)
	D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
	DSVDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	DSVDesc.Texture2D.MipSlice = 0;

	hr = Device->CreateDepthStencilView(DepthTexture, &DSVDesc, &DSV);
	if (FAILED(hr)) return false;

	// DepthSRV: 뎁스 24비트만 읽기 (HZB용)
	D3D11_SHADER_RESOURCE_VIEW_DESC DepthSRVDesc = {};
	DepthSRVDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	DepthSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	DepthSRVDesc.Texture2D.MipLevels = 1;
	DepthSRVDesc.Texture2D.MostDetailedMip = 0;

	hr = Device->CreateShaderResourceView(DepthTexture, &DepthSRVDesc, &DepthSRV);
	if (FAILED(hr)) return false;

	// StencilSRV: 스텐실 8비트만 읽기 (PostProcess edge detection용)
	D3D11_SHADER_RESOURCE_VIEW_DESC StencilSRVDesc = {};
	StencilSRVDesc.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
	StencilSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	StencilSRVDesc.Texture2D.MipLevels = 1;
	StencilSRVDesc.Texture2D.MostDetailedMip = 0;

	hr = Device->CreateShaderResourceView(DepthTexture, &StencilSRVDesc, &StencilSRV);
	if (FAILED(hr)) return false;

	// ── 뷰포트 렉트 ──
	ViewportRect.TopLeftX = 0.0f;
	ViewportRect.TopLeftY = 0.0f;
	ViewportRect.Width = static_cast<float>(Width);
	ViewportRect.Height = static_cast<float>(Height);
	ViewportRect.MinDepth = 0.0f;
	ViewportRect.MaxDepth = 1.0f;

	return true;
}

void FViewport::ReleaseResources()
{
	for (uint32 i = 0; i < PickingReadbackRingSize; ++i)
	{
		if (PickingReadbackRing[i]) { PickingReadbackRing[i]->Release(); PickingReadbackRing[i] = nullptr; }
		PickingReadbackRequestIds[i] = 0u;
		bPickingReadbackInFlight[i] = false;
	}
	NextPickingReadbackSlot = 0;
	NextPickingReadbackRequestId = 1;

	if (DepthSRV) { DepthSRV->Release(); DepthSRV = nullptr; }
	if (StencilSRV) { StencilSRV->Release(); StencilSRV = nullptr; }
	if (PickingRTV) { PickingRTV->Release(); PickingRTV = nullptr; }
	if (PickingTexture) { PickingTexture->Release(); PickingTexture = nullptr; }
	if (DSV) { DSV->Release(); DSV = nullptr; }
	if (DepthTexture) { DepthTexture->Release(); DepthTexture = nullptr; }
	if (SRV) { SRV->Release(); SRV = nullptr; }
	if (RTV) { RTV->Release(); RTV = nullptr; }
	if (RTTexture) { RTTexture->Release(); RTTexture = nullptr; }
}
