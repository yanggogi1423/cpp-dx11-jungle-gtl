#include "ObjViewer/ObjViewerViewportModes.h"

#include "ObjViewer/ObjViewerViewportClient.h"

FObjViewerOrbitMode::FObjViewerOrbitMode(FObjViewerViewportClient* InOwner)
{
	if (InOwner)
	{
		NavigationTool = std::make_unique<FObjViewerNavigationTool>(InOwner);
	}
}

bool FObjViewerOrbitMode::HandleNavigationInput(float DeltaTime)
{
	if (!NavigationTool)
	{
		return false;
	}

	return NavigationTool->HandleInput(DeltaTime);
}
