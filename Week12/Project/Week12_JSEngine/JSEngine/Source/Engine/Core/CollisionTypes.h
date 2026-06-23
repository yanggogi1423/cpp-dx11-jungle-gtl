#pragma once
#include "Math/Vector.h" // 필요한 최소한의 수학 라이브러리만

class AActor;
class UPrimitiveComponent;

struct FCollisionQueryParams
{
    AActor* IgnoredActor = nullptr;
    UPrimitiveComponent* IgnoredComponent = nullptr;
    bool bTraceVisibleOnly = true;
    bool bSimpleCollisionOnly = false;
};

enum class ECollisionShapeType : uint8
{
    Line,
    Sphere,
    Box,
    Capsule,
};

struct FCollisionShape
{
    ECollisionShapeType ShapeType = ECollisionShapeType::Line;
    FVector HalfExtent = FVector::ZeroVector;
    float Radius = 0.0f;
    float HalfHeight = 0.0f;

    static FCollisionShape MakeLine()
    {
        return FCollisionShape();
    }

    static FCollisionShape MakeSphere(float InRadius)
    {
        FCollisionShape Shape;
        Shape.ShapeType = ECollisionShapeType::Sphere;
        Shape.Radius = InRadius;
        return Shape;
    }

    static FCollisionShape MakeBox(const FVector& InHalfExtent)
    {
        FCollisionShape Shape;
        Shape.ShapeType = ECollisionShapeType::Box;
        Shape.HalfExtent = InHalfExtent;
        return Shape;
    }

    static FCollisionShape MakeCapsule(float InRadius, float InHalfHeight)
    {
        FCollisionShape Shape;
        Shape.ShapeType = ECollisionShapeType::Capsule;
        Shape.Radius = InRadius;
        Shape.HalfHeight = InHalfHeight;
        return Shape;
    }

    bool IsLine() const { return ShapeType == ECollisionShapeType::Line; }
    bool IsSphere() const { return ShapeType == ECollisionShapeType::Sphere; }
    bool IsBox() const { return ShapeType == ECollisionShapeType::Box; }
    bool IsCapsule() const { return ShapeType == ECollisionShapeType::Capsule; }
};

struct FHitResult 
{
    UPrimitiveComponent* HitComponent = nullptr;

    float Distance = FLT_MAX;
	
	// World-space hit location. For sweep queries, this is the swept shape center/location at impact.
    FVector Location = { 0, 0, 0 };
	// World-space contact point on the hit surface.
    FVector ImpactPoint = { 0, 0, 0 };
    FVector Normal = { 0, 0, 0 };
	
    int FaceIndex = -1; 

    bool bHit = false;
	
	void Reset()
	{
		HitComponent = nullptr;
		Distance = FLT_MAX;
		Location = { 0, 0, 0 };
		ImpactPoint = { 0, 0, 0 };
		Normal = { 0, 0, 0 };
		FaceIndex = -1;
		bHit = false;
	}
	
	bool IsValid() const
	{
		return bHit && (HitComponent != nullptr);
	}
};
