#pragma once

class FObjViewerViewportClient;

class IObjViewerViewportTool
{
public:
	virtual ~IObjViewerViewportTool() = default;
	virtual bool HandleInput(float DeltaTime) = 0;
};

class FObjViewerNavigationTool final : public IObjViewerViewportTool
{
public:
	explicit FObjViewerNavigationTool(FObjViewerViewportClient* InOwner);
	bool HandleInput(float DeltaTime) override;

private:
	FObjViewerViewportClient* Owner = nullptr;
};
