#pragma once

#include "Core/CoreTypes.h"

struct FArchive;

struct FAssetHeader
{
	static constexpr uint32 ExpectedMagic = 0x54455341; // ASET
	static constexpr uint32 CurrentVersion = 1;

	uint32 Magic = ExpectedMagic;
	uint32 Version = CurrentVersion;
};

FArchive& operator<<(FArchive& Ar, FAssetHeader& Header);
