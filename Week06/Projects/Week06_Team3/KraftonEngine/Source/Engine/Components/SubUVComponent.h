#pragma once

#include "BillboardComponent.h"
#include "Core/ResourceTypes.h"
#include "Object/FName.h"

class USubUVComponent : public UBillboardComponent
{
public:
	DECLARE_CLASS(USubUVComponent, UBillboardComponent)

	USubUVComponent();
	~USubUVComponent() override = default;

	// --- Particle Resource ---
	// FName 키로 ResourceManager에서 FParticleResource*를 찾아 캐싱
	void SetParticle(const FName& InParticleName);
	const FParticleResource* GetParticle() const { return CachedParticle; }
	const FName& GetParticleName() const { return ParticleName; }

	// --- SubUV Frame ---
	void SetFrameIndex(uint32 InIndex)
	{
		const uint32 MinFrame = static_cast<uint32>(StartFrameIndex);
		const uint32 MaxFrame = static_cast<uint32>(EndFrameIndex);

		if (InIndex < MinFrame) FrameIndex = MinFrame;
		else if (InIndex > MaxFrame) FrameIndex = MaxFrame;
		else FrameIndex = InIndex;
	}
	uint32 GetFrameIndex() const { return FrameIndex; }
	uint32 GetColumns() const { return static_cast<uint32>(Columns); }
	uint32 GetRows() const { return static_cast<uint32>(Rows); }
	uint32 GetEndFrameIndex() const { return static_cast<uint32>(EndFrameIndex); }

	// --- Playback ---
	void SetFrameRate(float InFPS) { PlayRate = InFPS; }
	void SetLoop(bool bInLoop) { bLoop = bInLoop; }
	bool IsLoop()     const { return bLoop; }
	bool IsFinished() const { return !bLoop && bIsExecute; }
	void Play() { FrameIndex = StartFrameIndex; TimeAccumulator = 0.0f; bIsExecute = false; }

	// Sprite Size(Width/Height)는 UBillboardComponent로 끌어올림 — 상속받아 사용.

	// --- Property / Serialization ---
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	void UpdateWorldAABB() const override;
	FVector GetVisualScale() const override;

	// UI용
	void SyncFrameDisplayFromInternal()
	{
		StartFrameDisplay = StartFrameIndex + 1;
		EndFrameDisplay = EndFrameIndex + 1;
	}
	inline int32 GetStartFrameDisplay() const { return StartFrameIndex + 1; }
	inline void SetEndFrameDisplay(int32 InValue)
	{
		const int32 TotalFrames = std::max(1, Columns) * std::max(1, Rows);
		EndFrameDisplay = std::max(1, std::min(InValue, TotalFrames));
		EndFrameIndex = EndFrameDisplay - 1;

		// End < Start 방지
		if (EndFrameIndex < StartFrameIndex)
		{
			StartFrameIndex = EndFrameIndex;
			StartFrameDisplay = StartFrameIndex + 1;
		}
	}
	inline int32 GetEndFrameDisplay() const { return EndFrameIndex + 1; }
	inline void SetStartFrameDisplay(int32 InValue)
	{
		const int32 TotalFrames = std::max(1, Columns) * std::max(1, Rows);
		StartFrameDisplay = std::max(1, std::min(InValue, TotalFrames));
		StartFrameIndex = StartFrameDisplay - 1;

		// Start > End 방지
		if (StartFrameIndex > EndFrameIndex)
		{
			EndFrameIndex = StartFrameIndex;
			EndFrameDisplay = StartFrameDisplay;
		}
	}

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
private:
	FName ParticleName;
	FParticleResource* CachedParticle = nullptr; // ResourceManager 소유, 여기선 참조만

	uint32 FrameIndex = 0;
	int32 Columns = 1;
	int32 Rows = 1;
	int32 StartFrameIndex = 0;   // 내부에서 실제 값(0~Columns*Rows - 1)
	int32 EndFrameIndex = 0;
	int32 StartFrameDisplay = 1; // 에디터 표시용 값(1~Columns*Rows)
	int32 EndFrameDisplay = 1;
	float  PlayRate = 30.0f;   // 초당 프레임 수
	float  TimeAccumulator = 0.0f;

	bool bLoop = true;
	bool bIsExecute = false;

};
