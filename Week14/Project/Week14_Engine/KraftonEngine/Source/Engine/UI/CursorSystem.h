#pragma once

#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"
#include "Object/GarbageCollection.h"

struct FPassContext;
class UTexture2D;
struct ID3D11Buffer;
struct ID3D11Device;
struct ID3D11ShaderResourceView;

class FCursorSystem final : public TSingleton<FCursorSystem>, public FGCObject
{
	friend class TSingleton<FCursorSystem>;

public:
	~FCursorSystem() override;

	void SetSoftwareCursorVisible(bool bVisible);
	bool IsSoftwareCursorVisible() const { return bSoftwareCursorVisible; }
	bool IsSoftwareCursorActive() const { return bSoftwareCursorVisible; }

	bool SetCursorImage(const FString& TexturePath, float Width = 0.0f, float Height = 0.0f, float HotSpotX = 0.0f, float HotSpotY = 0.0f);
	void ClearCursorImage();

	void SetCursorHotSpot(float X, float Y);
	void SetCursorSize(float Width, float Height);
	void SetCursorHitBox(float OffsetX, float OffsetY, float Width, float Height);

	bool ShouldRender(const FPassContext& Ctx) const;
	void Render(const FPassContext& Ctx);
	void ResetRuntimeState();

	bool IsCursorOverRect(float X, float Y, float Width, float Height) const;
	float GetCursorX() const { return LastCursorX; }
	float GetCursorY() const { return LastCursorY; }
	float GetCursorHitX() const { return LastHitX; }
	float GetCursorHitY() const { return LastHitY; }
	float GetCursorHitWidth() const { return LastHitWidth; }
	float GetCursorHitHeight() const { return LastHitHeight; }

	const char* GetReferencerName() const override { return "FCursorSystem"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	FCursorSystem() = default;

	void RefreshHardwareCursor() const;
	void ReleaseGPUResources();
	bool EnsureGPUResources(ID3D11Device* Device);
	bool UploadGeometry(const FPassContext& Ctx, const void* Vertices, uint32 VertexCount, const uint32* Indices, uint32 IndexCount);
	void DrawGeometry(const FPassContext& Ctx, ID3D11ShaderResourceView* SRV, uint32 IndexCount);
	void DrawImageCursor(const FPassContext& Ctx, float X, float Y, float Width, float Height);
	void DrawFallbackCursor(const FPassContext& Ctx, float X, float Y, float Width, float Height);
	void DrawFallbackArrow(const FPassContext& Ctx, float X, float Y, float Width, float Height, float R, float G, float B, float A);
	void UpdateLastCursorRect(const FPassContext& Ctx, float TopLeftX, float TopLeftY, float Width, float Height);

private:
	bool bSoftwareCursorVisible = false;

	FString CursorTexturePath;
	UTexture2D* CursorTexture = nullptr;

	float CursorWidth = 24.0f;
	float CursorHeight = 32.0f;
	float HotSpotX = 1.0f;
	float HotSpotY = 1.0f;
	float HitOffsetX = 0.0f;
	float HitOffsetY = 0.0f;
	float HitWidth = 24.0f;
	float HitHeight = 32.0f;

	float LastCursorX = -1.0f;
	float LastCursorY = -1.0f;
	float LastHitX = -1.0f;
	float LastHitY = -1.0f;
	float LastHitWidth = 0.0f;
	float LastHitHeight = 0.0f;

	ID3D11Buffer* VertexBuffer = nullptr;
	ID3D11Buffer* IndexBuffer = nullptr;
	ID3D11Buffer* PerFrameCB = nullptr;
	ID3D11ShaderResourceView* WhiteTextureSRV = nullptr;
	uint32 VertexCapacity = 0;
	uint32 IndexCapacity = 0;
};
