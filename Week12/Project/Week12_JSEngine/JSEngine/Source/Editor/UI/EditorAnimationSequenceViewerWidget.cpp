#include "Editor/UI/EditorAnimationSequenceViewerWidget.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimNotify.h"
#include "Asset/AssetFile.h"
#include "Asset/CurveFloatAsset.h"
#include "Asset/SkeletalMesh.h"
#include "Component/SkinnedMeshComponent.h"
#include "Core/EditorResourcePaths.h"
#include "Core/Paths.h"
#include "Core/Reflection/ReflectionRegistry.h"
#include "Core/ResourceManager.h"
#include "Core/Logging/SkinningStats.h"
#include "Component/SkeletalMeshComponent.h"
#include "Editor/Animation/AnimPreviewInstance.h"
#include "Editor/Animation/DebugSkelMeshComponent.h"
#include "Editor/EditorEngine.h"
#include "Editor/EditorUtils.h"
#include "Editor/UI/EditorMainPanel.h"
#include "Editor/UI/AnimSequenceViewerContextBuilder.h"
#include "Editor/UI/EditorChromeConstants.h"
#include "Editor/Viewer/EditorViewer.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Engine/Runtime/Script/ScriptManager.h"
#include "Engine/Input/InputTypes.h"
#include "GameFramework/PrimitiveActors.h"
#include "Render/Resource/Texture.h"

#include "imgui.h"
#include "ImGui/imgui_impl_win32.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

namespace
{
FString GetBaseFileName(const FString& Path)
{
	if (Path.empty())
	{
		return "Animation Sequence";
	}

	const size_t SlashIndex = Path.find_last_of("/\\");
	return SlashIndex == FString::npos ? Path : Path.substr(SlashIndex + 1);
}

constexpr float AnimSequencerToolbarHeight = 34.0f;
constexpr float AnimSequencerRulerHeight = 30.0f;
constexpr float AnimSequencerRowHeight = 28.0f;
constexpr float AnimSequencerMinVisibleFrames = 0.05f;
constexpr float AnimSequencerMaxVisibleFrames = 100000.0f;
constexpr ImVec2 AnimToolbarIconSize(16.0f, 16.0f);

float ClampSequencerFrameRange(float Range)
{
	return std::clamp(Range, AnimSequencerMinVisibleFrames, AnimSequencerMaxVisibleFrames);
}

bool DrawAnimTimelineIconButton(
	const char* Id,
	const char* IconFileName,
	const char* FallbackLabel,
	const char* Tooltip,
	bool bSelected = false)
{
	UTexture* IconTexture = IconFileName
		? FResourceManager::Get().LoadTexture(FEditorResourcePaths::ToolIcon(IconFileName))
		: nullptr;
	ID3D11ShaderResourceView* SRV = IconTexture ? IconTexture->GetSRV() : nullptr;

	bool bPressed = false;
	if (!SRV)
	{
		bPressed = ImGui::Button(FallbackLabel);
	}
	else
	{
		if (bSelected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.40f, 0.58f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.34f, 0.49f, 0.68f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.33f, 0.48f, 1.0f));
		}

		const ImVec2 Padding = ImGui::GetStyle().FramePadding;
		const ImVec2 ButtonSize(
			AnimToolbarIconSize.x + Padding.x * 2.0f,
			ImGui::GetFrameHeight());
		bPressed = ImGui::InvisibleButton(Id, ButtonSize);
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		const bool bHovered = ImGui::IsItemHovered();
		const bool bHeld = ImGui::IsItemActive();
		const ImU32 BgColor = ImGui::GetColorU32(
			bHeld ? ImGuiCol_ButtonActive : (bHovered || bSelected ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
		ImGui::GetWindowDrawList()->AddRectFilled(Min, Max, BgColor, ImGui::GetStyle().FrameRounding);
		ImGui::GetWindowDrawList()->AddImage(
			reinterpret_cast<ImTextureID>(SRV),
			ImVec2(Min.x + Padding.x, Min.y + (ButtonSize.y - AnimToolbarIconSize.y) * 0.5f),
			ImVec2(Min.x + Padding.x + AnimToolbarIconSize.x, Min.y + (ButtonSize.y + AnimToolbarIconSize.y) * 0.5f),
			ImVec2(0.0f, 0.0f),
			ImVec2(1.0f, 1.0f),
			ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));

		if (bSelected)
		{
			ImGui::PopStyleColor(3);
		}
	}

	if (Tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("%s", Tooltip);
	}
	return bPressed;
}

float ResolveFramesPerSecond(UAnimSequenceBase* Sequence)
{
	UAnimDataModel* DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
	const float FPS = DataModel ? DataModel->GetFrameRate().AsDecimal() : 30.0f;
	return FPS > 0.0f ? FPS : 30.0f;
}

int32 ResolveFrameCount(UAnimSequenceBase* Sequence, float PlayLength, float FramesPerSecond)
{
	UAnimDataModel* DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
	if (DataModel && DataModel->GetNumberOfFrames() > 0)
	{
		return DataModel->GetNumberOfFrames();
	}

	return std::max(1, static_cast<int32>(std::floor(std::max(0.0f, PlayLength) * FramesPerSecond)) + 1);
}

int32 TimeToFrame(float TimeSeconds, float FramesPerSecond, int32 LastFrame)
{
	return std::clamp(static_cast<int32>(std::round(TimeSeconds * FramesPerSecond)), 0, LastFrame);
}

float FrameToTime(int32 Frame, float FramesPerSecond, float PlayLength)
{
	if (FramesPerSecond <= 0.0f)
	{
		return 0.0f;
	}

	return std::clamp(static_cast<float>(Frame) / FramesPerSecond, 0.0f, std::max(0.0f, PlayLength));
}

int32 DurationToFrameCount(float DurationSeconds, float FramesPerSecond, int32 LastFrame)
{
	if (DurationSeconds <= 0.0f || FramesPerSecond <= 0.0f)
	{
		return 0;
	}

	return std::clamp(std::max(1, static_cast<int32>(std::round(DurationSeconds * FramesPerSecond))), 0, LastFrame);
}

float FrameCountToDuration(int32 FrameCount, float FramesPerSecond, float PlayLength)
{
	if (FramesPerSecond <= 0.0f)
	{
		return 0.0f;
	}

	return std::clamp(static_cast<float>(std::max(0, FrameCount)) / FramesPerSecond, 0.0f, std::max(0.0f, PlayLength));
}

ImU32 WithAlpha(ImU32 Color, float Alpha)
{
	const ImU32 A = static_cast<ImU32>(std::clamp(Alpha, 0.0f, 1.0f) * 255.0f);
	return (Color & 0x00ffffff) | (A << 24);
}

void SetOpaqueBlendStateCallback(const ImDrawList*, const ImDrawCmd* Cmd)
{
	ID3D11DeviceContext* DeviceContext = static_cast<ID3D11DeviceContext*>(Cmd->UserCallbackData);
	if (!DeviceContext)
	{
		return;
	}

	const float BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	DeviceContext->OMSetBlendState(nullptr, BlendFactor, 0xffffffff);
}

void RenderAnimViewportStatsOverlay(
	const void* OverlayId,
	const ImVec2& ViewMin,
	float DeltaTime,
	const FEditorViewportState& ViewportState)
{
	if (!ViewportState.bShowStatFPS && !ViewportState.bShowStatSkinning)
	{
		return;
	}

	constexpr ImGuiWindowFlags Flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoInputs;

	ImGui::SetNextWindowPos(ImVec2(ViewMin.x + 8.0f, ViewMin.y + 32.0f), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.3f);

	char WindowId[64];
	snprintf(WindowId, sizeof(WindowId), "##AnimStatOverlay_%p", OverlayId);
	if (ImGui::Begin(WindowId, nullptr, Flags))
	{
		if (ViewportState.bShowStatFPS)
		{
			const float FPS = DeltaTime > 0.0f ? 1.0f / DeltaTime : 0.0f;
			ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "FPS: %.1f (%.2f ms)", FPS, DeltaTime * 1000.0f);
		}

		if (ViewportState.bShowStatSkinning)
		{
			if (ViewportState.bShowStatFPS)
			{
				ImGui::Separator();
			}

			ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Skinning");
			const ESkinningModeOverride SkinningOverride = USkinnedMeshComponent::GetGlobalSkinningModeOverride();
			const char* SkinningModeText = "Component";
			if (SkinningOverride == ESkinningModeOverride::CPU)
			{
				SkinningModeText = "CPU";
			}
			else if (SkinningOverride == ESkinningModeOverride::GPU)
			{
				SkinningModeText = "GPU";
			}

			const FSkinningStatsFrame& SkinningStats = FSkinningStats::Get().GetSnapshot();
			ImGui::TextColored(
				ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
				"- Mode: %s",
				SkinningModeText);
			ImGui::TextColored(
				ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
				"- CPU Frame: %.3f ms / GPU Frame: %.3f ms",
				SkinningStats.CPUFrameTimeMs,
				SkinningStats.GPUFrameTimeMs);
			ImGui::TextColored(
				ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
				"- Anim: %.3f ms / Pose: %.3f ms / CPU Skin: %.3f ms",
				SkinningStats.CPUAnimationUpdateMs,
				SkinningStats.CPUPoseBuildMs,
				SkinningStats.CPUSkinningMs);
			ImGui::TextColored(
				ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
				"- GPU Bone Upload: %.3f ms / %llu matrices / %llu bytes",
				SkinningStats.GPUBoneMatrixUploadMs,
				static_cast<unsigned long long>(SkinningStats.GPUBoneMatrixUploadCount),
				static_cast<unsigned long long>(SkinningStats.GPUBoneMatrixUploadBytes));
			ImGui::TextColored(
				ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
				"- CPU VB Upload: %.3f ms / %llu calls / %llu bytes",
				SkinningStats.CPUSkinnedVertexBufferUploadMs,
				static_cast<unsigned long long>(SkinningStats.CPUSkinnedVertexBufferUploadCallCount),
				static_cast<unsigned long long>(SkinningStats.CPUSkinnedVertexBufferUploadBytes));
			ImGui::TextColored(
				ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
				"- Visible: %u meshes (%u CPU / %u GPU), %llu verts",
				SkinningStats.VisibleSkinnedMeshCount,
				SkinningStats.VisibleCPUSkinnedMeshCount,
				SkinningStats.VisibleGPUSkinnedMeshCount,
				static_cast<unsigned long long>(SkinningStats.VisibleSkinnedVertexCount));
			ImGui::TextColored(
				ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
				"- GPU Draws: %u / Work: %.0f influences / Avg Influence: %.2f",
				SkinningStats.GPUSkinnedDrawPassCount,
				SkinningStats.EstimatedGPUSkinningInfluenceWork,
				SkinningStats.GetAvgBoneInfluencePerVertex());
		}
	}
	ImGui::End();
}

bool UsesAbsoluteImGuiCoordinates()
{
	return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
}

POINT ImGuiScreenToClientPoint(FWindowsWindow* Window, const ImVec2& Point)
{
	POINT Result =
	{
		static_cast<LONG>(std::lround(Point.x)),
		static_cast<LONG>(std::lround(Point.y))
	};
	if (Window && Window->GetHWND() && UsesAbsoluteImGuiCoordinates())
	{
		::ScreenToClient(Window->GetHWND(), &Result);
	}
	return Result;
}

FString GetBaseFileNameWithoutExtension(const FString& Path)
{
	if (Path.empty())
	{
		return "Animation Sequence";
	}

	const size_t SlashPos = Path.find_last_of("/\\");
	const size_t NameBegin = SlashPos == FString::npos ? 0 : SlashPos + 1;
	FString Name = Path.substr(NameBegin);

	const size_t DotPos = Name.find_last_of('.');
	if (DotPos != FString::npos && DotPos > 0)
	{
		Name = Name.substr(0, DotPos);
	}

	return Name.empty() ? "Animation Sequence" : Name;
}

FString GetAnimationViewerAssetLabel(FEditorViewer* Viewer)
{
	return "Anim: " + (Viewer ? GetBaseFileNameWithoutExtension(Viewer->GetFileName()) : FString("Animation Sequence"));
}

TArray<const UClass*> GetRegisteredNotifyTypes()
{
	TArray<const UClass*> NotifyTypes;
	TArray<UClass*> RegisteredTypes;
	FReflectionRegistry::Get().GetClassesDerivedFrom(UAnimNotify::StaticClass(), RegisteredTypes);
	for (const UClass* Class : RegisteredTypes)
	{
		if (Class
			&& Class != UAnimNotify::StaticClass()
			&& Class != UAnimNotifyState::StaticClass()
			&& !Class->HasAnyClassFlags(CF_Abstract)
			&& Class->IsChildOf(UAnimNotify::StaticClass()))
		{
			NotifyTypes.push_back(Class);
		}
	}

	std::sort(
		NotifyTypes.begin(),
		NotifyTypes.end(),
		[](const UClass* A, const UClass* B)
		{
			const char* AName = A ? A->GetDisplayName() : "";
			const char* BName = B ? B->GetDisplayName() : "";
			return std::strcmp(AName, BName) < 0;
		});
	return NotifyTypes;
}

FString GetAnimNotifyClassDisplayName(const FString& ClassName)
{
	if (UClass* Class = FReflectionRegistry::Get().FindClass(ClassName))
	{
		return Class->GetDisplayName();
	}
	return ClassName.empty() ? FString("None / Name Only") : ClassName;
}

bool DrawAnimNotifyClassCombo(const char* Label, FString& InOutClassName)
{
	const TArray<const UClass*> NotifyTypes = GetRegisteredNotifyTypes();
	bool bChanged = false;
	const FString Preview = GetAnimNotifyClassDisplayName(InOutClassName);
	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::BeginCombo(Label, Preview.c_str()))
	{
		const bool bNoneSelected = InOutClassName.empty();
		if (ImGui::Selectable("None / Name Only", bNoneSelected))
		{
			InOutClassName.clear();
			bChanged = true;
		}
		if (bNoneSelected)
		{
			ImGui::SetItemDefaultFocus();
		}

		if (!NotifyTypes.empty())
		{
			ImGui::Separator();
		}

		for (const UClass* Class : NotifyTypes)
		{
			if (!Class)
			{
				continue;
			}

			const FString ClassName = Class->GetName();
			const bool bSelected = ClassName == InOutClassName;
			const FString ItemLabel = FString(Class->GetDisplayName()) + "##" + ClassName;
			if (ImGui::Selectable(ItemLabel.c_str(), bSelected))
			{
				InOutClassName = ClassName;
				bChanged = true;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	return bChanged;
}

bool IsLuaAnimNotifyClass(const FString& ClassName)
{
	return ClassName == UAnimNotify_LuaEvent::StaticClass()->GetName()
		|| ClassName == UAnimNotifyState_LuaEvent::StaticClass()->GetName();
}

const char* GetLuaAnimNotifyTargetPolicyLabel(int32 Policy)
{
	switch (static_cast<EAnimNotifyLuaTargetPolicy>(Policy))
	{
	case EAnimNotifyLuaTargetPolicy::NamedScript:
		return "Named Script";
	case EAnimNotifyLuaTargetPolicy::AllOwnerScripts:
		return "All Owner Scripts";
	case EAnimNotifyLuaTargetPolicy::OwnerScript:
	default:
		return "Owner Script";
	}
}

FString MakeScriptReferenceFromPath(const FString& PathText)
{
	if (PathText.empty())
	{
		return {};
	}

	std::filesystem::path ScriptPath(FPaths::ToWide(PathText));
	if (ScriptPath.is_absolute())
	{
		return FPaths::ToRelativeString(ScriptPath.lexically_normal().wstring());
	}
	return FPaths::Normalize(PathText);
}

FString GetLuaScriptDisplayName(const FString& ScriptReference)
{
	if (ScriptReference.empty())
	{
		return "<None>";
	}

	std::filesystem::path ScriptPath(FPaths::ToWide(ScriptReference));
	if (ScriptPath.has_filename())
	{
		return FPaths::ToUtf8(ScriptPath.filename().generic_wstring());
	}

	return ScriptReference;
}

bool DrawLuaScriptCombo(const char* Label, FString& Value)
{
	bool bChanged = false;
	const FString Preview = GetLuaScriptDisplayName(Value);
	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::BeginCombo(Label, Preview.c_str()))
	{
		FScriptManager& ScriptManager = FScriptManager::Get();
		ScriptManager.RefreshLuaScriptFiles();

		TArray<FString> ScriptReferences;
		for (const auto& Pair : ScriptManager.GetScriptArray())
		{
			const FLuaScriptInfo& Info = Pair.second;
			ScriptReferences.push_back(!Info.ScriptPath.empty()
				? MakeScriptReferenceFromPath(FPaths::ToUtf8(Info.ScriptPath))
				: Pair.first.ToString());
		}

		std::sort(ScriptReferences.begin(), ScriptReferences.end());
		ScriptReferences.erase(std::unique(ScriptReferences.begin(), ScriptReferences.end()), ScriptReferences.end());

		const bool bNoneSelected = Value.empty();
		if (ImGui::Selectable("<None>", bNoneSelected))
		{
			Value.clear();
			bChanged = true;
		}
		if (bNoneSelected)
		{
			ImGui::SetItemDefaultFocus();
		}

		for (const FString& ScriptReference : ScriptReferences)
		{
			const FString DisplayName = GetLuaScriptDisplayName(ScriptReference);
			const bool bSelected = Value == ScriptReference || GetLuaScriptDisplayName(Value) == DisplayName;
			if (ImGui::Selectable(DisplayName.c_str(), bSelected))
			{
				Value = ScriptReference;
				bChanged = true;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}

	return bChanged;
}

FString MakeLuaFunctionSuffix(const FString& EventName)
{
	FString Suffix;
	Suffix.reserve(EventName.size());
	for (unsigned char Ch : EventName)
	{
		Suffix.push_back((std::isalnum(Ch) || Ch == '_') ? static_cast<char>(Ch) : '_');
	}
	return Suffix.empty() ? FString("AnimNotify") : Suffix;
}

FString ResolveLuaScriptClassName(const FString& ScriptPathText, const FString& ScriptReference)
{
	std::filesystem::path ScriptPath(FPaths::ToWide(ScriptPathText.empty() ? ScriptReference : ScriptPathText));
	FString ClassName = FPaths::ToUtf8(ScriptPath.stem().generic_wstring());
	return ClassName.empty() ? FString("Script") : MakeLuaFunctionSuffix(ClassName);
}

FString ResolveLuaScriptClassName(const FString& Source, const FString& ScriptPathText, const FString& ScriptReference)
{
	const FString ReturnKeyword = "return";
	size_t Pos = Source.rfind(ReturnKeyword);
	while (Pos != FString::npos)
	{
		const bool bAtTokenStart = Pos == 0 || std::isspace(static_cast<unsigned char>(Source[Pos - 1]));
		size_t Cursor = Pos + ReturnKeyword.size();
		const bool bAtTokenEnd = Cursor >= Source.size() || std::isspace(static_cast<unsigned char>(Source[Cursor]));
		if (bAtTokenStart && bAtTokenEnd)
		{
			while (Cursor < Source.size() && std::isspace(static_cast<unsigned char>(Source[Cursor])))
			{
				++Cursor;
			}

			FString ClassName;
			while (Cursor < Source.size())
			{
				const unsigned char Ch = static_cast<unsigned char>(Source[Cursor]);
				if (!std::isalnum(Ch) && Ch != '_')
				{
					break;
				}
				ClassName.push_back(static_cast<char>(Ch));
				++Cursor;
			}

			if (!ClassName.empty())
			{
				return ClassName;
			}
		}

		if (Pos == 0)
		{
			break;
		}
		Pos = Source.rfind(ReturnKeyword, Pos - 1);
	}

	return ResolveLuaScriptClassName(ScriptPathText, ScriptReference);
}

bool ReadTextFile(const FString& PathText, FString& OutText)
{
	std::ifstream File(std::filesystem::path(FPaths::ToWide(PathText)), std::ios::binary);
	if (!File.is_open())
	{
		return false;
	}

	std::ostringstream Stream;
	Stream << File.rdbuf();
	OutText = Stream.str();
	return true;
}

bool WriteTextFile(const FString& PathText, const FString& Text)
{
	std::ofstream File(std::filesystem::path(FPaths::ToWide(PathText)), std::ios::binary | std::ios::trunc);
	if (!File.is_open())
	{
		return false;
	}

	File << Text;
	return File.good();
}

size_t FindLuaReturnInsertionPoint(const FString& Source, const FString& ScriptClassName)
{
	const FString ExactReturn = "return " + ScriptClassName;
	size_t Pos = Source.rfind(ExactReturn);
	if (Pos != FString::npos)
	{
		return Pos;
	}

	Pos = Source.rfind("\nreturn ");
	if (Pos != FString::npos)
	{
		return Pos + 1;
	}

	Pos = Source.rfind("\r\nreturn ");
	if (Pos != FString::npos)
	{
		return Pos + 2;
	}

	return Source.size();
}

FString InsertLuaHandlerStubBeforeReturn(const FString& Source, const FString& Stub, const FString& ScriptClassName)
{
	const size_t InsertPos = FindLuaReturnInsertionPoint(Source, ScriptClassName);
	FString Result = Source.substr(0, InsertPos);
	if (!Result.empty() && Result.back() != '\n' && Result.back() != '\r')
	{
		Result += "\n";
	}
	Result += Stub;
	if (InsertPos < Source.size() && !Result.empty() && Result.back() != '\n')
	{
		Result += "\n";
	}
	Result += Source.substr(InsertPos);
	return Result;
}

FString BuildLuaAnimNotifyHandlerStub(const FString& ScriptClassName, const FString& EventName, bool bStateNotify)
{
	const FString Suffix = MakeLuaFunctionSuffix(EventName);
	FString Stub = "\n";
	Stub += "-- Anim notify handler generated by the editor.\n";
	if (!bStateNotify)
	{
		Stub += "function " + ScriptClassName + ":AnimNotify_" + Suffix + "(context)\n";
		Stub += "end\n";
		return Stub;
	}

	Stub += "function " + ScriptClassName + ":AnimNotify_" + Suffix + "_Begin(context)\n";
	Stub += "end\n\n";
	Stub += "function " + ScriptClassName + ":AnimNotify_" + Suffix + "_Tick(context)\n";
	Stub += "end\n\n";
	Stub += "function " + ScriptClassName + ":AnimNotify_" + Suffix + "_End(context)\n";
	Stub += "end\n";
	return Stub;
}

bool LuaAnimNotifyHandlerExists(const FString& Source, const FString& ScriptClassName, const FString& EventName, bool bStateNotify)
{
	const FString Suffix = MakeLuaFunctionSuffix(EventName);
	const FString BaseName = bStateNotify ? ("AnimNotify_" + Suffix + "_Begin") : ("AnimNotify_" + Suffix);
	return Source.find("function " + ScriptClassName + ":" + BaseName) != FString::npos
		|| Source.find(BaseName) != FString::npos;
}

bool AddLuaAnimNotifyHandlerStub(
	const FAnimNotifyStateEvent& Notify,
	FString& InOutTargetScript,
	FString& OutMessage,
	bool* bOutAlreadyExists = nullptr)
{
	if (bOutAlreadyExists)
	{
		*bOutAlreadyExists = false;
	}

	const FString EventName = !Notify.LuaEventName.empty() ? Notify.LuaEventName : Notify.NotifyName.ToString();
	if (EventName.empty())
	{
		OutMessage = "Lua event name is empty.";
		return false;
	}

	if (InOutTargetScript.empty())
	{
		OutMessage = "Select a Lua script first.";
		return false;
	}

	FString ScriptPath;
	if (!FScriptManager::Get().ResolveScriptPath(InOutTargetScript, ScriptPath))
	{
		OutMessage = "Lua script file not found.";
		return false;
	}

	FString Source;
	if (!ReadTextFile(ScriptPath, Source))
	{
		OutMessage = "Failed to read Lua script.";
		return false;
	}

	const FString ScriptClassName = ResolveLuaScriptClassName(Source, ScriptPath, InOutTargetScript);
	const bool bStateNotify = Notify.NotifyClassName == UAnimNotifyState_LuaEvent::StaticClass()->GetName();
	if (LuaAnimNotifyHandlerExists(Source, ScriptClassName, EventName, bStateNotify))
	{
		if (bOutAlreadyExists)
		{
			*bOutAlreadyExists = true;
		}
		OutMessage = "Lua notify handler already exists.";
		return false;
	}

	const FString Stub = BuildLuaAnimNotifyHandlerStub(ScriptClassName, EventName, bStateNotify);
	const FString UpdatedSource = InsertLuaHandlerStubBeforeReturn(Source, Stub, ScriptClassName);
	if (!WriteTextFile(ScriptPath, UpdatedSource))
	{
		OutMessage = "Failed to write Lua notify handler.";
		return false;
	}

	InOutTargetScript = MakeScriptReferenceFromPath(ScriptPath);
	FScriptManager::Get().RefreshLuaScriptFiles();
	OutMessage = "Lua notify handler added.";
	return true;
}

void ApplyAnimDetachedDocumentWindowClass()
{
	ImGuiWindowClass WindowClass;
	WindowClass.ClassId = 0x4A534153u; // "JSAS" - animation sequence detached document window class
	WindowClass.ViewportFlagsOverrideSet =
		ImGuiViewportFlags_NoAutoMerge |
		ImGuiViewportFlags_NoDecoration;
	WindowClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoTaskBarIcon;
	ImGui::SetNextWindowClass(&WindowClass);
}

HWND GetCurrentViewportHwnd()
{
	ImGuiViewport* Viewport = ImGui::GetWindowViewport();
	if (!Viewport)
	{
		return nullptr;
	}
	return static_cast<HWND>(Viewport->PlatformHandleRaw ? Viewport->PlatformHandleRaw : Viewport->PlatformHandle);
}

ImGui_ImplWin32_CustomChromeRect MakeChromeRect(const ImVec2& Min, const ImVec2& Max, const ImVec2& WindowPos)
{
	return ImGui_ImplWin32_CustomChromeRect{
		static_cast<int>(Min.x - WindowPos.x),
		static_cast<int>(Min.y - WindowPos.y),
		static_cast<int>(Max.x - WindowPos.x),
		static_cast<int>(Max.y - WindowPos.y)
	};
}

void AddChromeRect(ImGui_ImplWin32_CustomChromeRect* Rects, int& Count, const ImVec2& Min, const ImVec2& Max, const ImVec2& WindowPos)
{
	if (Count >= 16)
	{
		return;
	}
	Rects[Count++] = MakeChromeRect(Min, Max, WindowPos);
}

bool IsViewportMaximized(HWND Hwnd)
{
	return Hwnd && ::IsZoomed(Hwnd) != FALSE;
}

void ToggleViewportMaximize(HWND Hwnd)
{
	if (!Hwnd)
	{
		return;
	}
	::PostMessageW(Hwnd, WM_SYSCOMMAND, IsViewportMaximized(Hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
}

bool DrawDetachedWindowButton(
	const char* Id,
	const char* Tooltip,
	const ImVec2& Size,
	const ImVec4& HoverColor,
	const ImVec4& ActiveColor,
	const std::function<void(ImDrawList*, const ImVec2&, const ImVec2&, ImU32)>& DrawIcon)
{
	ImGui::PushID(Id);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HoverColor);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ActiveColor);

	const bool bClicked = ImGui::InvisibleButton("##Button", Size);
	const bool bHovered = ImGui::IsItemHovered();
	const bool bActive = ImGui::IsItemActive();
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	const ImU32 BgColor = ImGui::GetColorU32(
		bActive ? ActiveColor : (bHovered ? HoverColor : ImVec4(0.0f, 0.0f, 0.0f, 0.0f)));

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(Min, Max, BgColor, 0.0f);
	DrawIcon(DrawList, Min, Max, ImGui::GetColorU32(ImVec4(0.82f, 0.85f, 0.90f, 1.0f)));

	if (bHovered && Tooltip)
	{
		ImGui::SetTooltip("%s", Tooltip);
	}

	ImGui::PopStyleColor(3);
	ImGui::PopID();
	return bClicked;
}
}

void FEditorAnimationSequenceViewerWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	CurveEditorWidget.Initialize(InEditorEngine);
}

void FEditorAnimationSequenceViewerWidget::SetViewer(FEditorViewer* InViewer)
{
	Viewer = InViewer;
	if (Viewer)
	{
		Viewer->GetClient().GetShowFlags().bShowSkeletalMesh = true;
	}
}

void FEditorAnimationSequenceViewerWidget::SetContext(const FAnimSequenceViewerContext& InContext)
{
	AssetPath = InContext.AssetPath;
	TargetSkeletalMeshPath = InContext.TargetSkeletalMeshPath;
	AnimSequence = InContext.AnimSequence;
	PreviewTime = 0.0f;
	bPreviewPlaying = AnimSequence != nullptr;
	SelectedNotifyTrackIndex = -1;
	SelectedNotifyIndex = -1;
	SelectedNotifyNameBuffer[0] = '\0';
	SelectedNotifyNameBufferTrackIndex = -1;
	SelectedNotifyNameBufferNotifyIndex = -1;
	SelectedNotifyLuaEventNameBuffer[0] = '\0';
	SelectedNotifyLuaTargetScriptBuffer[0] = '\0';
	SelectedNotifyLuaBufferTrackIndex = -1;
	SelectedNotifyLuaBufferNotifyIndex = -1;
	SelectedTrackIndex = -1;
	ContextNotifyTrackIndex = -1;
	ContextNotifyIndex = -1;
	ContextNotifyFrame = 0;
	bDraggingNotify = false;
	bDraggingNotifyDirty = false;
	DraggingNotifyTrackIndex = -1;
	DraggingNotifyIndex = -1;
	DraggingNotifyMode = 0;
	DraggingNotifyGrabFrameOffset = 0;
	SelectedCurveTrackIndex = -1;
	ContextCurveTrackIndex = -1;
	ViewStartFrame = 0.0f;
	ViewEndFrame = 30.0f;
	bTimelineViewInitialized = false;
	ReleasePreviewInstance();
	CurveEditorWidget.Close();
	if (Viewer)
	{
		Viewer->GetClient().GetShowFlags().bShowSkeletalMesh = true;
	}
}

FString FEditorAnimationSequenceViewerWidget::GetWindowName() const
{
	char WindowName[80];
	sprintf_s(WindowName, "###AnimationSequenceViewer_%p", Viewer);
	return "Animation Sequence - " + GetBaseFileName(Viewer ? Viewer->GetFileName() : FString()) + WindowName;
}

void FEditorAnimationSequenceViewerWidget::Render(float DeltaTime)
{
	if (!bOpen || !Viewer)
	{
		return;
	}

	const float TitleBarFramePaddingY = std::max(
		0.0f,
		(FEditorChromeMetrics::ApplicationTitleBarHeight - ImGui::GetFontSize()) * 0.5f);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(13.0f, TitleBarFramePaddingY));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f, 4.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.060f, 0.072f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.055f, 0.060f, 0.072f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.20f, 0.25f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.15f, 0.17f, 0.22f, 1.0f));

	bool bDockRequested = false;
	bool bCloseRequested = false;

	ApplyAnimDetachedDocumentWindowClass();
	ImGui::SetNextWindowSize(ImVec2(1060, 680), ImGuiCond_FirstUseEver);
	if (const ImGuiViewport* MainViewport = ImGui::GetMainViewport())
	{
		ImGui::SetNextWindowPos(
			ImVec2(MainViewport->Pos.x + 140.0f, MainViewport->Pos.y + 110.0f),
			ImGuiCond_FirstUseEver);
	}

	constexpr ImGuiWindowFlags WindowFlags =
		ImGuiWindowFlags_MenuBar |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;

	if (ImGui::Begin(GetWindowName().c_str(), &bOpen, WindowFlags))
	{
		RenderDetachedDocumentChrome(bDockRequested, bCloseRequested);
		RenderDetachedDocumentToolbar(bDockRequested);
		RenderContent(DeltaTime);
	}
	ImGui::End();

	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar(5);

	if (bDockRequested && EditorEngine)
	{
		EditorEngine->GetMainPanel().RequestDockViewer(Viewer);
		return;
	}

	if (bCloseRequested)
	{
		bOpen = false;
		CurveEditorWidget.Close();
	}

	if (!bOpen && EditorEngine && Viewer)
	{
		FEditorViewer* ViewerToRemove = Viewer;
		CurveEditorWidget.Close();
		EditorEngine->RemoveViewer(ViewerToRemove);
		return;
	}

	CurveEditorWidget.Render(DeltaTime);
}

void FEditorAnimationSequenceViewerWidget::RenderDetachedDocumentChrome(bool& bDockRequested, bool& bCloseRequested)
{
	if (!Viewer || !ImGui::BeginMenuBar())
	{
		return;
	}

	constexpr float WindowButtonWidth = 48.0f;
	constexpr float TitleBarHeight = FEditorChromeMetrics::ApplicationTitleBarHeight;
	constexpr float MenuStartX = 0.0f;

	HWND ViewportHwnd = GetCurrentViewportHwnd();
	const ImVec2 WindowPos = ImGui::GetWindowPos();
	const ImVec2 WindowSize = ImGui::GetWindowSize();
	const float ButtonStartX = std::max(0.0f, WindowSize.x - WindowButtonWidth * 3.0f);

	ImGui_ImplWin32_CustomChromeRect ChromeRects[16] = {};
	int ChromeRectCount = 0;

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
	const float TitleBarFramePaddingY = std::max(
		0.0f,
		(TitleBarHeight - ImGui::GetFontSize()) * 0.5f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, TitleBarFramePaddingY));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 8.0f));

	ImGui::SetCursorPos(ImVec2(MenuStartX, 0.0f));

	if (ImGui::BeginMenu("File"))
	{
		const bool bAssetAvailable = AnimSequence && AnimSequence->GetDataModel();
		if (ImGui::MenuItem("Save Animation", "Ctrl+S", false, bAssetAvailable))
		{
			SaveAnimationAsset();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Close"))
		{
			bCloseRequested = true;
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit"))
	{
		ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
		ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, false);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Asset"))
	{
		ImGui::TextDisabled("Animation Sequence");
		if (Viewer)
		{
			ImGui::Separator();
			ImGui::TextDisabled("%s", Viewer->GetFileName().c_str());
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Window"))
	{
		if (ImGui::MenuItem("Dock Back"))
		{
			bDockRequested = true;
		}
		if (ImGui::MenuItem("Close"))
		{
			bCloseRequested = true;
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Tools"))
	{
		FSkeletalViewerShowFlags& ShowFlags = Viewer->GetClient().GetShowFlags();
		ImGui::MenuItem("Skeletal Mesh", nullptr, &ShowFlags.bShowSkeletalMesh);
		ImGui::MenuItem("Bones", nullptr, &ShowFlags.bShowBones);
		ImGui::MenuItem("Bone Weight Heatmap", nullptr, &ShowFlags.bShowBoneWeightHeatmap);
		ImGui::MenuItem("Bounding Box", nullptr, &ShowFlags.bShowBoundingBox);
		ImGui::MenuItem("Outline", nullptr, &ShowFlags.bShowOutline);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Settings"))
	{
		FEditorMainPanel& MainPanel = EditorEngine->GetMainPanel();
		if (ImGui::MenuItem("Project Settings"))
		{
			MainPanel.OpenProjectSettingsPanel();
		}
		if (ImGui::MenuItem("World Settings"))
		{
			MainPanel.OpenWorldSettingsPanel();
		}
		if (ImGui::MenuItem("Editor Settings"))
		{
			MainPanel.OpenEditorSettingsPanel();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Help"))
	{
		ImGui::TextDisabled("Animation Sequence Viewer");
		ImGui::EndMenu();
	}

	const float MenuEndX = std::min(ButtonStartX, ImGui::GetCursorScreenPos().x - WindowPos.x + 8.0f);
	AddChromeRect(
		ChromeRects,
		ChromeRectCount,
		ImVec2(WindowPos.x, WindowPos.y),
		ImVec2(WindowPos.x + MenuEndX, WindowPos.y + TitleBarHeight),
		WindowPos);

	const FString AssetLabel = GetAnimationViewerAssetLabel(Viewer);
	const ImVec2 TitleSize = ImGui::CalcTextSize(AssetLabel.c_str());
	const float TitleX = std::clamp(
		MenuEndX + (ButtonStartX - MenuEndX - TitleSize.x) * 0.5f,
		MenuEndX + 8.0f,
		std::max(MenuEndX + 8.0f, ButtonStartX - TitleSize.x - 8.0f));
	DrawList->AddText(
		ImVec2(WindowPos.x + TitleX, WindowPos.y + (TitleBarHeight - TitleSize.y) * 0.5f),
		ImGui::GetColorU32(ImVec4(0.72f, 0.76f, 0.84f, 1.0f)),
		AssetLabel.c_str());

	const ImVec2 ButtonSize(WindowButtonWidth, TitleBarHeight);
	ImGui::SetCursorPos(ImVec2(ButtonStartX, 0.0f));
	if (DrawDetachedWindowButton(
		"AnimDetachedMinimize",
		"Minimize",
		ButtonSize,
		ImVec4(0.14f, 0.16f, 0.20f, 1.0f),
		ImVec4(0.18f, 0.20f, 0.25f, 1.0f),
		[](ImDrawList* InDrawList, const ImVec2& Min, const ImVec2& Max, ImU32 Color)
		{
			const float Y = (Min.y + Max.y) * 0.5f + 4.0f;
			InDrawList->AddLine(ImVec2(Min.x + 17.0f, Y), ImVec2(Max.x - 17.0f, Y), Color, 1.6f);
		}))
	{
		if (ViewportHwnd)
		{
			::PostMessageW(ViewportHwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
		}
	}
	AddChromeRect(ChromeRects, ChromeRectCount, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), WindowPos);

	ImGui::SameLine(0.0f, 0.0f);
	if (DrawDetachedWindowButton(
		"AnimDetachedMaximize",
		IsViewportMaximized(ViewportHwnd) ? "Restore" : "Maximize",
		ButtonSize,
		ImVec4(0.14f, 0.16f, 0.20f, 1.0f),
		ImVec4(0.18f, 0.20f, 0.25f, 1.0f),
		[ViewportHwnd](ImDrawList* InDrawList, const ImVec2& Min, const ImVec2& Max, ImU32 Color)
		{
			const bool bMaximized = IsViewportMaximized(ViewportHwnd);
			const ImVec2 A(Min.x + 17.0f, Min.y + 12.0f);
			const ImVec2 B(Max.x - 17.0f, Max.y - 12.0f);
			if (bMaximized)
			{
				InDrawList->AddRect(ImVec2(A.x + 3.0f, A.y), ImVec2(B.x + 3.0f, B.y - 3.0f), Color, 0.0f, 0, 1.4f);
				InDrawList->AddRect(ImVec2(A.x, A.y + 3.0f), ImVec2(B.x, B.y), Color, 0.0f, 0, 1.4f);
			}
			else
			{
				InDrawList->AddRect(A, B, Color, 0.0f, 0, 1.4f);
			}
		}))
	{
		ToggleViewportMaximize(ViewportHwnd);
	}
	AddChromeRect(ChromeRects, ChromeRectCount, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), WindowPos);

	ImGui::SameLine(0.0f, 0.0f);
	if (DrawDetachedWindowButton(
		"AnimDetachedClose",
		"Close",
		ButtonSize,
		ImVec4(0.62f, 0.18f, 0.20f, 1.0f),
		ImVec4(0.46f, 0.10f, 0.13f, 1.0f),
		[](ImDrawList* InDrawList, const ImVec2& Min, const ImVec2& Max, ImU32 Color)
		{
			InDrawList->AddLine(ImVec2(Min.x + 17.0f, Min.y + 12.0f), ImVec2(Max.x - 17.0f, Max.y - 12.0f), Color, 1.6f);
			InDrawList->AddLine(ImVec2(Max.x - 17.0f, Min.y + 12.0f), ImVec2(Min.x + 17.0f, Max.y - 12.0f), Color, 1.6f);
		}))
	{
		bCloseRequested = true;
	}
	AddChromeRect(ChromeRects, ChromeRectCount, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), WindowPos);

	ImGui_ImplWin32_SetCustomChrome(ViewportHwnd, static_cast<int>(TitleBarHeight), ChromeRects, ChromeRectCount);
	ImGui::PopStyleVar(3);
	ImGui::EndMenuBar();
}

void FEditorAnimationSequenceViewerWidget::RenderDetachedDocumentToolbar(bool& bDockRequested)
{
	if (!Viewer || !EditorEngine)
	{
		return;
	}

	constexpr ImGuiWindowFlags ToolbarFlags =
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::BeginChild("##DetachedAnimSequenceToolbar", ImVec2(0.0f, 40.0f), false, ToolbarFlags);
	ImGui::SetCursorPos(ImVec2(8.0f, 6.0f));
	if (ImGui::Button("Dock"))
	{
		bDockRequested = true;
	}
	ImGui::SameLine(0.0f, 12.0f);
	EditorEngine->GetMainPanel().RenderViewerToolbarControls(Viewer);
	ImGui::EndChild();
}

void FEditorAnimationSequenceViewerWidget::RenderEmbedded(float DeltaTime)
{
	if (!Viewer)
	{
		ImGui::TextDisabled("Animation sequence viewer target is not available.");
		return;
	}

	RenderContent(DeltaTime);
	CurveEditorWidget.Render(DeltaTime);
}

void FEditorAnimationSequenceViewerWidget::RenderContent(float DeltaTime)
{
	UDebugSkelMeshComponent* SkelComp = nullptr;
	FSkeletalMesh* MeshData = ResolveCurrentMeshData(&SkelComp);
	const bool bAssetAvailable = AnimSequence && AnimSequence->GetDataModel();
	if (MeshData != CachedMesh)
	{
		CachedMesh = MeshData;
		RebuildBoneTreeCaches(MeshData);
	}

	const ImVec2 FullSize = ImGui::GetContentRegionAvail();
	const float SplitterWidth = 2.0f;
	const float CenterWidth = std::max(
		220.0f,
		FullSize.x - LeftPanelWidth - RightPanelWidth - ImGui::GetStyle().ItemSpacing.x * 4.0f - SplitterWidth * 2.0f);

	ImGui::BeginChild("AnimSkeletonPanel", ImVec2(LeftPanelWidth, 0.0f), true);
	if (!bAssetAvailable)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.20f, 1.0f), "Animation asset is missing.");
		ImGui::TextWrapped("%s", AssetPath.c_str());
		ImGui::Separator();
	}
	RenderSkeletonPanel(SkelComp);
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::Button("##AnimLeftSplitter", ImVec2(SplitterWidth, -1.0f));
	if (ImGui::IsItemActive())
	{
		LeftPanelWidth += ImGui::GetIO().MouseDelta.x;
		LeftPanelWidth = std::clamp(LeftPanelWidth, 140.0f, FullSize.x * 0.45f);
	}

	ImGui::SameLine();
	ImGui::BeginChild("AnimCenterPanel", ImVec2(CenterWidth, 0.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	RenderViewportPanel(DeltaTime);
	ImGui::Button("##AnimTimelineHeightSplitter", ImVec2(-1.0f, 2.0f));
	if (ImGui::IsItemActive())
	{
		TimelineHeight -= ImGui::GetIO().MouseDelta.y;
		TimelineHeight = std::clamp(TimelineHeight, 120.0f, std::max(120.0f, FullSize.y - 160.0f));
	}
	RenderTimelinePanel(SkelComp, DeltaTime);
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::Button("##AnimRightSplitter", ImVec2(SplitterWidth, -1.0f));
	if (ImGui::IsItemActive())
	{
		RightPanelWidth -= ImGui::GetIO().MouseDelta.x;
		RightPanelWidth = std::clamp(RightPanelWidth, 160.0f, FullSize.x * 0.45f);
	}

	ImGui::SameLine();
	ImGui::BeginChild("AnimDetailsPanel", ImVec2(RightPanelWidth, 0.0f), true);
	RenderDetailsPanel(SkelComp);
	ImGui::EndChild();
}

void FEditorAnimationSequenceViewerWidget::RenderSkeletonPanel(USkeletalMeshComponent* SkelComp)
{
	ImGui::TextUnformatted("Bone Tree");
	ImGui::Separator();

	FSkeletalMesh* MeshData = SkelComp && SkelComp->GetSkeletalMesh()
		? SkelComp->GetSkeletalMesh()->GetMeshData()
		: nullptr;
	if (!MeshData || MeshData->Bones.empty())
	{
		ImGui::TextDisabled("No skeletal mesh loaded.");
		return;
	}

	if (ImGui::Button("Reset Pose"))
	{
		SkelComp->ResetToBindPose();
	}
	ImGui::SameLine();
	FSkeletalViewerShowFlags& ShowFlags = Viewer->GetClient().GetShowFlags();
	ImGui::Checkbox("Mesh", &ShowFlags.bShowSkeletalMesh);
	ImGui::SameLine();
	ImGui::Checkbox("Bones", &ShowFlags.bShowBones);
	ImGui::SameLine();
	ImGui::Checkbox("Weights", &ShowFlags.bShowBoneWeightHeatmap);

	ImGui::Separator();
	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshData->Bones.size()); ++BoneIndex)
	{
		if (MeshData->Bones[BoneIndex].ParentIndex < 0)
		{
			DrawBoneNode(BoneIndex, MeshData->Bones, Children);
		}
	}
}

void FEditorAnimationSequenceViewerWidget::RenderViewportPanel(float DeltaTime)
{
	const float ViewportHeight = std::max(180.0f, ImGui::GetContentRegionAvail().y - TimelineHeight - ImGui::GetStyle().ItemSpacing.y);
	ImGui::BeginChild("AnimViewportHost", ImVec2(0.0f, ViewportHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	const ImVec2 Size = ImGui::GetContentRegionAvail();
	const int32 Width = std::max(1, static_cast<int32>(Size.x));
	const int32 Height = std::max(1, static_cast<int32>(Size.y));
	(void)Width;
	(void)Height;

	FSceneViewport& SceneViewport = Viewer->GetViewport();
	ID3D11ShaderResourceView* SRV = SceneViewport.GetOutSRV();

	ImGui::Dummy(Size);
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	const POINT ClientMin = ImGuiScreenToClientPoint(EditorEngine ? EditorEngine->GetWindow() : nullptr, Min);
	const bool bViewportHovered = ImGui::IsItemHovered();
	const bool bViewportClicked =
		bViewportHovered &&
		(ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
		 ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
		 ImGui::IsMouseClicked(ImGuiMouseButton_Middle));

	FViewportRect NewRect;
	NewRect.X = static_cast<int32>(ClientMin.x);
	NewRect.Y = static_cast<int32>(ClientMin.y);
	NewRect.Width = static_cast<int32>(Max.x - Min.x);
	NewRect.Height = static_cast<int32>(Max.y - Min.y);

	SceneViewport.SetRect(NewRect);
	if (auto* Client = SceneViewport.GetClient())
	{
		Client->SetViewportSize(static_cast<float>(NewRect.Width), static_cast<float>(NewRect.Height));
	}
	if (bViewportClicked && EditorEngine)
	{
		EditorEngine->FocusViewportInput(&SceneViewport);
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	if (SRV)
	{
		ID3D11DeviceContext* DC = EditorEngine
			? EditorEngine->GetRenderer().GetFD3DDevice().GetDeviceContext()
			: nullptr;
		DrawList->AddCallback(SetOpaqueBlendStateCallback, DC);
		DrawList->AddImage(reinterpret_cast<ImTextureID>(SRV), Min, Max);
		DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
	}
	else
	{
		DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(ImVec4(0.04f, 0.045f, 0.055f, 1.0f)));
		DrawList->AddText(ImVec2(Min.x + 12.0f, Min.y + 12.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), "Viewport is warming up...");
	}
	RenderAnimViewportStatsOverlay(&SceneViewport, Min, DeltaTime, SceneViewport.GetState());

	ImGui::EndChild();
}

void FEditorAnimationSequenceViewerWidget::RenderTimelinePanel(UDebugSkelMeshComponent* SkelComp, float DeltaTime)
{
	ImGui::BeginChild("AnimTimelinePanel", ImVec2(0.0f, TimelineHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	UAnimSequenceBase* Sequence = AnimSequence;
	const float PlayLength = ResolvePlayLength();
	const float FramesPerSecond = ResolveFramesPerSecond(Sequence);
	const int32 FrameCount = ResolveFrameCount(Sequence, PlayLength, FramesPerSecond);
	const int32 LastFrame = std::max(0, FrameCount - 1);

	if (!bTimelineViewInitialized || ViewEndFrame <= ViewStartFrame)
	{
		ViewStartFrame = 0.0f;
		ViewEndFrame = static_cast<float>(std::max(30, LastFrame));
		bTimelineViewInitialized = true;
	}

	if (bPreviewPlaying && Sequence && SkelComp)
	{
		if (UAnimPreviewInstance* Preview = EnsurePreviewInstance(SkelComp))
		{
			Preview->SyncPreviewPlayback(bPreviewPlaying, bPreviewLooping, bPreviewReverse, PreviewPlayRate);
			PreviewTime = Preview->GetCurrentAnimTime();
			bPreviewPlaying = Preview->IsPlaying();
		}
	}
	else if (!Sequence || !SkelComp)
	{
		bPreviewPlaying = false;
		ReleasePreviewInstance();
	}
	else
	{
		SyncPreviewInstance(SkelComp);
	}

	RenderTimelineToolbar(SkelComp, PlayLength, FramesPerSecond, LastFrame);
	ProcessKeyboardShortcuts(SkelComp, PlayLength, FramesPerSecond, LastFrame);
	RenderTimelineCanvas(SkelComp, PlayLength, FramesPerSecond, LastFrame);

	ImGui::EndChild();
}

bool FEditorAnimationSequenceViewerWidget::CanPreviewPlayback(UDebugSkelMeshComponent* SkelComp) const
{
	return IsAssetAvailable() && SkelComp;
}

bool FEditorAnimationSequenceViewerWidget::IsAssetAvailable() const
{
	return AnimSequence && AnimSequence->GetDataModel();
}

void FEditorAnimationSequenceViewerWidget::TogglePreviewPlayback(UDebugSkelMeshComponent* SkelComp)
{
	if (!CanPreviewPlayback(SkelComp))
	{
		return;
	}

	bPreviewPlaying = !bPreviewPlaying;
	if (UAnimPreviewInstance* Preview = EnsurePreviewInstance(SkelComp))
	{
		SyncPreviewInstance(SkelComp);
		Preview->SyncPreviewPlayback(bPreviewPlaying, bPreviewLooping, bPreviewReverse, PreviewPlayRate);
	}
}

bool FEditorAnimationSequenceViewerWidget::HasKeyboardFocusForShortcuts() const
{
	const ImGuiIO& IO = ImGui::GetIO();
	if (IO.WantTextInput || ImGui::IsAnyItemActive())
	{
		return false;
	}

	return ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
}

void FEditorAnimationSequenceViewerWidget::ProcessKeyboardShortcuts(
	UDebugSkelMeshComponent* SkelComp,
	float,
	float,
	int32)
{
	if (!HasKeyboardFocusForShortcuts())
	{
		return;
	}

	const ImGuiIO& IO = ImGui::GetIO();
	if (IO.KeyCtrl || IO.KeyAlt || IO.KeyShift || IO.KeySuper)
	{
		return;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
	{
		TogglePreviewPlayback(SkelComp);
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		DeleteSelectedTimelineItem();
	}
}

bool FEditorAnimationSequenceViewerWidget::HandleViewportShortcutInput(const FViewportInputContext& Context)
{
	if (Context.bImGuiCapturedKeyboard ||
		Context.Frame.IsCtrlDown() ||
		Context.Frame.IsAltDown() ||
		Context.Frame.IsShiftDown())
	{
		return false;
	}

	UDebugSkelMeshComponent* SkelComp = nullptr;
	ResolveCurrentMeshData(&SkelComp);

	if (Context.WasReleased(VK_DELETE))
	{
		return DeleteSelectedTimelineItem();
	}

	return false;
}

bool FEditorAnimationSequenceViewerWidget::DeleteSelectedTimelineItem()
{
	if (!AnimSequence || !IsAssetAvailable())
	{
		return false;
	}

	if (SelectedNotifyTrackIndex >= 0)
	{
		const TArray<FAnimNotifyTrack>& Tracks = AnimSequence->GetNotifyTracks();
		if (SelectedNotifyTrackIndex >= static_cast<int32>(Tracks.size()))
		{
			SelectedNotifyTrackIndex = -1;
			SelectedNotifyIndex = -1;
			return false;
		}

		if (SelectedNotifyIndex >= 0 &&
			SelectedNotifyIndex < static_cast<int32>(Tracks[SelectedNotifyTrackIndex].Notifies.size()))
		{
			if (!AnimSequence->RemoveNotifyEvent(SelectedNotifyTrackIndex, SelectedNotifyIndex))
			{
				return false;
			}

			SelectedNotifyIndex = -1;
			ContextNotifyIndex = -1;
			SaveAnimationAsset();
			return true;
		}

		if (Tracks.size() > 1 && AnimSequence->RemoveNotifyTrack(SelectedNotifyTrackIndex))
		{
			SelectedTrackIndex = -1;
			SelectedNotifyTrackIndex = -1;
			SelectedNotifyIndex = -1;
			ContextNotifyTrackIndex = -1;
			ContextNotifyIndex = -1;
			SaveAnimationAsset();
			return true;
		}

		return false;
	}

	UAnimDataModel* DataModel = AnimSequence->GetDataModel();
	FAnimationCurveData* CurveData = DataModel ? &DataModel->GetMutableCurveData() : nullptr;
	if (CurveData &&
		SelectedCurveTrackIndex >= 0 &&
		SelectedCurveTrackIndex < static_cast<int32>(CurveData->FloatCurves.size()))
	{
		CurveData->FloatCurves.erase(CurveData->FloatCurves.begin() + SelectedCurveTrackIndex);
		SelectedTrackIndex = -1;
		SelectedCurveTrackIndex = -1;
		ContextCurveTrackIndex = -1;
		SaveAnimationAsset();
		return true;
	}

	return false;
}

void FEditorAnimationSequenceViewerWidget::RenderTimelineToolbar(UDebugSkelMeshComponent* SkelComp, float PlayLength, float FramesPerSecond, int32 LastFrame)
{
	ImGui::BeginChild("##AnimSequencerToolbar", ImVec2(0.0f, AnimSequencerToolbarHeight), false, ImGuiWindowFlags_NoScrollbar);

	const bool bHasPlayableSequence = CanPreviewPlayback(SkelComp);
	ImGui::BeginDisabled(!bHasPlayableSequence);
	if (DrawAnimTimelineIconButton(
		"##AnimTimelineToStart",
		"PlayControlsToFront.png",
		"|<",
		"Go to first frame"))
	{
		SetPreviewFrame(SkelComp, 0, FramesPerSecond, PlayLength, LastFrame);
	}
	ImGui::SameLine();
	if (DrawAnimTimelineIconButton(
		"##AnimTimelinePrevious",
		"PlayControlsToPrevious.png",
		"<",
		"Previous frame"))
	{
		StepPreviewFrame(SkelComp, -1, FramesPerSecond, PlayLength, LastFrame);
	}
	ImGui::SameLine();
	if (DrawAnimTimelineIconButton(
		"##AnimTimelinePlayPause",
		bPreviewPlaying ? "PlayControlsPause.png" : "PlayControlsPlayForward.png",
		bPreviewPlaying ? "Pause" : "Play",
		bPreviewPlaying ? "Pause preview" : "Play preview",
		bPreviewPlaying))
	{
		TogglePreviewPlayback(SkelComp);
	}
	ImGui::SameLine();
	if (DrawAnimTimelineIconButton(
		"##AnimTimelineNext",
		"PlayControlsToNext.png",
		">",
		"Next frame"))
	{
		StepPreviewFrame(SkelComp, 1, FramesPerSecond, PlayLength, LastFrame);
	}
	ImGui::SameLine();
	if (DrawAnimTimelineIconButton(
		"##AnimTimelineToEnd",
		"PlayControlsToEnd.png",
		">|",
		"Go to last frame"))
	{
		SetPreviewFrame(SkelComp, LastFrame, FramesPerSecond, PlayLength, LastFrame);
	}
	ImGui::SameLine();
	if (DrawAnimTimelineIconButton(
		"##AnimTimelineStop",
		"PlayControlsStop.png",
		"Stop",
		"Stop preview"))
	{
		bPreviewPlaying = false;
		SetPreviewFrame(SkelComp, 0, FramesPerSecond, PlayLength, LastFrame);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::Checkbox("Loop", &bPreviewLooping);
	ImGui::SameLine();
	ImGui::Checkbox("Reverse", &bPreviewReverse);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(96.0f);
	ImGui::DragFloat("Rate", &PreviewPlayRate, 0.01f, 0.01f, 8.0f, "%.2fx");

	ImGui::SameLine();
	ImGui::TextDisabled("| Ctrl+Wheel: Zoom  Tree Wheel: Scroll  MMB Drag: Pan  LMB Ruler Drag: Playhead");

	ImGui::SameLine();
	const int32 CurrentFrame = TimeToFrame(PreviewTime, FramesPerSecond, LastFrame);
	ImGui::SetNextItemWidth(92.0f);
	int32 EditableFrame = CurrentFrame;
	if (ImGui::DragInt("##AnimSequencerCurrentFrame", &EditableFrame, 1.0f, 0, LastFrame, "F %d"))
	{
		SetPreviewFrame(SkelComp, EditableFrame, FramesPerSecond, PlayLength, LastFrame);
	}
	ImGui::SameLine();
	ImGui::Text(" / %d", LastFrame);
	ImGui::SameLine();
	ImGui::TextDisabled("%.3fs  %.0f fps", PreviewTime, FramesPerSecond);

	ImGui::EndChild();
}

void FEditorAnimationSequenceViewerWidget::RenderTimelineCanvas(UDebugSkelMeshComponent* SkelComp, float PlayLength, float FramesPerSecond, int32 LastFrame)
{
	UAnimSequenceBase* Sequence = AnimSequence;
	const TArray<FAnimNotifyTrack>* NotifyTracks = Sequence ? &Sequence->GetNotifyTracks() : nullptr;

	const ImVec2 PanelPos = ImGui::GetCursorScreenPos();
	const ImVec2 PanelSize = ImGui::GetContentRegionAvail();
	const ImVec2 PanelEnd(PanelPos.x + PanelSize.x, PanelPos.y + PanelSize.y);
	const float TimelineX = PanelPos.x + TimelineLeftPanelWidth;
	const float TimelineWidth = std::max(1.0f, PanelEnd.x - TimelineX);
	const float TrackTop = PanelPos.y + AnimSequencerRulerHeight;
	const float TrackHeight = std::max(1.0f, PanelEnd.y - TrackTop);

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImU32 BgColor = ImGui::GetColorU32(ImGuiCol_WindowBg);
	const ImU32 HeaderColor = ImGui::GetColorU32(ImGuiCol_Header);
	const ImU32 BorderColor = ImGui::GetColorU32(ImGuiCol_Border);
	const ImU32 GridColor = WithAlpha(ImGui::GetColorU32(ImGuiCol_TextDisabled), 0.35f);
	const ImU32 MinorGridColor = WithAlpha(ImGui::GetColorU32(ImGuiCol_TextDisabled), 0.18f);
	const ImU32 SectionColor = ImGui::GetColorU32(ImGuiCol_Button);
	const ImU32 SelectedColor = ImGui::GetColorU32(ImGuiCol_ButtonActive);

	DrawList->AddRectFilled(PanelPos, PanelEnd, BgColor);
	DrawList->AddRectFilled(PanelPos, ImVec2(TimelineX, PanelEnd.y), WithAlpha(HeaderColor, 0.45f));
	DrawList->AddRectFilled(ImVec2(TimelineX, PanelPos.y), PanelEnd, WithAlpha(BgColor, 0.92f));
	DrawList->AddLine(ImVec2(TimelineX, PanelPos.y), ImVec2(TimelineX, PanelEnd.y), BorderColor);
	DrawList->AddLine(ImVec2(PanelPos.x, TrackTop), ImVec2(PanelEnd.x, TrackTop), BorderColor);
	DrawList->AddRect(PanelPos, PanelEnd, BorderColor);

	ImGui::InvisibleButton(
		"##AnimSequencerCanvas",
		PanelSize,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool bCanvasHovered = ImGui::IsItemHovered();
	const bool bCanvasActive = ImGui::IsItemActive();
	const ImVec2 Mouse = ImGui::GetIO().MousePos;

	const float VisibleRange = ClampSequencerFrameRange(ViewEndFrame - ViewStartFrame);
	const auto FrameToX = [&](float Frame)
	{
		return TimelineX + ((Frame - ViewStartFrame) / VisibleRange) * TimelineWidth;
	};
	const auto XToFrame = [&](float X)
	{
		return ViewStartFrame + ((X - TimelineX) / TimelineWidth) * VisibleRange;
	};
	const int32 TimelineEndFrame = std::max(
		LastFrame,
		static_cast<int32>(std::round(std::max(0.0f, PlayLength) * FramesPerSecond)));

	const bool bMouseInTrackTree = bCanvasHovered
		&& Mouse.x >= PanelPos.x
		&& Mouse.x < TimelineX
		&& Mouse.y >= TrackTop
		&& Mouse.y <= PanelEnd.y;

	if (bCanvasHovered && std::fabs(ImGui::GetIO().MouseWheel) > 0.0f && ImGui::GetIO().KeyCtrl)
	{
		const float MouseFrame = XToFrame(std::clamp(Mouse.x, TimelineX, PanelEnd.x));
		const float ZoomFactor = ImGui::GetIO().MouseWheel > 0.0f ? 0.85f : 1.0f / 0.85f;
		const float NewRange = ClampSequencerFrameRange(VisibleRange * ZoomFactor);
		const float AnchorAlpha = std::clamp((MouseFrame - ViewStartFrame) / VisibleRange, 0.0f, 1.0f);
		ViewStartFrame = MouseFrame - NewRange * AnchorAlpha;
		ViewEndFrame = ViewStartFrame + NewRange;
	}
	else if (bMouseInTrackTree && std::fabs(ImGui::GetIO().MouseWheel) > 0.0f)
	{
		const float ContentHeight = std::max(0.0f, static_cast<float>(LastTimelineRowCount) * AnimSequencerRowHeight);
		const float MaxScrollY = std::max(0.0f, ContentHeight - TrackHeight);
		TrackScrollY = std::clamp(
			TrackScrollY - ImGui::GetIO().MouseWheel * AnimSequencerRowHeight * 2.0f,
			0.0f,
			MaxScrollY);
	}

	if (bCanvasActive && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
	{
		const float DeltaFrame = -(ImGui::GetIO().MouseDelta.x / TimelineWidth) * VisibleRange;
		ViewStartFrame += DeltaFrame;
		ViewEndFrame += DeltaFrame;
	}

	const int32 CurrentFrame = TimeToFrame(PreviewTime, FramesPerSecond, LastFrame);
	const float PlayheadX = FrameToX(static_cast<float>(CurrentFrame));
	const bool bMouseInRuler = bCanvasHovered && Mouse.x >= TimelineX && Mouse.y >= PanelPos.y && Mouse.y <= TrackTop;
	if (bMouseInRuler && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		bDraggingPlayhead = true;
	}
	if (bDraggingPlayhead && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		const int32 NewFrame = std::clamp(static_cast<int32>(std::round(XToFrame(Mouse.x))), 0, LastFrame);
		SetPreviewFrame(SkelComp, NewFrame, FramesPerSecond, PlayLength, LastFrame);
	}
	if (bDraggingPlayhead && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		bDraggingPlayhead = false;
	}
	if (bDraggingNotify && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
	{
		const TArray<FAnimNotifyTrack>& Tracks = AnimSequence ? AnimSequence->GetNotifyTracks() : TArray<FAnimNotifyTrack>{};
		if (DraggingNotifyTrackIndex >= 0
			&& DraggingNotifyTrackIndex < static_cast<int32>(Tracks.size())
			&& DraggingNotifyIndex >= 0
			&& DraggingNotifyIndex < static_cast<int32>(Tracks[DraggingNotifyTrackIndex].Notifies.size()))
		{
			const FAnimNotifyEvent& Notify = Tracks[DraggingNotifyTrackIndex].Notifies[DraggingNotifyIndex];
			const int32 CurrentStartFrame = TimeToFrame(Notify.TriggerTime, FramesPerSecond, LastFrame);
			const int32 CurrentDurationFrames = DurationToFrameCount(Notify.Duration, FramesPerSecond, TimelineEndFrame);
			const int32 CurrentEndFrame = std::clamp(CurrentStartFrame + CurrentDurationFrames, 0, TimelineEndFrame);
			const int32 MouseFrame = std::clamp(static_cast<int32>(std::round(XToFrame(Mouse.x))), 0, TimelineEndFrame);

			int32 NewStartFrame = CurrentStartFrame;
			int32 NewEndFrame = CurrentEndFrame;
			if (DraggingNotifyMode == 2)
			{
				NewStartFrame = std::clamp(MouseFrame, 0, CurrentEndFrame);
			}
			else if (DraggingNotifyMode == 3)
			{
				NewEndFrame = std::clamp(MouseFrame, CurrentStartFrame, TimelineEndFrame);
			}
			else
			{
				const int32 DurationFrames = std::max(0, CurrentDurationFrames);
				NewStartFrame = std::clamp(MouseFrame + DraggingNotifyGrabFrameOffset, 0, std::max(0, TimelineEndFrame - DurationFrames));
				NewEndFrame = std::clamp(NewStartFrame + DurationFrames, 0, TimelineEndFrame);
			}

			const float NewTime = FrameToTime(NewStartFrame, FramesPerSecond, PlayLength);
			const float NewDuration = FrameCountToDuration(std::max(0, NewEndFrame - NewStartFrame), FramesPerSecond, PlayLength);
			if (AnimSequence->SetNotifyTiming(DraggingNotifyTrackIndex, DraggingNotifyIndex, NewTime, NewDuration))
			{
				ContextNotifyFrame = NewStartFrame;
				bDraggingNotifyDirty = true;
			}
		}
	}
	if (bDraggingNotify && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		bDraggingNotify = false;
		DraggingNotifyTrackIndex = -1;
		DraggingNotifyIndex = -1;
		DraggingNotifyMode = 0;
		DraggingNotifyGrabFrameOffset = 0;
		if (bDraggingNotifyDirty)
		{
			SaveAnimationAsset();
		}
		bDraggingNotifyDirty = false;
	}

	DrawList->PushClipRect(ImVec2(TimelineX, PanelPos.y), PanelEnd, true);
	const float MajorStepCandidates[] = { 1.0f, 2.0f, 5.0f, 10.0f, 15.0f, 30.0f, 60.0f, 120.0f, 300.0f, 600.0f, 1200.0f };
	float MajorStep = 1.0f;
	for (float Candidate : MajorStepCandidates)
	{
		if ((Candidate / VisibleRange) * TimelineWidth >= 72.0f)
		{
			MajorStep = Candidate;
			break;
		}
	}
	const float MinorStep = std::max(1.0f, MajorStep * 0.5f);
	const float FirstMinor = std::floor(ViewStartFrame / MinorStep) * MinorStep;
	for (float Frame = FirstMinor; Frame <= ViewEndFrame + MinorStep; Frame += MinorStep)
	{
		const float X = FrameToX(Frame);
		const bool bMajor = std::fabs(std::fmod(std::fabs(Frame), MajorStep)) < 0.0001f;
		DrawList->AddLine(ImVec2(X, PanelPos.y), ImVec2(X, PanelEnd.y), bMajor ? GridColor : MinorGridColor);
		if (bMajor)
		{
			char Label[32];
			snprintf(Label, sizeof(Label), "%d", static_cast<int32>(std::round(Frame)));
			DrawList->AddText(ImVec2(X + 4.0f, PanelPos.y + 7.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), Label);
		}
	}

	const float PlaybackStartX = FrameToX(0.0f);
	const float PlaybackEndX = FrameToX(static_cast<float>(TimelineEndFrame));
	DrawList->AddRectFilled(ImVec2(PlaybackStartX, PanelPos.y), ImVec2(PlaybackEndX, TrackTop), IM_COL32(84, 150, 84, 32));
	DrawList->AddLine(ImVec2(PlaybackStartX, PanelPos.y), ImVec2(PlaybackStartX, PanelEnd.y), IM_COL32(44, 220, 84, 255), 2.0f);
	DrawList->AddLine(ImVec2(PlaybackEndX, PanelPos.y), ImVec2(PlaybackEndX, PanelEnd.y), IM_COL32(220, 24, 24, 255), 2.0f);
	DrawList->PopClipRect();

	const auto DrawRowBase = [&](int32 RowIndex, const char* Label, bool bSelected)
	{
		const float RowY = TrackTop + RowIndex * AnimSequencerRowHeight - TrackScrollY;
		if (RowY + AnimSequencerRowHeight < TrackTop || RowY > PanelEnd.y)
		{
			return RowY;
		}

		const ImVec2 RowMin(PanelPos.x, RowY);
		const ImVec2 RowMax(PanelEnd.x, RowY + AnimSequencerRowHeight);
		DrawList->AddRectFilled(RowMin, RowMax, bSelected ? WithAlpha(SelectedColor, 0.30f) : WithAlpha(HeaderColor, RowIndex % 2 == 0 ? 0.16f : 0.08f));
		DrawList->AddLine(ImVec2(PanelPos.x, RowMax.y), ImVec2(PanelEnd.x, RowMax.y), WithAlpha(BorderColor, 0.55f));
		DrawList->AddText(ImVec2(PanelPos.x + 12.0f, RowY + 7.0f), ImGui::GetColorU32(ImGuiCol_Text), Label);
		return RowY;
	};

	int32 RowIndex = 0;
	float RowY = DrawRowBase(RowIndex, "Animation", SelectedTrackIndex == RowIndex);
	if (RowY + AnimSequencerRowHeight >= TrackTop && RowY <= PanelEnd.y)
	{
		const ImVec2 SectionMin(std::max(TimelineX + 2.0f, PlaybackStartX), RowY + 5.0f);
		const ImVec2 SectionMax(std::min(PanelEnd.x - 2.0f, PlaybackEndX), RowY + AnimSequencerRowHeight - 5.0f);
		if (SectionMax.x > SectionMin.x)
		{
			DrawList->AddRectFilled(SectionMin, SectionMax, SectionColor, 4.0f);
			DrawList->AddRect(SectionMin, SectionMax, WithAlpha(ImGui::GetColorU32(ImGuiCol_Text), 0.45f), 4.0f);
			DrawList->AddText(ImVec2(SectionMin.x + 8.0f, RowY + 7.0f), ImGui::GetColorU32(ImGuiCol_Text), "Sequence");
		}
	}
	if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && Mouse.x >= PanelPos.x && Mouse.y >= RowY && Mouse.y < RowY + AnimSequencerRowHeight)
	{
		SelectedTrackIndex = RowIndex;
		SelectedNotifyIndex = -1;
	}
	++RowIndex;

	const int32 NotifyTrackCount = NotifyTracks ? static_cast<int32>(NotifyTracks->size()) : 0;
	if (NotifyTrackCount == 0)
	{
		RowY = DrawRowBase(RowIndex, "Notifies", SelectedTrackIndex == RowIndex);
		if (RowY + AnimSequencerRowHeight >= TrackTop && RowY <= PanelEnd.y)
		{
			DrawList->AddText(ImVec2(TimelineX + 8.0f, RowY + 7.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), "Right-click to add notify track");
		}
		if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && Mouse.y >= RowY && Mouse.y < RowY + AnimSequencerRowHeight)
		{
			ContextNotifyTrackIndex = -1;
			ContextNotifyIndex = -1;
			ContextNotifyFrame = std::clamp(static_cast<int32>(std::round(XToFrame(Mouse.x))), 0, LastFrame);
			ImGui::OpenPopup("AnimNotifyContextMenu");
		}
		++RowIndex;
	}
	else
	{
		for (int32 NotifyTrackIndex = 0; NotifyTrackIndex < NotifyTrackCount; ++NotifyTrackIndex)
		{
			const FAnimNotifyTrack& NotifyTrack = (*NotifyTracks)[NotifyTrackIndex];
			const FString TrackLabel = NotifyTrack.TrackName.ToString().empty()
				? FString("Notify Track")
				: NotifyTrack.TrackName.ToString();
			RowY = DrawRowBase(RowIndex, TrackLabel.c_str(), SelectedTrackIndex == RowIndex);
			bool bNotifyContextClickHandled = false;
			bool bNotifyLeftClickHandled = false;
			if (RowY + AnimSequencerRowHeight >= TrackTop && RowY <= PanelEnd.y)
			{
				DrawList->PushClipRect(ImVec2(TimelineX, RowY), ImVec2(PanelEnd.x, RowY + AnimSequencerRowHeight), true);
				if (!NotifyTrack.Notifies.empty())
				{
					for (int32 NotifyIndex = 0; NotifyIndex < static_cast<int32>(NotifyTrack.Notifies.size()); ++NotifyIndex)
					{
						const FAnimNotifyEvent& Notify = NotifyTrack.Notifies[NotifyIndex];
						const int32 NotifyFrame = TimeToFrame(Notify.TriggerTime, FramesPerSecond, LastFrame);
						const int32 DurationFrames = DurationToFrameCount(Notify.Duration, FramesPerSecond, TimelineEndFrame);
						const int32 NotifyEndFrame = std::clamp(NotifyFrame + DurationFrames, 0, TimelineEndFrame);
						const bool bHasDuration = DurationFrames > 0;
						const float NotifyX = FrameToX(static_cast<float>(NotifyFrame));
						const float NotifyEndX = FrameToX(static_cast<float>(NotifyEndFrame));
						if ((bHasDuration && NotifyEndX < TimelineX) || NotifyX > PanelEnd.x || (!bHasDuration && NotifyX < TimelineX))
						{
							continue;
						}

						const bool bSelectedNotify = SelectedNotifyTrackIndex == NotifyTrackIndex && SelectedNotifyIndex == NotifyIndex;
						const ImU32 NotifyColor = bHasDuration
							? (bSelectedNotify ? IM_COL32(214, 146, 255, 255) : IM_COL32(156, 92, 255, 255))
							: (bSelectedNotify ? IM_COL32(255, 190, 82, 255) : IM_COL32(88, 178, 255, 255));
						const FString NotifyLabel = Notify.NotifyName.ToString().empty() ? FString("Notify") : Notify.NotifyName.ToString();
						const ImVec2 LabelSize = ImGui::CalcTextSize(NotifyLabel.c_str());
						const float MarkerY = RowY + AnimSequencerRowHeight * 0.5f;
						const ImVec2 Diamond[4] =
						{
							ImVec2(NotifyX, MarkerY - 6.0f),
							ImVec2(NotifyX + 6.0f, MarkerY),
							ImVec2(NotifyX, MarkerY + 6.0f),
							ImVec2(NotifyX - 6.0f, MarkerY)
						};
						const ImVec2 EndDiamond[4] =
						{
							ImVec2(NotifyEndX, MarkerY - 6.0f),
							ImVec2(NotifyEndX + 6.0f, MarkerY),
							ImVec2(NotifyEndX, MarkerY + 6.0f),
							ImVec2(NotifyEndX - 6.0f, MarkerY)
						};
						const ImVec2 LabelMin(NotifyX + 7.0f, RowY + 5.0f);
						const float LabelNaturalMaxX = LabelMin.x + LabelSize.x + 10.0f;
						const float LabelDurationMaxX = bHasDuration ? std::max(LabelMin.x + 4.0f, NotifyEndX - 7.0f) : LabelNaturalMaxX;
						const ImVec2 LabelMax(bHasDuration ? LabelDurationMaxX : LabelNaturalMaxX, RowY + AnimSequencerRowHeight - 5.0f);
						const ImVec2 StartHandleMin(NotifyX - 7.0f, RowY + 4.0f);
						const ImVec2 StartHandleMax(NotifyX + 7.0f, RowY + AnimSequencerRowHeight - 4.0f);
						const ImVec2 EndHandleMin(NotifyEndX - 7.0f, RowY + 4.0f);
						const ImVec2 EndHandleMax(NotifyEndX + 7.0f, RowY + AnimSequencerRowHeight - 4.0f);
						const ImVec2 MarkerMin(std::min(NotifyX - 7.0f, bHasDuration ? NotifyEndX - 4.0f : NotifyX - 7.0f), RowY + 4.0f);
						const ImVec2 MarkerMax(std::max(std::max(NotifyX + 7.0f, LabelMax.x), bHasDuration ? NotifyEndX + 4.0f : NotifyX + 7.0f), RowY + AnimSequencerRowHeight - 4.0f);

						DrawList->AddRectFilled(LabelMin, LabelMax, WithAlpha(NotifyColor, bSelectedNotify ? 0.56f : 0.36f), 2.0f);
						DrawList->AddRect(LabelMin, LabelMax, WithAlpha(NotifyColor, bSelectedNotify ? 0.90f : 0.60f), 2.0f);
						DrawList->AddConvexPolyFilled(Diamond, 4, NotifyColor);
						DrawList->AddPolyline(Diamond, 4, WithAlpha(ImGui::GetColorU32(ImGuiCol_Text), 0.65f), ImDrawFlags_Closed, 1.0f);
						if (bHasDuration)
						{
							DrawList->AddConvexPolyFilled(EndDiamond, 4, NotifyColor);
							DrawList->AddPolyline(EndDiamond, 4, WithAlpha(ImGui::GetColorU32(ImGuiCol_Text), 0.65f), ImDrawFlags_Closed, 1.0f);
						}
						DrawList->AddLine(ImVec2(NotifyX, RowY + 3.0f), ImVec2(NotifyX, RowY + AnimSequencerRowHeight - 3.0f), WithAlpha(NotifyColor, 0.80f), 1.0f);
						const ImVec4 LabelClipRect(LabelMin.x + 3.0f, LabelMin.y, LabelMax.x - 3.0f, LabelMax.y);
						DrawList->AddText(
							nullptr,
							0.0f,
							ImVec2(LabelMin.x + 5.0f, RowY + 7.0f),
							ImGui::GetColorU32(ImGuiCol_Text),
							NotifyLabel.c_str(),
							nullptr,
							0.0f,
							&LabelClipRect);

						const bool bMouseOnStartHandle = bHasDuration
							&& Mouse.x >= StartHandleMin.x - 3.0f
							&& Mouse.x <= StartHandleMax.x + 3.0f
							&& Mouse.y >= StartHandleMin.y
							&& Mouse.y <= StartHandleMax.y;
						const bool bMouseOnEndHandle = bHasDuration
							&& Mouse.x >= EndHandleMin.x - 3.0f
							&& Mouse.x <= EndHandleMax.x + 3.0f
							&& Mouse.y >= EndHandleMin.y
							&& Mouse.y <= EndHandleMax.y;
						const bool bMouseOnMarker = Mouse.x >= MarkerMin.x - 3.0f
							&& Mouse.x <= MarkerMax.x + 3.0f
							&& Mouse.y >= RowY
							&& Mouse.y < RowY + AnimSequencerRowHeight;
						if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (bMouseOnStartHandle || bMouseOnEndHandle || bMouseOnMarker))
						{
							SelectedTrackIndex = RowIndex;
							SelectedNotifyTrackIndex = NotifyTrackIndex;
							SelectedNotifyIndex = NotifyIndex;
							SelectedCurveTrackIndex = -1;
							bDraggingNotify = true;
							bDraggingNotifyDirty = false;
							DraggingNotifyTrackIndex = NotifyTrackIndex;
							DraggingNotifyIndex = NotifyIndex;
							DraggingNotifyMode = bMouseOnStartHandle ? 2 : (bMouseOnEndHandle ? 3 : 1);
							DraggingNotifyGrabFrameOffset = NotifyFrame - std::clamp(static_cast<int32>(std::round(XToFrame(Mouse.x))), 0, TimelineEndFrame);
							bNotifyLeftClickHandled = true;
						}
						if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && bMouseOnMarker)
						{
							ContextNotifyTrackIndex = NotifyTrackIndex;
							ContextNotifyIndex = NotifyIndex;
							ContextNotifyFrame = NotifyFrame;
							SelectedTrackIndex = RowIndex;
							SelectedNotifyTrackIndex = NotifyTrackIndex;
							SelectedNotifyIndex = NotifyIndex;
							SelectedCurveTrackIndex = -1;
							bNotifyContextClickHandled = true;
							ImGui::OpenPopup("AnimNotifyContextMenu");
						}
					}
				}
				else
				{
					DrawList->AddText(ImVec2(TimelineX + 8.0f, RowY + 7.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), "Right-click to add notify");
				}
				DrawList->PopClipRect();
			}
			if (!bNotifyLeftClickHandled && bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && Mouse.x >= PanelPos.x && Mouse.y >= RowY && Mouse.y < RowY + AnimSequencerRowHeight)
			{
				SelectedTrackIndex = RowIndex;
				SelectedNotifyTrackIndex = NotifyTrackIndex;
				SelectedNotifyIndex = -1;
				SelectedCurveTrackIndex = -1;
			}
			if (!bNotifyContextClickHandled && bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && Mouse.y >= RowY && Mouse.y < RowY + AnimSequencerRowHeight)
			{
				ContextNotifyTrackIndex = NotifyTrackIndex;
				ContextNotifyIndex = -1;
				ContextNotifyFrame = std::clamp(static_cast<int32>(std::round(XToFrame(Mouse.x))), 0, LastFrame);
				ImGui::OpenPopup("AnimNotifyContextMenu");
			}
			++RowIndex;
		}
	}

	RenderNotifyContextMenu(FramesPerSecond, PlayLength, LastFrame);

	UAnimDataModel* DataModel = AnimSequence ? AnimSequence->GetDataModel() : nullptr;
	FAnimationCurveData* CurveData = DataModel ? &DataModel->GetMutableCurveData() : nullptr;
	const int32 CurveTrackCount = CurveData ? static_cast<int32>(CurveData->FloatCurves.size()) : 0;
	if (CurveTrackCount == 0)
	{
		RowY = DrawRowBase(RowIndex, "Curves", SelectedTrackIndex == RowIndex);
		if (RowY + AnimSequencerRowHeight >= TrackTop && RowY <= PanelEnd.y)
		{
			DrawList->AddText(ImVec2(TimelineX + 8.0f, RowY + 7.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), "Right-click to add curve track");
		}
		if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && Mouse.x >= PanelPos.x && Mouse.y >= RowY && Mouse.y < RowY + AnimSequencerRowHeight)
		{
			SelectedTrackIndex = RowIndex;
			SelectedNotifyIndex = -1;
			SelectedCurveTrackIndex = -1;
		}
		if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && Mouse.y >= RowY && Mouse.y < RowY + AnimSequencerRowHeight)
		{
			ContextCurveTrackIndex = -1;
			ImGui::OpenPopup("AnimCurveContextMenu");
		}
		++RowIndex;
	}
	else
	{
		for (int32 CurveTrackIndex = 0; CurveTrackIndex < CurveTrackCount; ++CurveTrackIndex)
		{
			const FAnimCurveTrack& CurveTrack = CurveData->FloatCurves[CurveTrackIndex];
			const FString TrackLabel = CurveTrack.CurveName.ToString().empty()
				? FString("Curve Track")
				: CurveTrack.CurveName.ToString();
			RowY = DrawRowBase(RowIndex, TrackLabel.c_str(), SelectedTrackIndex == RowIndex);
			if (RowY + AnimSequencerRowHeight >= TrackTop && RowY <= PanelEnd.y)
			{
				DrawList->PushClipRect(ImVec2(TimelineX, RowY), ImVec2(PanelEnd.x, RowY + AnimSequencerRowHeight), true);
				if (CurveTrack.Curve.Keys.empty())
				{
					DrawList->AddText(ImVec2(TimelineX + 8.0f, RowY + 7.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), "Right-click to edit curve");
				}
				else
				{
					const ImU32 CurveColor = SelectedCurveTrackIndex == CurveTrackIndex
						? IM_COL32(255, 190, 82, 255)
						: IM_COL32(118, 215, 142, 255);
					const float ValueMin = -1.0f;
					const float ValueMax = 1.0f;
					const float SampleStep = std::max(1.0f, std::ceil(VisibleRange / 512.0f));
					ImVec2 PreviousPoint;
					bool bHasPreviousPoint = false;
					for (float Frame = std::max(0.0f, std::floor(ViewStartFrame)); Frame <= std::min(static_cast<float>(LastFrame), std::ceil(ViewEndFrame)); Frame += SampleStep)
					{
						const float Time = FrameToTime(static_cast<int32>(Frame), FramesPerSecond, PlayLength);
						const float Value = std::clamp(CurveTrack.Curve.Evaluate(Time), ValueMin, ValueMax);
						const float NormalizedValue = (Value - ValueMin) / (ValueMax - ValueMin);
						const ImVec2 Point(FrameToX(Frame), RowY + AnimSequencerRowHeight - 5.0f - NormalizedValue * (AnimSequencerRowHeight - 10.0f));
						if (bHasPreviousPoint)
						{
							DrawList->AddLine(PreviousPoint, Point, CurveColor, 1.5f);
						}
						PreviousPoint = Point;
						bHasPreviousPoint = true;
					}

					for (const FCurveKey& Key : CurveTrack.Curve.Keys)
					{
						const int32 KeyFrame = TimeToFrame(Key.Time, FramesPerSecond, LastFrame);
						const float KeyX = FrameToX(static_cast<float>(KeyFrame));
						if (KeyX < TimelineX || KeyX > PanelEnd.x)
						{
							continue;
						}
						DrawList->AddCircleFilled(ImVec2(KeyX, RowY + AnimSequencerRowHeight * 0.5f), 3.0f, CurveColor);
					}
				}
				DrawList->PopClipRect();
			}
			if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && Mouse.x >= PanelPos.x && Mouse.y >= RowY && Mouse.y < RowY + AnimSequencerRowHeight)
			{
				SelectedTrackIndex = RowIndex;
				SelectedNotifyIndex = -1;
				SelectedCurveTrackIndex = CurveTrackIndex;
			}
			if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && Mouse.y >= RowY && Mouse.y < RowY + AnimSequencerRowHeight)
			{
				SelectedTrackIndex = RowIndex;
				SelectedNotifyIndex = -1;
				SelectedCurveTrackIndex = CurveTrackIndex;
				ContextCurveTrackIndex = CurveTrackIndex;
				ImGui::OpenPopup("AnimCurveContextMenu");
			}
			++RowIndex;
		}
	}

	RenderCurveContextMenu(PlayLength);

	LastTimelineRowCount = RowIndex;
	const float ContentHeight = std::max(0.0f, static_cast<float>(LastTimelineRowCount) * AnimSequencerRowHeight);
	const float MaxScrollY = std::max(0.0f, ContentHeight - TrackHeight);
	TrackScrollY = std::clamp(TrackScrollY, 0.0f, MaxScrollY);

	DrawList->PushClipRect(ImVec2(TimelineX, PanelPos.y), PanelEnd, true);
	const float DrawPlayheadX = FrameToX(static_cast<float>(CurrentFrame));
	DrawList->AddLine(ImVec2(DrawPlayheadX, PanelPos.y), ImVec2(DrawPlayheadX, PanelEnd.y), IM_COL32(255, 164, 64, 255), 2.0f);
	DrawList->AddTriangleFilled(
		ImVec2(DrawPlayheadX - 6.0f, PanelPos.y + 2.0f),
		ImVec2(DrawPlayheadX + 6.0f, PanelPos.y + 2.0f),
		ImVec2(DrawPlayheadX, PanelPos.y + 15.0f),
		IM_COL32(255, 164, 64, 255));
	DrawList->PopClipRect();
}

void FEditorAnimationSequenceViewerWidget::RenderNotifyContextMenu(float FramesPerSecond, float PlayLength, int32 LastFrame)
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
	if (!ImGui::BeginPopup("AnimNotifyContextMenu", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
	{
		return;
	}

	const bool bCanEdit = AnimSequence && AnimSequence->GetDataModel();
	ImGui::BeginDisabled(!bCanEdit);
	if (ImGui::BeginMenu("Add Notify"))
	{
		const TArray<const UClass*> NotifyTypes = GetRegisteredNotifyTypes();
		if (NotifyTypes.empty())
		{
			ImGui::MenuItem("No AnimNotify classes", nullptr, false, false);
		}
		else
		{
			for (const UClass* NotifyClass : NotifyTypes)
			{
				if (!NotifyClass || std::strlen(NotifyClass->GetName()) == 0)
				{
					continue;
				}

				const FString ClassName = NotifyClass->GetName();
				const FString DisplayName = NotifyClass->GetDisplayName();
				if (ImGui::MenuItem(DisplayName.c_str()))
				{
					const int32 TrackIndex = ContextNotifyTrackIndex >= 0 ? ContextNotifyTrackIndex : AnimSequence->AddNotifyTrack(FName("Notifies"));
					FAnimNotifyEvent Notify;
					Notify.TriggerTime = FrameToTime(std::clamp(ContextNotifyFrame, 0, LastFrame), FramesPerSecond, PlayLength);
					Notify.Duration = NotifyClass->IsChildOf(UAnimNotifyState::StaticClass()) ? (1.0f / std::max(1.0f, FramesPerSecond)) : 0.0f;
					Notify.NotifyName = FName(DisplayName);
					Notify.NotifyClassName = ClassName;
					SelectedNotifyTrackIndex = TrackIndex;
					SelectedNotifyIndex = AnimSequence->AddNotifyEvent(TrackIndex, Notify);
					SelectedCurveTrackIndex = -1;
					SelectedTrackIndex = 1 + std::max(0, TrackIndex);
					SaveAnimationAsset();
				}
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Add Notify Track"))
	{
		const int32 TrackIndex = AnimSequence->AddNotifyTrack(FName("Notify Track"));
		SelectedNotifyTrackIndex = TrackIndex;
		SelectedNotifyIndex = -1;
		SelectedCurveTrackIndex = -1;
		SelectedTrackIndex = 1 + TrackIndex;
		SaveAnimationAsset();
	}

	ImGui::Separator();
	const bool bCanDeleteNotify = ContextNotifyTrackIndex >= 0 && ContextNotifyIndex >= 0;
	ImGui::BeginDisabled(!bCanDeleteNotify);
	if (ImGui::MenuItem("Delete Notify"))
	{
		if (AnimSequence->RemoveNotifyEvent(ContextNotifyTrackIndex, ContextNotifyIndex))
		{
			if (SelectedNotifyTrackIndex == ContextNotifyTrackIndex && SelectedNotifyIndex == ContextNotifyIndex)
			{
				SelectedNotifyIndex = -1;
			}
			else if (SelectedNotifyTrackIndex == ContextNotifyTrackIndex && SelectedNotifyIndex > ContextNotifyIndex)
			{
				--SelectedNotifyIndex;
			}
			ContextNotifyIndex = -1;
			SaveAnimationAsset();
		}
	}
	ImGui::EndDisabled();

	const bool bCanDeleteTrack = bCanEdit && ContextNotifyTrackIndex >= 0 && AnimSequence->GetNotifyTracks().size() > 1;
	ImGui::BeginDisabled(!bCanDeleteTrack);
	if (ImGui::MenuItem("Delete Notify Track"))
	{
		if (AnimSequence->RemoveNotifyTrack(ContextNotifyTrackIndex))
		{
			SelectedNotifyTrackIndex = -1;
			SelectedNotifyIndex = -1;
			SaveAnimationAsset();
		}
	}
	ImGui::EndDisabled();
	ImGui::EndDisabled();

	if (!bCanEdit && ImGui::IsWindowAppearing())
	{
		ImGui::SetTooltip("Animation asset is missing.");
	}

	ImGui::EndPopup();
}

void FEditorAnimationSequenceViewerWidget::RenderCurveContextMenu(float PlayLength)
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(190.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
	if (!ImGui::BeginPopup("AnimCurveContextMenu", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
	{
		return;
	}

	const bool bCanEdit = AnimSequence && AnimSequence->GetDataModel();
	ImGui::BeginDisabled(!bCanEdit);
	if (ImGui::MenuItem("Add Curve Track"))
	{
		FAnimationCurveData& CurveData = AnimSequence->GetDataModel()->GetMutableCurveData();
		FAnimCurveTrack CurveTrack;
		const int32 NewTrackIndex = static_cast<int32>(CurveData.FloatCurves.size());
		CurveTrack.CurveName = FName(("Curve " + std::to_string(NewTrackIndex + 1)).c_str());
		FCurveKey StartKey;
		StartKey.Time = 0.0f;
		StartKey.Value = 0.0f;
		CurveTrack.Curve.Keys.push_back(StartKey);
		FCurveKey EndKey = StartKey;
		EndKey.Time = std::max(0.0f, PlayLength);
		CurveTrack.Curve.Keys.push_back(EndKey);
		CurveTrack.Curve.SortKeys();
		CurveData.FloatCurves.push_back(CurveTrack);
		SelectedCurveTrackIndex = NewTrackIndex;
		SelectedNotifyTrackIndex = -1;
		SelectedNotifyIndex = -1;
		SaveAnimationAsset();
	}

	const bool bCanOpenCurve = bCanEdit && ContextCurveTrackIndex >= 0;
	ImGui::BeginDisabled(!bCanOpenCurve);
	if (ImGui::MenuItem("Open Curve Editor"))
	{
		OpenCurveTrackEditor(ContextCurveTrackIndex);
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::BeginDisabled(!bCanOpenCurve);
	if (ImGui::MenuItem("Delete Curve Track"))
	{
		FAnimationCurveData& CurveData = AnimSequence->GetDataModel()->GetMutableCurveData();
		if (ContextCurveTrackIndex >= 0 && ContextCurveTrackIndex < static_cast<int32>(CurveData.FloatCurves.size()))
		{
			CurveData.FloatCurves.erase(CurveData.FloatCurves.begin() + ContextCurveTrackIndex);
			SelectedCurveTrackIndex = -1;
			ContextCurveTrackIndex = -1;
			SaveAnimationAsset();
		}
	}
	ImGui::EndDisabled();
	ImGui::EndDisabled();

	ImGui::EndPopup();
}

void FEditorAnimationSequenceViewerWidget::OpenCurveTrackEditor(int32 CurveTrackIndex)
{
	if (!EditorEngine || !AnimSequence || !AnimSequence->GetDataModel() || CurveTrackIndex < 0)
	{
		return;
	}

	FAnimationCurveData& CurveData = AnimSequence->GetDataModel()->GetMutableCurveData();
	if (CurveTrackIndex >= static_cast<int32>(CurveData.FloatCurves.size()))
	{
		return;
	}

	UCurveFloatAsset* EditableCurve = UObjectManager::Get().CreateObject<UCurveFloatAsset>();
	if (!EditableCurve)
	{
		return;
	}

	EditableCurve->GetMutableCurve() = CurveData.FloatCurves[CurveTrackIndex].Curve;
	const FString SourceLabel = CurveData.FloatCurves[CurveTrackIndex].CurveName.ToString();
	const FString SourcePath = AssetPath;
	CurveEditorWidget.OpenCurveFromAnimSequence(
		EditableCurve,
		SourceLabel.empty() ? FString("Curve") : SourceLabel,
		SourcePath,
		[this, CurveTrackIndex](UCurveFloatAsset* EditedCurve)
		{
			if (!EditedCurve || !AnimSequence || !AnimSequence->GetDataModel())
			{
				return false;
			}

			FAnimationCurveData& TargetCurveData = AnimSequence->GetDataModel()->GetMutableCurveData();
			if (CurveTrackIndex < 0 || CurveTrackIndex >= static_cast<int32>(TargetCurveData.FloatCurves.size()))
			{
				return false;
			}

			TargetCurveData.FloatCurves[CurveTrackIndex].Curve = EditedCurve->GetCurve();
			TargetCurveData.FloatCurves[CurveTrackIndex].Curve.SortKeys();
			return SaveAnimationAsset();
		});
}

void FEditorAnimationSequenceViewerWidget::RenderDetailsPanel(USkeletalMeshComponent* SkelComp)
{
	ImGui::TextUnformatted("Animation");
	ImGui::Separator();
	ImGui::TextDisabled("Source");
	ImGui::TextWrapped("%s", AssetPath.empty() ? (Viewer ? Viewer->GetFileName().c_str() : "") : AssetPath.c_str());

	const bool bAssetAvailable = AnimSequence && AnimSequence->GetDataModel();

	if (!bAssetAvailable)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.20f, 1.0f), "Missing on disk");
	}
	if (!TargetSkeletalMeshPath.empty())
	{
		ImGui::TextDisabled("Preview Mesh");
		ImGui::TextWrapped("%s", TargetSkeletalMeshPath.c_str());
	}
	ImGui::Spacing();

	ImGui::TextUnformatted("Selection");
	if (Viewer && Viewer->GetSelectedBoneIndex() >= 0)
	{
		const int32 BoneIndex = Viewer->GetSelectedBoneIndex();
		FSkeletalMesh* MeshData = ResolveCurrentMeshData();
		const char* BoneName = MeshData && BoneIndex < static_cast<int32>(MeshData->Bones.size())
			? MeshData->Bones[BoneIndex].Name.c_str()
			: "Unknown";
		ImGui::Text("Bone %d", BoneIndex);
		ImGui::TextWrapped("%s", BoneName);

		if (SkelComp)
		{
			FMatrix Local = SkelComp->GetBoneLocalTransform(BoneIndex);
			FVector Translation;
			FVector Scale;
			FMatrix Rotation;
			Local.Decompose(Translation, Rotation, Scale);
			ImGui::Text("T %.2f %.2f %.2f", Translation.X, Translation.Y, Translation.Z);
			ImGui::Text("S %.2f %.2f %.2f", Scale.X, Scale.Y, Scale.Z);
		}
	}
	else
	{
		ImGui::TextDisabled("No bone selected.");
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::TextUnformatted("Notify");
	const TArray<FAnimNotifyTrack>& NotifyTracks = AnimSequence ? AnimSequence->GetNotifyTracks() : TArray<FAnimNotifyTrack>{};
	const bool bHasSelectedNotify = SelectedNotifyTrackIndex >= 0
		&& SelectedNotifyTrackIndex < static_cast<int32>(NotifyTracks.size())
		&& SelectedNotifyIndex >= 0
		&& SelectedNotifyIndex < static_cast<int32>(NotifyTracks[SelectedNotifyTrackIndex].Notifies.size());
	if (bHasSelectedNotify)
	{
		const FAnimNotifyTrack& Track = NotifyTracks[SelectedNotifyTrackIndex];
		const FAnimNotifyEvent& Notify = Track.Notifies[SelectedNotifyIndex];
		const float PlayLength = ResolvePlayLength();
		const float FramesPerSecond = ResolveFramesPerSecond(AnimSequence);
		const int32 FrameCount = ResolveFrameCount(AnimSequence, PlayLength, FramesPerSecond);
		const int32 LastFrame = std::max(0, FrameCount - 1);
		const int32 TimelineEndFrame = std::max(
			LastFrame,
			static_cast<int32>(std::round(std::max(0.0f, PlayLength) * FramesPerSecond)));
		const int32 TriggerFrame = TimeToFrame(Notify.TriggerTime, FramesPerSecond, LastFrame);
		const int32 DurationFrames = DurationToFrameCount(Notify.Duration, FramesPerSecond, TimelineEndFrame);
		const int32 EndFrame = std::clamp(TriggerFrame + DurationFrames, 0, TimelineEndFrame);
		ImGui::Text("Track: %s", Track.TrackName.ToString().c_str());
		ImGui::Text("Notify: %s", Notify.NotifyName.ToString().c_str());
		ImGui::Text("Time: %.3fs  Frame: %d", Notify.TriggerTime, TriggerFrame);
		ImGui::Text("Duration: %.3fs  Frames: %d", Notify.Duration, DurationFrames);
		if (DurationFrames > 0)
		{
			ImGui::Text("End: %.3fs  Frame: %d", Notify.TriggerTime + Notify.Duration, EndFrame);
		}

		if (SelectedNotifyNameBufferTrackIndex != SelectedNotifyTrackIndex
			|| SelectedNotifyNameBufferNotifyIndex != SelectedNotifyIndex)
		{
			std::snprintf(
				SelectedNotifyNameBuffer,
				sizeof(SelectedNotifyNameBuffer),
				"%s",
				Notify.NotifyName.ToString().c_str());
			SelectedNotifyNameBufferTrackIndex = SelectedNotifyTrackIndex;
			SelectedNotifyNameBufferNotifyIndex = SelectedNotifyIndex;
		}

		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::InputText("Name", SelectedNotifyNameBuffer, sizeof(SelectedNotifyNameBuffer)))
		{
			if (SelectedNotifyNameBuffer[0] != '\0'
				&& AnimSequence->SetNotifyName(
					SelectedNotifyTrackIndex,
					SelectedNotifyIndex,
					FName(FString(SelectedNotifyNameBuffer))))
			{
				SaveAnimationAsset();
			}
		}

		FString SelectedNotifyClassName = Notify.NotifyClassName;
		if (DrawAnimNotifyClassCombo("Class", SelectedNotifyClassName))
		{
			if (AnimSequence->SetNotifyClassName(SelectedNotifyTrackIndex, SelectedNotifyIndex, SelectedNotifyClassName))
			{
				const FString DisplayName = GetAnimNotifyClassDisplayName(SelectedNotifyClassName);
				if (!DisplayName.empty())
				{
					AnimSequence->SetNotifyName(SelectedNotifyTrackIndex, SelectedNotifyIndex, FName(DisplayName));
					std::snprintf(SelectedNotifyNameBuffer, sizeof(SelectedNotifyNameBuffer), "%s", DisplayName.c_str());
				}
				SaveAnimationAsset();
			}
		}

		if (IsLuaAnimNotifyClass(SelectedNotifyClassName))
		{
			if (SelectedNotifyLuaBufferTrackIndex != SelectedNotifyTrackIndex
				|| SelectedNotifyLuaBufferNotifyIndex != SelectedNotifyIndex)
			{
				std::snprintf(
					SelectedNotifyLuaEventNameBuffer,
					sizeof(SelectedNotifyLuaEventNameBuffer),
					"%s",
					Notify.LuaEventName.c_str());
				std::snprintf(
					SelectedNotifyLuaTargetScriptBuffer,
					sizeof(SelectedNotifyLuaTargetScriptBuffer),
					"%s",
					Notify.LuaTargetScript.c_str());
				SelectedNotifyLuaBufferTrackIndex = SelectedNotifyTrackIndex;
				SelectedNotifyLuaBufferNotifyIndex = SelectedNotifyIndex;
			}

			ImGui::SeparatorText("Lua Notify");
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::InputText("Lua Event", SelectedNotifyLuaEventNameBuffer, sizeof(SelectedNotifyLuaEventNameBuffer)))
			{
				if (AnimSequence->SetNotifyLuaEventName(
					SelectedNotifyTrackIndex,
					SelectedNotifyIndex,
					FString(SelectedNotifyLuaEventNameBuffer)))
				{
					SaveAnimationAsset();
				}
			}

			int32 TargetPolicy = std::clamp(Notify.LuaTargetPolicy, 0, 2);
			const char* CurrentPolicyLabel = GetLuaAnimNotifyTargetPolicyLabel(TargetPolicy);
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::BeginCombo("Target", CurrentPolicyLabel))
			{
				for (int32 PolicyIndex = 0; PolicyIndex < 3; ++PolicyIndex)
				{
					const bool bSelected = PolicyIndex == TargetPolicy;
					if (ImGui::Selectable(GetLuaAnimNotifyTargetPolicyLabel(PolicyIndex), bSelected))
					{
						TargetPolicy = PolicyIndex;
						if (AnimSequence->SetNotifyLuaTargetPolicy(
							SelectedNotifyTrackIndex,
							SelectedNotifyIndex,
							TargetPolicy))
						{
							SaveAnimationAsset();
						}
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			FString TargetScriptValue = SelectedNotifyLuaTargetScriptBuffer;
			if (TargetPolicy == static_cast<int32>(EAnimNotifyLuaTargetPolicy::NamedScript))
			{
				if (DrawLuaScriptCombo("Script", TargetScriptValue))
				{
					std::snprintf(
						SelectedNotifyLuaTargetScriptBuffer,
						sizeof(SelectedNotifyLuaTargetScriptBuffer),
						"%s",
						TargetScriptValue.c_str());
					if (AnimSequence->SetNotifyLuaTargetScript(
						SelectedNotifyTrackIndex,
						SelectedNotifyIndex,
						TargetScriptValue))
					{
						SaveAnimationAsset();
					}
				}

				ImGui::SetNextItemWidth(220.0f);
				if (ImGui::InputText("Script Ref", SelectedNotifyLuaTargetScriptBuffer, sizeof(SelectedNotifyLuaTargetScriptBuffer)))
				{
					if (AnimSequence->SetNotifyLuaTargetScript(
						SelectedNotifyTrackIndex,
						SelectedNotifyIndex,
						FString(SelectedNotifyLuaTargetScriptBuffer)))
					{
						SaveAnimationAsset();
					}
				}

				ImGui::BeginDisabled(SelectedNotifyLuaTargetScriptBuffer[0] == '\0');
				if (ImGui::Button("Add Lua Handler"))
				{
					FString TargetScript = SelectedNotifyLuaTargetScriptBuffer;
					FString Message;
					bool bAlreadyExists = false;
					FAnimNotifyStateEvent NotifyForAdd = Notify;
					NotifyForAdd.LuaEventName = SelectedNotifyLuaEventNameBuffer;
					NotifyForAdd.LuaTargetScript = TargetScript;
					NotifyForAdd.LuaTargetPolicy = static_cast<int32>(EAnimNotifyLuaTargetPolicy::NamedScript);
					if (AddLuaAnimNotifyHandlerStub(NotifyForAdd, TargetScript, Message, &bAlreadyExists))
					{
						std::snprintf(
							SelectedNotifyLuaTargetScriptBuffer,
							sizeof(SelectedNotifyLuaTargetScriptBuffer),
							"%s",
							TargetScript.c_str());
						AnimSequence->SetNotifyLuaTargetScript(SelectedNotifyTrackIndex, SelectedNotifyIndex, TargetScript);
						AnimSequence->SetNotifyLuaTargetPolicy(
							SelectedNotifyTrackIndex,
							SelectedNotifyIndex,
							static_cast<int32>(EAnimNotifyLuaTargetPolicy::NamedScript));
						SaveAnimationAsset();
						if (EditorEngine)
						{
							EditorEngine->GetNotificationService().Info(Message);
						}
					}
					else if (EditorEngine)
					{
						(bAlreadyExists ? EditorEngine->GetNotificationService().Info(Message) : EditorEngine->GetNotificationService().Warning(Message));
					}
				}
				ImGui::EndDisabled();
			}
		}

		bool bUseDuration = Notify.Duration > 0.0f;
		if (ImGui::Checkbox("Use Duration", &bUseDuration))
		{
			const int32 RemainingFrames = std::max(0, TimelineEndFrame - TriggerFrame);
			const int32 NewDurationFrames = bUseDuration && RemainingFrames > 0
				? std::clamp(DurationFrames > 0 ? DurationFrames : 1, 1, RemainingFrames)
				: 0;
			AnimSequence->SetNotifyTiming(
				SelectedNotifyTrackIndex,
				SelectedNotifyIndex,
				FrameToTime(TriggerFrame, FramesPerSecond, PlayLength),
				FrameCountToDuration(NewDurationFrames, FramesPerSecond, PlayLength));
			SaveAnimationAsset();
		}

		int32 EditableTriggerFrame = TriggerFrame;
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::DragInt("Trigger Frame", &EditableTriggerFrame, 0.1f, 0, LastFrame))
		{
			const int32 MaxTriggerFrame = std::max(0, TimelineEndFrame - DurationFrames);
			EditableTriggerFrame = std::clamp(EditableTriggerFrame, 0, MaxTriggerFrame);
			AnimSequence->SetNotifyTiming(
				SelectedNotifyTrackIndex,
				SelectedNotifyIndex,
				FrameToTime(EditableTriggerFrame, FramesPerSecond, PlayLength),
				FrameCountToDuration(DurationFrames, FramesPerSecond, PlayLength));
			SaveAnimationAsset();
		}

		float EditableTriggerTime = Notify.TriggerTime;
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::DragFloat("Trigger Time", &EditableTriggerTime, 0.001f, 0.0f, PlayLength, "%.3fs"))
		{
			const int32 NewTriggerFrame = std::clamp(static_cast<int32>(std::round(EditableTriggerTime * FramesPerSecond)), 0, std::max(0, TimelineEndFrame - DurationFrames));
			AnimSequence->SetNotifyTiming(
				SelectedNotifyTrackIndex,
				SelectedNotifyIndex,
				FrameToTime(NewTriggerFrame, FramesPerSecond, PlayLength),
				FrameCountToDuration(DurationFrames, FramesPerSecond, PlayLength));
			SaveAnimationAsset();
		}

		ImGui::BeginDisabled(!bUseDuration);
		int32 EditableDurationFrames = DurationFrames;
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::DragInt("Duration Frames", &EditableDurationFrames, 0.1f, 1, std::max(1, TimelineEndFrame - TriggerFrame)))
		{
			EditableDurationFrames = std::clamp(EditableDurationFrames, 1, std::max(1, TimelineEndFrame - TriggerFrame));
			AnimSequence->SetNotifyTiming(
				SelectedNotifyTrackIndex,
				SelectedNotifyIndex,
				FrameToTime(TriggerFrame, FramesPerSecond, PlayLength),
				FrameCountToDuration(EditableDurationFrames, FramesPerSecond, PlayLength));
			SaveAnimationAsset();
		}

		float EditableDuration = Notify.Duration;
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::DragFloat("Duration Time", &EditableDuration, 0.001f, 0.0f, std::max(0.0f, PlayLength - Notify.TriggerTime), "%.3fs"))
		{
			const int32 NewDurationFrames = std::clamp(static_cast<int32>(std::round(std::max(0.0f, EditableDuration) * FramesPerSecond)), 1, std::max(1, TimelineEndFrame - TriggerFrame));
			AnimSequence->SetNotifyTiming(
				SelectedNotifyTrackIndex,
				SelectedNotifyIndex,
				FrameToTime(TriggerFrame, FramesPerSecond, PlayLength),
				FrameCountToDuration(NewDurationFrames, FramesPerSecond, PlayLength));
			SaveAnimationAsset();
		}
		ImGui::EndDisabled();
	}
	else
	{
		ImGui::TextDisabled("No notify selected. Right-click Notify Track to add/delete.");
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::TextUnformatted("Curve");
	UAnimDataModel* DataModel = AnimSequence ? AnimSequence->GetDataModel() : nullptr;
	const TArray<FAnimCurveTrack>& CurveTracks = DataModel ? DataModel->GetCurveData().FloatCurves : TArray<FAnimCurveTrack>{};
	const bool bHasSelectedCurve = SelectedCurveTrackIndex >= 0
		&& SelectedCurveTrackIndex < static_cast<int32>(CurveTracks.size());
	if (bHasSelectedCurve)
	{
		const FAnimCurveTrack& CurveTrack = CurveTracks[SelectedCurveTrackIndex];
		ImGui::Text("Track: %s", CurveTrack.CurveName.ToString().c_str());
		ImGui::Text("Keys: %d", static_cast<int32>(CurveTrack.Curve.Keys.size()));
		float CurrentValue = 0.0f;
		if (AnimSequence && AnimSequence->EvaluateCurve(CurveTrack.CurveName, PreviewTime, CurrentValue))
		{
			ImGui::Text("Value @ Current Frame: %.3f", CurrentValue);
		}
		if (ImGui::Button("Open Curve Editor"))
		{
			OpenCurveTrackEditor(SelectedCurveTrackIndex);
		}
	}
	else
	{
		ImGui::TextDisabled("No curve selected. Right-click Curve Track to add/delete/open.");
	}
}

void FEditorAnimationSequenceViewerWidget::DrawBoneNode(
	int32 BoneIndex,
	const TArray<FBoneInfo>& Bones,
	const TArray<TArray<int32>>& InChildren)
{
	if (!Viewer || BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
	{
		return;
	}

	const bool bSelected = Viewer->GetSelectedBoneIndex() == BoneIndex;
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (bSelected)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (BoneIndex >= static_cast<int32>(InChildren.size()) || InChildren[BoneIndex].empty())
	{
		Flags |= ImGuiTreeNodeFlags_Leaf;
	}

	ImGui::PushID(BoneIndex);
	const bool bOpen = ImGui::TreeNodeEx(Bones[BoneIndex].Name.c_str(), Flags);
	if (ImGui::IsItemClicked())
	{
		Viewer->SelectBone(BoneIndex);
	}
	if (bOpen)
	{
		if (BoneIndex < static_cast<int32>(InChildren.size()))
		{
			for (int32 ChildIndex : InChildren[BoneIndex])
			{
				DrawBoneNode(ChildIndex, Bones, InChildren);
			}
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void FEditorAnimationSequenceViewerWidget::RebuildBoneTreeCaches(const FSkeletalMesh* MeshData)
{
	Children.clear();
	if (!MeshData)
	{
		return;
	}

	Children.resize(MeshData->Bones.size());
	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshData->Bones.size()); ++BoneIndex)
	{
		const int32 ParentIndex = MeshData->Bones[BoneIndex].ParentIndex;
		if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(MeshData->Bones.size()))
		{
			Children[ParentIndex].push_back(BoneIndex);
		}
	}
}

FSkeletalMesh* FEditorAnimationSequenceViewerWidget::ResolveCurrentMeshData(UDebugSkelMeshComponent** OutSkelComp) const
{
	if (OutSkelComp)
	{
		*OutSkelComp = nullptr;
	}

	if (!Viewer)
	{
		return nullptr;
	}

	ASkeletalMeshActor* ViewTarget = Viewer->GetViewTarget();
	UDebugSkelMeshComponent* SkelComp = Cast<UDebugSkelMeshComponent>(
		ViewTarget ? ViewTarget->GetSkeletalMeshComponent() : nullptr);
	if (OutSkelComp)
	{
		*OutSkelComp = SkelComp;
	}

	USkeletalMesh* Mesh = SkelComp ? SkelComp->GetSkeletalMesh() : nullptr;
	return Mesh ? Mesh->GetMeshData() : nullptr;
}

float FEditorAnimationSequenceViewerWidget::ResolvePlayLength() const
{
	UAnimSequenceBase* Sequence = AnimSequence;
	const float SequenceLength = Sequence ? Sequence->GetPlayLength() : 0.0f;
	return std::max(0.001f, SequenceLength > 0.0f ? SequenceLength : 1.0f);
}

UAnimPreviewInstance* FEditorAnimationSequenceViewerWidget::EnsurePreviewInstance(UDebugSkelMeshComponent* SkelComp)
{
	if (!SkelComp || !AnimSequence || !AnimSequence->GetDataModel())
	{
		ReleasePreviewInstance();
		return nullptr;
	}

	if (PreviewInstanceComponent && PreviewInstanceComponent != SkelComp)
	{
		ReleasePreviewInstance();
	}

	SkelComp->SetPreviewAnimation(AnimSequence);
	PreviewInstanceComponent = SkelComp;
	return SkelComp->GetPreviewAnimInstance();
}

void FEditorAnimationSequenceViewerWidget::ReleasePreviewInstance()
{
	if (PreviewInstanceComponent)
	{
		PreviewInstanceComponent->ClearPreviewAnimation();
	}
	PreviewInstanceComponent = nullptr;
}

bool FEditorAnimationSequenceViewerWidget::SyncPreviewInstance(UDebugSkelMeshComponent* SkelComp)
{
	UAnimPreviewInstance* Preview = EnsurePreviewInstance(SkelComp);
	if (!Preview)
	{
		return false;
	}

	Preview->SyncPreviewPlayback(bPreviewPlaying, bPreviewLooping, bPreviewReverse, PreviewPlayRate);
	if (std::fabs(Preview->GetCurrentAnimTime() - PreviewTime) > 0.0001f)
	{
		SkelComp->SetPreviewPosition(PreviewTime);
	}
	return true;
}

void FEditorAnimationSequenceViewerWidget::SetPreviewFrame(
	UDebugSkelMeshComponent* SkelComp,
	int32 Frame,
	float FramesPerSecond,
	float PlayLength,
	int32 LastFrame)
{
	const int32 ClampedFrame = std::clamp(Frame, 0, LastFrame);
	PreviewTime = FrameToTime(ClampedFrame, FramesPerSecond, PlayLength);
	SeekPreviewTime(SkelComp, PreviewTime);
}

void FEditorAnimationSequenceViewerWidget::StepPreviewFrame(
	UDebugSkelMeshComponent* SkelComp,
	int32 FrameDelta,
	float FramesPerSecond,
	float PlayLength,
	int32 LastFrame)
{
	const int32 CurrentFrame = TimeToFrame(PreviewTime, FramesPerSecond, LastFrame);
	SetPreviewFrame(SkelComp, CurrentFrame + FrameDelta, FramesPerSecond, PlayLength, LastFrame);
}

bool FEditorAnimationSequenceViewerWidget::SeekPreviewTime(UDebugSkelMeshComponent* SkelComp, float TimeSeconds)
{
	UAnimPreviewInstance* Preview = EnsurePreviewInstance(SkelComp);
	if (!Preview)
	{
		return false;
	}

	PreviewTime = TimeSeconds;
	Preview->SyncPreviewPlayback(bPreviewPlaying, bPreviewLooping, bPreviewReverse, PreviewPlayRate);
	return SkelComp->SetPreviewPosition(PreviewTime);
}

bool FEditorAnimationSequenceViewerWidget::SaveAnimationAsset()
{
	if (!AnimSequence || !AnimSequence->GetDataModel() || AssetPath.empty())
	{
		return false;
	}

	FAssetMetaData MetaData;
	if (!FAssetFile::LoadMetadataOnly(AssetPath, MetaData))
	{
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Error("Failed to read AnimSequence metadata.");
		}
		return false;
	}

	if (MetaData.ClassName != UAnimSequence::StaticClass()->GetName())
	{
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Error("Selected asset is not an AnimSequence.");
		}
		return false;
	}

	FAnimSequenceAssetPayload ExistingPayload;
	FAssetFile::Load(AssetPath, MetaData, [&](FArchive& Ar)
	{
		ExistingPayload.Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});

	FAnimSequenceAssetPayload Payload;
	Payload.TargetSkeletalMeshPath = TargetSkeletalMeshPath.empty()
		? ExistingPayload.TargetSkeletalMeshPath
		: TargetSkeletalMeshPath;
	Payload.SourceAnimStackIndex = ExistingPayload.SourceAnimStackIndex;
	Payload.DataModel = AnimSequence->GetDataModel();
	Payload.NotifyTracks = AnimSequence->GetNotifyTracks();

	const bool bSaved = FAssetFile::Save(AssetPath, MetaData, [&](FArchive& Ar)
	{
		Payload.Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});

	if (!bSaved)
	{
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Error("Failed to save AnimSequence data.");
		}
		return false;
	}

	if (EditorEngine)
	{
		EditorEngine->GetNotificationService().Info("Saved AnimSequence data.");
	}
	return true;
}
