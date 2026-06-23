#pragma once
#include "Render/Common/RenderTypes.h"
#include "Core/CoreTypes.h"

struct FRenderTarget
{
    TComPtr<ID3D11Texture2D> Texture;
    TComPtr<ID3D11RenderTargetView> RTV;
    TComPtr<ID3D11ShaderResourceView> SRV;
    TComPtr<ID3D11UnorderedAccessView> UAV;

	uint32 Width = 0;
    uint32 Height = 0;
	DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
};
