#pragma once

#include "Core/Containers/String.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"

#include <cctype>
#include <filesystem>
#include <functional>
#include <sstream>
#include <system_error>
#include <Windows.h>

class FExternalPathStaging
{
public:
	bool PrepareFile(const FString& SourcePath, const char* Label)
	{
		Reset();

		OriginalPath = FPaths::Normalize(SourcePath);
		if (OriginalPath.empty())
		{
			return false;
		}

		const std::filesystem::path OriginalFsPath =
			std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(OriginalPath))).lexically_normal();
		OriginalAbsolutePath = OriginalFsPath;

		const FString OriginalAbsoluteText = FPaths::ToUtf8(OriginalFsPath.generic_wstring());
		if (!ContainsNonAscii(OriginalAbsoluteText))
		{
			LibraryPath = OriginalAbsoluteText;
			return true;
		}

		FString ShortPath;
		if (TryGetAsciiShortPath(OriginalFsPath, ShortPath))
		{
			LibraryPath = ShortPath;
			return true;
		}

		std::filesystem::path StagingBase = std::filesystem::path(FPaths::RootDir()) / L"Saved" / L"ImportStaging";
		std::error_code Ec;
		std::filesystem::create_directories(StagingBase, Ec);
		if (Ec)
		{
			UE_LOG_ERROR("[ExternalPathStaging] Failed to create staging directory for %s: %s",
				Label ? Label : "file",
				FPaths::ToUtf8(StagingBase.wstring()).c_str());
			return false;
		}

		const std::filesystem::path StagedFsPath = MakeStagedFilePath(StagingBase, OriginalFsPath);
		std::filesystem::create_directories(StagedFsPath.parent_path(), Ec);
		if (Ec)
		{
			return false;
		}

		std::filesystem::copy_file(
			OriginalFsPath,
			StagedFsPath,
			std::filesystem::copy_options::overwrite_existing,
			Ec);
		if (Ec)
		{
			UE_LOG_ERROR("[ExternalPathStaging] Failed to stage %s: %s",
				Label ? Label : "file",
				OriginalAbsoluteText.c_str());
			return false;
		}

		bStaged = true;
		StagedAbsolutePath = StagedFsPath;

		if (TryGetAsciiShortPath(StagedFsPath, ShortPath))
		{
			LibraryPath = ShortPath;
			return true;
		}

		LibraryPath = FPaths::ToUtf8(StagedFsPath.generic_wstring());
		if (ContainsNonAscii(LibraryPath))
		{
			UE_LOG_WARNING("[ExternalPathStaging] Staged path still contains non-ASCII characters: %s", LibraryPath.c_str());
		}
		return true;
	}

	const FString& GetLibraryPath() const { return LibraryPath; }
	const FString& GetOriginalPath() const { return OriginalPath; }
	const std::filesystem::path& GetOriginalAbsolutePath() const { return OriginalAbsolutePath; }
	const std::filesystem::path& GetStagedAbsolutePath() const { return StagedAbsolutePath; }
	bool IsStaged() const { return bStaged; }

	static bool ContainsNonAscii(const FString& Text)
	{
		for (unsigned char Ch : Text)
		{
			if (Ch > 0x7F)
			{
				return true;
			}
		}
		return false;
	}

private:
	void Reset()
	{
		OriginalPath.clear();
		LibraryPath.clear();
		OriginalAbsolutePath.clear();
		StagedAbsolutePath.clear();
		bStaged = false;
	}

	static bool TryGetAsciiShortPath(const std::filesystem::path& Path, FString& OutPath)
	{
		OutPath.clear();

		const std::wstring WidePath = Path.wstring();
		const DWORD RequiredLength = ::GetShortPathNameW(WidePath.c_str(), nullptr, 0);
		if (RequiredLength == 0)
		{
			return false;
		}

		std::wstring Buffer(RequiredLength, L'\0');
		const DWORD WrittenLength = ::GetShortPathNameW(WidePath.c_str(), Buffer.data(), RequiredLength);
		if (WrittenLength == 0 || WrittenLength >= RequiredLength)
		{
			return false;
		}

		Buffer.resize(WrittenLength);
		const FString ShortPath = FPaths::Normalize(FPaths::ToUtf8(Buffer));
		if (ShortPath.empty() || ContainsNonAscii(ShortPath))
		{
			return false;
		}

		OutPath = ShortPath;
		return true;
	}

	static FString SanitizeAsciiStem(FString Stem)
	{
		FString Result;
		Result.reserve(Stem.size());
		bool bLastWasSeparator = false;
		for (unsigned char Ch : Stem)
		{
			const bool bAllowed = std::isalnum(Ch) || Ch == '_' || Ch == '-';
			if (bAllowed)
			{
				Result.push_back(static_cast<char>(Ch));
				bLastWasSeparator = false;
			}
			else if (!Result.empty() && !bLastWasSeparator)
			{
				Result.push_back('_');
				bLastWasSeparator = true;
			}
		}
		while (!Result.empty() && Result.back() == '_')
		{
			Result.pop_back();
		}
		return Result.empty() ? "Source" : Result;
	}

	static std::filesystem::path MakeStagedFilePath(
		const std::filesystem::path& StagingBase,
		const std::filesystem::path& OriginalFsPath)
	{
		const FString OriginalText = FPaths::ToUtf8(OriginalFsPath.generic_wstring());
		const size_t Hash = std::hash<FString>{}(OriginalText);

		std::ostringstream HashStream;
		HashStream << std::hex << Hash;

		const FString Stem = SanitizeAsciiStem(FPaths::ToUtf8(OriginalFsPath.stem().wstring()));
		const FString Extension = FPaths::ToUtf8(OriginalFsPath.extension().wstring());
		const FString FileName = Stem + Extension;

		return (StagingBase / FPaths::ToWide(HashStream.str()) / FPaths::ToWide(FileName)).lexically_normal();
	}

	FString OriginalPath;
	FString LibraryPath;
	std::filesystem::path OriginalAbsolutePath;
	std::filesystem::path StagedAbsolutePath;
	bool bStaged = false;
};
