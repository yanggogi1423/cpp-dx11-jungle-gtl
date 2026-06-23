#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Singleton.h"
#include "Math/Vector.h"
#include "Object/GarbageCollection.h"
#include "Render/Types/RenderTypes.h"

#ifdef GetNextSibling
#undef GetNextSibling
#endif
#ifdef GetFirstChild
#undef GetFirstChild
#endif
#include <RmlUi/Core.h>

#include <chrono>
#include <vector>

class APlayerController;
class UUserWidget;
struct FFrameContext;
struct FPassContext;
struct ID3D11Buffer;
struct ID3D11DepthStencilView;
struct ID3D11Device;
struct ID3D11RasterizerState;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

class FRmlSystemInterface final : public Rml::SystemInterface
{
public:
	double GetElapsedTime() override;
	void JoinPath(Rml::String& TranslatedPath, const Rml::String& DocumentPath, const Rml::String& Path) override;
	bool LogMessage(Rml::Log::Type Type, const Rml::String& Message) override;

private:
	std::chrono::steady_clock::time_point StartTime = std::chrono::steady_clock::now();
};

// 한글 경로 호환 — RmlUi 기본 FileInterface 는 fopen(UTF-8 bytes) 로 동작하는데, Windows
// fopen 은 인자를 ANSI 코드페이지(예: CP949) 로 해석해 한글 경로의 UTF-8 바이트를 깨뜨린다.
// 이 인터페이스는 항상 wide(_wfopen) 로 열어 한글 경로 디렉토리에서도 RML / 폰트 / CSS 가
// 정상 로드되게 한다.
class FRmlFileInterfaceWide final : public Rml::FileInterface
{
public:
	Rml::FileHandle Open(const Rml::String& Path) override;
	void Close(Rml::FileHandle FileHandle) override;
	size_t Read(void* Buffer, size_t Size, Rml::FileHandle FileHandle) override;
	bool Seek(Rml::FileHandle FileHandle, long Offset, int Origin) override;
	size_t Tell(Rml::FileHandle FileHandle) override;
};

struct FUIInputCaptureState
{
	bool bWantsMouse = false;
	bool bWantsKeyboard = false;
	bool bWantsTextInput = false;

	bool bBlocksGameInput = false;
	bool bBlocksGameKeyboard = false;
	bool bBlocksGameMouseLook = false;

	bool bConsumedMouseThisFrame = false;
	bool bConsumedKeyboardThisFrame = false;
	bool bConsumedTextInputThisFrame = false;
};

class FRmlRenderInterfaceD3D11 final : public Rml::RenderInterface
{
public:
	explicit FRmlRenderInterfaceD3D11(ID3D11Device* InDevice);
	~FRmlRenderInterfaceD3D11() override;

	void BeginFrame(const FPassContext& InCtx);
	void EndFrame();

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> Vertices, Rml::Span<const int> Indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle GeometryHandle, Rml::Vector2f Translation, Rml::TextureHandle Texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle GeometryHandle) override;
	Rml::TextureHandle LoadTexture(Rml::Vector2i& TextureDimensions, const Rml::String& Source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> Source, Rml::Vector2i SourceDimensions) override;
	void ReleaseTexture(Rml::TextureHandle Texture) override;
	void EnableScissorRegion(bool Enable) override;
	void SetScissorRegion(Rml::Rectanglei Region) override;
	void SetTransform(const Rml::Matrix4f* Transform) override;
	void EnableClipMask(bool Enable) override;
	void RenderToClipMask(Rml::ClipMaskOperation Operation, Rml::CompiledGeometryHandle Geometry, Rml::Vector2f Translation) override;
	Rml::LayerHandle PushLayer() override;
	void CompositeLayers(Rml::LayerHandle Source, Rml::LayerHandle Destination, Rml::BlendMode BlendMode, Rml::Span<const Rml::CompiledFilterHandle> Filters) override;
	void PopLayer() override;
	Rml::TextureHandle SaveLayerAsTexture() override;
	Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
	Rml::CompiledFilterHandle CompileFilter(const Rml::String& Name, const Rml::Dictionary& Parameters) override;
	void ReleaseFilter(Rml::CompiledFilterHandle Filter) override;

private:
	void CreateConstantBuffer();
	void CreateWhiteTexture();
	void ReleaseWhiteTexture();
	void ReleaseFrameLayers();

private:
	ID3D11Device* Device = nullptr;
	ID3D11Buffer* PerFrameCB = nullptr;
	ID3D11Buffer* CompositeCB = nullptr;
	ID3D11ShaderResourceView* WhiteTextureSRV = nullptr;
	ID3D11RasterizerState* ScissorRasterizerState = nullptr;
	ID3D11RenderTargetView* CurrentRenderTargetView = nullptr;
	ID3D11DepthStencilView* CurrentDepthStencilView = nullptr;
	void* CurrentLayer = nullptr;
	Rml::Matrix4f CurrentTransform;
	const FPassContext* Ctx = nullptr;
	bool bScissorEnabled = false;
	bool bClipMaskEnabled = false;
	std::vector<void*> FrameLayers;
	std::vector<void*> LayerStack;
};

class UUIManager : public TSingleton<UUIManager>, public FGCObject
{
	friend class TSingleton<UUIManager>;

public:
	void Initialize(ID3D11Device* InDevice);
	void Shutdown();

	UUserWidget* CreateWidget(APlayerController* OwningPlayer, const FString& DocumentPath);
	void AddToViewport(UUserWidget* Widget, int32 ZOrder);
	void RemoveFromViewport(UUserWidget* Widget);
	void BeginInputFrame();
	bool PumpViewportInput(uint32 ViewportWidth, uint32 ViewportHeight,
		int32 ViewportClientX, int32 ViewportClientY,
		int32 ViewportClientWidth, int32 ViewportClientHeight);
	// PIE end / TransitionToScene 같은 라이프사이클 경계 — viewport 만 비우고 widget UObject
	// 는 유지 (Lua 가 캐시한 핸들이 valid 한 채로 다음 세션에 재사용되도록).
	void ClearViewport();
	void ReleaseLuaCallbacks();
	// 엔진 shutdown 전용 — 모든 widget UObject 파괴.
	void DestroyAllWidgets();

	void Render(const FPassContext& Ctx);
	bool HasViewportWidgets() const { return !ViewportWidgets.empty(); }
	const char* GetReferencerName() const override { return "UUIManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

		// viewport 에 올라온 widget들의 입력 요구/게임 입력 차단 정책을 합산한다.
		FUIInputCaptureState GetViewportInputCaptureState() const;
		bool AnyViewportWidgetWantsMouse() const;
		FString GetElementText(const FString& ElementId) const;
		bool SetElementText(const FString& ElementId, const FString& Text);
		FString GetElementValue(const FString& ElementId) const;
		bool SetElementValue(const FString& ElementId, const FString& Value);
		bool SetElementClass(const FString& ElementId, const FString& ClassName, bool bEnabled);
		bool HasElementClass(const FString& ElementId, const FString& ClassName) const;
		FString GetElementClassNames(const FString& ElementId) const;
		bool SetElementClassNames(const FString& ElementId, const FString& ClassNames);
		bool HasElementAttribute(const FString& ElementId, const FString& AttributeName) const;
		FString GetElementAttribute(const FString& ElementId, const FString& AttributeName) const;
		bool SetElementAttribute(const FString& ElementId, const FString& AttributeName, const FString& Value);
		bool RemoveElementAttribute(const FString& ElementId, const FString& AttributeName);
		FString GetElementStyle(const FString& ElementId, const FString& StyleName) const;
		bool SetElementStyle(const FString& ElementId, const FString& StyleName, const FString& Value);
		bool RemoveElementStyle(const FString& ElementId, const FString& StyleName);
		bool FocusElement(const FString& ElementId, bool bFocusVisible = false);
		bool BlurElement(const FString& ElementId);
		bool IsElementFocused(const FString& ElementId) const;
		bool ClickElement(const FString& ElementId);
		bool SetElementVisible(const FString& ElementId, bool bVisible);
		bool SetElementEnabled(const FString& ElementId, bool bEnabled);
		bool SetActionEvent(const FString& ElementId, const FString& EventName);
		TArray<FString> PollActionEvents();
		FVector2 GetVirtualViewportSize() const;
		FVector2 GetPhysicalViewportSize() const;

private:
	UUIManager() = default;
	~UUIManager() = default;

	bool LoadDocument(UUserWidget* Widget);
	void CloseDocument(UUserWidget* Widget);
	void CompactInvalidWidgets();
	void ProcessInput(const FFrameContext& Frame);
	void ProcessInputAtPosition(int32 MouseX, int32 MouseY, bool bMouseInsideViewport);
	void RemoveFromViewportImmediate(UUserWidget* Widget);
	void FlushDeferredViewportRemovals();

private:
	TArray<UUserWidget*> ViewportWidgets;
	TArray<UUserWidget*> CreatedWidgets;
	TArray<UUserWidget*> PendingRemoveWidgets;

	ID3D11Device* CachedDevice = nullptr;
	FRmlSystemInterface* SystemInterface = nullptr;
	FRmlFileInterfaceWide* FileInterface = nullptr;
	FRmlRenderInterfaceD3D11* RenderInterface = nullptr;
	Rml::Context* RmlContext = nullptr;
	float LastPhysicalViewportWidth = 0.0f;
	float LastPhysicalViewportHeight = 0.0f;
	bool bRmlInitialized = false;
	bool bDispatchingRmlEvents = false;
	bool bInputProcessedThisFrame = false;
	FUIInputCaptureState LastFrameInputCaptureState;
};
