#include "Serialization/WindowsBinWriter.h"

#include "Core/Paths.h"

#include <filesystem>

FWindowsBinWriter::FWindowsBinWriter(const FString& Path)
	: CurrentKey(), bError(false)
{
	Stream.open(std::filesystem::path(FPaths::ToWide(Path)), std::ios::binary);
	if (!Stream.is_open())
	{
		bError = true;
	}
}

FWindowsBinWriter::~FWindowsBinWriter()
{
	if (Stream.is_open())
	{
		Stream.close();
	}
}

void FWindowsBinWriter::Serialize(void* Data, uint32 Size)
{
	if (bError) return;

	Stream.write(reinterpret_cast<const char*>(Data), Size);
	if (!Stream.good())
	{
		bError = true;
	}
}

FArchive& FWindowsBinWriter::operator<<(const char* Value)
{
	CurrentKey = Value;
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(bool& Value)
{
	uint8 Stored = Value ? 1 : 0;
	Serialize(&Stored, sizeof(Stored));
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(int32& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(uint32& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(float& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(FString& Value)
{
	uint32 Length = (uint32)Value.size();
	Serialize(&Length, sizeof(Length));
	Serialize(Value.data(), Length);
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(FName& Value)
{
	FString Str = Value.ToString();
	*this << Str;
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(FVector2& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(FVector& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(FVector4& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(FRotator& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(FQuat& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(FColor& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(FMatrix& Value)
{
	Serialize(&Value, sizeof(Value));
	return *this;
}
