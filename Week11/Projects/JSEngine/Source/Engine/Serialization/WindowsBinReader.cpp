#include "Serialization/WindowsBinReader.h"

#include "Core/Paths.h"

#include <filesystem>

FWindowsBinReader::FWindowsBinReader(const FString& Path)
	: CurrentKey(), bError(false)
{

	Stream.open(std::filesystem::path(FPaths::ToWide(Path)), std::ios::binary);
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

void FWindowsBinReader::Serialize(void* Data, uint32 Size)
{
	if (bError) return;
	Stream.read(reinterpret_cast<char*>(Data), Size);
	if (!Stream.good())
	{
		bError = true;
	}
}

FArchive& FWindowsBinReader::operator<<(const char* Value)
{
	CurrentKey = Value;
	return *this;
}

FArchive& FWindowsBinReader::operator<<(bool& Value)
{
	uint8 Stored = Value ? 1 : 0;
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
	static constexpr uint32 MaxStringLength = 16 * 1024 * 1024;
	uint32 Length = 0;
	Serialize(&Length, sizeof(Length));

	if (Length > MaxStringLength)
	{
		bError = true;
		Value.clear();
		return *this;
	}

	if (Length > 0)
	{
		Value.resize(Length);
		Serialize(Value.data(), Length);
	}
	else
	{
		Value.clear();
	}
	return *this;
}

FArchive& FWindowsBinReader::operator<<(FName& Value)
{
	FString NameStr;
	*this << NameStr;
	Value = FName(NameStr);
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

FArchive& FWindowsBinReader::operator<<(FRotator& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinReader::operator<<(FQuat& Value)
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
