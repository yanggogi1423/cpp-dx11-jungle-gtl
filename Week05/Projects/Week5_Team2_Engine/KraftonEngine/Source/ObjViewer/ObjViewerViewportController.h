#pragma once

#include "ObjViewer/ObjViewerViewportModes.h"

#include <memory>

class FObjViewerViewportClient;

class FObjViewerViewportController
{
public:
	explicit FObjViewerViewportController(FObjViewerViewportClient* InOwner);

	bool SetMode(EObjViewerViewportModeType InModeType);
	EObjViewerViewportModeType GetMode() const;

	bool HandleCommandInput(float DeltaTime);
	bool HandleNavigationInput(float DeltaTime);

private:
	FObjViewerViewportClient* Owner = nullptr;
	std::unique_ptr<IObjViewerViewportMode> ActiveMode;
};
