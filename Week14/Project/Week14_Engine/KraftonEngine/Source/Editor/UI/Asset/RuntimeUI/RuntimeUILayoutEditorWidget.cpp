#include "Editor/UI/Asset/RuntimeUI/RuntimeUILayoutEditorWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/Undo/EditorUndoSystem.h"
#include "Editor/UI/Util/EditorTextureManager.h"
#include "Object/Object.h"
#include "Platform/Paths.h"
#include "Serialization/MemoryArchive.h"
#include "UI/RuntimeUILayoutAsset.h"
#include "UI/RuntimeUILayoutManager.h"

#include "ImGui/imgui.h"

#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace
{
	const char* GetWidgetTypeLabel(ERuntimeUIWidgetType Type)
	{
		switch (Type)
		{
		case ERuntimeUIWidgetType::Canvas: return "Canvas";
		case ERuntimeUIWidgetType::Panel: return "Panel";
		case ERuntimeUIWidgetType::Text: return "Text";
		case ERuntimeUIWidgetType::Image: return "Image";
		case ERuntimeUIWidgetType::Button: return "Button";
		default: return "Widget";
		}
	}

	const char* GetImageFitLabel(ERuntimeUIImageFit Fit)
	{
		switch (Fit)
		{
		case ERuntimeUIImageFit::Contain: return "Contain";
		case ERuntimeUIImageFit::Cover: return "Cover";
		case ERuntimeUIImageFit::Stretch:
		default: return "Stretch";
		}
	}

	FString GetPathStem(const FString& Path)
	{
		if (Path.empty())
		{
			return "Runtime UI Layout";
		}
		std::filesystem::path FilePath(FPaths::ToWide(Path));
		const FString Stem = FPaths::ToUtf8(FilePath.stem().wstring());
		return Stem.empty() ? FString("Runtime UI Layout") : Stem;
	}

	FString ResolveRuntimeUILayoutPreviewImagePath(const FString& ImagePath)
	{
		if (ImagePath.empty())
		{
			return FString();
		}

		const std::filesystem::path SourcePath(FPaths::ToWide(ImagePath));
		if (SourcePath.is_absolute())
		{
			return FPaths::ToUtf8(SourcePath.lexically_normal().wstring());
		}

		TArray<std::filesystem::path> Candidates;
		const std::filesystem::path RootPath(FPaths::RootDir());
		const std::filesystem::path AssetPath(FPaths::AssetDir());
		const std::filesystem::path CurrentPath = std::filesystem::current_path();
		const FString NormalizedPath = FPaths::MakeProjectRelative(ImagePath);
		const bool bStartsAtContent = NormalizedPath.rfind("Content/", 0) == 0 || NormalizedPath.rfind("Content\\", 0) == 0;

		if (bStartsAtContent)
		{
			Candidates.push_back(RootPath / FPaths::ToWide(NormalizedPath));
			Candidates.push_back(CurrentPath / FPaths::ToWide(NormalizedPath));
			Candidates.push_back(CurrentPath / L"KraftonEngine" / FPaths::ToWide(NormalizedPath));
		}
		else
		{
			Candidates.push_back(AssetPath / L"UI" / SourcePath);
			Candidates.push_back(RootPath / L"Content" / L"UI" / SourcePath);
			Candidates.push_back(CurrentPath / L"Content" / L"UI" / SourcePath);
			Candidates.push_back(CurrentPath / L"KraftonEngine" / L"Content" / L"UI" / SourcePath);
		}

		std::error_code Ec;
		for (const std::filesystem::path& Candidate : Candidates)
		{
			const std::filesystem::path NormalizedCandidate = Candidate.lexically_normal();
			if (std::filesystem::exists(NormalizedCandidate, Ec) && !Ec)
			{
				return FPaths::ToUtf8(NormalizedCandidate.wstring());
			}
			Ec.clear();
		}

		return FPaths::ToUtf8(Candidates.empty() ? SourcePath.generic_wstring() : Candidates.front().lexically_normal().wstring());
	}

	FVector2 GetRuntimeUILayoutPreviewImageSize(ID3D11ShaderResourceView* ImageSRV)
	{
		if (!ImageSRV)
		{
			return FVector2(0.0f, 0.0f);
		}

		ID3D11Resource* Resource = nullptr;
		ImageSRV->GetResource(&Resource);
		if (!Resource)
		{
			return FVector2(0.0f, 0.0f);
		}

		ID3D11Texture2D* Texture = nullptr;
		const HRESULT Hr = Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture));
		Resource->Release();
		if (FAILED(Hr) || !Texture)
		{
			return FVector2(0.0f, 0.0f);
		}

		D3D11_TEXTURE2D_DESC Desc = {};
		Texture->GetDesc(&Desc);
		Texture->Release();
		return FVector2(static_cast<float>(Desc.Width), static_cast<float>(Desc.Height));
	}

	bool EditString(const char* Label, FString& Value)
	{
		char Buffer[512] = {};
		std::snprintf(Buffer, sizeof(Buffer), "%s", Value.c_str());
		if (ImGui::InputText(Label, Buffer, sizeof(Buffer)))
		{
			Value = Buffer;
			return true;
		}
		return false;
	}

	bool EditVector2(const char* Label, FVector2& Value)
	{
		float Buffer[2] = { Value.X, Value.Y };
		if (ImGui::InputFloat2(Label, Buffer, "%.2f"))
		{
			Value = FVector2(Buffer[0], Buffer[1]);
			return true;
		}
		return false;
	}

	bool EditVector4(const char* Label, FVector4& Value)
	{
		float Buffer[4] = { Value.X, Value.Y, Value.Z, Value.W };
		if (ImGui::InputFloat4(Label, Buffer, "%.2f"))
		{
			Value = FVector4(Buffer[0], Buffer[1], Buffer[2], Buffer[3]);
			return true;
		}
		return false;
	}

	bool EditColor(const char* Label, FVector4& Value)
	{
		float Buffer[4] = { Value.X, Value.Y, Value.Z, Value.W };
		if (ImGui::ColorEdit4(Label, Buffer))
		{
			Value = FVector4(Buffer[0], Buffer[1], Buffer[2], Buffer[3]);
			return true;
		}
		return false;
	}

	ImU32 ToImColor(const FVector4& Color)
	{
		return ImGui::ColorConvertFloat4ToU32(ImVec4(Color.X, Color.Y, Color.Z, Color.W));
	}

	bool IsPointInsideRect(const FVector2& Point, const FVector2& Min, const FVector2& Max)
	{
		return Point.X >= Min.X && Point.Y >= Min.Y && Point.X <= Max.X && Point.Y <= Max.Y;
	}

	float ClampPreviewZoom(float Zoom)
	{
		return (std::max)(0.25f, (std::min)(Zoom, 8.0f));
	}

	float SnapRuntimeUILayoutPreviewValue(float Value, float Target, float Threshold, bool& bOutSnapped)
	{
		if (std::abs(Value - Target) <= Threshold)
		{
			bOutSnapped = true;
			return Target;
		}
		return Value;
	}

	bool TrySnapRuntimeUILayoutPreviewValue(
		float Value,
		float Target,
		float Guide,
		float Threshold,
		float& InOutBestDistance,
		float& OutValue,
		float& OutGuide)
	{
		const float Distance = (std::abs)(Value - Target);
		if (Distance > Threshold || Distance >= InOutBestDistance)
		{
			return false;
		}

		InOutBestDistance = Distance;
		OutValue = Target;
		OutGuide = Guide;
		return true;
	}

	float SnapRuntimeUILayoutPixelValue(float Value)
	{
		return (std::round)(Value);
	}

	FVector2 SnapRuntimeUILayoutPixelPosition(const FVector2& Value)
	{
		return FVector2(
			SnapRuntimeUILayoutPixelValue(Value.X),
			SnapRuntimeUILayoutPixelValue(Value.Y));
	}

	void GetRuntimeUILayoutPreviewImageFitRect(
		ERuntimeUIImageFit Fit,
		const ImVec2& Min,
		const ImVec2& Max,
		const FVector2& SourceSize,
		ImVec2& OutMin,
		ImVec2& OutMax)
	{
		OutMin = Min;
		OutMax = Max;
		if (Fit == ERuntimeUIImageFit::Stretch || SourceSize.X <= 0.0f || SourceSize.Y <= 0.0f)
		{
			return;
		}

		const float BoxWidth = (std::max)(1.0f, Max.x - Min.x);
		const float BoxHeight = (std::max)(1.0f, Max.y - Min.y);
		const float ScaleX = BoxWidth / SourceSize.X;
		const float ScaleY = BoxHeight / SourceSize.Y;
		const float FitScale = Fit == ERuntimeUIImageFit::Cover
			? (std::max)(ScaleX, ScaleY)
			: (std::min)(ScaleX, ScaleY);
		const ImVec2 FittedSize(SourceSize.X * FitScale, SourceSize.Y * FitScale);
		OutMin = ImVec2(Min.x + (BoxWidth - FittedSize.x) * 0.5f, Min.y + (BoxHeight - FittedSize.y) * 0.5f);
		OutMax = ImVec2(OutMin.x + FittedSize.x, OutMin.y + FittedSize.y);
	}

	bool IntersectRuntimeUILayoutPreviewClip(ImVec2& ClipMin, ImVec2& ClipMax, const ImVec2& RectMin, const ImVec2& RectMax)
	{
		ClipMin.x = (std::max)(ClipMin.x, RectMin.x);
		ClipMin.y = (std::max)(ClipMin.y, RectMin.y);
		ClipMax.x = (std::min)(ClipMax.x, RectMax.x);
		ClipMax.y = (std::min)(ClipMax.y, RectMax.y);
		return ClipMin.x < ClipMax.x && ClipMin.y < ClipMax.y;
	}

	FVector2 ResolveRuntimeUILayoutNodeSize(
		const TArray<FRuntimeUIWidgetNode>& Widgets,
		const FVector2& CanvasSize,
		int32 Index,
		int32 Depth = 0)
	{
		if (Index < 0 || Index >= static_cast<int32>(Widgets.size()) || Depth > static_cast<int32>(Widgets.size()))
		{
			return CanvasSize;
		}

		const FRuntimeUIWidgetNode& Node = Widgets[Index];
		if (Index == 0)
		{
			return CanvasSize;
		}

		const FVector2 ParentSize = ResolveRuntimeUILayoutNodeSize(Widgets, CanvasSize, Node.ParentIndex, Depth + 1);
		return FVector2(
			Node.bUseWidthPercent ? ParentSize.X * Node.SizePercent.X * 0.01f : Node.Size.X,
			Node.bUseHeightPercent ? ParentSize.Y * Node.SizePercent.Y * 0.01f : Node.Size.Y);
	}

	FVector2 ResolveRuntimeUILayoutNodePosition(
		const TArray<FRuntimeUIWidgetNode>& Widgets,
		const FVector2& CanvasSize,
		int32 Index,
		int32 Depth = 0)
	{
		if (Index < 0 || Index >= static_cast<int32>(Widgets.size()) || Depth > static_cast<int32>(Widgets.size()))
		{
			return FVector2(0.0f, 0.0f);
		}

		const FRuntimeUIWidgetNode& Node = Widgets[Index];
		FVector2 LocalPosition = Node.Position;
		if (Node.ParentIndex >= 0 && Node.ParentIndex != Index)
		{
			const FVector2 ParentPosition = ResolveRuntimeUILayoutNodePosition(Widgets, CanvasSize, Node.ParentIndex, Depth + 1);
			const FVector2 ParentSize = ResolveRuntimeUILayoutNodeSize(Widgets, CanvasSize, Node.ParentIndex);
			const FVector2 NodeSize = ResolveRuntimeUILayoutNodeSize(Widgets, CanvasSize, Index);
			if (Node.bUseRight)
			{
				LocalPosition.X = ParentSize.X - Node.Right - NodeSize.X;
			}
			else if (Node.bUseLeftPercent)
			{
				LocalPosition.X = ParentSize.X * Node.PositionPercent.X * 0.01f;
			}
			if (Node.bUseBottom)
			{
				LocalPosition.Y = ParentSize.Y - Node.Bottom - NodeSize.Y;
			}
			else if (Node.bUseTopPercent)
			{
				LocalPosition.Y = ParentSize.Y * Node.PositionPercent.Y * 0.01f;
			}
			return FVector2(ParentPosition.X + LocalPosition.X, ParentPosition.Y + LocalPosition.Y);
		}
		return LocalPosition;
	}

	FVector2 ResolveRuntimeUILayoutNodeLocalPosition(
		const TArray<FRuntimeUIWidgetNode>& Widgets,
		const FVector2& CanvasSize,
		int32 Index)
	{
		if (Index < 0 || Index >= static_cast<int32>(Widgets.size()))
		{
			return FVector2(0.0f, 0.0f);
		}

		const FRuntimeUIWidgetNode& Node = Widgets[Index];
		if (Index == 0)
		{
			return FVector2(0.0f, 0.0f);
		}

		const FVector2 ParentSize = Node.ParentIndex >= 0
			? ResolveRuntimeUILayoutNodeSize(Widgets, CanvasSize, Node.ParentIndex)
			: CanvasSize;
		const FVector2 NodeSize = ResolveRuntimeUILayoutNodeSize(Widgets, CanvasSize, Index);
		FVector2 LocalPosition = Node.Position;
		if (Node.bUseRight)
		{
			LocalPosition.X = ParentSize.X - Node.Right - NodeSize.X;
		}
		else if (Node.bUseLeftPercent)
		{
			LocalPosition.X = ParentSize.X * Node.PositionPercent.X * 0.01f;
		}
		if (Node.bUseBottom)
		{
			LocalPosition.Y = ParentSize.Y - Node.Bottom - NodeSize.Y;
		}
		else if (Node.bUseTopPercent)
		{
			LocalPosition.Y = ParentSize.Y * Node.PositionPercent.Y * 0.01f;
		}
		return LocalPosition;
	}

	void ApplyRuntimeUILayoutNodeLocalPosition(
		FRuntimeUIWidgetNode& Node,
		const FVector2& ParentSize,
		const FVector2& NodeSize,
		const FVector2& LocalPosition)
	{
		const FVector2 PixelPosition = SnapRuntimeUILayoutPixelPosition(LocalPosition);
		Node.Position = PixelPosition;
		if (Node.bUseRight)
		{
			Node.Right = SnapRuntimeUILayoutPixelValue(ParentSize.X - PixelPosition.X - NodeSize.X);
		}
		else if (Node.bUseLeftPercent)
		{
			Node.PositionPercent.X = ParentSize.X > 0.001f ? PixelPosition.X / ParentSize.X * 100.0f : 0.0f;
		}

		if (Node.bUseBottom)
		{
			Node.Bottom = SnapRuntimeUILayoutPixelValue(ParentSize.Y - PixelPosition.Y - NodeSize.Y);
		}
		else if (Node.bUseTopPercent)
		{
			Node.PositionPercent.Y = ParentSize.Y > 0.001f ? PixelPosition.Y / ParentSize.Y * 100.0f : 0.0f;
		}
	}

	struct FRuntimeUILayoutRmlPatch
	{
		FString DisplayName;
		bool bHasDisplayName = false;
		FString StyleClass;
		bool bHasStyleClass = false;
		FString Action;
		bool bHasAction = false;
		FString ImagePath;
		bool bHasImagePath = false;
		FString Text;
		bool bHasText = false;
		ERuntimeUIWidgetType Type = ERuntimeUIWidgetType::Panel;
		bool bHasType = false;
		ERuntimeUIImageFit ImageFit = ERuntimeUIImageFit::Stretch;
		bool bHasImageFit = false;
	};

	struct FRuntimeUILayoutStylePatch
	{
		float Left = 0.0f;
		bool bHasLeft = false;
		float LeftPercent = 0.0f;
		bool bHasLeftPercent = false;
		float Top = 0.0f;
		bool bHasTop = false;
		float TopPercent = 0.0f;
		bool bHasTopPercent = false;
		float Right = 0.0f;
		bool bHasRight = false;
		float Bottom = 0.0f;
		bool bHasBottom = false;
		float Width = 0.0f;
		bool bHasWidth = false;
		float WidthPercent = 0.0f;
		bool bHasWidthPercent = false;
		float Height = 0.0f;
		bool bHasHeight = false;
		float HeightPercent = 0.0f;
		bool bHasHeightPercent = false;
		float Opacity = 1.0f;
		bool bHasOpacity = false;
		FString MaskImagePath;
		bool bHasMaskImagePath = false;
		FString Display;
		bool bHasDisplay = false;
		FString JustifyContent;
		bool bHasJustifyContent = false;
		FString AlignItems;
		bool bHasAlignItems = false;
		FVector4 BackgroundColor = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		bool bHasBackgroundColor = false;
		FVector4 TextColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		bool bHasTextColor = false;
		FVector4 BorderColor = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
		bool bHasBorderColor = false;
		FVector4 BorderWidth = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		bool bHasBorderWidth = false;
		FVector4 Padding = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		bool bHasPadding = false;
		float BorderRadius = 0.0f;
		bool bHasBorderRadius = false;
		float FontSize = 24.0f;
		bool bHasFontSize = false;
		ERuntimeUIImageFit ImageFit = ERuntimeUIImageFit::Stretch;
		bool bHasImageFit = false;
	};

	struct FRuntimeUILayoutImportedPatch
	{
		std::unordered_map<FString, FRuntimeUILayoutRmlPatch> RmlNodes;
		std::unordered_map<FString, FRuntimeUILayoutStylePatch> Styles;
		struct FImportedRmlNode
		{
			FString Id;
			FString ParentId;
		};
		TArray<FImportedRmlNode> RmlNodeOrder;
		TArray<FString> DuplicateRmlIds;
		int32 IgnoredIdlessElementCount = 0;
		int32 IgnoredMeaningfulIdlessElementCount = 0;
		FVector2 CanvasSize = FVector2(0.0f, 0.0f);
		bool bHasCanvasSize = false;
	};

	std::filesystem::path ToAbsoluteProjectPathForRuntimeUILayoutEditor(const FString& Path)
	{
		std::filesystem::path Result(FPaths::ToWide(Path));
		if (Result.is_relative())
		{
			Result = std::filesystem::path(FPaths::RootDir()) / Result;
		}
		return Result.lexically_normal();
	}

	FString TrimCopy(const FString& Text)
	{
		size_t Begin = 0;
		while (Begin < Text.size() && std::isspace(static_cast<unsigned char>(Text[Begin])))
		{
			++Begin;
		}

		size_t End = Text.size();
		while (End > Begin && std::isspace(static_cast<unsigned char>(Text[End - 1])))
		{
			--End;
		}
		return Text.substr(Begin, End - Begin);
	}

	FString ToLowerCopy(const FString& Text)
	{
		FString Result = Text;
		for (char& Ch : Result)
		{
			Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
		}
		return Result;
	}

	bool EqualsIgnoreCase(const FString& A, const char* B)
	{
		return ToLowerCopy(TrimCopy(A)) == ToLowerCopy(B ? FString(B) : FString());
	}

	FString DecodeXmlEntities(const FString& Text)
	{
		FString Result;
		Result.reserve(Text.size());
		for (size_t Index = 0; Index < Text.size();)
		{
			if (Text.compare(Index, 5, "&amp;") == 0)
			{
				Result.push_back('&');
				Index += 5;
			}
			else if (Text.compare(Index, 4, "&lt;") == 0)
			{
				Result.push_back('<');
				Index += 4;
			}
			else if (Text.compare(Index, 4, "&gt;") == 0)
			{
				Result.push_back('>');
				Index += 4;
			}
			else if (Text.compare(Index, 6, "&quot;") == 0)
			{
				Result.push_back('"');
				Index += 6;
			}
			else if (Text.compare(Index, 6, "&apos;") == 0)
			{
				Result.push_back('\'');
				Index += 6;
			}
			else
			{
				Result.push_back(Text[Index]);
				++Index;
			}
		}
		return Result;
	}

	FString ToCssIdForRuntimeUILayoutEditor(const FString& Id)
	{
		FString Result = Id;
		for (char& Ch : Result)
		{
			if (!std::isalnum(static_cast<unsigned char>(Ch)) && Ch != '_' && Ch != '-')
			{
				Ch = '_';
			}
		}
		return Result.empty() ? FString("RuntimeUI") : Result;
	}

	bool ReadTextFileIfExists(const FString& Path, FString& OutText)
	{
		if (Path.empty())
		{
			return false;
		}

		const std::filesystem::path AbsolutePath = ToAbsoluteProjectPathForRuntimeUILayoutEditor(Path);
		std::error_code Ec;
		if (!std::filesystem::exists(AbsolutePath, Ec) || Ec)
		{
			return false;
		}

		std::ifstream File(AbsolutePath, std::ios::binary);
		if (!File)
		{
			return false;
		}

		std::ostringstream Stream;
		Stream << File.rdbuf();
		OutText = Stream.str();
		return true;
	}

	FString ExtractTagName(const FString& Tag)
	{
		size_t Cursor = 0;
		while (Cursor < Tag.size() && std::isspace(static_cast<unsigned char>(Tag[Cursor])))
		{
			++Cursor;
		}

		const size_t Begin = Cursor;
		while (Cursor < Tag.size()
			&& !std::isspace(static_cast<unsigned char>(Tag[Cursor]))
			&& Tag[Cursor] != '/'
			&& Tag[Cursor] != '>')
		{
			++Cursor;
		}
		return Tag.substr(Begin, Cursor - Begin);
	}

	bool ExtractTagAttribute(const FString& Tag, const char* Name, FString& OutValue)
	{
		if (!Name || !*Name)
		{
			return false;
		}

		const FString AttributeName = Name;
		size_t Search = 0;
		while (Search < Tag.size())
		{
			const size_t Pos = Tag.find(AttributeName, Search);
			if (Pos == FString::npos)
			{
				return false;
			}

			const bool bValidBefore = Pos == 0 || std::isspace(static_cast<unsigned char>(Tag[Pos - 1]));
			size_t Cursor = Pos + AttributeName.size();
			while (Cursor < Tag.size() && std::isspace(static_cast<unsigned char>(Tag[Cursor])))
			{
				++Cursor;
			}

			if (bValidBefore && Cursor < Tag.size() && Tag[Cursor] == '=')
			{
				++Cursor;
				while (Cursor < Tag.size() && std::isspace(static_cast<unsigned char>(Tag[Cursor])))
				{
					++Cursor;
				}

				if (Cursor >= Tag.size())
				{
					OutValue.clear();
					return true;
				}

				const char Quote = (Tag[Cursor] == '"' || Tag[Cursor] == '\'') ? Tag[Cursor++] : '\0';
				const size_t ValueBegin = Cursor;
				size_t ValueEnd = Cursor;
				if (Quote != '\0')
				{
					ValueEnd = Tag.find(Quote, Cursor);
					if (ValueEnd == FString::npos)
					{
						ValueEnd = Tag.size();
					}
				}
				else
				{
					while (ValueEnd < Tag.size() && !std::isspace(static_cast<unsigned char>(Tag[ValueEnd])) && Tag[ValueEnd] != '/')
					{
						++ValueEnd;
					}
				}

				OutValue = DecodeXmlEntities(Tag.substr(ValueBegin, ValueEnd - ValueBegin));
				return true;
			}

			Search = Pos + AttributeName.size();
		}

		return false;
	}

	bool ParseWidgetTypeName(const FString& Value, ERuntimeUIWidgetType& OutType)
	{
		const FString Lower = ToLowerCopy(TrimCopy(Value));
		if (Lower == "canvas")
		{
			OutType = ERuntimeUIWidgetType::Canvas;
			return true;
		}
		if (Lower == "panel" || Lower == "div")
		{
			OutType = ERuntimeUIWidgetType::Panel;
			return true;
		}
		if (Lower == "text")
		{
			OutType = ERuntimeUIWidgetType::Text;
			return true;
		}
		if (Lower == "image" || Lower == "img")
		{
			OutType = ERuntimeUIWidgetType::Image;
			return true;
		}
		if (Lower == "button")
		{
			OutType = ERuntimeUIWidgetType::Button;
			return true;
		}
		return false;
	}

	bool ParseImageFitName(const FString& Value, ERuntimeUIImageFit& OutFit)
	{
		const FString Lower = ToLowerCopy(TrimCopy(Value));
		if (Lower == "contain")
		{
			OutFit = ERuntimeUIImageFit::Contain;
			return true;
		}
		if (Lower == "cover")
		{
			OutFit = ERuntimeUIImageFit::Cover;
			return true;
		}
		if (Lower == "fill" || Lower == "stretch")
		{
			OutFit = ERuntimeUIImageFit::Stretch;
			return true;
		}
		return false;
	}

	bool IsVoidRmlTag(const FString& TagName)
	{
		const FString Lower = ToLowerCopy(TagName);
		return Lower == "img"
			|| Lower == "br"
			|| Lower == "hr"
			|| Lower == "input"
			|| Lower == "link"
			|| Lower == "meta";
	}

	bool IsRmlDocumentScaffoldTag(const FString& TagName)
	{
		const FString Lower = ToLowerCopy(TagName);
		return Lower == "rml"
			|| Lower == "head"
			|| Lower == "body"
			|| Lower == "title"
			|| Lower == "link"
			|| Lower == "script"
			|| Lower == "style";
	}

	bool IsMeaningfulIdlessRmlElement(const FString& Tag, const FString& TagName)
	{
		if (IsRmlDocumentScaffoldTag(TagName))
		{
			return false;
		}

		FString Value;
		return ExtractTagAttribute(Tag, "class", Value)
			|| ExtractTagAttribute(Tag, "style", Value)
			|| ExtractTagAttribute(Tag, "data-ui-name", Value)
			|| ExtractTagAttribute(Tag, "data-ui-type", Value)
			|| ExtractTagAttribute(Tag, "data-action", Value)
			|| ExtractTagAttribute(Tag, "action", Value)
			|| ExtractTagAttribute(Tag, "src", Value);
	}

	FString FindNearestOpenRmlId(const TArray<FString>& OpenElementIds)
	{
		for (int32 Index = static_cast<int32>(OpenElementIds.size()) - 1; Index >= 0; --Index)
		{
			if (!OpenElementIds[Index].empty())
			{
				return OpenElementIds[Index];
			}
		}
		return FString();
	}

	bool AppendUniqueDuplicateRmlId(TArray<FString>& DuplicateIds, const FString& Id)
	{
		for (const FString& ExistingId : DuplicateIds)
		{
			if (ExistingId == Id)
			{
				return false;
			}
		}
		DuplicateIds.push_back(Id);
		return true;
	}

	FString DescribeDuplicateRmlIds(const TArray<FString>& DuplicateIds)
	{
		if (DuplicateIds.empty())
		{
			return FString();
		}

		FString Result = "duplicate id";
		Result += DuplicateIds.size() == 1 ? " " : "s ";
		Result += "'";
		Result += DuplicateIds[0];
		Result += "'";
		const int32 MaxListedIds = (std::min)(static_cast<int32>(DuplicateIds.size()), 3);
		for (int32 Index = 1; Index < MaxListedIds; ++Index)
		{
			Result += ", '";
			Result += DuplicateIds[Index];
			Result += "'";
		}
		if (static_cast<int32>(DuplicateIds.size()) > MaxListedIds)
		{
			Result += ", +";
			Result += std::to_string(static_cast<int32>(DuplicateIds.size()) - MaxListedIds);
			Result += " more";
		}
		return Result;
	}

	void AppendIdlessRmlPolicyStatus(FString& Status, const FRuntimeUILayoutImportedPatch& ImportedPatch)
	{
		if (ImportedPatch.IgnoredIdlessElementCount <= 0)
		{
			return;
		}

		Status += " Ignored ";
		Status += std::to_string(ImportedPatch.IgnoredIdlessElementCount);
		Status += " id-less RML element";
		Status += ImportedPatch.IgnoredIdlessElementCount == 1 ? " as a transparent wrapper." : "s as transparent wrappers.";

		if (ImportedPatch.IgnoredMeaningfulIdlessElementCount > 0)
		{
			Status += " ";
			Status += std::to_string(ImportedPatch.IgnoredMeaningfulIdlessElementCount);
			Status += " had UI attributes; add ids to round-trip them.";
		}
	}

	void ParseRmlPatches(const FString& Source, FRuntimeUILayoutImportedPatch& OutPatch)
	{
		TArray<FString> OpenElementIds;
		size_t Search = 0;
		while (Search < Source.size())
		{
			const size_t TagStart = Source.find('<', Search);
			if (TagStart == FString::npos || TagStart + 1 >= Source.size())
			{
				break;
			}

			const size_t TagEnd = Source.find('>', TagStart + 1);
			if (TagEnd == FString::npos)
			{
				break;
			}

			const char Next = Source[TagStart + 1];
			if (Next == '!' || Next == '?')
			{
				Search = TagEnd + 1;
				continue;
			}
			if (Next == '/')
			{
				if (!OpenElementIds.empty())
				{
					OpenElementIds.pop_back();
				}
				Search = TagEnd + 1;
				continue;
			}

			const FString Tag = Source.substr(TagStart + 1, TagEnd - TagStart - 1);
			const FString TagName = ExtractTagName(Tag);
			const size_t LastContentChar = Tag.find_last_not_of(" \t\r\n");
			const bool bSelfClosing = LastContentChar != FString::npos && Tag[LastContentChar] == '/';

			FString Id;
			if (!ExtractTagAttribute(Tag, "id", Id) || Id.empty())
			{
				if (!IsRmlDocumentScaffoldTag(TagName))
				{
					++OutPatch.IgnoredIdlessElementCount;
					if (IsMeaningfulIdlessRmlElement(Tag, TagName))
					{
						++OutPatch.IgnoredMeaningfulIdlessElementCount;
					}
				}
				if (!bSelfClosing && !TagName.empty() && !IsVoidRmlTag(TagName))
				{
					OpenElementIds.push_back(FString());
				}
				Search = TagEnd + 1;
				continue;
			}

			const bool bNewImportedNode = OutPatch.RmlNodes.find(Id) == OutPatch.RmlNodes.end();
			FRuntimeUILayoutRmlPatch& NodePatch = OutPatch.RmlNodes[Id];
			if (bNewImportedNode)
			{
				FRuntimeUILayoutImportedPatch::FImportedRmlNode ImportedNode;
				ImportedNode.Id = Id;
				ImportedNode.ParentId = FindNearestOpenRmlId(OpenElementIds);
				OutPatch.RmlNodeOrder.push_back(ImportedNode);
			}
			else
			{
				AppendUniqueDuplicateRmlId(OutPatch.DuplicateRmlIds, Id);
			}

			FString Value;
			if (ExtractTagAttribute(Tag, "data-ui-name", Value))
			{
				NodePatch.DisplayName = Value;
				NodePatch.bHasDisplayName = true;
			}
			if (ExtractTagAttribute(Tag, "class", Value))
			{
				NodePatch.StyleClass = Value;
				NodePatch.bHasStyleClass = true;
			}
			if (ExtractTagAttribute(Tag, "data-action", Value) || ExtractTagAttribute(Tag, "action", Value))
			{
				NodePatch.Action = Value;
				NodePatch.bHasAction = true;
			}
			if (ExtractTagAttribute(Tag, "src", Value))
			{
				NodePatch.ImagePath = Value;
				NodePatch.bHasImagePath = true;
			}
			if (ExtractTagAttribute(Tag, "data-ui-type", Value) && ParseWidgetTypeName(Value, NodePatch.Type))
			{
				NodePatch.bHasType = true;
			}
			else if (ParseWidgetTypeName(TagName, NodePatch.Type))
			{
				NodePatch.bHasType = true;
			}
			if (ExtractTagAttribute(Tag, "data-ui-fit", Value) && ParseImageFitName(Value, NodePatch.ImageFit))
			{
				NodePatch.bHasImageFit = true;
			}

			if (!bSelfClosing && !TagName.empty())
			{
				const FString CloseNeedle = "</" + TagName + ">";
				const size_t CloseStart = Source.find(CloseNeedle, TagEnd + 1);
				if (CloseStart != FString::npos)
				{
					const FString Inner = Source.substr(TagEnd + 1, CloseStart - TagEnd - 1);
					if (Inner.find('<') == FString::npos)
					{
						NodePatch.Text = TrimCopy(DecodeXmlEntities(Inner));
						NodePatch.bHasText = true;
					}
				}
			}

			if (!bSelfClosing && !TagName.empty() && !IsVoidRmlTag(TagName))
			{
				OpenElementIds.push_back(Id);
			}

			Search = TagEnd + 1;
		}
	}

	FString StripCssComments(const FString& Source)
	{
		FString Result;
		size_t Search = 0;
		while (Search < Source.size())
		{
			const size_t CommentStart = Source.find("/*", Search);
			if (CommentStart == FString::npos)
			{
				Result += Source.substr(Search);
				break;
			}

			Result += Source.substr(Search, CommentStart - Search);
			const size_t CommentEnd = Source.find("*/", CommentStart + 2);
			if (CommentEnd == FString::npos)
			{
				break;
			}
			Search = CommentEnd + 2;
		}
		return Result;
	}

	bool ExtractCssProperty(const FString& Block, const char* Name, FString& OutValue)
	{
		if (!Name || !*Name)
		{
			return false;
		}

		const FString LowerBlock = ToLowerCopy(Block);
		const FString LowerName = ToLowerCopy(Name);
		size_t Search = 0;
		while (Search < LowerBlock.size())
		{
			const size_t Pos = LowerBlock.find(LowerName, Search);
			if (Pos == FString::npos)
			{
				return false;
			}

			const bool bValidBefore = Pos == 0
				|| (!std::isalnum(static_cast<unsigned char>(LowerBlock[Pos - 1]))
					&& LowerBlock[Pos - 1] != '-'
					&& LowerBlock[Pos - 1] != '_');
			size_t Cursor = Pos + LowerName.size();
			while (Cursor < LowerBlock.size() && std::isspace(static_cast<unsigned char>(LowerBlock[Cursor])))
			{
				++Cursor;
			}

			if (bValidBefore && Cursor < LowerBlock.size() && LowerBlock[Cursor] == ':')
			{
				const size_t ValueBegin = Cursor + 1;
				size_t ValueEnd = Block.find(';', ValueBegin);
				if (ValueEnd == FString::npos)
				{
					ValueEnd = Block.size();
				}
				OutValue = TrimCopy(Block.substr(ValueBegin, ValueEnd - ValueBegin));
				return true;
			}

			Search = Pos + LowerName.size();
		}
		return false;
	}

	bool ParseCssNumber(const FString& Value, float& OutValue)
	{
		const FString Trimmed = TrimCopy(Value);
		if (Trimmed.empty())
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

	bool ParseCssUrl(const FString& Value, FString& OutPath)
	{
		FString Trimmed = TrimCopy(Value);
		if (Trimmed.empty() || EqualsIgnoreCase(Trimmed, "none"))
		{
			return false;
		}

		const FString Lower = ToLowerCopy(Trimmed);
		if (Lower.rfind("url(", 0) == 0 && Trimmed.size() >= 5 && Trimmed.back() == ')')
		{
			Trimmed = TrimCopy(Trimmed.substr(4, Trimmed.size() - 5));
		}

		if (Trimmed.size() >= 2 &&
			((Trimmed.front() == '"' && Trimmed.back() == '"') ||
				(Trimmed.front() == '\'' && Trimmed.back() == '\'')))
		{
			Trimmed = Trimmed.substr(1, Trimmed.size() - 2);
		}

		OutPath = Trimmed;
		return !OutPath.empty();
	}

	bool ParseCssPercent(const FString& Value, float& OutValue)
	{
		FString Trimmed = TrimCopy(Value);
		if (Trimmed.empty() || Trimmed.back() != '%')
		{
			return false;
		}

		Trimmed.pop_back();
		return ParseCssNumber(Trimmed, OutValue);
	}

	TArray<float> ParseCssNumberList(const FString& Value)
	{
		TArray<float> Values;
		const char* Cursor = Value.c_str();
		while (*Cursor)
		{
			char* End = nullptr;
			const float Parsed = std::strtof(Cursor, &End);
			if (End != Cursor)
			{
				Values.push_back(Parsed);
				Cursor = End;
			}
			else
			{
				++Cursor;
			}
		}
		return Values;
	}

	bool ParseCssColor(const FString& Value, FVector4& OutColor)
	{
		const FString Trimmed = TrimCopy(Value);
		if (Trimmed.empty())
		{
			return false;
		}

		if (Trimmed[0] == '#' && (Trimmed.size() == 7 || Trimmed.size() == 9))
		{
			const unsigned int R = std::strtoul(Trimmed.substr(1, 2).c_str(), nullptr, 16);
			const unsigned int G = std::strtoul(Trimmed.substr(3, 2).c_str(), nullptr, 16);
			const unsigned int B = std::strtoul(Trimmed.substr(5, 2).c_str(), nullptr, 16);
			const unsigned int A = Trimmed.size() == 9 ? std::strtoul(Trimmed.substr(7, 2).c_str(), nullptr, 16) : 255u;
			OutColor = FVector4(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f);
			return true;
		}

		const TArray<float> Values = ParseCssNumberList(Trimmed);
		if (Values.size() < 3)
		{
			return false;
		}

		const float R = Values[0] > 1.0f ? Values[0] / 255.0f : Values[0];
		const float G = Values[1] > 1.0f ? Values[1] / 255.0f : Values[1];
		const float B = Values[2] > 1.0f ? Values[2] / 255.0f : Values[2];
		const float ASource = Values.size() > 3 ? Values[3] : 1.0f;
		const float A = ASource > 1.0f ? ASource / 255.0f : ASource;
		OutColor = FVector4(
			std::clamp(R, 0.0f, 1.0f),
			std::clamp(G, 0.0f, 1.0f),
			std::clamp(B, 0.0f, 1.0f),
			std::clamp(A, 0.0f, 1.0f));
		return true;
	}

	bool ParseCssBox(const FString& Value, FVector4& OutBox)
	{
		const TArray<float> Values = ParseCssNumberList(Value);
		if (Values.empty())
		{
			return false;
		}

		float Top = Values[0];
		float Right = Values[0];
		float Bottom = Values[0];
		float Left = Values[0];
		if (Values.size() == 2)
		{
			Right = Values[1];
			Left = Values[1];
		}
		else if (Values.size() == 3)
		{
			Right = Values[1];
			Bottom = Values[2];
			Left = Values[1];
		}
		else if (Values.size() >= 4)
		{
			Right = Values[1];
			Bottom = Values[2];
			Left = Values[3];
		}

		OutBox = FVector4(Left, Top, Right, Bottom);
		return true;
	}

	void ParseStyleBlock(const FString& Block, FRuntimeUILayoutStylePatch& OutStyle)
	{
		FString Value;
		if (ExtractCssProperty(Block, "left", Value) && ParseCssPercent(Value, OutStyle.LeftPercent))
		{
			OutStyle.bHasLeftPercent = true;
		}
		else if (ExtractCssProperty(Block, "left", Value) && ParseCssNumber(Value, OutStyle.Left))
		{
			OutStyle.bHasLeft = true;
		}
		if (ExtractCssProperty(Block, "top", Value) && ParseCssPercent(Value, OutStyle.TopPercent))
		{
			OutStyle.bHasTopPercent = true;
		}
		else if (ExtractCssProperty(Block, "top", Value) && ParseCssNumber(Value, OutStyle.Top))
		{
			OutStyle.bHasTop = true;
		}
		if (ExtractCssProperty(Block, "right", Value) && ParseCssNumber(Value, OutStyle.Right))
		{
			OutStyle.bHasRight = true;
		}
		if (ExtractCssProperty(Block, "bottom", Value) && ParseCssNumber(Value, OutStyle.Bottom))
		{
			OutStyle.bHasBottom = true;
		}
		if (ExtractCssProperty(Block, "width", Value) && ParseCssPercent(Value, OutStyle.WidthPercent))
		{
			OutStyle.bHasWidthPercent = true;
		}
		else if (ExtractCssProperty(Block, "width", Value) && ParseCssNumber(Value, OutStyle.Width))
		{
			OutStyle.bHasWidth = true;
		}
		if (ExtractCssProperty(Block, "height", Value) && ParseCssPercent(Value, OutStyle.HeightPercent))
		{
			OutStyle.bHasHeightPercent = true;
		}
		else if (ExtractCssProperty(Block, "height", Value) && ParseCssNumber(Value, OutStyle.Height))
		{
			OutStyle.bHasHeight = true;
		}
		if (ExtractCssProperty(Block, "display", Value))
		{
			OutStyle.Display = ToLowerCopy(TrimCopy(Value));
			OutStyle.bHasDisplay = true;
		}
		if (ExtractCssProperty(Block, "justify-content", Value))
		{
			OutStyle.JustifyContent = ToLowerCopy(TrimCopy(Value));
			OutStyle.bHasJustifyContent = true;
		}
		if (ExtractCssProperty(Block, "align-items", Value))
		{
			OutStyle.AlignItems = ToLowerCopy(TrimCopy(Value));
			OutStyle.bHasAlignItems = true;
		}
		if (ExtractCssProperty(Block, "opacity", Value) && ParseCssNumber(Value, OutStyle.Opacity))
		{
			OutStyle.Opacity = std::clamp(OutStyle.Opacity, 0.0f, 1.0f);
			OutStyle.bHasOpacity = true;
		}
		if (ExtractCssProperty(Block, "mask-image", Value) && ParseCssUrl(Value, OutStyle.MaskImagePath))
		{
			OutStyle.bHasMaskImagePath = true;
		}
		if ((ExtractCssProperty(Block, "background-color", Value) || ExtractCssProperty(Block, "background", Value))
			&& ParseCssColor(Value, OutStyle.BackgroundColor))
		{
			OutStyle.bHasBackgroundColor = true;
		}
		if (ExtractCssProperty(Block, "color", Value) && ParseCssColor(Value, OutStyle.TextColor))
		{
			OutStyle.bHasTextColor = true;
		}
		if (ExtractCssProperty(Block, "border-color", Value) && ParseCssColor(Value, OutStyle.BorderColor))
		{
			OutStyle.bHasBorderColor = true;
		}
		if (ExtractCssProperty(Block, "border-width", Value) && ParseCssBox(Value, OutStyle.BorderWidth))
		{
			OutStyle.bHasBorderWidth = true;
		}
		if (ExtractCssProperty(Block, "padding", Value) && ParseCssBox(Value, OutStyle.Padding))
		{
			OutStyle.bHasPadding = true;
		}
		if (ExtractCssProperty(Block, "border-radius", Value) && ParseCssNumber(Value, OutStyle.BorderRadius))
		{
			OutStyle.bHasBorderRadius = true;
		}
		if (ExtractCssProperty(Block, "font-size", Value) && ParseCssNumber(Value, OutStyle.FontSize))
		{
			OutStyle.bHasFontSize = true;
		}
		if (ExtractCssProperty(Block, "object-fit", Value) && ParseImageFitName(Value, OutStyle.ImageFit))
		{
			OutStyle.bHasImageFit = true;
		}
	}

	void ParseRcssPatches(const FString& Source, FRuntimeUILayoutImportedPatch& OutPatch)
	{
		const FString CleanSource = StripCssComments(Source);
		size_t Search = 0;
		while (Search < CleanSource.size())
		{
			const size_t OpenBrace = CleanSource.find('{', Search);
			if (OpenBrace == FString::npos)
			{
				break;
			}
			const size_t CloseBrace = CleanSource.find('}', OpenBrace + 1);
			if (CloseBrace == FString::npos)
			{
				break;
			}

			const FString Selector = TrimCopy(CleanSource.substr(Search, OpenBrace - Search));
			const FString Block = CleanSource.substr(OpenBrace + 1, CloseBrace - OpenBrace - 1);
			if (EqualsIgnoreCase(Selector, "body"))
			{
				float Width = 0.0f;
				float Height = 0.0f;
				FString Value;
				const bool bHasWidth = ExtractCssProperty(Block, "width", Value) && ParseCssNumber(Value, Width);
				const bool bHasHeight = ExtractCssProperty(Block, "height", Value) && ParseCssNumber(Value, Height);
				if (bHasWidth && bHasHeight)
				{
					OutPatch.CanvasSize = FVector2(Width, Height);
					OutPatch.bHasCanvasSize = true;
				}
			}
			else if (!Selector.empty() && Selector[0] == '#')
			{
				size_t IdEnd = 1;
				while (IdEnd < Selector.size()
					&& !std::isspace(static_cast<unsigned char>(Selector[IdEnd]))
					&& Selector[IdEnd] != ','
					&& Selector[IdEnd] != ':')
				{
					++IdEnd;
				}

				const FString Id = Selector.substr(1, IdEnd - 1);
				if (!Id.empty())
				{
					ParseStyleBlock(Block, OutPatch.Styles[Id]);
				}
			}

			Search = CloseBrace + 1;
		}
	}

	bool NearlyEqualFloat(float A, float B)
	{
		return std::abs(A - B) <= 0.001f;
	}

	void AssignStringIfChanged(FString& Target, const FString& Value, int32& ChangeCount)
	{
		if (Target != Value)
		{
			Target = Value;
			++ChangeCount;
		}
	}

	void AssignFloatIfChanged(float& Target, float Value, int32& ChangeCount)
	{
		if (!NearlyEqualFloat(Target, Value))
		{
			Target = Value;
			++ChangeCount;
		}
	}

	void AssignVector2IfChanged(FVector2& Target, const FVector2& Value, int32& ChangeCount)
	{
		if (!NearlyEqualFloat(Target.X, Value.X) || !NearlyEqualFloat(Target.Y, Value.Y))
		{
			Target = Value;
			++ChangeCount;
		}
	}

	void AssignVector4IfChanged(FVector4& Target, const FVector4& Value, int32& ChangeCount)
	{
		if (!NearlyEqualFloat(Target.X, Value.X)
			|| !NearlyEqualFloat(Target.Y, Value.Y)
			|| !NearlyEqualFloat(Target.Z, Value.Z)
			|| !NearlyEqualFloat(Target.W, Value.W))
		{
			Target = Value;
			++ChangeCount;
		}
	}

	template<typename TEnum>
	void AssignEnumIfChanged(TEnum& Target, TEnum Value, int32& ChangeCount)
	{
		if (Target != Value)
		{
			Target = Value;
			++ChangeCount;
		}
	}

	const FRuntimeUILayoutRmlPatch* FindRmlPatchForNode(
		const FRuntimeUILayoutImportedPatch& ImportedPatch,
		const FRuntimeUIWidgetNode& Node)
	{
		const auto ExactIt = ImportedPatch.RmlNodes.find(Node.Id);
		if (ExactIt != ImportedPatch.RmlNodes.end())
		{
			return &ExactIt->second;
		}

		const FString SafeId = ToCssIdForRuntimeUILayoutEditor(Node.Id);
		const auto SafeIt = ImportedPatch.RmlNodes.find(SafeId);
		return SafeIt != ImportedPatch.RmlNodes.end() ? &SafeIt->second : nullptr;
	}

	const FRuntimeUILayoutStylePatch* FindStylePatchForNode(
		const FRuntimeUILayoutImportedPatch& ImportedPatch,
		const FRuntimeUIWidgetNode& Node)
	{
		const auto ExactIt = ImportedPatch.Styles.find(Node.Id);
		if (ExactIt != ImportedPatch.Styles.end())
		{
			return &ExactIt->second;
		}

		const FString SafeId = ToCssIdForRuntimeUILayoutEditor(Node.Id);
		const auto SafeIt = ImportedPatch.Styles.find(SafeId);
		return SafeIt != ImportedPatch.Styles.end() ? &SafeIt->second : nullptr;
	}

	void RebuildRuntimeUILayoutChildren(TArray<FRuntimeUIWidgetNode>& Widgets)
	{
		for (FRuntimeUIWidgetNode& Node : Widgets)
		{
			Node.Children.clear();
		}

		for (int32 Index = 0; Index < static_cast<int32>(Widgets.size()); ++Index)
		{
			const int32 ParentIndex = Widgets[Index].ParentIndex;
			if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(Widgets.size()) && ParentIndex != Index)
			{
				Widgets[ParentIndex].Children.push_back(Index);
			}
			else if (Index == 0)
			{
				Widgets[Index].ParentIndex = -1;
			}
			else
			{
				Widgets[Index].ParentIndex = 0;
				if (!Widgets.empty())
				{
					Widgets[0].Children.push_back(Index);
				}
			}
		}
	}

	bool IsRuntimeUILayoutDescendant(
		const TArray<FRuntimeUIWidgetNode>& Widgets,
		int32 CandidateChildIndex,
		int32 CandidateAncestorIndex)
	{
		int32 Cursor = CandidateChildIndex;
		while (Cursor >= 0 && Cursor < static_cast<int32>(Widgets.size()))
		{
			if (Cursor == CandidateAncestorIndex)
			{
				return true;
			}
			Cursor = Widgets[Cursor].ParentIndex;
		}
		return false;
	}

	void ApplyRmlPatchToNode(FRuntimeUIWidgetNode& Node, const FRuntimeUILayoutRmlPatch& RmlPatch, bool bIsRoot, int32& ChangeCount)
	{
		if (RmlPatch.bHasType && !bIsRoot && RmlPatch.Type != ERuntimeUIWidgetType::Canvas)
		{
			AssignEnumIfChanged(Node.Type, RmlPatch.Type, ChangeCount);
		}
		if (RmlPatch.bHasDisplayName)
		{
			AssignStringIfChanged(Node.DisplayName, RmlPatch.DisplayName, ChangeCount);
		}
		if (RmlPatch.bHasStyleClass)
		{
			AssignStringIfChanged(Node.StyleClass, RmlPatch.StyleClass, ChangeCount);
		}
		if (RmlPatch.bHasAction)
		{
			AssignStringIfChanged(Node.OnClickAction, RmlPatch.Action, ChangeCount);
		}
		if (RmlPatch.bHasImagePath)
		{
			AssignStringIfChanged(Node.ImagePath, RmlPatch.ImagePath, ChangeCount);
		}
		if (RmlPatch.bHasText)
		{
			AssignStringIfChanged(Node.Text, RmlPatch.Text, ChangeCount);
		}
		if (RmlPatch.bHasImageFit)
		{
			AssignEnumIfChanged(Node.ImageFit, RmlPatch.ImageFit, ChangeCount);
		}
	}

	void ApplyStylePatchToNode(
		URuntimeUILayoutAsset* Layout,
		FRuntimeUIWidgetNode& Node,
		const FRuntimeUILayoutStylePatch& StylePatch,
		bool bIsRoot,
		int32& ChangeCount)
	{
		if (bIsRoot)
		{
			FVector2 CanvasSize = Layout->GetCanvasSize();
			if (StylePatch.bHasWidth)
			{
				CanvasSize.X = StylePatch.Width;
			}
			if (StylePatch.bHasHeight)
			{
				CanvasSize.Y = StylePatch.Height;
			}
			const FVector2 CurrentSize = Layout->GetCanvasSize();
			if (!NearlyEqualFloat(CurrentSize.X, CanvasSize.X) || !NearlyEqualFloat(CurrentSize.Y, CanvasSize.Y))
			{
				Layout->SetCanvasSize(CanvasSize);
				++ChangeCount;
			}
		}
		else
		{
			FVector2 Position = Node.Position;
			if (StylePatch.bHasLeft)
			{
				Position.X = StylePatch.Left;
				Node.bUseLeftPercent = false;
			}
			if (StylePatch.bHasTop)
			{
				Position.Y = StylePatch.Top;
				Node.bUseTopPercent = false;
			}
			AssignVector2IfChanged(Node.Position, Position, ChangeCount);

			FVector2 PositionPercent = Node.PositionPercent;
			if (StylePatch.bHasLeftPercent)
			{
				PositionPercent.X = StylePatch.LeftPercent;
				if (!Node.bUseLeftPercent)
				{
					Node.bUseLeftPercent = true;
					++ChangeCount;
				}
			}
			if (StylePatch.bHasTopPercent)
			{
				PositionPercent.Y = StylePatch.TopPercent;
				if (!Node.bUseTopPercent)
				{
					Node.bUseTopPercent = true;
					++ChangeCount;
				}
			}
			AssignVector2IfChanged(Node.PositionPercent, PositionPercent, ChangeCount);

			if (StylePatch.bHasRight)
			{
				AssignFloatIfChanged(Node.Right, StylePatch.Right, ChangeCount);
				if (!Node.bUseRight)
				{
					Node.bUseRight = true;
					++ChangeCount;
				}
			}
			if (StylePatch.bHasBottom)
			{
				AssignFloatIfChanged(Node.Bottom, StylePatch.Bottom, ChangeCount);
				if (!Node.bUseBottom)
				{
					Node.bUseBottom = true;
					++ChangeCount;
				}
			}

			FVector2 Size = Node.Size;
			if (StylePatch.bHasWidth)
			{
				Size.X = StylePatch.Width;
				Node.bUseWidthPercent = false;
			}
			if (StylePatch.bHasHeight)
			{
				Size.Y = StylePatch.Height;
				Node.bUseHeightPercent = false;
			}
			AssignVector2IfChanged(Node.Size, Size, ChangeCount);

			FVector2 SizePercent = Node.SizePercent;
			if (StylePatch.bHasWidthPercent)
			{
				SizePercent.X = StylePatch.WidthPercent;
				if (!Node.bUseWidthPercent)
				{
					Node.bUseWidthPercent = true;
					++ChangeCount;
				}
			}
			if (StylePatch.bHasHeightPercent)
			{
				SizePercent.Y = StylePatch.HeightPercent;
				if (!Node.bUseHeightPercent)
				{
					Node.bUseHeightPercent = true;
					++ChangeCount;
				}
			}
			AssignVector2IfChanged(Node.SizePercent, SizePercent, ChangeCount);
		}

		if (StylePatch.bHasDisplay)
		{
			const bool bShouldUseFlex = StylePatch.Display == "flex";
			if (Node.bUseFlexLayout != bShouldUseFlex)
			{
				Node.bUseFlexLayout = bShouldUseFlex;
				++ChangeCount;
			}
		}
		if (StylePatch.bHasJustifyContent)
		{
			AssignStringIfChanged(Node.JustifyContent, StylePatch.JustifyContent, ChangeCount);
		}
		if (StylePatch.bHasAlignItems)
		{
			AssignStringIfChanged(Node.AlignItems, StylePatch.AlignItems, ChangeCount);
		}

		if (StylePatch.bHasOpacity)
		{
			AssignFloatIfChanged(Node.Opacity, StylePatch.Opacity, ChangeCount);
		}
		if (StylePatch.bHasMaskImagePath)
		{
			AssignStringIfChanged(Node.MaskImagePath, StylePatch.MaskImagePath, ChangeCount);
		}
		if (StylePatch.bHasBackgroundColor)
		{
			AssignVector4IfChanged(Node.BackgroundColor, StylePatch.BackgroundColor, ChangeCount);
		}
		if (StylePatch.bHasTextColor)
		{
			AssignVector4IfChanged(Node.TextColor, StylePatch.TextColor, ChangeCount);
		}
		if (StylePatch.bHasBorderColor)
		{
			AssignVector4IfChanged(Node.BorderColor, StylePatch.BorderColor, ChangeCount);
		}
		if (StylePatch.bHasBorderWidth)
		{
			AssignVector4IfChanged(Node.BorderWidth, StylePatch.BorderWidth, ChangeCount);
		}
		if (StylePatch.bHasPadding)
		{
			AssignVector4IfChanged(Node.Padding, StylePatch.Padding, ChangeCount);
		}
		if (StylePatch.bHasBorderRadius)
		{
			AssignFloatIfChanged(Node.BorderRadius, StylePatch.BorderRadius, ChangeCount);
		}
		if (StylePatch.bHasFontSize)
		{
			AssignFloatIfChanged(Node.FontSize, StylePatch.FontSize, ChangeCount);
		}
		if (StylePatch.bHasImageFit)
		{
			AssignEnumIfChanged(Node.ImageFit, StylePatch.ImageFit, ChangeCount);
		}
	}
}

bool FRuntimeUILayoutEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<URuntimeUILayoutAsset>();
}

void FRuntimeUILayoutEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);
	SelectedWidgetIndex = 0;
	DraggingWidgetIndex = -1;
	DragGrabOffset = FVector2(0.0f, 0.0f);
	CanvasPreviewZoom = 1.0f;
	bDragMovedSinceCommit = false;
	bPendingStructuralImportConfirmation = false;
	PendingStructuralImportSignature.clear();
	LastStatus.clear();
	bLastOperationFailed = false;
	CaptureInitialUndoSnapshot(GetLayout());
}

URuntimeUILayoutAsset* FRuntimeUILayoutEditorWidget::GetLayout() const
{
	return EditedObject && EditedObject->IsA<URuntimeUILayoutAsset>()
		? static_cast<URuntimeUILayoutAsset*>(EditedObject)
		: nullptr;
}

FString FRuntimeUILayoutEditorWidget::GetDocumentTitle() const
{
	const URuntimeUILayoutAsset* Layout = GetLayout();
	FString Title = Layout ? GetPathStem(Layout->GetAssetPath()) : FString("Runtime UI Layout");
	if (IsDirty())
	{
		Title += "*";
	}
	return Title;
}

FString FRuntimeUILayoutEditorWidget::GetDocumentPayloadId() const
{
	const URuntimeUILayoutAsset* Layout = GetLayout();
	if (Layout && !Layout->GetAssetPath().empty())
	{
		return Layout->GetAssetPath();
	}
	return FAssetEditorWidget::GetDocumentPayloadId();
}

void FRuntimeUILayoutEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	URuntimeUILayoutAsset* Layout = GetLayout();
	if (!Layout)
	{
		ImGui::TextUnformatted("Runtime UI Layout is not loaded.");
		return;
	}

	RenderToolbar(Layout);
	HandleUndoRedoShortcuts(Layout);
	ImGui::Separator();

	const float AvailableWidth = ImGui::GetContentRegionAvail().x;
	const float LeftWidth = (std::max)(220.0f, AvailableWidth * 0.22f);
	const float RightWidth = (std::max)(300.0f, AvailableWidth * 0.30f);

	ImGui::BeginChild("RuntimeUILayoutHierarchy", ImVec2(LeftWidth, 0.0f), true);
	RenderHierarchy(Layout);
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild(
		"RuntimeUILayoutCanvas",
		ImVec2((std::max)(240.0f, AvailableWidth - LeftWidth - RightWidth - 16.0f), 0.0f),
		true,
		ImGuiWindowFlags_HorizontalScrollbar);
	RenderCanvasPreview(Layout);
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("RuntimeUILayoutDetails", ImVec2(0.0f, 0.0f), true);
	RenderDetails(Layout);
	ImGui::EndChild();
}

void FRuntimeUILayoutEditorWidget::RenderToolbar(URuntimeUILayoutAsset* Layout)
{
	if (ImGui::Button(IsDirty() ? "Save *" : "Save"))
	{
		SaveAndExport(Layout, false);
	}
	ImGui::SameLine();
	if (!EditorEngine || !EditorEngine->GetUndoSystem().CanUndo())
	{
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Undo"))
	{
		UndoLayoutEdit(Layout);
	}
	if (!EditorEngine || !EditorEngine->GetUndoSystem().CanUndo())
	{
		ImGui::EndDisabled();
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Undo (Ctrl+Z)");
	}
	ImGui::SameLine();
	if (!EditorEngine || !EditorEngine->GetUndoSystem().CanRedo())
	{
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Redo"))
	{
		RedoLayoutEdit(Layout);
	}
	if (!EditorEngine || !EditorEngine->GetUndoSystem().CanRedo())
	{
		ImGui::EndDisabled();
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Redo (Ctrl+Y)");
	}
	ImGui::SameLine();
	if (ImGui::Button("Export RML"))
	{
		EnsureGeneratedPaths(Layout);
		FString Error;
		if (Layout->ExportRmlAndRcss(Layout->GetGeneratedRmlPath(), Layout->GetGeneratedRcssPath(), &Error))
		{
			LastStatus = "Exported generated RML/RCSS.";
			bLastOperationFailed = false;
			bPendingStructuralImportConfirmation = false;
			PendingStructuralImportSignature.clear();
		}
		else
		{
			LastStatus = Error.empty() ? FString("Failed to export generated RML/RCSS.") : Error;
			bLastOperationFailed = true;
		}
	}
	ImGui::SameLine();
	const char* ImportButtonLabel = bPendingStructuralImportConfirmation ? "Confirm Import" : "Import RML";
	if (ImGui::Button(ImportButtonLabel))
	{
		ImportGeneratedRmlAndRcss(Layout, bPendingStructuralImportConfirmation);
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip(bPendingStructuralImportConfirmation
			? "Apply the pending structural RML import."
			: "Reconcile existing layout nodes from generated RML/RCSS by id.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Open Generated RML"))
	{
		SaveAndExport(Layout, true);
	}

	ImGui::SameLine();
	ImGui::TextDisabled("%s", IsDirty() ? "Dirty" : "Saved");

	ImGui::SameLine();
	if (!LastStatus.empty())
	{
		const ImVec4 Color = bLastOperationFailed
			? ImVec4(1.0f, 0.36f, 0.28f, 1.0f)
			: ImVec4(0.38f, 0.82f, 0.54f, 1.0f);
		ImGui::TextColored(Color, "%s", LastStatus.c_str());
	}
}

void FRuntimeUILayoutEditorWidget::RenderHierarchy(URuntimeUILayoutAsset* Layout)
{
	ImGui::TextUnformatted("Hierarchy");
	ImGui::Separator();

	if (ImGui::Button("+ Panel"))
	{
		AddWidget(Layout, static_cast<int32>(ERuntimeUIWidgetType::Panel));
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Text"))
	{
		AddWidget(Layout, static_cast<int32>(ERuntimeUIWidgetType::Text));
	}
	if (ImGui::Button("+ Image"))
	{
		AddWidget(Layout, static_cast<int32>(ERuntimeUIWidgetType::Image));
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Button"))
	{
		AddWidget(Layout, static_cast<int32>(ERuntimeUIWidgetType::Button));
	}

	const bool bCanDelete = SelectedWidgetIndex > 0 && Layout->GetWidget(SelectedWidgetIndex);
	if (!bCanDelete)
	{
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Delete Selected") && bCanDelete)
	{
		if (Layout->RemoveWidget(SelectedWidgetIndex))
		{
			SelectedWidgetIndex = 0;
			DraggingWidgetIndex = -1;
			bDragMovedSinceCommit = false;
			CommitLayoutEdit(Layout);
		}
	}
	if (!bCanDelete)
	{
		ImGui::EndDisabled();
	}

	ImGui::Separator();
	if (!Layout->GetWidgets().empty())
	{
		RenderHierarchyNode(Layout, 0);
	}
}

void FRuntimeUILayoutEditorWidget::RenderHierarchyNode(URuntimeUILayoutAsset* Layout, int32 WidgetIndex)
{
	const FRuntimeUIWidgetNode* Node = Layout->GetWidget(WidgetIndex);
	if (!Node)
	{
		return;
	}

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_SpanFullWidth
		| ImGuiTreeNodeFlags_DefaultOpen;
	if (SelectedWidgetIndex == WidgetIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (Node->Children.empty())
	{
		Flags |= ImGuiTreeNodeFlags_Leaf;
	}

	const FString Label = Node->DisplayName.empty() ? Node->Id : Node->DisplayName;
	const FString TreeId = Label + "###RuntimeUILayoutTree_" + std::to_string(WidgetIndex);
	const bool bOpen = ImGui::TreeNodeEx(TreeId.c_str(), Flags);
	if (ImGui::IsItemClicked())
	{
		SelectedWidgetIndex = WidgetIndex;
	}

	if (bOpen)
	{
		for (const int32 ChildIndex : Node->Children)
		{
			RenderHierarchyNode(Layout, ChildIndex);
		}
		ImGui::TreePop();
	}
}

void FRuntimeUILayoutEditorWidget::RenderCanvasPreview(URuntimeUILayoutAsset* Layout)
{
	ImGui::TextUnformatted("Layout Preview");
	ImGui::SameLine();
	if (ImGui::SmallButton("Fit"))
	{
		CanvasPreviewZoom = 1.0f;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("-"))
	{
		CanvasPreviewZoom = ClampPreviewZoom(CanvasPreviewZoom / 1.25f);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("+"))
	{
		CanvasPreviewZoom = ClampPreviewZoom(CanvasPreviewZoom * 1.25f);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("Zoom %.0f%%", CanvasPreviewZoom * 100.0f);
	ImGui::Separator();

	const FVector2 CanvasSize = Layout->GetCanvasSize();
	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const float ScaleX = Available.x > 24.0f ? (Available.x - 24.0f) / CanvasSize.X : 0.1f;
	const float ScaleY = Available.y > 48.0f ? (Available.y - 48.0f) / CanvasSize.Y : 0.1f;
	const float FitScale = (std::max)(0.05f, (std::min)(ScaleX, ScaleY));
	const float Scale = (std::max)(0.01f, FitScale * CanvasPreviewZoom);
	const ImVec2 PreviewSize(CanvasSize.X * Scale, CanvasSize.Y * Scale);
	const ImVec2 Origin = ImGui::GetCursorScreenPos();

	ImGui::InvisibleButton("RuntimeUILayoutPreviewHitArea", PreviewSize);
	const bool bPreviewHovered = ImGui::IsItemHovered();
	const bool bPreviewFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	const ImGuiIO& IO = ImGui::GetIO();
	const ImVec2 Mouse = IO.MousePos;
	const FVector2 MouseLocal((Mouse.x - Origin.x) / Scale, (Mouse.y - Origin.y) / Scale);

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(Origin, ImVec2(Origin.x + PreviewSize.x, Origin.y + PreviewSize.y), IM_COL32(18, 22, 28, 255));
	DrawList->AddRect(Origin, ImVec2(Origin.x + PreviewSize.x, Origin.y + PreviewSize.y), IM_COL32(80, 92, 112, 255));

	const TArray<FRuntimeUIWidgetNode>& Widgets = Layout->GetWidgets();
	bool bShowVerticalCenterGuide = false;
	bool bShowHorizontalCenterGuide = false;
	float VerticalCenterGuideX = 0.0f;
	float HorizontalCenterGuideY = 0.0f;
	std::function<FVector2(int32)> GetNodeSize = [&](int32 Index) -> FVector2
	{
		return ResolveRuntimeUILayoutNodeSize(Widgets, CanvasSize, Index);
	};
	std::function<FVector2(int32)> GetGlobalPosition = [&](int32 Index) -> FVector2
	{
		return ResolveRuntimeUILayoutNodePosition(Widgets, CanvasSize, Index);
	};
	auto GetNodeRect = [&](int32 Index, ImVec2& OutMin, ImVec2& OutMax)
	{
		const FVector2 Pos = GetGlobalPosition(Index);
		const FVector2 NodeSize = GetNodeSize(Index);
		OutMin = ImVec2(Origin.x + Pos.X * Scale, Origin.y + Pos.Y * Scale);
		OutMax = ImVec2(OutMin.x + NodeSize.X * Scale, OutMin.y + NodeSize.Y * Scale);
	};
	auto GetParentClipRect = [&](int32 Index, ImVec2& OutMin, ImVec2& OutMax) -> bool
	{
		OutMin = Origin;
		OutMax = ImVec2(Origin.x + PreviewSize.x, Origin.y + PreviewSize.y);
		if (Index < 0 || Index >= static_cast<int32>(Widgets.size()))
		{
			return false;
		}

		int32 ParentIndex = Widgets[Index].ParentIndex;
		int32 Guard = 0;
		while (ParentIndex >= 0
			&& ParentIndex < static_cast<int32>(Widgets.size())
			&& Guard++ < static_cast<int32>(Widgets.size()))
		{
			ImVec2 ParentMin;
			ImVec2 ParentMax;
			GetNodeRect(ParentIndex, ParentMin, ParentMax);
			if (!IntersectRuntimeUILayoutPreviewClip(OutMin, OutMax, ParentMin, ParentMax))
			{
				return false;
			}

			const int32 NextParentIndex = Widgets[ParentIndex].ParentIndex;
			if (NextParentIndex == ParentIndex)
			{
				break;
			}
			ParentIndex = NextParentIndex;
		}
		return OutMin.x < OutMax.x && OutMin.y < OutMax.y;
	};
	for (int32 Index = 1; Index < static_cast<int32>(Widgets.size()); ++Index)
	{
		const FRuntimeUIWidgetNode& Node = Widgets[Index];
		if (!Node.bVisible)
		{
			continue;
		}

		ImVec2 ParentClipMin;
		ImVec2 ParentClipMax;
		if (!GetParentClipRect(Index, ParentClipMin, ParentClipMax))
		{
			continue;
		}

		ImVec2 Min;
		ImVec2 Max;
		GetNodeRect(Index, Min, Max);
		const ImU32 Fill = ToImColor(Node.BackgroundColor);
		const ImU32 Border = SelectedWidgetIndex == Index ? IM_COL32(255, 214, 96, 255) : ToImColor(Node.BorderColor);
		DrawList->PushClipRect(ParentClipMin, ParentClipMax, true);
		DrawList->AddRectFilled(Min, Max, Fill, Node.BorderRadius * Scale);
		bool bDrewImage = false;
		bool bMissingImage = false;
		if (Node.Type == ERuntimeUIWidgetType::Image && !Node.ImagePath.empty())
		{
			const FString ImagePath = ResolveRuntimeUILayoutPreviewImagePath(Node.ImagePath);
			if (ID3D11ShaderResourceView* ImageSRV = FEditorTextureManager::Get().GetOrLoadThumbnail(ImagePath))
			{
				ImVec2 ImageMin = Min;
				ImVec2 ImageMax = Max;
				GetRuntimeUILayoutPreviewImageFitRect(
					Node.ImageFit,
					Min,
					Max,
					GetRuntimeUILayoutPreviewImageSize(ImageSRV),
					ImageMin,
					ImageMax);
				ImVec2 ImageClipMin = ParentClipMin;
				ImVec2 ImageClipMax = ParentClipMax;
				if (IntersectRuntimeUILayoutPreviewClip(ImageClipMin, ImageClipMax, Min, Max))
				{
					DrawList->PushClipRect(ImageClipMin, ImageClipMax, true);
					DrawList->AddImage(reinterpret_cast<ImTextureID>(ImageSRV), ImageMin, ImageMax);
					DrawList->PopClipRect();
					bDrewImage = true;
				}
			}
			else
			{
				bMissingImage = true;
			}
		}
		else if (Node.Type == ERuntimeUIWidgetType::Image)
		{
			bMissingImage = true;
		}
		DrawList->AddRect(Min, Max, Border, Node.BorderRadius * Scale, 0, SelectedWidgetIndex == Index ? 2.0f : 1.0f);

		const FString Label = Node.Text.empty() ? Node.DisplayName : Node.Text;
		if (bMissingImage)
		{
			DrawList->AddLine(Min, Max, IM_COL32(255, 78, 78, 210), 1.5f);
			DrawList->AddLine(ImVec2(Max.x, Min.y), ImVec2(Min.x, Max.y), IM_COL32(255, 78, 78, 210), 1.5f);
			DrawList->AddText(ImVec2(Min.x + 6.0f, Min.y + 6.0f), IM_COL32(255, 108, 108, 255), "Missing image");
		}
		if (!bDrewImage && !bMissingImage && !Label.empty())
		{
			DrawList->AddText(ImVec2(Min.x + 6.0f, Min.y + 6.0f), ToImColor(Node.TextColor), Label.c_str());
		}
		DrawList->PopClipRect();
	}

	if (bPreviewHovered && IO.KeyCtrl && IO.MouseWheel != 0.0f)
	{
		const float ZoomFactor = IO.MouseWheel > 0.0f ? 1.15f : (1.0f / 1.15f);
		CanvasPreviewZoom = ClampPreviewZoom(CanvasPreviewZoom * ZoomFactor);
	}

	if (bPreviewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		int32 HitIndex = 0;
		for (int32 Index = static_cast<int32>(Widgets.size()) - 1; Index >= 1; --Index)
		{
			const FRuntimeUIWidgetNode& Node = Widgets[Index];
			if (!Node.bVisible)
			{
				continue;
			}
			const FVector2 Pos = GetGlobalPosition(Index);
			const FVector2 NodeSize = GetNodeSize(Index);
			if (IsPointInsideRect(MouseLocal, Pos, FVector2(Pos.X + NodeSize.X, Pos.Y + NodeSize.Y)))
			{
				HitIndex = Index;
				break;
			}
		}
		SelectedWidgetIndex = HitIndex;
		DraggingWidgetIndex = HitIndex > 0 ? HitIndex : -1;
		bDragMovedSinceCommit = false;
		if (DraggingWidgetIndex > 0)
		{
			const FVector2 HitPosition = GetGlobalPosition(DraggingWidgetIndex);
			DragGrabOffset = FVector2(MouseLocal.X - HitPosition.X, MouseLocal.Y - HitPosition.Y);
		}
	}

	if (DraggingWidgetIndex > 0)
	{
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (bDragMovedSinceCommit)
			{
				CommitLayoutEdit(Layout);
			}
			DraggingWidgetIndex = -1;
			bDragMovedSinceCommit = false;
		}
		else if (FRuntimeUIWidgetNode* DragNode = Layout->GetMutableWidget(DraggingWidgetIndex))
		{
			const FVector2 ParentGlobal = DragNode->ParentIndex >= 0
				? GetGlobalPosition(DragNode->ParentIndex)
				: FVector2(0.0f, 0.0f);
			const FVector2 ParentSize = DragNode->ParentIndex >= 0
				? GetNodeSize(DragNode->ParentIndex)
				: CanvasSize;
			const FVector2 DragNodeSize = GetNodeSize(DraggingWidgetIndex);
			const float SnapThreshold = (std::max)(4.0f, 8.0f / Scale);
			bool bSnappedX = false;
			bool bSnappedY = false;
			FVector2 NewPosition(
				MouseLocal.X - DragGrabOffset.X - ParentGlobal.X,
				MouseLocal.Y - DragGrabOffset.Y - ParentGlobal.Y);
			float BestSnapDistanceX = SnapThreshold + 1.0f;
			float BestSnapDistanceY = SnapThreshold + 1.0f;
			float SnappedX = NewPosition.X;
			float SnappedY = NewPosition.Y;
			float SnapGuideX = ParentGlobal.X + ParentSize.X * 0.5f;
			float SnapGuideY = ParentGlobal.Y + ParentSize.Y * 0.5f;
			bSnappedX |= TrySnapRuntimeUILayoutPreviewValue(
				NewPosition.X,
				0.0f,
				ParentGlobal.X,
				SnapThreshold,
				BestSnapDistanceX,
				SnappedX,
				SnapGuideX);
			bSnappedX |= TrySnapRuntimeUILayoutPreviewValue(
				NewPosition.X,
				(ParentSize.X - DragNodeSize.X) * 0.5f,
				ParentGlobal.X + ParentSize.X * 0.5f,
				SnapThreshold,
				BestSnapDistanceX,
				SnappedX,
				SnapGuideX);
			bSnappedX |= TrySnapRuntimeUILayoutPreviewValue(
				NewPosition.X,
				ParentSize.X - DragNodeSize.X,
				ParentGlobal.X + ParentSize.X,
				SnapThreshold,
				BestSnapDistanceX,
				SnappedX,
				SnapGuideX);
			bSnappedY |= TrySnapRuntimeUILayoutPreviewValue(
				NewPosition.Y,
				0.0f,
				ParentGlobal.Y,
				SnapThreshold,
				BestSnapDistanceY,
				SnappedY,
				SnapGuideY);
			bSnappedY |= TrySnapRuntimeUILayoutPreviewValue(
				NewPosition.Y,
				(ParentSize.Y - DragNodeSize.Y) * 0.5f,
				ParentGlobal.Y + ParentSize.Y * 0.5f,
				SnapThreshold,
				BestSnapDistanceY,
				SnappedY,
				SnapGuideY);
			bSnappedY |= TrySnapRuntimeUILayoutPreviewValue(
				NewPosition.Y,
				ParentSize.Y - DragNodeSize.Y,
				ParentGlobal.Y + ParentSize.Y,
				SnapThreshold,
				BestSnapDistanceY,
				SnappedY,
				SnapGuideY);
			NewPosition.X = SnappedX;
			NewPosition.Y = SnappedY;
			if (bSnappedX)
			{
				bShowVerticalCenterGuide = true;
				VerticalCenterGuideX = SnapGuideX;
			}
			if (bSnappedY)
			{
				bShowHorizontalCenterGuide = true;
				HorizontalCenterGuideY = SnapGuideY;
			}
			NewPosition = SnapRuntimeUILayoutPixelPosition(NewPosition);
			const FVector2 CurrentLocalPosition = ResolveRuntimeUILayoutNodeLocalPosition(Widgets, CanvasSize, DraggingWidgetIndex);
			if (std::abs(NewPosition.X - CurrentLocalPosition.X) > 0.01f
				|| std::abs(NewPosition.Y - CurrentLocalPosition.Y) > 0.01f)
			{
				ApplyRuntimeUILayoutNodeLocalPosition(*DragNode, ParentSize, DragNodeSize, NewPosition);
				bDragMovedSinceCommit = true;
				MarkLayoutDirty();
			}
		}
	}

	if (bShowVerticalCenterGuide)
	{
		const float X = Origin.x + VerticalCenterGuideX * Scale;
		DrawList->AddLine(ImVec2(X, Origin.y), ImVec2(X, Origin.y + PreviewSize.y), IM_COL32(72, 186, 255, 210), 1.5f);
	}
	if (bShowHorizontalCenterGuide)
	{
		const float Y = Origin.y + HorizontalCenterGuideY * Scale;
		DrawList->AddLine(ImVec2(Origin.x, Y), ImVec2(Origin.x + PreviewSize.x, Y), IM_COL32(72, 186, 255, 210), 1.5f);
	}

	if (SelectedWidgetIndex > 0 && bPreviewFocused && !IO.WantTextInput)
	{
		FVector2 NudgeDelta(0.0f, 0.0f);
		const float Step = IO.KeyShift ? 10.0f : 1.0f;
		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
		{
			NudgeDelta.X -= Step;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
		{
			NudgeDelta.X += Step;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
		{
			NudgeDelta.Y -= Step;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
		{
			NudgeDelta.Y += Step;
		}

		if ((std::abs)(NudgeDelta.X) > 0.0f || (std::abs)(NudgeDelta.Y) > 0.0f)
		{
			if (FRuntimeUIWidgetNode* Node = Layout->GetMutableWidget(SelectedWidgetIndex))
			{
				const FVector2 ParentSize = Node->ParentIndex >= 0 ? GetNodeSize(Node->ParentIndex) : CanvasSize;
				const FVector2 NodeSize = GetNodeSize(SelectedWidgetIndex);
				const FVector2 LocalPosition = ResolveRuntimeUILayoutNodeLocalPosition(Widgets, CanvasSize, SelectedWidgetIndex);
				ApplyRuntimeUILayoutNodeLocalPosition(
					*Node,
					ParentSize,
					NodeSize,
					FVector2(LocalPosition.X + NudgeDelta.X, LocalPosition.Y + NudgeDelta.Y));
				CommitLayoutEdit(Layout);
			}
		}
	}

	const ImU32 CanvasOriginColor = IM_COL32(255, 108, 108, 220);
	DrawList->AddLine(ImVec2(Origin.x - 7.0f, Origin.y), ImVec2(Origin.x + 7.0f, Origin.y), CanvasOriginColor, 1.5f);
	DrawList->AddLine(ImVec2(Origin.x, Origin.y - 7.0f), ImVec2(Origin.x, Origin.y + 7.0f), CanvasOriginColor, 1.5f);
	DrawList->AddText(ImVec2(Origin.x + 8.0f, Origin.y + 6.0f), CanvasOriginColor, "0,0");
	if (SelectedWidgetIndex > 0)
	{
		if (const FRuntimeUIWidgetNode* SelectedNode = Layout->GetWidget(SelectedWidgetIndex))
		{
			const FVector2 ParentOrigin = SelectedNode->ParentIndex >= 0
				? GetGlobalPosition(SelectedNode->ParentIndex)
				: FVector2(0.0f, 0.0f);
			const ImVec2 ParentOriginPos(Origin.x + ParentOrigin.X * Scale, Origin.y + ParentOrigin.Y * Scale);
			const ImU32 ParentOriginColor = IM_COL32(96, 255, 178, 220);
			DrawList->AddCircleFilled(ParentOriginPos, 3.0f, ParentOriginColor);
			DrawList->AddLine(ImVec2(ParentOriginPos.x - 9.0f, ParentOriginPos.y), ImVec2(ParentOriginPos.x + 9.0f, ParentOriginPos.y), ParentOriginColor, 1.25f);
			DrawList->AddLine(ImVec2(ParentOriginPos.x, ParentOriginPos.y - 9.0f), ImVec2(ParentOriginPos.x, ParentOriginPos.y + 9.0f), ParentOriginColor, 1.25f);
		}
	}

	ImGui::TextDisabled("Canvas %.0fx%.0f, fit %.2f, scale %.2f", CanvasSize.X, CanvasSize.Y, FitScale, Scale);
	if (const FRuntimeUIWidgetNode* SelectedNode = Layout->GetWidget(SelectedWidgetIndex))
	{
		const FVector2 SelectedLocalPosition = ResolveRuntimeUILayoutNodeLocalPosition(Widgets, CanvasSize, SelectedWidgetIndex);
		const FVector2 SelectedSize = GetNodeSize(SelectedWidgetIndex);
		ImGui::TextDisabled(
			"Selected %s  Pos %.0f, %.0f  Size %.0f, %.0f",
			SelectedNode->DisplayName.empty() ? SelectedNode->Id.c_str() : SelectedNode->DisplayName.c_str(),
			SelectedLocalPosition.X,
			SelectedLocalPosition.Y,
			SelectedSize.X,
			SelectedSize.Y);
	}
}

void FRuntimeUILayoutEditorWidget::RenderDetails(URuntimeUILayoutAsset* Layout)
{
	ImGui::TextUnformatted("Details");
	ImGui::Separator();

	FRuntimeUIWidgetNode* Node = Layout->GetMutableWidget(SelectedWidgetIndex);
	if (!Node)
	{
		ImGui::TextUnformatted("No widget selected.");
		return;
	}

	bool bChanged = false;
	bool bLiveLayoutChanged = false;

	ImGui::TextDisabled("%s", GetWidgetTypeLabel(Node->Type));
	if (SelectedWidgetIndex > 0)
	{
		int32 TypeIndex = static_cast<int32>(Node->Type);
		const char* Items[] = { "Canvas", "Panel", "Text", "Image", "Button" };
		if (ImGui::Combo("Type", &TypeIndex, Items, IM_ARRAYSIZE(Items)) && TypeIndex > 0)
		{
			Node->Type = static_cast<ERuntimeUIWidgetType>(TypeIndex);
			bChanged = true;
		}
	}

	bChanged |= EditString("Id", Node->Id);
	bChanged |= EditString("Name", Node->DisplayName);
	if (SelectedWidgetIndex == 0)
	{
		FVector2 CanvasSize = Layout->GetCanvasSize();
		if (EditVector2("Canvas Size", CanvasSize))
		{
			Layout->SetCanvasSize(CanvasSize);
			bChanged = true;
		}
	}
	else
	{
		const TArray<FRuntimeUIWidgetNode>& Widgets = Layout->GetWidgets();
		std::function<FVector2(int32)> ResolveNodeSize = [&](int32 Index) -> FVector2
		{
			return ResolveRuntimeUILayoutNodeSize(Widgets, Layout->GetCanvasSize(), Index);
		};
		std::function<FVector2(int32)> ResolveNodePosition = [&](int32 Index) -> FVector2
		{
			return ResolveRuntimeUILayoutNodePosition(Widgets, Layout->GetCanvasSize(), Index);
		};
		std::function<FVector2(int32)> ResolveNodeLocalPosition = [&](int32 Index) -> FVector2
		{
			return ResolveRuntimeUILayoutNodeLocalPosition(Widgets, Layout->GetCanvasSize(), Index);
		};
		auto ApplyNodeLocalPosition = [&](const FVector2& LocalPosition)
		{
			const FVector2 ParentSize = Node->ParentIndex >= 0 ? ResolveNodeSize(Node->ParentIndex) : Layout->GetCanvasSize();
			const FVector2 CurrentSize = ResolveNodeSize(SelectedWidgetIndex);
			ApplyRuntimeUILayoutNodeLocalPosition(*Node, ParentSize, CurrentSize, LocalPosition);
		};

		const FVector2 CurrentLocalPosition = ResolveNodeLocalPosition(SelectedWidgetIndex);
		int PositionValues[2] = {
			static_cast<int>(SnapRuntimeUILayoutPixelValue(CurrentLocalPosition.X)),
			static_cast<int>(SnapRuntimeUILayoutPixelValue(CurrentLocalPosition.Y))
		};
		if (ImGui::InputInt2("Position", PositionValues))
		{
			const FVector2 ParentSize = Node->ParentIndex >= 0 ? ResolveNodeSize(Node->ParentIndex) : Layout->GetCanvasSize();
			const FVector2 CurrentSize = ResolveNodeSize(SelectedWidgetIndex);
			ApplyRuntimeUILayoutNodeLocalPosition(
				*Node,
				ParentSize,
				CurrentSize,
				FVector2(static_cast<float>(PositionValues[0]), static_cast<float>(PositionValues[1])));
			bLiveLayoutChanged = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			bChanged = true;
		}

		bChanged |= ImGui::Checkbox("Lock Aspect Ratio", &Node->bLockAspectRatio);
		const FVector2 OldSize = Node->Size;
		bool bSizeChanged = false;
		float Width = Node->Size.X;
		float Height = Node->Size.Y;
		if (ImGui::InputFloat("Width", &Width, 1.0f, 8.0f, "%.2f"))
		{
			Node->Size.X = (std::max)(1.0f, Width);
			if (Node->bLockAspectRatio && OldSize.X > 0.001f && OldSize.Y > 0.001f)
			{
				Node->Size.Y = Node->Size.X * (OldSize.Y / OldSize.X);
			}
			bSizeChanged = true;
			bLiveLayoutChanged = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			bChanged = true;
		}
		if (ImGui::InputFloat("Height", &Height, 1.0f, 8.0f, "%.2f"))
		{
			Node->Size.Y = (std::max)(1.0f, Height);
			if (Node->bLockAspectRatio && OldSize.X > 0.001f && OldSize.Y > 0.001f)
			{
				Node->Size.X = Node->Size.Y * (OldSize.X / OldSize.Y);
			}
			bSizeChanged = true;
			bLiveLayoutChanged = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			bChanged = true;
		}
		if (bSizeChanged)
		{
			Node->bUseWidthPercent = false;
			Node->bUseHeightPercent = false;
		}

		const FVector2 ResolvedPosition = ResolveNodePosition(SelectedWidgetIndex);
		const FVector2 ResolvedSize = ResolveNodeSize(SelectedWidgetIndex);
		ImGui::TextDisabled("Resolved Pos %.0f, %.0f  Size %.0f, %.0f", ResolvedPosition.X, ResolvedPosition.Y, ResolvedSize.X, ResolvedSize.Y);

		if (ImGui::Button("Center H"))
		{
			const FVector2 ParentSize = Node->ParentIndex >= 0 ? ResolveNodeSize(Node->ParentIndex) : Layout->GetCanvasSize();
			const FVector2 CurrentSize = ResolveNodeSize(SelectedWidgetIndex);
			Node->Position.X = (ParentSize.X - CurrentSize.X) * 0.5f;
			Node->bUseLeftPercent = false;
			Node->bUseRight = false;
			bChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Center V"))
		{
			const FVector2 ParentSize = Node->ParentIndex >= 0 ? ResolveNodeSize(Node->ParentIndex) : Layout->GetCanvasSize();
			const FVector2 CurrentSize = ResolveNodeSize(SelectedWidgetIndex);
			Node->Position.Y = (ParentSize.Y - CurrentSize.Y) * 0.5f;
			Node->bUseTopPercent = false;
			Node->bUseBottom = false;
			bChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Bottom Center"))
		{
			const FVector2 ParentSize = Node->ParentIndex >= 0 ? ResolveNodeSize(Node->ParentIndex) : Layout->GetCanvasSize();
			const FVector2 CurrentSize = ResolveNodeSize(SelectedWidgetIndex);
			Node->Position.X = (ParentSize.X - CurrentSize.X) * 0.5f;
			Node->Bottom = 0.0f;
			Node->bUseLeftPercent = false;
			Node->bUseTopPercent = false;
			Node->bUseRight = false;
			Node->bUseBottom = true;
			bChanged = true;
		}
		if (ImGui::Button("Fill Parent"))
		{
			Node->Position = FVector2(0.0f, 0.0f);
			Node->SizePercent = FVector2(100.0f, 100.0f);
			Node->bUseLeftPercent = false;
			Node->bUseTopPercent = false;
			Node->bUseRight = false;
			Node->bUseBottom = false;
			Node->bUseWidthPercent = true;
			Node->bUseHeightPercent = true;
			bChanged = true;
		}
		if (ImGui::TreeNodeEx("Advanced Layout", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool bUseLeftPercent = Node->bUseLeftPercent;
			if (ImGui::Checkbox("Use Left %", &bUseLeftPercent))
			{
				const FVector2 LocalPositionBeforeToggle = ResolveNodeLocalPosition(SelectedWidgetIndex);
				Node->bUseLeftPercent = bUseLeftPercent;
				if (Node->bUseLeftPercent)
				{
					Node->bUseRight = false;
				}
				ApplyNodeLocalPosition(LocalPositionBeforeToggle);
				bChanged = true;
			}
			if (Node->bUseLeftPercent)
			{
				bChanged |= ImGui::InputFloat("Left %", &Node->PositionPercent.X, 1.0f, 5.0f, "%.2f");
			}
			bool bUseTopPercent = Node->bUseTopPercent;
			if (ImGui::Checkbox("Use Top %", &bUseTopPercent))
			{
				const FVector2 LocalPositionBeforeToggle = ResolveNodeLocalPosition(SelectedWidgetIndex);
				Node->bUseTopPercent = bUseTopPercent;
				if (Node->bUseTopPercent)
				{
					Node->bUseBottom = false;
				}
				ApplyNodeLocalPosition(LocalPositionBeforeToggle);
				bChanged = true;
			}
			if (Node->bUseTopPercent)
			{
				bChanged |= ImGui::InputFloat("Top %", &Node->PositionPercent.Y, 1.0f, 5.0f, "%.2f");
			}
			bChanged |= ImGui::Checkbox("Use Width %", &Node->bUseWidthPercent);
			if (Node->bUseWidthPercent)
			{
				bChanged |= ImGui::InputFloat("Width %", &Node->SizePercent.X, 1.0f, 5.0f, "%.2f");
			}
			bChanged |= ImGui::Checkbox("Use Height %", &Node->bUseHeightPercent);
			if (Node->bUseHeightPercent)
			{
				bChanged |= ImGui::InputFloat("Height %", &Node->SizePercent.Y, 1.0f, 5.0f, "%.2f");
			}
			bool bUseRight = Node->bUseRight;
			if (ImGui::Checkbox("Use Right Anchor", &bUseRight))
			{
				const FVector2 LocalPositionBeforeToggle = ResolveNodeLocalPosition(SelectedWidgetIndex);
				Node->bUseRight = bUseRight;
				if (Node->bUseRight)
				{
					Node->bUseLeftPercent = false;
				}
				ApplyNodeLocalPosition(LocalPositionBeforeToggle);
				bChanged = true;
			}
			if (Node->bUseRight)
			{
				bChanged |= ImGui::InputFloat("Right", &Node->Right, 1.0f, 8.0f, "%.2f");
			}
			bool bUseBottom = Node->bUseBottom;
			if (ImGui::Checkbox("Use Bottom Anchor", &bUseBottom))
			{
				const FVector2 LocalPositionBeforeToggle = ResolveNodeLocalPosition(SelectedWidgetIndex);
				Node->bUseBottom = bUseBottom;
				if (Node->bUseBottom)
				{
					Node->bUseTopPercent = false;
				}
				ApplyNodeLocalPosition(LocalPositionBeforeToggle);
				bChanged = true;
			}
			if (Node->bUseBottom)
			{
				bChanged |= ImGui::InputFloat("Bottom", &Node->Bottom, 1.0f, 8.0f, "%.2f");
			}
			bChanged |= ImGui::Checkbox("Flex Container", &Node->bUseFlexLayout);
			if (Node->bUseFlexLayout)
			{
				bChanged |= EditString("Justify Content", Node->JustifyContent);
				bChanged |= EditString("Align Items", Node->AlignItems);
			}
			ImGui::TreePop();
		}
	}

	bChanged |= EditString("Text", Node->Text);
	bChanged |= EditString("Image Path", Node->ImagePath);
	if (Node->Type == ERuntimeUIWidgetType::Image)
	{
		if (ImGui::SmallButton("Use Scope_3"))
		{
			Node->ImagePath = "Image/Scope/Scope_3.png";
			bChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Use Compass"))
		{
			Node->ImagePath = "Image/Hor-Compass/Compass.png";
			bChanged = true;
		}

		const FString ResolvedImagePath = ResolveRuntimeUILayoutPreviewImagePath(Node->ImagePath);
		ImGui::TextDisabled("Resolved Image");
		ImGui::TextWrapped("%s", ResolvedImagePath.empty() ? "(empty)" : ResolvedImagePath.c_str());

		bool bImageExists = false;
		if (!ResolvedImagePath.empty())
		{
			std::error_code Ec;
			bImageExists = std::filesystem::exists(std::filesystem::path(FPaths::ToWide(ResolvedImagePath)), Ec) && !Ec;
		}

		if (!bImageExists)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.28f, 1.0f), "Image file missing");
		}
		else if (ID3D11ShaderResourceView* ImageSRV = FEditorTextureManager::Get().GetOrLoadThumbnail(ResolvedImagePath))
		{
			const FVector2 ImageSize = GetRuntimeUILayoutPreviewImageSize(ImageSRV);
			ImGui::TextColored(ImVec4(0.38f, 0.82f, 0.54f, 1.0f), "Image loaded %.0fx%.0f", ImageSize.X, ImageSize.Y);
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.56f, 0.28f, 1.0f), "Image file exists, texture load failed");
		}
	}
	bChanged |= EditString("Style Class", Node->StyleClass);
	bChanged |= EditString("Action", Node->OnClickAction);
	bChanged |= EditString("Mask Image Path", Node->MaskImagePath);
	if (ImGui::SmallButton("Use Compass Mask"))
	{
		Node->MaskImagePath = "Image/Hor-Compass/Compass_Mask.png";
		bChanged = true;
	}
	bChanged |= EditColor("Background", Node->BackgroundColor);
	bChanged |= EditColor("Text Color", Node->TextColor);
	bChanged |= EditColor("Border Color", Node->BorderColor);
	bChanged |= EditVector4("Border Width", Node->BorderWidth);
	bChanged |= EditVector4("Padding", Node->Padding);
	bChanged |= ImGui::InputFloat("Border Radius", &Node->BorderRadius, 1.0f, 8.0f, "%.2f");
	bChanged |= ImGui::InputFloat("Font Size", &Node->FontSize, 1.0f, 8.0f, "%.2f");
	bChanged |= ImGui::SliderFloat("Opacity", &Node->Opacity, 0.0f, 1.0f);

	int32 ImageFit = static_cast<int32>(Node->ImageFit);
	const char* FitItems[] = { "Stretch", "Contain", "Cover" };
	if (ImGui::Combo("Image Fit", &ImageFit, FitItems, IM_ARRAYSIZE(FitItems)))
	{
		Node->ImageFit = static_cast<ERuntimeUIImageFit>(ImageFit);
		bChanged = true;
	}
	bChanged |= ImGui::Checkbox("Visible", &Node->bVisible);

	ImGui::Separator();
	ImGui::TextDisabled("Generated RML");
	ImGui::TextWrapped("%s", Layout->GetGeneratedRmlPath().empty() ? "(not set)" : Layout->GetGeneratedRmlPath().c_str());
	ImGui::TextDisabled("Generated RCSS");
	ImGui::TextWrapped("%s", Layout->GetGeneratedRcssPath().empty() ? "(not set)" : Layout->GetGeneratedRcssPath().c_str());

	if (bChanged)
	{
		CommitLayoutEdit(Layout);
	}
	else if (bLiveLayoutChanged)
	{
		MarkLayoutDirty();
	}
}

void FRuntimeUILayoutEditorWidget::AddWidget(URuntimeUILayoutAsset* Layout, int32 TypeIndex)
{
	if (!Layout)
	{
		return;
	}

	const int32 ParentIndex = Layout->GetWidget(SelectedWidgetIndex) ? SelectedWidgetIndex : 0;
	const int32 NewIndex = Layout->AddWidget(static_cast<ERuntimeUIWidgetType>(TypeIndex), ParentIndex);
	if (NewIndex >= 0)
	{
		SelectedWidgetIndex = NewIndex;
		CommitLayoutEdit(Layout);
	}
}

bool FRuntimeUILayoutEditorWidget::SaveAndExport(URuntimeUILayoutAsset* Layout, bool bOpenGeneratedRml)
{
	if (!Layout)
	{
		return false;
	}

	EnsureGeneratedPaths(Layout);
	const bool bSaved = FRuntimeUILayoutManager::Get().Save(Layout);
	if (!bSaved)
	{
		LastStatus = "Failed to save Runtime UI Layout.";
		bLastOperationFailed = true;
		return false;
	}

	FString ExportError;
	if (!Layout->ExportRmlAndRcss(Layout->GetGeneratedRmlPath(), Layout->GetGeneratedRcssPath(), &ExportError))
	{
		LastStatus = ExportError.empty() ? FString("Saved, but failed to export RML/RCSS.") : ExportError;
		bLastOperationFailed = true;
		return false;
	}

	ClearDirty();
	LastStatus = "Saved and exported.";
	bLastOperationFailed = false;
	bPendingStructuralImportConfirmation = false;
	PendingStructuralImportSignature.clear();
	if (bOpenGeneratedRml && EditorEngine)
	{
		EditorEngine->OpenRuntimeUIPreviewDocument(Layout->GetGeneratedRmlPath());
	}
	return true;
}

bool FRuntimeUILayoutEditorWidget::ImportGeneratedRmlAndRcss(URuntimeUILayoutAsset* Layout, bool bConfirmStructuralChanges)
{
	if (!Layout)
	{
		return false;
	}

	EnsureGeneratedPaths(Layout);

	FString RmlSource;
	FString RcssSource;
	const bool bLoadedRml = ReadTextFileIfExists(Layout->GetGeneratedRmlPath(), RmlSource);
	const bool bLoadedRcss = ReadTextFileIfExists(Layout->GetGeneratedRcssPath(), RcssSource);
	if (!bLoadedRml && !bLoadedRcss)
	{
		bPendingStructuralImportConfirmation = false;
		PendingStructuralImportSignature.clear();
		LastStatus = "Generated RML/RCSS files were not found. Export first.";
		bLastOperationFailed = true;
		return false;
	}

	const FString ImportSignature =
		Layout->GetGeneratedRmlPath()
		+ "|"
		+ Layout->GetGeneratedRcssPath()
		+ "|"
		+ std::to_string(RmlSource.size())
		+ ":"
		+ std::to_string(std::hash<FString>{}(RmlSource))
		+ "|"
		+ std::to_string(RcssSource.size())
		+ ":"
		+ std::to_string(std::hash<FString>{}(RcssSource));

	FRuntimeUILayoutImportedPatch ImportedPatch;
	if (bLoadedRml)
	{
		ParseRmlPatches(RmlSource, ImportedPatch);
	}
	if (bLoadedRcss)
	{
		ParseRcssPatches(RcssSource, ImportedPatch);
	}

	TArray<FRuntimeUIWidgetNode>& Widgets = Layout->GetMutableWidgets();
	std::unordered_map<FString, int32> WidgetIndexByImportedId;
	auto RegisterWidgetIndex = [&WidgetIndexByImportedId](const FRuntimeUIWidgetNode& Node, int32 Index)
	{
		if (Node.Id.empty())
		{
			return;
		}

		WidgetIndexByImportedId.emplace(Node.Id, Index);
		WidgetIndexByImportedId.emplace(ToCssIdForRuntimeUILayoutEditor(Node.Id), Index);
	};
	auto RebuildWidgetIndex = [&Widgets, &WidgetIndexByImportedId, &RegisterWidgetIndex]()
	{
		WidgetIndexByImportedId.clear();
		for (int32 Index = 0; Index < static_cast<int32>(Widgets.size()); ++Index)
		{
			RegisterWidgetIndex(Widgets[Index], Index);
		}
	};

	RebuildWidgetIndex();

	const bool bCanReconcileStructure = bLoadedRml && ImportedPatch.DuplicateRmlIds.empty();
	int32 PlannedCreatedCount = 0;
	int32 PlannedRemovedCount = 0;
	int32 PlannedReparentedCount = 0;
	if (bCanReconcileStructure)
	{
		for (const FRuntimeUILayoutImportedPatch::FImportedRmlNode& ImportedNode : ImportedPatch.RmlNodeOrder)
		{
			if (ImportedNode.Id.empty() || WidgetIndexByImportedId.find(ImportedNode.Id) != WidgetIndexByImportedId.end())
			{
				continue;
			}

			const auto RmlPatchIt = ImportedPatch.RmlNodes.find(ImportedNode.Id);
			ERuntimeUIWidgetType NewType = ERuntimeUIWidgetType::Panel;
			if (RmlPatchIt != ImportedPatch.RmlNodes.end() && RmlPatchIt->second.bHasType)
			{
				NewType = RmlPatchIt->second.Type;
			}
			if (NewType != ERuntimeUIWidgetType::Canvas)
			{
				++PlannedCreatedCount;
			}
		}

		std::unordered_set<FString> ImportedIds;
		for (const FRuntimeUILayoutImportedPatch::FImportedRmlNode& ImportedNode : ImportedPatch.RmlNodeOrder)
		{
			if (!ImportedNode.Id.empty())
			{
				ImportedIds.insert(ImportedNode.Id);
				ImportedIds.insert(ToCssIdForRuntimeUILayoutEditor(ImportedNode.Id));
			}
		}

		for (int32 Index = 1; Index < static_cast<int32>(Widgets.size()); ++Index)
		{
			const FRuntimeUIWidgetNode& Node = Widgets[Index];
			if (!Node.Id.empty()
				&& Node.bVisible
				&& ImportedIds.find(Node.Id) == ImportedIds.end()
				&& ImportedIds.find(ToCssIdForRuntimeUILayoutEditor(Node.Id)) == ImportedIds.end())
			{
				++PlannedRemovedCount;
			}
		}

		for (const FRuntimeUILayoutImportedPatch::FImportedRmlNode& ImportedNode : ImportedPatch.RmlNodeOrder)
		{
			const auto NodeIt = WidgetIndexByImportedId.find(ImportedNode.Id);
			if (NodeIt == WidgetIndexByImportedId.end())
			{
				continue;
			}

			const int32 NodeIndex = NodeIt->second;
			if (NodeIndex <= 0 || NodeIndex >= static_cast<int32>(Widgets.size()))
			{
				continue;
			}

			int32 ParentIndex = 0;
			if (!ImportedNode.ParentId.empty())
			{
				const auto ParentIt = WidgetIndexByImportedId.find(ImportedNode.ParentId);
				if (ParentIt != WidgetIndexByImportedId.end())
				{
					ParentIndex = ParentIt->second;
				}
			}

			if (ParentIndex < 0
				|| ParentIndex >= static_cast<int32>(Widgets.size())
				|| ParentIndex == NodeIndex
				|| IsRuntimeUILayoutDescendant(Widgets, ParentIndex, NodeIndex))
			{
				continue;
			}

			if (Widgets[NodeIndex].ParentIndex != ParentIndex)
			{
				++PlannedReparentedCount;
			}
		}
	}

	const bool bHasStructuralChanges = PlannedCreatedCount > 0 || PlannedRemovedCount > 0 || PlannedReparentedCount > 0;
	const bool bConfirmationMatches = bPendingStructuralImportConfirmation && PendingStructuralImportSignature == ImportSignature;
	if (bHasStructuralChanges && (!bConfirmStructuralChanges || !bConfirmationMatches))
	{
		bPendingStructuralImportConfirmation = true;
		PendingStructuralImportSignature = ImportSignature;
		LastStatus = "Import preview: ";
		LastStatus += std::to_string(PlannedCreatedCount);
		LastStatus += " create, ";
		LastStatus += std::to_string(PlannedRemovedCount);
		LastStatus += " remove, ";
		LastStatus += std::to_string(PlannedReparentedCount);
		LastStatus += " reparent. Press Confirm Import to apply.";
		AppendIdlessRmlPolicyStatus(LastStatus, ImportedPatch);
		bLastOperationFailed = true;
		return true;
	}

	bPendingStructuralImportConfirmation = false;
	PendingStructuralImportSignature.clear();

	int32 ChangeCount = 0;
	if (ImportedPatch.bHasCanvasSize)
	{
		const FVector2 CurrentSize = Layout->GetCanvasSize();
		if (!NearlyEqualFloat(CurrentSize.X, ImportedPatch.CanvasSize.X)
			|| !NearlyEqualFloat(CurrentSize.Y, ImportedPatch.CanvasSize.Y))
		{
			Layout->SetCanvasSize(ImportedPatch.CanvasSize);
			++ChangeCount;
		}
	}

	if (bCanReconcileStructure)
	{
		bool bRebuiltHierarchy = false;
		for (const FRuntimeUILayoutImportedPatch::FImportedRmlNode& ImportedNode : ImportedPatch.RmlNodeOrder)
		{
			const auto NodeIt = WidgetIndexByImportedId.find(ImportedNode.Id);
			if (NodeIt == WidgetIndexByImportedId.end())
			{
				continue;
			}

			const int32 NodeIndex = NodeIt->second;
			if (NodeIndex <= 0 || NodeIndex >= static_cast<int32>(Widgets.size()))
			{
				continue;
			}

			int32 ParentIndex = 0;
			if (!ImportedNode.ParentId.empty())
			{
				const auto ParentIt = WidgetIndexByImportedId.find(ImportedNode.ParentId);
				if (ParentIt != WidgetIndexByImportedId.end())
				{
					ParentIndex = ParentIt->second;
				}
			}

			if (ParentIndex < 0
				|| ParentIndex >= static_cast<int32>(Widgets.size())
				|| ParentIndex == NodeIndex
				|| IsRuntimeUILayoutDescendant(Widgets, ParentIndex, NodeIndex))
			{
				continue;
			}

			if (Widgets[NodeIndex].ParentIndex != ParentIndex)
			{
				Widgets[NodeIndex].ParentIndex = ParentIndex;
				bRebuiltHierarchy = true;
				++ChangeCount;
			}
		}

		if (bRebuiltHierarchy)
		{
			RebuildRuntimeUILayoutChildren(Widgets);
			RebuildWidgetIndex();
		}
	}

	const int32 ExistingWidgetCount = static_cast<int32>(Widgets.size());
	for (int32 Index = 0; Index < ExistingWidgetCount; ++Index)
	{
		FRuntimeUIWidgetNode& Node = Widgets[Index];
		const bool bIsRoot = Index == 0;

		if (const FRuntimeUILayoutRmlPatch* RmlPatch = FindRmlPatchForNode(ImportedPatch, Node))
		{
			ApplyRmlPatchToNode(Node, *RmlPatch, bIsRoot, ChangeCount);
		}

		if (const FRuntimeUILayoutStylePatch* StylePatch = FindStylePatchForNode(ImportedPatch, Node))
		{
			ApplyStylePatchToNode(Layout, Node, *StylePatch, bIsRoot, ChangeCount);
		}
	}

	int32 CreatedCount = 0;
	if (bCanReconcileStructure)
	{
		for (const FRuntimeUILayoutImportedPatch::FImportedRmlNode& ImportedNode : ImportedPatch.RmlNodeOrder)
		{
			if (ImportedNode.Id.empty() || WidgetIndexByImportedId.find(ImportedNode.Id) != WidgetIndexByImportedId.end())
			{
				continue;
			}

			const auto RmlPatchIt = ImportedPatch.RmlNodes.find(ImportedNode.Id);
			ERuntimeUIWidgetType NewType = ERuntimeUIWidgetType::Panel;
			if (RmlPatchIt != ImportedPatch.RmlNodes.end() && RmlPatchIt->second.bHasType)
			{
				NewType = RmlPatchIt->second.Type;
			}
			if (NewType == ERuntimeUIWidgetType::Canvas)
			{
				continue;
			}

			int32 ParentIndex = 0;
			if (!ImportedNode.ParentId.empty())
			{
				const auto ParentIt = WidgetIndexByImportedId.find(ImportedNode.ParentId);
				if (ParentIt != WidgetIndexByImportedId.end())
				{
					ParentIndex = ParentIt->second;
				}
			}

			const int32 NewIndex = Layout->AddWidget(NewType, ParentIndex);
			if (NewIndex < 0)
			{
				continue;
			}

			FRuntimeUIWidgetNode& NewNode = Widgets[NewIndex];
			NewNode.Id = ImportedNode.Id;
			if (RmlPatchIt == ImportedPatch.RmlNodes.end() || !RmlPatchIt->second.bHasDisplayName)
			{
				NewNode.DisplayName = ImportedNode.Id;
			}

			if (RmlPatchIt != ImportedPatch.RmlNodes.end())
			{
				ApplyRmlPatchToNode(NewNode, RmlPatchIt->second, false, ChangeCount);
			}

			const auto StyleIt = ImportedPatch.Styles.find(ImportedNode.Id);
			if (StyleIt != ImportedPatch.Styles.end())
			{
				ApplyStylePatchToNode(Layout, NewNode, StyleIt->second, false, ChangeCount);
			}

			RegisterWidgetIndex(NewNode, NewIndex);
			SelectedWidgetIndex = NewIndex;
			++CreatedCount;
		}
	}

	int32 RemovedCount = 0;
	if (bCanReconcileStructure)
	{
		std::unordered_set<FString> ImportedIds;
		for (const FRuntimeUILayoutImportedPatch::FImportedRmlNode& ImportedNode : ImportedPatch.RmlNodeOrder)
		{
			if (!ImportedNode.Id.empty())
			{
				ImportedIds.insert(ImportedNode.Id);
				ImportedIds.insert(ToCssIdForRuntimeUILayoutEditor(ImportedNode.Id));
			}
		}

		for (int32 Index = static_cast<int32>(Widgets.size()) - 1; Index > 0; --Index)
		{
			const FRuntimeUIWidgetNode* Node = Layout->GetWidget(Index);
			if (!Node || Node->Id.empty() || !Node->bVisible)
			{
				continue;
			}

			if (ImportedIds.find(Node->Id) != ImportedIds.end()
				|| ImportedIds.find(ToCssIdForRuntimeUILayoutEditor(Node->Id)) != ImportedIds.end())
			{
				continue;
			}

			if (Layout->RemoveWidget(Index))
			{
				++RemovedCount;
				++ChangeCount;
			}
		}
	}

	if (ChangeCount <= 0 && CreatedCount <= 0 && RemovedCount <= 0)
	{
		LastStatus = ImportedPatch.DuplicateRmlIds.empty()
			? FString("No matching generated RML/RCSS edits found.")
			: FString("Duplicate RML ids found (") + DescribeDuplicateRmlIds(ImportedPatch.DuplicateRmlIds) + "); skipped structural import.";
		AppendIdlessRmlPolicyStatus(LastStatus, ImportedPatch);
		bLastOperationFailed = false;
		return true;
	}

	SelectedWidgetIndex = (std::max)(0, (std::min)(SelectedWidgetIndex, static_cast<int32>(Widgets.size()) - 1));
	CommitLayoutEdit(Layout);
	LastStatus = "Imported generated RML/RCSS edits.";
	if (CreatedCount > 0)
	{
		LastStatus += " Created ";
		LastStatus += std::to_string(CreatedCount);
		LastStatus += " new widget";
		LastStatus += CreatedCount == 1 ? "." : "s.";
	}
	if (RemovedCount > 0)
	{
		LastStatus += " Removed ";
		LastStatus += std::to_string(RemovedCount);
		LastStatus += " widget";
		LastStatus += RemovedCount == 1 ? "." : "s.";
	}
	if (!ImportedPatch.DuplicateRmlIds.empty())
	{
		LastStatus += " Duplicate ids skipped structural sync (";
		LastStatus += DescribeDuplicateRmlIds(ImportedPatch.DuplicateRmlIds);
		LastStatus += ").";
	}
	AppendIdlessRmlPolicyStatus(LastStatus, ImportedPatch);
	bLastOperationFailed = false;
	return true;
}

void FRuntimeUILayoutEditorWidget::EnsureGeneratedPaths(URuntimeUILayoutAsset* Layout)
{
	if (!Layout || (!Layout->GetGeneratedRmlPath().empty() && !Layout->GetGeneratedRcssPath().empty()))
	{
		return;
	}

	std::filesystem::path RmlPath(FPaths::ToWide(Layout->GetAssetPath()));
	std::filesystem::path RcssPath = RmlPath;
	RmlPath.replace_extension(L".rml");
	RcssPath.replace_extension(L".rcss");
	Layout->SetGeneratedPaths(
		FPaths::ToUtf8(RmlPath.generic_wstring()),
		FPaths::ToUtf8(RcssPath.generic_wstring()));
}

void FRuntimeUILayoutEditorWidget::MarkLayoutDirty()
{
	MarkDirty();
	LastStatus.clear();
	bLastOperationFailed = false;
	bPendingStructuralImportConfirmation = false;
	PendingStructuralImportSignature.clear();
}

void FRuntimeUILayoutEditorWidget::HandleUndoRedoShortcuts(URuntimeUILayoutAsset* Layout)
{
	ImGuiIO& IO = ImGui::GetIO();
	if (!Layout || !IO.KeyCtrl || IO.WantTextInput)
	{
		return;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Z))
	{
		if (IO.KeyShift)
		{
			RedoLayoutEdit(Layout);
		}
		else
		{
			UndoLayoutEdit(Layout);
		}
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Y))
	{
		RedoLayoutEdit(Layout);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_S))
	{
		SaveAndExport(Layout, false);
	}
}

void FRuntimeUILayoutEditorWidget::CaptureInitialUndoSnapshot(URuntimeUILayoutAsset* Layout)
{
	UndoStack.clear();
	RedoStack.clear();

	const TArray<uint8> Snapshot = CaptureLayoutSnapshot(Layout);
	if (!Snapshot.empty())
	{
		UndoStack.push_back(Snapshot);
	}
}

void FRuntimeUILayoutEditorWidget::CommitLayoutEdit(URuntimeUILayoutAsset* Layout)
{
	if (!Layout || bRestoringSnapshot)
	{
		return;
	}

	const TArray<uint8> BeforeSnapshot = UndoStack.empty() ? TArray<uint8>() : UndoStack.back();
	const TArray<uint8> Snapshot = CaptureLayoutSnapshot(Layout);
	if (!Snapshot.empty() && (UndoStack.empty() || UndoStack.back() != Snapshot))
	{
		if (!BeforeSnapshot.empty() && EditorEngine)
		{
			std::shared_ptr<bool> UndoLifetime = GetEditorLifetimeToken();
			FEditorUndoSystem& UndoSystem = EditorEngine->GetUndoSystem();
			UndoSystem.BeginTransaction("Edit Runtime UI Layout");
			UndoSystem.AddCommand(std::make_unique<FLambdaEditorUndoCommand>(
				"Edit Runtime UI Layout",
				BeforeSnapshot,
				Snapshot,
				[this, Layout, UndoLifetime](FEditorUndoContext&, const TArray<uint8>& RestoreSnapshot)
				{
					if (!UndoLifetime || !*UndoLifetime)
					{
						return false;
					}

					if (!IsOpen() || !IsEditingObject(Layout) || !IsValid(Layout) || RestoreSnapshot.empty())
					{
						return false;
					}

					const bool bRestored = RestoreLayoutSnapshot(Layout, RestoreSnapshot);
					if (bRestored)
					{
						UndoStack.clear();
						UndoStack.push_back(RestoreSnapshot);
						RedoStack.clear();
					}
					return bRestored;
				}));
			UndoSystem.EndTransaction();
		}
		UndoStack.clear();
		UndoStack.push_back(Snapshot);
	}
	RedoStack.clear();
	MarkLayoutDirty();
}

void FRuntimeUILayoutEditorWidget::UndoLayoutEdit(URuntimeUILayoutAsset* Layout)
{
	if (Layout && EditorEngine)
	{
		EditorEngine->GetUndoSystem().Undo();
	}
}

void FRuntimeUILayoutEditorWidget::RedoLayoutEdit(URuntimeUILayoutAsset* Layout)
{
	if (Layout && EditorEngine)
	{
		EditorEngine->GetUndoSystem().Redo();
	}
}

TArray<uint8> FRuntimeUILayoutEditorWidget::CaptureLayoutSnapshot(URuntimeUILayoutAsset* Layout) const
{
	TArray<uint8> Buffer;
	if (!Layout)
	{
		return Buffer;
	}

	FMemoryArchive Saver(true);
	Layout->Serialize(Saver);
	Buffer = Saver.GetBuffer();
	return Buffer;
}

bool FRuntimeUILayoutEditorWidget::RestoreLayoutSnapshot(URuntimeUILayoutAsset* Layout, const TArray<uint8>& Snapshot)
{
	if (!Layout || Snapshot.empty())
	{
		return false;
	}

	bRestoringSnapshot = true;
	FMemoryArchive Loader(Snapshot, false);
	Layout->Serialize(Loader);
	bRestoringSnapshot = false;

	const int32 WidgetCount = static_cast<int32>(Layout->GetWidgets().size());
	if (WidgetCount <= 0)
	{
		SelectedWidgetIndex = -1;
	}
	else if (SelectedWidgetIndex < 0 || SelectedWidgetIndex >= WidgetCount)
	{
		SelectedWidgetIndex = 0;
	}

	DraggingWidgetIndex = -1;
	bDragMovedSinceCommit = false;
	MarkLayoutDirty();
	return true;
}
