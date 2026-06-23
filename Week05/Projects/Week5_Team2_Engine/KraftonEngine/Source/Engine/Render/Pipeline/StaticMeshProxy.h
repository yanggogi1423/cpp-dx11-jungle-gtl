#pragma once
#include "PrimitiveProxy.h"

class UStaticMeshComponent;

class FStaticMeshProxy : public FPrimitiveProxy
{
public:
	FStaticMeshProxy(UStaticMeshComponent* InOwner);
	virtual ~FStaticMeshProxy() = default;

	void UpdateProxy() override;
};
