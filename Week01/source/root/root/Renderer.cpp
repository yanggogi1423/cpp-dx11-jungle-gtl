#include "d3d11_mock.h"
#include "sphere.h"
// D3D library link
#pragma comment(lib , "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib , "d3dcompiler")


struct FVector
{
	float x, y, z;
	FVector(float _x = 0, float _y = 0, float _z = 0)
		:x(_x), y(_y), z(_z) {
	}

	// 벡터 덧셈
	FVector operator+(const FVector& other) const {
		return FVector(x + other.x, y + other.y, z + other.z);
	}
	// 벡터 뺄셈
	FVector operator-(const FVector& other) const {
		return FVector(x - other.x, y - other.y, z - other.z);
	}
	// 벡터 스칼라 곱
	FVector operator*(float scalar) const {
		return FVector(x * scalar, y * scalar, z * scalar);
	}
	// 벡터 스칼라 나눗셈
	FVector operator/(float scalar) const {
		return FVector(x / scalar, y / scalar, z / scalar);
	}
	// 복합 대입 연산자 (+=)
	FVector& operator+=(const FVector& other) {
		x += other.x; y += other.y; z += other.z;
		return *this;
	}
	// 복합 대입 연산자 (-=)
	FVector& operator-=(const FVector& other) {
		x -= other.x; y -= other.y; z -= other.z;
		return *this;
	}

	// 내적 (Dot Product)
	static float Dot(const FVector& a, const FVector& b) {
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	// 길이 (Magnitude)
	float Size() const {
		return sqrtf(x * x + y * y + z * z);
	}

	// 정규화 (Normalize)
	FVector GetSafeNormal() const {
		float size = Size();
		if (size < 1.e-4f) return FVector(0, 0, 0);
		return FVector(x / size, y / size, z / size);
	}
};

struct FConstants
{
	FVector offset;
	float radius;
	FVector angle;
	float padding;
};


class URenderer {

public:

	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* DeviceContext = nullptr;
	IDXGISwapChain* SwapChain = nullptr; // 

	ID3D11Texture2D* FrameBuffer = nullptr;
	ID3D11RenderTargetView* FrameBufferRTV = nullptr;
	ID3D11RasterizerState* RasterizerState = nullptr;
	ID3D11Buffer* ConstantBuffer = nullptr;

	FLOAT ClearColor[4] = { 0.025f , 0.025f , 0.025f , 1.0f };
	D3D11_VIEWPORT ViewportInfo; // 렌더링 영역을 정의하는 뷰포트 정보

	ID3D11VertexShader* SimpleVertexShader;
	ID3D11PixelShader* SimplePixelShader;
	ID3D11InputLayout* SimpleInputLayout; // which type this data is


	unsigned int Stride; // 데이터 한칸의 크기 , 그림그리는 보폭



public:
	//renderer init
	void Create(HWND hWindow) {

		//Direct3D device and swapchain create
		CreateDeviceAndSwapChain(hWindow);

		// make frame buffer
		CreateFrameBuffer();

		// make rasterizer state
		CreateRasterizerState();

	}

	void CreateDeviceAndSwapChain(HWND hWindow) {
		// define direct3d function level
		D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

		// init swapchain setting 
		DXGI_SWAP_CHAIN_DESC swapchaindesc = {}; // 크기 등등 관리할 영역을 알려주는 메타 데이터
		swapchaindesc.BufferDesc.Width = 0; // auto setting on window
		swapchaindesc.BufferDesc.Height = 0; // auto setting on window
		swapchaindesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // color format
		swapchaindesc.SampleDesc.Count = 1;
		swapchaindesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // rendering target
		swapchaindesc.BufferCount = 2;
		swapchaindesc.OutputWindow = hWindow;
		swapchaindesc.Windowed = TRUE;
		swapchaindesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // swap method

		// make Direct3d device and swap chain
		D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
			featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
			&swapchaindesc, &SwapChain, &Device, nullptr, &DeviceContext);

		//get swap chain information
		SwapChain->GetDesc(&swapchaindesc);

		// set viewport information
		ViewportInfo = { 0.0f , 0.0f , (float)swapchaindesc.BufferDesc.Width, (float)swapchaindesc.BufferDesc.Height, 0.0f , 1.0f };

	}

	// 할당한 메모리 해제 
	void ReleaseDeviceAndSawpChain()
	{
		if (DeviceContext)
		{
			DeviceContext->Flush(); // order remain GPU command
		}

		if (SwapChain)
		{
			SwapChain->Release();
			SwapChain = nullptr;
		}

		if (Device)
		{
			Device->Release();
			Device = nullptr;
		}

		if (DeviceContext)
		{
			DeviceContext->Release();
			DeviceContext = nullptr;
		}

	}

	// generate frame buffer

	void CreateFrameBuffer()
	{
		// get back buffer texture from swap chain

		SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&FrameBuffer);

		// make render target view
		D3D11_RENDER_TARGET_VIEW_DESC framebufferRTVdesc = {};
		framebufferRTVdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; // color format
		framebufferRTVdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; // 2D Texture

		Device->CreateRenderTargetView(FrameBuffer, &framebufferRTVdesc, &FrameBufferRTV);

	}

	// delete frame buffer function
	void ReleaseFrameBuffer()
	{
		if (FrameBuffer)
		{
			FrameBuffer->Release();
			FrameBuffer = nullptr;
		}
		if (FrameBufferRTV)
		{
			FrameBufferRTV->Release();
			FrameBufferRTV = nullptr;
		}
	}

	void CreateRasterizerState()
	{
		D3D11_RASTERIZER_DESC rasterizerdesc = {};
		rasterizerdesc.FillMode = D3D11_FILL_SOLID; // filling mode
		rasterizerdesc.CullMode = D3D11_CULL_BACK; // back face culling

		Device->CreateRasterizerState(&rasterizerdesc, &RasterizerState);
	}

	// Delete RasterizerState
	void ReleaseRasterizerState()
	{
		if (RasterizerState)
		{
			RasterizerState->Release();
			RasterizerState = nullptr;
		}
	}

	// delete all resource using on renderer
	void Release()
	{
		// init rendertarget
		DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

		ReleaseFrameBuffer();
		ReleaseDeviceAndSawpChain();
	}

	// change swapchain's back buffer with front buffer and show screen
	void SwapBuffer()
	{
		SwapChain->Present(1, 0); // 1 : Vsync on
	}

	void CreateShader()
	{
		ID3DBlob* vertexshaderCSO; // CSO = compile shader object
		ID3DBlob* pixelshaderCSO;

		D3DCompileFromFile(L"ShaderW0.hlsl", nullptr, nullptr, "mainVS", "vs_5_0", 0, 0, &vertexshaderCSO, nullptr);

		Device->CreateVertexShader(vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), nullptr, &SimpleVertexShader);

		D3DCompileFromFile(L"ShaderW0.hlsl", nullptr, nullptr, "mainPS", "ps_5_0", 0, 0, &pixelshaderCSO, nullptr);

		Device->CreatePixelShader(pixelshaderCSO->GetBufferPointer(), pixelshaderCSO->GetBufferSize(), nullptr, &SimplePixelShader);

		D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{"POSITION" , 0 , DXGI_FORMAT_R32G32B32_FLOAT, 0,0, D3D11_INPUT_PER_VERTEX_DATA , 0 },
			{"COLOR" , 0 , DXGI_FORMAT_R32G32B32A32_FLOAT, 0,12, D3D11_INPUT_PER_VERTEX_DATA , 0 }

		}; //DXGI_FORMAT_R32G32B32_FLOAT 같은 것들이 바로 "이건 float 3개짜리 좌표야"라고 말해주는 구체적인 지시 내용입니다.

		Device->CreateInputLayout(layout, ARRAYSIZE(layout), vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), &SimpleInputLayout);

		Stride = sizeof(FVertexSimple);

		vertexshaderCSO->Release();
		pixelshaderCSO->Release();

	}

	void ReleaseShader()
	{
		if (SimpleInputLayout)
		{
			SimpleInputLayout->Release();
			SimpleInputLayout = nullptr;
		}
		if (SimplePixelShader)
		{
			SimplePixelShader->Release();
			SimplePixelShader = nullptr;
		}
		if (SimpleVertexShader)
		{
			SimpleVertexShader->Release();
			SimpleVertexShader = nullptr;
		}
	}

	void Prepare()
	{
		DeviceContext->ClearRenderTargetView(FrameBufferRTV, ClearColor); // 그리기 전에 일단 지움 

		DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // 모양 만드는듯

		DeviceContext->RSSetViewports(1, &ViewportInfo); // Device가 띄울 창 설정
		DeviceContext->RSSetState(RasterizerState); // rasterizerstate 설정하는 단계

		DeviceContext->OMSetRenderTargets(1, &FrameBufferRTV, nullptr); // rendering 할 framebuffer 주소 설정
		DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);

	}

	// shader setting
	void PrepareShader()
	{
		DeviceContext->VSSetShader(SimpleVertexShader, nullptr, 0);
		DeviceContext->PSSetShader(SimplePixelShader, nullptr, 0);
		DeviceContext->IASetInputLayout(SimpleInputLayout);

		if (ConstantBuffer)
		{
			DeviceContext->VSSetConstantBuffers(0, 1, &ConstantBuffer);
		}
	}

	ID3D11Buffer* CreateVertexBuffer(FVertexSimple* vertices, UINT bytewidth)
	{
		// create other vertex buffer
		D3D11_BUFFER_DESC vertexbufferdesc = {};
		vertexbufferdesc.ByteWidth = bytewidth;
		vertexbufferdesc.Usage = D3D11_USAGE_IMMUTABLE; // will never be updated;
		vertexbufferdesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		D3D11_SUBRESOURCE_DATA vertexbufferSRD = { vertices };

		ID3D11Buffer* vertexBuffer;

		Device->CreateBuffer(&vertexbufferdesc, &vertexbufferSRD, &vertexBuffer);
		return vertexBuffer;
	}



	void RenderPrimitive(ID3D11Buffer* pBuffer, UINT numVertices)
	{
		UINT offset = 0; // 시작점 
		DeviceContext->IASetVertexBuffers(0, 1, &pBuffer, &Stride, &offset);
		DeviceContext->Draw(numVertices, 0);
	}


	void ReleaseVertexBuffer(ID3D11Buffer* vertexBuffer)
	{
		vertexBuffer->Release();
	}

	void CreateConstantBuffer()
	{
		D3D11_BUFFER_DESC constantbufferdesc = {};
		constantbufferdesc.ByteWidth = sizeof(FConstants) + 0xf & 0xfffffff0; // ensure constant buffer size is multiple of 16 bytes
		constantbufferdesc.Usage = D3D11_USAGE_DYNAMIC;// will be updated from CPU every frame
		constantbufferdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		constantbufferdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		Device->CreateBuffer(&constantbufferdesc, nullptr, &ConstantBuffer);

	}
	void UpdateConstant(FConstants& pConstants)
	{

		if (ConstantBuffer) {
			D3D11_MAPPED_SUBRESOURCE constantbufferMSR;

			DeviceContext->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR); // update constant buffer every frame

			FConstants* constants = static_cast<FConstants*>(constantbufferMSR.pData);
			{
				*constants = pConstants;

			}
			DeviceContext->Unmap(ConstantBuffer, 0);
		}

	}

	void ReleaseConstanBuffer()
	{
		if (ConstantBuffer)
		{
			ConstantBuffer->Release();
			ConstantBuffer = nullptr;
		}
	}

};
