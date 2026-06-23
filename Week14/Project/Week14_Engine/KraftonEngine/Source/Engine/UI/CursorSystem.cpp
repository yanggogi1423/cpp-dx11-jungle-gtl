#include "UI/CursorSystem.h"

#include "Core/Logging/Log.h"
#include "GameFramework/WorldContext.h"
#include "Platform/Paths.h"
#include "Render/Device/D3DDevice.h"
#include "Render/RenderPass/RenderPassBase.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Types/FrameContext.h"
#include "Runtime/Engine.h"
#include "Texture/Texture2D.h"
#include "Viewport/GameViewportClient.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <d3d11.h>
#include <filesystem>

namespace
{
	constexpr const char* CursorShaderPath = "Shaders/UI/RmlUi.hlsl";
	constexpr uint32 MaxCursorVertices = 8;
	constexpr uint32 MaxCursorIndices = 18;

	struct FCursorVertexD3D11
	{
		float X, Y;
		float R, G, B, A;
		float U, V;
	};

	struct FCursorPerFrameCB
	{
		float PhysicalViewportWidth = 1.0f;
		float PhysicalViewportHeight = 1.0f;
		float VirtualViewportWidth = 1.0f;
		float VirtualViewportHeight = 1.0f;
		float UIScale = 1.0f;
		float UIOffsetX = 0.0f;
		float UIOffsetY = 0.0f;
		float Padding0 = 0.0f;
		float TranslationX = 0.0f;
		float TranslationY = 0.0f;
		float Padding1 = 0.0f;
		float Padding2 = 0.0f;
		float Transform[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
		};
	};
	static_assert(sizeof(FCursorPerFrameCB) % 16 == 0, "Cursor constant buffer must be 16-byte aligned.");

	FString ResolveCursorTexturePath(const FString& Path)
	{
		std::filesystem::path Resolved(FPaths::ToWide(Path));
		if (Resolved.is_relative())
		{
			Resolved = std::filesystem::path(FPaths::RootDir()) / Resolved;
		}
		return FPaths::ToUtf8(Resolved.wstring());
	}

	float PositiveOrDefault(float Value, float DefaultValue)
	{
		return Value > 0.0f ? Value : DefaultValue;
	}
}

FCursorSystem::~FCursorSystem()
{
	ReleaseGPUResources();
}

void FCursorSystem::SetSoftwareCursorVisible(bool bVisible)
{
	if (bSoftwareCursorVisible == bVisible)
	{
		return;
	}

	bSoftwareCursorVisible = bVisible;
	RefreshHardwareCursor();
}

bool FCursorSystem::SetCursorImage(const FString& TexturePath, float Width, float Height, float InHotSpotX, float InHotSpotY)
{
	if (TexturePath.empty())
	{
		ClearCursorImage();
		SetSoftwareCursorVisible(true);
		return true;
	}

	UGameViewportClient* ViewportClient = GEngine ? GEngine->GetGameViewportClient() : nullptr;
	ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
	if (!ViewportClient || !Device)
	{
		return false;
	}

	const FString ResolvedPath = ResolveCursorTexturePath(TexturePath);
	UTexture2D* LoadedTexture = UTexture2D::LoadFromFile(ResolvedPath, Device, ETextureColorSpace::SRGB);
	if (!LoadedTexture || !LoadedTexture->GetSRV())
	{
		UE_LOG("CursorSystem: failed to load cursor image '%s'", TexturePath.c_str());
		return false;
	}

	CursorTexturePath = ResolvedPath;
	CursorTexture = LoadedTexture;
	CursorWidth = PositiveOrDefault(Width, static_cast<float>(LoadedTexture->GetWidth()));
	CursorHeight = PositiveOrDefault(Height, static_cast<float>(LoadedTexture->GetHeight()));
	HotSpotX = (std::max)(0.0f, InHotSpotX);
	HotSpotY = (std::max)(0.0f, InHotSpotY);
	HitOffsetX = 0.0f;
	HitOffsetY = 0.0f;
	HitWidth = CursorWidth;
	HitHeight = CursorHeight;

	SetSoftwareCursorVisible(true);
	return true;
}

void FCursorSystem::ClearCursorImage()
{
	CursorTexturePath.clear();
	CursorTexture = nullptr;
}

void FCursorSystem::SetCursorHotSpot(float X, float Y)
{
	HotSpotX = (std::max)(0.0f, X);
	HotSpotY = (std::max)(0.0f, Y);
}

void FCursorSystem::SetCursorSize(float Width, float Height)
{
	CursorWidth = PositiveOrDefault(Width, CursorWidth);
	CursorHeight = PositiveOrDefault(Height, CursorHeight);
	HitWidth = PositiveOrDefault(HitWidth, CursorWidth);
	HitHeight = PositiveOrDefault(HitHeight, CursorHeight);
}

void FCursorSystem::SetCursorHitBox(float OffsetX, float OffsetY, float Width, float Height)
{
	HitOffsetX = OffsetX;
	HitOffsetY = OffsetY;
	HitWidth = PositiveOrDefault(Width, CursorWidth);
	HitHeight = PositiveOrDefault(Height, CursorHeight);
}

bool FCursorSystem::ShouldRender(const FPassContext& Ctx) const
{
	if (!bSoftwareCursorVisible || !Ctx.Frame.ViewportRTV)
	{
		return false;
	}

	const EWorldType WorldType = Ctx.Frame.WorldType;
	if (WorldType != EWorldType::Game && WorldType != EWorldType::PIE)
	{
		return false;
	}

	if (Ctx.Frame.CursorViewportX == UINT32_MAX || Ctx.Frame.CursorViewportY == UINT32_MAX)
	{
		return false;
	}

	const UGameViewportClient* ViewportClient = GEngine ? GEngine->GetGameViewportClient() : nullptr;
	return ViewportClient && ViewportClient->IsCursorVisible();
}

void FCursorSystem::Render(const FPassContext& Ctx)
{
	if (!ShouldRender(Ctx))
	{
		return;
	}

	ID3D11Device* Device = Ctx.Device.GetDevice();
	if (!EnsureGPUResources(Device))
	{
		return;
	}

	const float CursorX = static_cast<float>(Ctx.Frame.CursorViewportX);
	const float CursorY = static_cast<float>(Ctx.Frame.CursorViewportY);
	const float DrawWidth = PositiveOrDefault(CursorWidth, CursorTexture ? static_cast<float>(CursorTexture->GetWidth()) : 24.0f);
	const float DrawHeight = PositiveOrDefault(CursorHeight, CursorTexture ? static_cast<float>(CursorTexture->GetHeight()) : 32.0f);
	const float TopLeftX = CursorX - HotSpotX;
	const float TopLeftY = CursorY - HotSpotY;

	UpdateLastCursorRect(Ctx, TopLeftX, TopLeftY, DrawWidth, DrawHeight);

	if (CursorTexture && CursorTexture->GetSRV())
	{
		DrawImageCursor(Ctx, TopLeftX, TopLeftY, DrawWidth, DrawHeight);
		return;
	}

	DrawFallbackCursor(Ctx, TopLeftX, TopLeftY, DrawWidth, DrawHeight);
}

void FCursorSystem::ResetRuntimeState()
{
	bSoftwareCursorVisible = false;
	ClearCursorImage();
	LastCursorX = -1.0f;
	LastCursorY = -1.0f;
	LastHitX = -1.0f;
	LastHitY = -1.0f;
	LastHitWidth = 0.0f;
	LastHitHeight = 0.0f;
	RefreshHardwareCursor();
}

bool FCursorSystem::IsCursorOverRect(float X, float Y, float Width, float Height) const
{
	if (LastHitWidth <= 0.0f || LastHitHeight <= 0.0f || Width <= 0.0f || Height <= 0.0f)
	{
		return false;
	}

	return LastHitX < X + Width
		&& LastHitX + LastHitWidth > X
		&& LastHitY < Y + Height
		&& LastHitY + LastHitHeight > Y;
}

void FCursorSystem::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(CursorTexture, "CursorTexture");
}

void FCursorSystem::RefreshHardwareCursor() const
{
	if (GEngine)
	{
		if (UGameViewportClient* ViewportClient = GEngine->GetGameViewportClient())
		{
			ViewportClient->RefreshCursorVisibility();
		}
	}
}

void FCursorSystem::ReleaseGPUResources()
{
	if (VertexBuffer)
	{
		VertexBuffer->Release();
		VertexBuffer = nullptr;
	}
	if (IndexBuffer)
	{
		IndexBuffer->Release();
		IndexBuffer = nullptr;
	}
	if (PerFrameCB)
	{
		PerFrameCB->Release();
		PerFrameCB = nullptr;
	}
	if (WhiteTextureSRV)
	{
		WhiteTextureSRV->Release();
		WhiteTextureSRV = nullptr;
	}
	VertexCapacity = 0;
	IndexCapacity = 0;
}

bool FCursorSystem::EnsureGPUResources(ID3D11Device* Device)
{
	if (!Device)
	{
		return false;
	}

	if (!VertexBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.ByteWidth = sizeof(FCursorVertexD3D11) * MaxCursorVertices;
		Desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &VertexBuffer)))
		{
			return false;
		}
		VertexBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("CursorVertexBuffer")), "CursorVertexBuffer");
		VertexCapacity = MaxCursorVertices;
	}

	if (!IndexBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.ByteWidth = sizeof(uint32) * MaxCursorIndices;
		Desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &IndexBuffer)))
		{
			return false;
		}
		IndexBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("CursorIndexBuffer")), "CursorIndexBuffer");
		IndexCapacity = MaxCursorIndices;
	}

	if (!PerFrameCB)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.ByteWidth = sizeof(FCursorPerFrameCB);
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &PerFrameCB)))
		{
			return false;
		}
		PerFrameCB->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("CursorPerFrameCB")), "CursorPerFrameCB");
	}

	if (!WhiteTextureSRV)
	{
		const uint32 WhitePixel = 0xffffffff;
		D3D11_TEXTURE2D_DESC TextureDesc = {};
		TextureDesc.Width = 1;
		TextureDesc.Height = 1;
		TextureDesc.MipLevels = 1;
		TextureDesc.ArraySize = 1;
		TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		TextureDesc.SampleDesc.Count = 1;
		TextureDesc.Usage = D3D11_USAGE_DEFAULT;
		TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA InitialData = {};
		InitialData.pSysMem = &WhitePixel;
		InitialData.SysMemPitch = sizeof(uint32);

		ID3D11Texture2D* Texture = nullptr;
		if (FAILED(Device->CreateTexture2D(&TextureDesc, &InitialData, &Texture)))
		{
			return false;
		}
		const HRESULT HR = Device->CreateShaderResourceView(Texture, nullptr, &WhiteTextureSRV);
		Texture->Release();
		if (FAILED(HR))
		{
			return false;
		}
		WhiteTextureSRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("CursorWhiteTextureSRV")), "CursorWhiteTextureSRV");
	}

	return true;
}

bool FCursorSystem::UploadGeometry(const FPassContext& Ctx, const void* Vertices, uint32 VertexCount, const uint32* Indices, uint32 IndexCount)
{
	if (!VertexBuffer || !IndexBuffer || VertexCount == 0 || IndexCount == 0 ||
		VertexCount > VertexCapacity || IndexCount > IndexCapacity)
	{
		return false;
	}

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	if (!DC)
	{
		return false;
	}

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (FAILED(DC->Map(VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}
	memcpy(Mapped.pData, Vertices, sizeof(FCursorVertexD3D11) * VertexCount);
	DC->Unmap(VertexBuffer, 0);

	if (FAILED(DC->Map(IndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}
	memcpy(Mapped.pData, Indices, sizeof(uint32) * IndexCount);
	DC->Unmap(IndexBuffer, 0);

	return true;
}

void FCursorSystem::DrawGeometry(const FPassContext& Ctx, ID3D11ShaderResourceView* SRV, uint32 IndexCount)
{
	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	if (!DC || !PerFrameCB || !VertexBuffer || !IndexBuffer)
	{
		return;
	}

	FShader* Shader = FShaderManager::Get().GetOrCreate(CursorShaderPath);
	if (!Shader || !Shader->IsValid())
	{
		return;
	}

	Ctx.Resources.SetDepthStencilState(Ctx.Device, EDepthStencilState::NoDepth);
	Ctx.Resources.SetBlendState(Ctx.Device, EBlendState::AlphaBlend);
	Ctx.Resources.SetRasterizerState(Ctx.Device, ERasterizerState::SolidNoCull);
	Ctx.Resources.BindSystemSamplers(Ctx.Device);

	DC->OMSetRenderTargets(1, &Ctx.Cache.RTV, Ctx.Cache.DSV);
	DC->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Shader->Bind(DC);

	FCursorPerFrameCB CBData;
	CBData.PhysicalViewportWidth = (std::max)(Ctx.Frame.ViewportWidth, 1.0f);
	CBData.PhysicalViewportHeight = (std::max)(Ctx.Frame.ViewportHeight, 1.0f);
	CBData.VirtualViewportWidth = CBData.PhysicalViewportWidth;
	CBData.VirtualViewportHeight = CBData.PhysicalViewportHeight;
	DC->UpdateSubresource(PerFrameCB, 0, nullptr, &CBData, 0, 0);
	DC->VSSetConstantBuffers(0, 1, &PerFrameCB);

	ID3D11ShaderResourceView* BoundSRV = SRV ? SRV : WhiteTextureSRV;
	DC->PSSetShaderResources(0, 1, &BoundSRV);

	UINT Stride = sizeof(FCursorVertexD3D11);
	UINT Offset = 0;
	DC->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);
	DC->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	DC->DrawIndexed(IndexCount, 0, 0);
}

void FCursorSystem::DrawImageCursor(const FPassContext& Ctx, float X, float Y, float Width, float Height)
{
	const FCursorVertexD3D11 Vertices[] = {
		{ X,         Y,          1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f },
		{ X + Width, Y,          1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f },
		{ X + Width, Y + Height, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
		{ X,         Y + Height, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f },
	};
	const uint32 Indices[] = { 0, 1, 2, 0, 2, 3 };

	if (UploadGeometry(Ctx, Vertices, 4, Indices, 6))
	{
		DrawGeometry(Ctx, CursorTexture ? CursorTexture->GetSRV() : nullptr, 6);
	}
}

void FCursorSystem::DrawFallbackCursor(const FPassContext& Ctx, float X, float Y, float Width, float Height)
{
	DrawFallbackArrow(Ctx, X + 1.0f, Y + 1.0f, Width, Height, 0.0f, 0.0f, 0.0f, 0.75f);
	DrawFallbackArrow(Ctx, X, Y, Width, Height, 1.0f, 1.0f, 1.0f, 1.0f);
}

void FCursorSystem::DrawFallbackArrow(const FPassContext& Ctx, float X, float Y, float Width, float Height, float R, float G, float B, float A)
{
	const float SX = PositiveOrDefault(Width, 24.0f) / 24.0f;
	const float SY = PositiveOrDefault(Height, 32.0f) / 32.0f;
	auto PX = [X, SX](float V) { return X + V * SX; };
	auto PY = [Y, SY](float V) { return Y + V * SY; };

	const FCursorVertexD3D11 Vertices[] = {
		{ PX(0.0f),  PY(0.0f),  R, G, B, A, 0.5f, 0.5f },
		{ PX(0.0f),  PY(28.0f), R, G, B, A, 0.5f, 0.5f },
		{ PX(7.0f),  PY(21.0f), R, G, B, A, 0.5f, 0.5f },
		{ PX(12.0f), PY(32.0f), R, G, B, A, 0.5f, 0.5f },
		{ PX(17.0f), PY(30.0f), R, G, B, A, 0.5f, 0.5f },
		{ PX(12.0f), PY(19.0f), R, G, B, A, 0.5f, 0.5f },
		{ PX(24.0f), PY(19.0f), R, G, B, A, 0.5f, 0.5f },
	};
	const uint32 Indices[] = {
		0, 1, 2,
		0, 2, 6,
		6, 2, 5,
		2, 3, 5,
		3, 4, 5,
	};

	if (UploadGeometry(Ctx, Vertices, 7, Indices, 15))
	{
		DrawGeometry(Ctx, WhiteTextureSRV, 15);
	}
}

void FCursorSystem::UpdateLastCursorRect(const FPassContext& Ctx, float TopLeftX, float TopLeftY, float Width, float Height)
{
	LastCursorX = static_cast<float>(Ctx.Frame.CursorViewportX);
	LastCursorY = static_cast<float>(Ctx.Frame.CursorViewportY);
	LastHitX = TopLeftX + HitOffsetX;
	LastHitY = TopLeftY + HitOffsetY;
	LastHitWidth = PositiveOrDefault(HitWidth, Width);
	LastHitHeight = PositiveOrDefault(HitHeight, Height);
}
