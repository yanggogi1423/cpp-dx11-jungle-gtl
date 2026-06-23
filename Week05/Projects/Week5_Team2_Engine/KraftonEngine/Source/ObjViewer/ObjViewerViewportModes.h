#pragma once

#include "Core/CoreTypes.h"
#include "ObjViewer/ObjViewerViewportTools.h"

#include <memory>

class FObjViewerViewportClient;

enum class EObjViewerViewportModeType : uint8
{
	Orbit
};

class IObjViewerViewportMode
{
public:
	virtual ~IObjViewerViewportMode() = default;
	virtual EObjViewerViewportModeType GetType() const = 0;
	virtual bool HandleNavigationInput(float DeltaTime) = 0;
};

class FObjViewerOrbitMode final : public IObjViewerViewportMode
{
public:
	explicit FObjViewerOrbitMode(FObjViewerViewportClient* InOwner);

	EObjViewerViewportModeType GetType() const override { return EObjViewerViewportModeType::Orbit; }
	bool HandleNavigationInput(float DeltaTime) override;

private:
	std::unique_ptr<IObjViewerViewportTool> NavigationTool;
};
