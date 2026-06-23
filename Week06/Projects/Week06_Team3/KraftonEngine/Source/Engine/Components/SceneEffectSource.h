#pragma once

#include "Render/Pipeline/RenderConstants.h"

// 씬 효과 제공자가 FScene에 노출시킬 정보
class ISceneEffectSource
{
public:
	virtual ~ISceneEffectSource() = default;

	virtual bool IsSceneEffectActive() const = 0;
	virtual void WriteSceneEffectConstants(FSceneEffectConstants& OutConstants, uint32 SlotIndex) const = 0;
};
