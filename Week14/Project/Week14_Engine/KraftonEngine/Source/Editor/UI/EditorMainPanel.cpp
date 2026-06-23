#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewport/Level/LevelEditorViewportClient.h"
#include "Render/Types/MinimalViewInfo.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "Component/ActorSequenceComponent.h"
#include "Object/Object.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Platform/WindowsWindow.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include <d3d11.h>

#include "Render/Pipeline/Renderer.h"
#include "Engine/Input/InputSystem.h"
#include "Lua/LuaDebugManager.h"
#include "LuaBlueprint/LuaBlueprintAsset.h"
#include "LuaBlueprint/LuaBlueprintManager.h"
#include "UI/UIManager.h"
#include "UI/UserWidget.h"

#include "Editor/Slate/SlateApplication.h"
#include "Editor/UI/Util/ImGuiSetting.h"
#include "Editor/UI/Util/EditorTextureManager.h"
#include "Editor/UI/Util/NotificationToast.h"

#include "Editor/UI/Asset/Curve/FloatCurveEditorWidget.h"
#include "Editor/UI/Asset/CameraShake/CameraShakeEditorWidget.h"
#include "Editor/UI/Asset/ActorSequence/ActorSequenceEditorWidget.h"
#include "Editor/UI/Asset/Mesh/MeshEditorWidget.h"
#include "Editor/UI/Asset/Mesh/StaticMeshEditorWidget.h"
#include "Editor/UI/Asset/Animation/AnimGraphEditorWidget.h"
#include "Editor/UI/Asset/LuaBlueprint/LuaBlueprintEditorWidget.h"
#include "Editor/UI/Asset/Material/MaterialEditorWidget.h"
#include "Editor/UI/Asset/Physics/PhysicsAssetEditorWidget.h"
#include "Editor/UI/Asset/RuntimeUI/RuntimeUILayoutEditorWidget.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "Asset/Particle/ParticleEditorWidget.h"

namespace
{
struct FDebugPlaceActorOption
{
	const char* Label = "";
	FLevelViewportLayout::EViewportPlaceActorType Type = FLevelViewportLayout::EViewportPlaceActorType::Cube;
};

const FDebugPlaceActorOption GDebugPlaceActorOptions[] = {
	{ "Empty Actor", FLevelViewportLayout::EViewportPlaceActorType::EmptyActor },
	{ "Cube", FLevelViewportLayout::EViewportPlaceActorType::Cube },
	{ "Sphere", FLevelViewportLayout::EViewportPlaceActorType::Sphere },
	{ "Cylinder", FLevelViewportLayout::EViewportPlaceActorType::Cylinder },
	{ "Decal", FLevelViewportLayout::EViewportPlaceActorType::Decal },
	{ "Height Fog", FLevelViewportLayout::EViewportPlaceActorType::HeightFog },
	{ "Ambient Light", FLevelViewportLayout::EViewportPlaceActorType::AmbientLight },
	{ "Directional Light", FLevelViewportLayout::EViewportPlaceActorType::DirectionalLight },
	{ "Point Light", FLevelViewportLayout::EViewportPlaceActorType::PointLight },
	{ "Spot Light", FLevelViewportLayout::EViewportPlaceActorType::SpotLight },
	{ "Character",     FLevelViewportLayout::EViewportPlaceActorType::Character },
	{ "Lua Character", FLevelViewportLayout::EViewportPlaceActorType::LuaCharacter },
	{ "Sniper Pawn", FLevelViewportLayout::EViewportPlaceActorType::SniperPawn },
	{ "Wheeled Vehicle", FLevelViewportLayout::EViewportPlaceActorType::WheeledVehicle },
};

constexpr float GDocumentTabStripHeight = 34.0f;
constexpr float GDocumentTabWidth = 180.0f;
constexpr float GDocumentTabRightPadding = 8.0f;

struct FDocumentTabVisual
{
	ImVec4 AccentColor;
	const char* Tooltip = "";
};

FDocumentTabVisual GetDocumentTabVisual(EEditorDocumentTabKind Kind)
{
	switch (Kind)
	{
	case EEditorDocumentTabKind::LevelEditor:
		return { ImVec4(0.96f, 0.67f, 0.16f, 1.0f), "Level Scene" };
	case EEditorDocumentTabKind::StaticMeshEditor:
		return { ImVec4(0.22f, 0.78f, 0.45f, 1.0f), "Static Mesh Editor" };
	case EEditorDocumentTabKind::SkeletalMeshEditor:
		return { ImVec4(0.18f, 0.70f, 0.95f, 1.0f), "Skeletal Mesh Editor" };
	case EEditorDocumentTabKind::ParticleEditor:
		return { ImVec4(0.72f, 0.42f, 0.95f, 1.0f), "Particle Editor" };
	case EEditorDocumentTabKind::AnimGraphEditor:
		return { ImVec4(0.86f, 0.58f, 0.22f, 1.0f), "Anim Graph Editor" };
	case EEditorDocumentTabKind::LuaBlueprintEditor:
		return { ImVec4(0.36f, 0.52f, 0.94f, 1.0f), "Lua Blueprint Editor" };
	case EEditorDocumentTabKind::PhysicsAssetEditor:
		return { ImVec4(0.84f, 0.38f, 0.30f, 1.0f), "Physics Asset Editor" };
    case EEditorDocumentTabKind::MaterialEditor:
        return { ImVec4(0.92f, 0.44f, 0.24f, 1.0f), "Material Graph Editor" };
	case EEditorDocumentTabKind::ActorSequencer:
		return { ImVec4(0.64f, 0.74f, 0.98f, 1.0f), "Actor Sequencer" };
	case EEditorDocumentTabKind::RuntimeUIPreview:
		return { ImVec4(0.38f, 0.82f, 0.78f, 1.0f), "Runtime UI Preview" };
	case EEditorDocumentTabKind::RuntimeUILayoutEditor:
		return { ImVec4(0.24f, 0.72f, 0.86f, 1.0f), "Runtime UI Layout Editor" };
	case EEditorDocumentTabKind::Unsupported:
	default:
		return { ImVec4(0.58f, 0.62f, 0.70f, 1.0f), "Editor Tab" };
	}
}

const char* GetDocumentTabKindName(EEditorDocumentTabKind Kind)
{
	switch (Kind)
	{
	case EEditorDocumentTabKind::LevelEditor: return "Level";
	case EEditorDocumentTabKind::StaticMeshEditor: return "StaticMesh";
	case EEditorDocumentTabKind::SkeletalMeshEditor: return "SkeletalMesh";
	case EEditorDocumentTabKind::ParticleEditor: return "Particle";
	case EEditorDocumentTabKind::AnimGraphEditor: return "AnimGraph";
	case EEditorDocumentTabKind::LuaBlueprintEditor: return "LuaBlueprint";
	case EEditorDocumentTabKind::PhysicsAssetEditor: return "PhysicsAsset";
    case EEditorDocumentTabKind::MaterialEditor:
        return "Material";
	case EEditorDocumentTabKind::ActorSequencer:
		return "ActorSequencer";
	case EEditorDocumentTabKind::RuntimeUIPreview:
		return "RuntimeUI";
	case EEditorDocumentTabKind::RuntimeUILayoutEditor:
		return "RuntimeUILayout";
	case EEditorDocumentTabKind::Unsupported:
	default:
		return "Unsupported";
	}
}

FString MakeDocumentTabImGuiId(const FEditorDocumentTabEntry& Tab)
{
	return Tab.Label + "###DocumentTab_" + GetDocumentTabKindName(Tab.Id.Kind) + "_" + Tab.Id.PayloadId;
}

FString GetFileStemForDisplay(const FString& Path)
{
	if (Path.empty())
	{
		return FString();
	}

	std::filesystem::path FilePath(FPaths::ToWide(Path));
	FString Stem = FPaths::ToUtf8(FilePath.stem().wstring());
	if (!Stem.empty())
	{
		return Stem;
	}

	return FPaths::ToUtf8(FilePath.filename().wstring());
}

FString GetFileNameForDisplay(const FString& Path)
{
	if (Path.empty())
	{
		return FString();
	}

	std::filesystem::path FilePath(FPaths::ToWide(Path));
	return FPaths::ToUtf8(FilePath.filename().wstring());
}

void PushUniqueRuntimeUIValue(TArray<FString>& Values, const FString& Value)
{
	if (Value.empty())
	{
		return;
	}
	if (std::find(Values.begin(), Values.end(), Value) == Values.end())
	{
		Values.push_back(Value);
	}
}

void CollectRuntimeUIAttributeValues(const FString& Source, const char* AttributeName, TArray<FString>& OutValues)
{
	if (!AttributeName || AttributeName[0] == '\0')
	{
		return;
	}

	const FString Needle(AttributeName);
	size_t SearchPos = 0;
	while (SearchPos < Source.size())
	{
		const size_t AttrPos = Source.find(Needle, SearchPos);
		if (AttrPos == FString::npos)
		{
			break;
		}

		const size_t Before = AttrPos > 0 ? AttrPos - 1 : AttrPos;
		if (AttrPos > 0)
		{
			const char Prev = Source[Before];
			if ((Prev >= 'A' && Prev <= 'Z') || (Prev >= 'a' && Prev <= 'z') || Prev == '-' || Prev == '_')
			{
				SearchPos = AttrPos + Needle.size();
				continue;
			}
		}

		size_t Cursor = AttrPos + Needle.size();
		while (Cursor < Source.size() && (Source[Cursor] == ' ' || Source[Cursor] == '\t' || Source[Cursor] == '\r' || Source[Cursor] == '\n'))
		{
			++Cursor;
		}
		if (Cursor >= Source.size() || Source[Cursor] != '=')
		{
			SearchPos = Cursor;
			continue;
		}
		++Cursor;
		while (Cursor < Source.size() && (Source[Cursor] == ' ' || Source[Cursor] == '\t' || Source[Cursor] == '\r' || Source[Cursor] == '\n'))
		{
			++Cursor;
		}
		if (Cursor >= Source.size() || (Source[Cursor] != '"' && Source[Cursor] != '\''))
		{
			SearchPos = Cursor;
			continue;
		}

		const char Quote = Source[Cursor++];
		const size_t ValueStart = Cursor;
		while (Cursor < Source.size() && Source[Cursor] != Quote)
		{
			++Cursor;
		}
		if (Cursor < Source.size())
		{
			PushUniqueRuntimeUIValue(OutValues, Source.substr(ValueStart, Cursor - ValueStart));
		}
		SearchPos = Cursor < Source.size() ? Cursor + 1 : Cursor;
	}
}

FString TrimRuntimeUIPreviewText(const FString& Value)
{
	size_t Begin = 0;
	size_t End = Value.size();
	while (Begin < End && std::isspace(static_cast<unsigned char>(Value[Begin]))) ++Begin;
	while (End > Begin && std::isspace(static_cast<unsigned char>(Value[End - 1]))) --End;
	return Value.substr(Begin, End - Begin);
}

FString CollapseRuntimeUIPreviewWhitespace(const FString& Value)
{
	FString Result;
	bool bWasSpace = false;
	for (char Ch : Value)
	{
		const bool bIsSpace = std::isspace(static_cast<unsigned char>(Ch)) != 0;
		if (bIsSpace)
		{
			if (!bWasSpace && !Result.empty())
			{
				Result.push_back(' ');
			}
			bWasSpace = true;
			continue;
		}
		Result.push_back(Ch);
		bWasSpace = false;
	}
	return TrimRuntimeUIPreviewText(Result);
}

FString ToLowerRuntimeUIPreviewText(FString Value)
{
	for (char& Ch : Value)
	{
		Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
	}
	return Value;
}

bool RuntimeUIPreviewStartsWith(const FString& Value, const char* Prefix)
{
	const size_t PrefixLength = Prefix ? std::strlen(Prefix) : 0;
	return PrefixLength > 0 && Value.size() >= PrefixLength && Value.compare(0, PrefixLength, Prefix) == 0;
}

TArray<FString> SplitRuntimeUIPreviewText(const FString& Value, char Separator)
{
	TArray<FString> Result;
	size_t Start = 0;
	while (Start <= Value.size())
	{
		size_t End = Value.find(Separator, Start);
		if (End == FString::npos)
		{
			End = Value.size();
		}

		FString Token = TrimRuntimeUIPreviewText(Value.substr(Start, End - Start));
		if (!Token.empty())
		{
			Result.push_back(Token);
		}

		if (End == Value.size())
		{
			break;
		}
		Start = End + 1;
	}
	return Result;
}

float RuntimeUIPreviewMin(float A, float B)
{
	return A < B ? A : B;
}

float RuntimeUIPreviewMax(float A, float B)
{
	return A > B ? A : B;
}

bool ParseRuntimeUIPreviewFloat(const FString& Value, float& OutValue)
{
	FString Trimmed = TrimRuntimeUIPreviewText(Value);
	if (Trimmed.empty() || Trimmed.find('%') != FString::npos)
	{
		return false;
	}

	char* End = nullptr;
	const float Parsed = std::strtof(Trimmed.c_str(), &End);
	if (End == Trimmed.c_str())
	{
		return false;
	}

	OutValue = Parsed;
	return true;
}

bool ParseRuntimeUIPreviewPercent(const FString& Value, float& OutValue)
{
	FString Trimmed = TrimRuntimeUIPreviewText(Value);
	if (Trimmed.empty() || Trimmed.back() != '%')
	{
		return false;
	}

	Trimmed.pop_back();
	float Percent = 0.0f;
	if (!ParseRuntimeUIPreviewFloat(Trimmed, Percent))
	{
		return false;
	}

	OutValue = Percent * 0.01f;
	return true;
}

bool ParseRuntimeUIPreviewColor(const FString& RawValue, ImVec4& OutColor)
{
	FString Value = ToLowerRuntimeUIPreviewText(TrimRuntimeUIPreviewText(RawValue));
	if (Value.empty())
	{
		return false;
	}

	if (Value[0] == '#')
	{
		if (Value.size() == 4)
		{
			const int R = std::strtol(FString(2, Value[1]).c_str(), nullptr, 16);
			const int G = std::strtol(FString(2, Value[2]).c_str(), nullptr, 16);
			const int B = std::strtol(FString(2, Value[3]).c_str(), nullptr, 16);
			OutColor = ImVec4(R / 255.0f, G / 255.0f, B / 255.0f, 1.0f);
			return true;
		}
		if (Value.size() >= 7)
		{
			const int R = std::strtol(Value.substr(1, 2).c_str(), nullptr, 16);
			const int G = std::strtol(Value.substr(3, 2).c_str(), nullptr, 16);
			const int B = std::strtol(Value.substr(5, 2).c_str(), nullptr, 16);
			OutColor = ImVec4(R / 255.0f, G / 255.0f, B / 255.0f, 1.0f);
			return true;
		}
		return false;
	}

	const bool bRgba = RuntimeUIPreviewStartsWith(Value, "rgba(");
	const bool bRgb = RuntimeUIPreviewStartsWith(Value, "rgb(");
	if (!bRgba && !bRgb)
	{
		return false;
	}

	const size_t Open = Value.find('(');
	const size_t Close = Value.rfind(')');
	if (Open == FString::npos || Close == FString::npos || Close <= Open)
	{
		return false;
	}

	TArray<FString> Parts = SplitRuntimeUIPreviewText(Value.substr(Open + 1, Close - Open - 1), ',');
	if (Parts.size() < 3)
	{
		return false;
	}

	float R = 0.0f;
	float G = 0.0f;
	float B = 0.0f;
	float A = 1.0f;
	if (!ParseRuntimeUIPreviewFloat(Parts[0], R) ||
		!ParseRuntimeUIPreviewFloat(Parts[1], G) ||
		!ParseRuntimeUIPreviewFloat(Parts[2], B))
	{
		return false;
	}
	if (Parts.size() >= 4)
	{
		ParseRuntimeUIPreviewFloat(Parts[3], A);
	}

	if (R > 1.0f || G > 1.0f || B > 1.0f)
	{
		R /= 255.0f;
		G /= 255.0f;
		B /= 255.0f;
	}
	if (A > 1.0f)
	{
		A /= 255.0f;
	}

	OutColor = ImVec4(R, G, B, A);
	return true;
}

struct FRuntimeUIPreviewStyle
{
	bool bHasLeft = false;
	bool bHasTop = false;
	bool bHasWidth = false;
	bool bHasHeight = false;
	bool bHasLeftPercent = false;
	bool bHasTopPercent = false;
	bool bHasWidthPercent = false;
	bool bHasHeightPercent = false;
	bool bHasRight = false;
	bool bHasBottom = false;
	bool bHasPadding = false;
	bool bHasMarginLeft = false;
	bool bHasMarginTop = false;
	bool bHasMarginBottom = false;
	bool bHasMarginRight = false;
	bool bHasOpacity = false;
	bool bHasDisplay = false;
	bool bHasJustifyContent = false;
	bool bHasAlignItems = false;
	bool bHasBackgroundColor = false;
	bool bHasBorderColor = false;
	bool bHasTextColor = false;
	bool bHasBorderWidth = false;
	bool bHasBorderRadius = false;
	bool bHasFontSize = false;
	bool bHasObjectFit = false;

	float Left = 0.0f;
	float Top = 0.0f;
	float Width = 0.0f;
	float Height = 0.0f;
	float LeftPercent = 0.0f;
	float TopPercent = 0.0f;
	float WidthPercent = 0.0f;
	float HeightPercent = 0.0f;
	float Right = 0.0f;
	float Bottom = 0.0f;
	float Padding = 0.0f;
	float MarginLeft = 0.0f;
	float MarginTop = 0.0f;
	float MarginBottom = 0.0f;
	float MarginRight = 0.0f;
	float Opacity = 1.0f;
	FString Display;
	FString JustifyContent;
	FString AlignItems;
	FString ObjectFit;
	float BorderWidth = 0.0f;
	float BorderRadius = 0.0f;
	float FontSize = 14.0f;
	ImVec4 BackgroundColor = ImVec4(0.13f, 0.15f, 0.18f, 0.94f);
	ImVec4 BorderColor = ImVec4(0.38f, 0.58f, 0.78f, 0.78f);
	ImVec4 TextColor = ImVec4(0.88f, 0.92f, 0.96f, 1.0f);
};

struct FRuntimeUIPreviewStyleSheet
{
	std::unordered_map<FString, FRuntimeUIPreviewStyle> IdStyles;
	std::unordered_map<FString, FRuntimeUIPreviewStyle> ClassStyles;
	std::unordered_map<FString, FRuntimeUIPreviewStyle> TagStyles;
};

struct FRuntimeUIPreviewNode
{
	FString Id;
	FString TagName;
	FString ClassName;
	FString Text;
	FString Action;
	FString ImageSource;
	bool bGeneratedId = false;
	int32 ParentIndex = -1;
	FRuntimeUIPreviewStyle Style;
};

struct FRuntimeUIPreviewModel
{
	TArray<FRuntimeUIPreviewNode> Nodes;
	ImVec2 CanvasSize = ImVec2(1920.0f, 1080.0f);
};

void MergeRuntimeUIPreviewStyle(FRuntimeUIPreviewStyle& Target, const FRuntimeUIPreviewStyle& Source)
{
	if (Source.bHasLeft) { Target.Left = Source.Left; Target.bHasLeft = true; }
	if (Source.bHasTop) { Target.Top = Source.Top; Target.bHasTop = true; }
	if (Source.bHasWidth) { Target.Width = Source.Width; Target.bHasWidth = true; }
	if (Source.bHasHeight) { Target.Height = Source.Height; Target.bHasHeight = true; }
	if (Source.bHasLeftPercent) { Target.LeftPercent = Source.LeftPercent; Target.bHasLeftPercent = true; }
	if (Source.bHasTopPercent) { Target.TopPercent = Source.TopPercent; Target.bHasTopPercent = true; }
	if (Source.bHasWidthPercent) { Target.WidthPercent = Source.WidthPercent; Target.bHasWidthPercent = true; }
	if (Source.bHasHeightPercent) { Target.HeightPercent = Source.HeightPercent; Target.bHasHeightPercent = true; }
	if (Source.bHasRight) { Target.Right = Source.Right; Target.bHasRight = true; }
	if (Source.bHasBottom) { Target.Bottom = Source.Bottom; Target.bHasBottom = true; }
	if (Source.bHasPadding) { Target.Padding = Source.Padding; Target.bHasPadding = true; }
	if (Source.bHasMarginLeft) { Target.MarginLeft = Source.MarginLeft; Target.bHasMarginLeft = true; }
	if (Source.bHasMarginTop) { Target.MarginTop = Source.MarginTop; Target.bHasMarginTop = true; }
	if (Source.bHasMarginBottom) { Target.MarginBottom = Source.MarginBottom; Target.bHasMarginBottom = true; }
	if (Source.bHasMarginRight) { Target.MarginRight = Source.MarginRight; Target.bHasMarginRight = true; }
	if (Source.bHasOpacity) { Target.Opacity = Source.Opacity; Target.bHasOpacity = true; }
	if (Source.bHasDisplay) { Target.Display = Source.Display; Target.bHasDisplay = true; }
	if (Source.bHasJustifyContent) { Target.JustifyContent = Source.JustifyContent; Target.bHasJustifyContent = true; }
	if (Source.bHasAlignItems) { Target.AlignItems = Source.AlignItems; Target.bHasAlignItems = true; }
	if (Source.bHasBackgroundColor) { Target.BackgroundColor = Source.BackgroundColor; Target.bHasBackgroundColor = true; }
	if (Source.bHasBorderColor) { Target.BorderColor = Source.BorderColor; Target.bHasBorderColor = true; }
	if (Source.bHasTextColor) { Target.TextColor = Source.TextColor; Target.bHasTextColor = true; }
	if (Source.bHasBorderWidth) { Target.BorderWidth = Source.BorderWidth; Target.bHasBorderWidth = true; }
	if (Source.bHasBorderRadius) { Target.BorderRadius = Source.BorderRadius; Target.bHasBorderRadius = true; }
	if (Source.bHasFontSize) { Target.FontSize = Source.FontSize; Target.bHasFontSize = true; }
	if (Source.bHasObjectFit) { Target.ObjectFit = Source.ObjectFit; Target.bHasObjectFit = true; }
}

void ApplyRuntimeUIPreviewStyleProperty(FRuntimeUIPreviewStyle& Style, const FString& RawName, const FString& RawValue)
{
	const FString Name = ToLowerRuntimeUIPreviewText(TrimRuntimeUIPreviewText(RawName));
	const FString Value = TrimRuntimeUIPreviewText(RawValue);
	float Number = 0.0f;
	if (Name == "left" && ParseRuntimeUIPreviewPercent(Value, Number))
	{
		Style.LeftPercent = Number;
		Style.bHasLeftPercent = true;
	}
	else if (Name == "left" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.Left = Number;
		Style.bHasLeft = true;
	}
	else if (Name == "top" && ParseRuntimeUIPreviewPercent(Value, Number))
	{
		Style.TopPercent = Number;
		Style.bHasTopPercent = true;
	}
	else if (Name == "top" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.Top = Number;
		Style.bHasTop = true;
	}
	else if (Name == "width" && ParseRuntimeUIPreviewPercent(Value, Number))
	{
		Style.WidthPercent = Number;
		Style.bHasWidthPercent = true;
	}
	else if (Name == "width" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.Width = Number;
		Style.bHasWidth = true;
	}
	else if (Name == "height" && ParseRuntimeUIPreviewPercent(Value, Number))
	{
		Style.HeightPercent = Number;
		Style.bHasHeightPercent = true;
	}
	else if (Name == "height" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.Height = Number;
		Style.bHasHeight = true;
	}
	else if (Name == "right" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.Right = Number;
		Style.bHasRight = true;
	}
	else if (Name == "bottom" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.Bottom = Number;
		Style.bHasBottom = true;
	}
	else if (Name == "padding" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.Padding = Number;
		Style.bHasPadding = true;
	}
	else if (Name == "margin" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.MarginLeft = Number;
		Style.MarginTop = Number;
		Style.MarginBottom = Number;
		Style.MarginRight = Number;
		Style.bHasMarginTop = true;
		Style.bHasMarginBottom = true;
		Style.bHasMarginRight = true;
		Style.bHasMarginLeft = true;
	}
	else if (Name == "margin-left" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.MarginLeft = Number;
		Style.bHasMarginLeft = true;
	}
	else if (Name == "margin-top" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.MarginTop = Number;
		Style.bHasMarginTop = true;
	}
	else if (Name == "margin-bottom" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.MarginBottom = Number;
		Style.bHasMarginBottom = true;
	}
	else if (Name == "margin-right" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.MarginRight = Number;
		Style.bHasMarginRight = true;
	}
	else if (Name == "opacity" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.Opacity = RuntimeUIPreviewMax(0.0f, RuntimeUIPreviewMin(Number, 1.0f));
		Style.bHasOpacity = true;
	}
	else if (Name == "display")
	{
		Style.Display = ToLowerRuntimeUIPreviewText(Value);
		Style.bHasDisplay = true;
	}
	else if (Name == "justify-content")
	{
		Style.JustifyContent = ToLowerRuntimeUIPreviewText(Value);
		Style.bHasJustifyContent = true;
	}
	else if (Name == "align-items")
	{
		Style.AlignItems = ToLowerRuntimeUIPreviewText(Value);
		Style.bHasAlignItems = true;
	}
	else if ((Name == "background" || Name == "background-color") && ParseRuntimeUIPreviewColor(Value, Style.BackgroundColor))
	{
		Style.bHasBackgroundColor = true;
	}
	else if (Name == "border-color" && ParseRuntimeUIPreviewColor(Value, Style.BorderColor))
	{
		Style.bHasBorderColor = true;
	}
	else if (Name == "color" && ParseRuntimeUIPreviewColor(Value, Style.TextColor))
	{
		Style.bHasTextColor = true;
	}
	else if (Name == "border-width" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.BorderWidth = Number;
		Style.bHasBorderWidth = true;
	}
	else if (Name == "border-radius" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.BorderRadius = Number;
		Style.bHasBorderRadius = true;
	}
	else if (Name == "font-size" && ParseRuntimeUIPreviewFloat(Value, Number))
	{
		Style.FontSize = Number;
		Style.bHasFontSize = true;
	}
	else if (Name == "object-fit")
	{
		Style.ObjectFit = ToLowerRuntimeUIPreviewText(Value);
		if (Style.ObjectFit == "fill")
		{
			Style.ObjectFit = "stretch";
		}
		Style.bHasObjectFit = true;
	}
}

FRuntimeUIPreviewStyle ParseRuntimeUIPreviewStyleBlock(const FString& Block)
{
	FRuntimeUIPreviewStyle Style;
	size_t Start = 0;
	while (Start < Block.size())
	{
		size_t End = Block.find(';', Start);
		if (End == FString::npos)
		{
			End = Block.size();
		}

		const FString Declaration = Block.substr(Start, End - Start);
		const size_t Colon = Declaration.find(':');
		if (Colon != FString::npos)
		{
			ApplyRuntimeUIPreviewStyleProperty(Style, Declaration.substr(0, Colon), Declaration.substr(Colon + 1));
		}

		if (End == Block.size())
		{
			break;
		}
		Start = End + 1;
	}
	return Style;
}

void StoreRuntimeUIPreviewSelectorStyle(
	FRuntimeUIPreviewStyleSheet& StyleSheet,
	const FString& RawSelector,
	const FRuntimeUIPreviewStyle& Style)
{
	FString Selector = TrimRuntimeUIPreviewText(RawSelector);
	if (Selector.empty())
	{
		return;
	}

	const size_t Pseudo = Selector.find(':');
	if (Pseudo != FString::npos)
	{
		Selector = Selector.substr(0, Pseudo);
	}
	const size_t LastSpace = Selector.find_last_of(" \t\r\n");
	if (LastSpace != FString::npos)
	{
		Selector = Selector.substr(LastSpace + 1);
	}

	Selector = ToLowerRuntimeUIPreviewText(TrimRuntimeUIPreviewText(Selector));
	if (Selector.empty())
	{
		return;
	}

	if (Selector[0] == '#')
	{
		FString Id = Selector.substr(1);
		MergeRuntimeUIPreviewStyle(StyleSheet.IdStyles[Id], Style);
	}
	else if (Selector[0] == '.')
	{
		FString ClassName = Selector.substr(1);
		MergeRuntimeUIPreviewStyle(StyleSheet.ClassStyles[ClassName], Style);
	}
	else
	{
		MergeRuntimeUIPreviewStyle(StyleSheet.TagStyles[Selector], Style);
	}
}

FRuntimeUIPreviewStyleSheet ParseRuntimeUIPreviewStyleSheet(const FString& Source)
{
	FRuntimeUIPreviewStyleSheet StyleSheet;
	size_t SearchPos = 0;
	while (SearchPos < Source.size())
	{
		const size_t StyleOpen = Source.find("<style", SearchPos);
		if (StyleOpen == FString::npos)
		{
			break;
		}
		const size_t StyleOpenEnd = Source.find('>', StyleOpen);
		const size_t StyleClose = Source.find("</style>", StyleOpenEnd == FString::npos ? StyleOpen : StyleOpenEnd);
		if (StyleOpenEnd == FString::npos || StyleClose == FString::npos)
		{
			break;
		}

		const FString StyleSource = Source.substr(StyleOpenEnd + 1, StyleClose - StyleOpenEnd - 1);
		size_t RuleStart = 0;
		while (RuleStart < StyleSource.size())
		{
			const size_t BraceOpen = StyleSource.find('{', RuleStart);
			if (BraceOpen == FString::npos)
			{
				break;
			}
			const size_t BraceClose = StyleSource.find('}', BraceOpen + 1);
			if (BraceClose == FString::npos)
			{
				break;
			}

			const FString SelectorBlock = StyleSource.substr(RuleStart, BraceOpen - RuleStart);
			const FRuntimeUIPreviewStyle Style = ParseRuntimeUIPreviewStyleBlock(StyleSource.substr(BraceOpen + 1, BraceClose - BraceOpen - 1));
			for (const FString& Selector : SplitRuntimeUIPreviewText(SelectorBlock, ','))
			{
				StoreRuntimeUIPreviewSelectorStyle(StyleSheet, Selector, Style);
			}
			RuleStart = BraceClose + 1;
		}

		SearchPos = StyleClose + 8;
	}
	return StyleSheet;
}

FString ExtractRuntimeUIPreviewAttribute(const FString& TagSource, const char* AttributeName);
FString ExtractRuntimeUIPreviewTagName(const FString& TagSource);

std::filesystem::path ResolveRuntimeUIPreviewRelativePath(const FString& BaseDocumentPath, const FString& ResourcePath)
{
	std::filesystem::path Path(FPaths::ToWide(ResourcePath));
	if (Path.is_relative())
	{
		std::filesystem::path BasePath(FPaths::ToWide(BaseDocumentPath));
		if (BasePath.is_relative())
		{
			BasePath = std::filesystem::path(FPaths::RootDir()) / BasePath;
		}
		Path = BasePath.parent_path() / Path;
	}
	return Path.lexically_normal();
}

FString ReadRuntimeUIPreviewTextFile(const std::filesystem::path& Path)
{
	std::ifstream File(Path, std::ios::binary);
	if (!File)
	{
		return FString();
	}

	std::ostringstream Stream;
	Stream << File.rdbuf();
	return Stream.str();
}

bool WriteRuntimeUIPreviewTextFile(const std::filesystem::path& Path, const FString& Source)
{
	std::error_code Ec;
	std::filesystem::create_directories(Path.parent_path(), Ec);
	if (Ec)
	{
		return false;
	}

	std::ofstream File(Path, std::ios::binary);
	if (!File)
	{
		return false;
	}

	File << Source;
	return File.good();
}

bool FindRuntimeUIPreviewLinkedStylesheetPath(const FString& Source, const FString& DocumentPath, FString& OutPath)
{
	size_t SearchPos = 0;
	while (SearchPos < Source.size())
	{
		const size_t Open = Source.find('<', SearchPos);
		if (Open == FString::npos)
		{
			break;
		}

		const size_t Close = Source.find('>', Open + 1);
		if (Close == FString::npos)
		{
			break;
		}

		FString TagSource = TrimRuntimeUIPreviewText(Source.substr(Open + 1, Close - Open - 1));
		if (!TagSource.empty() && TagSource[0] != '/' && ExtractRuntimeUIPreviewTagName(TagSource) == "link")
		{
			const FString Type = ToLowerRuntimeUIPreviewText(ExtractRuntimeUIPreviewAttribute(TagSource, "type"));
			const FString Href = ExtractRuntimeUIPreviewAttribute(TagSource, "href");
			if (!Href.empty() && (Type.empty() || Type == "text/rcss"))
			{
				OutPath = FPaths::ToUtf8(ResolveRuntimeUIPreviewRelativePath(DocumentPath, Href).generic_wstring());
				return true;
			}
		}

		SearchPos = Close + 1;
	}
	return false;
}

constexpr size_t GRuntimeUIPreviewSourceEditorCapacity = 1024 * 1024;

void SyncRuntimeUIPreviewEditBytesFromString(const FString& Source, TArray<char>& OutBytes)
{
	const size_t BufferSize = (std::max)(GRuntimeUIPreviewSourceEditorCapacity, Source.size() + 4096);
	OutBytes.assign(BufferSize, '\0');
	if (!Source.empty())
	{
		const size_t CopySize = (std::min)(Source.size(), BufferSize - 1);
		std::memcpy(OutBytes.data(), Source.data(), CopySize);
	}
}

bool RenderRuntimeUIPreviewSourceEditor(const char* Label, TArray<char>& Bytes, FString& OutSource, const ImVec2& Size)
{
	if (Bytes.empty())
	{
		Bytes.assign(GRuntimeUIPreviewSourceEditorCapacity, '\0');
	}

	const bool bChanged = ImGui::InputTextMultiline(
		Label,
		Bytes.data(),
		static_cast<int>(Bytes.size()),
		Size,
		ImGuiInputTextFlags_AllowTabInput);
	if (bChanged)
	{
		OutSource = Bytes.data();
	}
	return bChanged;
}

FString BuildRuntimeUIPreviewSourceWithLinkedStyles(const FString& Source, const FString& DocumentPath)
{
	FString Combined = Source;
	size_t SearchPos = 0;
	while (SearchPos < Source.size())
	{
		const size_t LinkOpen = Source.find("<link", SearchPos);
		if (LinkOpen == FString::npos)
		{
			break;
		}

		const size_t LinkClose = Source.find('>', LinkOpen + 1);
		if (LinkClose == FString::npos)
		{
			break;
		}

		const FString LinkTag = Source.substr(LinkOpen + 1, LinkClose - LinkOpen - 1);
		const FString Href = ExtractRuntimeUIPreviewAttribute(LinkTag, "href");
		if (!Href.empty())
		{
			const std::filesystem::path StylePath = ResolveRuntimeUIPreviewRelativePath(DocumentPath, Href);
			const FString StyleSource = ReadRuntimeUIPreviewTextFile(StylePath);
			if (!StyleSource.empty())
			{
				Combined += "\n<style>\n";
				Combined += StyleSource;
				Combined += "\n</style>\n";
			}
		}

		SearchPos = LinkClose + 1;
	}
	return Combined;
}

FString ExtractRuntimeUIPreviewTagName(const FString& TagSource)
{
	size_t Cursor = 0;
	while (Cursor < TagSource.size() && std::isspace(static_cast<unsigned char>(TagSource[Cursor]))) ++Cursor;
	while (Cursor < TagSource.size() && (TagSource[Cursor] == '/' || TagSource[Cursor] == '<')) ++Cursor;
	const size_t Start = Cursor;
	while (Cursor < TagSource.size())
	{
		const char Ch = TagSource[Cursor];
		if (!(std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '-' || Ch == '_'))
		{
			break;
		}
		++Cursor;
	}
	return ToLowerRuntimeUIPreviewText(TagSource.substr(Start, Cursor - Start));
}

FString ExtractRuntimeUIPreviewAttribute(const FString& TagSource, const char* AttributeName)
{
	if (!AttributeName || AttributeName[0] == '\0')
	{
		return FString();
	}

	const FString Needle(AttributeName);
	size_t SearchPos = 0;
	while (SearchPos < TagSource.size())
	{
		const size_t AttrPos = TagSource.find(Needle, SearchPos);
		if (AttrPos == FString::npos)
		{
			break;
		}

		if (AttrPos > 0)
		{
			const char Prev = TagSource[AttrPos - 1];
			if (std::isalnum(static_cast<unsigned char>(Prev)) || Prev == '-' || Prev == '_')
			{
				SearchPos = AttrPos + Needle.size();
				continue;
			}
		}

		size_t Cursor = AttrPos + Needle.size();
		if (Cursor < TagSource.size())
		{
			const char Next = TagSource[Cursor];
			if (std::isalnum(static_cast<unsigned char>(Next)) || Next == '-' || Next == '_')
			{
				SearchPos = Cursor + 1;
				continue;
			}
		}

		while (Cursor < TagSource.size() && std::isspace(static_cast<unsigned char>(TagSource[Cursor]))) ++Cursor;
		if (Cursor >= TagSource.size() || TagSource[Cursor] != '=')
		{
			SearchPos = Cursor;
			continue;
		}
		++Cursor;
		while (Cursor < TagSource.size() && std::isspace(static_cast<unsigned char>(TagSource[Cursor]))) ++Cursor;
		if (Cursor >= TagSource.size() || (TagSource[Cursor] != '"' && TagSource[Cursor] != '\''))
		{
			SearchPos = Cursor;
			continue;
		}

		const char Quote = TagSource[Cursor++];
		const size_t ValueStart = Cursor;
		while (Cursor < TagSource.size() && TagSource[Cursor] != Quote)
		{
			++Cursor;
		}
		return Cursor < TagSource.size() ? TagSource.substr(ValueStart, Cursor - ValueStart) : FString();
	}
	return FString();
}

bool IsRuntimeUIPreviewSelfClosingTag(const FString& TagName, const FString& TagSource)
{
	if (!TagSource.empty() && TagSource[TagSource.size() - 1] == '/')
	{
		return true;
	}
	return TagName == "input" || TagName == "br" || TagName == "img" || TagName == "hr";
}

void ApplyRuntimeUIPreviewNodeStyles(
	FRuntimeUIPreviewNode& Node,
	const FRuntimeUIPreviewStyleSheet& StyleSheet)
{
	auto TagIt = StyleSheet.TagStyles.find(Node.TagName);
	if (TagIt != StyleSheet.TagStyles.end())
	{
		MergeRuntimeUIPreviewStyle(Node.Style, TagIt->second);
	}

	for (FString ClassName : SplitRuntimeUIPreviewText(Node.ClassName, ' '))
	{
		ClassName = ToLowerRuntimeUIPreviewText(ClassName);
		auto ClassIt = StyleSheet.ClassStyles.find(ClassName);
		if (ClassIt != StyleSheet.ClassStyles.end())
		{
			MergeRuntimeUIPreviewStyle(Node.Style, ClassIt->second);
		}
	}

	if (!Node.Id.empty())
	{
		auto IdIt = StyleSheet.IdStyles.find(ToLowerRuntimeUIPreviewText(Node.Id));
		if (IdIt != StyleSheet.IdStyles.end())
		{
			MergeRuntimeUIPreviewStyle(Node.Style, IdIt->second);
		}
	}
}

FRuntimeUIPreviewModel ParseRuntimeUIPreviewModel(const FString& Source)
{
	FRuntimeUIPreviewModel Model;
	const FRuntimeUIPreviewStyleSheet StyleSheet = ParseRuntimeUIPreviewStyleSheet(Source);

	struct FStackEntry
	{
		FString TagName;
		int32 NodeIndex = -1;
	};
	TArray<FStackEntry> Stack;

	int32 GeneratedId = 0;
	size_t SearchPos = 0;
	while (SearchPos < Source.size())
	{
		const size_t Open = Source.find('<', SearchPos);
		if (Open == FString::npos)
		{
			break;
		}
		const size_t Close = Source.find('>', Open + 1);
		if (Close == FString::npos)
		{
			break;
		}

		FString TagSource = TrimRuntimeUIPreviewText(Source.substr(Open + 1, Close - Open - 1));
		if (TagSource.empty())
		{
			SearchPos = Close + 1;
			continue;
		}

		if (TagSource[0] == '!' || TagSource[0] == '?')
		{
			SearchPos = Close + 1;
			continue;
		}

		const bool bClosing = TagSource[0] == '/';
		const FString TagName = ExtractRuntimeUIPreviewTagName(TagSource);
		if (bClosing)
		{
			for (int32 Index = static_cast<int32>(Stack.size()) - 1; Index >= 0; --Index)
			{
				if (Stack[Index].TagName == TagName)
				{
					Stack.resize(Index);
					break;
				}
			}
			SearchPos = Close + 1;
			continue;
		}

		if (TagName == "style")
		{
			const size_t StyleClose = Source.find("</style>", Close + 1);
			SearchPos = StyleClose == FString::npos ? Close + 1 : StyleClose + 8;
			continue;
		}
		if (TagName == "head" || TagName == "rml")
		{
			SearchPos = Close + 1;
			continue;
		}

		const bool bSelfClosing = IsRuntimeUIPreviewSelfClosingTag(TagName, TagSource);
		FString Id = ExtractRuntimeUIPreviewAttribute(TagSource, "id");
		const FString ClassName = ExtractRuntimeUIPreviewAttribute(TagSource, "class");
		const FString ImageSource = ExtractRuntimeUIPreviewAttribute(TagSource, "src");
		FString Action = ExtractRuntimeUIPreviewAttribute(TagSource, "data-action");
		if (Action.empty())
		{
			Action = ExtractRuntimeUIPreviewAttribute(TagSource, "action");
		}

		FString Text;
		if (TagName == "input")
		{
			Text = ExtractRuntimeUIPreviewAttribute(TagSource, "value");
		}
		else
		{
			const size_t TextEnd = Source.find('<', Close + 1);
			if (TextEnd != FString::npos && TextEnd > Close + 1)
			{
				Text = CollapseRuntimeUIPreviewWhitespace(Source.substr(Close + 1, TextEnd - Close - 1));
			}
		}

		const bool bCreateNode =
			!Id.empty() ||
			!Action.empty() ||
			TagName == "button" ||
			TagName == "input" ||
			TagName == "img" ||
			(!Text.empty() && (TagName == "div" || TagName == "span" || TagName == "p"));

		int32 CreatedNodeIndex = -1;
		if (bCreateNode)
		{
			FRuntimeUIPreviewNode Node;
			Node.TagName = TagName;
			Node.Id = Id;
			Node.ClassName = ClassName;
			Node.Text = Text;
			Node.Action = Action;
			Node.ImageSource = ImageSource;
			Node.ParentIndex = Stack.empty() ? -1 : Stack.back().NodeIndex;
			if (Node.Id.empty())
			{
				Node.Id = TagName + "_" + std::to_string(++GeneratedId);
				Node.bGeneratedId = true;
			}
			ApplyRuntimeUIPreviewNodeStyles(Node, StyleSheet);
			Model.Nodes.push_back(Node);
			CreatedNodeIndex = static_cast<int32>(Model.Nodes.size()) - 1;
		}

		if (!bSelfClosing)
		{
			const int32 TransparentParentIndex = Stack.empty() ? -1 : Stack.back().NodeIndex;
			Stack.push_back({ TagName, CreatedNodeIndex >= 0 ? CreatedNodeIndex : TransparentParentIndex });
		}

		SearchPos = Close + 1;
	}

	return Model;
}

float GetRuntimeUIPreviewDefaultWidth(const FRuntimeUIPreviewNode& Node, float ParentWidth)
{
	if (Node.Style.bHasWidth)
	{
		return Node.Style.Width;
	}
	if (Node.TagName == "button")
	{
		return 120.0f;
	}
	if (Node.TagName == "input")
	{
		return RuntimeUIPreviewMax(180.0f, ParentWidth - 36.0f);
	}
	return RuntimeUIPreviewMax(160.0f, ParentWidth - 36.0f);
}

float GetRuntimeUIPreviewDefaultHeight(const FRuntimeUIPreviewNode& Node)
{
	if (Node.Style.bHasHeight)
	{
		return Node.Style.Height;
	}
	if (Node.TagName == "button")
	{
		return 32.0f;
	}
	if (Node.TagName == "input")
	{
		return 28.0f;
	}
	if (Node.TagName == "div" && Node.Text.empty())
	{
		return 120.0f;
	}
	return RuntimeUIPreviewMax(24.0f, Node.Style.FontSize + 8.0f);
}

bool RuntimeUIPreviewHasRecentAction(const TArray<FString>& RuntimeEvents, const FString& Action)
{
	return !Action.empty() && std::find(RuntimeEvents.begin(), RuntimeEvents.end(), Action) != RuntimeEvents.end();
}

FString ResolveRuntimeUIPreviewImagePath(const FString& DocumentPath, const FString& ImageSource)
{
	if (ImageSource.empty())
	{
		return FString();
	}

	return FPaths::ToUtf8(ResolveRuntimeUIPreviewRelativePath(DocumentPath, ImageSource).generic_wstring());
}

ImVec2 GetRuntimeUIPreviewImageSize(ID3D11ShaderResourceView* ImageSRV)
{
	if (!ImageSRV)
	{
		return ImVec2(0.0f, 0.0f);
	}

	ID3D11Resource* Resource = nullptr;
	ImageSRV->GetResource(&Resource);
	if (!Resource)
	{
		return ImVec2(0.0f, 0.0f);
	}

	ID3D11Texture2D* Texture = nullptr;
	const HRESULT Hr = Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture));
	Resource->Release();
	if (FAILED(Hr) || !Texture)
	{
		return ImVec2(0.0f, 0.0f);
	}

	D3D11_TEXTURE2D_DESC Desc = {};
	Texture->GetDesc(&Desc);
	Texture->Release();
	return ImVec2(static_cast<float>(Desc.Width), static_cast<float>(Desc.Height));
}

void GetRuntimeUIPreviewImageFitRect(
	const FString& ObjectFit,
	const ImVec2& Min,
	const ImVec2& Max,
	const ImVec2& SourceSize,
	ImVec2& OutMin,
	ImVec2& OutMax)
{
	OutMin = Min;
	OutMax = Max;
	const FString Fit = ToLowerRuntimeUIPreviewText(ObjectFit.empty() ? FString("stretch") : ObjectFit);
	if (Fit == "stretch" || Fit == "fill" || SourceSize.x <= 0.0f || SourceSize.y <= 0.0f)
	{
		return;
	}

	const float BoxWidth = RuntimeUIPreviewMax(1.0f, Max.x - Min.x);
	const float BoxHeight = RuntimeUIPreviewMax(1.0f, Max.y - Min.y);
	const float ScaleX = BoxWidth / SourceSize.x;
	const float ScaleY = BoxHeight / SourceSize.y;
	const float FitScale = Fit == "cover"
		? RuntimeUIPreviewMax(ScaleX, ScaleY)
		: RuntimeUIPreviewMin(ScaleX, ScaleY);
	const ImVec2 FittedSize(SourceSize.x * FitScale, SourceSize.y * FitScale);
	OutMin = ImVec2(Min.x + (BoxWidth - FittedSize.x) * 0.5f, Min.y + (BoxHeight - FittedSize.y) * 0.5f);
	OutMax = ImVec2(OutMin.x + FittedSize.x, OutMin.y + FittedSize.y);
}

void RenderRuntimeUIPreviewBoxPreview(const FString& Source, const FString& DocumentPath, const TArray<FString>& RuntimeEvents)
{
	const FString PreviewSource = BuildRuntimeUIPreviewSourceWithLinkedStyles(Source, DocumentPath);
	FRuntimeUIPreviewModel Model = ParseRuntimeUIPreviewModel(PreviewSource);

	ImVec2 Available = ImGui::GetContentRegionAvail();
	if (Available.x < 64.0f) Available.x = 64.0f;
	if (Available.y < 64.0f) Available.y = 64.0f;

	ImGui::BeginChild("RuntimeUIPreviewBoxPreviewCanvas", Available, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImVec2 CanvasOrigin = ImGui::GetCursorScreenPos();
	ImVec2 InnerAvailable = ImGui::GetContentRegionAvail();
	const float ScaleX = InnerAvailable.x / Model.CanvasSize.x;
	const float ScaleY = InnerAvailable.y / Model.CanvasSize.y;
	float Scale = RuntimeUIPreviewMin(ScaleX, ScaleY);
	if (Scale <= 0.0f)
	{
		Scale = 1.0f;
	}
	Scale = RuntimeUIPreviewMin(Scale, 1.0f);

	ImVec2 CanvasSize(Model.CanvasSize.x * Scale, Model.CanvasSize.y * Scale);
	if (CanvasSize.x < 1.0f) CanvasSize.x = 1.0f;
	if (CanvasSize.y < 1.0f) CanvasSize.y = 1.0f;

	ImGui::InvisibleButton("##RuntimeUIPreviewBoxPreviewSurface", CanvasSize);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 CanvasMin = CanvasOrigin;
	const ImVec2 CanvasMax(CanvasMin.x + CanvasSize.x, CanvasMin.y + CanvasSize.y);
	DrawList->AddRectFilled(CanvasMin, CanvasMax, ImGui::GetColorU32(ImVec4(0.055f, 0.058f, 0.066f, 1.0f)), 4.0f);
	DrawList->AddRect(CanvasMin, CanvasMax, ImGui::GetColorU32(ImVec4(0.18f, 0.20f, 0.24f, 1.0f)), 4.0f);

	if (Model.Nodes.empty())
	{
		DrawList->AddText(ImVec2(CanvasMin.x + 14.0f, CanvasMin.y + 14.0f), ImGui::GetColorU32(ImVec4(0.52f, 0.56f, 0.62f, 1.0f)), "No previewable elements.");
		ImGui::EndChild();
		return;
	}

	TArray<ImVec2> RectMin(Model.Nodes.size(), ImVec2(0.0f, 0.0f));
	TArray<ImVec2> RectMax(Model.Nodes.size(), ImVec2(0.0f, 0.0f));
	TArray<float> ChildCursorY(Model.Nodes.size(), 0.0f);

	for (size_t Index = 0; Index < Model.Nodes.size(); ++Index)
	{
		const FRuntimeUIPreviewNode& Node = Model.Nodes[Index];
		const int32 ParentIndex = Node.ParentIndex;
		const bool bHasParent = ParentIndex >= 0 && ParentIndex < static_cast<int32>(Model.Nodes.size());
		const FRuntimeUIPreviewStyle* ParentStyle = bHasParent ? &Model.Nodes[ParentIndex].Style : nullptr;
		const bool bUsesExplicitPosition =
			Node.Style.bHasLeft || Node.Style.bHasLeftPercent || Node.Style.bHasRight ||
			Node.Style.bHasTop || Node.Style.bHasTopPercent || Node.Style.bHasBottom;
		const float ParentPadding = bHasParent
			? (ParentStyle && ParentStyle->bHasPadding ? ParentStyle->Padding : (bUsesExplicitPosition ? 0.0f : 12.0f))
			: 0.0f;
		const ImVec2 ParentMin = bHasParent ? RectMin[ParentIndex] : ImVec2(0.0f, 0.0f);
		const ImVec2 ParentMax = bHasParent ? RectMax[ParentIndex] : Model.CanvasSize;
		const float ParentWidth = RuntimeUIPreviewMax(80.0f, ParentMax.x - ParentMin.x - ParentPadding * 2.0f);
		const float ParentHeight = RuntimeUIPreviewMax(80.0f, ParentMax.y - ParentMin.y - ParentPadding * 2.0f);
		const bool bParentFlex = ParentStyle && ParentStyle->bHasDisplay && ParentStyle->Display == "flex";
		const float Width = Node.Style.bHasWidthPercent
			? ParentWidth * Node.Style.WidthPercent
			: GetRuntimeUIPreviewDefaultWidth(Node, ParentWidth);
		const float Height = Node.Style.bHasHeightPercent
			? ParentHeight * Node.Style.HeightPercent
			: GetRuntimeUIPreviewDefaultHeight(Node);

		float X = ParentMin.x + ParentPadding;
		float Y = ParentMin.y + ParentPadding;
		if (Node.Style.bHasLeftPercent)
		{
			X = ParentMin.x + ParentPadding + ParentWidth * Node.Style.LeftPercent;
		}
		else if (Node.Style.bHasLeft)
		{
			X = ParentMin.x + Node.Style.Left;
		}
		else if (Node.Style.bHasRight)
		{
			X = ParentMax.x - ParentPadding - Node.Style.Right - Width;
		}
		if (Node.Style.bHasMarginLeft)
		{
			X += Node.Style.MarginLeft;
		}
		if (bParentFlex && ParentStyle->bHasAlignItems && ParentStyle->AlignItems == "center" && !bUsesExplicitPosition)
		{
			X = ParentMin.x + ParentPadding + (ParentWidth - Width) * 0.5f;
		}

		if (Node.Style.bHasTopPercent)
		{
			Y = ParentMin.y + ParentPadding + ParentHeight * Node.Style.TopPercent;
		}
		else if (Node.Style.bHasTop)
		{
			Y = ParentMin.y + Node.Style.Top;
		}
		else if (Node.Style.bHasBottom)
		{
			Y = ParentMax.y - ParentPadding - Node.Style.Bottom - Height;
		}
		else if (bHasParent)
		{
			Y = ParentMin.y + ParentPadding + ChildCursorY[ParentIndex] + (Node.Style.bHasMarginTop ? Node.Style.MarginTop : 0.0f);
		}
		else
		{
			Y += static_cast<float>(Index) * 38.0f;
		}
		if (bParentFlex && ParentStyle->bHasJustifyContent && ParentStyle->JustifyContent == "flex-end" && !bUsesExplicitPosition)
		{
			Y = ParentMax.y - ParentPadding - Height;
		}

		RectMin[Index] = ImVec2(X, Y);
		RectMax[Index] = ImVec2(X + Width, Y + Height);

		if (bHasParent)
		{
			const float NextY = (RectMax[Index].y - ParentMin.y - ParentPadding)
				+ (Node.Style.bHasMarginBottom ? Node.Style.MarginBottom : 6.0f);
			ChildCursorY[ParentIndex] = RuntimeUIPreviewMax(ChildCursorY[ParentIndex], NextY);
			if (!Model.Nodes[ParentIndex].Style.bHasHeight)
			{
				RectMax[ParentIndex].y = RuntimeUIPreviewMax(RectMax[ParentIndex].y, RectMax[Index].y + ParentPadding);
			}
		}
	}

	for (size_t Index = 0; Index < Model.Nodes.size(); ++Index)
	{
		const FRuntimeUIPreviewNode& Node = Model.Nodes[Index];
		const ImVec2 Min(CanvasMin.x + RectMin[Index].x * Scale, CanvasMin.y + RectMin[Index].y * Scale);
		const ImVec2 Max(CanvasMin.x + RectMax[Index].x * Scale, CanvasMin.y + RectMax[Index].y * Scale);
		const bool bFired = RuntimeUIPreviewHasRecentAction(RuntimeEvents, Node.Action);
		ImVec4 Fill = Node.Style.bHasBackgroundColor ? Node.Style.BackgroundColor : ImVec4(0.11f, 0.13f, 0.16f, Node.TagName == "div" ? 0.42f : 0.86f);
		if (Node.TagName == "input" && !Node.Style.bHasBackgroundColor)
		{
			Fill = ImVec4(0.90f, 0.93f, 0.96f, 1.0f);
		}
		if (Node.TagName == "button" && !Node.Style.bHasBackgroundColor)
		{
			Fill = ImVec4(0.18f, 0.38f, 0.78f, 1.0f);
		}

		const float Radius = Node.Style.bHasBorderRadius ? Node.Style.BorderRadius * Scale : 3.0f;
		DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(Fill), Radius);

		ID3D11ShaderResourceView* ImageSRV = nullptr;
		bool bMissingImage = false;
		if (Node.TagName == "img" && !Node.ImageSource.empty())
		{
			const FString ImagePath = ResolveRuntimeUIPreviewImagePath(DocumentPath, Node.ImageSource);
			ImageSRV = FEditorTextureManager::Get().GetOrLoadThumbnail(ImagePath);
			if (ImageSRV)
			{
				ImVec2 ImageMin = Min;
				ImVec2 ImageMax = Max;
				GetRuntimeUIPreviewImageFitRect(
					Node.Style.bHasObjectFit ? Node.Style.ObjectFit : FString("stretch"),
					Min,
					Max,
					GetRuntimeUIPreviewImageSize(ImageSRV),
					ImageMin,
					ImageMax);
				DrawList->PushClipRect(Min, Max, true);
				DrawList->AddImage(reinterpret_cast<ImTextureID>(ImageSRV), ImageMin, ImageMax);
				DrawList->PopClipRect();
			}
			else
			{
				bMissingImage = true;
			}
		}
		else if (Node.TagName == "img")
		{
			bMissingImage = true;
		}

		ImVec4 Border = bFired
			? ImVec4(0.36f, 0.92f, 0.56f, 1.0f)
			: (Node.Style.bHasBorderColor ? Node.Style.BorderColor : ImVec4(0.30f, 0.36f, 0.44f, 0.80f));
		const float BorderWidth = bFired ? 2.5f : RuntimeUIPreviewMax(1.0f, Node.Style.bHasBorderWidth ? Node.Style.BorderWidth * Scale : 1.0f);
		DrawList->AddRect(Min, Max, ImGui::GetColorU32(Border), Radius, 0, BorderWidth);

		FString Label = !Node.Text.empty() ? Node.Text : (Node.bGeneratedId ? Node.TagName : Node.Id);
		if (!Node.Action.empty())
		{
			Label += " [" + Node.Action + "]";
		}
		ImVec4 TextColor = Node.Style.bHasTextColor ? Node.Style.TextColor : ImVec4(0.88f, 0.92f, 0.96f, 1.0f);
		if (Node.TagName == "input" && !Node.Style.bHasTextColor)
		{
			TextColor = ImVec4(0.08f, 0.11f, 0.16f, 1.0f);
		}
		if (bMissingImage)
		{
			DrawList->AddLine(Min, Max, ImGui::GetColorU32(ImVec4(1.0f, 0.25f, 0.22f, 0.85f)), 1.5f);
			DrawList->AddLine(ImVec2(Max.x, Min.y), ImVec2(Min.x, Max.y), ImGui::GetColorU32(ImVec4(1.0f, 0.25f, 0.22f, 0.85f)), 1.5f);
			DrawList->PushClipRect(Min, Max, true);
			DrawList->AddText(ImVec2(Min.x + 8.0f, Min.y + 6.0f), ImGui::GetColorU32(ImVec4(1.0f, 0.42f, 0.36f, 1.0f)), "Missing image");
			DrawList->PopClipRect();
		}
		else if (!ImageSRV || Node.TagName != "img")
		{
			DrawList->PushClipRect(Min, Max, true);
			DrawList->AddText(ImVec2(Min.x + 8.0f, Min.y + 6.0f), ImGui::GetColorU32(TextColor), Label.c_str());
			DrawList->PopClipRect();
		}
	}

	ImGui::EndChild();
}

FString MakeRuntimeUIPreviewPayloadId()
{
	return "__RuntimeUIPreview";
}

}

void FEditorMainPanel::Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiSetting::LoadSetting();

	ImGuiIO& IO = ImGui::GetIO();
	IO.IniFilename = "Settings/imgui.ini";
	IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	Window = InWindow;
	EditorEngine = InEditorEngine;

	// 한글 지원 폰트 로드 (시스템 맑은 고딕)
	IO.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/malgun.ttf", 16.0f, nullptr, IO.Fonts->GetGlyphRangesKorean());

	ImGui_ImplWin32_Init((void*)InWindow->GetHWND());
	ImGui_ImplDX11_Init(InRenderer.GetFD3DDevice().GetDevice(), InRenderer.GetFD3DDevice().GetDeviceContext());

	ImGuiStyle& Style = ImGui::GetStyle();
	ImVec4* Colors = Style.Colors;
	
	const ImVec4 UnifiedBlack = ImVec4(0.024f, 0.024f, 0.028f, 1.0f);
	const ImVec4 UnifiedBlackHovered = ImVec4(0.075f, 0.075f, 0.085f, 1.0f);
	const ImVec4 UnifiedBlackActive = ImVec4(0.105f, 0.105f, 0.118f, 1.0f);

	Colors[ImGuiCol_WindowBg] = UnifiedBlack;
	Colors[ImGuiCol_ChildBg] = UnifiedBlack;
	Colors[ImGuiCol_PopupBg] = UnifiedBlack;
	Colors[ImGuiCol_TitleBg] = UnifiedBlack;
	Colors[ImGuiCol_TitleBgActive] = UnifiedBlack;
	Colors[ImGuiCol_TitleBgCollapsed] = UnifiedBlack;
	Colors[ImGuiCol_MenuBarBg] = UnifiedBlack;
	Colors[ImGuiCol_Tab] = UnifiedBlack;
	Colors[ImGuiCol_TabSelected] = UnifiedBlackActive;
	Colors[ImGuiCol_TabHovered] = UnifiedBlackHovered;
	Colors[ImGuiCol_TabDimmed] = UnifiedBlack;
	Colors[ImGuiCol_TabDimmedSelected] = UnifiedBlackActive;
	Colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.28f, 0.28f, 0.30f, 1.0f);
	Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
	Colors[ImGuiCol_DockingEmptyBg] = UnifiedBlack;
	Colors[ImGuiCol_Header] = UnifiedBlack;
	Colors[ImGuiCol_HeaderHovered] = UnifiedBlackHovered;
	Colors[ImGuiCol_HeaderActive] = UnifiedBlackActive;
	Colors[ImGuiCol_Button] = UnifiedBlackActive;
	Colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.16f, 0.18f, 1.0f);
	Colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
	Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
	Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
	Colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
	Colors[ImGuiCol_CheckMark] = ImVec4(0.82f, 0.82f, 0.82f, 1.0f);
	Colors[ImGuiCol_Border] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);

	ConsoleWidget.Initialize(InEditorEngine);
	ControlWidget.Initialize(InEditorEngine);
	PropertyWidget.Initialize(InEditorEngine);
	SceneWidget.Initialize(InEditorEngine);
	StatWidget.Initialize(InEditorEngine);
	ContentBrowserWidget.Initialize(InEditorEngine, InRenderer.GetFD3DDevice().GetDevice());
	ShadowMapDebugWidget.Initialize(InEditorEngine);
	AnimationDebugWidget.Initialize(InEditorEngine);
	CombatMapEditorWidget.Initialize(InEditorEngine);
	LevelActorSequencerWidget.Initialize(InEditorEngine);
	AssetEditorManager.Initialize(InEditorEngine);

	AssetEditorManager.RegisterEditor<FFloatCurveEditorWidget>();
	AssetEditorManager.RegisterEditor<FCameraShakeEditorWidget>();
	AssetEditorManager.RegisterEditor<FActorSequenceEditorWidget>();
	AssetEditorManager.RegisterEditor<FMeshEditorWidget>();
	AssetEditorManager.RegisterEditor<FStaticMeshEditorWidget>();
	AssetEditorManager.RegisterEditor<FAnimGraphEditorWidget>();
	AssetEditorManager.RegisterEditor<FParticleEditorWidget>();
	AssetEditorManager.RegisterEditor<FLuaBlueprintEditorWidget>();
    AssetEditorManager.RegisterEditor<FMaterialEditorWidget>();
	AssetEditorManager.RegisterEditor<FRuntimeUILayoutEditorWidget>();
}

void FEditorMainPanel::Release()
{
	UnmountRuntimeUIPreviewFromViewport();
	LevelActorSequencerWidget.Close();
	AssetEditorManager.CloseAll();
	ConsoleWidget.Shutdown();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void FEditorMainPanel::SaveToSettings() const
{
	ContentBrowserWidget.SaveToSettings();
}

void FEditorMainPanel::AddReferencedObjects(FReferenceCollector& Collector)
{
	AssetEditorManager.AddReferencedObjects(Collector);
	LevelActorSequencerWidget.AddReferencedObjects(Collector);
	Collector.AddReferencedObject(static_cast<UObject*>(RuntimeUIPreviewViewportWidget), "RuntimeUIPreviewViewportWidget");
}

void FEditorMainPanel::TickAssetEditors(float DeltaTime)
{
	AssetEditorManager.Tick(DeltaTime);
}

void FEditorMainPanel::Render(float DeltaTime)
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	const FEditorSettings& Settings = FEditorSettings::Get();
	const bool bLevelDocumentActive = DocumentTabs.IsLevelEditorActive();
	const bool bPIEFullscreenPreview = EditorEngine
		&& bLevelDocumentActive
		&& EditorEngine->IsPlayingInEditor()
		&& Settings.PIEViewportPreview.bFullscreenPreview;

	if (bPIEFullscreenPreview)
	{
		SCOPE_STAT_CAT("EditorEngine->RenderViewportUI", "5_UI");
		EditorEngine->RenderViewportUI(DeltaTime);

		if (FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport())
		{
			EditorEngine->GetOverlayStatSystem().RenderImGui(*EditorEngine, ActiveViewport->GetViewportScreenRect());
		}

		FNotificationToast::Render();

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		return;
	}

	RenderMainMenuBar();

	const float FooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	RenderDocumentTabStrip(FooterHeight);
	RenderMainDockSpace(FooterHeight, GDocumentTabStripHeight);

	// 뷰포트 렌더링은 EditorEngine이 담당 (SSplitter 레이아웃 + ImGui::Image)
	if (EditorEngine && DocumentTabs.IsLevelEditorActive())
	{
		SCOPE_STAT_CAT("EditorEngine->RenderViewportUI", "5_UI");
		EditorEngine->RenderViewportUI(DeltaTime);

		if (FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport())
		{
			EditorEngine->GetOverlayStatSystem().RenderImGui(*EditorEngine, ActiveViewport->GetViewportScreenRect());
		}
	}

	if (!bHideEditorWindows && Settings.UI.bImGUISettings)
	{
		ImGuiSetting::ShowSetting();
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bControl)
	{
		SCOPE_STAT_CAT("ControlWidget.Render", "5_UI");
		ControlWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bProperty)
	{
		SCOPE_STAT_CAT("PropertyWidget.Render", "5_UI");
		PropertyWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bScene)
	{
		SCOPE_STAT_CAT("SceneWidget.Render", "5_UI");
		SceneWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bStat)
	{
		SCOPE_STAT_CAT("StatWidget.Render", "5_UI");
		StatWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bShadowMapDebug)
	{
		ShadowMapDebugWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bAnimationDebug)
	{
		SCOPE_STAT_CAT("AnimationDebugWidget.Render", "5_UI");
		AnimationDebugWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bCombatMapEditor)
	{
		SCOPE_STAT_CAT("CombatMapEditorWidget.Render", "5_UI");
		CombatMapEditorWidget.Render(DeltaTime);
	}

	ProjectSettingsWidget.Render();
	WorldSettingsWidget.Render();

	if (!bHideEditorWindows && bLevelDocumentActive)
	{
		RenderEditorDebugPanel();
	}

	if (!bHideEditorWindows && bLevelDocumentActive && LevelActorSequencerWidget.IsOpen())
	{
		LevelActorSequencerWidget.Tick(DeltaTime);
		LevelActorSequencerWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && !bLevelDocumentActive)
	{
		RenderActiveDocument(GDocumentTabStripHeight, FooterHeight, DeltaTime);
	}

	RenderShortcutOverlay();
	RenderContentBrowserDrawer(DeltaTime);
	RenderConsoleDrawer(DeltaTime);
	RenderFooterOverlay(DeltaTime);

	AssetEditorManager.Render(DeltaTime);

	// 토스트 알림 (항상 최상위에 표시)
	FNotificationToast::Render();

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void FEditorMainPanel::RenderMainMenuBar()
{
	if (!ImGui::BeginMainMenuBar())
	{
		return;
	}

	FEditorSettings& Settings = FEditorSettings::Get();

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New Scene", "Ctrl+N") && EditorEngine)
		{
			EditorEngine->NewScene();
		}
		if (ImGui::MenuItem("Open Scene...", "Ctrl+O") && EditorEngine)
		{
			EditorEngine->LoadSceneWithDialog();
		}
		if (ImGui::MenuItem("Save Scene", "Ctrl+S") && EditorEngine)
		{
			EditorEngine->SaveScene();
		}
		if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S") && EditorEngine)
		{
			EditorEngine->SaveSceneAsWithDialog();
		}

		ImGui::Separator();
		const char* CurrentSceneLabel = "Current: Unsaved Scene";
		FString CurrentScenePath;
		FString CurrentSceneText;
		const bool bSceneDirty = EditorEngine && EditorEngine->IsSceneDirty();
		if (EditorEngine && EditorEngine->HasCurrentLevelFilePath())
		{
			CurrentScenePath = EditorEngine->GetCurrentLevelFilePath();
			CurrentSceneText = FString("Current: ") + (bSceneDirty ? FString("* ") : FString()) + CurrentScenePath;
			CurrentSceneLabel = CurrentSceneText.c_str();
		}
		else if (bSceneDirty)
		{
			CurrentSceneLabel = "Current: * Unsaved Scene";
		}
		ImGui::BeginDisabled();
		ImGui::MenuItem(CurrentSceneLabel, nullptr, false, false);
		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Windows"))
	{
		bShowWidgetList = true;
		ImGui::OpenPopup("##WidgetListPopup");
	}
	if (ImGui::BeginPopup("##WidgetListPopup"))
	{
		ImGui::Checkbox("Camera", &Settings.UI.bControl);
		ImGui::Checkbox("Detial", &Settings.UI.bProperty);
		ImGui::Checkbox("Outliner", &Settings.UI.bScene);
		ImGui::Checkbox("Stat Profiler", &Settings.UI.bStat);
		if (ImGui::Checkbox("Content Browser (Ctrl+Space)", &Settings.UI.bContentBrowser) && Settings.UI.bContentBrowser)
		{
			bConsoleDrawerVisible = false;
			ConsoleDrawerAnim = 0.0f;
			ConsoleBacktickCycleState = 0;
			bFocusConsoleInputNextFrame = false;
			bBringConsoleDrawerToFrontNextFrame = false;
			bBringContentBrowserDrawerToFrontNextFrame = true;
		}
		ImGui::Checkbox("Editor Debug", &Settings.UI.bEditorDebug);
		ImGui::Checkbox("Shadow Map Debug", &Settings.UI.bShadowMapDebug);
		ImGui::Checkbox("Animation Debug", &Settings.UI.bAnimationDebug);
		ImGui::Checkbox("Combat Map Editor", &Settings.UI.bCombatMapEditor);
		ImGui::Separator();
		ImGui::Checkbox("IMGUI_Setting", &Settings.UI.bImGUISettings);
		ImGui::EndPopup();
	}
	else
	{
		bShowWidgetList = false;
	}

	if (ImGui::MenuItem("Project Settings"))
	{
		ProjectSettingsWidget.bOpen = true;
	}

	if (ImGui::MenuItem("World Settings"))
	{
		WorldSettingsWidget.bOpen = true;
	}

	if (ImGui::MenuItem("Shortcut"))
	{
		bShowShortcutOverlay = !bShowShortcutOverlay;
	}

	ImGui::EndMainMenuBar();
}

void FEditorMainPanel::RenderMainDockSpace(float ReservedBottomHeight, float ReservedTopHeight)
{
	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	ImVec2 DockSpaceSize = MainViewport->WorkSize;
	DockSpaceSize.y = max(1.0f, DockSpaceSize.y - ReservedTopHeight - ReservedBottomHeight);
	ImVec2 DockSpacePos = MainViewport->WorkPos;
	DockSpacePos.y += ReservedTopHeight;

	ImGui::SetNextWindowPos(DockSpacePos);
	ImGui::SetNextWindowSize(DockSpaceSize);
	ImGui::SetNextWindowViewport(MainViewport->ID);

	ImGuiWindowFlags HostWindowFlags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus;

	char Label[32];
	std::snprintf(Label, sizeof(Label), "WindowOverViewport_%08X", MainViewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin(Label, nullptr, HostWindowFlags);
	ImGui::PopStyleVar(3);

	ImGuiID DockSpaceId = ImGui::GetID("DockSpace");
	ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f));

	ImGui::End();
}

void FEditorMainPanel::RenderDocumentTabStrip(float ReservedBottomHeight)
{
	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	ImVec2 TabPos = MainViewport->WorkPos;
	ImVec2 TabSize = ImVec2(MainViewport->WorkSize.x, GDocumentTabStripHeight);
	TabSize.y = max(1.0f, (std::min)(TabSize.y, MainViewport->WorkSize.y - ReservedBottomHeight));

	ImGui::SetNextWindowPos(TabPos);
	ImGui::SetNextWindowSize(TabSize);
	ImGui::SetNextWindowViewport(MainViewport->ID);

	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoScrollWithMouse
		| ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.065f, 0.070f, 0.085f, 1.0f));
	ImGui::Begin("DocumentTabsHost", nullptr, WindowFlags);
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);

	TArray<FEditorDocumentTabId> PendingCloseTabs;
	bool bHasPendingActiveTab = false;
	FEditorDocumentTabId PendingActiveTab;
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const TArray<FEditorDocumentTabEntry>& Tabs = DocumentTabs.GetTabs();

	for (int32 Index = 0; Index < static_cast<int32>(Tabs.size()); ++Index)
	{
		const FEditorDocumentTabEntry& Tab = Tabs[Index];
		FString Label = Tab.Label;
		if (Tab.Id.Kind == EEditorDocumentTabKind::LevelEditor)
		{
			Label = (EditorEngine && EditorEngine->HasCurrentLevelFilePath())
				? FString("Level - ") + GetFileStemForDisplay(EditorEngine->GetCurrentLevelFilePath())
				: FString("Level - Untitled");
		}
		if (Tab.bDirty)
		{
			Label += "*";
		}

		ImGui::PushID(Index);
		const ImVec2 TabMin = ImGui::GetCursorScreenPos();
		const ImVec2 TabMax(TabMin.x + GDocumentTabWidth, TabMin.y + GDocumentTabStripHeight);
		ImGui::InvisibleButton("##DocumentTab", ImVec2(GDocumentTabWidth, GDocumentTabStripHeight));
		const bool bHovered = ImGui::IsItemHovered();
		const bool bActive = DocumentTabs.GetActiveTab() == Tab.Id;
		const FDocumentTabVisual Visual = GetDocumentTabVisual(Tab.Id.Kind);

		const float CloseSize = 16.0f;
		const ImVec2 CloseMin(TabMax.x - CloseSize - 7.0f, TabMin.y + (GDocumentTabStripHeight - CloseSize) * 0.5f);
		const ImVec2 CloseMax(CloseMin.x + CloseSize, CloseMin.y + CloseSize);
		const bool bCloseHovered = Tab.bCanClose && ImGui::IsMouseHoveringRect(CloseMin, CloseMax);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !bCloseHovered)
		{
			bHasPendingActiveTab = true;
			PendingActiveTab = Tab.Id;
		}
		if (bCloseHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			PendingCloseTabs.push_back(Tab.Id);
		}

		const ImU32 TabBg = ImGui::GetColorU32(
			bActive
				? ImVec4(0.115f, 0.125f, 0.150f, 1.0f)
				: (bHovered ? ImVec4(0.095f, 0.102f, 0.125f, 1.0f) : ImVec4(0.075f, 0.080f, 0.098f, 1.0f)));
		const ImU32 BorderColor = ImGui::GetColorU32(ImVec4(0.035f, 0.038f, 0.046f, 1.0f));
		DrawList->AddRectFilled(TabMin, TabMax, TabBg, 0.0f);
		DrawList->AddRect(TabMin, TabMax, BorderColor);

		const ImVec2 IconCenter(TabMin.x + 17.0f, TabMin.y + GDocumentTabStripHeight * 0.5f);
		DrawList->AddCircleFilled(IconCenter, 5.0f, ImGui::GetColorU32(Visual.AccentColor), 12);
		if (Tab.Id.Kind == EEditorDocumentTabKind::LevelEditor)
		{
			DrawList->AddTriangleFilled(
				ImVec2(IconCenter.x, IconCenter.y - 6.0f),
				ImVec2(IconCenter.x - 6.0f, IconCenter.y + 5.0f),
				ImVec2(IconCenter.x + 6.0f, IconCenter.y + 5.0f),
				ImGui::GetColorU32(Visual.AccentColor));
		}

		const ImU32 TextColor = ImGui::GetColorU32(bActive ? ImVec4(0.88f, 0.91f, 0.96f, 1.0f) : ImVec4(0.58f, 0.62f, 0.70f, 1.0f));
		const ImVec2 TextMin(TabMin.x + 32.0f, TabMin.y + (GDocumentTabStripHeight - ImGui::GetTextLineHeight()) * 0.5f);
		const ImVec2 TextMax(Tab.bCanClose ? CloseMin.x - 5.0f : TabMax.x - 10.0f, TabMax.y - 5.0f);
		if (TextMax.x > TextMin.x + 6.0f)
		{
			DrawList->PushClipRect(TextMin, TextMax, true);
			DrawList->AddText(TextMin, TextColor, Label.c_str());
			DrawList->PopClipRect();
		}

		if (Tab.bCanClose)
		{
			const ImU32 CloseColor = ImGui::GetColorU32(
				bCloseHovered ? ImVec4(0.94f, 0.96f, 1.0f, 1.0f) : ImVec4(0.50f, 0.54f, 0.62f, 1.0f));
			if (bCloseHovered)
			{
				DrawList->AddRectFilled(CloseMin, CloseMax, ImGui::GetColorU32(ImVec4(0.23f, 0.25f, 0.31f, 1.0f)), 3.0f);
			}
			DrawList->AddLine(ImVec2(CloseMin.x + 4.0f, CloseMin.y + 4.0f), ImVec2(CloseMax.x - 4.0f, CloseMax.y - 4.0f), CloseColor, 1.5f);
			DrawList->AddLine(ImVec2(CloseMax.x - 4.0f, CloseMin.y + 4.0f), ImVec2(CloseMin.x + 4.0f, CloseMax.y - 4.0f), CloseColor, 1.5f);
		}

		if (bActive)
		{
			DrawList->AddLine(
				ImVec2(TabMin.x, TabMax.y - 1.0f),
				ImVec2(TabMax.x, TabMax.y - 1.0f),
				ImGui::GetColorU32(Visual.AccentColor),
				2.0f);
		}

		if (bHovered)
		{
			ImGui::SetTooltip("%s\n%s", Label.c_str(), Visual.Tooltip);
		}

		ImGui::PopID();
		ImGui::SameLine(0.0f, 0.0f);
	}

	if (bHasPendingActiveTab)
	{
		DocumentTabs.SetActiveTab(PendingActiveTab);
	}

	for (const FEditorDocumentTabId& TabId : PendingCloseTabs)
	{
		AssetEditorManager.CloseEditorForTab(TabId);
		DocumentTabs.CloseTab(TabId);
	}

	const float UsedWidth = GDocumentTabWidth * static_cast<float>(Tabs.size());
	if (UsedWidth < TabSize.x - GDocumentTabRightPadding)
	{
		ImGui::SetCursorScreenPos(ImVec2(TabPos.x + UsedWidth, TabPos.y));
		ImGui::Dummy(ImVec2(TabSize.x - UsedWidth - GDocumentTabRightPadding, GDocumentTabStripHeight));
	}

	ImGui::End();
}

void FEditorMainPanel::RenderActiveDocument(float ReservedTopHeight, float ReservedBottomHeight, float DeltaTime)
{
	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	ImVec2 HostPos = MainViewport->WorkPos;
	HostPos.y += ReservedTopHeight;
	ImVec2 HostSize = MainViewport->WorkSize;
	HostSize.y = max(1.0f, HostSize.y - ReservedTopHeight - ReservedBottomHeight);

	ImGui::SetNextWindowPos(HostPos);
	ImGui::SetNextWindowSize(HostSize);
	ImGui::SetNextWindowViewport(MainViewport->ID);

	ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
	ImGui::Begin("ActiveDocumentHost", nullptr, HostFlags);
	ImGui::PopStyleVar(3);

	const FEditorDocumentTabId& ActiveTab = DocumentTabs.GetActiveTab();
	if (ActiveTab.Kind == EEditorDocumentTabKind::RuntimeUIPreview)
	{
		RenderRuntimeUIPreviewDocument();
		ImGui::End();
		return;
	}

	if (!AssetEditorManager.RenderActiveEditorDocument(ActiveTab, DeltaTime))
	{
		DocumentTabs.CloseTab(ActiveTab);
	}

	ImGui::End();
}

void FEditorMainPanel::RenderRuntimeUIPreviewDocument()
{
	if (RuntimeUIPreviewPath.empty())
	{
		ImGui::TextUnformatted("No Runtime UI document.");
		return;
	}

	const bool bMounted = IsRuntimeUIPreviewMounted();
	const bool bDirty = bRuntimeUIPreviewSourceDirty || bRuntimeUIPreviewRcssDirty;
	if (ImGui::Button(bDirty ? "Save *" : "Save"))
	{
		SaveRuntimeUIPreviewSources();
	}
	ImGui::SameLine();
	if (ImGui::Button("Save + Remount"))
	{
		if (SaveRuntimeUIPreviewSources() && RuntimeUIPreviewError.empty())
		{
			MountRuntimeUIPreviewInViewport(true);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload"))
	{
		ReloadRuntimeUIPreviewDocument();
		if (bMounted && RuntimeUIPreviewError.empty())
		{
			MountRuntimeUIPreviewInViewport(true);
		}
	}
	ImGui::SameLine();
	if (bMounted)
	{
		if (ImGui::Button("Unmount Viewport"))
		{
			UnmountRuntimeUIPreviewFromViewport();
		}
	}
	else
	{
		if (ImGui::Button("Mount In Level Viewport"))
		{
			if (MountRuntimeUIPreviewInViewport(false))
			{
				FEditorDocumentTabId LevelTabId;
				LevelTabId.Kind = EEditorDocumentTabKind::LevelEditor;
				DocumentTabs.SetActiveTab(LevelTabId);
			}
		}
	}
	ImGui::SameLine();
	ImGui::TextUnformatted(GetFileNameForDisplay(RuntimeUIPreviewPath).c_str());
	if (IsRuntimeUIPreviewMounted())
	{
		PollRuntimeUIPreviewEvents();
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.38f, 0.82f, 0.54f, 1.0f), "Rml viewport mounted");
	}

	if (!RuntimeUIPreviewError.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.42f, 0.36f, 1.0f));
		ImGui::TextWrapped("%s", RuntimeUIPreviewError.c_str());
		ImGui::PopStyleColor();
		return;
	}

	ImGui::TextDisabled("%s", RuntimeUIPreviewPath.c_str());
	ImGui::Separator();

	const float LeftWidth = 260.0f;
	ImGui::BeginChild("RuntimeUIPreviewInspector", ImVec2(LeftWidth, 0.0f), true);
	ImGui::TextUnformatted("Actions");
	if (RuntimeUIPreviewActionEvents.empty())
	{
		ImGui::TextDisabled("none");
	}
	else
	{
		for (const FString& Action : RuntimeUIPreviewActionEvents)
		{
			ImGui::BulletText("%s", Action.c_str());
		}
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Runtime Events");
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear###RuntimeUIPreviewClearEvents"))
	{
		RuntimeUIPreviewRuntimeEvents.clear();
	}
	if (RuntimeUIPreviewRuntimeEvents.empty())
	{
		ImGui::TextDisabled("none");
	}
	else
	{
		for (const FString& EventName : RuntimeUIPreviewRuntimeEvents)
		{
			ImGui::BulletText("%s", EventName.c_str());
		}
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Element IDs");
	if (RuntimeUIPreviewElementIds.empty())
	{
		ImGui::TextDisabled("none");
	}
	else
	{
		for (const FString& ElementId : RuntimeUIPreviewElementIds)
		{
			ImGui::BulletText("%s", ElementId.c_str());
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("RuntimeUIPreviewMain", ImVec2(0.0f, 0.0f), true);
	if (ImGui::BeginTabBar("RuntimeUIPreviewTabs"))
	{
		if (ImGui::BeginTabItem("Preview"))
		{
			RenderRuntimeUIPreviewBoxPreview(RuntimeUIPreviewSourceEditBuffer, RuntimeUIPreviewPath, RuntimeUIPreviewRuntimeEvents);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(bRuntimeUIPreviewSourceDirty ? "RML *" : "RML"))
		{
			if (RenderRuntimeUIPreviewSourceEditor("##RuntimeUIPreviewRmlSource", RuntimeUIPreviewSourceEditBytes, RuntimeUIPreviewSourceEditBuffer, ImVec2(0.0f, 0.0f)))
			{
				bRuntimeUIPreviewSourceDirty = RuntimeUIPreviewSourceEditBuffer != RuntimeUIPreviewSource;
			}
			ImGui::EndTabItem();
		}
		if (!RuntimeUIPreviewRcssPath.empty() && ImGui::BeginTabItem(bRuntimeUIPreviewRcssDirty ? "RCSS *" : "RCSS"))
		{
			ImGui::TextDisabled("%s", RuntimeUIPreviewRcssPath.c_str());
			if (RenderRuntimeUIPreviewSourceEditor("##RuntimeUIPreviewRcssSource", RuntimeUIPreviewRcssEditBytes, RuntimeUIPreviewRcssEditBuffer, ImVec2(0.0f, -ImGui::GetTextLineHeightWithSpacing())))
			{
				bRuntimeUIPreviewRcssDirty = RuntimeUIPreviewRcssEditBuffer != RuntimeUIPreviewRcssSource;
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::EndChild();
}

void FEditorMainPanel::ReloadRuntimeUIPreviewDocument()
{
	RuntimeUIPreviewSource.clear();
	RuntimeUIPreviewSourceEditBuffer.clear();
	RuntimeUIPreviewSourceEditBytes.clear();
	RuntimeUIPreviewRcssPath.clear();
	RuntimeUIPreviewRcssSource.clear();
	RuntimeUIPreviewRcssEditBuffer.clear();
	RuntimeUIPreviewRcssEditBytes.clear();
	RuntimeUIPreviewError.clear();
	RuntimeUIPreviewActionEvents.clear();
	RuntimeUIPreviewElementIds.clear();
	bRuntimeUIPreviewSourceDirty = false;
	bRuntimeUIPreviewRcssDirty = false;

	if (RuntimeUIPreviewPath.empty())
	{
		RuntimeUIPreviewError = "Runtime UI path is empty.";
		return;
	}

	std::filesystem::path Path(FPaths::ToWide(RuntimeUIPreviewPath));
	if (Path.is_relative())
	{
		Path = std::filesystem::path(FPaths::RootDir()) / Path;
	}
	Path = Path.lexically_normal();

	if (!std::filesystem::exists(Path))
	{
		RuntimeUIPreviewError = FString("Runtime UI document not found: ") + RuntimeUIPreviewPath;
		return;
	}

	std::ifstream File(Path, std::ios::binary);
	if (!File)
	{
		RuntimeUIPreviewError = FString("Failed to open Runtime UI document: ") + RuntimeUIPreviewPath;
		return;
	}

	std::ostringstream Stream;
	Stream << File.rdbuf();
	RuntimeUIPreviewSource = Stream.str();
	RuntimeUIPreviewSourceEditBuffer = RuntimeUIPreviewSource;
	SyncRuntimeUIPreviewEditBytesFromString(RuntimeUIPreviewSourceEditBuffer, RuntimeUIPreviewSourceEditBytes);

	if (FindRuntimeUIPreviewLinkedStylesheetPath(RuntimeUIPreviewSource, RuntimeUIPreviewPath, RuntimeUIPreviewRcssPath))
	{
		RuntimeUIPreviewRcssSource = ReadRuntimeUIPreviewTextFile(std::filesystem::path(FPaths::ToWide(RuntimeUIPreviewRcssPath)));
		RuntimeUIPreviewRcssEditBuffer = RuntimeUIPreviewRcssSource;
		SyncRuntimeUIPreviewEditBytesFromString(RuntimeUIPreviewRcssEditBuffer, RuntimeUIPreviewRcssEditBytes);
	}

	CollectRuntimeUIAttributeValues(RuntimeUIPreviewSource, "data-action", RuntimeUIPreviewActionEvents);
	CollectRuntimeUIAttributeValues(RuntimeUIPreviewSource, "action", RuntimeUIPreviewActionEvents);
	CollectRuntimeUIAttributeValues(RuntimeUIPreviewSource, "id", RuntimeUIPreviewElementIds);
}

bool FEditorMainPanel::SaveRuntimeUIPreviewSources()
{
	RuntimeUIPreviewError.clear();
	if (RuntimeUIPreviewPath.empty())
	{
		RuntimeUIPreviewError = "Runtime UI path is empty.";
		return false;
	}

	std::filesystem::path RmlPath(FPaths::ToWide(RuntimeUIPreviewPath));
	if (RmlPath.is_relative())
	{
		RmlPath = std::filesystem::path(FPaths::RootDir()) / RmlPath;
	}
	RmlPath = RmlPath.lexically_normal();

	if (!WriteRuntimeUIPreviewTextFile(RmlPath, RuntimeUIPreviewSourceEditBuffer))
	{
		RuntimeUIPreviewError = FString("Failed to save Runtime UI document: ") + RuntimeUIPreviewPath;
		return false;
	}

	RuntimeUIPreviewSource = RuntimeUIPreviewSourceEditBuffer;
	bRuntimeUIPreviewSourceDirty = false;

	if (!RuntimeUIPreviewRcssPath.empty())
	{
		std::filesystem::path RcssPath(FPaths::ToWide(RuntimeUIPreviewRcssPath));
		if (RcssPath.is_relative())
		{
			RcssPath = std::filesystem::path(FPaths::RootDir()) / RcssPath;
		}
		RcssPath = RcssPath.lexically_normal();

		if (!WriteRuntimeUIPreviewTextFile(RcssPath, RuntimeUIPreviewRcssEditBuffer))
		{
			RuntimeUIPreviewError = FString("Failed to save Runtime UI stylesheet: ") + RuntimeUIPreviewRcssPath;
			return false;
		}

		RuntimeUIPreviewRcssSource = RuntimeUIPreviewRcssEditBuffer;
		bRuntimeUIPreviewRcssDirty = false;
	}

	RuntimeUIPreviewActionEvents.clear();
	RuntimeUIPreviewElementIds.clear();
	CollectRuntimeUIAttributeValues(RuntimeUIPreviewSource, "data-action", RuntimeUIPreviewActionEvents);
	CollectRuntimeUIAttributeValues(RuntimeUIPreviewSource, "action", RuntimeUIPreviewActionEvents);
	CollectRuntimeUIAttributeValues(RuntimeUIPreviewSource, "id", RuntimeUIPreviewElementIds);
	return true;
}

bool FEditorMainPanel::MountRuntimeUIPreviewInViewport(bool bForceReload)
{
	if (RuntimeUIPreviewPath.empty() || !RuntimeUIPreviewError.empty())
	{
		return false;
	}

	UUserWidget* Widget = nullptr;
	if (IsValid(RuntimeUIPreviewViewportWidget) &&
		RuntimeUIPreviewViewportWidget->GetDocumentPath() == RuntimeUIPreviewPath)
	{
		Widget = RuntimeUIPreviewViewportWidget;
	}
	else
	{
		UnmountRuntimeUIPreviewFromViewport();
		Widget = UUIManager::Get().CreateWidget(nullptr, RuntimeUIPreviewPath);
		RuntimeUIPreviewViewportWidget = Widget;
		RuntimeUIPreviewRuntimeEvents.clear();
	}

	if (!IsValid(Widget))
	{
		RuntimeUIPreviewError = "Failed to create Runtime UI preview widget.";
		return false;
	}

	if (bForceReload && Widget->IsInViewport())
	{
		Widget->RemoveFromParent();
	}

	Widget->SetWantsMouse(true);
	Widget->SetWantsKeyboard(true);
	Widget->SetWantsTextInput(true);
	Widget->SetBlocksGameInput(true);
	Widget->SetBlocksGameKeyboard(true);
	Widget->SetBlocksGameMouseLook(true);
	Widget->AddToViewport(10000);

	if (!Widget->IsDocumentLoaded())
	{
		Widget->RemoveFromParent();
		RuntimeUIPreviewError = FString("Failed to mount Runtime UI document in viewport: ") + RuntimeUIPreviewPath;
		return false;
	}
	return true;
}

void FEditorMainPanel::UnmountRuntimeUIPreviewFromViewport()
{
	if (IsValid(RuntimeUIPreviewViewportWidget) && RuntimeUIPreviewViewportWidget->IsInViewport())
	{
		RuntimeUIPreviewViewportWidget->RemoveFromParent();
	}
	RuntimeUIPreviewViewportWidget = nullptr;
}

bool FEditorMainPanel::IsRuntimeUIPreviewMounted() const
{
	return IsValid(RuntimeUIPreviewViewportWidget) && RuntimeUIPreviewViewportWidget->IsInViewport();
}

void FEditorMainPanel::PollRuntimeUIPreviewEvents()
{
	if (!IsRuntimeUIPreviewMounted())
	{
		return;
	}

	const TArray<FString> Events = RuntimeUIPreviewViewportWidget->PollActionEvents();
	for (const FString& EventName : Events)
	{
		RuntimeUIPreviewRuntimeEvents.push_back(EventName);
	}
	while (RuntimeUIPreviewRuntimeEvents.size() > 32)
	{
		RuntimeUIPreviewRuntimeEvents.erase(RuntimeUIPreviewRuntimeEvents.begin());
	}
}

void FEditorMainPanel::RenderShortcutOverlay()
{
	if (!bShowShortcutOverlay)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(320.0f, 150.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Shortcut Help", &bShowShortcutOverlay, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::End();
		return;
	}

	ImGui::TextUnformatted("File");
	ImGui::Separator();
	ImGui::TextUnformatted("Ctrl+N : New Scene");
	ImGui::TextUnformatted("Ctrl+O : Open Scene");
	ImGui::TextUnformatted("Ctrl+S : Save Scene");
	ImGui::TextUnformatted("Ctrl+Shift+S : Save Scene As");
	ImGui::Separator();
	ImGui::TextUnformatted("Editor");
	ImGui::Separator();
	ImGui::TextUnformatted("` : Focus console input / open console drawer");
	ImGui::TextUnformatted("Crtl + Spacebar : open content bowser");
	ImGui::TextUnformatted("F : Focus on selection");
	ImGui::TextUnformatted("Ctrl + LMB : Multi Picking (Toggle)");
	ImGui::TextUnformatted("Ctrl + Alt + LMB Drag : Area Selection");
	ImGui::TextUnformatted("F2: Actor or Component Rename");
	ImGui::Separator();
	ImGui::TextUnformatted("Physics Editor");
	ImGui::Separator();
	ImGui::TextUnformatted("F : Focus viewport on selected body, shape, or constraint");
	ImGui::TextUnformatted("Delete : Remove selected body or constraint");
	ImGui::Separator();
	ImGui::TextUnformatted("EsterEgg");
	ImGui::Separator();
	ImGui::TextUnformatted("Why does no one care about my shortcuts?");

	ImGui::End();
}

void FEditorMainPanel::RenderEditorDebugPanel()
{
	FEditorSettings& Settings = FEditorSettings::Get();
	if (!Settings.UI.bEditorDebug || !EditorEngine)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(520.0f, 320.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Editor Debug", &Settings.UI.bEditorDebug))
	{
		ImGui::End();
		return;
	}

	if (ImGui::CollapsingHeader("Picking", ImGuiTreeNodeFlags_DefaultOpen))
	{
		int32 CurrentPickingMode = static_cast<int32>(Settings.PickingMode);
		ImGui::RadioButton("ID Buffer", &CurrentPickingMode, static_cast<int32>(EEditorPickingMode::IdBuffer));
		ImGui::SameLine();
		ImGui::RadioButton("Ray", &CurrentPickingMode, static_cast<int32>(EEditorPickingMode::Ray));
		if (CurrentPickingMode >= 0 && CurrentPickingMode < static_cast<int32>(EEditorPickingMode::Count))
		{
			Settings.PickingMode = static_cast<EEditorPickingMode>(CurrentPickingMode);
		}
	}

	if (ImGui::CollapsingHeader("Scope Lens", ImGuiTreeNodeFlags_DefaultOpen))
	{
		static bool bDebugScopeLensEnabled = false;
		static float DebugScopeLensRadius = 0.42f;
		static float DebugScopeLensCenterX = 0.5f;
		static float DebugScopeLensCenterY = 0.5f;
		static float DebugScopeLensFeather = 0.08f;
		static float DebugScopeLensOuterBlur = 3.0f;
		static float DebugScopeLensEdgeBlur = 1.25f;
		static float DebugScopeLensZoomFOV = 0.39269908f;
		static float DebugScopeLensIntensity = 1.0f;
		static float DebugScopeLensLookScale = 0.275f;
		static float DebugScopeLensBlendTime = 0.08f;

		APlayerController* PC = EditorEngine->GetWorld() ? EditorEngine->GetWorld()->GetFirstPlayerController() : nullptr;
		APlayerCameraManager* CameraManager = PC ? PC->GetPlayerCameraManager() : nullptr;
		if (CameraManager)
		{
			const FCameraScopeLensState& ScopeLens = CameraManager->GetScopeLensProfile();
			bDebugScopeLensEnabled = CameraManager->IsScopeZoomEnabled();
			DebugScopeLensRadius = ScopeLens.Radius;
			DebugScopeLensCenterX = ScopeLens.CenterX;
			DebugScopeLensCenterY = ScopeLens.CenterY;
			DebugScopeLensFeather = ScopeLens.Feather;
			DebugScopeLensOuterBlur = ScopeLens.OuterBlurRadius;
			DebugScopeLensEdgeBlur = ScopeLens.EdgeBlurRadius;
			DebugScopeLensZoomFOV = ScopeLens.ZoomFOV;
			DebugScopeLensIntensity = ScopeLens.Intensity;
			DebugScopeLensLookScale = ScopeLens.LookSensitivityScale;
			DebugScopeLensBlendTime = ScopeLens.BlendTime;
		}

		bool bChanged = false;
		bChanged |= ImGui::Checkbox("Enabled", &bDebugScopeLensEnabled);
		bChanged |= ImGui::SliderFloat("Radius", &DebugScopeLensRadius, 0.05f, 0.95f, "%.3f");
		bChanged |= ImGui::SliderFloat("Center X", &DebugScopeLensCenterX, 0.0f, 1.0f, "%.3f");
		bChanged |= ImGui::SliderFloat("Center Y", &DebugScopeLensCenterY, 0.0f, 1.0f, "%.3f");
		bChanged |= ImGui::SliderFloat("Feather", &DebugScopeLensFeather, 0.001f, 0.35f, "%.3f");
		bChanged |= ImGui::SliderFloat("Outer Blur", &DebugScopeLensOuterBlur, 0.0f, 12.0f, "%.2f");
		bChanged |= ImGui::SliderFloat("Edge Blur", &DebugScopeLensEdgeBlur, 0.0f, 8.0f, "%.2f");
		bChanged |= ImGui::SliderFloat("Zoom FOV (rad)", &DebugScopeLensZoomFOV, 0.05f, 1.2f, "%.3f");
		bChanged |= ImGui::SliderFloat("Intensity", &DebugScopeLensIntensity, 0.0f, 1.0f, "%.2f");
		bChanged |= ImGui::SliderFloat("Look Sensitivity Scale", &DebugScopeLensLookScale, 0.05f, 1.0f, "%.3f");
		bChanged |= ImGui::SliderFloat("Blend Time", &DebugScopeLensBlendTime, 0.0f, 0.5f, "%.3f");

		if (CameraManager && bChanged)
		{
			CameraManager->SetScopeLensProfile(
				DebugScopeLensRadius,
				DebugScopeLensOuterBlur,
				DebugScopeLensZoomFOV,
				DebugScopeLensFeather,
				DebugScopeLensEdgeBlur,
				DebugScopeLensIntensity,
				DebugScopeLensLookScale,
				DebugScopeLensBlendTime,
				DebugScopeLensCenterX,
				DebugScopeLensCenterY);
			CameraManager->SetScopeZoomEnabled(bDebugScopeLensEnabled);
		}
	}

	if (ImGui::CollapsingHeader("Place Actors (Grid)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const int32 OptionCount = static_cast<int32>(sizeof(GDebugPlaceActorOptions) / sizeof(GDebugPlaceActorOptions[0]));
		if (DebugPlaceActorTypeIndex < 0)
		{
			DebugPlaceActorTypeIndex = 0;
		}
		if (DebugPlaceActorTypeIndex >= OptionCount)
		{
			DebugPlaceActorTypeIndex = OptionCount - 1;
		}

		const char* CurrentActorLabel = GDebugPlaceActorOptions[DebugPlaceActorTypeIndex].Label;
		if (ImGui::BeginCombo("Actor Type", CurrentActorLabel))
		{
			for (int32 Index = 0; Index < OptionCount; ++Index)
			{
				const bool bSelected = (DebugPlaceActorTypeIndex == Index);
				if (ImGui::Selectable(GDebugPlaceActorOptions[Index].Label, bSelected))
				{
					DebugPlaceActorTypeIndex = Index;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::DragInt("Rows", &DebugGridRows, 1.0f, 1, 1024, "%d");
		ImGui::DragInt("Cols", &DebugGridCols, 1.0f, 1, 1024, "%d");
		ImGui::DragInt("Layers", &DebugGridLayers, 1.0f, 1, 256, "%d");
		ImGui::DragFloat("Grid Spacing", &DebugGridSpacing, 0.1f, 0.1f, 1000.0f, "%.2f");
		ImGui::Checkbox("Center Grid Around Origin", &bDebugGridCenter);

		ImGui::Separator();
		ImGui::Checkbox("Use Camera Forward Origin", &bDebugUseCameraOrigin);
		if (bDebugUseCameraOrigin)
		{
			ImGui::DragFloat("Camera Forward Distance", &DebugCameraForwardDistance, 0.5f, 0.0f, 100000.0f, "%.1f");
		}
		else
		{
			ImGui::DragFloat3("Manual Origin", &DebugManualGridOrigin.X, 0.1f, -100000.0f, 100000.0f, "%.2f");
		}

		ImGui::Separator();
		ImGui::Checkbox("Random Yaw", &bDebugRandomYaw);
		ImGui::BeginDisabled(!bDebugRandomYaw);
		ImGui::DragFloat("Yaw Range (+/-)", &DebugRandomYawRange, 1.0f, 0.0f, 180.0f, "%.1f");
		ImGui::EndDisabled();

		ImGui::Checkbox("Apply Position Jitter", &bDebugApplyJitter);
		ImGui::BeginDisabled(!bDebugApplyJitter);
		ImGui::DragFloat("Jitter XY", &DebugJitterXY, 0.05f, 0.0f, 1000.0f, "%.2f");
		ImGui::DragFloat("Jitter Z", &DebugJitterZ, 0.05f, 0.0f, 1000.0f, "%.2f");
		ImGui::EndDisabled();

		if (DebugGridRows < 1) DebugGridRows = 1;
		if (DebugGridCols < 1) DebugGridCols = 1;
		if (DebugGridLayers < 1) DebugGridLayers = 1;
		if (DebugGridSpacing < 0.1f) DebugGridSpacing = 0.1f;
		if (DebugRandomYawRange < 0.0f) DebugRandomYawRange = 0.0f;
		if (DebugRandomYawRange > 180.0f) DebugRandomYawRange = 180.0f;
		if (DebugJitterXY < 0.0f) DebugJitterXY = 0.0f;
		if (DebugJitterZ < 0.0f) DebugJitterZ = 0.0f;

		const long long TotalSpawnCount =
			static_cast<long long>(DebugGridRows) *
			static_cast<long long>(DebugGridCols) *
			static_cast<long long>(DebugGridLayers);
		ImGui::Text("Total Actors: %lld", TotalSpawnCount);
		ImGui::Text("Last Batch: %u", static_cast<uint32>(DebugLastSpawnedActors.size()));

		if (ImGui::Button("Spawn Grid Actors"))
		{
			UWorld* World = EditorEngine->GetWorld();
			if (!World)
			{
				FEditorConsoleWidget::AddLog("Grid spawn failed: invalid world\n");
			}
			else
			{
				FVector GridOrigin = DebugManualGridOrigin;
				FVector GridRight(1.0f, 0.0f, 0.0f);
				FVector GridForward(0.0f, 1.0f, 0.0f);
				if (bDebugUseCameraOrigin)
				{
					// D.3: 컴포넌트가 아닌 POV 통화로 read.
					FMinimalViewInfo POV;
					if (EditorEngine->GetActiveViewportPOV(POV))
					{
						FVector CameraForward = POV.Rotation.GetForwardVector();
						CameraForward.Z = 0.0f;
						if (CameraForward.Length() > 0.0001f)
						{
							CameraForward.Normalize();
							GridForward = CameraForward;
							GridRight = FVector(-CameraForward.Y, CameraForward.X, 0.0f);
						}
						GridOrigin = POV.Location + POV.Rotation.GetForwardVector() * DebugCameraForwardDistance;
					}
				}

				const float RowOffset = bDebugGridCenter ? (static_cast<float>(DebugGridRows - 1) * 0.5f) : 0.0f;
				const float ColOffset = bDebugGridCenter ? (static_cast<float>(DebugGridCols - 1) * 0.5f) : 0.0f;
				const float LayerOffset = bDebugGridCenter ? (static_cast<float>(DebugGridLayers - 1) * 0.5f) : 0.0f;

				std::mt19937 RNG{ std::random_device{}() };
				std::uniform_real_distribution<float> YawDist(-DebugRandomYawRange, DebugRandomYawRange);
				std::uniform_real_distribution<float> JitterXYDist(-DebugJitterXY, DebugJitterXY);
				std::uniform_real_distribution<float> JitterZDist(-DebugJitterZ, DebugJitterZ);

				TArray<AActor*> SpawnedActors;
				SpawnedActors.reserve(static_cast<size_t>(TotalSpawnCount));
				int32 SpawnedCount = 0;
				const FDebugPlaceActorOption& Option = GDebugPlaceActorOptions[DebugPlaceActorTypeIndex];

				for (int32 Layer = 0; Layer < DebugGridLayers; ++Layer)
				{
					for (int32 Row = 0; Row < DebugGridRows; ++Row)
					{
						for (int32 Col = 0; Col < DebugGridCols; ++Col)
						{
							FVector SpawnLocation = GridOrigin
								+ GridRight * ((static_cast<float>(Col) - ColOffset) * DebugGridSpacing)
								+ GridForward * ((static_cast<float>(Row) - RowOffset) * DebugGridSpacing)
								+ FVector(0.0f, 0.0f, (static_cast<float>(Layer) - LayerOffset) * DebugGridSpacing);

							if (bDebugApplyJitter)
							{
								SpawnLocation += GridRight * JitterXYDist(RNG)
									+ GridForward * JitterXYDist(RNG)
									+ FVector(0.0f, 0.0f, JitterZDist(RNG));
							}

							AActor* SpawnedActor = EditorEngine->SpawnPlaceActor(Option.Type, SpawnLocation);
							if (!SpawnedActor)
							{
								continue;
							}

							if (bDebugRandomYaw)
							{
								SpawnedActor->SetActorRotation(FVector(0.0f, YawDist(RNG), 0.0f));
							}

							SpawnedActors.push_back(SpawnedActor);
							++SpawnedCount;
						}
					}
				}

				DebugLastSpawnedActors = std::move(SpawnedActors);
				FEditorConsoleWidget::AddLog("Grid placed: %d actors\n", SpawnedCount);
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Clear Last Batch"))
		{
			bPendingClearLastBatch = true;
		}
	}

	ImGui::End();
}

void FEditorMainPanel::RenderContentBrowserDrawer(float DeltaTime)
{
	constexpr float DrawerMaxHeight = 320.0f;
	constexpr float AnimSpeed = 16.0f;
	constexpr float ConsoleDrawerMaxHeight = 320.0f;

	const bool bShouldBeVisible = !bHideEditorWindows
		&& DocumentTabs.IsLevelEditorActive()
		&& FEditorSettings::Get().UI.bContentBrowser;
	const float TargetAnim = bShouldBeVisible ? 1.0f : 0.0f;
	float Alpha = DeltaTime * AnimSpeed;
	if (Alpha > 1.0f)
	{
		Alpha = 1.0f;
	}
	ContentBrowserDrawerAnim += (TargetAnim - ContentBrowserDrawerAnim) * Alpha;
	if (!bShouldBeVisible && ContentBrowserDrawerAnim < 0.001f)
	{
		ContentBrowserDrawerAnim = 0.0f;
	}
	if (ContentBrowserDrawerAnim <= 0.001f)
	{
		return;
	}

	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	const float FooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	const float ConsoleHeight = ConsoleDrawerMaxHeight * ConsoleDrawerAnim;
	const float DrawerHeight = DrawerMaxHeight * ContentBrowserDrawerAnim;
	if (DrawerHeight <= 1.0f)
	{
		return;
	}

	const ImVec2 DrawerPos(
		MainViewport->WorkPos.x,
		MainViewport->WorkPos.y + MainViewport->WorkSize.y - FooterHeight - ConsoleHeight - DrawerHeight);
	const ImVec2 DrawerSize(MainViewport->WorkSize.x, DrawerHeight);
	ImGui::SetNextWindowPos(DrawerPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(DrawerSize, ImGuiCond_Always);
	if (bBringContentBrowserDrawerToFrontNextFrame)
	{
		ImGui::SetNextWindowFocus();
	}

	ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoCollapse;

	ContentBrowserWidget.RenderWithFlags(DeltaTime, Flags);
	bBringContentBrowserDrawerToFrontNextFrame = false;
}

void FEditorMainPanel::RenderConsoleDrawer(float DeltaTime)
{
	constexpr float DrawerMaxHeight = 320.0f;
	constexpr float AnimSpeed = 16.0f;

	const float TargetAnim = bConsoleDrawerVisible ? 1.0f : 0.0f;
	float Alpha = DeltaTime * AnimSpeed;
	if (Alpha > 1.0f)
	{
		Alpha = 1.0f;
	}
	ConsoleDrawerAnim += (TargetAnim - ConsoleDrawerAnim) * Alpha;
	if (!bConsoleDrawerVisible && ConsoleDrawerAnim < 0.001f)
	{
		ConsoleDrawerAnim = 0.0f;
	}
	if (ConsoleDrawerAnim <= 0.001f)
	{
		return;
	}

	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	const float FooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	const float DrawerHeight = DrawerMaxHeight * ConsoleDrawerAnim;
	if (DrawerHeight <= 1.0f)
	{
		return;
	}

	const ImVec2 DrawerPos(
		MainViewport->WorkPos.x,
		MainViewport->WorkPos.y + MainViewport->WorkSize.y - FooterHeight - DrawerHeight);
	const ImVec2 DrawerSize(MainViewport->WorkSize.x, DrawerHeight);
	ImGui::SetNextWindowPos(DrawerPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(DrawerSize, ImGuiCond_Always);
	if (bBringConsoleDrawerToFrontNextFrame)
	{
		ImGui::SetNextWindowFocus();
	}

	ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoFocusOnAppearing;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.11f, 0.98f));
	if (ImGui::Begin("##ConsoleDrawer", nullptr, Flags))
	{
		ConsoleWidget.RenderDrawerToolbar();
		ImGui::Separator();
		ConsoleWidget.RenderLogContents(0.0f);
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);

	bBringConsoleDrawerToFrontNextFrame = false;
}

void FEditorMainPanel::RenderFooterOverlay(float DeltaTime)
{
	(void)DeltaTime;

	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	const float FooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	const ImVec2 FooterPos(
		MainViewport->WorkPos.x,
		MainViewport->WorkPos.y + MainViewport->WorkSize.y - FooterHeight);
	const ImVec2 FooterSize(MainViewport->WorkSize.x, FooterHeight);

	ImGui::SetNextWindowPos(FooterPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(FooterSize, ImGuiCond_Always);
	ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoNav;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.065f, 0.075f, 0.98f));
	if (ImGui::Begin("##EditorFooter", nullptr, Flags))
	{
		if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
		{
			switch (ConsoleBacktickCycleState)
			{
			case 0:
				ConsoleBacktickCycleState = 1;
				bConsoleDrawerVisible = false;
				bFocusConsoleInputNextFrame = true;
				break;
			case 1:
				ConsoleBacktickCycleState = 2;
				bConsoleDrawerVisible = true;
				FEditorSettings::Get().UI.bContentBrowser = false;
				ContentBrowserDrawerAnim = 0.0f;
				bBringContentBrowserDrawerToFrontNextFrame = false;
				bBringConsoleDrawerToFrontNextFrame = true;
				bFocusConsoleInputNextFrame = true;
				break;
			default:
				ConsoleBacktickCycleState = 0;
				bConsoleDrawerVisible = false;
				bFocusConsoleInputNextFrame = false;
				bFocusConsoleButtonNextFrame = true;
				break;
			}
		}

		if (bFocusConsoleButtonNextFrame)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusConsoleButtonNextFrame = false;
		}

		const bool bContentBrowserOpen = FEditorSettings::Get().UI.bContentBrowser;
		if (bContentBrowserOpen)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		if (ImGui::SmallButton("Content"))
		{
			ToggleContentBrowserDrawer();
		}
		if (bContentBrowserOpen)
		{
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();

		if (ImGui::SmallButton("Console"))
		{
			ToggleConsoleDrawer(true);
		}

		ImGui::SameLine();
		const bool bDrawerOpen = ConsoleDrawerAnim > 0.5f;
		const float InputWidth = MainViewport->WorkSize.x * (bDrawerOpen ? 0.35f : 0.175f);
		ConsoleWidget.RenderInputLine("##FooterConsoleInput", InputWidth, bFocusConsoleInputNextFrame);
		if (bFocusConsoleInputNextFrame)
		{
			ConsoleBacktickCycleState = bConsoleDrawerVisible ? 2 : 1;
		}
		bFocusConsoleInputNextFrame = false;

		ImGui::SameLine();
		ImGui::Text("Domain: %s", EditorEngine && EditorEngine->IsPlayingInEditor() ? "PIE" : "Editor");

		const FString LevelLabel = EditorEngine && EditorEngine->HasCurrentLevelFilePath()
			? FString("Level: ") + EditorEngine->GetCurrentLevelFilePath()
			: FString("Level: Unsaved");
		const float LevelWidth = ImGui::CalcTextSize(LevelLabel.c_str()).x;
		const float LevelX = MainViewport->WorkSize.x - ImGui::GetStyle().WindowPadding.x - LevelWidth;

		const char* LatestLog = ConsoleWidget.GetLatestLogMessage();
		if (LatestLog && LatestLog[0] != '\0')
		{
			const float LogWidth = ImGui::CalcTextSize(LatestLog).x;
			float LogX = LevelX - 16.0f - LogWidth;
			const float MinLogX = ImGui::GetCursorPosX() + 8.0f;
			if (LogX < MinLogX)
			{
				LogX = MinLogX;
			}
			ImGui::SameLine(LogX);
			ImGui::TextUnformatted(LatestLog);
		}

		ImGui::SameLine(LevelX);
		ImGui::TextUnformatted(LevelLabel.c_str());
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

void FEditorMainPanel::Update()
{
	HandleGlobalShortcuts();
	ProcessPendingDebugActions();

	ImGuiIO& IO = ImGui::GetIO();

	// GuiState 는 ImGui IO 의 충실한 미러 한 곳뿐.
	// "뷰포트 위면 해제" 핵은 제거 — 입력 소유권은 이제 FSlateApplication 의
	// ImGui 인지 hover 가 단독으로 결정한다.
	InputSystem::Get().GetGuiInputState().bUsingMouse     = IO.WantCaptureMouse;
	InputSystem::Get().GetGuiInputState().bUsingKeyboard  = IO.WantCaptureKeyboard || bShowShortcutOverlay;
	InputSystem::Get().GetGuiInputState().bUsingTextInput = IO.WantTextInput;

	// ImGui 사실을 입력 소유권 중재자에 주입
	FSlateApplication::Get().SetTextInputActive(IO.WantTextInput);

	// IME는 ImGui가 텍스트 입력을 원할 때만 활성화.
	if (Window)
	{
		HWND hWnd = Window->GetHWND();
		if (IO.WantTextInput)
		{
			ImmAssociateContextEx(hWnd, NULL, IACE_DEFAULT);
		}
		else
		{
			ImmAssociateContext(hWnd, NULL);
		}
	}
}

void FEditorMainPanel::ToggleContentBrowserDrawer()
{
	FEditorSettings& Settings = FEditorSettings::Get();
	Settings.UI.bContentBrowser = !Settings.UI.bContentBrowser;
	if (Settings.UI.bContentBrowser)
	{
		bConsoleDrawerVisible = false;
		ConsoleDrawerAnim = 0.0f;
		ConsoleBacktickCycleState = 0;
		bFocusConsoleInputNextFrame = false;
		bBringConsoleDrawerToFrontNextFrame = false;
	}
	bBringContentBrowserDrawerToFrontNextFrame = Settings.UI.bContentBrowser;
}

void FEditorMainPanel::ToggleConsoleDrawer(bool bFocusInput)
{
	bConsoleDrawerVisible = !bConsoleDrawerVisible;
	if (bConsoleDrawerVisible)
	{
		FEditorSettings::Get().UI.bContentBrowser = false;
		ContentBrowserDrawerAnim = 0.0f;
		bBringContentBrowserDrawerToFrontNextFrame = false;
	}
	bBringConsoleDrawerToFrontNextFrame = bConsoleDrawerVisible;
	bFocusConsoleInputNextFrame = bConsoleDrawerVisible && bFocusInput;
	ConsoleBacktickCycleState = bConsoleDrawerVisible ? 2 : 0;
	if (!bConsoleDrawerVisible)
	{
		bFocusConsoleButtonNextFrame = true;
	}
}

void FEditorMainPanel::ProcessPendingDebugActions()
{
	if (!EditorEngine)
	{
		return;
	}

	FLuaDebugEditorFocusRequest LuaDebugFocus;
	if (FLuaDebugManager::PeekEditorFocusRequest(LuaDebugFocus)
		&& LuaDebugFocus.Serial != LastLuaDebugEditorFocusSerial
		&& !LuaDebugFocus.Location.BlueprintPath.empty())
	{
		LastLuaDebugEditorFocusSerial = LuaDebugFocus.Serial;
		if (EditorEngine && EditorEngine->IsPIEPossessedMode())
		{
			EditorEngine->TogglePIEControlMode();
		}
		FSlateApplication::Get().ClearInputOwner();
		InputSystem::Get().ResetTransientState();
		if (ULuaBlueprintAsset* Blueprint = FLuaBlueprintManager::Get().Load(LuaDebugFocus.Location.BlueprintPath))
		{
			ShowEditorWindows();
			OpenAssetEditorForObject(Blueprint);
		}
	}

	if (!bPendingClearLastBatch)
	{
		return;
	}
	bPendingClearLastBatch = false;

	UWorld* World = EditorEngine->GetWorld();
	int32 DestroyedCount = 0;
	if (!World)
	{
		DebugLastSpawnedActors.clear();
		FEditorConsoleWidget::AddLog("Grid cleared: 0 actors\n");
		return;
	}

	EditorEngine->GetSelectionManager().ClearSelection();
	for (AActor* Actor : DebugLastSpawnedActors)
	{
		if (!IsValid(Actor) || Actor->GetWorld() != World)
		{
			continue;
		}

		World->DestroyActor(Actor);
		++DestroyedCount;
	}

	DebugLastSpawnedActors.clear();
	FEditorConsoleWidget::AddLog("Grid cleared: %d actors\n", DestroyedCount);
}

void FEditorMainPanel::HandleGlobalShortcuts()
{
	if (!EditorEngine)
	{
		return;
	}
	if (EditorEngine->IsPIEPossessedMode())
	{
		return;
	}

	ImGuiIO& IO = ImGui::GetIO();
	if (IO.WantTextInput)
	{
		return;
	}

	InputSystem& Input = InputSystem::Get();
	if (!Input.GetKey(VK_CONTROL))
	{
		return;
	}

	if (Input.GetKeyDown(VK_SPACE))
	{
		ToggleContentBrowserDrawer();
		return;
	}

	const bool bShift = Input.GetKey(VK_SHIFT);
	if (Input.GetKeyDown('N'))
	{
		EditorEngine->NewScene();
	}
	else if (Input.GetKeyDown('O'))
	{
		EditorEngine->LoadSceneWithDialog();
	}
	else if (Input.GetKeyDown('S'))
	{
		if (bShift)
		{
			EditorEngine->SaveSceneAsWithDialog();
		}
		else
		{
			EditorEngine->SaveScene();
		}
	}
}

void FEditorMainPanel::HideEditorWindows()
{
	if (bHasSavedUIVisibility)
	{
		bHideEditorWindows = true;
		bShowWidgetList = false;
		return;
	}

	FEditorSettings& Settings = FEditorSettings::Get();
	SavedUIVisibility = Settings.UI;
	bSavedShowWidgetList = bShowWidgetList;
	bHasSavedUIVisibility = true;
	bHideEditorWindows = true;
	bShowWidgetList = false;

	Settings.UI.bConsole = false;
	Settings.UI.bControl = false;
	Settings.UI.bProperty = false;
	Settings.UI.bScene = false;
	Settings.UI.bStat = false;
	Settings.UI.bContentBrowser = false;
	Settings.UI.bImGUISettings = false;
	Settings.UI.bEditorDebug = false;
	Settings.UI.bShadowMapDebug = false;
}

void FEditorMainPanel::ShowEditorWindows()
{
	if (!bHasSavedUIVisibility)
	{
		bHideEditorWindows = false;
		return;
	}

	FEditorSettings& Settings = FEditorSettings::Get();
	Settings.UI = SavedUIVisibility;
	bShowWidgetList = bSavedShowWidgetList;
	bHideEditorWindows = false;
	bHasSavedUIVisibility = false;
}

void FEditorMainPanel::HideEditorWindowsForPIE()
{
	HideEditorWindows();
}

void FEditorMainPanel::RestoreEditorWindowsAfterPIE()
{
	ShowEditorWindows();
}

void FEditorMainPanel::OpenAssetEditorForObject(UObject* Object)
{
	FAssetEditorOpenResult Result = AssetEditorManager.OpenEditorForObject(Object);
	if (Result.bOpened && Result.bDocumentTab)
	{
		DocumentTabs.OpenOrFocusTab(Result.TabId, Result.Label, true);
	}
}

void FEditorMainPanel::OpenLevelActorSequencer(UActorSequenceComponent* SequenceComp)
{
	if (!IsValid(SequenceComp))
	{
		return;
	}

	LevelActorSequencerWidget.Open(SequenceComp);
	LevelActorSequencerWidget.RequestFocus();
}

void FEditorMainPanel::OpenRuntimeUIPreviewDocument(const FString& DocumentPath)
{
	RuntimeUIPreviewPath = DocumentPath;
	ReloadRuntimeUIPreviewDocument();

	FEditorDocumentTabId TabId;
	TabId.Kind = EEditorDocumentTabKind::RuntimeUIPreview;
	TabId.PayloadId = MakeRuntimeUIPreviewPayloadId();

	FString Label = GetFileStemForDisplay(DocumentPath);
	if (Label.empty())
	{
		Label = "Runtime UI";
	}
	DocumentTabs.OpenOrFocusTab(TabId, Label, true);
}

void FEditorMainPanel::CollectAssetEditorPreviewViewportClients(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (!DocumentTabs.IsLevelEditorActive())
	{
		AssetEditorManager.CollectPreviewViewportClientsForTab(DocumentTabs.GetActiveTab(), OutClients);
	}
}

bool FEditorMainPanel::IsMouseOverAssetEditorPreviewViewport() const
{
	if (DocumentTabs.IsLevelEditorActive())
	{
		return false;
	}

	return AssetEditorManager.IsMouseOverEditorViewportForTab(DocumentTabs.GetActiveTab());
}
