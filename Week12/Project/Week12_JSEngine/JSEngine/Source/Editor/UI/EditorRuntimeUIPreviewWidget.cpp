#include "Editor/UI/EditorRuntimeUIPreviewWidget.h"

#include "Editor/EditorEngine.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Engine/Input/InputSystem.h"
#include "Render/Resource/Texture.h"
#include "Runtime/ViewportRect.h"

#include "ImGui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <commdlg.h>
#include <cstdio>
#include <filesystem>
#include <utility>
#include <Windows.h>

#pragma comment(lib, "Comdlg32.lib")

namespace
{
	struct FPreviewResolutionPreset
	{
		const char* Label;
		int32 Width;
		int32 Height;
	};

	constexpr FPreviewResolutionPreset PreviewResolutionPresets[] =
	{
		{ "1920 x 1080", 1920, 1080 },
		{ "1600 x 900", 1600, 900 },
		{ "1280 x 720", 1280, 720 },
		{ "1024 x 768", 1024, 768 },
		{ "800 x 600", 800, 600 },
		{ "Custom", 0, 0 },
	};

	void GetPreviewResolution(int32 PresetIndex, int32 CustomWidth, int32 CustomHeight, int32& OutWidth, int32& OutHeight)
	{
		const int32 PresetCount = static_cast<int32>(sizeof(PreviewResolutionPresets) / sizeof(PreviewResolutionPresets[0]));
		PresetIndex = std::clamp(PresetIndex, 0, PresetCount - 1);
		const FPreviewResolutionPreset& Preset = PreviewResolutionPresets[PresetIndex];
		if (Preset.Width > 0 && Preset.Height > 0)
		{
			OutWidth = Preset.Width;
			OutHeight = Preset.Height;
			return;
		}

		OutWidth = std::max(320, CustomWidth);
		OutHeight = std::max(180, CustomHeight);
	}

	FString ToLower(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(),
			[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
		return Value;
	}

	const char* GetRuntimeUIWidgetTypeLabel(ERuntimeUIWidgetType Type)
	{
		switch (Type)
		{
		case ERuntimeUIWidgetType::Canvas:
			return "Canvas";
		case ERuntimeUIWidgetType::Panel:
			return "Panel";
		case ERuntimeUIWidgetType::Text:
			return "Text";
		case ERuntimeUIWidgetType::Image:
			return "Image";
		case ERuntimeUIWidgetType::Button:
			return "Button";
		default:
			return "Widget";
		}
	}

	const char* GetRuntimeUIImageFitLabel(ERuntimeUIImageFit ImageFit)
	{
		switch (ImageFit)
		{
		case ERuntimeUIImageFit::Contain:
			return "Contain";
		case ERuntimeUIImageFit::Cover:
			return "Cover";
		case ERuntimeUIImageFit::Stretch:
		default:
			return "Stretch";
		}
	}

	const char* GetRuntimeUILayoutModeLabel(ERuntimeUILayoutMode LayoutMode)
	{
		switch (LayoutMode)
		{
		case ERuntimeUILayoutMode::Horizontal:
			return "Horizontal";
		case ERuntimeUILayoutMode::Vertical:
			return "Vertical";
		case ERuntimeUILayoutMode::Free:
		default:
			return "Free";
		}
	}

	const char* GetRuntimeUILayoutAlignmentLabel(ERuntimeUILayoutAlignment Alignment)
	{
		switch (Alignment)
		{
		case ERuntimeUILayoutAlignment::Center:
			return "Center";
		case ERuntimeUILayoutAlignment::End:
			return "End";
		case ERuntimeUILayoutAlignment::Stretch:
			return "Stretch";
		case ERuntimeUILayoutAlignment::Start:
		default:
			return "Start";
		}
	}

	const char* GetRuntimeUILayoutSizeRuleLabel(ERuntimeUILayoutSizeRule SizeRule)
	{
		switch (SizeRule)
		{
		case ERuntimeUILayoutSizeRule::Fill:
			return "Fill";
		case ERuntimeUILayoutSizeRule::Auto:
		default:
			return "Auto";
		}
	}

	bool IsRuntimeUILayoutContainer(const FRuntimeUIWidgetNode& Node)
	{
		return (Node.Type == ERuntimeUIWidgetType::Canvas || Node.Type == ERuntimeUIWidgetType::Panel)
			&& Node.LayoutMode != ERuntimeUILayoutMode::Free;
	}

	bool GetRuntimeUITextureSize(UTexture* Texture, float& OutWidth, float& OutHeight)
	{
		OutWidth = 0.0f;
		OutHeight = 0.0f;
		if (!Texture || !Texture->GetSRV())
		{
			return false;
		}

		ID3D11Resource* Resource = nullptr;
		Texture->GetSRV()->GetResource(&Resource);
		if (!Resource)
		{
			return false;
		}

		ID3D11Texture2D* Texture2D = nullptr;
		const HRESULT Hr = Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture2D));
		Resource->Release();
		if (FAILED(Hr) || !Texture2D)
		{
			return false;
		}

		D3D11_TEXTURE2D_DESC Desc = {};
		Texture2D->GetDesc(&Desc);
		Texture2D->Release();
		OutWidth = static_cast<float>(Desc.Width);
		OutHeight = static_cast<float>(Desc.Height);
		return OutWidth > 0.0f && OutHeight > 0.0f;
	}

	void CalculateRuntimeUIImageFit(
		ERuntimeUIImageFit ImageFit,
		const ImVec2& BoxMin,
		const ImVec2& BoxMax,
		float TextureWidth,
		float TextureHeight,
		ImVec2& OutMin,
		ImVec2& OutMax,
		ImVec2& OutUvMin,
		ImVec2& OutUvMax)
	{
		OutMin = BoxMin;
		OutMax = BoxMax;
		OutUvMin = ImVec2(0.0f, 0.0f);
		OutUvMax = ImVec2(1.0f, 1.0f);

		const float BoxWidth = std::max(1.0f, BoxMax.x - BoxMin.x);
		const float BoxHeight = std::max(1.0f, BoxMax.y - BoxMin.y);
		if (TextureWidth <= 0.0f || TextureHeight <= 0.0f || ImageFit == ERuntimeUIImageFit::Stretch)
		{
			return;
		}

		const float TextureAspect = TextureWidth / TextureHeight;
		const float BoxAspect = BoxWidth / BoxHeight;
		if (ImageFit == ERuntimeUIImageFit::Contain)
		{
			float DrawWidth = BoxWidth;
			float DrawHeight = BoxHeight;
			if (TextureAspect > BoxAspect)
			{
				DrawHeight = BoxWidth / TextureAspect;
			}
			else
			{
				DrawWidth = BoxHeight * TextureAspect;
			}

			OutMin = ImVec2(BoxMin.x + (BoxWidth - DrawWidth) * 0.5f, BoxMin.y + (BoxHeight - DrawHeight) * 0.5f);
			OutMax = ImVec2(OutMin.x + DrawWidth, OutMin.y + DrawHeight);
			return;
		}

		if (ImageFit == ERuntimeUIImageFit::Cover)
		{
			if (TextureAspect > BoxAspect)
			{
				const float VisibleU = std::clamp(BoxAspect / TextureAspect, 0.0f, 1.0f);
				OutUvMin.x = (1.0f - VisibleU) * 0.5f;
				OutUvMax.x = OutUvMin.x + VisibleU;
			}
			else
			{
				const float VisibleV = std::clamp(TextureAspect / BoxAspect, 0.0f, 1.0f);
				OutUvMin.y = (1.0f - VisibleV) * 0.5f;
				OutUvMax.y = OutUvMin.y + VisibleV;
			}
		}
	}

	bool DrawRuntimeUIStringInput(const char* Label, FString& Value)
	{
		char Buffer[260] = {};
		strncpy_s(Buffer, Value.c_str(), _TRUNCATE);
		if (ImGui::InputText(Label, Buffer, IM_ARRAYSIZE(Buffer)))
		{
			Value = Buffer;
			return true;
		}
		return false;
	}

	FVector4 AdjustRuntimeUIButtonColor(const FVector4& Color, float RgbScale, float AlphaScale = 1.0f)
	{
		return FVector4(
			std::clamp(Color.X * RgbScale, 0.0f, 1.0f),
			std::clamp(Color.Y * RgbScale, 0.0f, 1.0f),
			std::clamp(Color.Z * RgbScale, 0.0f, 1.0f),
			std::clamp(Color.W * AlphaScale, 0.0f, 1.0f));
	}

	FRuntimeUIButtonStateStyle MakeRuntimeUIButtonStateStyle(
		const FRuntimeUIWidgetNode& Node,
		float BackgroundScale,
		float TextScale,
		float BorderScale,
		float AlphaScale = 1.0f)
	{
		FRuntimeUIButtonStateStyle Style;
		Style.BackgroundColor = AdjustRuntimeUIButtonColor(Node.BackgroundColor, BackgroundScale, AlphaScale);
		Style.TextColor = AdjustRuntimeUIButtonColor(Node.TextColor, TextScale, AlphaScale);
		Style.BorderColor = AdjustRuntimeUIButtonColor(Node.BorderColor, BorderScale, AlphaScale);
		return Style;
	}

	void DeriveRuntimeUIButtonStateStyles(FRuntimeUIWidgetNode& Node)
	{
		Node.ButtonHoverStyle = MakeRuntimeUIButtonStateStyle(Node, 1.14f, 1.0f, 1.12f);
		Node.ButtonPressedStyle = MakeRuntimeUIButtonStateStyle(Node, 0.82f, 0.94f, 0.88f);
		Node.ButtonDisabledStyle = MakeRuntimeUIButtonStateStyle(Node, 0.52f, 0.62f, 0.45f, 0.58f);
	}

	void ApplyRuntimeUITextPreset(FRuntimeUIWidgetNode& Node, const char* PresetName)
	{
		if (std::strcmp(PresetName, "Title") == 0)
		{
			Node.FontFamily = "Nexon Lv1 Gothic";
			Node.FontSize = 56.0f;
			Node.FontWeight = 700;
			Node.LineHeight = 64.0f;
			Node.LetterSpacing = 0.0f;
			Node.bTextWrap = false;
			Node.TextAlign = "center";
			Node.TextColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else if (std::strcmp(PresetName, "Heading") == 0)
		{
			Node.FontFamily = "Nexon Lv1 Gothic";
			Node.FontSize = 34.0f;
			Node.FontWeight = 700;
			Node.LineHeight = 42.0f;
			Node.LetterSpacing = 0.0f;
			Node.bTextWrap = false;
			Node.TextAlign = "left";
			Node.TextColor = FVector4(0.92f, 0.95f, 1.0f, 1.0f);
		}
		else if (std::strcmp(PresetName, "Body") == 0)
		{
			Node.FontFamily = "Malgun Gothic";
			Node.FontSize = 22.0f;
			Node.FontWeight = 400;
			Node.LineHeight = 30.0f;
			Node.LetterSpacing = 0.0f;
			Node.bTextWrap = true;
			Node.TextAlign = "left";
			Node.TextColor = FVector4(0.82f, 0.86f, 0.92f, 1.0f);
		}
		else if (std::strcmp(PresetName, "Caption") == 0)
		{
			Node.FontFamily = "Malgun Gothic";
			Node.FontSize = 14.0f;
			Node.FontWeight = 400;
			Node.LineHeight = 18.0f;
			Node.LetterSpacing = 0.4f;
			Node.bTextWrap = false;
			Node.TextAlign = "left";
			Node.TextColor = FVector4(0.58f, 0.64f, 0.72f, 1.0f);
		}
		else if (std::strcmp(PresetName, "Button") == 0)
		{
			Node.FontFamily = "Nexon Lv1 Gothic";
			Node.FontSize = 22.0f;
			Node.FontWeight = 700;
			Node.LineHeight = std::max(1.0f, Node.Size.Y);
			Node.LetterSpacing = 0.0f;
			Node.bTextWrap = false;
			Node.TextAlign = "center";
			Node.TextColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	void ApplyRuntimeUIStylePreset(FRuntimeUIWidgetNode& Node, const char* PresetName)
	{
		if (std::strcmp(PresetName, "Clear") == 0)
		{
			Node.BackgroundColor = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
			Node.BorderColor = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
			Node.BorderWidth = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
			Node.BorderRadius = 0.0f;
			Node.Opacity = 1.0f;
		}
		else if (std::strcmp(PresetName, "Panel") == 0)
		{
			Node.BackgroundColor = FVector4(0.125f, 0.141f, 0.173f, 0.88f);
			Node.BorderColor = FVector4(0.35f, 0.42f, 0.52f, 0.8f);
			Node.BorderWidth = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			Node.BorderRadius = 4.0f;
			Node.Opacity = 1.0f;
		}
		else if (std::strcmp(PresetName, "Surface") == 0)
		{
			Node.BackgroundColor = FVector4(0.075f, 0.083f, 0.102f, 0.96f);
			Node.BorderColor = FVector4(0.22f, 0.27f, 0.34f, 0.86f);
			Node.BorderWidth = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			Node.BorderRadius = 3.0f;
			Node.Opacity = 1.0f;
		}
		else if (std::strcmp(PresetName, "Primary") == 0)
		{
			Node.BackgroundColor = FVector4(0.235f, 0.329f, 0.502f, 0.92f);
			Node.BorderColor = FVector4(0.48f, 0.62f, 0.86f, 0.95f);
			Node.BorderWidth = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			Node.BorderRadius = 5.0f;
			Node.Opacity = 1.0f;
			if (Node.Type == ERuntimeUIWidgetType::Text || Node.Type == ERuntimeUIWidgetType::Button)
			{
				Node.TextColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
		else if (std::strcmp(PresetName, "Secondary") == 0)
		{
			Node.BackgroundColor = FVector4(0.18f, 0.21f, 0.25f, 0.9f);
			Node.BorderColor = FVector4(0.38f, 0.45f, 0.54f, 0.86f);
			Node.BorderWidth = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			Node.BorderRadius = 5.0f;
			Node.Opacity = 1.0f;
		}
		else if (std::strcmp(PresetName, "Danger") == 0)
		{
			Node.BackgroundColor = FVector4(0.56f, 0.12f, 0.13f, 0.92f);
			Node.BorderColor = FVector4(0.95f, 0.34f, 0.30f, 0.94f);
			Node.BorderWidth = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			Node.BorderRadius = 5.0f;
			Node.Opacity = 1.0f;
			if (Node.Type == ERuntimeUIWidgetType::Text || Node.Type == ERuntimeUIWidgetType::Button)
			{
				Node.TextColor = FVector4(1.0f, 0.92f, 0.90f, 1.0f);
			}
		}
		else if (std::strcmp(PresetName, "Success") == 0)
		{
			Node.BackgroundColor = FVector4(0.10f, 0.38f, 0.25f, 0.92f);
			Node.BorderColor = FVector4(0.32f, 0.78f, 0.52f, 0.9f);
			Node.BorderWidth = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			Node.BorderRadius = 5.0f;
			Node.Opacity = 1.0f;
			if (Node.Type == ERuntimeUIWidgetType::Text || Node.Type == ERuntimeUIWidgetType::Button)
			{
				Node.TextColor = FVector4(0.90f, 1.0f, 0.94f, 1.0f);
			}
		}
		else if (std::strcmp(PresetName, "Ghost") == 0)
		{
			Node.BackgroundColor = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
			Node.BorderColor = FVector4(0.55f, 0.64f, 0.76f, 0.85f);
			Node.BorderWidth = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			Node.BorderRadius = 5.0f;
			Node.Opacity = 1.0f;
			if (Node.Type == ERuntimeUIWidgetType::Text || Node.Type == ERuntimeUIWidgetType::Button)
			{
				Node.TextColor = FVector4(0.82f, 0.88f, 0.96f, 1.0f);
			}
		}
		else if (std::strcmp(PresetName, "HUD") == 0)
		{
			Node.BackgroundColor = FVector4(0.02f, 0.04f, 0.06f, 0.72f);
			Node.BorderColor = FVector4(0.20f, 0.75f, 0.95f, 0.84f);
			Node.BorderWidth = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			Node.BorderRadius = 2.0f;
			Node.Opacity = 1.0f;
			if (Node.Type == ERuntimeUIWidgetType::Text || Node.Type == ERuntimeUIWidgetType::Button)
			{
				Node.TextColor = FVector4(0.72f, 0.95f, 1.0f, 1.0f);
				Node.FontFamily = "Consolas";
				Node.FontWeight = 600;
				Node.LetterSpacing = 0.4f;
			}
		}

		if (Node.Type == ERuntimeUIWidgetType::Button)
		{
			DeriveRuntimeUIButtonStateStyles(Node);
		}
	}

	void DrawRuntimeUIButtonStateStyleEditor(const char* Label, FRuntimeUIButtonStateStyle& Style)
	{
		if (!ImGui::CollapsingHeader(Label, ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		FString BackgroundLabel = FString("Background##") + Label;
		FString TextLabel = FString("Text##") + Label;
		FString BorderLabel = FString("Border##") + Label;
		ImGui::ColorEdit4(BackgroundLabel.c_str(), &Style.BackgroundColor.X);
		ImGui::ColorEdit4(TextLabel.c_str(), &Style.TextColor.X);
		ImGui::ColorEdit4(BorderLabel.c_str(), &Style.BorderColor.X);
	}

	bool IsRuntimeUIActionNameValid(const FString& ActionName)
	{
		if (ActionName.empty())
		{
			return true;
		}

		for (const char Ch : ActionName)
		{
			const bool bValidChar =
				std::isalnum(static_cast<unsigned char>(Ch)) ||
				Ch == '_' ||
				Ch == '-' ||
				Ch == '.' ||
				Ch == ':';
			if (!bValidChar)
			{
				return false;
			}
		}
		return true;
	}

	FString MakeActionNameFromLabel(FString Label)
	{
		FString Result;
		Result.reserve(Label.size());
		bool bCapitalizeNext = true;
		for (const char Ch : Label)
		{
			if (std::isalnum(static_cast<unsigned char>(Ch)))
			{
				Result.push_back(bCapitalizeNext ? static_cast<char>(std::toupper(static_cast<unsigned char>(Ch))) : Ch);
				bCapitalizeNext = false;
			}
			else
			{
				bCapitalizeNext = true;
			}
		}
		return Result.empty() ? "UIAction" : Result;
	}

	bool IsLayoutAssetPath(const FString& Path)
	{
		std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
		const FString Extension = ToLower(FPaths::ToUtf8(FsPath.extension().wstring()));
		return Extension == ".uasset";
	}

	bool IsRuntimeUIImagePath(const FString& Path)
	{
		std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
		const FString Extension = ToLower(FPaths::ToUtf8(FsPath.extension().wstring()));
		return Extension == ".png" || Extension == ".jpg" || Extension == ".jpeg" || Extension == ".dds" || Extension == ".bmp";
	}

	bool IsPointInRect(const ImVec2& Point, const ImVec2& Min, const ImVec2& Max)
	{
		return Point.x >= Min.x && Point.x <= Max.x && Point.y >= Min.y && Point.y <= Max.y;
	}

	void ApplyRuntimeUIAnchorPreset(
		FRuntimeUIWidgetNode& Node,
		const FVector2& AnchorMin,
		const FVector2& AnchorMax,
		const FVector2& Pivot)
	{
		Node.AnchorMin = FVector2(
			std::clamp(AnchorMin.X, 0.0f, 1.0f),
			std::clamp(AnchorMin.Y, 0.0f, 1.0f));
		Node.AnchorMax = FVector2(
			std::clamp(AnchorMax.X, Node.AnchorMin.X, 1.0f),
			std::clamp(AnchorMax.Y, Node.AnchorMin.Y, 1.0f));
		Node.Pivot = FVector2(
			std::clamp(Pivot.X, 0.0f, 1.0f),
			std::clamp(Pivot.Y, 0.0f, 1.0f));
	}

	bool DrawRuntimeUIAnchorPresetButton(
		const char* Label,
		FRuntimeUIWidgetNode& Node,
		const FVector2& AnchorMin,
		const FVector2& AnchorMax,
		const FVector2& Pivot,
		const ImVec2& Size)
	{
		if (!ImGui::Button(Label, Size))
		{
			return false;
		}

		ApplyRuntimeUIAnchorPreset(Node, AnchorMin, AnchorMax, Pivot);
		return true;
	}

	bool HasParentDirectoryReference(const std::filesystem::path& Path)
	{
		for (const std::filesystem::path& Part : Path)
		{
			if (Part == std::filesystem::path(L".."))
			{
				return true;
			}
		}
		return false;
	}

	bool NormalizeRmlPath(const FString& InPath, FString& OutPath)
	{
		OutPath.clear();
		if (InPath.empty())
		{
			return false;
		}

		std::filesystem::path Path(FPaths::ToWide(InPath));
		const std::filesystem::path Root = std::filesystem::path(FPaths::RootDir()).lexically_normal();
		const std::filesystem::path UiRoot = (Root / L"Asset" / L"UI").lexically_normal();
		if (!Path.is_absolute())
		{
			Path = Root / Path;
		}
		Path = Path.lexically_normal();

		const FString Extension = ToLower(FPaths::ToUtf8(Path.extension().wstring()));
		if (Extension != ".rml")
		{
			return false;
		}

		const std::filesystem::path RelativeToUi = Path.lexically_relative(UiRoot);
		if (RelativeToUi.empty() || HasParentDirectoryReference(RelativeToUi))
		{
			return false;
		}

		const std::filesystem::path RelativeToRoot = Path.lexically_relative(Root);
		if (RelativeToRoot.empty() || HasParentDirectoryReference(RelativeToRoot))
		{
			return false;
		}

		OutPath = FPaths::Normalize(FPaths::ToUtf8(RelativeToRoot.generic_wstring()));
		return true;
	}

	bool NormalizeRuntimeUIImagePath(const FString& InPath, FString& OutPath)
	{
		OutPath.clear();
		if (InPath.empty() || !IsRuntimeUIImagePath(InPath))
		{
			return false;
		}

		std::filesystem::path Path(FPaths::ToWide(InPath));
		const std::filesystem::path Root = std::filesystem::path(FPaths::RootDir()).lexically_normal();
		if (!Path.is_absolute())
		{
			Path = Root / Path;
		}
		Path = Path.lexically_normal();

		const std::filesystem::path RelativeToRoot = Path.lexically_relative(Root);
		if (RelativeToRoot.empty() || HasParentDirectoryReference(RelativeToRoot))
		{
			return false;
		}

		OutPath = FPaths::Normalize(FPaths::ToUtf8(RelativeToRoot.generic_wstring()));
		return true;
	}

	bool AcceptRuntimeUIImageDragDrop(FString& OutPath)
	{
		OutPath.clear();
		if (!ImGui::BeginDragDropTarget())
		{
			return false;
		}

		const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PNGElement");
		if (!Payload)
		{
			Payload = ImGui::AcceptDragDropPayload("TextureContentItem");
		}
		if (!Payload)
		{
			Payload = ImGui::AcceptDragDropPayload("ContentBrowserPath");
		}

		bool bAccepted = false;
		if (Payload && Payload->Data && Payload->DataSize > 0)
		{
			const FString Path(static_cast<const char*>(Payload->Data));
			bAccepted = NormalizeRuntimeUIImagePath(Path, OutPath);
		}

		ImGui::EndDragDropTarget();
		return bAccepted;
	}

	void HashBytes(uint64& Hash, const void* Data, size_t Size)
	{
		const unsigned char* Bytes = static_cast<const unsigned char*>(Data);
		for (size_t Index = 0; Index < Size; ++Index)
		{
			Hash ^= static_cast<uint64>(Bytes[Index]);
			Hash *= 1099511628211ull;
		}
	}

	void HashString(uint64& Hash, const FString& Value)
	{
		HashBytes(Hash, Value.data(), Value.size());
		const char Terminator = '\0';
		HashBytes(Hash, &Terminator, sizeof(Terminator));
	}

	template <typename T>
	void HashValue(uint64& Hash, const T& Value)
	{
		HashBytes(Hash, &Value, sizeof(T));
	}
}

void FEditorRuntimeUIPreviewWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	SelectSingleWidget(0);
	SavedLayoutFingerprint = ComputeLayoutFingerprint();
	ExportedLayoutFingerprint = 0;
	ResetUndoHistory();
	UpdateLayoutDirtyState();
}

void FEditorRuntimeUIPreviewWidget::SetRmlRenderQueue(std::function<void(const FRuntimeUIRenderContext&)> InQueueCallback)
{
	QueueRmlRenderContext = std::move(InQueueCallback);
}

void FEditorRuntimeUIPreviewWidget::Render(float DeltaTime)
{
	ImGui::SetNextWindowSize(ImVec2(1120.0f, 760.0f), ImGuiCond_Once);
	if (!ImGui::Begin("Runtime UI Preview"))
	{
		ImGui::End();
		return;
	}

	DrawContent(DeltaTime);
	ImGui::End();
}

void FEditorRuntimeUIPreviewWidget::RenderEmbedded(float DeltaTime)
{
	DrawContent(DeltaTime);
}

bool FEditorRuntimeUIPreviewWidget::OpenPreviewDocument(const FString& Path)
{
	if (!Path.empty() && !SetPreviewDocumentPath(Path))
	{
		return false;
	}

	RefreshPreviewDocument();
	return bPreviewDocumentLoaded;
}

bool FEditorRuntimeUIPreviewWidget::OpenLayoutAsset(const FString& Path)
{
	if (Path.empty())
	{
		return false;
	}

	const FString NormalizedPath = FPaths::Normalize(Path);
	FString AssetPath = NormalizedPath;
	bool bLoaded = LayoutAsset.LoadFromFile(NormalizedPath);
	if (!bLoaded)
	{
		return false;
	}

	strncpy_s(LayoutAssetPathBuffer, AssetPath.c_str(), _TRUNCATE);
	SyncGeneratedPathsFromLayoutPath(true);
	SelectSingleWidget(0);
	bDesignMode = true;
	SavedLayoutFingerprint = ComputeLayoutFingerprint();
	ExportedLayoutFingerprint = 0;
	ResetUndoHistory();
	UpdateLayoutDirtyState();
	return ExportLayoutToPreview();
}

FString FEditorRuntimeUIPreviewWidget::GetPreviewDocumentPath() const
{
	return PreviewDocumentPathBuffer;
}

FString FEditorRuntimeUIPreviewWidget::GetLayoutAssetPath() const
{
	return LayoutAssetPathBuffer;
}

void FEditorRuntimeUIPreviewWidget::DrawContent(float DeltaTime)
{
	HandleUndoRedoShortcuts();
	DrawToolbar();
	ImGui::Separator();

	if (bDesignMode)
	{
		if (ImGui::BeginTable(
			"##RmlRuntimeUIDesignerLayout",
			3,
			ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthFixed, 230.0f);
			ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 330.0f);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			DrawDesignerHierarchy();
			ImGui::TableSetColumnIndex(1);
			DrawPreviewSurface(DeltaTime);
			ImGui::TableSetColumnIndex(2);
			DrawDesignerDetails();
			DrawActionEvents();
			ImGui::EndTable();
		}
		CommitPendingUndoSnapshot(false);
		return;
	}

	if (ImGui::BeginTable(
		"##RmlRuntimeUIPreviewLayout",
		2,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthFixed, 320.0f);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		DrawPreviewSurface(DeltaTime);
		ImGui::TableSetColumnIndex(1);
		DrawDocumentInfo();
		DrawActionEvents();
		DrawAuthoringGuidance();
		ImGui::EndTable();
	}
}

void FEditorRuntimeUIPreviewWidget::DrawToolbar()
{
	UpdateLayoutDirtyState();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
	ImGui::BeginChild("##RmlRuntimeUIPreviewToolbar", ImVec2(0.0f, bDesignMode ? 152.0f : 92.0f), false, ImGuiWindowFlags_NoScrollbar);
	const float ButtonWidth = 72.0f;
	const float PathLabelWidth = ImGui::CalcTextSize("RML").x;
	const float AvailableWidth = ImGui::GetContentRegionAvail().x;
	const float PathWidth = std::max(
		160.0f,
		AvailableWidth - PathLabelWidth - (ButtonWidth * 2.0f) - (ImGui::GetStyle().ItemSpacing.x * 4.0f));

	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("RML");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(PathWidth);
	if (ImGui::InputText("##RmlRuntimeUIPreviewPath", PreviewDocumentPathBuffer, IM_ARRAYSIZE(PreviewDocumentPathBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		RefreshPreviewDocument();
	}
	AcceptRmlDragDropTarget();

	ImGui::SameLine();
	if (ImGui::Button("Load", ImVec2(ButtonWidth, 0.0f)))
	{
		FString PickedPath;
		if (OpenRmlFileDialog(PickedPath) && SetPreviewDocumentPath(PickedPath))
		{
			RefreshPreviewDocument();
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Reload", ImVec2(ButtonWidth, 0.0f)))
	{
		RefreshPreviewDocument();
	}

	const int32 PresetCount = static_cast<int32>(sizeof(PreviewResolutionPresets) / sizeof(PreviewResolutionPresets[0]));
	const char* CurrentPreset = PreviewResolutionPresets[std::clamp(ResolutionPresetIndex, 0, PresetCount - 1)].Label;
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);
	ImGui::Checkbox("Input", &bEnableInteraction);
	ImGui::SameLine();
	ImGui::Checkbox("Design", &bDesignMode);
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Resolution");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(140.0f);
	if (ImGui::BeginCombo("##RmlRuntimeUIPreviewResolution", CurrentPreset))
	{
		for (int32 i = 0; i < PresetCount; ++i)
		{
			const bool bSelected = ResolutionPresetIndex == i;
			if (ImGui::Selectable(PreviewResolutionPresets[i].Label, bSelected))
			{
				ResolutionPresetIndex = i;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	if (ResolutionPresetIndex == PresetCount - 1)
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(78.0f);
		ImGui::DragInt("W", &CustomWidth, 8.0f, 320, 7680);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(78.0f);
		ImGui::DragInt("H", &CustomHeight, 8.0f, 180, 4320);
	}

	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Zoom");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	ImGui::SliderFloat("##RmlRuntimeUIPreviewZoom", &PreviewZoom, 0.25f, 1.5f, "%.2fx");
	ImGui::SameLine();
	ImGui::Checkbox("Guide", &bShowGuidance);

	if (bDesignMode)
	{
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Layout");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(std::max(220.0f, ImGui::GetContentRegionAvail().x - 540.0f));
		ImGui::InputText("##RuntimeUILayoutAssetPath", LayoutAssetPathBuffer, IM_ARRAYSIZE(LayoutAssetPathBuffer));
		AcceptLayoutDragDropTarget();
		ImGui::SameLine();
		if (ImGui::Button("New##RuntimeUINewLayout", ImVec2(58.0f, 0.0f)))
		{
			LayoutAsset.ResetToDefault();
			SelectSingleWidget(0);
			bPreviewDocumentLoaded = false;
			UpdateLayoutDirtyState();
		}
		ImGui::SameLine();
		if (ImGui::Button("Save##RuntimeUISaveLayout", ImVec2(58.0f, 0.0f)))
		{
			SaveLayoutAsset();
		}
		ImGui::SameLine();
		if (ImGui::Button("Export##RuntimeUIExportLayout", ImVec2(68.0f, 0.0f)))
		{
			ExportLayoutToPreview();
		}
		ImGui::SameLine();
		if (ImGui::Button("All##RuntimeUISaveExportLayout", ImVec2(48.0f, 0.0f)))
		{
			SaveAndExportLayout();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!CanUndoLayoutEdit());
		if (ImGui::Button("Undo##RuntimeUIUndo", ImVec2(58.0f, 0.0f)))
		{
			UndoLayoutEdit();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!CanRedoLayoutEdit());
		if (ImGui::Button("Redo##RuntimeUIRedo", ImVec2(58.0f, 0.0f)))
		{
			RedoLayoutEdit();
		}
		ImGui::EndDisabled();

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
		ImGui::TextDisabled(
			"Asset: %s | Preview: %s",
			bLayoutDirty ? "Dirty" : "Saved",
			bPreviewExportDirty ? "Stale" : "Fresh");
		ImGui::SameLine();
		if (ImGui::Button("Sync Paths##RuntimeUISyncGeneratedPaths", ImVec2(88.0f, 0.0f)))
		{
			SyncGeneratedPathsFromLayoutPath(true);
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();
}

void FEditorRuntimeUIPreviewWidget::DrawPreviewSurface(float DeltaTime)
{
	if (!EditorEngine)
	{
		ImGui::TextDisabled("EditorEngine is not ready.");
		return;
	}

	if (!bPreviewDocumentLoaded)
	{
		LoadPreviewDocument();
	}

	int32 TargetWidth = 1920;
	int32 TargetHeight = 1080;
	GetPreviewResolution(ResolutionPresetIndex, CustomWidth, CustomHeight, TargetWidth, TargetHeight);

	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const float FitScale = std::min(
		Available.x > 0.0f ? Available.x / static_cast<float>(TargetWidth) : 1.0f,
		Available.y > 0.0f ? Available.y / static_cast<float>(TargetHeight) : 1.0f);
	const float Scale = std::max(0.05f, FitScale * PreviewZoom);
	const ImVec2 PreviewSize(
		std::max(1.0f, static_cast<float>(TargetWidth) * Scale),
		std::max(1.0f, static_cast<float>(TargetHeight) * Scale));

	ImGui::BeginChild("##RmlRuntimeUIPreviewSurface", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoScrollWithMouse);

	const ImVec2 ChildAvail = ImGui::GetContentRegionAvail();
	const ImVec2 Start(
		ImGui::GetCursorScreenPos().x + std::max(0.0f, (ChildAvail.x - PreviewSize.x) * 0.5f),
		ImGui::GetCursorScreenPos().y + std::max(0.0f, (ChildAvail.y - PreviewSize.y) * 0.5f));
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(Start, ImVec2(Start.x + PreviewSize.x, Start.y + PreviewSize.y),
		ImGui::GetColorU32(ImVec4(0.025f, 0.027f, 0.032f, 1.0f)), 6.0f);
	DrawList->AddRect(Start, ImVec2(Start.x + PreviewSize.x, Start.y + PreviewSize.y),
		ImGui::GetColorU32(ImVec4(0.25f, 0.29f, 0.35f, 1.0f)), 6.0f);

	ImGui::SetCursorScreenPos(Start);
	ImGui::InvisibleButton("##RmlRuntimeUIPreviewInputSurface", PreviewSize);
	if (bDesignMode && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		const ImVec2 LocalMouse(
			ImGui::GetIO().MousePos.x - Start.x,
			ImGui::GetIO().MousePos.y - Start.y);
		const int32 HitIndex = HitTestWidgetAtCanvasPosition(LocalMouse, Scale);
		if (!IsWidgetSelected(HitIndex))
		{
			SelectSingleWidget(HitIndex);
		}
		ImGui::OpenPopup("RuntimeUICanvasContextMenu");
	}
	if (bDesignMode && ImGui::BeginPopup("RuntimeUICanvasContextMenu"))
	{
		DrawWidgetContextMenu(SelectedWidgetIndex);
		ImGui::EndPopup();
	}
	if (bDesignMode)
	{
		FString DroppedImagePath;
		if (AcceptRuntimeUIImageDragDrop(DroppedImagePath))
		{
			const ImVec2 LocalMouse(
				ImGui::GetIO().MousePos.x - Start.x,
				ImGui::GetIO().MousePos.y - Start.y);
			int32 DropParentIndex = HitTestWidgetAtCanvasPosition(LocalMouse, Scale);
			if (const FRuntimeUIWidgetNode* DropNode = LayoutAsset.GetWidget(DropParentIndex))
			{
				if (DropNode->bLocked || (DropNode->Type != ERuntimeUIWidgetType::Canvas && DropNode->Type != ERuntimeUIWidgetType::Panel))
				{
					DropParentIndex = LayoutAsset.GetWidget(DropNode->ParentIndex) ? DropNode->ParentIndex : 0;
				}
			}
			else
			{
				DropParentIndex = 0;
			}

			const FVector2 DropParentAbsolute = GetWidgetAbsolutePosition(DropParentIndex);
			const int32 NewImageIndex = LayoutAsset.AddWidget(ERuntimeUIWidgetType::Image, DropParentIndex);
			if (FRuntimeUIWidgetNode* ImageNode = LayoutAsset.GetMutableWidget(NewImageIndex))
			{
				ImageNode->ImagePath = DroppedImagePath;
				ImageNode->Position = SnapPosition(FVector2(LocalMouse.x / Scale, LocalMouse.y / Scale) - DropParentAbsolute);
				ImageNode->Size = FVector2(256.0f, 256.0f);
				SelectSingleWidget(NewImageIndex);
			}
		}
	}
	else
	{
		AcceptRmlDragDropTarget();
	}
	const bool bPreviewHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

	FRuntimeUIRenderContext Context;
	Context.RenderMode = ERuntimeUIRenderMode::PIE;
	Context.ViewportMin = FRuntimeUIVector2(Start.x, Start.y);
	Context.ViewportSize = FRuntimeUIVector2(PreviewSize.x, PreviewSize.y);
	Context.LayoutSize = FRuntimeUIVector2(static_cast<float>(TargetWidth), static_cast<float>(TargetHeight));
	Context.DeltaTime = DeltaTime;
	Context.bPreviewDocumentOnly = true;

	if (QueueRmlRenderContext)
	{
		QueueRmlRenderContext(Context);
	}

	if (bDesignMode)
	{
		DrawDesignerGrid(DrawList, Start, PreviewSize, Scale);
	}

	if (bDesignMode && bPreviewHovered)
	{
		NormalizeWidgetSelection();
		const ImVec2 LocalMouse(
			ImGui::GetIO().MousePos.x - Start.x,
			ImGui::GetIO().MousePos.y - Start.y);
		const bool bAllowShortcuts = !ImGui::GetIO().WantTextInput && SelectedWidgetIndex != 0;
		const FRuntimeUIWidgetNode* SelectedNode = LayoutAsset.GetWidget(SelectedWidgetIndex);
		const bool bSelectedLocked = SelectedNode && SelectedNode->bLocked;
		if (bAllowShortcuts && !bSelectedLocked && ImGui::IsKeyPressed(ImGuiKey_Delete))
		{
			DeleteSelectedWidgets();
		}
		if (bAllowShortcuts && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
		{
			DuplicateSelectedWidgets();
		}
		if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
		{
			SelectAllWidgets();
		}
		if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			ClearWidgetSelection();
		}
		if (bAllowShortcuts && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
		{
			if (const FRuntimeUIWidgetNode* Node = LayoutAsset.GetWidget(SelectedWidgetIndex))
			{
				CopiedWidgetId = Node->Id;
			}
		}
		if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
		{
			int32 SourceIndex = -1;
			const TArray<FRuntimeUIWidgetNode>& Widgets = LayoutAsset.GetWidgets();
			for (int32 Index = 1; Index < static_cast<int32>(Widgets.size()); ++Index)
			{
				if (Widgets[Index].Id == CopiedWidgetId)
				{
					SourceIndex = Index;
					break;
				}
			}
			if (SourceIndex >= 0)
			{
				const int32 DuplicatedIndex = LayoutAsset.DuplicateWidget(SourceIndex);
				if (DuplicatedIndex >= 0)
				{
					SelectSingleWidget(DuplicatedIndex);
					if (const FRuntimeUIWidgetNode* Node = LayoutAsset.GetWidget(DuplicatedIndex))
					{
						CopiedWidgetId = Node->Id;
					}
				}
			}
			else
			{
				CopiedWidgetId.clear();
			}
		}
		if (bAllowShortcuts && !bSelectedLocked)
		{
			FRuntimeUIWidgetNode* Node = LayoutAsset.GetMutableWidget(SelectedWidgetIndex);
			const FRuntimeUIWidgetNode* ParentNode = Node ? LayoutAsset.GetWidget(Node->ParentIndex) : nullptr;
			const bool bManagedByParent = ParentNode && IsRuntimeUILayoutContainer(*ParentNode);
			const float Nudge = ImGui::GetIO().KeyShift ? 10.0f : 1.0f;
			FVector2 Delta(0.0f, 0.0f);
			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
			{
				Delta.X -= Nudge;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
			{
				Delta.X += Nudge;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			{
				Delta.Y -= Nudge;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			{
				Delta.Y += Nudge;
			}
			if (Node && !bManagedByParent && (Delta.X != 0.0f || Delta.Y != 0.0f))
			{
				if (HasMultiSelection())
				{
					MoveSelectedWidgets(Delta);
				}
				else
				{
					Node->Position = SnapPosition(Node->Position + Delta);
				}
			}
		}

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			const int32 HitIndex = HitTestWidgetAtCanvasPosition(LocalMouse, Scale);
			const bool bHitSelected = IsWidgetSelected(HitIndex);
			const bool bToggleSelection = ImGui::GetIO().KeyCtrl;
			const bool bAddSelection = ImGui::GetIO().KeyShift;
			const bool bStartResize = !HasMultiSelection() && IsMouseOverResizeHandle(SelectedWidgetIndex, Start, Scale);
			if (!bStartResize)
			{
				SelectWidgetAtCanvasPosition(LocalMouse, Scale, bToggleSelection, bAddSelection);
			}

			if (FRuntimeUIWidgetNode* Node = LayoutAsset.GetMutableWidget(SelectedWidgetIndex))
			{
				if (SelectedWidgetIndex != 0 && !Node->bLocked && (bHitSelected || !bToggleSelection))
				{
					const FRuntimeUIWidgetNode* ParentNode = LayoutAsset.GetWidget(Node->ParentIndex);
					const bool bManagedByParent = ParentNode && IsRuntimeUILayoutContainer(*ParentNode);
					if (!bManagedByParent || bStartResize)
					{
						bDraggingWidget = true;
						DesignerDragMode = bStartResize ? EDesignerDragMode::ResizeBottomRight : EDesignerDragMode::Move;
						DragStartMouse = ImGui::GetIO().MousePos;
						DragStartPosition = Node->Position;
						DragStartSize = Node->Size;
						DragStartWidgets.clear();
						for (const int32 Index : GetEditableSelectedWidgets())
						{
							if (const FRuntimeUIWidgetNode* DragNode = LayoutAsset.GetWidget(Index))
							{
								DragStartWidgets.push_back({ Index, DragNode->Position, DragNode->Size });
							}
						}
					}
				}
			}
		}
		if (bDraggingWidget && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			if (FRuntimeUIWidgetNode* Node = LayoutAsset.GetMutableWidget(SelectedWidgetIndex))
			{
				const ImVec2 Delta(
					(ImGui::GetIO().MousePos.x - DragStartMouse.x) / Scale,
					(ImGui::GetIO().MousePos.y - DragStartMouse.y) / Scale);
				if (DesignerDragMode == EDesignerDragMode::ResizeBottomRight)
				{
					Node->Size = SnapSize(FVector2(DragStartSize.X + Delta.x, DragStartSize.Y + Delta.y));
				}
				else
				{
					const FVector2 MoveDelta = HasMultiSelection()
						? SnapSelectionMoveDelta(FVector2(Delta.x, Delta.y))
						: FVector2(Delta.x, Delta.y);
					if (HasMultiSelection())
					{
						for (const FDragStartWidget& DragWidget : DragStartWidgets)
						{
							if (FRuntimeUIWidgetNode* DragNode = LayoutAsset.GetMutableWidget(DragWidget.WidgetIndex))
							{
								const FRuntimeUIWidgetNode* ParentNode = LayoutAsset.GetWidget(DragNode->ParentIndex);
								if (ParentNode && IsRuntimeUILayoutContainer(*ParentNode))
								{
									continue;
								}
								DragNode->Position = SnapPosition(DragWidget.Position + MoveDelta);
							}
						}
					}
					else
					{
						Node->Position = SnapWidgetMovePosition(SelectedWidgetIndex, FVector2(DragStartPosition.X + Delta.x, DragStartPosition.Y + Delta.y));
					}
				}
			}
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			bDraggingWidget = false;
			DesignerDragMode = EDesignerDragMode::None;
			DragStartWidgets.clear();
			ActiveGuideLines.clear();
		}
	}
	else if (bEnableInteraction && bPreviewHovered)
	{
		FViewportRect PreviewRect(
			static_cast<int32>(Start.x),
			static_cast<int32>(Start.y),
			static_cast<int32>(PreviewSize.x),
			static_cast<int32>(PreviewSize.y));
		if (EditorEngine->GetRmlUiSystem().PumpViewportInput(InputSystem::Get(), EditorEngine->GetWindow(), EditorEngine->BuildRuntimeInputPermissions(InputSystem::Get().GetGuiInputState()).bAllowRuntimeUIInput, PreviewRect, TargetWidth, TargetHeight, true))
		{
			InputSystem::Get().SetGuiMouseCapture(true);
			InputSystem::Get().SetGuiViewportMouseBlock(true);
		}
	}

	const TArray<FString> NewEvents = EditorEngine->GetRmlUiSystem().PollPreviewActionEvents();
	for (const FString& Event : NewEvents)
	{
		if (!Event.empty())
		{
			PreviewActionEvents.push_back(Event);
		}
	}
	while (PreviewActionEvents.size() > 12)
	{
		PreviewActionEvents.erase(PreviewActionEvents.begin());
	}

	char OverlayText[192];
	std::snprintf(OverlayText, sizeof(OverlayText), "%d x %d | %.2fx | %s",
		TargetWidth, TargetHeight, Scale, bPreviewDocumentLoaded ? "RML loaded" : "RML missing");
	DrawList->AddText(ImVec2(Start.x + 10.0f, Start.y + 8.0f),
		ImGui::GetColorU32(ImVec4(0.72f, 0.76f, 0.82f, 0.9f)), OverlayText);

	if (bDesignMode)
	{
		DrawDesignerOverlay(DrawList, Start, Scale);
	}

	ImGui::EndChild();
}

void FEditorRuntimeUIPreviewWidget::DrawDesignerHierarchy()
{
	ImGui::TextUnformatted("Hierarchy");
	ImGui::Separator();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##RuntimeUIHierarchySearch", "Search id or name", HierarchySearchBuffer, IM_ARRAYSIZE(HierarchySearchBuffer));

	int32 PendingDragSource = -1;
	int32 PendingDragTarget = -1;
	int32 PendingContextWidget = -1;
	bool bPendingReorder = false;
	bool bPendingAfter = false;
	const FString SearchText = ToLower(FString(HierarchySearchBuffer));

	auto NodeMatchesSearch = [this, &SearchText](auto&& Self, int32 WidgetIndex) -> bool
	{
		if (SearchText.empty())
		{
			return true;
		}

		const FRuntimeUIWidgetNode* Node = LayoutAsset.GetWidget(WidgetIndex);
		if (!Node)
		{
			return false;
		}

		const FString Id = ToLower(Node->Id);
		const FString Name = ToLower(Node->DisplayName);
		if (Id.find(SearchText) != FString::npos || Name.find(SearchText) != FString::npos)
		{
			return true;
		}

		for (const int32 ChildIndex : Node->Children)
		{
			if (Self(Self, ChildIndex))
			{
				return true;
			}
		}
		return false;
	};

	auto DrawNode = [this, &NodeMatchesSearch, &PendingDragSource, &PendingDragTarget, &PendingContextWidget, &bPendingReorder, &bPendingAfter](auto&& Self, int32 WidgetIndex) -> void
	{
		const FRuntimeUIWidgetNode* Node = LayoutAsset.GetWidget(WidgetIndex);
		if (!Node)
		{
			return;
		}

		const bool bSearching = HierarchySearchBuffer[0] != '\0';
		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
		if (bSearching)
		{
			Flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		if (Node->Children.empty())
		{
			Flags |= ImGuiTreeNodeFlags_Leaf;
		}
		if (IsWidgetSelected(WidgetIndex))
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}

		ImGui::PushID(WidgetIndex);
		FString Label = Node->DisplayName.empty() ? Node->Id : Node->DisplayName;
		if (!Node->bVisible)
		{
			Label = "[H] " + Label;
		}
		if (Node->bLocked)
		{
			Label = "[L] " + Label;
		}
		if (!Node->bVisible)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}
		const bool bOpen = ImGui::TreeNodeEx("##RuntimeUIWidgetNode", Flags, "%s", Label.c_str());
		if (!Node->bVisible)
		{
			ImGui::PopStyleColor();
		}
		if (ImGui::IsItemClicked())
		{
			if (ImGui::GetIO().KeyCtrl)
			{
				ToggleWidgetSelection(WidgetIndex);
			}
			else if (ImGui::GetIO().KeyShift)
			{
				AddWidgetSelection(WidgetIndex);
			}
			else
			{
				SelectSingleWidget(WidgetIndex);
			}
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			if (!IsWidgetSelected(WidgetIndex))
			{
				SelectSingleWidget(WidgetIndex);
			}
			PendingContextWidget = WidgetIndex;
		}
		const ImVec2 ItemMin = ImGui::GetItemRectMin();
		const ImVec2 ItemMax = ImGui::GetItemRectMax();
		if (WidgetIndex != 0 && !Node->bLocked && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			ImGui::SetDragDropPayload("RuntimeUIWidgetNode", &WidgetIndex, sizeof(int32));
			ImGui::TextUnformatted(Label.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("RuntimeUIWidgetNode");
			if (Payload && Payload->Data && Payload->DataSize == sizeof(int32))
			{
				const int32 SourceIndex = *static_cast<const int32*>(Payload->Data);
				const FRuntimeUIWidgetNode* SourceNode = LayoutAsset.GetWidget(SourceIndex);
				if (SourceIndex != WidgetIndex && SourceNode && !SourceNode->bLocked && !Node->bLocked)
				{
					PendingDragSource = SourceIndex;
					PendingDragTarget = WidgetIndex;
					bPendingReorder = SourceNode->ParentIndex == Node->ParentIndex && WidgetIndex != 0;
					bPendingAfter = ImGui::GetMousePos().y > (ItemMin.y + ItemMax.y) * 0.5f;
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (bOpen)
		{
			for (const int32 ChildIndex : Node->Children)
			{
				if (HierarchySearchBuffer[0] != '\0' && !NodeMatchesSearch(NodeMatchesSearch, ChildIndex))
				{
					continue;
				}
				Self(Self, ChildIndex);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	};

	auto DrawFilteredNode = [this, &DrawNode, &NodeMatchesSearch](auto&& Self, int32 WidgetIndex) -> void
	{
		if (NodeMatchesSearch(NodeMatchesSearch, WidgetIndex))
		{
			DrawNode(DrawNode, WidgetIndex);
		}
	};

	DrawFilteredNode(DrawFilteredNode, 0);
	if (PendingContextWidget >= 0)
	{
		ImGui::OpenPopup("RuntimeUIHierarchyContextMenu");
	}
	if (ImGui::BeginPopup("RuntimeUIHierarchyContextMenu"))
	{
		DrawWidgetContextMenu(SelectedWidgetIndex);
		ImGui::EndPopup();
	}
	if (PendingDragSource >= 0 && PendingDragTarget >= 0)
	{
		if (bPendingReorder)
		{
			SelectSingleWidget(LayoutAsset.MoveWidgetRelativeToSibling(PendingDragSource, PendingDragTarget, bPendingAfter));
		}
		else if (LayoutAsset.SetWidgetParent(PendingDragSource, PendingDragTarget))
		{
			SelectSingleWidget(PendingDragSource);
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Checkbox("Add as Child##RuntimeUIAddAsChild", &bAddWidgetsAsChild);

	const FRuntimeUIWidgetNode* SelectedNode = LayoutAsset.GetWidget(SelectedWidgetIndex);
	int32 AddParentIndex = 0;
	if (SelectedNode)
	{
		if (bAddWidgetsAsChild && SelectedWidgetIndex != 0)
		{
			AddParentIndex = SelectedWidgetIndex;
		}
		else if (SelectedNode->ParentIndex >= 0 && LayoutAsset.GetWidget(SelectedNode->ParentIndex))
		{
			AddParentIndex = SelectedNode->ParentIndex;
		}
	}

	const FRuntimeUIWidgetNode* AddParentNode = LayoutAsset.GetWidget(AddParentIndex);
	const FString AddParentLabel = AddParentNode
		? (AddParentNode->DisplayName.empty() ? AddParentNode->Id : AddParentNode->DisplayName)
		: "Root Canvas";
	ImGui::TextDisabled("Add target: %s", AddParentLabel.c_str());

	if (ImGui::Button("Panel##RuntimeUIAddPanel", ImVec2(68.0f, 0.0f)))
	{
		SelectSingleWidget(LayoutAsset.AddWidget(ERuntimeUIWidgetType::Panel, AddParentIndex));
	}
	ImGui::SameLine();
	if (ImGui::Button("Text##RuntimeUIAddText", ImVec2(58.0f, 0.0f)))
	{
		SelectSingleWidget(LayoutAsset.AddWidget(ERuntimeUIWidgetType::Text, AddParentIndex));
	}
	ImGui::SameLine();
	if (ImGui::Button("Button##RuntimeUIAddButton", ImVec2(68.0f, 0.0f)))
	{
		SelectSingleWidget(LayoutAsset.AddWidget(ERuntimeUIWidgetType::Button, AddParentIndex));
	}
	if (ImGui::Button("Image##RuntimeUIAddImage", ImVec2(68.0f, 0.0f)))
	{
		SelectSingleWidget(LayoutAsset.AddWidget(ERuntimeUIWidgetType::Image, AddParentIndex));
	}
	if (SelectedWidgetIndex != 0)
	{
		ImGui::SameLine();
		if (ImGui::Button("Duplicate##RuntimeUIDuplicateWidget", ImVec2(82.0f, 0.0f)))
		{
			DuplicateSelectedWidgets();
		}
		ImGui::SameLine();
		const FRuntimeUIWidgetNode* CurrentNode = LayoutAsset.GetWidget(SelectedWidgetIndex);
		const bool bCurrentLocked = CurrentNode && CurrentNode->bLocked;
		ImGui::BeginDisabled(bCurrentLocked);
		if (ImGui::Button("Delete##RuntimeUIDeleteWidget", ImVec2(68.0f, 0.0f)))
		{
			DeleteSelectedWidgets();
		}
		ImGui::EndDisabled();

		ImGui::BeginDisabled(bCurrentLocked);
		if (ImGui::Button("Up##RuntimeUIMoveWidgetUp", ImVec2(58.0f, 0.0f)))
		{
			SelectSingleWidget(LayoutAsset.MoveWidgetWithinParent(SelectedWidgetIndex, -1));
		}
		ImGui::SameLine();
		if (ImGui::Button("Down##RuntimeUIMoveWidgetDown", ImVec2(68.0f, 0.0f)))
		{
			SelectSingleWidget(LayoutAsset.MoveWidgetWithinParent(SelectedWidgetIndex, 1));
		}
		ImGui::SameLine();
		if (ImGui::Button("To Root##RuntimeUIParentToRoot", ImVec2(82.0f, 0.0f)))
		{
			LayoutAsset.SetWidgetParent(SelectedWidgetIndex, 0);
		}
		ImGui::EndDisabled();
	}
}

void FEditorRuntimeUIPreviewWidget::DrawDesignerDetails()
{
	ImGui::TextUnformatted("Details");
	ImGui::Separator();
	NormalizeWidgetSelection();

	if (HasMultiSelection())
	{
		FVector2 Min;
		FVector2 Max;
		const bool bHasBounds = GetSelectionBounds(Min, Max);
		ImGui::TextDisabled("Multi Selection");
		ImGui::Text("%d widgets", static_cast<int32>(GetEditableSelectedWidgets().size()));
		if (bHasBounds)
		{
			ImGui::TextDisabled("Bounds %.0f, %.0f  %.0f x %.0f", Min.X, Min.Y, Max.X - Min.X, Max.Y - Min.Y);
		}
		ImGui::Separator();
		ImGui::TextDisabled("Align to selection");
		if (ImGui::Button("Left##RuntimeUIMultiAlignLeft", ImVec2(72.0f, 0.0f)))
		{
			AlignSelectedWidgetsToSelection('x', 0.0f);
		}
		ImGui::SameLine();
		if (ImGui::Button("H Center##RuntimeUIMultiAlignHCenter", ImVec2(88.0f, 0.0f)))
		{
			AlignSelectedWidgetsToSelection('x', 0.5f);
		}
		ImGui::SameLine();
		if (ImGui::Button("Right##RuntimeUIMultiAlignRight", ImVec2(72.0f, 0.0f)))
		{
			AlignSelectedWidgetsToSelection('x', 1.0f);
		}
		if (ImGui::Button("Top##RuntimeUIMultiAlignTop", ImVec2(72.0f, 0.0f)))
		{
			AlignSelectedWidgetsToSelection('y', 0.0f);
		}
		ImGui::SameLine();
		if (ImGui::Button("V Center##RuntimeUIMultiAlignVCenter", ImVec2(88.0f, 0.0f)))
		{
			AlignSelectedWidgetsToSelection('y', 0.5f);
		}
		ImGui::SameLine();
		if (ImGui::Button("Bottom##RuntimeUIMultiAlignBottom", ImVec2(72.0f, 0.0f)))
		{
			AlignSelectedWidgetsToSelection('y', 1.0f);
		}
		ImGui::TextDisabled("Distribute");
		if (ImGui::Button("Horizontal##RuntimeUIMultiDistributeH", ImVec2(104.0f, 0.0f)))
		{
			DistributeSelectedWidgets(true);
		}
		ImGui::SameLine();
		if (ImGui::Button("Vertical##RuntimeUIMultiDistributeV", ImVec2(88.0f, 0.0f)))
		{
			DistributeSelectedWidgets(false);
		}
		if (ImGui::Button("Wrap In Panel##RuntimeUIMultiWrapPanel", ImVec2(124.0f, 0.0f)))
		{
			WrapSelectedWidgetsInPanel();
		}
		ImGui::Separator();
		if (ImGui::Button("Duplicate Selection##RuntimeUIMultiDuplicate", ImVec2(142.0f, 0.0f)))
		{
			DuplicateSelectedWidgets();
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete Selection##RuntimeUIMultiDelete", ImVec2(124.0f, 0.0f)))
		{
			DeleteSelectedWidgets();
		}
		ImGui::Separator();
		ImGui::Checkbox("Grid", &bShowDesignerGrid);
		ImGui::SameLine();
		ImGui::Checkbox("Snap", &bSnapToGrid);
		ImGui::SameLine();
		ImGui::Checkbox("Smart", &bSmartGuides);
		ImGui::DragFloat("Grid Size", &DesignerGridSize, 1.0f, 1.0f, 256.0f, "%.0f");
		return;
	}

	FRuntimeUIWidgetNode* Node = LayoutAsset.GetMutableWidget(SelectedWidgetIndex);
	if (!Node)
	{
		ImGui::TextDisabled("No widget selected.");
		return;
	}

	ImGui::TextDisabled("%s", GetRuntimeUIWidgetTypeLabel(Node->Type));
	ImGui::TextDisabled("Parent: %d", Node->ParentIndex);
	if (SelectedWidgetIndex != 0)
	{
		ImGui::Checkbox("Visible##RuntimeUIWidgetVisible", &Node->bVisible);
		ImGui::SameLine();
		ImGui::Checkbox("Locked##RuntimeUIWidgetLocked", &Node->bLocked);
	}
	if (SelectedWidgetIndex != 0)
	{
		auto IsDescendantOfSelected = [this](int32 CandidateIndex) -> bool
		{
			int32 CurrentIndex = CandidateIndex;
			while (const FRuntimeUIWidgetNode* CurrentNode = LayoutAsset.GetWidget(CurrentIndex))
			{
				if (CurrentNode->ParentIndex == SelectedWidgetIndex)
				{
					return true;
				}
				CurrentIndex = CurrentNode->ParentIndex;
			}
			return false;
		};

		const FRuntimeUIWidgetNode* ParentNode = LayoutAsset.GetWidget(Node->ParentIndex);
		const FString CurrentParentLabel = ParentNode
			? ((ParentNode->DisplayName.empty() ? ParentNode->Id : ParentNode->DisplayName) + "##" + std::to_string(Node->ParentIndex))
			: "None";
		ImGui::BeginDisabled(Node->bLocked);
		if (ImGui::BeginCombo("Parent##RuntimeUIParentCombo", CurrentParentLabel.c_str()))
		{
			const TArray<FRuntimeUIWidgetNode>& Widgets = LayoutAsset.GetWidgets();
			for (int32 CandidateIndex = 0; CandidateIndex < static_cast<int32>(Widgets.size()); ++CandidateIndex)
			{
				if (CandidateIndex == SelectedWidgetIndex || IsDescendantOfSelected(CandidateIndex))
				{
					continue;
				}

				const FRuntimeUIWidgetNode& Candidate = Widgets[CandidateIndex];
				const FString Label = (Candidate.DisplayName.empty() ? Candidate.Id : Candidate.DisplayName)
					+ " (" + Candidate.Id + ")";
				const bool bSelected = Node->ParentIndex == CandidateIndex;
				if (ImGui::Selectable(Label.c_str(), bSelected))
				{
					LayoutAsset.SetWidgetParent(SelectedWidgetIndex, CandidateIndex);
					Node = LayoutAsset.GetMutableWidget(SelectedWidgetIndex);
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
	}
	DrawRuntimeUIStringInput("Id##RuntimeUIWidgetId", Node->Id);
	FString ValidationError;
	if (!LayoutAsset.ValidateForExport(&ValidationError))
	{
		ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.35f, 1.0f), "%s", ValidationError.c_str());
	}
	DrawRuntimeUIStringInput("Name##RuntimeUIWidgetName", Node->DisplayName);
	DrawRuntimeUIStringInput("Class##RuntimeUIWidgetStyleClass", Node->StyleClass);

	ImGui::BeginDisabled(SelectedWidgetIndex != 0 && Node->bLocked);
	ImGui::DragFloat2("Position", &Node->Position.X, 1.0f);
	ImGui::DragFloat2("Size", &Node->Size.X, 1.0f, 1.0f, 8192.0f);
	ImGui::DragFloat2("Pivot", &Node->Pivot.X, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat2("Anchor Min", &Node->AnchorMin.X, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat2("Anchor Max", &Node->AnchorMax.X, 0.01f, 0.0f, 1.0f);
	Node->AnchorMin.X = std::clamp(Node->AnchorMin.X, 0.0f, 1.0f);
	Node->AnchorMin.Y = std::clamp(Node->AnchorMin.Y, 0.0f, 1.0f);
	Node->AnchorMax.X = std::clamp(Node->AnchorMax.X, Node->AnchorMin.X, 1.0f);
	Node->AnchorMax.Y = std::clamp(Node->AnchorMax.Y, Node->AnchorMin.Y, 1.0f);
	ImGui::DragFloat("Rotation", &Node->Rotation, 1.0f, -360.0f, 360.0f, "%.0f deg");
	ImGui::DragFloat2("Scale", &Node->Scale.X, 0.01f, 0.01f, 16.0f);

	if (SelectedWidgetIndex != 0)
	{
		const FRuntimeUIWidgetNode* ParentNode = LayoutAsset.GetWidget(Node->ParentIndex);
		if (ParentNode)
		{
			if (IsRuntimeUILayoutContainer(*ParentNode))
			{
				ImGui::TextColored(
					ImVec4(0.55f, 0.74f, 1.0f, 1.0f),
					"Managed by parent %s layout. Position is ignored; reorder in Hierarchy.",
					GetRuntimeUILayoutModeLabel(ParentNode->LayoutMode));
				const ERuntimeUILayoutSizeRule SizeRules[] =
				{
					ERuntimeUILayoutSizeRule::Auto,
					ERuntimeUILayoutSizeRule::Fill,
				};
				if (ImGui::BeginCombo("Slot Size##RuntimeUILayoutSizeRule", GetRuntimeUILayoutSizeRuleLabel(Node->LayoutSizeRule)))
				{
					for (ERuntimeUILayoutSizeRule SizeRule : SizeRules)
					{
						const bool bSelected = Node->LayoutSizeRule == SizeRule;
						if (ImGui::Selectable(GetRuntimeUILayoutSizeRuleLabel(SizeRule), bSelected))
						{
							Node->LayoutSizeRule = SizeRule;
						}
						if (bSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
				if (Node->LayoutSizeRule == ERuntimeUILayoutSizeRule::Fill)
				{
					ImGui::SetNextItemWidth(112.0f);
					ImGui::DragFloat("Fill Weight##RuntimeUILayoutFillWeight", &Node->LayoutFillWeight, 0.05f, 0.01f, 100.0f, "%.2f");
				}
			}
			const bool bStretchX = std::abs(Node->AnchorMax.X - Node->AnchorMin.X) > 0.0001f;
			const bool bStretchY = std::abs(Node->AnchorMax.Y - Node->AnchorMin.Y) > 0.0001f;
			const float LeftOffset = Node->Position.X - ParentNode->Size.X * Node->AnchorMin.X;
			const float TopOffset = Node->Position.Y - ParentNode->Size.Y * Node->AnchorMin.Y;
			const float RightOffset = ParentNode->Size.X * Node->AnchorMax.X - (Node->Position.X + Node->Size.X);
			const float BottomOffset = ParentNode->Size.Y * Node->AnchorMax.Y - (Node->Position.Y + Node->Size.Y);
			ImGui::TextDisabled(
				"Responsive: X %s  Y %s",
				bStretchX ? "Stretch" : "Fixed",
				bStretchY ? "Stretch" : "Fixed");
			if (bStretchX || bStretchY)
			{
				ImGui::TextDisabled("Offsets L %.0f T %.0f R %.0f B %.0f", LeftOffset, TopOffset, RightOffset, BottomOffset);
			}

			auto ApplyPosition = [this, Node](float X, float Y)
			{
				Node->Position = SnapPosition(FVector2(X, Y));
			};
			auto ApplySize = [this, Node](float X, float Y)
			{
				Node->Size = SnapSize(FVector2(std::max(1.0f, X), std::max(1.0f, Y)));
			};

			const FVector2 ParentSize = ParentNode->Size;
			ImGui::TextDisabled("Layout");
			if (ImGui::Button("Left##RuntimeUIAlignLeft", ImVec2(72.0f, 0.0f)))
			{
				ApplyPosition(0.0f, Node->Position.Y);
			}
			ImGui::SameLine();
			if (ImGui::Button("H Center##RuntimeUIAlignHCenter", ImVec2(88.0f, 0.0f)))
			{
				ApplyPosition((ParentSize.X - Node->Size.X) * 0.5f, Node->Position.Y);
			}
			ImGui::SameLine();
			if (ImGui::Button("Right##RuntimeUIAlignRight", ImVec2(72.0f, 0.0f)))
			{
				ApplyPosition(ParentSize.X - Node->Size.X, Node->Position.Y);
			}

			if (ImGui::Button("Top##RuntimeUIAlignTop", ImVec2(72.0f, 0.0f)))
			{
				ApplyPosition(Node->Position.X, 0.0f);
			}
			ImGui::SameLine();
			if (ImGui::Button("V Center##RuntimeUIAlignVCenter", ImVec2(88.0f, 0.0f)))
			{
				ApplyPosition(Node->Position.X, (ParentSize.Y - Node->Size.Y) * 0.5f);
			}
			ImGui::SameLine();
			if (ImGui::Button("Bottom##RuntimeUIAlignBottom", ImVec2(72.0f, 0.0f)))
			{
				ApplyPosition(Node->Position.X, ParentSize.Y - Node->Size.Y);
			}

			if (ImGui::Button("Fill##RuntimeUIFill", ImVec2(72.0f, 0.0f)))
			{
				ApplyPosition(0.0f, 0.0f);
				ApplySize(ParentSize.X, ParentSize.Y);
			}
			ImGui::SameLine();
			if (ImGui::Button("Fill W##RuntimeUIFillWidth", ImVec2(88.0f, 0.0f)))
			{
				ApplyPosition(0.0f, Node->Position.Y);
				ApplySize(ParentSize.X, Node->Size.Y);
			}
			ImGui::SameLine();
			if (ImGui::Button("Fill H##RuntimeUIFillHeight", ImVec2(72.0f, 0.0f)))
			{
				ApplyPosition(Node->Position.X, 0.0f);
				ApplySize(Node->Size.X, ParentSize.Y);
			}
		}
	}

	ImGui::TextDisabled("Anchor Preset");
	const ImVec2 AnchorButtonSize(52.0f, 0.0f);
	DrawRuntimeUIAnchorPresetButton("TL##RuntimeUIAnchorTL", *Node, FVector2(0.0f, 0.0f), FVector2(0.0f, 0.0f), FVector2(0.0f, 0.0f), AnchorButtonSize);
	ImGui::SameLine();
	DrawRuntimeUIAnchorPresetButton("TC##RuntimeUIAnchorTC", *Node, FVector2(0.5f, 0.0f), FVector2(0.5f, 0.0f), FVector2(0.5f, 0.0f), AnchorButtonSize);
	ImGui::SameLine();
	DrawRuntimeUIAnchorPresetButton("TR##RuntimeUIAnchorTR", *Node, FVector2(1.0f, 0.0f), FVector2(1.0f, 0.0f), FVector2(1.0f, 0.0f), AnchorButtonSize);

	DrawRuntimeUIAnchorPresetButton("ML##RuntimeUIAnchorML", *Node, FVector2(0.0f, 0.5f), FVector2(0.0f, 0.5f), FVector2(0.0f, 0.5f), AnchorButtonSize);
	ImGui::SameLine();
	DrawRuntimeUIAnchorPresetButton("MC##RuntimeUIAnchorMC", *Node, FVector2(0.5f, 0.5f), FVector2(0.5f, 0.5f), FVector2(0.5f, 0.5f), AnchorButtonSize);
	ImGui::SameLine();
	DrawRuntimeUIAnchorPresetButton("MR##RuntimeUIAnchorMR", *Node, FVector2(1.0f, 0.5f), FVector2(1.0f, 0.5f), FVector2(1.0f, 0.5f), AnchorButtonSize);

	DrawRuntimeUIAnchorPresetButton("BL##RuntimeUIAnchorBL", *Node, FVector2(0.0f, 1.0f), FVector2(0.0f, 1.0f), FVector2(0.0f, 1.0f), AnchorButtonSize);
	ImGui::SameLine();
	DrawRuntimeUIAnchorPresetButton("BC##RuntimeUIAnchorBC", *Node, FVector2(0.5f, 1.0f), FVector2(0.5f, 1.0f), FVector2(0.5f, 1.0f), AnchorButtonSize);
	ImGui::SameLine();
	DrawRuntimeUIAnchorPresetButton("BR##RuntimeUIAnchorBR", *Node, FVector2(1.0f, 1.0f), FVector2(1.0f, 1.0f), FVector2(1.0f, 1.0f), AnchorButtonSize);

	if (ImGui::Button("Stretch X##RuntimeUIAnchorStretchX", ImVec2(82.0f, 0.0f)))
	{
		ApplyRuntimeUIAnchorPreset(*Node, FVector2(0.0f, Node->AnchorMin.Y), FVector2(1.0f, Node->AnchorMax.Y), FVector2(0.5f, Node->Pivot.Y));
	}
	ImGui::SameLine();
	if (ImGui::Button("Stretch Y##RuntimeUIAnchorStretchY", ImVec2(82.0f, 0.0f)))
	{
		ApplyRuntimeUIAnchorPreset(*Node, FVector2(Node->AnchorMin.X, 0.0f), FVector2(Node->AnchorMax.X, 1.0f), FVector2(Node->Pivot.X, 0.5f));
	}
	ImGui::SameLine();
	DrawRuntimeUIAnchorPresetButton("Full##RuntimeUIAnchorFull", *Node, FVector2(0.0f, 0.0f), FVector2(1.0f, 1.0f), FVector2(0.5f, 0.5f), ImVec2(52.0f, 0.0f));
	ImGui::EndDisabled();

	if (Node->Type == ERuntimeUIWidgetType::Canvas || Node->Type == ERuntimeUIWidgetType::Panel)
	{
		ImGui::Separator();
		ImGui::TextDisabled("Children Layout");
		const ERuntimeUILayoutMode LayoutModes[] =
		{
			ERuntimeUILayoutMode::Free,
			ERuntimeUILayoutMode::Horizontal,
			ERuntimeUILayoutMode::Vertical,
		};
		if (ImGui::BeginCombo("Mode##RuntimeUILayoutMode", GetRuntimeUILayoutModeLabel(Node->LayoutMode)))
		{
			for (ERuntimeUILayoutMode LayoutMode : LayoutModes)
			{
				const bool bSelected = Node->LayoutMode == LayoutMode;
				if (ImGui::Selectable(GetRuntimeUILayoutModeLabel(LayoutMode), bSelected))
				{
					Node->LayoutMode = LayoutMode;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (Node->LayoutMode != ERuntimeUILayoutMode::Free)
		{
			const ERuntimeUILayoutAlignment Alignments[] =
			{
				ERuntimeUILayoutAlignment::Start,
				ERuntimeUILayoutAlignment::Center,
				ERuntimeUILayoutAlignment::End,
				ERuntimeUILayoutAlignment::Stretch,
			};
			if (ImGui::BeginCombo("Cross Align##RuntimeUILayoutAlignment", GetRuntimeUILayoutAlignmentLabel(Node->LayoutAlignment)))
			{
				for (ERuntimeUILayoutAlignment Alignment : Alignments)
				{
					const bool bSelected = Node->LayoutAlignment == Alignment;
					if (ImGui::Selectable(GetRuntimeUILayoutAlignmentLabel(Alignment), bSelected))
					{
						Node->LayoutAlignment = Alignment;
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::DragFloat("Gap##RuntimeUILayoutGap", &Node->LayoutGap, 1.0f, 0.0f, 512.0f, "%.0f px");
			ImGui::DragFloat4("Padding LTRB##RuntimeUILayoutPadding", &Node->LayoutPadding.X, 1.0f, 0.0f, 2048.0f, "%.0f");
			ImGui::TextDisabled("Horizontal/Vertical mode uses child order from the hierarchy.");
		}
	}

	ImGui::Separator();
	ImGui::Checkbox("Grid", &bShowDesignerGrid);
	ImGui::SameLine();
	ImGui::Checkbox("Snap", &bSnapToGrid);
	ImGui::SameLine();
	ImGui::Checkbox("Smart", &bSmartGuides);
	ImGui::SetNextItemWidth(110.0f);
	ImGui::DragFloat("Grid Size", &DesignerGridSize, 1.0f, 1.0f, 256.0f, "%.0f");

	if (Node->Type == ERuntimeUIWidgetType::Text || Node->Type == ERuntimeUIWidgetType::Button)
	{
		DrawRuntimeUIStringInput("Text##RuntimeUIWidgetText", Node->Text);
	}
	if (Node->Type == ERuntimeUIWidgetType::Image)
	{
		DrawRuntimeUIStringInput("Image##RuntimeUIWidgetImage", Node->ImagePath);
		FString DroppedImagePath;
		if (AcceptRuntimeUIImageDragDrop(DroppedImagePath))
		{
			Node->ImagePath = DroppedImagePath;
		}
		ImGui::ColorEdit4("Image Tint##RuntimeUIImageTint", &Node->ImageTint.X);
		const ERuntimeUIImageFit ImageFitItems[] =
		{
			ERuntimeUIImageFit::Stretch,
			ERuntimeUIImageFit::Contain,
			ERuntimeUIImageFit::Cover,
		};
		if (ImGui::BeginCombo("Image Fit##RuntimeUIImageFit", GetRuntimeUIImageFitLabel(Node->ImageFit)))
		{
			for (ERuntimeUIImageFit ImageFit : ImageFitItems)
			{
				const bool bSelected = Node->ImageFit == ImageFit;
				if (ImGui::Selectable(GetRuntimeUIImageFitLabel(ImageFit), bSelected))
				{
					Node->ImageFit = ImageFit;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::TextDisabled("Drop image asset here");
	}
	if (Node->Type == ERuntimeUIWidgetType::Button)
	{
		DrawButtonActionEditor(*Node);
	}

	ImGui::Separator();
	ImGui::TextDisabled("Style");
	if (ImGui::Button("Clear##RuntimeUIStyleClear", ImVec2(64.0f, 0.0f)))
	{
		ApplyRuntimeUIStylePreset(*Node, "Clear");
	}
	ImGui::SameLine();
	if (ImGui::Button("Panel##RuntimeUIStylePanel", ImVec2(64.0f, 0.0f)))
	{
		ApplyRuntimeUIStylePreset(*Node, "Panel");
	}
	ImGui::SameLine();
	if (ImGui::Button("Primary##RuntimeUIStylePrimary", ImVec2(72.0f, 0.0f)))
	{
		ApplyRuntimeUIStylePreset(*Node, "Primary");
	}
	if (ImGui::Button("Surface##RuntimeUIStyleSurface", ImVec2(72.0f, 0.0f)))
	{
		ApplyRuntimeUIStylePreset(*Node, "Surface");
	}
	ImGui::SameLine();
	if (ImGui::Button("Secondary##RuntimeUIStyleSecondary", ImVec2(86.0f, 0.0f)))
	{
		ApplyRuntimeUIStylePreset(*Node, "Secondary");
	}
	ImGui::SameLine();
	if (ImGui::Button("Ghost##RuntimeUIStyleGhost", ImVec2(64.0f, 0.0f)))
	{
		ApplyRuntimeUIStylePreset(*Node, "Ghost");
	}
	if (ImGui::Button("Danger##RuntimeUIStyleDanger", ImVec2(72.0f, 0.0f)))
	{
		ApplyRuntimeUIStylePreset(*Node, "Danger");
	}
	ImGui::SameLine();
	if (ImGui::Button("Success##RuntimeUIStyleSuccess", ImVec2(78.0f, 0.0f)))
	{
		ApplyRuntimeUIStylePreset(*Node, "Success");
	}
	ImGui::SameLine();
	if (ImGui::Button("HUD##RuntimeUIStyleHUD", ImVec2(56.0f, 0.0f)))
	{
		ApplyRuntimeUIStylePreset(*Node, "HUD");
	}
	ImGui::ColorEdit4("Background##RuntimeUIBackgroundColor", &Node->BackgroundColor.X);
	ImGui::DragFloat("Opacity##RuntimeUIOpacity", &Node->Opacity, 0.01f, 0.0f, 1.0f, "%.2f");
	ImGui::ColorEdit4("Border Color##RuntimeUIBorderColor", &Node->BorderColor.X);
	ImGui::DragFloat4("Border Width##RuntimeUIBorderWidth", &Node->BorderWidth.X, 1.0f, 0.0f, 128.0f, "%.0f px");
	ImGui::DragFloat("Radius##RuntimeUIBorderRadius", &Node->BorderRadius, 1.0f, 0.0f, 256.0f, "%.0f px");
	if (Node->Type == ERuntimeUIWidgetType::Text || Node->Type == ERuntimeUIWidgetType::Button)
	{
		ImGui::Separator();
		ImGui::TextDisabled("Typography");
		if (ImGui::Button("Title##RuntimeUITextPresetTitle", ImVec2(58.0f, 0.0f)))
		{
			ApplyRuntimeUITextPreset(*Node, "Title");
		}
		ImGui::SameLine();
		if (ImGui::Button("Heading##RuntimeUITextPresetHeading", ImVec2(78.0f, 0.0f)))
		{
			ApplyRuntimeUITextPreset(*Node, "Heading");
		}
		ImGui::SameLine();
		if (ImGui::Button("Body##RuntimeUITextPresetBody", ImVec2(58.0f, 0.0f)))
		{
			ApplyRuntimeUITextPreset(*Node, "Body");
		}
		if (ImGui::Button("Caption##RuntimeUITextPresetCaption", ImVec2(72.0f, 0.0f)))
		{
			ApplyRuntimeUITextPreset(*Node, "Caption");
		}
		ImGui::SameLine();
		if (ImGui::Button("Button##RuntimeUITextPresetButton", ImVec2(68.0f, 0.0f)))
		{
			ApplyRuntimeUITextPreset(*Node, "Button");
		}
		ImGui::ColorEdit4("Text Color##RuntimeUITextColor", &Node->TextColor.X);
		ImGui::DragFloat("Font Size##RuntimeUIFontSize", &Node->FontSize, 1.0f, 1.0f, 256.0f, "%.0f px");

		const char* FontFamilyItems[] =
		{
			"Malgun Gothic",
			"Malgun Gothic Semilight",
			"Nexon Lv1 Gothic",
			"Noto Sans KR",
			"Segoe UI",
			"Segoe UI Semibold",
			"Arial",
			"Verdana",
			"Tahoma",
			"Consolas",
			"Courier New",
			"Georgia",
			"Times New Roman",
			"serif",
			"sans-serif",
			"monospace",
		};
		const char* CurrentFontFamilyLabel = Node->FontFamily.empty() ? FontFamilyItems[0] : Node->FontFamily.c_str();
		if (ImGui::BeginCombo("Font Family##RuntimeUIFontFamily", CurrentFontFamilyLabel))
		{
			for (const char* FontFamily : FontFamilyItems)
			{
				const bool bSelected = Node->FontFamily == FontFamily || (Node->FontFamily.empty() && std::strcmp(FontFamily, FontFamilyItems[0]) == 0);
				if (ImGui::Selectable(FontFamily, bSelected))
				{
					Node->FontFamily = FontFamily;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		DrawRuntimeUIStringInput("Custom Font##RuntimeUICustomFontFamily", Node->FontFamily);
		const int32 FontWeights[] = { 100, 200, 300, 400, 500, 600, 700, 800, 900 };
		FString WeightLabel = std::to_string(std::clamp(Node->FontWeight, 100, 900));
		if (ImGui::BeginCombo("Weight##RuntimeUIFontWeight", WeightLabel.c_str()))
		{
			for (int32 FontWeight : FontWeights)
			{
				const bool bSelected = Node->FontWeight == FontWeight;
				FString ItemLabel = std::to_string(FontWeight);
				if (FontWeight == 400)
				{
					ItemLabel += " Regular";
				}
				else if (FontWeight == 700)
				{
					ItemLabel += " Bold";
				}
				if (ImGui::Selectable(ItemLabel.c_str(), bSelected))
				{
					Node->FontWeight = FontWeight;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SetNextItemWidth(112.0f);
		ImGui::DragFloat("Line Height##RuntimeUILineHeight", &Node->LineHeight, 1.0f, 0.0f, 512.0f, "%.0f px");
		ImGui::SameLine();
		if (ImGui::Button("Auto##RuntimeUILineHeightAuto", ImVec2(52.0f, 0.0f)))
		{
			Node->LineHeight = 0.0f;
		}
		if (Node->LineHeight <= 0.0f)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("uses widget height");
		}
		ImGui::SetNextItemWidth(112.0f);
		ImGui::DragFloat("Letter Spacing##RuntimeUILetterSpacing", &Node->LetterSpacing, 0.1f, -32.0f, 64.0f, "%.1f px");
		ImGui::Checkbox("Wrap##RuntimeUITextWrap", &Node->bTextWrap);

		const char* AlignItems[] = { "left", "center", "right" };
		int32 CurrentAlign = 1;
		for (int32 AlignIndex = 0; AlignIndex < 3; ++AlignIndex)
		{
			if (Node->TextAlign == AlignItems[AlignIndex])
			{
				CurrentAlign = AlignIndex;
				break;
			}
		}
		if (ImGui::BeginCombo("Text Align##RuntimeUITextAlign", AlignItems[CurrentAlign]))
		{
			for (int32 AlignIndex = 0; AlignIndex < 3; ++AlignIndex)
			{
				const bool bSelected = CurrentAlign == AlignIndex;
				if (ImGui::Selectable(AlignItems[AlignIndex], bSelected))
				{
					Node->TextAlign = AlignItems[AlignIndex];
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}

	if (Node->Type == ERuntimeUIWidgetType::Button)
	{
		ImGui::Separator();
		ImGui::TextDisabled("Button States");
		ImGui::Checkbox("Use State Style##RuntimeUIButtonUseStateStyle", &Node->bUseButtonStateStyle);
		ImGui::SameLine();
		if (ImGui::Button("Derive From Normal##RuntimeUIButtonDeriveStates", ImVec2(136.0f, 0.0f)))
		{
			DeriveRuntimeUIButtonStateStyles(*Node);
			Node->bUseButtonStateStyle = true;
		}
		if (Node->bUseButtonStateStyle)
		{
			DrawRuntimeUIButtonStateStyleEditor("Hover##RuntimeUIButtonHoverStyle", Node->ButtonHoverStyle);
			DrawRuntimeUIButtonStateStyleEditor("Pressed##RuntimeUIButtonPressedStyle", Node->ButtonPressedStyle);
			DrawRuntimeUIButtonStateStyleEditor("Disabled##RuntimeUIButtonDisabledStyle", Node->ButtonDisabledStyle);
		}
	}

	ImGui::Separator();
	ImGui::Checkbox("NineSlice", &Node->bUseNineSlice);
	if (Node->bUseNineSlice)
	{
		ImGui::DragFloat4("Border", &Node->NineSliceBorder.X, 1.0f, 0.0f, 512.0f);
	}

	ImGui::Separator();
	ImGui::TextDisabled("Generated");
	ImGui::InputText("RML##RuntimeUIGeneratedRml", GeneratedRmlPathBuffer, IM_ARRAYSIZE(GeneratedRmlPathBuffer));
	ImGui::InputText("RCSS##RuntimeUIGeneratedRcss", GeneratedRcssPathBuffer, IM_ARRAYSIZE(GeneratedRcssPathBuffer));
}

void FEditorRuntimeUIPreviewWidget::DrawButtonActionEditor(FRuntimeUIWidgetNode& Node)
{
	ImGui::Separator();
	ImGui::TextDisabled("Action");
	DrawRuntimeUIStringInput("OnClick##RuntimeUIWidgetOnClick", Node.OnClickAction);
	if (!IsRuntimeUIActionNameValid(Node.OnClickAction))
	{
		ImGui::TextColored(
			ImVec4(1.0f, 0.58f, 0.25f, 1.0f),
			"Use letters, numbers, _, -, . or : for Lua-friendly action names.");
	}
	else if (Node.OnClickAction.empty())
	{
		ImGui::TextDisabled("No data-action will be exported for this button.");
	}
	else
	{
		ImGui::TextDisabled("Exports as data-action=\"%s\"", Node.OnClickAction.c_str());
	}

	if (ImGui::Button("From Id##RuntimeUIActionFromId", ImVec2(72.0f, 0.0f)))
	{
		Node.OnClickAction = MakeActionNameFromLabel(Node.Id);
	}
	ImGui::SameLine();
	if (ImGui::Button("From Name##RuntimeUIActionFromName", ImVec2(88.0f, 0.0f)))
	{
		Node.OnClickAction = MakeActionNameFromLabel(Node.DisplayName.empty() ? Node.Text : Node.DisplayName);
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear##RuntimeUIActionClear", ImVec2(58.0f, 0.0f)))
	{
		Node.OnClickAction.clear();
	}

	const TArray<FString> KnownActions = CollectKnownActionNames();
	const FString CurrentActionLabel = Node.OnClickAction.empty() ? FString("None") : Node.OnClickAction;
	if (ImGui::BeginCombo("Known##RuntimeUIKnownActions", CurrentActionLabel.c_str()))
	{
		if (ImGui::Selectable("None", Node.OnClickAction.empty()))
		{
			Node.OnClickAction.clear();
		}
		for (const FString& ActionName : KnownActions)
		{
			const bool bSelected = Node.OnClickAction == ActionName;
			if (ImGui::Selectable(ActionName.c_str(), bSelected))
			{
				Node.OnClickAction = ActionName;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
}

void FEditorRuntimeUIPreviewWidget::DrawWidgetContextMenu(int32 WidgetIndex)
{
	FRuntimeUIWidgetNode* Node = LayoutAsset.GetMutableWidget(WidgetIndex);
	if (!Node)
	{
		WidgetIndex = 0;
		Node = LayoutAsset.GetMutableWidget(WidgetIndex);
	}
	if (!Node)
	{
		return;
	}

	const FString Label = Node->DisplayName.empty() ? Node->Id : Node->DisplayName;
	ImGui::TextDisabled("%s", Label.c_str());
	ImGui::Separator();

	if (HasMultiSelection())
	{
		ImGui::Text("%d selected", static_cast<int32>(GetEditableSelectedWidgets().size()));
		if (ImGui::MenuItem("Duplicate Selection##RuntimeUIContextDuplicateSelection", "Ctrl+D"))
		{
			DuplicateSelectedWidgets();
			ImGui::CloseCurrentPopup();
			return;
		}
		if (ImGui::MenuItem("Delete Selection##RuntimeUIContextDeleteSelection", "Del"))
		{
			DeleteSelectedWidgets();
			ImGui::CloseCurrentPopup();
			return;
		}
		if (ImGui::BeginMenu("Align Selection##RuntimeUIContextAlignSelection"))
		{
			if (ImGui::MenuItem("Left##RuntimeUIContextMultiAlignLeft"))
			{
				AlignSelectedWidgetsToSelection('x', 0.0f);
			}
			if (ImGui::MenuItem("Horizontal Center##RuntimeUIContextMultiAlignHCenter"))
			{
				AlignSelectedWidgetsToSelection('x', 0.5f);
			}
			if (ImGui::MenuItem("Right##RuntimeUIContextMultiAlignRight"))
			{
				AlignSelectedWidgetsToSelection('x', 1.0f);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Top##RuntimeUIContextMultiAlignTop"))
			{
				AlignSelectedWidgetsToSelection('y', 0.0f);
			}
			if (ImGui::MenuItem("Vertical Center##RuntimeUIContextMultiAlignVCenter"))
			{
				AlignSelectedWidgetsToSelection('y', 0.5f);
			}
			if (ImGui::MenuItem("Bottom##RuntimeUIContextMultiAlignBottom"))
			{
				AlignSelectedWidgetsToSelection('y', 1.0f);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Distribute Selection##RuntimeUIContextDistributeSelection"))
		{
			if (ImGui::MenuItem("Horizontal##RuntimeUIContextMultiDistributeH"))
			{
				DistributeSelectedWidgets(true);
			}
			if (ImGui::MenuItem("Vertical##RuntimeUIContextMultiDistributeV"))
			{
				DistributeSelectedWidgets(false);
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Wrap In Panel##RuntimeUIContextWrapSelection"))
		{
			WrapSelectedWidgetsInPanel();
			ImGui::CloseCurrentPopup();
			return;
		}
		ImGui::Separator();
	}

	DrawRuntimeUIStringInput("Id##RuntimeUIContextId", Node->Id);
	DrawRuntimeUIStringInput("Name##RuntimeUIContextName", Node->DisplayName);
	if (Node->Type == ERuntimeUIWidgetType::Button)
	{
		DrawRuntimeUIStringInput("Action##RuntimeUIContextAction", Node->OnClickAction);
		if (ImGui::BeginMenu("Known Actions##RuntimeUIContextKnownActions"))
		{
			if (ImGui::MenuItem("None##RuntimeUIContextActionNone", nullptr, Node->OnClickAction.empty()))
			{
				Node->OnClickAction.clear();
			}
			for (const FString& ActionName : CollectKnownActionNames())
			{
				const bool bSelected = Node->OnClickAction == ActionName;
				if (ImGui::MenuItem(ActionName.c_str(), nullptr, bSelected))
				{
					Node->OnClickAction = ActionName;
				}
			}
			ImGui::EndMenu();
		}
		if (!IsRuntimeUIActionNameValid(Node->OnClickAction))
		{
			ImGui::TextColored(ImVec4(1.0f, 0.58f, 0.25f, 1.0f), "Lua-unfriendly action name");
		}
	}
	ImGui::Separator();

	if (ImGui::BeginMenu("Style Preset##RuntimeUIContextStylePreset"))
	{
		const char* StylePresets[] =
		{
			"Clear",
			"Panel",
			"Surface",
			"Primary",
			"Secondary",
			"Ghost",
			"Danger",
			"Success",
			"HUD",
		};
		for (const char* PresetName : StylePresets)
		{
			if (ImGui::MenuItem(PresetName))
			{
				ApplyRuntimeUIStylePreset(*Node, PresetName);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndMenu();
	}
	if (Node->Type == ERuntimeUIWidgetType::Text || Node->Type == ERuntimeUIWidgetType::Button)
	{
		if (ImGui::BeginMenu("Text Preset##RuntimeUIContextTextPreset"))
		{
			const char* TextPresets[] = { "Title", "Heading", "Body", "Caption", "Button" };
			for (const char* PresetName : TextPresets)
			{
				if (ImGui::MenuItem(PresetName))
				{
					ApplyRuntimeUITextPreset(*Node, PresetName);
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndMenu();
		}
	}
	ImGui::Separator();

	if (Node->Type == ERuntimeUIWidgetType::Canvas || Node->Type == ERuntimeUIWidgetType::Panel)
	{
		if (ImGui::BeginMenu("Children Layout##RuntimeUIContextChildrenLayout"))
		{
			if (ImGui::MenuItem("Free##RuntimeUIContextLayoutFree", nullptr, Node->LayoutMode == ERuntimeUILayoutMode::Free))
			{
				Node->LayoutMode = ERuntimeUILayoutMode::Free;
			}
			if (ImGui::MenuItem("Horizontal##RuntimeUIContextLayoutHorizontal", nullptr, Node->LayoutMode == ERuntimeUILayoutMode::Horizontal))
			{
				Node->LayoutMode = ERuntimeUILayoutMode::Horizontal;
			}
			if (ImGui::MenuItem("Vertical##RuntimeUIContextLayoutVertical", nullptr, Node->LayoutMode == ERuntimeUILayoutMode::Vertical))
			{
				Node->LayoutMode = ERuntimeUILayoutMode::Vertical;
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
	}

	auto ResolveAddParent = [this, WidgetIndex]() -> int32
	{
		const FRuntimeUIWidgetNode* TargetNode = LayoutAsset.GetWidget(WidgetIndex);
		if (!TargetNode)
		{
			return 0;
		}
		if (!TargetNode->bLocked && (TargetNode->Type == ERuntimeUIWidgetType::Canvas || TargetNode->Type == ERuntimeUIWidgetType::Panel))
		{
			return WidgetIndex;
		}
		return LayoutAsset.GetWidget(TargetNode->ParentIndex) ? TargetNode->ParentIndex : 0;
	};

	auto AddWidgetFromMenu = [this, &ResolveAddParent](ERuntimeUIWidgetType Type)
	{
		const int32 NewWidgetIndex = LayoutAsset.AddWidget(Type, ResolveAddParent());
		if (NewWidgetIndex >= 0)
		{
			SelectSingleWidget(NewWidgetIndex);
			ImGui::CloseCurrentPopup();
		}
	};

	auto AddTemplateFromMenu = [this, &ResolveAddParent](const char* TemplateName)
	{
		ERuntimeUIWidgetType Type = ERuntimeUIWidgetType::Panel;
		if (std::strcmp(TemplateName, "Primary Button") == 0)
		{
			Type = ERuntimeUIWidgetType::Button;
		}
		else if (std::strcmp(TemplateName, "HUD Label") == 0)
		{
			Type = ERuntimeUIWidgetType::Text;
		}
		else if (std::strcmp(TemplateName, "Image Slot") == 0)
		{
			Type = ERuntimeUIWidgetType::Image;
		}

		const int32 NewWidgetIndex = LayoutAsset.AddWidget(Type, ResolveAddParent());
		if (NewWidgetIndex < 0)
		{
			return;
		}

		if (FRuntimeUIWidgetNode* NewNode = LayoutAsset.GetMutableWidget(NewWidgetIndex))
		{
			if (std::strcmp(TemplateName, "Horizontal Panel") == 0)
			{
				NewNode->DisplayName = "Horizontal Panel";
				NewNode->Size = FVector2(420.0f, 96.0f);
				NewNode->LayoutMode = ERuntimeUILayoutMode::Horizontal;
				NewNode->LayoutAlignment = ERuntimeUILayoutAlignment::Center;
				NewNode->LayoutPadding = FVector4(12.0f, 12.0f, 12.0f, 12.0f);
				NewNode->LayoutGap = 8.0f;
				ApplyRuntimeUIStylePreset(*NewNode, "Panel");
			}
			else if (std::strcmp(TemplateName, "Vertical Panel") == 0)
			{
				NewNode->DisplayName = "Vertical Panel";
				NewNode->Size = FVector2(320.0f, 260.0f);
				NewNode->LayoutMode = ERuntimeUILayoutMode::Vertical;
				NewNode->LayoutAlignment = ERuntimeUILayoutAlignment::Stretch;
				NewNode->LayoutPadding = FVector4(14.0f, 14.0f, 14.0f, 14.0f);
				NewNode->LayoutGap = 10.0f;
				ApplyRuntimeUIStylePreset(*NewNode, "Surface");
			}
			else if (std::strcmp(TemplateName, "HUD Panel") == 0)
			{
				NewNode->DisplayName = "HUD Panel";
				NewNode->Size = FVector2(360.0f, 88.0f);
				NewNode->LayoutMode = ERuntimeUILayoutMode::Horizontal;
				NewNode->LayoutAlignment = ERuntimeUILayoutAlignment::Center;
				NewNode->LayoutPadding = FVector4(12.0f, 10.0f, 12.0f, 10.0f);
				NewNode->LayoutGap = 12.0f;
				ApplyRuntimeUIStylePreset(*NewNode, "HUD");
			}
			else if (std::strcmp(TemplateName, "Primary Button") == 0)
			{
				NewNode->DisplayName = "Primary Button";
				NewNode->Text = "Button";
				NewNode->Size = FVector2(192.0f, 52.0f);
				ApplyRuntimeUIStylePreset(*NewNode, "Primary");
				ApplyRuntimeUITextPreset(*NewNode, "Button");
			}
			else if (std::strcmp(TemplateName, "HUD Label") == 0)
			{
				NewNode->DisplayName = "HUD Label";
				NewNode->Text = "HUD_LABEL";
				NewNode->Size = FVector2(220.0f, 36.0f);
				ApplyRuntimeUIStylePreset(*NewNode, "HUD");
				ApplyRuntimeUITextPreset(*NewNode, "Caption");
				NewNode->FontFamily = "Consolas";
				NewNode->FontWeight = 600;
				NewNode->LetterSpacing = 0.6f;
				NewNode->TextAlign = "center";
			}
			else if (std::strcmp(TemplateName, "Image Slot") == 0)
			{
				NewNode->DisplayName = "Image Slot";
				NewNode->Size = FVector2(256.0f, 256.0f);
				NewNode->ImageFit = ERuntimeUIImageFit::Cover;
				ApplyRuntimeUIStylePreset(*NewNode, "Ghost");
			}
		}

		SelectSingleWidget(NewWidgetIndex);
		ImGui::CloseCurrentPopup();
	};

	if (ImGui::BeginMenu("Add Child/Sibling##RuntimeUIContextAdd"))
	{
		if (ImGui::MenuItem("Panel##RuntimeUIContextAddPanel"))
		{
			AddWidgetFromMenu(ERuntimeUIWidgetType::Panel);
		}
		if (ImGui::MenuItem("Text##RuntimeUIContextAddText"))
		{
			AddWidgetFromMenu(ERuntimeUIWidgetType::Text);
		}
		if (ImGui::MenuItem("Button##RuntimeUIContextAddButton"))
		{
			AddWidgetFromMenu(ERuntimeUIWidgetType::Button);
		}
		if (ImGui::MenuItem("Image##RuntimeUIContextAddImage"))
		{
			AddWidgetFromMenu(ERuntimeUIWidgetType::Image);
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Add Template##RuntimeUIContextAddTemplate"))
	{
		if (ImGui::MenuItem("Horizontal Panel##RuntimeUIContextTemplateHorizontalPanel"))
		{
			AddTemplateFromMenu("Horizontal Panel");
		}
		if (ImGui::MenuItem("Vertical Panel##RuntimeUIContextTemplateVerticalPanel"))
		{
			AddTemplateFromMenu("Vertical Panel");
		}
		if (ImGui::MenuItem("HUD Panel##RuntimeUIContextTemplateHUDPanel"))
		{
			AddTemplateFromMenu("HUD Panel");
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Primary Button##RuntimeUIContextTemplatePrimaryButton"))
		{
			AddTemplateFromMenu("Primary Button");
		}
		if (ImGui::MenuItem("HUD Label##RuntimeUIContextTemplateHUDLabel"))
		{
			AddTemplateFromMenu("HUD Label");
		}
		if (ImGui::MenuItem("Image Slot##RuntimeUIContextTemplateImageSlot"))
		{
			AddTemplateFromMenu("Image Slot");
		}
		ImGui::EndMenu();
	}

	if (WidgetIndex != 0)
	{
		if (ImGui::MenuItem("Copy##RuntimeUIContextCopy"))
		{
			CopiedWidgetId = Node->Id;
		}

		const bool bLocked = Node->bLocked;
		if (ImGui::MenuItem("Duplicate##RuntimeUIContextDuplicate", "Ctrl+D", false, !bLocked))
		{
			const int32 DuplicatedIndex = LayoutAsset.DuplicateWidget(WidgetIndex);
			if (DuplicatedIndex >= 0)
			{
				SelectSingleWidget(DuplicatedIndex);
				ImGui::CloseCurrentPopup();
			}
		}
		if (ImGui::MenuItem("Delete##RuntimeUIContextDelete", "Del", false, !bLocked))
		{
			LayoutAsset.RemoveWidget(WidgetIndex);
			ClearWidgetSelection();
			ImGui::CloseCurrentPopup();
			return;
		}

		ImGui::Separator();
		ImGui::MenuItem("Visible##RuntimeUIContextVisible", nullptr, &Node->bVisible);
		ImGui::MenuItem("Locked##RuntimeUIContextLocked", nullptr, &Node->bLocked);

		ImGui::Separator();
		if (ImGui::MenuItem("Move Up##RuntimeUIContextMoveUp", nullptr, false, !bLocked))
		{
			SelectSingleWidget(LayoutAsset.MoveWidgetWithinParent(WidgetIndex, -1));
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::MenuItem("Move Down##RuntimeUIContextMoveDown", nullptr, false, !bLocked))
		{
			SelectSingleWidget(LayoutAsset.MoveWidgetWithinParent(WidgetIndex, 1));
			ImGui::CloseCurrentPopup();
		}
		const int32 ParentIndex = Node->ParentIndex;
		if (const FRuntimeUIWidgetNode* ParentNode = LayoutAsset.GetWidget(ParentIndex))
		{
			if (!ParentNode->Children.empty())
			{
				const int32 FirstSiblingIndex = ParentNode->Children.front();
				const int32 LastSiblingIndex = ParentNode->Children.back();
				if (ImGui::MenuItem("Send To Back##RuntimeUIContextSendBack", nullptr, false, !bLocked && WidgetIndex != FirstSiblingIndex))
				{
					SelectSingleWidget(LayoutAsset.MoveWidgetRelativeToSibling(WidgetIndex, FirstSiblingIndex, false));
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Bring To Front##RuntimeUIContextBringFront", nullptr, false, !bLocked && WidgetIndex != LastSiblingIndex))
				{
					SelectSingleWidget(LayoutAsset.MoveWidgetRelativeToSibling(WidgetIndex, LastSiblingIndex, true));
					ImGui::CloseCurrentPopup();
				}
			}
		}
		if (ImGui::MenuItem("Parent To Root##RuntimeUIContextParentRoot", nullptr, false, !bLocked))
		{
			LayoutAsset.SetWidgetParent(WidgetIndex, 0);
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::BeginMenu("Layout##RuntimeUIContextLayout", !bLocked))
		{
			FRuntimeUIWidgetNode* MutableNode = LayoutAsset.GetMutableWidget(WidgetIndex);
			const FRuntimeUIWidgetNode* ParentNode = MutableNode ? LayoutAsset.GetWidget(MutableNode->ParentIndex) : nullptr;
			if (MutableNode && ParentNode)
			{
				auto ApplyPosition = [this, MutableNode](float X, float Y)
				{
					MutableNode->Position = SnapPosition(FVector2(X, Y));
				};
				auto ApplySize = [this, MutableNode](float X, float Y)
				{
					MutableNode->Size = SnapSize(FVector2(std::max(1.0f, X), std::max(1.0f, Y)));
				};

				if (ImGui::MenuItem("Align Left##RuntimeUIContextAlignLeft"))
				{
					ApplyPosition(0.0f, MutableNode->Position.Y);
				}
				if (ImGui::MenuItem("Align Horizontal Center##RuntimeUIContextAlignHCenter"))
				{
					ApplyPosition((ParentNode->Size.X - MutableNode->Size.X) * 0.5f, MutableNode->Position.Y);
				}
				if (ImGui::MenuItem("Align Right##RuntimeUIContextAlignRight"))
				{
					ApplyPosition(ParentNode->Size.X - MutableNode->Size.X, MutableNode->Position.Y);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Align Top##RuntimeUIContextAlignTop"))
				{
					ApplyPosition(MutableNode->Position.X, 0.0f);
				}
				if (ImGui::MenuItem("Align Vertical Center##RuntimeUIContextAlignVCenter"))
				{
					ApplyPosition(MutableNode->Position.X, (ParentNode->Size.Y - MutableNode->Size.Y) * 0.5f);
				}
				if (ImGui::MenuItem("Align Bottom##RuntimeUIContextAlignBottom"))
				{
					ApplyPosition(MutableNode->Position.X, ParentNode->Size.Y - MutableNode->Size.Y);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Fill Parent##RuntimeUIContextFillParent"))
				{
					ApplyPosition(0.0f, 0.0f);
					ApplySize(ParentNode->Size.X, ParentNode->Size.Y);
					ApplyRuntimeUIAnchorPreset(*MutableNode, FVector2(0.0f, 0.0f), FVector2(1.0f, 1.0f), FVector2(0.5f, 0.5f));
				}
				if (ImGui::MenuItem("Fill Width##RuntimeUIContextFillWidth"))
				{
					ApplyPosition(0.0f, MutableNode->Position.Y);
					ApplySize(ParentNode->Size.X, MutableNode->Size.Y);
					ApplyRuntimeUIAnchorPreset(*MutableNode, FVector2(0.0f, MutableNode->AnchorMin.Y), FVector2(1.0f, MutableNode->AnchorMax.Y), FVector2(0.5f, MutableNode->Pivot.Y));
				}
				if (ImGui::MenuItem("Fill Height##RuntimeUIContextFillHeight"))
				{
					ApplyPosition(MutableNode->Position.X, 0.0f);
					ApplySize(MutableNode->Size.X, ParentNode->Size.Y);
					ApplyRuntimeUIAnchorPreset(*MutableNode, FVector2(MutableNode->AnchorMin.X, 0.0f), FVector2(MutableNode->AnchorMax.X, 1.0f), FVector2(MutableNode->Pivot.X, 0.5f));
				}
			}
			else
			{
				ImGui::TextDisabled("No parent.");
			}
			ImGui::EndMenu();
		}
	}

	if (!CopiedWidgetId.empty())
	{
		ImGui::Separator();
		if (ImGui::MenuItem("Paste Copy Here##RuntimeUIContextPaste"))
		{
			int32 SourceIndex = -1;
			const TArray<FRuntimeUIWidgetNode>& Widgets = LayoutAsset.GetWidgets();
			for (int32 Index = 1; Index < static_cast<int32>(Widgets.size()); ++Index)
			{
				if (Widgets[Index].Id == CopiedWidgetId)
				{
					SourceIndex = Index;
					break;
				}
			}
			const int32 DuplicatedIndex = SourceIndex >= 0 ? LayoutAsset.DuplicateWidget(SourceIndex) : -1;
			if (DuplicatedIndex >= 0)
			{
				LayoutAsset.SetWidgetParent(DuplicatedIndex, ResolveAddParent());
				SelectSingleWidget(DuplicatedIndex);
				if (const FRuntimeUIWidgetNode* DuplicatedNode = LayoutAsset.GetWidget(DuplicatedIndex))
				{
					CopiedWidgetId = DuplicatedNode->Id;
				}
				ImGui::CloseCurrentPopup();
			}
			else
			{
				CopiedWidgetId.clear();
			}
		}
	}
}

void FEditorRuntimeUIPreviewWidget::DrawDesignerGrid(
	ImDrawList* DrawList,
	const ImVec2& CanvasMin,
	const ImVec2& CanvasSize,
	float Scale) const
{
	if (!DrawList || !bShowDesignerGrid || DesignerGridSize <= 0.0f)
	{
		return;
	}

	const float Step = DesignerGridSize * Scale;
	if (Step < 4.0f)
	{
		return;
	}

	const ImVec2 CanvasMax(CanvasMin.x + CanvasSize.x, CanvasMin.y + CanvasSize.y);
	const ImU32 MinorColor = ImGui::GetColorU32(ImVec4(0.22f, 0.27f, 0.34f, 0.35f));
	const ImU32 MajorColor = ImGui::GetColorU32(ImVec4(0.32f, 0.39f, 0.48f, 0.42f));
	int32 LineIndex = 0;
	for (float X = CanvasMin.x; X <= CanvasMax.x; X += Step, ++LineIndex)
	{
		const ImU32 Color = (LineIndex % 10 == 0) ? MajorColor : MinorColor;
		DrawList->AddLine(ImVec2(X, CanvasMin.y), ImVec2(X, CanvasMax.y), Color, 1.0f);
	}
	LineIndex = 0;
	for (float Y = CanvasMin.y; Y <= CanvasMax.y; Y += Step, ++LineIndex)
	{
		const ImU32 Color = (LineIndex % 10 == 0) ? MajorColor : MinorColor;
		DrawList->AddLine(ImVec2(CanvasMin.x, Y), ImVec2(CanvasMax.x, Y), Color, 1.0f);
	}
}

void FEditorRuntimeUIPreviewWidget::DrawDesignerOverlay(ImDrawList* DrawList, const ImVec2& CanvasMin, float Scale)
{
	if (!DrawList)
	{
		return;
	}

	const TArray<FRuntimeUIWidgetNode>& Widgets = LayoutAsset.GetWidgets();
	for (int32 Index = 0; Index < static_cast<int32>(Widgets.size()); ++Index)
	{
		const FRuntimeUIWidgetNode& Node = Widgets[Index];
		if (!Node.bVisible)
		{
			continue;
		}
		FVector2 Absolute;
		FVector2 ResolvedSize;
		if (!GetWidgetResolvedRect(Index, Absolute, ResolvedSize))
		{
			continue;
		}
		const ImVec2 Min(CanvasMin.x + Absolute.X * Scale, CanvasMin.y + Absolute.Y * Scale);
		const ImVec2 Max(Min.x + ResolvedSize.X * Scale, Min.y + ResolvedSize.Y * Scale);
		const bool bSelected = IsWidgetSelected(Index);
		const float NodeOpacity = std::clamp(Node.Opacity, 0.0f, 1.0f);
		if (Node.BackgroundColor.W > 0.0f && NodeOpacity > 0.0f)
		{
			DrawList->AddRectFilled(
				Min,
				Max,
				ImGui::GetColorU32(ImVec4(
					Node.BackgroundColor.X,
					Node.BackgroundColor.Y,
					Node.BackgroundColor.Z,
					Node.BackgroundColor.W * NodeOpacity)),
				std::max(0.0f, Node.BorderRadius * Scale));
		}
		const float TotalBorderWidth = Node.BorderWidth.X + Node.BorderWidth.Y + Node.BorderWidth.Z + Node.BorderWidth.W;
		if (TotalBorderWidth > 0.0f && Node.BorderColor.W > 0.0f && NodeOpacity > 0.0f)
		{
			const float BorderThickness = std::max(1.0f, ((Node.BorderWidth.X + Node.BorderWidth.Y + Node.BorderWidth.Z + Node.BorderWidth.W) * 0.25f) * Scale);
			DrawList->AddRect(
				Min,
				Max,
				ImGui::GetColorU32(ImVec4(
					Node.BorderColor.X,
					Node.BorderColor.Y,
					Node.BorderColor.Z,
					Node.BorderColor.W * NodeOpacity)),
				std::max(0.0f, Node.BorderRadius * Scale),
				0,
				BorderThickness);
		}
		if ((Node.Type == ERuntimeUIWidgetType::Text || Node.Type == ERuntimeUIWidgetType::Button) && !Node.Text.empty() && NodeOpacity > 0.0f)
		{
			const float FontSize = std::max(1.0f, Node.FontSize * Scale);
			const float LineHeight = (Node.LineHeight > 0.0f ? Node.LineHeight : std::max(1.0f, ResolvedSize.Y)) * Scale;
			const float TextScale = FontSize / std::max(1.0f, ImGui::GetFontSize());
			const ImVec2 BaseTextSize = ImGui::CalcTextSize(Node.Text.c_str());
			const ImVec2 TextSize(BaseTextSize.x * TextScale, BaseTextSize.y * TextScale);
			float TextX = Min.x + 6.0f;
			if (Node.TextAlign == "center")
			{
				TextX = Min.x + std::max(0.0f, (ResolvedSize.X * Scale - TextSize.x) * 0.5f);
			}
			else if (Node.TextAlign == "right")
			{
				TextX = Max.x - TextSize.x - 6.0f;
			}
			const float TextY = Node.bTextWrap
				? Min.y + 4.0f
				: Min.y + std::max(0.0f, (ResolvedSize.Y * Scale - std::max(FontSize, LineHeight)) * 0.5f);
			const ImVec4 ClipRect(Min.x, Min.y, Max.x, Max.y);
			DrawList->AddText(
				ImGui::GetFont(),
				FontSize,
				ImVec2(TextX, TextY),
				ImGui::GetColorU32(ImVec4(Node.TextColor.X, Node.TextColor.Y, Node.TextColor.Z, Node.TextColor.W * NodeOpacity)),
				Node.Text.c_str(),
				nullptr,
				Node.bTextWrap ? std::max(1.0f, ResolvedSize.X * Scale - 12.0f) : 0.0f,
				&ClipRect);
		}
		else if (Node.Type == ERuntimeUIWidgetType::Image && !Node.ImagePath.empty() && NodeOpacity > 0.0f)
		{
			UTexture* Texture = FResourceManager::Get().LoadTexture(Node.ImagePath);
			if (Texture && Texture->GetSRV())
			{
				float TextureWidth = 0.0f;
				float TextureHeight = 0.0f;
				GetRuntimeUITextureSize(Texture, TextureWidth, TextureHeight);
				ImVec2 ImageMin;
				ImVec2 ImageMax;
				ImVec2 UvMin;
				ImVec2 UvMax;
				CalculateRuntimeUIImageFit(Node.ImageFit, Min, Max, TextureWidth, TextureHeight, ImageMin, ImageMax, UvMin, UvMax);
				DrawList->AddImage(
					reinterpret_cast<ImTextureID>(Texture->GetSRV()),
					ImageMin,
					ImageMax,
					UvMin,
					UvMax,
					ImGui::GetColorU32(ImVec4(
						Node.ImageTint.X,
						Node.ImageTint.Y,
						Node.ImageTint.Z,
						Node.ImageTint.W * NodeOpacity)));
			}
			else
			{
				const FString ImageLabel = std::filesystem::path(FPaths::ToWide(Node.ImagePath)).filename().string();
				DrawList->AddText(
					ImVec2(Min.x + 6.0f, Min.y + 6.0f),
					ImGui::GetColorU32(ImVec4(0.72f, 0.82f, 1.0f, 0.75f * NodeOpacity)),
					ImageLabel.c_str());
			}
		}
		const ImU32 Color = ImGui::GetColorU32(bSelected
			? ImVec4(1.0f, 0.74f, 0.22f, 1.0f)
			: ImVec4(0.42f, 0.64f, 1.0f, 0.8f));
		DrawList->AddRect(Min, Max, Color, 0.0f, 0, bSelected ? 2.0f : 1.0f);
		if (bSelected)
		{
			if (Index != 0)
			{
				FVector2 ParentAbsolute;
				FVector2 ParentSize;
				if (GetWidgetResolvedRect(Node.ParentIndex, ParentAbsolute, ParentSize))
				{
					const ImVec2 ParentMin(CanvasMin.x + ParentAbsolute.X * Scale, CanvasMin.y + ParentAbsolute.Y * Scale);
					const ImVec2 ParentMax(ParentMin.x + ParentSize.X * Scale, ParentMin.y + ParentSize.Y * Scale);
					DrawList->AddRect(
						ParentMin,
						ParentMax,
						ImGui::GetColorU32(ImVec4(0.32f, 0.78f, 0.92f, 0.35f)),
						0.0f,
						0,
						1.0f);

					const ImVec2 AnchorMinPoint(
						ParentMin.x + ParentSize.X * Node.AnchorMin.X * Scale,
						ParentMin.y + ParentSize.Y * Node.AnchorMin.Y * Scale);
					const ImVec2 AnchorMaxPoint(
						ParentMin.x + ParentSize.X * Node.AnchorMax.X * Scale,
						ParentMin.y + ParentSize.Y * Node.AnchorMax.Y * Scale);
					const ImU32 AnchorColor = ImGui::GetColorU32(ImVec4(0.25f, 0.86f, 1.0f, 0.85f));
					DrawList->AddCircleFilled(AnchorMinPoint, 3.5f, AnchorColor, 12);
					DrawList->AddCircleFilled(AnchorMaxPoint, 3.5f, AnchorColor, 12);
					DrawList->AddLine(AnchorMinPoint, Min, AnchorColor, 1.0f);
					DrawList->AddLine(AnchorMaxPoint, Max, AnchorColor, 1.0f);
				}
			}
			const ImVec2 Pivot(Min.x + ResolvedSize.X * Node.Pivot.X * Scale, Min.y + ResolvedSize.Y * Node.Pivot.Y * Scale);
			DrawList->AddCircleFilled(Pivot, 4.0f, Color, 16);
			if (Index != 0)
			{
				const float HandleSize = 9.0f;
				const ImVec2 HandleMin(Max.x - HandleSize, Max.y - HandleSize);
				const ImVec2 HandleMax(Max.x + HandleSize, Max.y + HandleSize);
				DrawList->AddRectFilled(
					HandleMin,
					HandleMax,
					ImGui::GetColorU32(ImVec4(1.0f, 0.74f, 0.22f, 1.0f)),
					2.0f);
				DrawList->AddRect(
					HandleMin,
					HandleMax,
					ImGui::GetColorU32(ImVec4(0.05f, 0.06f, 0.07f, 1.0f)),
					2.0f);
			}
		}
	}

	FVector2 SelectionMin;
	FVector2 SelectionMax;
	if (HasMultiSelection() && GetSelectionBounds(SelectionMin, SelectionMax))
	{
		const ImVec2 Min(CanvasMin.x + SelectionMin.X * Scale, CanvasMin.y + SelectionMin.Y * Scale);
		const ImVec2 Max(CanvasMin.x + SelectionMax.X * Scale, CanvasMin.y + SelectionMax.Y * Scale);
		DrawList->AddRect(
			Min,
			Max,
			ImGui::GetColorU32(ImVec4(1.0f, 0.74f, 0.22f, 0.85f)),
			0.0f,
			0,
			2.0f);
		DrawList->AddText(
			ImVec2(Min.x + 6.0f, Min.y - 18.0f),
			ImGui::GetColorU32(ImVec4(1.0f, 0.84f, 0.34f, 0.95f)),
			"Selection");
	}

	for (const FDesignerGuideLine& Guide : ActiveGuideLines)
	{
		const ImU32 GuideColor = ImGui::GetColorU32(ImVec4(0.2f, 0.85f, 1.0f, 0.9f));
		if (Guide.bVertical)
		{
			const float X = CanvasMin.x + Guide.Position * Scale;
			DrawList->AddLine(
				ImVec2(X, CanvasMin.y + Guide.Min * Scale),
				ImVec2(X, CanvasMin.y + Guide.Max * Scale),
				GuideColor,
				1.5f);
		}
		else
		{
			const float Y = CanvasMin.y + Guide.Position * Scale;
			DrawList->AddLine(
				ImVec2(CanvasMin.x + Guide.Min * Scale, Y),
				ImVec2(CanvasMin.x + Guide.Max * Scale, Y),
				GuideColor,
				1.5f);
		}
	}
}

void FEditorRuntimeUIPreviewWidget::DrawDocumentInfo() const
{
	int32 TargetWidth = 1920;
	int32 TargetHeight = 1080;
	GetPreviewResolution(ResolutionPresetIndex, CustomWidth, CustomHeight, TargetWidth, TargetHeight);

	ImGui::Text("Preview");
	ImGui::Separator();
	ImGui::TextDisabled("Document");
	ImGui::TextWrapped("%s", PreviewDocumentPathBuffer);
	ImGui::TextDisabled("Screen Id");
	ImGui::TextWrapped("%s", PreviewScreenIdBuffer);
	ImGui::TextDisabled("Layout");
	ImGui::Text("%d x %d", TargetWidth, TargetHeight);
	ImGui::TextDisabled("Status");
	ImGui::TextUnformatted(bPreviewDocumentLoaded ? "Loaded" : "Not loaded");
	ImGui::Spacing();
}

void FEditorRuntimeUIPreviewWidget::DrawActionEvents()
{
	ImGui::Text("RmlUi Action Events");
	ImGui::Separator();
	ImGui::TextDisabled("Authored");
	bool bHasAuthoredActions = false;
	const TArray<FRuntimeUIWidgetNode>& Widgets = LayoutAsset.GetWidgets();
	for (const FRuntimeUIWidgetNode& Widget : Widgets)
	{
		if (Widget.OnClickAction.empty())
		{
			continue;
		}

		bHasAuthoredActions = true;
		const FString Label = Widget.Id.empty() ? Widget.DisplayName : Widget.Id;
		ImGui::BulletText("%s -> %s", Label.c_str(), Widget.OnClickAction.c_str());
		if (ImGui::IsItemClicked())
		{
			ImGui::SetClipboardText(Widget.OnClickAction.c_str());
		}
	}
	if (!bHasAuthoredActions)
	{
		ImGui::TextDisabled("No authored actions.");
	}
	ImGui::Spacing();
	ImGui::TextDisabled("Preview events");
	if (PreviewActionEvents.empty())
	{
		ImGui::TextDisabled("No events yet.");
		return;
	}

	if (ImGui::Button("Copy Last##RuntimeUICopyLastActionEvent", ImVec2(82.0f, 0.0f)))
	{
		ImGui::SetClipboardText(PreviewActionEvents.back().c_str());
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear##RuntimeUIClearActionEvents", ImVec2(58.0f, 0.0f)))
	{
		PreviewActionEvents.clear();
		return;
	}

	for (auto It = PreviewActionEvents.rbegin(); It != PreviewActionEvents.rend(); ++It)
	{
		ImGui::BulletText("%s", It->c_str());
	}
}

void FEditorRuntimeUIPreviewWidget::DrawAuthoringGuidance() const
{
	if (!bShowGuidance)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::Text("RML Preview Rule");
	ImGui::Separator();
	ImGui::TextWrapped("Load an .rml document under Asset/UI. Linked .rcss files are resolved by the RmlUi file interface.");
	ImGui::Spacing();
	ImGui::TextDisabled("Action event");
	ImGui::TextWrapped("<button id=\"StartButton\" data-action=\"StartGame\">START</button>");
	ImGui::Spacing();
	ImGui::TextDisabled("Lua runtime");
	ImGui::TextWrapped("Engine.API.UI.LoadDocument(\"Title\", \"Asset/UI/Title/Title.rml\")");
	ImGui::TextWrapped("Engine.API.UI.ShowDocument(\"Title\")");
	ImGui::TextWrapped("Engine.API.UI.SetPosition(\"StartButton\", 100, 80)");
	ImGui::TextWrapped("Engine.API.UI.SetSize(\"StartButton\", 240, 64)");
	ImGui::TextWrapped("Engine.API.UI.DispatchActionEvents({ StartGame = function() end })");
}

bool FEditorRuntimeUIPreviewWidget::LoadPreviewDocument()
{
	if (!EditorEngine)
	{
		return false;
	}

	const FString ScreenId = PreviewScreenIdBuffer;
	FString Path;
	if (!NormalizeRmlPath(PreviewDocumentPathBuffer, Path))
	{
		bPreviewDocumentLoaded = false;
		return false;
	}
	strncpy_s(PreviewDocumentPathBuffer, Path.c_str(), _TRUNCATE);
	bPreviewDocumentLoaded = EditorEngine->GetRmlUiSystem().LoadDocument(ScreenId, Path);
	if (bPreviewDocumentLoaded)
	{
		EditorEngine->GetRmlUiSystem().ShowScreen(ScreenId);
	}
	return bPreviewDocumentLoaded;
}

bool FEditorRuntimeUIPreviewWidget::OpenRmlFileDialog(FString& OutPath) const
{
	OutPath.clear();

	WCHAR FileBuffer[MAX_PATH] = {};
	OPENFILENAMEW DialogDesc = {};
	DialogDesc.lStructSize = sizeof(DialogDesc);
	DialogDesc.hwndOwner = ImGui::GetMainViewport()
		? static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw)
		: nullptr;
	DialogDesc.lpstrFilter = L"RML Files (*.rml)\0*.rml\0All Files (*.*)\0*.*\0";
	DialogDesc.lpstrFile = FileBuffer;
	DialogDesc.nMaxFile = MAX_PATH;
	DialogDesc.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	const std::filesystem::path InitialDir = (std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"UI").lexically_normal();
	const std::wstring InitialDirText = InitialDir.wstring();
	DialogDesc.lpstrInitialDir = InitialDirText.c_str();

	const std::filesystem::path PrevCwd = std::filesystem::current_path();
	const BOOL bPicked = GetOpenFileNameW(&DialogDesc);
	std::error_code RestoreEc;
	std::filesystem::current_path(PrevCwd, RestoreEc);
	if (!bPicked)
	{
		return false;
	}

	return NormalizeRmlPath(FPaths::ToUtf8(std::wstring(FileBuffer)), OutPath);
}

bool FEditorRuntimeUIPreviewWidget::SetPreviewDocumentPath(const FString& Path)
{
	FString NormalizedPath;
	if (!NormalizeRmlPath(Path, NormalizedPath))
	{
		return false;
	}

	strncpy_s(PreviewDocumentPathBuffer, NormalizedPath.c_str(), _TRUNCATE);
	return true;
}

bool FEditorRuntimeUIPreviewWidget::AcceptRmlDragDropTarget()
{
	if (!ImGui::BeginDragDropTarget())
	{
		return false;
	}

	bool bAccepted = false;
	const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("RMLContentItem");
	if (!Payload)
	{
		Payload = ImGui::AcceptDragDropPayload("ContentBrowserPath");
	}
	if (Payload && Payload->Data && Payload->DataSize > 0)
	{
		const FString Path(static_cast<const char*>(Payload->Data));
		if (SetPreviewDocumentPath(Path))
		{
			RefreshPreviewDocument();
			bAccepted = true;
		}
	}

	ImGui::EndDragDropTarget();
	return bAccepted;
}

bool FEditorRuntimeUIPreviewWidget::AcceptLayoutDragDropTarget()
{
	if (!ImGui::BeginDragDropTarget())
	{
		return false;
	}

	bool bAccepted = false;
	const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("RuntimeUILayoutContentItem");
	if (Payload && Payload->Data && Payload->DataSize > 0)
	{
		const FString Path(static_cast<const char*>(Payload->Data));
		if (IsLayoutAssetPath(Path) && OpenLayoutAsset(Path))
		{
			bAccepted = true;
		}
	}

	ImGui::EndDragDropTarget();
	return bAccepted;
}

void FEditorRuntimeUIPreviewWidget::SelectWidgetAtCanvasPosition(
	const ImVec2& CanvasPosition,
	float Scale,
	bool bToggleSelection,
	bool bAddSelection)
{
	const int32 HitIndex = HitTestWidgetAtCanvasPosition(CanvasPosition, Scale);
	if (bToggleSelection)
	{
		ToggleWidgetSelection(HitIndex);
		return;
	}
	if (bAddSelection)
	{
		AddWidgetSelection(HitIndex);
		return;
	}
	if (!IsWidgetSelected(HitIndex) || HitIndex == 0)
	{
		SelectSingleWidget(HitIndex);
	}
}

int32 FEditorRuntimeUIPreviewWidget::HitTestWidgetAtCanvasPosition(const ImVec2& CanvasPosition, float Scale) const
{
	if (Scale <= 0.0f)
	{
		return 0;
	}
	const FVector2 Point(CanvasPosition.x / Scale, CanvasPosition.y / Scale);
	const TArray<FRuntimeUIWidgetNode>& Widgets = LayoutAsset.GetWidgets();
	for (int32 Index = static_cast<int32>(Widgets.size()) - 1; Index >= 0; --Index)
	{
		const FRuntimeUIWidgetNode& Node = Widgets[Index];
		if (!Node.bVisible)
		{
			continue;
		}
		FVector2 Min;
		FVector2 Size;
		if (!GetWidgetResolvedRect(Index, Min, Size))
		{
			continue;
		}
		const FVector2 Max(Min.X + Size.X, Min.Y + Size.Y);
		if (Point.X >= Min.X && Point.X <= Max.X && Point.Y >= Min.Y && Point.Y <= Max.Y)
		{
			return Index;
		}
	}
	return 0;
}

void FEditorRuntimeUIPreviewWidget::SelectSingleWidget(int32 WidgetIndex)
{
	if (!LayoutAsset.GetWidget(WidgetIndex))
	{
		WidgetIndex = 0;
	}
	SelectedWidgetIndex = WidgetIndex;
	SelectedWidgetIndices.clear();
	SelectedWidgetIndices.push_back(WidgetIndex);
}

void FEditorRuntimeUIPreviewWidget::ToggleWidgetSelection(int32 WidgetIndex)
{
	if (!LayoutAsset.GetWidget(WidgetIndex))
	{
		return;
	}
	if (WidgetIndex == 0)
	{
		SelectSingleWidget(0);
		return;
	}

	auto Found = std::find(SelectedWidgetIndices.begin(), SelectedWidgetIndices.end(), WidgetIndex);
	if (Found != SelectedWidgetIndices.end())
	{
		SelectedWidgetIndices.erase(Found);
		if (SelectedWidgetIndex == WidgetIndex)
		{
			SelectedWidgetIndex = SelectedWidgetIndices.empty() ? 0 : SelectedWidgetIndices.back();
		}
		if (SelectedWidgetIndices.empty())
		{
			SelectedWidgetIndices.push_back(0);
			SelectedWidgetIndex = 0;
		}
		return;
	}

	SelectedWidgetIndices.erase(std::remove(SelectedWidgetIndices.begin(), SelectedWidgetIndices.end(), 0), SelectedWidgetIndices.end());
	SelectedWidgetIndices.push_back(WidgetIndex);
	SelectedWidgetIndex = WidgetIndex;
}

void FEditorRuntimeUIPreviewWidget::AddWidgetSelection(int32 WidgetIndex)
{
	if (!LayoutAsset.GetWidget(WidgetIndex))
	{
		return;
	}
	if (WidgetIndex == 0)
	{
		SelectSingleWidget(0);
		return;
	}
	if (std::find(SelectedWidgetIndices.begin(), SelectedWidgetIndices.end(), WidgetIndex) == SelectedWidgetIndices.end())
	{
		SelectedWidgetIndices.erase(std::remove(SelectedWidgetIndices.begin(), SelectedWidgetIndices.end(), 0), SelectedWidgetIndices.end());
		SelectedWidgetIndices.push_back(WidgetIndex);
	}
	SelectedWidgetIndex = WidgetIndex;
}

void FEditorRuntimeUIPreviewWidget::SelectAllWidgets()
{
	SelectedWidgetIndices.clear();
	const TArray<FRuntimeUIWidgetNode>& Widgets = LayoutAsset.GetWidgets();
	for (int32 Index = 1; Index < static_cast<int32>(Widgets.size()); ++Index)
	{
		if (!Widgets[Index].bLocked)
		{
			SelectedWidgetIndices.push_back(Index);
		}
	}
	if (SelectedWidgetIndices.empty())
	{
		SelectedWidgetIndices.push_back(0);
		SelectedWidgetIndex = 0;
	}
	else
	{
		SelectedWidgetIndex = SelectedWidgetIndices.back();
	}
}

void FEditorRuntimeUIPreviewWidget::ClearWidgetSelection()
{
	SelectSingleWidget(0);
}

void FEditorRuntimeUIPreviewWidget::NormalizeWidgetSelection()
{
	TArray<int32> Normalized;
	for (const int32 Index : SelectedWidgetIndices)
	{
		if (!LayoutAsset.GetWidget(Index))
		{
			continue;
		}
		if (std::find(Normalized.begin(), Normalized.end(), Index) == Normalized.end())
		{
			Normalized.push_back(Index);
		}
	}

	if (Normalized.empty())
	{
		Normalized.push_back(0);
		SelectedWidgetIndex = 0;
	}
	else if (std::find(Normalized.begin(), Normalized.end(), SelectedWidgetIndex) == Normalized.end())
	{
		SelectedWidgetIndex = Normalized.back();
	}
	SelectedWidgetIndices = std::move(Normalized);
}

bool FEditorRuntimeUIPreviewWidget::IsWidgetSelected(int32 WidgetIndex) const
{
	return std::find(SelectedWidgetIndices.begin(), SelectedWidgetIndices.end(), WidgetIndex) != SelectedWidgetIndices.end();
}

bool FEditorRuntimeUIPreviewWidget::HasMultiSelection() const
{
	int32 EditableCount = 0;
	for (const int32 Index : SelectedWidgetIndices)
	{
		if (Index != 0 && LayoutAsset.GetWidget(Index))
		{
			++EditableCount;
		}
	}
	return EditableCount > 1;
}

TArray<int32> FEditorRuntimeUIPreviewWidget::GetEditableSelectedWidgets() const
{
	TArray<int32> Result;
	for (const int32 Index : SelectedWidgetIndices)
	{
		const FRuntimeUIWidgetNode* Node = LayoutAsset.GetWidget(Index);
		if (!Node || Index == 0 || Node->bLocked)
		{
			continue;
		}
		if (std::find(Result.begin(), Result.end(), Index) == Result.end())
		{
			Result.push_back(Index);
		}
	}
	return Result;
}

bool FEditorRuntimeUIPreviewWidget::GetSelectionBounds(FVector2& OutMin, FVector2& OutMax) const
{
	bool bHasBounds = false;
	for (const int32 Index : SelectedWidgetIndices)
	{
		if (Index == 0)
		{
			continue;
		}
		FVector2 Position;
		FVector2 Size;
		if (!GetWidgetResolvedRect(Index, Position, Size))
		{
			continue;
		}
		const FVector2 Max(Position.X + Size.X, Position.Y + Size.Y);
		if (!bHasBounds)
		{
			OutMin = Position;
			OutMax = Max;
			bHasBounds = true;
		}
		else
		{
			OutMin.X = std::min(OutMin.X, Position.X);
			OutMin.Y = std::min(OutMin.Y, Position.Y);
			OutMax.X = std::max(OutMax.X, Max.X);
			OutMax.Y = std::max(OutMax.Y, Max.Y);
		}
	}
	return bHasBounds;
}

void FEditorRuntimeUIPreviewWidget::DeleteSelectedWidgets()
{
	TArray<int32> Targets = GetEditableSelectedWidgets();
	std::sort(Targets.begin(), Targets.end(), std::greater<int32>());
	for (const int32 Index : Targets)
	{
		LayoutAsset.RemoveWidget(Index);
	}
	ClearWidgetSelection();
}

void FEditorRuntimeUIPreviewWidget::DuplicateSelectedWidgets()
{
	TArray<int32> Targets = GetEditableSelectedWidgets();
	TArray<int32> Duplicated;
	for (const int32 Index : Targets)
	{
		const int32 DuplicatedIndex = LayoutAsset.DuplicateWidget(Index);
		if (DuplicatedIndex >= 0)
		{
			Duplicated.push_back(DuplicatedIndex);
		}
	}
	if (!Duplicated.empty())
	{
		SelectedWidgetIndices = Duplicated;
		SelectedWidgetIndex = Duplicated.back();
	}
}

void FEditorRuntimeUIPreviewWidget::MoveSelectedWidgets(const FVector2& Delta)
{
	for (const int32 Index : GetEditableSelectedWidgets())
	{
		FRuntimeUIWidgetNode* Node = LayoutAsset.GetMutableWidget(Index);
		const FRuntimeUIWidgetNode* ParentNode = Node ? LayoutAsset.GetWidget(Node->ParentIndex) : nullptr;
		if (!Node || (ParentNode && IsRuntimeUILayoutContainer(*ParentNode)))
		{
			continue;
		}
		Node->Position = SnapPosition(Node->Position + Delta);
	}
}

void FEditorRuntimeUIPreviewWidget::AlignSelectedWidgetsToSelection(char Axis, float Factor)
{
	FVector2 Min;
	FVector2 Max;
	if (!GetSelectionBounds(Min, Max))
	{
		return;
	}
	for (const int32 Index : GetEditableSelectedWidgets())
	{
		FRuntimeUIWidgetNode* Node = LayoutAsset.GetMutableWidget(Index);
		const FRuntimeUIWidgetNode* ParentNode = Node ? LayoutAsset.GetWidget(Node->ParentIndex) : nullptr;
		if (!Node || (ParentNode && IsRuntimeUILayoutContainer(*ParentNode)))
		{
			continue;
		}
		FVector2 Absolute;
		FVector2 Size;
		if (!GetWidgetResolvedRect(Index, Absolute, Size))
		{
			continue;
		}
		if (Axis == 'x')
		{
			const float Target = Min.X + (Max.X - Min.X - Size.X) * Factor;
			Node->Position.X = SnapPosition(FVector2(Node->Position.X + Target - Absolute.X, Node->Position.Y)).X;
		}
		else
		{
			const float Target = Min.Y + (Max.Y - Min.Y - Size.Y) * Factor;
			Node->Position.Y = SnapPosition(FVector2(Node->Position.X, Node->Position.Y + Target - Absolute.Y)).Y;
		}
	}
}

void FEditorRuntimeUIPreviewWidget::DistributeSelectedWidgets(bool bHorizontal)
{
	TArray<int32> Targets = GetEditableSelectedWidgets();
	if (Targets.size() < 3)
	{
		return;
	}

	std::sort(Targets.begin(), Targets.end(), [this, bHorizontal](int32 A, int32 B)
	{
		FVector2 PosA;
		FVector2 SizeA;
		FVector2 PosB;
		FVector2 SizeB;
		GetWidgetResolvedRect(A, PosA, SizeA);
		GetWidgetResolvedRect(B, PosB, SizeB);
		return bHorizontal ? PosA.X < PosB.X : PosA.Y < PosB.Y;
	});

	FVector2 FirstPos;
	FVector2 FirstSize;
	FVector2 LastPos;
	FVector2 LastSize;
	if (!GetWidgetResolvedRect(Targets.front(), FirstPos, FirstSize) || !GetWidgetResolvedRect(Targets.back(), LastPos, LastSize))
	{
		return;
	}

	float TotalSize = 0.0f;
	for (const int32 Index : Targets)
	{
		FVector2 Pos;
		FVector2 Size;
		if (GetWidgetResolvedRect(Index, Pos, Size))
		{
			TotalSize += bHorizontal ? Size.X : Size.Y;
		}
	}

	const float Start = bHorizontal ? FirstPos.X : FirstPos.Y;
	const float End = bHorizontal ? (LastPos.X + LastSize.X) : (LastPos.Y + LastSize.Y);
	const float Gap = (End - Start - TotalSize) / static_cast<float>(Targets.size() - 1);
	float Cursor = Start;
	for (const int32 Index : Targets)
	{
		FRuntimeUIWidgetNode* Node = LayoutAsset.GetMutableWidget(Index);
		const FRuntimeUIWidgetNode* ParentNode = Node ? LayoutAsset.GetWidget(Node->ParentIndex) : nullptr;
		if (!Node || (ParentNode && IsRuntimeUILayoutContainer(*ParentNode)))
		{
			continue;
		}

		FVector2 Absolute;
		FVector2 Size;
		if (!GetWidgetResolvedRect(Index, Absolute, Size))
		{
			continue;
		}
		if (bHorizontal)
		{
			Node->Position.X = SnapPosition(FVector2(Node->Position.X + Cursor - Absolute.X, Node->Position.Y)).X;
			Cursor += Size.X + Gap;
		}
		else
		{
			Node->Position.Y = SnapPosition(FVector2(Node->Position.X, Node->Position.Y + Cursor - Absolute.Y)).Y;
			Cursor += Size.Y + Gap;
		}
	}
}

void FEditorRuntimeUIPreviewWidget::WrapSelectedWidgetsInPanel()
{
	TArray<int32> Targets = GetEditableSelectedWidgets();
	if (Targets.empty())
	{
		return;
	}

	FVector2 Min;
	FVector2 Max;
	if (!GetSelectionBounds(Min, Max))
	{
		return;
	}

	int32 ParentIndex = -1;
	bool bSameParent = true;
	for (const int32 Index : Targets)
	{
		const FRuntimeUIWidgetNode* Node = LayoutAsset.GetWidget(Index);
		if (!Node)
		{
			continue;
		}
		if (ParentIndex < 0)
		{
			ParentIndex = Node->ParentIndex;
		}
		else if (ParentIndex != Node->ParentIndex)
		{
			bSameParent = false;
			break;
		}
	}
	if (!bSameParent || !LayoutAsset.GetWidget(ParentIndex))
	{
		ParentIndex = 0;
	}

	FVector2 ParentAbsolute;
	FVector2 ParentSize;
	if (!GetWidgetResolvedRect(ParentIndex, ParentAbsolute, ParentSize))
	{
		ParentAbsolute = FVector2(0.0f, 0.0f);
	}

	const int32 PanelIndex = LayoutAsset.AddWidget(ERuntimeUIWidgetType::Panel, ParentIndex);
	FRuntimeUIWidgetNode* Panel = LayoutAsset.GetMutableWidget(PanelIndex);
	if (!Panel)
	{
		return;
	}

	Panel->DisplayName = "Group Panel";
	Panel->Position = SnapPosition(Min - ParentAbsolute);
	Panel->Size = SnapSize(FVector2(std::max(8.0f, Max.X - Min.X), std::max(8.0f, Max.Y - Min.Y)));
	Panel->LayoutMode = ERuntimeUILayoutMode::Free;
	ApplyRuntimeUIStylePreset(*Panel, "Panel");

	for (const int32 Index : Targets)
	{
		FVector2 Absolute;
		FVector2 Size;
		if (!GetWidgetResolvedRect(Index, Absolute, Size))
		{
			continue;
		}
		if (LayoutAsset.SetWidgetParent(Index, PanelIndex))
		{
			if (FRuntimeUIWidgetNode* Node = LayoutAsset.GetMutableWidget(Index))
			{
				Node->AnchorMin = FVector2(0.0f, 0.0f);
				Node->AnchorMax = FVector2(0.0f, 0.0f);
				Node->Position = SnapPosition(Absolute - Min);
				Node->Size = Size;
			}
		}
	}
	SelectSingleWidget(PanelIndex);
}

FVector2 FEditorRuntimeUIPreviewWidget::SnapWidgetMovePosition(int32 WidgetIndex, const FVector2& DesiredPosition)
{
	FVector2 SnappedPosition = SnapPosition(DesiredPosition);
	ActiveGuideLines.clear();
	if (!bSmartGuides)
	{
		return SnappedPosition;
	}

	const FRuntimeUIWidgetNode* Node = LayoutAsset.GetWidget(WidgetIndex);
	if (!Node)
	{
		return SnappedPosition;
	}
	FVector2 CurrentAbsolute;
	FVector2 Size;
	if (!GetWidgetResolvedRect(WidgetIndex, CurrentAbsolute, Size))
	{
		return SnappedPosition;
	}
	const FVector2 Delta = SnappedPosition - Node->Position;
	FVector2 MovedMin = CurrentAbsolute + Delta;
	FVector2 MovedMax(MovedMin.X + Size.X, MovedMin.Y + Size.Y);
	const FVector2 MovedCenter((MovedMin.X + MovedMax.X) * 0.5f, (MovedMin.Y + MovedMax.Y) * 0.5f);

	float BestX = 7.0f;
	float BestY = 7.0f;
	float OffsetX = 0.0f;
	float OffsetY = 0.0f;
	auto TestX = [&](float Moving, float Target, float LineMin, float LineMax)
	{
		const float Distance = std::abs(Moving - Target);
		if (Distance < BestX)
		{
			BestX = Distance;
			OffsetX = Target - Moving;
			ActiveGuideLines.push_back({ true, Target, LineMin, LineMax });
		}
	};
	auto TestY = [&](float Moving, float Target, float LineMin, float LineMax)
	{
		const float Distance = std::abs(Moving - Target);
		if (Distance < BestY)
		{
			BestY = Distance;
			OffsetY = Target - Moving;
			ActiveGuideLines.push_back({ false, Target, LineMin, LineMax });
		}
	};

	const TArray<FRuntimeUIWidgetNode>& Widgets = LayoutAsset.GetWidgets();
	for (int32 Index = 0; Index < static_cast<int32>(Widgets.size()); ++Index)
	{
		if (Index == WidgetIndex || IsWidgetSelected(Index))
		{
			continue;
		}
		FVector2 TargetMin;
		FVector2 TargetSize;
		if (!GetWidgetResolvedRect(Index, TargetMin, TargetSize))
		{
			continue;
		}
		const FVector2 TargetMax(TargetMin.X + TargetSize.X, TargetMin.Y + TargetSize.Y);
		const FVector2 TargetCenter((TargetMin.X + TargetMax.X) * 0.5f, (TargetMin.Y + TargetMax.Y) * 0.5f);
		const float LineMinY = std::min(MovedMin.Y, TargetMin.Y);
		const float LineMaxY = std::max(MovedMax.Y, TargetMax.Y);
		const float LineMinX = std::min(MovedMin.X, TargetMin.X);
		const float LineMaxX = std::max(MovedMax.X, TargetMax.X);
		TestX(MovedMin.X, TargetMin.X, LineMinY, LineMaxY);
		TestX(MovedCenter.X, TargetCenter.X, LineMinY, LineMaxY);
		TestX(MovedMax.X, TargetMax.X, LineMinY, LineMaxY);
		TestY(MovedMin.Y, TargetMin.Y, LineMinX, LineMaxX);
		TestY(MovedCenter.Y, TargetCenter.Y, LineMinX, LineMaxX);
		TestY(MovedMax.Y, TargetMax.Y, LineMinX, LineMaxX);
	}

	if (BestX <= 6.0f)
	{
		SnappedPosition.X += OffsetX;
	}
	if (BestY <= 6.0f)
	{
		SnappedPosition.Y += OffsetY;
	}
	return SnappedPosition;
}

FVector2 FEditorRuntimeUIPreviewWidget::SnapSelectionMoveDelta(const FVector2& DesiredDelta)
{
	ActiveGuideLines.clear();
	if (!bSmartGuides)
	{
		return DesiredDelta;
	}
	FVector2 Min;
	FVector2 Max;
	if (!GetSelectionBounds(Min, Max))
	{
		return DesiredDelta;
	}

	FVector2 MovedMin = Min + DesiredDelta;
	FVector2 MovedMax = Max + DesiredDelta;
	const FVector2 MovedCenter((MovedMin.X + MovedMax.X) * 0.5f, (MovedMin.Y + MovedMax.Y) * 0.5f);

	float BestX = 7.0f;
	float BestY = 7.0f;
	float OffsetX = 0.0f;
	float OffsetY = 0.0f;
	auto TestX = [&](float Moving, float Target, float LineMin, float LineMax)
	{
		const float Distance = std::abs(Moving - Target);
		if (Distance < BestX)
		{
			BestX = Distance;
			OffsetX = Target - Moving;
			ActiveGuideLines.push_back({ true, Target, LineMin, LineMax });
		}
	};
	auto TestY = [&](float Moving, float Target, float LineMin, float LineMax)
	{
		const float Distance = std::abs(Moving - Target);
		if (Distance < BestY)
		{
			BestY = Distance;
			OffsetY = Target - Moving;
			ActiveGuideLines.push_back({ false, Target, LineMin, LineMax });
		}
	};

	const TArray<FRuntimeUIWidgetNode>& Widgets = LayoutAsset.GetWidgets();
	for (int32 Index = 0; Index < static_cast<int32>(Widgets.size()); ++Index)
	{
		if (IsWidgetSelected(Index))
		{
			continue;
		}
		FVector2 TargetMin;
		FVector2 TargetSize;
		if (!GetWidgetResolvedRect(Index, TargetMin, TargetSize))
		{
			continue;
		}
		const FVector2 TargetMax(TargetMin.X + TargetSize.X, TargetMin.Y + TargetSize.Y);
		const FVector2 TargetCenter((TargetMin.X + TargetMax.X) * 0.5f, (TargetMin.Y + TargetMax.Y) * 0.5f);
		const float LineMinY = std::min(MovedMin.Y, TargetMin.Y);
		const float LineMaxY = std::max(MovedMax.Y, TargetMax.Y);
		const float LineMinX = std::min(MovedMin.X, TargetMin.X);
		const float LineMaxX = std::max(MovedMax.X, TargetMax.X);
		TestX(MovedMin.X, TargetMin.X, LineMinY, LineMaxY);
		TestX(MovedCenter.X, TargetCenter.X, LineMinY, LineMaxY);
		TestX(MovedMax.X, TargetMax.X, LineMinY, LineMaxY);
		TestY(MovedMin.Y, TargetMin.Y, LineMinX, LineMaxX);
		TestY(MovedCenter.Y, TargetCenter.Y, LineMinX, LineMaxX);
		TestY(MovedMax.Y, TargetMax.Y, LineMinX, LineMaxX);
	}

	FVector2 Result = DesiredDelta;
	if (BestX <= 6.0f)
	{
		Result.X += OffsetX;
	}
	if (BestY <= 6.0f)
	{
		Result.Y += OffsetY;
	}
	return Result;
}

FVector2 FEditorRuntimeUIPreviewWidget::GetDesignerCanvasSize() const
{
	int32 TargetWidth = 1920;
	int32 TargetHeight = 1080;
	GetPreviewResolution(ResolutionPresetIndex, CustomWidth, CustomHeight, TargetWidth, TargetHeight);
	return FVector2(static_cast<float>(TargetWidth), static_cast<float>(TargetHeight));
}

FVector2 FEditorRuntimeUIPreviewWidget::GetWidgetAuthoringParentSize(int32 WidgetIndex) const
{
	const FRuntimeUIWidgetNode* Node = LayoutAsset.GetWidget(WidgetIndex);
	if (!Node)
	{
		return GetDesignerCanvasSize();
	}

	if (const FRuntimeUIWidgetNode* ParentNode = LayoutAsset.GetWidget(Node->ParentIndex))
	{
		return ParentNode->Size;
	}
	return Node->Size;
}

bool FEditorRuntimeUIPreviewWidget::GetWidgetResolvedRect(int32 WidgetIndex, FVector2& OutPosition, FVector2& OutSize) const
{
	const FRuntimeUIWidgetNode* Node = LayoutAsset.GetWidget(WidgetIndex);
	if (!Node || !Node->bVisible)
	{
		return false;
	}

	if (WidgetIndex == 0 || Node->Type == ERuntimeUIWidgetType::Canvas)
	{
		OutPosition = FVector2(0.0f, 0.0f);
		OutSize = GetDesignerCanvasSize();
		return true;
	}

	FVector2 ParentPosition;
	FVector2 ParentSize;
	if (!GetWidgetResolvedRect(Node->ParentIndex, ParentPosition, ParentSize))
	{
		return false;
	}

	if (const FRuntimeUIWidgetNode* ParentNode = LayoutAsset.GetWidget(Node->ParentIndex))
	{
		if (IsRuntimeUILayoutContainer(*ParentNode))
		{
			const float ContentLeft = ParentPosition.X + ParentNode->LayoutPadding.X;
			const float ContentTop = ParentPosition.Y + ParentNode->LayoutPadding.Y;
			const float ContentWidth = std::max(1.0f, ParentSize.X - ParentNode->LayoutPadding.X - ParentNode->LayoutPadding.Z);
			const float ContentHeight = std::max(1.0f, ParentSize.Y - ParentNode->LayoutPadding.Y - ParentNode->LayoutPadding.W);
			const bool bHorizontal = ParentNode->LayoutMode == ERuntimeUILayoutMode::Horizontal;
			int32 VisibleChildCount = 0;
			float FixedMainSize = 0.0f;
			float TotalFillWeight = 0.0f;
			for (const int32 ChildIndex : ParentNode->Children)
			{
				const FRuntimeUIWidgetNode* ChildNode = LayoutAsset.GetWidget(ChildIndex);
				if (!ChildNode || !ChildNode->bVisible)
				{
					continue;
				}

				++VisibleChildCount;
				if (ChildNode->LayoutSizeRule == ERuntimeUILayoutSizeRule::Fill)
				{
					TotalFillWeight += std::max(0.01f, ChildNode->LayoutFillWeight);
				}
				else
				{
					FixedMainSize += bHorizontal ? ChildNode->Size.X : ChildNode->Size.Y;
				}
			}
			const float TotalGap = std::max(0, VisibleChildCount - 1) * ParentNode->LayoutGap;
			const float AvailableMainSize = bHorizontal ? ContentWidth : ContentHeight;
			const float FillMainSize = std::max(1.0f, AvailableMainSize - FixedMainSize - TotalGap);
			float Cursor = ParentNode->LayoutMode == ERuntimeUILayoutMode::Horizontal ? ContentLeft : ContentTop;
			int32 VisibleChildOrdinal = 0;

			for (const int32 ChildIndex : ParentNode->Children)
			{
				const FRuntimeUIWidgetNode* ChildNode = LayoutAsset.GetWidget(ChildIndex);
				if (!ChildNode || !ChildNode->bVisible)
				{
					continue;
				}

				FVector2 ChildSize = ChildNode->Size;
				FVector2 ChildPosition(0.0f, 0.0f);
				const bool bFill = ChildNode->LayoutSizeRule == ERuntimeUILayoutSizeRule::Fill && TotalFillWeight > 0.0f;
				if (ParentNode->LayoutMode == ERuntimeUILayoutMode::Horizontal)
				{
					ChildPosition.X = Cursor;
					if (bFill)
					{
						ChildSize.X = FillMainSize * (std::max(0.01f, ChildNode->LayoutFillWeight) / TotalFillWeight);
					}
					ChildSize.Y = ParentNode->LayoutAlignment == ERuntimeUILayoutAlignment::Stretch ? ContentHeight : ChildSize.Y;
					if (ParentNode->LayoutAlignment == ERuntimeUILayoutAlignment::Center)
					{
						ChildPosition.Y = ContentTop + (ContentHeight - ChildSize.Y) * 0.5f;
					}
					else if (ParentNode->LayoutAlignment == ERuntimeUILayoutAlignment::End)
					{
						ChildPosition.Y = ContentTop + ContentHeight - ChildSize.Y;
					}
					else
					{
						ChildPosition.Y = ContentTop;
					}
					Cursor += ChildSize.X;
				}
				else
				{
					ChildPosition.Y = Cursor;
					if (bFill)
					{
						ChildSize.Y = FillMainSize * (std::max(0.01f, ChildNode->LayoutFillWeight) / TotalFillWeight);
					}
					ChildSize.X = ParentNode->LayoutAlignment == ERuntimeUILayoutAlignment::Stretch ? ContentWidth : ChildSize.X;
					if (ParentNode->LayoutAlignment == ERuntimeUILayoutAlignment::Center)
					{
						ChildPosition.X = ContentLeft + (ContentWidth - ChildSize.X) * 0.5f;
					}
					else if (ParentNode->LayoutAlignment == ERuntimeUILayoutAlignment::End)
					{
						ChildPosition.X = ContentLeft + ContentWidth - ChildSize.X;
					}
					else
					{
						ChildPosition.X = ContentLeft;
					}
					Cursor += ChildSize.Y;
				}
				++VisibleChildOrdinal;
				if (VisibleChildOrdinal < VisibleChildCount)
				{
					Cursor += ParentNode->LayoutGap;
				}

				if (ChildIndex == WidgetIndex)
				{
					OutPosition = ChildPosition;
					OutSize = ChildSize;
					return true;
				}
			}
		}
	}

	const FVector2 AuthoringParentSize = GetWidgetAuthoringParentSize(WidgetIndex);
	const float LeftOffset = Node->Position.X - AuthoringParentSize.X * Node->AnchorMin.X;
	const float TopOffset = Node->Position.Y - AuthoringParentSize.Y * Node->AnchorMin.Y;
	const float RightOffset = AuthoringParentSize.X * Node->AnchorMax.X - (Node->Position.X + Node->Size.X);
	const float BottomOffset = AuthoringParentSize.Y * Node->AnchorMax.Y - (Node->Position.Y + Node->Size.Y);
	const bool bStretchX = std::abs(Node->AnchorMax.X - Node->AnchorMin.X) > 0.0001f;
	const bool bStretchY = std::abs(Node->AnchorMax.Y - Node->AnchorMin.Y) > 0.0001f;

	OutPosition.X = ParentPosition.X + ParentSize.X * Node->AnchorMin.X + LeftOffset;
	OutPosition.Y = ParentPosition.Y + ParentSize.Y * Node->AnchorMin.Y + TopOffset;

	if (bStretchX)
	{
		const float Right = ParentPosition.X + ParentSize.X * Node->AnchorMax.X - RightOffset;
		OutSize.X = std::max(1.0f, Right - OutPosition.X);
	}
	else
	{
		OutSize.X = Node->Size.X;
	}

	if (bStretchY)
	{
		const float Bottom = ParentPosition.Y + ParentSize.Y * Node->AnchorMax.Y - BottomOffset;
		OutSize.Y = std::max(1.0f, Bottom - OutPosition.Y);
	}
	else
	{
		OutSize.Y = Node->Size.Y;
	}
	return true;
}

FVector2 FEditorRuntimeUIPreviewWidget::GetWidgetAbsolutePosition(int32 WidgetIndex) const
{
	FVector2 Position;
	FVector2 Size;
	if (!GetWidgetResolvedRect(WidgetIndex, Position, Size))
	{
		return FVector2(0.0f, 0.0f);
	}
	return Position;
}

bool FEditorRuntimeUIPreviewWidget::GetWidgetScreenRect(
	int32 WidgetIndex,
	const ImVec2& CanvasMin,
	float Scale,
	ImVec2& OutMin,
	ImVec2& OutMax) const
{
	const FRuntimeUIWidgetNode* Node = LayoutAsset.GetWidget(WidgetIndex);
	if (!Node || !Node->bVisible || Scale <= 0.0f)
	{
		return false;
	}

	FVector2 Absolute;
	FVector2 Size;
	if (!GetWidgetResolvedRect(WidgetIndex, Absolute, Size))
	{
		return false;
	}
	OutMin = ImVec2(CanvasMin.x + Absolute.X * Scale, CanvasMin.y + Absolute.Y * Scale);
	OutMax = ImVec2(OutMin.x + Size.X * Scale, OutMin.y + Size.Y * Scale);
	return true;
}

bool FEditorRuntimeUIPreviewWidget::IsMouseOverResizeHandle(int32 WidgetIndex, const ImVec2& CanvasMin, float Scale) const
{
	const FRuntimeUIWidgetNode* Node = LayoutAsset.GetWidget(WidgetIndex);
	if (WidgetIndex == 0 || !Node || Node->bLocked)
	{
		return false;
	}

	ImVec2 Min;
	ImVec2 Max;
	if (!GetWidgetScreenRect(WidgetIndex, CanvasMin, Scale, Min, Max))
	{
		return false;
	}

	const float HandleSize = 11.0f;
	const ImVec2 HandleMin(Max.x - HandleSize, Max.y - HandleSize);
	const ImVec2 HandleMax(Max.x + HandleSize, Max.y + HandleSize);
	return IsPointInRect(ImGui::GetIO().MousePos, HandleMin, HandleMax);
}

FVector2 FEditorRuntimeUIPreviewWidget::SnapPosition(const FVector2& Value) const
{
	if (!bSnapToGrid || DesignerGridSize <= 0.0f)
	{
		return Value;
	}
	return FVector2(
		std::round(Value.X / DesignerGridSize) * DesignerGridSize,
		std::round(Value.Y / DesignerGridSize) * DesignerGridSize);
}

FVector2 FEditorRuntimeUIPreviewWidget::SnapSize(const FVector2& Value) const
{
	const FVector2 Clamped(std::max(8.0f, Value.X), std::max(8.0f, Value.Y));
	if (!bSnapToGrid || DesignerGridSize <= 0.0f)
	{
		return Clamped;
	}
	return FVector2(
		std::max(8.0f, std::round(Clamped.X / DesignerGridSize) * DesignerGridSize),
		std::max(8.0f, std::round(Clamped.Y / DesignerGridSize) * DesignerGridSize));
}

TArray<FString> FEditorRuntimeUIPreviewWidget::CollectKnownActionNames() const
{
	TArray<FString> Actions;
	auto AddUnique = [&Actions](const FString& ActionName)
	{
		if (ActionName.empty())
		{
			return;
		}
		if (std::find(Actions.begin(), Actions.end(), ActionName) == Actions.end())
		{
			Actions.push_back(ActionName);
		}
	};

	const char* Defaults[] =
	{
		"StartGame",
		"ResumeGame",
		"OpenMenu",
		"CloseMenu",
		"Confirm",
		"Cancel",
		"Back",
		"QuitGame",
	};
	for (const char* DefaultAction : Defaults)
	{
		AddUnique(DefaultAction);
	}

	for (const FRuntimeUIWidgetNode& Widget : LayoutAsset.GetWidgets())
	{
		AddUnique(Widget.OnClickAction);
	}
	for (const FString& EventName : PreviewActionEvents)
	{
		AddUnique(EventName);
	}
	return Actions;
}

void FEditorRuntimeUIPreviewWidget::RefreshPreviewDocument()
{
	if (!EditorEngine)
	{
		bPreviewDocumentLoaded = false;
		return;
	}

	EditorEngine->GetRmlUiSystem().UnloadDocument(PreviewScreenIdBuffer);
	bPreviewDocumentLoaded = false;
	PreviewActionEvents.clear();
	LoadPreviewDocument();
}

bool FEditorRuntimeUIPreviewWidget::SaveLayoutAsset()
{
	const FString Path = FPaths::Normalize(LayoutAssetPathBuffer);
	if (!IsLayoutAssetPath(Path))
	{
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Warning("Runtime UI layout must be saved as .uasset.");
		}
		return false;
	}

	SyncGeneratedPathsFromLayoutPath(false);
	if (!LayoutAsset.SaveToFile(Path))
	{
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Error("Failed to save Runtime UI layout asset.");
		}
		return false;
	}

	strncpy_s(LayoutAssetPathBuffer, Path.c_str(), _TRUNCATE);
	SavedLayoutFingerprint = ComputeLayoutFingerprint();
	UpdateLayoutDirtyState();
	if (EditorEngine)
	{
		EditorEngine->GetNotificationService().Info("Runtime UI layout saved.");
		EditorEngine->GetAssetService().RefreshAssetDatabase();
		EditorEngine->GetMainPanel().RefreshContentBrowser();
	}
	return true;
}

bool FEditorRuntimeUIPreviewWidget::ExportLayoutToPreview()
{
	FString ErrorMessage;
	const FString RmlPath = FPaths::Normalize(GeneratedRmlPathBuffer);
	const FString RcssPath = FPaths::Normalize(GeneratedRcssPathBuffer);
	if (!LayoutAsset.ExportRmlAndRcss(RmlPath, RcssPath, &ErrorMessage))
	{
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Error(ErrorMessage.empty() ? "Failed to export Runtime UI layout." : ErrorMessage);
		}
		return false;
	}

	strncpy_s(PreviewDocumentPathBuffer, RmlPath.c_str(), _TRUNCATE);
	RefreshPreviewDocument();
	ExportedLayoutFingerprint = ComputeLayoutFingerprint();
	UpdateLayoutDirtyState();
	if (EditorEngine)
	{
		EditorEngine->GetNotificationService().Info("Runtime UI layout exported.");
	}
	return bPreviewDocumentLoaded;
}

bool FEditorRuntimeUIPreviewWidget::SaveAndExportLayout()
{
	if (!SaveLayoutAsset())
	{
		return false;
	}
	return ExportLayoutToPreview();
}

void FEditorRuntimeUIPreviewWidget::SyncGeneratedPathsFromLayoutPath(bool bForce)
{
	const FString LayoutPath = FPaths::Normalize(LayoutAssetPathBuffer);
	if (!IsLayoutAssetPath(LayoutPath))
	{
		return;
	}

	std::filesystem::path FsPath(FPaths::ToWide(LayoutPath));
	const FString Stem = FsPath.stem().string();
	if (Stem.empty())
	{
		return;
	}

	const FString RmlPath = "Asset/UI/Generated/" + Stem + ".rml";
	const FString RcssPath = "Asset/UI/Generated/" + Stem + ".rcss";
	const bool bGeneratedPathsEmpty = GeneratedRmlPathBuffer[0] == '\0' || GeneratedRcssPathBuffer[0] == '\0';
	const bool bGeneratedPathsDefault =
		FString(GeneratedRmlPathBuffer) == "Asset/UI/Generated/NewRuntimeUI.rml" &&
		FString(GeneratedRcssPathBuffer) == "Asset/UI/Generated/NewRuntimeUI.rcss";
	if (bForce || bGeneratedPathsEmpty || bGeneratedPathsDefault)
	{
		strncpy_s(GeneratedRmlPathBuffer, RmlPath.c_str(), _TRUNCATE);
		strncpy_s(GeneratedRcssPathBuffer, RcssPath.c_str(), _TRUNCATE);
	}
	LayoutAsset.SetGeneratedPaths(GeneratedRmlPathBuffer, GeneratedRcssPathBuffer);
}

void FEditorRuntimeUIPreviewWidget::UpdateLayoutDirtyState()
{
	const uint64 CurrentFingerprint = ComputeLayoutFingerprint();
	bLayoutDirty = CurrentFingerprint != SavedLayoutFingerprint;
	bPreviewExportDirty = CurrentFingerprint != ExportedLayoutFingerprint;
}

FEditorRuntimeUIPreviewWidget::FLayoutUndoSnapshot FEditorRuntimeUIPreviewWidget::MakeUndoSnapshot() const
{
	FLayoutUndoSnapshot Snapshot;
	Snapshot.Widgets = LayoutAsset.GetWidgets();
	Snapshot.SelectedWidgetIndex = SelectedWidgetIndex;
	Snapshot.SelectedWidgetIndices = SelectedWidgetIndices;
	Snapshot.CopiedWidgetId = CopiedWidgetId;
	return Snapshot;
}

void FEditorRuntimeUIPreviewWidget::RestoreUndoSnapshot(const FLayoutUndoSnapshot& Snapshot)
{
	LayoutAsset.GetMutableWidgets() = Snapshot.Widgets;
	const int32 WidgetCount = static_cast<int32>(LayoutAsset.GetWidgets().size());
	SelectedWidgetIndex = WidgetCount > 0 ? std::clamp(Snapshot.SelectedWidgetIndex, 0, WidgetCount - 1) : 0;
	SelectedWidgetIndices = Snapshot.SelectedWidgetIndices;
	if (SelectedWidgetIndices.empty())
	{
		SelectedWidgetIndices.push_back(SelectedWidgetIndex);
	}
	NormalizeWidgetSelection();
	CopiedWidgetId = Snapshot.CopiedWidgetId;
	bDraggingWidget = false;
	DesignerDragMode = EDesignerDragMode::None;
	DragStartWidgets.clear();
	ActiveGuideLines.clear();
	LastUndoSnapshot = MakeUndoSnapshot();
	LastUndoFingerprint = ComputeLayoutFingerprint();
	bUndoBaselineValid = true;
	UpdateLayoutDirtyState();
}

void FEditorRuntimeUIPreviewWidget::ResetUndoHistory()
{
	UndoStack.clear();
	RedoStack.clear();
	LastUndoSnapshot = MakeUndoSnapshot();
	LastUndoFingerprint = ComputeLayoutFingerprint();
	bUndoBaselineValid = true;
}

void FEditorRuntimeUIPreviewWidget::PushUndoSnapshot(const FLayoutUndoSnapshot& Snapshot)
{
	constexpr size_t MaxUndoSnapshots = 64;
	UndoStack.push_back(Snapshot);
	while (UndoStack.size() > MaxUndoSnapshots)
	{
		UndoStack.erase(UndoStack.begin());
	}
}

void FEditorRuntimeUIPreviewWidget::CommitPendingUndoSnapshot(bool bForce)
{
	if (!bDesignMode)
	{
		return;
	}
	if (!bUndoBaselineValid)
	{
		ResetUndoHistory();
		return;
	}

	const uint64 CurrentFingerprint = ComputeLayoutFingerprint();
	if (CurrentFingerprint == LastUndoFingerprint)
	{
		return;
	}

	if (!bForce)
	{
		const ImGuiIO& IO = ImGui::GetIO();
		if (bDraggingWidget || ImGui::IsAnyItemActive() || IO.MouseDown[ImGuiMouseButton_Left] || IO.MouseDown[ImGuiMouseButton_Right])
		{
			return;
		}
	}

	PushUndoSnapshot(LastUndoSnapshot);
	RedoStack.clear();
	LastUndoSnapshot = MakeUndoSnapshot();
	LastUndoFingerprint = CurrentFingerprint;
	UpdateLayoutDirtyState();
}

bool FEditorRuntimeUIPreviewWidget::CanUndoLayoutEdit() const
{
	if (!bDesignMode || !bUndoBaselineValid)
	{
		return false;
	}
	return !UndoStack.empty() || ComputeLayoutFingerprint() != LastUndoFingerprint;
}

bool FEditorRuntimeUIPreviewWidget::CanRedoLayoutEdit() const
{
	return bDesignMode && !RedoStack.empty();
}

void FEditorRuntimeUIPreviewWidget::UndoLayoutEdit()
{
	if (!bDesignMode)
	{
		return;
	}

	CommitPendingUndoSnapshot(true);
	if (UndoStack.empty())
	{
		return;
	}

	FLayoutUndoSnapshot CurrentSnapshot = MakeUndoSnapshot();
	FLayoutUndoSnapshot PreviousSnapshot = UndoStack.back();
	UndoStack.pop_back();
	RedoStack.push_back(CurrentSnapshot);
	RestoreUndoSnapshot(PreviousSnapshot);
}

void FEditorRuntimeUIPreviewWidget::RedoLayoutEdit()
{
	if (!CanRedoLayoutEdit())
	{
		return;
	}

	FLayoutUndoSnapshot CurrentSnapshot = MakeUndoSnapshot();
	FLayoutUndoSnapshot NextSnapshot = RedoStack.back();
	RedoStack.pop_back();
	PushUndoSnapshot(CurrentSnapshot);
	RestoreUndoSnapshot(NextSnapshot);
}

void FEditorRuntimeUIPreviewWidget::HandleUndoRedoShortcuts()
{
	if (!bDesignMode || ImGui::GetIO().WantTextInput)
	{
		return;
	}

	const ImGuiIO& IO = ImGui::GetIO();
	if (!IO.KeyCtrl)
	{
		return;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Z))
	{
		if (IO.KeyShift)
		{
			RedoLayoutEdit();
		}
		else
		{
			UndoLayoutEdit();
		}
	}
	else if (ImGui::IsKeyPressed(ImGuiKey_Y))
	{
		RedoLayoutEdit();
	}
}

uint64 FEditorRuntimeUIPreviewWidget::ComputeLayoutFingerprint() const
{
	uint64 Hash = 1469598103934665603ull;
	const TArray<FRuntimeUIWidgetNode>& Widgets = LayoutAsset.GetWidgets();
	const uint64 Count = static_cast<uint64>(Widgets.size());
	HashValue(Hash, Count);
	for (const FRuntimeUIWidgetNode& Node : Widgets)
	{
		HashString(Hash, Node.Id);
		HashString(Hash, Node.DisplayName);
		const uint8 Type = static_cast<uint8>(Node.Type);
		HashValue(Hash, Type);
		HashValue(Hash, Node.ParentIndex);
		const uint64 ChildCount = static_cast<uint64>(Node.Children.size());
		HashValue(Hash, ChildCount);
		for (const int32 ChildIndex : Node.Children)
		{
			HashValue(Hash, ChildIndex);
		}
		HashValue(Hash, Node.AnchorMin);
		HashValue(Hash, Node.AnchorMax);
		HashValue(Hash, Node.Pivot);
		HashValue(Hash, Node.Position);
		HashValue(Hash, Node.Size);
		HashValue(Hash, Node.Rotation);
		HashValue(Hash, Node.Scale);
		HashString(Hash, Node.Text);
		HashString(Hash, Node.ImagePath);
		HashString(Hash, Node.StyleClass);
		HashValue(Hash, Node.bVisible);
		HashValue(Hash, Node.bLocked);
		HashValue(Hash, Node.BackgroundColor);
		HashValue(Hash, Node.TextColor);
		HashValue(Hash, Node.FontSize);
		HashString(Hash, Node.FontFamily);
		HashValue(Hash, Node.FontWeight);
		HashValue(Hash, Node.LineHeight);
		HashValue(Hash, Node.LetterSpacing);
		HashValue(Hash, Node.bTextWrap);
		HashString(Hash, Node.TextAlign);
		HashValue(Hash, Node.ImageTint);
		const uint8 ImageFit = static_cast<uint8>(Node.ImageFit);
		HashValue(Hash, ImageFit);
		HashValue(Hash, Node.Opacity);
		HashValue(Hash, Node.BorderColor);
		HashValue(Hash, Node.BorderWidth);
		HashValue(Hash, Node.BorderRadius);
		const uint8 LayoutMode = static_cast<uint8>(Node.LayoutMode);
		const uint8 LayoutAlignment = static_cast<uint8>(Node.LayoutAlignment);
		const uint8 LayoutSizeRule = static_cast<uint8>(Node.LayoutSizeRule);
		HashValue(Hash, LayoutMode);
		HashValue(Hash, LayoutAlignment);
		HashValue(Hash, LayoutSizeRule);
		HashValue(Hash, Node.LayoutPadding);
		HashValue(Hash, Node.LayoutGap);
		HashValue(Hash, Node.LayoutFillWeight);
		HashValue(Hash, Node.bUseButtonStateStyle);
		HashValue(Hash, Node.ButtonHoverStyle.BackgroundColor);
		HashValue(Hash, Node.ButtonHoverStyle.TextColor);
		HashValue(Hash, Node.ButtonHoverStyle.BorderColor);
		HashValue(Hash, Node.ButtonPressedStyle.BackgroundColor);
		HashValue(Hash, Node.ButtonPressedStyle.TextColor);
		HashValue(Hash, Node.ButtonPressedStyle.BorderColor);
		HashValue(Hash, Node.ButtonDisabledStyle.BackgroundColor);
		HashValue(Hash, Node.ButtonDisabledStyle.TextColor);
		HashValue(Hash, Node.ButtonDisabledStyle.BorderColor);
		HashValue(Hash, Node.bUseNineSlice);
		HashValue(Hash, Node.NineSliceBorder);
		HashString(Hash, Node.OnClickAction);
	}
	return Hash;
}
