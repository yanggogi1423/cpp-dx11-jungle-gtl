#include "Editor/Viewport/EditorViewportClient.h"

#include <algorithm>

#include "Editor/UI/EditorConsoleWidget.h"
#include "Editor/Input/EditorViewportController.h"
#include "Editor/Input/EditorViewportInputMapping.h"
#include "Editor/Input/EditorViewportInputUtils.h"
#include "Editor/Subsystem/OverlayStatSystem.h"
#include "Editor/Settings/EditorSettings.h"
#include "Engine/Profiling/PlatformTime.h"
#include "Engine/Runtime/WindowsWindow.h"

#include "Components/CameraComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/DecalComponent.h"
#include "Components/GizmoComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/TextRenderComponent.h"
#include "Collision/RayUtils.h"
#include "Object/Object.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/EditorEngine.h"
#include "GameFramework/AActor.h"
#include "Render/Proxy/FScene.h"
#include "Viewport/Viewport.h"
#include "GameFramework/World.h"
#include "Engine/Runtime/Engine.h"
#include "ImGui/imgui.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <windows.h>
#include <d3d11.h>

namespace
{
	// Billboard ID picking mismatch 분석용 임시 로그 스위치.
	// 원인 확인 후 false로 내리면 된다.
	constexpr bool bDebugIdPickingTrace = true;
	constexpr float BillboardPickAlphaThreshold = 0.01f;

	void DebugIdPickTrace(const char* Format, ...)
	{
		char Buffer[1024] = {};
		va_list Args;
		va_start(Args, Format);
		vsnprintf(Buffer, sizeof(Buffer), Format, Args);
		va_end(Args);

		UE_LOG("%s", Buffer);
		::OutputDebugStringA(Buffer);
		::OutputDebugStringA("\n");
	}

	bool SampleTextureAlphaAtUV(ID3D11DeviceContext* Context, ID3D11ShaderResourceView* SRV, float U, float V, float& OutAlpha01)
	{
		OutAlpha01 = 1.0f;
		if (!Context || !SRV)
		{
			return false;
		}

		ID3D11Resource* Resource = nullptr;
		SRV->GetResource(&Resource);
		if (!Resource)
		{
			return false;
		}

		ID3D11Texture2D* Texture = nullptr;
		const HRESULT QIHR = Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture));
		Resource->Release();
		if (FAILED(QIHR) || !Texture)
		{
			return false;
		}

		D3D11_TEXTURE2D_DESC SrcDesc = {};
		Texture->GetDesc(&SrcDesc);
		if (SrcDesc.Width == 0 || SrcDesc.Height == 0)
		{
			Texture->Release();
			return false;
		}

		D3D11_TEXTURE2D_DESC StagingDesc = SrcDesc;
		StagingDesc.BindFlags = 0;
		StagingDesc.MiscFlags = 0;
		StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		StagingDesc.Usage = D3D11_USAGE_STAGING;
		StagingDesc.MipLevels = 1;
		StagingDesc.ArraySize = 1;

		ID3D11Device* Device = nullptr;
		Context->GetDevice(&Device);
		if (!Device)
		{
			Texture->Release();
			return false;
		}

		ID3D11Texture2D* Staging = nullptr;
		const HRESULT CreateHR = Device->CreateTexture2D(&StagingDesc, nullptr, &Staging);
		Device->Release();
		if (FAILED(CreateHR) || !Staging)
		{
			Texture->Release();
			return false;
		}

		D3D11_BOX SrcBox = {};
		const uint32 X = static_cast<uint32>(Clamp(U, 0.0f, 1.0f) * static_cast<float>(SrcDesc.Width - 1));
		const uint32 Y = static_cast<uint32>(Clamp(V, 0.0f, 1.0f) * static_cast<float>(SrcDesc.Height - 1));
		SrcBox.left = X;
		SrcBox.right = X + 1;
		SrcBox.top = Y;
		SrcBox.bottom = Y + 1;
		SrcBox.front = 0;
		SrcBox.back = 1;

		Context->CopySubresourceRegion(Staging, 0, 0, 0, 0, Texture, 0, &SrcBox);
		Texture->Release();

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		const HRESULT MapHR = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Mapped);
		if (FAILED(MapHR) || !Mapped.pData)
		{
			Staging->Release();
			return false;
		}

		const uint8* Pixel = static_cast<const uint8*>(Mapped.pData);
		const uint8 Alpha8 = Pixel[3];
		OutAlpha01 = static_cast<float>(Alpha8) / 255.0f;
		Context->Unmap(Staging, 0);
		Staging->Release();
		return true;
	}
}

UWorld* FEditorViewportClient::GetWorld() const
{
	return GEngine ? GEngine->GetWorld() : nullptr;
}

UWorld* FEditorViewportClient::ResolveInteractionWorld() const
{
	if (bHasRoutedInputContext && RoutedInputContext.TargetWorld)
	{
		return RoutedInputContext.TargetWorld;
	}
	return GetWorld();
}

void FEditorViewportClient::Initialize(FWindowsWindow* InWindow)
{
	Window = InWindow;
}

void FEditorViewportClient::CreateCamera()
{
	DestroyCamera();
	Camera = UObjectManager::Get().CreateObject<UCameraComponent>();
}

void FEditorViewportClient::DestroyCamera()
{
	if (Camera)
	{
		UObjectManager::Get().DestroyObject(Camera);
		Camera = nullptr;
	}
}

void FEditorViewportClient::ResetCamera()
{
	if (!Camera || !Settings) return;
	Camera->SetWorldLocation(Settings->InitViewPos);
	Camera->LookAt(Settings->InitLookAt);
	SyncNavigationCameraTargetFromCurrent();
}

void FEditorViewportClient::SyncNavigationCameraTargetFromCurrent()
{
	FEditorViewportController* Controller = GetInputController();
	if (!Controller)
	{
		return;
	}

	if (FEditorNavigationTool* NavigationTool = Controller->GetNavigationTool())
	{
		NavigationTool->SyncTargetFromCameraImmediate();
	}
}

void FEditorViewportClient::SetViewportType(ELevelViewportType NewType)
{
	if (!Camera) return;

	RenderOptions.ViewportType = NewType;

	if (NewType == ELevelViewportType::Perspective)
	{
		Camera->SetOrthographic(false);
		return;
	}

	// FreeOrthographic: 현재 카메라 위치/회전 유지, 투영만 Ortho로 전환
	if (NewType == ELevelViewportType::FreeOrthographic)
	{
		Camera->SetOrthographic(true);
		return;
	}

	// 고정 방향 Orthographic: 카메라를 프리셋 방향으로 설정
	Camera->SetOrthographic(true);

	constexpr float OrthoDistance = 50.0f;
	FVector Position = FVector(0, 0, 0);
	FVector Rotation = FVector(0, 0, 0); // (Roll, Pitch, Yaw)

	switch (NewType)
	{
	case ELevelViewportType::Top:
		Position = FVector(0, 0, OrthoDistance);
		Rotation = FVector(0, 90.0f, 0);	// Pitch down (positive pitch = look -Z)
		break;
	case ELevelViewportType::Bottom:
		Position = FVector(0, 0, -OrthoDistance);
		Rotation = FVector(0, -90.0f, 0);	// Pitch up (negative pitch = look +Z)
		break;
	case ELevelViewportType::Front:
		Position = FVector(OrthoDistance, 0, 0);
		Rotation = FVector(0, 0, 180.0f);	// Yaw to look -X
		break;
	case ELevelViewportType::Back:
		Position = FVector(-OrthoDistance, 0, 0);
		Rotation = FVector(0, 0, 0.0f);		// Yaw to look +X
		break;
	case ELevelViewportType::Left:
		Position = FVector(0, -OrthoDistance, 0);
		Rotation = FVector(0, 0, 90.0f);	// Yaw to look +Y
		break;
	case ELevelViewportType::Right:
		Position = FVector(0, OrthoDistance, 0);
		Rotation = FVector(0, 0, -90.0f);	// Yaw to look -Y
		break;
	default:
		break;
	}

	Camera->SetRelativeLocation(Position);
	Camera->SetRelativeRotation(Rotation);
}

void FEditorViewportClient::SetViewportSize(float InWidth, float InHeight)
{
	if (InWidth > 0.0f)
	{
		WindowWidth = InWidth;
	}

	if (InHeight > 0.0f)
	{
		WindowHeight = InHeight;
	}

	if (Camera)
	{
		Camera->OnResize(static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	}
}

void FEditorViewportClient::Tick(float DeltaTime)
{
	if (bPIEOutlineFlashActive)
	{
		PIEOutlineFlashElapsed += DeltaTime;
		const float TotalDuration = PIEOutlineFlashHoldDuration + PIEOutlineFlashFadeDuration;
		if (PIEOutlineFlashElapsed >= TotalDuration)
		{
			bPIEOutlineFlashActive = false;
			PIEOutlineFlashElapsed = 0.0f;
		}
	}

	if (!bIsActive || !bHasRoutedInputContext)
	{
		return;
	}

	GetInputController();
	EnsureInputContextStack();
	const bool bHasEvents = !RoutedInputContext.Events.empty();
	bool bConsumedByContext = false;
	for (IEditorViewportInputContext* Context : InputContextStack)
	{
		if (Context && Context->HandleInput(DeltaTime))
		{
			bConsumedByContext = true;
			if (bHasEvents)
			{
				const char* ContextName = "Unknown";
				if (Context == CommandInputContext.get()) { ContextName = "Command"; }
				else if (Context == GizmoInputContext.get()) { ContextName = "Gizmo"; }
				else if (Context == SelectionInputContext.get()) { ContextName = "Selection"; }
				else if (Context == NavigationInputContext.get()) { ContextName = "Navigation"; }

				// UE_LOG(
				// 	"[InputTrace] Domain=%d Context=%s Consumed=1 Events=%d Hover=%d Focus=%d Capture=%d Alt=%d Ctrl=%d Shift=%d",
				// 	static_cast<int32>(RoutedInputContext.Domain),
				// 	ContextName,
				// 	static_cast<int32>(RoutedInputContext.Events.size()),
				// 	RoutedInputContext.bHovered ? 1 : 0,
				// 	RoutedInputContext.bFocused ? 1 : 0,
				// 	RoutedInputContext.bCaptured ? 1 : 0,
				// 	RoutedInputContext.Frame.IsDown(VK_MENU) ? 1 : 0,
				// 	RoutedInputContext.Frame.IsDown(VK_CONTROL) ? 1 : 0,
				// 	RoutedInputContext.Frame.IsDown(VK_SHIFT) ? 1 : 0);
			}
			break;
		}
	}
	if (bHasEvents && !bConsumedByContext)
	{
		// UE_LOG(
		// 	"[InputTrace] Domain=%d Context=None Consumed=0 Events=%d Hover=%d Focus=%d Capture=%d Alt=%d Ctrl=%d Shift=%d",
		// 	static_cast<int32>(RoutedInputContext.Domain),
		// 	static_cast<int32>(RoutedInputContext.Events.size()),
		// 	RoutedInputContext.bHovered ? 1 : 0,
		// 	RoutedInputContext.bFocused ? 1 : 0,
		// 	RoutedInputContext.bCaptured ? 1 : 0,
		// 	RoutedInputContext.Frame.IsDown(VK_MENU) ? 1 : 0,
		// 	RoutedInputContext.Frame.IsDown(VK_CONTROL) ? 1 : 0,
		// 	RoutedInputContext.Frame.IsDown(VK_SHIFT) ? 1 : 0);
	}

	bHasRoutedInputContext = false;
}

FEditorViewportController* FEditorViewportClient::GetInputController()
{
	if (!InputController)
	{
		InputController = std::make_unique<FEditorViewportController>(this);
	}
	return InputController.get();
}

bool FEditorViewportClient::SetInteractionMode(EEditorViewportModeType InModeType)
{
	FEditorViewportController* Controller = GetInputController();
	return Controller ? Controller->SetMode(InModeType) : false;
}

EEditorViewportModeType FEditorViewportClient::GetInteractionMode() const
{
	if (!InputController)
	{
		return EEditorViewportModeType::Select;
	}
	return InputController->GetMode();
}

bool FEditorViewportClient::CycleInteractionMode()
{
	FEditorViewportController* Controller = GetInputController();
	return Controller ? Controller->CycleMode() : false;
}

void FEditorViewportClient::EnsureInputContextStack()
{
	if (bInputContextStackInitialized)
	{
		return;
	}

	CommandInputContext = std::make_unique<FEditorViewportCommandContext>(this);
	GizmoInputContext = std::make_unique<FEditorViewportGizmoContext>(this);
	SelectionInputContext = std::make_unique<FEditorViewportSelectionContext>(this);
	NavigationInputContext = std::make_unique<FEditorViewportNavigationContext>(this);

	InputContextStack.clear();
	InputContextStack.push_back(CommandInputContext.get());
	InputContextStack.push_back(GizmoInputContext.get());
	InputContextStack.push_back(SelectionInputContext.get());
	InputContextStack.push_back(NavigationInputContext.get());

	std::sort(
		InputContextStack.begin(),
		InputContextStack.end(),
		[](IEditorViewportInputContext* Lhs, IEditorViewportInputContext* Rhs)
		{
			if (!Lhs || !Rhs)
			{
				return Lhs != nullptr;
			}
			return Lhs->GetPriority() > Rhs->GetPriority();
		});

	bInputContextStackInitialized = true;
}

bool FEditorViewportClient::HandleCommandInput(float DeltaTime)
{
	FEditorViewportController* Controller = GetInputController();
	return Controller ? Controller->HandleViewportCommandInput(DeltaTime) : false;
}

bool FEditorViewportClient::HandleNavigationInput(float DeltaTime)
{
	FEditorViewportController* Controller = GetInputController();
	return Controller ? Controller->HandleNavigationInput(DeltaTime) : false;
}

bool FEditorViewportClient::HandleGizmoInput(float DeltaTime)
{
	FEditorViewportController* Controller = GetInputController();
	return Controller ? Controller->HandleGizmoInput(DeltaTime) : false;
}

bool FEditorViewportClient::HandleSelectionInput(float DeltaTime)
{
	FEditorViewportController* Controller = GetInputController();
	return Controller ? Controller->HandleSelectionInput(DeltaTime) : false;
}

bool FEditorViewportClient::ProcessInput(FViewportInputContext& Context)
{
	RoutedInputContext = Context;
	bHasRoutedInputContext = true;
	return false;
}

bool FEditorViewportClient::ConvertScreenToViewportLocal(const POINT& InScreenPos, POINT& OutLocal, bool bClampToViewport) const
{
	const FRect& ViewRect = GetViewportScreenRect();
	if (ViewRect.Width <= 0.0f || ViewRect.Height <= 0.0f)
	{
		return false;
	}

	POINT ClientPos = InScreenPos;
	if (Window)
	{
		ScreenToClient(Window->GetHWND(), &ClientPos);
	}

	const float LocalX = static_cast<float>(ClientPos.x) - ViewRect.X;
	const float LocalY = static_cast<float>(ClientPos.y) - ViewRect.Y;
	if (!bClampToViewport)
	{
		OutLocal.x = static_cast<LONG>(LocalX);
		OutLocal.y = static_cast<LONG>(LocalY);
		return true;
	}

	OutLocal.x = static_cast<LONG>(Clamp(LocalX, 0.0f, (std::max)(0.0f, ViewRect.Width - 1.0f)));
	OutLocal.y = static_cast<LONG>(Clamp(LocalY, 0.0f, (std::max)(0.0f, ViewRect.Height - 1.0f)));
	return true;
}

bool FEditorViewportClient::PickActorByIdAtLocalPoint(const POINT& InLocalPoint, AActor*& OutActor) const
{
	OutActor = nullptr;
	UWorld* InteractionWorld = ResolveInteractionWorld();
	if (!InteractionWorld || !Viewport || !GEngine)
	{
		return false;
	}

	ID3D11DeviceContext* Context = GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
	if (!Context)
	{
		return false;
	}

	uint32 PickedId = 0;
	const uint32 PixelX = (InLocalPoint.x >= 0) ? static_cast<uint32>(InLocalPoint.x) : 0u;
	const uint32 PixelY = (InLocalPoint.y >= 0) ? static_cast<uint32>(InLocalPoint.y) : 0u;
	if (!Viewport->ReadIdPickAt(PixelX, PixelY, Context, PickedId) || PickedId == 0u)
	{
		if constexpr (bDebugIdPickingTrace)
		{
			const uint32 W = Viewport->GetWidth();
			const uint32 H = Viewport->GetHeight();
			DebugIdPickTrace("[IDPickTrace] Miss Local=(%d,%d) Pixel=(%u,%u) Viewport=(%u,%u) PickedId=%u",
				InLocalPoint.x, InLocalPoint.y, PixelX, PixelY, W, H, PickedId);

			for (int32 dy = -1; dy <= 1; ++dy)
			{
				for (int32 dx = -1; dx <= 1; ++dx)
				{
					const int32 sx = static_cast<int32>(PixelX) + dx;
					const int32 sy = static_cast<int32>(PixelY) + dy;
					if (sx < 0 || sy < 0)
					{
						continue;
					}

					uint32 NeighborId = 0u;
					if (Viewport->ReadIdPickAt(static_cast<uint32>(sx), static_cast<uint32>(sy), Context, NeighborId))
					{
						DebugIdPickTrace("[IDPickTrace] Neighbor dx=%d dy=%d id=%u", dx, dy, NeighborId);
					}
				}
			}
		}
		return false;
	}

	const uint32 ProxyId = PickedId - 1u;
	const TArray<FPrimitiveSceneProxy*>& Proxies = InteractionWorld->GetScene().GetAllProxies();
	if (ProxyId >= static_cast<uint32>(Proxies.size()))
	{
		if constexpr (bDebugIdPickingTrace)
		{
			DebugIdPickTrace("[IDPickTrace] InvalidProxyId PickedId=%u ProxyId=%u ProxyCount=%d", PickedId, ProxyId, static_cast<int32>(Proxies.size()));
		}
		return false;
	}

	FPrimitiveSceneProxy* Proxy = Proxies[ProxyId];
	if (!Proxy || !Proxy->Owner || !Proxy->Owner->GetOwner())
	{
		if constexpr (bDebugIdPickingTrace)
		{
			DebugIdPickTrace("[IDPickTrace] NullProxy PickedId=%u ProxyId=%u Proxy=%p OwnerComp=%p", PickedId, ProxyId, Proxy, Proxy ? Proxy->Owner : nullptr);
		}
		return false;
	}
	if (!Proxy->Owner->SupportsPicking() || Proxy->Owner->IsA<UTextRenderComponent>())
	{
		if constexpr (bDebugIdPickingTrace)
		{
			DebugIdPickTrace("[IDPickTrace] RejectedComp PickedId=%u ProxyId=%u SupportsPicking=%d IsText=%d IsBillboard=%d IsDecal=%d Pass=%d",
				PickedId,
				ProxyId,
				Proxy->Owner->SupportsPicking() ? 1 : 0,
				Proxy->Owner->IsA<UTextRenderComponent>() ? 1 : 0,
				Proxy->Owner->IsA<UBillboardComponent>() ? 1 : 0,
				Proxy->Owner->IsA<UDecalComponent>() ? 1 : 0,
				static_cast<int32>(Proxy->Pass));
		}
		return false;
	}

	// 현재 프레임 가시 집합이 아닌 프록시 ID는 stale ID로 간주하고 무시한다.
	// (RT readback 지연/프레임 경계에서 이전 ID가 섞일 수 있음)
	if (!Proxy->bInVisibleSet || !Proxy->bVisible)
	{
		if constexpr (bDebugIdPickingTrace)
		{
			DebugIdPickTrace("[IDPickTrace] StaleProxy PickedId=%u ProxyId=%u bInVisibleSet=%d bVisible=%d",
				PickedId, ProxyId, Proxy->bInVisibleSet ? 1 : 0, Proxy->bVisible ? 1 : 0);
		}
		return false;
	}

	AActor* PickedActor = Proxy->Owner->GetOwner();
	if (!PickedActor->IsVisible() || PickedActor->GetWorld() != InteractionWorld)
	{
		if constexpr (bDebugIdPickingTrace)
		{
			DebugIdPickTrace("[IDPickTrace] RejectedActor PickedId=%u ProxyId=%u Actor=%s Visible=%d SameWorld=%d",
				PickedId,
				ProxyId,
				PickedActor->GetFName().ToString().c_str(),
				PickedActor->IsVisible() ? 1 : 0,
				PickedActor->GetWorld() == InteractionWorld ? 1 : 0);
		}
		return false;
	}

	// Billboard는 ID 픽셀을 맞췄더라도 최종적으로 클릭 지점의 텍스처 alpha를 한 번 더 확인한다.
	// (셰이더/샘플러/LOD 차이로 인한 투명부 오선택 방지)
	if (const UBillboardComponent* BillboardComp = Cast<UBillboardComponent>(Proxy->Owner))
	{
		if (BillboardComp->GetTexture() && BillboardComp->GetTexture()->SRV && Camera && Viewport)
		{
			const float VPWidth = static_cast<float>(Viewport->GetWidth());
			const float VPHeight = static_cast<float>(Viewport->GetHeight());
			const FRay PickRay = Camera->DeprojectScreenToWorld(
				static_cast<float>(InLocalPoint.x),
				static_cast<float>(InLocalPoint.y),
				VPWidth,
				VPHeight);

			const FMatrix BillboardWorld = BillboardComp->ComputeBillboardMatrix(Camera->GetForwardVector());
			const float WidthScale = (std::max)(0.0001f, BillboardComp->GetWidth() * 0.5f);
			const float HeightScale = (std::max)(0.0001f, BillboardComp->GetHeight() * 0.5f);
			const FMatrix SpriteSizeScale = FMatrix::MakeScaleMatrix(FVector(1.0f, WidthScale, HeightScale));
			const FMatrix FullWorld = SpriteSizeScale * BillboardWorld;
			const FVector PlaneCenter = FullWorld.GetLocation();
			FVector PlaneNormal = FullWorld.TransformVector(FVector(1.0f, 0.0f, 0.0f));
			PlaneNormal.Normalize();

			const float Denom = PlaneNormal.Dot(PickRay.Direction);
			if (std::abs(Denom) > 1e-6f)
			{
				const float T = (PlaneCenter - PickRay.Origin).Dot(PlaneNormal) / Denom;
				if (T >= 0.0f)
				{
					const FVector HitPoint = PickRay.Origin + PickRay.Direction * T;
					const FMatrix InvWorld = FullWorld.GetInverse();
					const FVector LocalHit = HitPoint * InvWorld;
					const float U = LocalHit.Y + 0.5f;
					const float V = 0.5f - LocalHit.Z;
					if (U >= 0.0f && U <= 1.0f && V >= 0.0f && V <= 1.0f)
					{
						float Alpha01 = 1.0f;
						const bool bAlphaSampled = SampleTextureAlphaAtUV(Context, BillboardComp->GetTexture()->SRV, U, V, Alpha01);
						if (bAlphaSampled && Alpha01 < BillboardPickAlphaThreshold)
						{
							if constexpr (bDebugIdPickingTrace)
							{
								DebugIdPickTrace("[IDPickTrace] BillboardAlphaReject U=%.4f V=%.4f Alpha=%.4f Threshold=%.4f Actor=%s",
									U, V, Alpha01, BillboardPickAlphaThreshold, PickedActor->GetFName().ToString().c_str());
							}
							return false;
						}
					}
				}
			}
		}
	}

	if constexpr (bDebugIdPickingTrace)
	{
		const uint32 SectionCount = static_cast<uint32>(Proxy->SectionDraws.size());
		ID3D11ShaderResourceView* FirstSRV = nullptr;
		if (!Proxy->SectionDraws.empty())
		{
			FirstSRV = Proxy->SectionDraws.front().DiffuseSRV;
		}

		const UBillboardComponent* BillboardComp = Cast<UBillboardComponent>(Proxy->Owner);
		const FTextureResource* BillboardTex = BillboardComp ? BillboardComp->GetTexture() : nullptr;
		const char* TexturePath = (BillboardTex && !BillboardTex->Path.empty()) ? BillboardTex->Path.c_str() : "None";

		DebugIdPickTrace("[IDPickTrace] Hit Local=(%d,%d) Pixel=(%u,%u) PickedId=%u ProxyId=%u Actor=%s Comp=%s Pass=%d Mesh=%p Shader=%p Sections=%u FirstSRV=%p BillboardTex=%p TexPath=%s",
			InLocalPoint.x,
			InLocalPoint.y,
			PixelX,
			PixelY,
			PickedId,
			ProxyId,
			PickedActor->GetFName().ToString().c_str(),
			Proxy->Owner->GetFName().ToString().c_str(),
			static_cast<int32>(Proxy->Pass),
			Proxy->MeshBuffer,
			Proxy->Shader,
			SectionCount,
			FirstSRV,
			BillboardTex,
			TexturePath);
	}

	OutActor = PickedActor;
	return true;
}

bool FEditorViewportClient::WantsRelativeMouseMode(const FViewportInputContext& Context, POINT& OutRestoreScreenPos) const
{
	OutRestoreScreenPos = Context.Frame.MouseScreenPos;
	if (!Camera || !Viewport)
	{
		return false;
	}
	// ImGui가 마우스를 캡처 중인 동안에는 상대마우스 모드 진입/유지를 금지한다.
	// (UI 클릭/드래그는 정상 동작하되, 커서 중앙 고정/복귀로 인한 간섭을 막는다.)
	if (Context.bImGuiCapturedMouse)
	{
		return false;
	}
	if (EditorViewportInputUtils::IsInViewportToolbarDeadZone(Context))
	{
		return false;
	}

	bool bGizmoBlocksLeftRelativeDrag = Gizmo && (Gizmo->IsHolding() || Gizmo->IsPressedOnHandle() || Gizmo->IsHovered());
	const bool bGizmoInteractionActive = Gizmo && (Gizmo->IsHolding() || Gizmo->IsPressedOnHandle());
	if (bGizmoInteractionActive)
	{
		return false;
	}
	const bool bLeftLookChord = Context.Frame.IsDown(VK_LBUTTON) && !Context.Frame.IsAltDown() && !Context.Frame.IsCtrlDown();

	// LMB 네비게이션 relative 유지 중에는 gizmo hover 재검사로 모드가 흔들리지 않도록 유지 우선.
	if (Context.bRelativeMouseMode && bLeftLookChord)
	{
		return true;
	}

	if (!bGizmoBlocksLeftRelativeDrag
		&& Gizmo
		&& Context.Frame.IsDown(VK_LBUTTON))
	{
		const float LocalMouseX = static_cast<float>(Context.MouseLocalPos.x);
		const float LocalMouseY = static_cast<float>(Context.MouseLocalPos.y);
		const float VPWidth = static_cast<float>(Viewport->GetWidth());
		const float VPHeight = static_cast<float>(Viewport->GetHeight());
		const FRay MouseRay = Camera->DeprojectScreenToWorld(LocalMouseX, LocalMouseY, VPWidth, VPHeight);
		FHitResult GizmoHit{};
		bGizmoBlocksLeftRelativeDrag = Gizmo->LineTraceComponent(MouseRay, GizmoHit);
	}

	const bool bLeftRelativeDrag = EditorViewportInputUtils::IsLeftNavigationDragActive(Context) && !bGizmoBlocksLeftRelativeDrag;
	const bool bRightHeld = EditorViewportInputMapping::IsTriggered(Context, EditorViewportInputMapping::EEditorViewportAction::NavLookRightDown);
	const bool bRightLookDrag = bRightHeld
		&& (Context.Frame.bRightDragging || Context.WasPointerDragStarted(EPointerButton::Right) || Context.bRelativeMouseMode);
	const bool bMouseOwnedByViewport = Context.bCaptured || Context.bHovered || Context.bRelativeMouseMode;
	if (!bMouseOwnedByViewport)
	{
		return false;
	}

	return bRightLookDrag
		|| EditorViewportInputMapping::IsTriggered(Context, EditorViewportInputMapping::EEditorViewportAction::NavLookMiddleDown)
		|| EditorViewportInputMapping::IsTriggered(Context, EditorViewportInputMapping::EEditorViewportAction::NavOrbitAltLeftDown)
		|| EditorViewportInputMapping::IsTriggered(Context, EditorViewportInputMapping::EEditorViewportAction::NavDollyAltRightDown)
		|| EditorViewportInputMapping::IsTriggered(Context, EditorViewportInputMapping::EEditorViewportAction::NavPanAltMiddleDown)
		|| bLeftRelativeDrag;
}

bool FEditorViewportClient::WantsAbsoluteMouseClip(const FViewportInputContext& Context, RECT& OutClipScreenRect) const
{
	OutClipScreenRect = { 0, 0, 0, 0 };
	if (!Gizmo || !Context.Frame.IsDown(VK_LBUTTON))
	{
		return false;
	}

	// Gizmo 드래그(또는 핸들 press 직후) 중에는 커서가 viewport 밖으로 빠져나가
	// drag state가 흔들리는 것을 방지하기 위해 viewport rect로 커서를 제한한다.
	if (!(Gizmo->IsHolding() || Gizmo->IsPressedOnHandle()))
	{
		return false;
	}

	const FRect& R = ViewportScreenRect;
	if (R.Width <= 1.0f || R.Height <= 1.0f || !Window)
	{
		return false;
	}

	POINT TopLeft =
	{
		static_cast<LONG>(R.X),
		static_cast<LONG>(R.Y)
	};
	POINT BottomRight =
	{
		static_cast<LONG>(R.X + R.Width),
		static_cast<LONG>(R.Y + R.Height)
	};
	POINT ClientTopLeft = { 0, 0 };
	::ClientToScreen(Window->GetHWND(), &TopLeft);
	::ClientToScreen(Window->GetHWND(), &BottomRight);
	::ClientToScreen(Window->GetHWND(), &ClientTopLeft);

	OutClipScreenRect.left = TopLeft.x;
	OutClipScreenRect.top =
		(TopLeft.y + EditorViewportInputUtils::PaneToolbarHeight > ClientTopLeft.y)
		? (TopLeft.y + EditorViewportInputUtils::PaneToolbarHeight)
		: ClientTopLeft.y;
	OutClipScreenRect.right = BottomRight.x;
	OutClipScreenRect.bottom = BottomRight.y;
	return true;
}

void FEditorViewportClient::UpdateLayoutRect()
{
	if (!LayoutWindow) return;

	const FRect& R = LayoutWindow->GetRect();
	ViewportScreenRect = R;

	// FViewport 리사이즈 요청 (슬롯 크기와 RT 크기 동기화)
	if (Viewport)
	{
		const float SafeWidth = (R.Width > 1.0f) ? R.Width : 1.0f;
		const float SafeHeight = (R.Height > 1.0f) ? R.Height : 1.0f;
		uint32 SlotW = static_cast<uint32>(std::lround(SafeWidth));
		uint32 SlotH = static_cast<uint32>(std::lround(SafeHeight));
		if (SlotW > 0 && SlotH > 0 && (SlotW != Viewport->GetWidth() || SlotH != Viewport->GetHeight()))
		{
			Viewport->RequestResize(SlotW, SlotH);
		}
	}
}

void FEditorViewportClient::RenderViewportImage(bool bIsActiveViewport, bool bDrawActiveOutline)
{
	if (!Viewport) return;

	const FRect& R = ViewportScreenRect;
	if (R.Width <= 0 || R.Height <= 0) return;

	ID3D11ShaderResourceView* ViewSRV = Viewport->GetSRV();
	if (FEditorSettings::Get().bShowIdBufferOverlay)
	{
		if (ID3D11ShaderResourceView* IdDebugSRV = Viewport->GetIdPickDebugSRV())
		{
			ViewSRV = IdDebugSRV;
		}
	}
	if (!ViewSRV)
	{
		return;
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	ImVec2 Min(R.X, R.Y);
	ImVec2 Max(R.X + R.Width, R.Y + R.Height);
	constexpr float ToolbarBorderOffsetY = 34.0f;
	const ImVec2 OutlineMin(R.X, R.Y + ToolbarBorderOffsetY);

	DrawList->AddImage((ImTextureID)ViewSRV, Min, Max);

	// 활성 뷰포트 테두리 강조
	if (bIsActiveViewport && bDrawActiveOutline)
	{
		DrawList->AddRect(OutlineMin, Max, IM_COL32(255, 200, 0, 200), 0.0f, 0, 2.0f);
	}

	if (bPIEOutlineFlashActive && PIEOutlineFlashFadeDuration > 0.0f)
	{
		float Alpha01 = 1.0f;
		if (PIEOutlineFlashElapsed > PIEOutlineFlashHoldDuration)
		{
			const float FadeElapsed = PIEOutlineFlashElapsed - PIEOutlineFlashHoldDuration;
			Alpha01 = 1.0f - Clamp(FadeElapsed / PIEOutlineFlashFadeDuration, 0.0f, 1.0f);
		}
		const int32 Alpha = static_cast<int32>(Alpha01 * 255.0f);
		DrawList->AddRect(OutlineMin, Max, IM_COL32(80, 255, 120, Alpha), 0.0f, 0, 3.0f);
	}

	POINT MarqueeStart = { 0, 0 };
	POINT MarqueeCurrent = { 0, 0 };
	bool bMarqueeAdditive = false;
	const bool bHasMarquee = InputController && InputController->GetSelectionMarquee(MarqueeStart, MarqueeCurrent, bMarqueeAdditive);
	if (bHasMarquee)
	{
		const float StartX = R.X + static_cast<float>(MarqueeStart.x);
		const float StartY = R.Y + static_cast<float>(MarqueeStart.y);
		const float CurrentX = R.X + static_cast<float>(MarqueeCurrent.x);
		const float CurrentY = R.Y + static_cast<float>(MarqueeCurrent.y);
		const float Left = (std::min)(StartX, CurrentX);
		const float Top = (std::min)(StartY, CurrentY);
		const float Right = (std::max)(StartX, CurrentX);
		const float Bottom = (std::max)(StartY, CurrentY);
		DrawList->AddRectFilled(ImVec2(Left, Top), ImVec2(Right, Bottom), IM_COL32(255, 255, 255, 48));
		DrawList->AddRect(ImVec2(Left, Top), ImVec2(Right, Bottom), IM_COL32(255, 255, 255, 210), 0.0f, 0, 1.5f);
	}
}

void FEditorViewportClient::TriggerPIEStartOutlineFlash(float HoldSeconds, float FadeSeconds)
{
	PIEOutlineFlashHoldDuration = HoldSeconds > 0.0f ? HoldSeconds : 1.0f;
	PIEOutlineFlashFadeDuration = FadeSeconds > 0.0f ? FadeSeconds : 2.0f;
	PIEOutlineFlashElapsed = 0.0f;
	bPIEOutlineFlashActive = true;
}

void FEditorViewportClient::ClearPIEStartOutlineFlash()
{
	bPIEOutlineFlashActive = false;
	PIEOutlineFlashElapsed = 0.0f;
}
