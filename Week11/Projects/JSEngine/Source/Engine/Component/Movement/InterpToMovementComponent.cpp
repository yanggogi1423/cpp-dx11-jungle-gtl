#include "InterpToMovementComponent.h"

#include "GameFramework/AActor.h"
#include "Object/Function.h"
#include "Object/ObjectFactory.h"
#include "Component/SceneComponent.h"
#include "Math/Quat.h"

#include "Blueprint/BlueprintGraph.h"
#include "Blueprint/BlueprintGraphExecutor.h"
#include "Blueprint/BlueprintAsset.h"

namespace {
	// Returns normalized direction from A to B
	FVector GetNormalizedDir(const FVector& A, const FVector& B) {
		FVector Dir = B - A;
		return Dir.GetSafeNormal();
	}

	float GetYaw(const FVector& NormDir) {
        return asinf(NormDir.Z);
	}

	float GetPitch(const FVector& NormDir) {
        return atan2f(NormDir.Y, NormDir.X);
	}
}

// --- Overrides ---------------------------------------------------------
void UInterpToMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);

	int32 BehaviourValue = static_cast<int32>(InterpBehaviour);
	Ar << "Interpolation Behaviour" << BehaviourValue;

	if (Ar.IsLoading())
	{
		InterpBehaviour = static_cast<EInterpBehaviour>(BehaviourValue);
	}

	Ar << "Control Points" << ControlPoints;
	Ar << "Duration" << Duration;
	Ar << "Rotation Speed" << RotateDuration;
	Ar << "Auto Activate" << bAutoActivate;
	Ar << "Orient To Movement" << bFaceTargetDir;
}

void UInterpToMovementComponent::BeginPlay() {
	for (auto& ControlPoint : ControlPoints) {
		ControlPoint += UpdatedComponent->GetWorldLocation();
	}
    if (bAutoActivate)
    {
        Initiate();
    }
}

void UInterpToMovementComponent::TickComponent(float DeltaTime) {
	if (!bisLerping) return;
	Elapsed += DeltaTime;

	// Lerp
	UpdateLerp(DeltaTime);

	// Rotate towards movement direction
	FaceTargetDir(DeltaTime);
}

void UInterpToMovementComponent::PostDuplicate(UObject* Original) {
    UActorComponent::PostDuplicate(Original);
    const UInterpToMovementComponent* Orig = Cast<UInterpToMovementComponent>(Original);

    // Copy configuration
    ControlPoints		= Orig->ControlPoints;
    Duration			= Orig->Duration;
    InterpBehaviour		= Orig->InterpBehaviour;

    Elapsed				= 0.f;
    bisLerping			= false;
    bPing				= true;
    CurrentPointID		= 0;
    NextPointID			= 1;
    TotalDistance		= 0.f;
    NextDistRatio		= 0.f;
}

void UInterpToMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) {
    UMovementComponent::GetEditableProperties(OutProps);
}

// --- Control Point Management--------------------------------------------
void UInterpToMovementComponent::AddControlPoint(FVector InControlPoint) {
	ControlPoints.push_back(InControlPoint);
	Reset();
}

void UInterpToMovementComponent::RemoveControlPoint(uint32 Index) {  
   if (Index >= ControlPoints.size()) return;  
   ControlPoints.erase(ControlPoints.begin() + Index);  
}

FVector& UInterpToMovementComponent::GetControlPoint(uint32 Index) {
	assert(Index < ControlPoints.size());
	return ControlPoints[Index];
}

void UInterpToMovementComponent::SetControlPoint(uint32 Index, FVector InPoint) {
	if (Index >= ControlPoints.size()) return;
	ControlPoints[Index] = InPoint;
}

// --- Interpolation Duration ---------------------------------------------
void UInterpToMovementComponent::SetInterpDuration(float InDuration) {
	if (InDuration <= 0) return;
	Duration = InDuration;
}

// --- Interpolation Behaviour --------------------------------------------
void UInterpToMovementComponent::SetInterpolationBehaviour(EInterpBehaviour InBehaviour) {
	Reset();
	InterpBehaviour = InBehaviour;
}

// --- Misc ---------------------------------------------------------------
void UInterpToMovementComponent::Initiate() {
	if (ControlPoints.size() <= 1) return;
	Reset();
	bisLerping = true;

	for (size_t i = ControlPoints.size() - 1; i > 0; i--)
    {
		TotalDistance += FVector::Dist(ControlPoints[i], ControlPoints[i - 1]);
	}

	SetNextDistRatio();

	// Snap to target direction upon init if flagged
	if (bFaceTargetDir && ControlPoints.size() >= 2) {
		FVector NormDir = GetNormalizedDir(ControlPoints[0], ControlPoints[1]);
		TargetYaw		= GetYaw(NormDir);
		TargetPitch		= GetPitch(NormDir);
		RotateDuration  = 0.01f;
	}
}

void UInterpToMovementComponent::Reset() {
    bPing			= true;
	Elapsed			= 0.f;
	CurrentPointID	= 0;
	NextPointID		= 1;
	TotalDistance   = 0;

	if (!ControlPoints.empty() && UpdatedComponent) {
		UpdatedComponent->SetWorldLocation(ControlPoints.front());
	}
}

void UInterpToMovementComponent::ResetAndHalt()
{
	Reset();
	bisLerping = false;

	UBlueprintAsset* LoadedAsset = new UBlueprintAsset();
	const bool bLoaded = LoadedAsset->LoadFromFile("Asset/Blueprint/TestBlueprint.uasset");

	bool bResolved = false;
	if (bLoaded)
	{
		bResolved = LoadedAsset->GetGraph().ResolveRuntimeReferences(this);
	}

	UE_LOG("Loaded: %d, Resolved: %d, Nodes: %d, Links: %d",
		bLoaded ? 1 : 0,
		bResolved ? 1 : 0,
		static_cast<int32>(LoadedAsset->GetGraph().Nodes.size()),
		static_cast<int32>(LoadedAsset->GetGraph().Links.size()));

	FBlueprintExecutionContext Context;
	Context.Self = this;

	FBlueprintGraphExecutor Executor;
	const bool bExecuted = Executor.ExecuteEvent(
		LoadedAsset->GetGraph(),
		"BeginPlay",
		Context
	);

	UE_LOG("Loaded blueprint graph executed: %d", bExecuted ? 1 : 0);
}

void UInterpToMovementComponent::TestFunction(float Value, bool bEnabled)
{
	UE_LOG("TestFunction called: Value=%f, bEnabled=%d", Value, bEnabled ? 1 : 0);
}

float UInterpToMovementComponent::TestFunction2() const
{
	UE_LOG("TestFunction2 called");
	return 123.0f;
}

void UInterpToMovementComponent::TestComponentFunction()
{
	UE_LOG("TestNoParam called by blueprint graph");
}

float UInterpToMovementComponent::TestReturn()
{
	UE_LOG("TestReturn called by blueprint graph");
	return 456.0f;
}

// --- Private -----------------------------------------------------------
void UInterpToMovementComponent::Ping() {
	// Forward
	if (bPing) return;
	bPing = true;
	CurrentPointID = 0;
	NextPointID    = 1;
}

void UInterpToMovementComponent::Pong() {
	// Backward
	if (!bPing) return;
	bPing = false;
    CurrentPointID = (uint32)(ControlPoints.size() - 1);
    NextPointID	   = (uint32)(ControlPoints.size() - 2);
}

void UInterpToMovementComponent::UpdateLerp(float DeltaTime) {
    // Lerp
    if (ControlPoints.size() <= 1 || NextPointID >= ControlPoints.size())
        return;
    float Alpha = (Elapsed / (Duration * NextDistRatio));
    UpdatedComponent->SetWorldLocation(FVector::Lerp(ControlPoints[CurrentPointID], ControlPoints[NextPointID], Alpha));

    // Check if target point has been reached
    if (Alpha >= 1.f)
    {
        DestinationReached();
    }
}

void UInterpToMovementComponent::FaceTargetDir(float DeltaTime) {
	if (!UpdatedComponent) return;
	if (!bFaceTargetDir || NextPointID >= ControlPoints.size()) return;

	FVector Dir = GetNormalizedDir(ControlPoints[CurrentPointID], ControlPoints[NextPointID]);
	if (Dir.IsNearlyZero()) return;

	// Build target quaternion that rotates the forward axis (+X) onto Dir.
	// Uses the half-vector trick: avoids acosf and handles all angles robustly.
	const FVector Forward = FVector::ForwardVector;
	const FVector Half = (Forward + Dir).GetSafeNormal();

	FQuat TargetQuat;
	if (Half.IsNearlyZero())
	{
		// Dir is exactly anti-parallel to Forward. Rotate 180 Deg around Up (+Z)
		TargetQuat = FQuat(FVector::UpVector, 3.14159265f);
	}
	else
	{
		FVector Axis = FVector::CrossProduct(Forward, Half);
		float   W    = FVector::DotProduct(Forward, Half);
		TargetQuat   = FQuat(Axis.X, Axis.Y, Axis.Z, W);
		TargetQuat.Normalize();
	}

	// Slerp from the current rotation towards the target over RotateDuration
	FQuat Current = UpdatedComponent->GetRelativeQuat();
	float Alpha   = (RotateDuration > 0.f) ? (DeltaTime / RotateDuration) : 1.f;
	Alpha = Alpha < 1.f ? Alpha : 1.f;

	UpdatedComponent->SetRelativeRotationQuat(FQuat::Slerp(Current, TargetQuat, Alpha));
}

void UInterpToMovementComponent::DestinationReached() {
	if (bPing && NextPointID >= ControlPoints.size() - 1) {
		EndOfChain();
		return;
	}

	if (!bPing && NextPointID <= 0)
    {
        EndOfChain();
        return;
	}

	Elapsed = 0;
	if (InterpBehaviour == EInterpBehaviour::PingPong && !bPing) {
		CurrentPointID -= 1;
		NextPointID	   -= 1;
	} else {
		CurrentPointID += 1;
		NextPointID    += 1;
	}
    SetNextDistRatio();
	SetRotationSpeed();
}

void UInterpToMovementComponent::EndOfChain() {
    switch (InterpBehaviour)
    {
    case EInterpBehaviour::OneShot:
    {
		bisLerping = false;
        return;
    }
    case EInterpBehaviour::OneShotReverse:
    {
        ResetAndHalt();
        return;
    }
    case EInterpBehaviour::Loop:
    {
		float CachedDist = TotalDistance;
        Reset();
		TotalDistance	 = CachedDist;
		SetNextDistRatio();
        return;
    }
    case EInterpBehaviour::PingPong:
    {
        Elapsed = 0;
        if (bPing)
            Pong();
        else
            Ping();
        return;
    }
    }
}

void UInterpToMovementComponent::SetNextDistRatio() {
	if (TotalDistance == 0) {
		NextDistRatio = 0;
		return;
	}

    NextDistRatio =
		FVector::Dist(ControlPoints[CurrentPointID], ControlPoints[NextPointID]) / TotalDistance;
}

void UInterpToMovementComponent::SetRotationSpeed() {
    float InvSpeed = Duration / (TotalDistance * NextDistRatio);
    float MinDuration = 0.8f;
    RotateDuration = MinDuration < InvSpeed ? MinDuration : InvSpeed;
}
