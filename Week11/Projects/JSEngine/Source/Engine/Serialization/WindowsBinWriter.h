#pragma once

#include "Serialization/Archive.h"

#include <fstream>

struct FWindowsBinWriter : public FArchive
{
	std::ofstream Stream;
	FString CurrentKey;
	bool bError = false;

	FWindowsBinWriter(const FString& Path);
	~FWindowsBinWriter() override;

	EArchiveFormat GetFormat() const override { return EArchiveFormat::Binary; }
	bool IsSaving() const override { return true; }

	const FString& GetCurrentKey() override { return CurrentKey; }
	void SetCurrentKey(const FString& Key) override { CurrentKey = Key; }

	bool HasError() const { return bError; }

	void Serialize(void* Data, uint32 Size) override;

	void BeginArray(const FString& Key, int32& OutCount) override
	{
		*this << OutCount;
	}
	void EndArray() override {}

	FArchive& operator<<(const char* Value) override;
	FArchive& operator<<(bool& Value) override;
	FArchive& operator<<(int32& Value) override;
	FArchive& operator<<(uint32& Value) override;
	FArchive& operator<<(float& Value) override;
	FArchive& operator<<(FString& Value) override;
	FArchive& operator<<(FName& Value) override;
	FArchive& operator<<(FVector2& Value) override;
	FArchive& operator<<(FVector& Value) override;
	FArchive& operator<<(FVector4& Value) override;
	FArchive& operator<<(FRotator& Value) override;
	FArchive& operator<<(FQuat& Value) override;
	FArchive& operator<<(FColor& Value) override;
	FArchive& operator<<(FMatrix& Value) override;
};
