#include "ObjViewer/ObjViewerViewportController.h"

#include "ObjViewer/ObjViewerViewportClient.h"

namespace
{
std::unique_ptr<IObjViewerViewportMode> CreateMode(EObjViewerViewportModeType InModeType, FObjViewerViewportClient* InOwner)
{
	switch (InModeType)
	{
	case EObjViewerViewportModeType::Orbit:
	default:
		return std::make_unique<FObjViewerOrbitMode>(InOwner);
	}
}
}

FObjViewerViewportController::FObjViewerViewportController(FObjViewerViewportClient* InOwner)
	: Owner(InOwner)
{
	if (Owner)
	{
		ActiveMode = CreateMode(EObjViewerViewportModeType::Orbit, Owner);
	}
}

bool FObjViewerViewportController::SetMode(EObjViewerViewportModeType InModeType)
{
	if (!Owner)
	{
		return false;
	}

	if (ActiveMode && ActiveMode->GetType() == InModeType)
	{
		return true;
	}

	ActiveMode = CreateMode(InModeType, Owner);
	return ActiveMode != nullptr;
}

EObjViewerViewportModeType FObjViewerViewportController::GetMode() const
{
	if (!ActiveMode)
	{
		return EObjViewerViewportModeType::Orbit;
	}

	return ActiveMode->GetType();
}

bool FObjViewerViewportController::HandleCommandInput(float DeltaTime)
{
	(void)DeltaTime;
	if (!Owner)
	{
		return false;
	}

	if (Owner->InputContext.bImGuiCapturedKeyboard)
	{
		return true;
	}

	return false;
}

bool FObjViewerViewportController::HandleNavigationInput(float DeltaTime)
{
	if (!ActiveMode)
	{
		return false;
	}

	return ActiveMode->HandleNavigationInput(DeltaTime);
}
