#pragma once

#include "Serialization/Archive.h"

#include <fstream>

struct FWindowsBinReader : public FArchive
{
	std::ifstream Stream;
	FString CurrentKey;
	IObjectReferenceResolver* ObjectResolver = nullptr;
	bool bError = false;

	explicit FWindowsBinReader(const FString& Path);
	~FWindowsBinReader() override;

	bool IsLoading() const override { return true; }
	bool HasKey(const FString& Key) override;
	const FString& GetCurrentKey() override { return CurrentKey; }
	void SetCurrentKey(const FString& Key) override { CurrentKey = Key; }
	IObjectReferenceResolver* GetObjectResolver() override { return ObjectResolver; }
	void SetObjectResolver(IObjectReferenceResolver* InResolver) override { ObjectResolver = InResolver; }
	bool HasError() const { return bError; }

	void Serialize(void* Data, uint32 Size) override;
	void BeginArray(const FString& Key, int32& OutCount) override;

	FArchive& operator<<(bool& Value) override;
	FArchive& operator<<(int32& Value) override;
	FArchive& operator<<(uint32& Value) override;
	FArchive& operator<<(float& Value) override;
	FArchive& operator<<(const char* Value) override;
	FArchive& operator<<(FString& Value) override;
	FArchive& operator<<(FName& Value) override;
	FArchive& operator<<(FVector2& Value) override;
	FArchive& operator<<(FVector& Value) override;
	FArchive& operator<<(FVector4& Value) override;
	FArchive& operator<<(FColor& Value) override;
	FArchive& operator<<(FMatrix& Value) override;
};
