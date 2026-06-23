#include "Viewport/Viewport.h"

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
	Ctx->ClearDepthStencilView(DSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	Ctx->OMSetRenderTargets(1, &RTV, DSV);
	Ctx->RSSetViewports(1, &VPRect);
}

bool FViewport::ReadIdPickAt(uint32 InX, uint32 InY, ID3D11DeviceContext* Ctx, uint32& OutId) const
{
	OutId = 0;
	if (!Ctx || !IdPickTexture || !IdPickReadbackTexture || Width == 0 || Height == 0)
	{
		return false;
	}

	const uint32 SafeX = (InX < Width) ? InX : (Width - 1);
	const uint32 SafeY = (InY < Height) ? InY : (Height - 1);

	D3D11_BOX SrcBox = {};
	SrcBox.left = SafeX;
	SrcBox.right = SafeX + 1;
	SrcBox.top = SafeY;
	SrcBox.bottom = SafeY + 1;
	SrcBox.front = 0;
	SrcBox.back = 1;
	Ctx->CopySubresourceRegion(IdPickReadbackTexture, 0, 0, 0, 0, IdPickTexture, 0, &SrcBox);

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	const HRESULT Hr = Ctx->Map(IdPickReadbackTexture, 0, D3D11_MAP_READ, 0, &Mapped);
	if (FAILED(Hr) || !Mapped.pData)
	{
		return false;
	}

	OutId = *reinterpret_cast<const uint32*>(Mapped.pData);
	Ctx->Unmap(IdPickReadbackTexture, 0);
	return true;
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

	D3D11_TEXTURE2D_DESC IdPickDesc = {};
	IdPickDesc.Width = Width;
	IdPickDesc.Height = Height;
	IdPickDesc.MipLevels = 1;
	IdPickDesc.ArraySize = 1;
	IdPickDesc.Format = DXGI_FORMAT_R32_UINT;
	IdPickDesc.SampleDesc.Count = 1;
	IdPickDesc.Usage = D3D11_USAGE_DEFAULT;
	IdPickDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	hr = Device->CreateTexture2D(&IdPickDesc, nullptr, &IdPickTexture);
	if (FAILED(hr)) return false;

	D3D11_RENDER_TARGET_VIEW_DESC IdRTVDesc = {};
	IdRTVDesc.Format = DXGI_FORMAT_R32_UINT;
	IdRTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	IdRTVDesc.Texture2D.MipSlice = 0;
	hr = Device->CreateRenderTargetView(IdPickTexture, &IdRTVDesc, &IdPickRTV);
	if (FAILED(hr)) return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC IdSRVDesc = {};
	IdSRVDesc.Format = DXGI_FORMAT_R32_UINT;
	IdSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	IdSRVDesc.Texture2D.MipLevels = 1;
	IdSRVDesc.Texture2D.MostDetailedMip = 0;
	hr = Device->CreateShaderResourceView(IdPickTexture, &IdSRVDesc, &IdPickSRV);
	if (FAILED(hr)) return false;

	D3D11_TEXTURE2D_DESC IdReadbackDesc = {};
	IdReadbackDesc.Width = 1;
	IdReadbackDesc.Height = 1;
	IdReadbackDesc.MipLevels = 1;
	IdReadbackDesc.ArraySize = 1;
	IdReadbackDesc.Format = DXGI_FORMAT_R32_UINT;
	IdReadbackDesc.SampleDesc.Count = 1;
	IdReadbackDesc.Usage = D3D11_USAGE_STAGING;
	IdReadbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	hr = Device->CreateTexture2D(&IdReadbackDesc, nullptr, &IdPickReadbackTexture);
	if (FAILED(hr)) return false;

	D3D11_TEXTURE2D_DESC IdDebugDesc = {};
	IdDebugDesc.Width = Width;
	IdDebugDesc.Height = Height;
	IdDebugDesc.MipLevels = 1;
	IdDebugDesc.ArraySize = 1;
	IdDebugDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	IdDebugDesc.SampleDesc.Count = 1;
	IdDebugDesc.Usage = D3D11_USAGE_DEFAULT;
	IdDebugDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	hr = Device->CreateTexture2D(&IdDebugDesc, nullptr, &IdPickDebugTexture);
	if (FAILED(hr)) return false;

	hr = Device->CreateRenderTargetView(IdPickDebugTexture, nullptr, &IdPickDebugRTV);
	if (FAILED(hr)) return false;

	hr = Device->CreateShaderResourceView(IdPickDebugTexture, nullptr, &IdPickDebugSRV);
	if (FAILED(hr)) return false;

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

	// DepthSRV: 뎁스 24비트 읽기 (Hi-Z / GPU Occlusion용)
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
	if (IdPickReadbackTexture) { IdPickReadbackTexture->Release(); IdPickReadbackTexture = nullptr; }
	if (IdPickDebugSRV) { IdPickDebugSRV->Release(); IdPickDebugSRV = nullptr; }
	if (IdPickDebugRTV) { IdPickDebugRTV->Release(); IdPickDebugRTV = nullptr; }
	if (IdPickDebugTexture) { IdPickDebugTexture->Release(); IdPickDebugTexture = nullptr; }
	if (IdPickSRV) { IdPickSRV->Release(); IdPickSRV = nullptr; }
	if (IdPickRTV) { IdPickRTV->Release(); IdPickRTV = nullptr; }
	if (IdPickTexture) { IdPickTexture->Release(); IdPickTexture = nullptr; }
	if (StencilSRV) { StencilSRV->Release(); StencilSRV = nullptr; }
	if (DepthSRV) { DepthSRV->Release(); DepthSRV = nullptr; }
	if (DSV) { DSV->Release(); DSV = nullptr; }
	if (DepthTexture) { DepthTexture->Release(); DepthTexture = nullptr; }
	if (SRV) { SRV->Release(); SRV = nullptr; }
	if (RTV) { RTV->Release(); RTV = nullptr; }
	if (RTTexture) { RTTexture->Release(); RTTexture = nullptr; }
}
