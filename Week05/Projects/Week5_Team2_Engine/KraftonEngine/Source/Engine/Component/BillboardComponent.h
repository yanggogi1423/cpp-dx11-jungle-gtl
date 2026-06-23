#pragma once
#include "PrimitiveComponent.h"

class UTexture2D;

class UBillboardComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(UBillboardComponent, UPrimitiveComponent)

	void TickComponent(float DeltaTime) override;

	void SetSprite(UTexture2D* NewSprite);

	UTexture2D* GetSprite() const { return Sprite; }

	FMeshBuffer*       GetMeshBuffer() const override;
	const FMeshData*   GetMeshData()   const override;
	FPrimitiveProxy*   CreateProxy()   override;
	// void               UpdateWorldAABB() const override;
	// bool               LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult, float InClosestT) override;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

private:
	UTexture2D* Sprite = nullptr;
	FString SpritePath = "None";

	float U  = 0.0f;
	float UL = 1.0f;
	float V  = 0.0f;
	float VL = 1.0f;

	bool  bIsScreenSizeScaled = false;
};