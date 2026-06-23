#include "Engine/Runtime/WindowsApplication.h"
#include "Engine/Runtime/resource.h"
#include "Engine/Input/CursorControl.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Runtime/Engine.h"

#include <windowsx.h>
#include <vector>

// ImGui Win32 메시지 핸들러
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, unsigned int Msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK FWindowsApplication::StaticWndProc(HWND hWnd, unsigned int Msg, WPARAM wParam, LPARAM lParam)
{
	FWindowsApplication* App = reinterpret_cast<FWindowsApplication*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	if (Msg == WM_NCCREATE)
	{
		CREATESTRUCT* CreateStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
		App = reinterpret_cast<FWindowsApplication*>(CreateStruct->lpCreateParams);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(App));
	}

	if (App)
	{
		return App->WndProc(hWnd, Msg, wParam, lParam);
	}

	return DefWindowProc(hWnd, Msg, wParam, lParam);
}

LRESULT FWindowsApplication::WndProc(HWND hWnd, unsigned int Msg, WPARAM wParam, LPARAM lParam)
{
	if (Msg == WM_INPUT)
	{
		UINT RawInputSize = 0;
		if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &RawInputSize, sizeof(RAWINPUTHEADER)) == 0
			&& RawInputSize > 0)
		{
			std::vector<BYTE> RawInputBuffer;
			RawInputBuffer.resize(RawInputSize);
			if (GetRawInputData(
				reinterpret_cast<HRAWINPUT>(lParam),
				RID_INPUT,
				RawInputBuffer.data(),
				&RawInputSize,
				sizeof(RAWINPUTHEADER)) == RawInputSize)
			{
				const RAWINPUT* RawInput = reinterpret_cast<const RAWINPUT*>(RawInputBuffer.data());
				if (RawInput && RawInput->header.dwType == RIM_TYPEMOUSE)
				{
					const int32 DeltaX = static_cast<int32>(RawInput->data.mouse.lLastX);
					const int32 DeltaY = static_cast<int32>(RawInput->data.mouse.lLastY);
					if (DeltaX != 0 || DeltaY != 0)
					{
						InputSystem::Get().AddRawMouseDelta(DeltaX, DeltaY);
					}
				}
			}
		}
	}

	if (Msg == WM_SETCURSOR)
	{
		const FCursorControlState CursorState = FCursorControl::GetState();
		if (CursorState.bHideInClient && CursorState.OwnerWindow == hWnd)
		{
			FCursorControl::Apply();
			return TRUE;
		}
	}

	if (Msg == WM_MOUSEWHEEL)
	{
		InputSystem::Get().AddScrollDelta(GET_WHEEL_DELTA_WPARAM(wParam));
	}

	if (ImGui_ImplWin32_WndProcHandler(hWnd, Msg, wParam, lParam))
	{
		return true;
	}

	switch (Msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_MOUSEWHEEL:
		return 0;
	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED)
		{
			unsigned int Width = LOWORD(lParam);
			unsigned int Height = HIWORD(lParam);
			Window.OnResized(Width, Height);
			if (GEngine)
			{
				GEngine->OnWindowResized(Width, Height);
			}
		}
		return 0;
	case WM_ENTERSIZEMOVE:
		bIsResizing = true;
		return 0;
	case WM_EXITSIZEMOVE:
		bIsResizing = false;
		return 0;
	case WM_SIZING:
		if (OnSizingCallback)
		{
			OnSizingCallback();
		}
		return 0;
	default:
		break;
	}

	return DefWindowProc(hWnd, Msg, wParam, lParam);
}

bool FWindowsApplication::Init(HINSTANCE InHInstance)
{
	HInstance = InHInstance;

	WCHAR WindowClass[] = L"JungleWindowClass";
	WCHAR Title[] = L"Game Tech Lab";
	WNDCLASSEXW WndClass = {};
	WndClass.cbSize = sizeof(WNDCLASSEXW);
	WndClass.lpfnWndProc = StaticWndProc;
	WndClass.hInstance = HInstance;
	WndClass.hIcon = LoadIconW(HInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
	WndClass.hIconSm = LoadIconW(HInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
	WndClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	WndClass.lpszClassName = WindowClass;

	RegisterClassExW(&WndClass);

	HWND HWindow = CreateWindowExW(
		0,
		WindowClass,
		Title,
		WS_POPUP | WS_VISIBLE | WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		1920, 1080,
		nullptr, nullptr, HInstance, this);

	if (!HWindow)
	{
		return false;
	}

	Window.Initialize(HWindow);

	RAWINPUTDEVICE RawMouseDevice = {};
	RawMouseDevice.usUsagePage = 0x01;
	RawMouseDevice.usUsage = 0x02;
	RawMouseDevice.dwFlags = RIDEV_INPUTSINK;
	RawMouseDevice.hwndTarget = HWindow;
	RegisterRawInputDevices(&RawMouseDevice, 1, sizeof(RAWINPUTDEVICE));

	return true;
}

void FWindowsApplication::PumpMessages()
{
	MSG Msg;
	while (PeekMessage(&Msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);

		if (Msg.message == WM_QUIT)
		{
			bIsExitRequested = true;
			break;
		}
	}

	FCursorControl::Apply();
}

void FWindowsApplication::Destroy()
{
	if (Window.GetHWND())
	{
		DestroyWindow(Window.GetHWND());
	}
}
