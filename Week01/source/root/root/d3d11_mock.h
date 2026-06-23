// d3d11_mock.h
#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>

// ==========================================
// 1. Windows Data Types & Macros
// ==========================================
#define WINAPI
#define CALLBACK
#define E_FAIL 0x80004005L
#define S_OK 0L

typedef int BOOL;
typedef long LONG;
typedef unsigned long ULONG;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef void* LPVOID;
typedef void* HANDLE;
typedef void* HINSTANCE;
typedef void* HWND;
typedef char* LPSTR;
typedef wchar_t WCHAR;
typedef const wchar_t* LPCWSTR;
typedef const char* LPCSTR;
typedef long long HRESULT;
typedef size_t WPARAM;
typedef size_t LPARAM;
typedef float FLOAT;

// Win32 Structs
typedef struct tagPOINT { LONG x; LONG y; } POINT;
typedef struct tagMSG {
    HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt;
} MSG;

typedef struct _WNDCLASSW {
    UINT style; void* lpfnWndProc; int cbClsExtra; int cbWndExtra;
    HINSTANCE hInstance; void* hIcon; void* hCursor; void* hbrBackground;
    LPCWSTR lpszMenuName; LPCWSTR lpszClassName;
} WNDCLASSW;

typedef struct _LARGE_INTEGER {
    long long QuadPart;
} LARGE_INTEGER;

// Win32 Constants
#define WS_POPUP 0
#define WS_VISIBLE 0
#define WS_OVERLAPPEDWINDOW 0
#define CW_USEDEFAULT 0
#define PM_REMOVE 0
#define WM_QUIT 0x0012
#define WM_DESTROY 0x0002

// Win32 Functions Mock
inline void PostQuitMessage(int nExitCode) {}
inline LRESULT DefWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) { return 0; }
inline int RegisterClassW(const WNDCLASSW* lpWndClass) { return 1; }
inline HWND CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, void* hMenu, HINSTANCE hInstance, LPVOID lpParam) { return (HWND)1; }
inline BOOL PeekMessage(MSG* lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) { return 0; }
inline BOOL TranslateMessage(const MSG* lpMsg) { return 0; }
inline LRESULT DispatchMessage(const MSG* lpMsg) { return 0; }
inline void Sleep(DWORD dwMilliseconds) {}
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) { return 1; }
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency) { lpFrequency->QuadPart = 1000; return 1; }
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);

// ==========================================
// 2. DXGI & D3D11 Enums/Structs
// ==========================================
#define __uuidof(x) 0

enum DXGI_FORMAT {
    DXGI_FORMAT_B8G8R8A8_UNORM = 87,
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
    DXGI_FORMAT_R32G32B32_FLOAT = 6,
    DXGI_FORMAT_R32G32B32A32_FLOAT = 2
};

enum D3D11_USAGE {
    D3D11_USAGE_DEFAULT = 0,
    D3D11_USAGE_IMMUTABLE = 1,
    D3D11_USAGE_DYNAMIC = 2,
    D3D11_USAGE_STAGING = 3
};

enum D3D11_BIND_FLAG {
    D3D11_BIND_VERTEX_BUFFER = 0x1L,
    D3D11_BIND_INDEX_BUFFER = 0x2L,
    D3D11_BIND_CONSTANT_BUFFER = 0x4L,
    D3D11_BIND_SHADER_RESOURCE = 0x8L,
    D3D11_BIND_RENDER_TARGET = 0x20L,
};

enum D3D11_CPU_ACCESS_FLAG {
    D3D11_CPU_ACCESS_WRITE = 0x10000L,
    D3D11_CPU_ACCESS_READ = 0x20000L
};

enum D3D11_INPUT_CLASSIFICATION {
    D3D11_INPUT_PER_VERTEX_DATA = 0,
    D3D11_INPUT_PER_INSTANCE_DATA = 1
};

enum D3D_DRIVER_TYPE { D3D_DRIVER_TYPE_HARDWARE = 1 };
enum D3D_FEATURE_LEVEL { D3D_FEATURE_LEVEL_11_0 = 0xb000 };
enum D3D11_PRIMITIVE_TOPOLOGY { D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4 };
enum D3D11_FILL_MODE { D3D11_FILL_SOLID = 3 };
enum D3D11_CULL_MODE { D3D11_CULL_BACK = 3 };
enum D3D11_RTV_DIMENSION { D3D11_RTV_DIMENSION_TEXTURE2D = 4 };
enum D3D11_MAP { D3D11_MAP_WRITE_DISCARD = 4 };

// DXGI Constants
#define DXGI_USAGE_RENDER_TARGET_OUTPUT 0x20L
#define DXGI_SWAP_EFFECT_FLIP_DISCARD 4
#define D3D11_CREATE_DEVICE_BGRA_SUPPORT 0x20
#define D3D11_CREATE_DEVICE_DEBUG 0x2
#define D3D11_SDK_VERSION 7
#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))

// Structs
struct DXGI_SAMPLE_DESC { UINT Count; UINT Quality; };
struct DXGI_MODE_DESC { UINT Width; UINT Height; UINT RefreshRate_Numerator; UINT RefreshRate_Denominator; DXGI_FORMAT Format; };
struct DXGI_SWAP_CHAIN_DESC {
    DXGI_MODE_DESC BufferDesc; DXGI_SAMPLE_DESC SampleDesc;
    UINT BufferUsage; UINT BufferCount; HWND OutputWindow; BOOL Windowed; UINT SwapEffect; UINT Flags;
};

struct D3D11_VIEWPORT { FLOAT TopLeftX; FLOAT TopLeftY; FLOAT Width; FLOAT Height; FLOAT MinDepth; FLOAT MaxDepth; };
struct D3D11_RENDER_TARGET_VIEW_DESC { DXGI_FORMAT Format; D3D11_RTV_DIMENSION ViewDimension; };
struct D3D11_RASTERIZER_DESC { D3D11_FILL_MODE FillMode; D3D11_CULL_MODE CullMode; };
struct D3D11_INPUT_ELEMENT_DESC {
    LPCSTR SemanticName; UINT SemanticIndex; DXGI_FORMAT Format; UINT InputSlot;
    UINT AlignedByteOffset; D3D11_INPUT_CLASSIFICATION InputSlotClass; UINT InstanceDataStepRate;
};
struct D3D11_BUFFER_DESC { UINT ByteWidth; D3D11_USAGE Usage; UINT BindFlags; UINT CPUAccessFlags; UINT MiscFlags; UINT StructureByteStride; };
struct D3D11_SUBRESOURCE_DATA { const void* pSysMem; UINT SysMemPitch; UINT SysMemSlicePitch; };
struct D3D11_MAPPED_SUBRESOURCE { void* pData; UINT RowPitch; UINT DepthPitch; };

// ==========================================
// 3. D3D11 Interfaces (Mock Classes)
// ==========================================

// Base Interface
struct IUnknown {
    virtual HRESULT QueryInterface(const void* riid, void** ppvObject) { return S_OK; }
    virtual ULONG AddRef() { return 1; }
    virtual ULONG Release() { return 0; }
};

struct ID3D11Buffer : public IUnknown {};
struct ID3D11Texture2D : public IUnknown {};
struct ID3D11RenderTargetView : public IUnknown {};
struct ID3D11RasterizerState : public IUnknown {};
struct ID3D11VertexShader : public IUnknown { void* GetBufferPointer() { return 0; } size_t GetBufferSize() { return 0; } };
struct ID3D11PixelShader : public IUnknown { void* GetBufferPointer() { return 0; } size_t GetBufferSize() { return 0; } };
struct ID3D11InputLayout : public IUnknown {};
struct ID3DBlob : public IUnknown { void* GetBufferPointer() { return nullptr; } size_t GetBufferSize() { return 0; } };

struct IDXGISwapChain : public IUnknown {
    HRESULT GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) { return S_OK; }
    HRESULT GetBuffer(UINT Buffer, const void* riid, void** ppSurface) { return S_OK; }
    HRESULT Present(UINT SyncInterval, UINT Flags) { return S_OK; }
};

struct ID3D11DeviceContext : public IUnknown {
    void ClearRenderTargetView(ID3D11RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4]) {}
    void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology) {}
    void RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports) {}
    void RSSetState(ID3D11RasterizerState* pRasterizerState) {}
    void OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews, void* pDepthStencilView) {}
    void OMSetBlendState(void* pBlendState, const FLOAT BlendFactor[4], UINT SampleMask) {}
    void VSSetShader(ID3D11VertexShader* pVertexShader, void** ppClassInstances, UINT NumClassInstances) {}
    void PSSetShader(ID3D11PixelShader* pPixelShader, void** ppClassInstances, UINT NumClassInstances) {}
    void IASetInputLayout(ID3D11InputLayout* pInputLayout) {}
    void VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {}
    void IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) {}
    void Draw(UINT VertexCount, UINT StartVertexLocation) {}
    void Map(ID3D11Buffer* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource) {}
    void Unmap(ID3D11Buffer* pResource, UINT Subresource) {}
    void Flush() {}
};

struct ID3D11Device : public IUnknown {
    HRESULT CreateRenderTargetView(ID3D11Texture2D* pResource, const D3D11_RENDER_TARGET_VIEW_DESC* pDesc, ID3D11RenderTargetView** ppRTView) { return S_OK; }
    HRESULT CreateRasterizerState(const D3D11_RASTERIZER_DESC* pRasterizerDesc, ID3D11RasterizerState** ppRasterizerState) { return S_OK; }
    HRESULT CreateVertexShader(const void* pShaderBytecode, size_t BytecodeLength, void* pClassLinkage, ID3D11VertexShader** ppVertexShader) { return S_OK; }
    HRESULT CreatePixelShader(const void* pShaderBytecode, size_t BytecodeLength, void* pClassLinkage, ID3D11PixelShader** ppPixelShader) { return S_OK; }
    HRESULT CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* pInputElementDescs, UINT NumElements, const void* pShaderBytecodeWithInputSignature, size_t BytecodeLength, ID3D11InputLayout** ppInputLayout) { return S_OK; }
    HRESULT CreateBuffer(const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer) { return S_OK; }
};

// Functions
inline HRESULT D3D11CreateDeviceAndSwapChain(void* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc, IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext) { return S_OK; }
inline HRESULT D3DCompileFromFile(LPCWSTR pFileName, const void* pDefines, void* pInclude, LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs) { return S_OK; }

// ==========================================
// 4. ImGui Mock (Minimal)
// ==========================================
namespace ImGui {
    struct IO { float DeltaTime; };
    inline void CheckVersion() {}
    inline void CreateContext() {}
    inline IO& GetIO() { static IO io; return io; }
    inline void NewFrame() {}
    inline void Render() {}
    inline void* GetDrawData() { return nullptr; }
    inline void Begin(const char*, bool* = 0, int = 0) {}
    inline void End() {}
    inline void Checkbox(const char*, bool*) {}
    inline void InputInt(const char*, int*) {}
    inline void DestroyContext() {}
}
#define IMGUI_CHECKVERSION() ImGui::CheckVersion()

// ImGui Impl Mocks
inline void ImGui_ImplWin32_Init(void*) {}
inline void ImGui_ImplDX11_Init(void*, void*) {}
inline LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline void ImGui_ImplDX11_NewFrame() {}
inline void ImGui_ImplWin32_NewFrame() {}
inline void ImGui_ImplDX11_RenderDrawData(void*) {}
inline void ImGui_ImplDX11_Shutdown() {}
inline void ImGui_ImplWin32_Shutdown() {}