#include "Serialization/PrefabManager.h"

#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Platform/Paths.h"
#include "Serialization/SceneSaveManager.h"
#include "SimpleJSON/json.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>

namespace
{
	constexpr int PrefabVersion = 1;
	constexpr const char* PrefabType = "KraftonPrefab";

	std::filesystem::path GetProjectRootPath()
	{
		return std::filesystem::path(FPaths::RootDir()).lexically_normal();
	}

	bool IsPathInsideProject(const std::filesystem::path& Path)
	{
		const std::filesystem::path ProjectRoot = GetProjectRootPath();
		const std::filesystem::path RelPath = Path.lexically_normal().lexically_relative(ProjectRoot);
		if (RelPath.empty())
		{
			return true;
		}

		if (RelPath.is_absolute())
		{
			return false;
		}

		for (const std::filesystem::path& Part : RelPath)
		{
			if (Part == L"..")
			{
				return false;
			}
		}

		return true;
	}

	bool ResolvePrefabPath(const FString& Path, std::filesystem::path& OutPath)
	{
		if (Path.empty())
		{
			return false;
		}

		std::filesystem::path Candidate(FPaths::ToWide(Path));
		if (Candidate.extension().empty())
		{
			Candidate += FPrefabManager::PrefabExtension;
		}

		if (!Candidate.is_absolute())
		{
			if (Candidate.parent_path().empty())
			{
				Candidate = std::filesystem::path(FPrefabManager::GetPrefabDirectory()) / Candidate;
			}
			else
			{
				Candidate = GetProjectRootPath() / Candidate;
			}
		}

		Candidate = Candidate.lexically_normal();
		if (!IsPathInsideProject(Candidate))
		{
			UE_LOG("[Prefab] Rejecting path outside project root: %s", Path.c_str());
			return false;
		}

		OutPath = Candidate;
		return true;
	}

	bool LooksLikeJsonObject(const FString& Content)
	{
		for (char Ch : Content)
		{
			if (std::isspace(static_cast<unsigned char>(Ch)))
			{
				continue;
			}
			return Ch == '{';
		}

		return false;
	}
}

std::wstring FPrefabManager::GetPrefabDirectory()
{
	return (std::filesystem::path(FPaths::AssetDir()) / L"Prefab").generic_wstring();
}

bool FPrefabManager::SaveActorPrefab(AActor* Actor, const FString& Path)
{
	if (!IsValid(Actor))
	{
		return false;
	}

	std::filesystem::path PrefabPath;
	if (!ResolvePrefabPath(Path, PrefabPath))
	{
		return false;
	}

	std::error_code DirError;
	std::filesystem::create_directories(PrefabPath.parent_path(), DirError);
	if (DirError)
	{
		UE_LOG("[Prefab] Failed to create directory '%s': %s",
			FPaths::ToUtf8(PrefabPath.parent_path().wstring()).c_str(),
			DirError.message().c_str());
		return false;
	}

	json::JSON Root = json::Object();
	Root["Type"] = PrefabType;
	Root["Version"] = PrefabVersion;
	Root["SourceActorName"] = Actor->GetFName().ToString();
	Root["Actor"] = FSceneSaveManager::SerializeActorForPrefab(Actor);

	std::ofstream File(PrefabPath);
	if (!File.is_open())
	{
		UE_LOG("[Prefab] Failed to open prefab for write: %s", FPaths::ToUtf8(PrefabPath.wstring()).c_str());
		return false;
	}

	File << Root.dump();
	File.flush();
	return File.good();
}

AActor* FPrefabManager::SpawnActorFromPrefab(UWorld* World, const FString& Path)
{
	if (!IsValid(World))
	{
		return nullptr;
	}

	std::filesystem::path PrefabPath;
	if (!ResolvePrefabPath(Path, PrefabPath))
	{
		return nullptr;
	}

	std::ifstream File(PrefabPath);
	if (!File.is_open())
	{
		UE_LOG("[Prefab] Failed to open prefab: %s", FPaths::ToUtf8(PrefabPath.wstring()).c_str());
		return nullptr;
	}

	const FString FileContent((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
	if (!LooksLikeJsonObject(FileContent))
	{
		UE_LOG("[Prefab] Invalid prefab JSON: %s", FPaths::ToUtf8(PrefabPath.wstring()).c_str());
		return nullptr;
	}

	json::JSON Root = json::JSON::Load(FileContent);
	json::JSON* ActorJSON = nullptr;
	if (Root.hasKey("Actor"))
	{
		ActorJSON = &Root["Actor"];
	}
	else if (Root.hasKey("ClassName"))
	{
		ActorJSON = &Root;
	}

	if (!ActorJSON)
	{
		UE_LOG("[Prefab] Invalid prefab payload: %s", FPaths::ToUtf8(PrefabPath.wstring()).c_str());
		return nullptr;
	}

	return FSceneSaveManager::SpawnActorFromSerializedActor(World, *ActorJSON, false);
}
