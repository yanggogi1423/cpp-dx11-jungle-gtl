#pragma once

#include "Core/Types/CoreTypes.h"

struct FCameraEditorMeshSelfTestResult
{
	bool bPassed = false;
	int32 ChecksRun = 0;
	FString Message;
};

class FCameraEditorMeshDiagnostics
{
public:
	static FCameraEditorMeshSelfTestResult RunSelfTest();
};
