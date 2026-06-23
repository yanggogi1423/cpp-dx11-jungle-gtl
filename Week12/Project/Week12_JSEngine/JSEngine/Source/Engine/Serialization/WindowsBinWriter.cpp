#include "Serialization/WindowsBinWriter.h"

#include "Core/Paths.h"
#include "Math/Matrix.h"
#include "Math/Vector2.h"

#include <filesystem>

FWindowsBinWriter::FWindowsBinWriter(const FString& Path)
{
	Stream.open(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(Path))), std::ios::binary | std::ios::trunc);
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
	if (bError || Size == 0)
	{
		return;
	}

	Stream.write(reinterpret_cast<const char*>(Data), Size);
	if (!Stream.good())
	{
		bError = true;
	}
}

void FWindowsBinWriter::BeginArray(const FString& Key, int32& OutCount)
{
	(void)Key;
	*this << OutCount;
}

FArchive& FWindowsBinWriter::operator<<(const char* Value)
{
	CurrentKey = Value ? Value : "";
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
	uint32 Length = static_cast<uint32>(Value.size());
	*this << Length;
	if (Length > 0)
	{
		Serialize(Value.data(), Length);
	}
	return *this;
}

FArchive& FWindowsBinWriter::operator<<(FName& Value)
{
	FString Text = Value.ToString();
	*this << Text;
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
