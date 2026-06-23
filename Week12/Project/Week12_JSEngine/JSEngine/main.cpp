#include "Engine/Runtime/Launch.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include <crtdbg.h>
#include <fbxsdk.h>
#include <fstream>
#include <filesystem>
#include <shellapi.h>

namespace
{
    bool HasCommandLineFlag(const wchar_t* Flag)
    {
        int Argc = 0;
        LPWSTR* Argv = CommandLineToArgvW(GetCommandLineW(), &Argc);
        if (!Argv)
        {
            return false;
        }

        bool bFound = false;
        for (int Index = 1; Index < Argc; ++Index)
        {
            if (_wcsicmp(Argv[Index], Flag) == 0)
            {
                bFound = true;
                break;
            }
        }

        LocalFree(Argv);
        return bFound;
    }

    int ImportSourceAssets(bool bImportSkeletalMeshes, bool bImportAnimations)
    {
        const std::filesystem::path LogPath = std::filesystem::path(FPaths::RootDir()) / L"Saves" / L"Logs" / L"AssetImportTool.log";
        std::filesystem::create_directories(LogPath.parent_path());
        std::ofstream Log(LogPath, std::ios::app);
        Log << "[ImportSourceAssets] Root=" << FPaths::ToUtf8(FPaths::RootDir())
            << " Skeletal=" << (bImportSkeletalMeshes ? "true" : "false")
            << " Animations=" << (bImportAnimations ? "true" : "false")
            << "\n";

        FResourceManager& ResourceManager = FResourceManager::Get();
        ResourceManager.LoadFromAssetDirectory("Asset");

        const std::filesystem::path SkeletalMeshRoot = std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"SkeletalMesh";
        std::error_code ErrorCode;
        if (!std::filesystem::exists(SkeletalMeshRoot, ErrorCode))
        {
            Log << "SkeletalMesh root missing\n";
            return 0;
        }

        int32 FbxCount = 0;
        int32 ImportedSkeletalCount = 0;
        int32 ImportedAnimationCount = 0;
        for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(SkeletalMeshRoot, ErrorCode))
        {
            if (ErrorCode)
            {
                Log << "Scan error\n";
                return 1;
            }
            if (!Entry.is_regular_file() || Entry.path().extension() != L".fbx")
            {
                continue;
            }

            ++FbxCount;
            const FString SourcePath = FPaths::ToRelativeString(Entry.path().wstring());
            FString ImportedSkeletalPath;
            if (bImportSkeletalMeshes)
            {
                ImportedSkeletalPath = ResourceManager.ImportSkeletalMeshFromSource(SourcePath);
                if (!ImportedSkeletalPath.empty())
                {
                    ++ImportedSkeletalCount;
                }
            }

            TArray<FString> ImportedAnimationPaths;
            if (bImportAnimations)
            {
                ImportedAnimationPaths = ResourceManager.ImportAnimationStacksFromFbx(SourcePath);
                ImportedAnimationCount += static_cast<int32>(ImportedAnimationPaths.size());
            }

            Log << "Source=" << SourcePath
                << " Skeletal=" << ImportedSkeletalPath
                << " Animations=" << ImportedAnimationPaths.size()
                << "\n";
        }

        Log << "FBX=" << FbxCount
            << " SkeletalImported=" << ImportedSkeletalCount
            << " AnimationImported=" << ImportedAnimationCount
            << "\n";
        return 0;
    }
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine, _In_ int nShowCmd)
{
    const bool bImportAllAssets =
        (lpCmdLine && wcsstr(lpCmdLine, L"--import-assets")) ||
        HasCommandLineFlag(L"--import-assets");
    const bool bImportSkeletalAssets =
        bImportAllAssets ||
        (lpCmdLine && wcsstr(lpCmdLine, L"--import-skeletal-assets")) ||
        HasCommandLineFlag(L"--import-skeletal-assets");
    const bool bImportAnimations =
        bImportAllAssets ||
        (lpCmdLine && wcsstr(lpCmdLine, L"--import-animations")) ||
        HasCommandLineFlag(L"--import-animations");

    if (bImportSkeletalAssets || bImportAnimations)
    {
        return ImportSourceAssets(bImportSkeletalAssets, bImportAnimations);
    }

#ifdef _MSC_VER
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // _CrtSetBreakAlloc(23304399);

	FbxManager* manager = FbxManager::Create();
    if (!manager)
    {
        OutputDebugStringA("FbxManager Creation Failed\n");
        return -1;
    }

    OutputDebugStringA("FbxManager Creation OK\n");

    FbxIOSettings* ios = FbxIOSettings::Create(manager, IOSROOT);
    manager->SetIOSettings(ios);

    OutputDebugStringA("IOSettings Creation OK\n");

    int major, minor, revision;
    FbxManager::GetFileFormatVersion(major, minor, revision);

    char buffer[128];
    sprintf_s(buffer, "FBX SDK Version: %d.%d.%d\n", major, minor, revision);
    OutputDebugStringA(buffer);

    manager->Destroy();

    OutputDebugStringA("FBX Log End\n");
#endif
#endif
	return Launch(hInstance, nShowCmd);
}
