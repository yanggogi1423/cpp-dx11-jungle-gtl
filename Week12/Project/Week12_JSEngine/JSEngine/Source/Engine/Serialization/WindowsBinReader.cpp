#include "Serialization/WindowsBinReader.h"

#include "Core/Paths.h"
#include "Math/Matrix.h"
#include "Math/Vector2.h"

#include <filesystem>

namespace
{
	constexpr uint32 MaxBinaryStringLength = 16u * 1024u * 1024u;
	constexpr int32 MaxBinaryArrayCount = 16 * 1024 * 1024;
}

FWindowsBinReader::FWindowsBinReader(const FString& Path)
{
	Stream.open(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(Path))), std::ios::binary);
	if (!Stream.is_open())
	{
		bError = true;
	}
}

FWindowsBinReader::~FWindowsBinReader()
{
	if (Stream.is_open())
	{
		Stream.close();
	}
}

bool FWindowsBinReader::HasKey(const FString& Key)
{
	(void)Key;
	return !bError;
}

void FWindowsBinReader::Serialize(void* Data, uint32 Size)
{
	if (bError || Size == 0)
	{
		return;
	}

	Stream.read(reinterpret_cast<char*>(Data), Size);
	if (!Stream.good())
	{
		bError = true;
	}
}

void FWindowsBinReader::BeginArray(const FString& Key, int32& OutCount)
{
	(void)Key;
	*this << OutCount;
	if (OutCount < 0 || OutCount > MaxBinaryArrayCount)
	{
		bError = true;
		OutCount = 0;
	}
}

FArchive& FWindowsBinReader::operator<<(const char* Value)
{
	CurrentKey = Value ? Value : "";
	return *this;
}

FArchive& FWindowsBinReader::operator<<(bool& Value)
{
	uint8 Stored = 0;
	Serialize(&Stored, sizeof(Stored));
	Value = Stored != 0;
	return *this;
}

FArchive& FWindowsBinReader::operator<<(int32& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinReader::operator<<(uint32& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinReader::operator<<(float& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinReader::operator<<(FString& Value)
{
	uint32 Length = 0;
	*this << Length;
	if (Length > MaxBinaryStringLength)
	{
		bError = true;
		Value.clear();
		return *this;
	}

	Value.resize(Length);
	if (Length > 0)
	{
		Serialize(Value.data(), Length);
	}
	return *this;
}

FArchive& FWindowsBinReader::operator<<(FName& Value)
{
	FString Text;
	*this << Text;
	Value = FName(Text);
	return *this;
}

FArchive& FWindowsBinReader::operator<<(FVector2& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinReader::operator<<(FVector& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinReader::operator<<(FVector4& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinReader::operator<<(FColor& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinReader::operator<<(FMatrix& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}
