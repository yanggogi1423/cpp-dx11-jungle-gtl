#include "UI/RuntimeUILayoutAsset.h"

#include "Platform/Paths.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace
{
	int32 GRuntimeUILayoutSerializingPayloadVersion = URuntimeUILayoutAsset::CurrentPayloadVersion;

	const char* GetWidgetTypeName(ERuntimeUIWidgetType Type)
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

	const char* GetRmlTagName(ERuntimeUIWidgetType Type)
	{
		return Type == ERuntimeUIWidgetType::Image ? "img"
			: Type == ERuntimeUIWidgetType::Button ? "button"
			: "div";
	}

	const char* GetImageFitName(ERuntimeUIImageFit Fit)
	{
		switch (Fit)
		{
		case ERuntimeUIImageFit::Contain: return "contain";
		case ERuntimeUIImageFit::Cover: return "cover";
		case ERuntimeUIImageFit::Stretch:
		default: return "fill";
		}
	}

	std::filesystem::path ToAbsoluteProjectPath(const FString& Path)
	{
		std::filesystem::path Result(FPaths::ToWide(Path));
		if (Result.is_relative())
		{
			Result = std::filesystem::path(FPaths::RootDir()) / Result;
		}
		return Result.lexically_normal();
	}

	FString EscapeXml(const FString& Text)
	{
		FString Result;
		Result.reserve(Text.size());
		for (const char Ch : Text)
		{
			switch (Ch)
			{
			case '&': Result += "&amp;"; break;
			case '<': Result += "&lt;"; break;
			case '>': Result += "&gt;"; break;
			case '"': Result += "&quot;"; break;
			case '\'': Result += "&apos;"; break;
			default: Result.push_back(Ch); break;
			}
		}
		return Result;
	}

	FString ToCssId(const FString& Id)
	{
		FString Result = Id;
		for (char& Ch : Result)
		{
			if (!std::isalnum(static_cast<unsigned char>(Ch)) && Ch != '_' && Ch != '-')
			{
				Ch = '_';
			}
		}
		return Result.empty() ? "RuntimeUI" : Result;
	}

	FString ToRcssColor(const FVector4& Color)
	{
		const int32 R = static_cast<int32>(std::round(std::clamp(Color.X, 0.0f, 1.0f) * 255.0f));
		const int32 G = static_cast<int32>(std::round(std::clamp(Color.Y, 0.0f, 1.0f) * 255.0f));
		const int32 B = static_cast<int32>(std::round(std::clamp(Color.Z, 0.0f, 1.0f) * 255.0f));
		const int32 A = static_cast<int32>(std::round(std::clamp(Color.W, 0.0f, 1.0f) * 255.0f));
		return "rgba(" + std::to_string(R) + ", " + std::to_string(G) + ", " + std::to_string(B) + ", " + std::to_string(A) + ")";
	}

	FString ToPx(float Value)
	{
		if (std::abs(Value) < 0.001f)
		{
			return "0px";
		}
		return std::to_string(Value) + "px";
	}

	FString ToPercent(float Value)
	{
		return std::to_string(Value) + "%";
	}

	FString MakeRelativeHref(const std::filesystem::path& RmlPath, const std::filesystem::path& RcssPath)
	{
		std::error_code Ec;
		const std::filesystem::path Relative = std::filesystem::relative(RcssPath, RmlPath.parent_path(), Ec);
		if (!Ec)
		{
			return FPaths::ToUtf8(Relative.generic_wstring());
		}
		return FPaths::ToUtf8(RcssPath.generic_wstring());
	}

	void AppendIndent(std::ostringstream& Stream, int32 Depth)
	{
		for (int32 Index = 0; Index < Depth; ++Index)
		{
			Stream << "    ";
		}
	}

	void AppendRmlNode(
		std::ostringstream& Stream,
		const TArray<FRuntimeUIWidgetNode>& Widgets,
		int32 WidgetIndex,
		int32 Depth)
	{
		if (WidgetIndex < 0 || WidgetIndex >= static_cast<int32>(Widgets.size()))
		{
			return;
		}

		const FRuntimeUIWidgetNode& Node = Widgets[WidgetIndex];
		if (!Node.bVisible)
		{
			return;
		}

		const char* TagName = GetRmlTagName(Node.Type);
		const FString SafeId = ToCssId(Node.Id);
		AppendIndent(Stream, Depth);
		Stream << "<" << TagName << " id=\"" << EscapeXml(SafeId) << "\"";
		Stream << " data-ui-type=\"" << GetWidgetTypeName(Node.Type) << "\"";
		if (!Node.DisplayName.empty())
		{
			Stream << " data-ui-name=\"" << EscapeXml(Node.DisplayName) << "\"";
		}
		if (!Node.StyleClass.empty())
		{
			Stream << " class=\"" << EscapeXml(Node.StyleClass) << "\"";
		}
		if (!Node.OnClickAction.empty())
		{
			Stream << " data-action=\"" << EscapeXml(Node.OnClickAction) << "\"";
		}
		if (Node.Type == ERuntimeUIWidgetType::Image)
		{
			Stream << " src=\"" << EscapeXml(Node.ImagePath) << "\"";
			Stream << " data-ui-fit=\"" << GetImageFitName(Node.ImageFit) << "\" />\n";
			return;
		}

		Stream << ">";
		if (!Node.Text.empty())
		{
			Stream << EscapeXml(Node.Text);
		}
		if (!Node.Children.empty())
		{
			Stream << "\n";
			for (const int32 ChildIndex : Node.Children)
			{
				AppendRmlNode(Stream, Widgets, ChildIndex, Depth + 1);
			}
			AppendIndent(Stream, Depth);
		}
		Stream << "</" << TagName << ">\n";
	}

	void AppendRcssNode(std::ostringstream& Stream, const FRuntimeUIWidgetNode& Node, bool bIsRoot)
	{
		if (!Node.bVisible)
		{
			return;
		}

		const FString SafeId = ToCssId(Node.Id);
		Stream << "#" << SafeId << " {\n";
		Stream << "    position: " << (bIsRoot ? "relative" : "absolute") << ";\n";
		if (Node.bUseRight)
		{
			Stream << "    right: " << ToPx(Node.Right) << ";\n";
		}
		else
		{
			Stream << "    left: " << (Node.bUseLeftPercent ? ToPercent(Node.PositionPercent.X) : ToPx(Node.Position.X)) << ";\n";
		}
		if (Node.bUseBottom)
		{
			Stream << "    bottom: " << ToPx(Node.Bottom) << ";\n";
		}
		else
		{
			Stream << "    top: " << (Node.bUseTopPercent ? ToPercent(Node.PositionPercent.Y) : ToPx(Node.Position.Y)) << ";\n";
		}
		Stream << "    width: " << (Node.bUseWidthPercent ? ToPercent(Node.SizePercent.X) : ToPx(Node.Size.X)) << ";\n";
		Stream << "    height: " << (Node.bUseHeightPercent ? ToPercent(Node.SizePercent.Y) : ToPx(Node.Size.Y)) << ";\n";
		Stream << "    box-sizing: border-box;\n";
		Stream << "    overflow: hidden;\n";
		Stream << "    opacity: " << Node.Opacity << ";\n";
		if (Node.bUseFlexLayout)
		{
			Stream << "    display: flex;\n";
			if (!Node.JustifyContent.empty())
			{
				Stream << "    justify-content: " << Node.JustifyContent << ";\n";
			}
			if (!Node.AlignItems.empty())
			{
				Stream << "    align-items: " << Node.AlignItems << ";\n";
			}
		}
		Stream << "    background-color: " << ToRcssColor(Node.BackgroundColor) << ";\n";
		Stream << "    color: " << ToRcssColor(Node.TextColor) << ";\n";
		Stream << "    border-color: " << ToRcssColor(Node.BorderColor) << ";\n";
		Stream << "    border-width: "
			<< ToPx(Node.BorderWidth.Y) << " "
			<< ToPx(Node.BorderWidth.Z) << " "
			<< ToPx(Node.BorderWidth.W) << " "
			<< ToPx(Node.BorderWidth.X) << ";\n";
		Stream << "    border-radius: " << ToPx(Node.BorderRadius) << ";\n";
		Stream << "    padding: "
			<< ToPx(Node.Padding.Y) << " "
			<< ToPx(Node.Padding.Z) << " "
			<< ToPx(Node.Padding.W) << " "
			<< ToPx(Node.Padding.X) << ";\n";
		Stream << "    font-size: " << ToPx(Node.FontSize) << ";\n";
		Stream << "    text-align: center;\n";
		if (Node.Type == ERuntimeUIWidgetType::Image)
		{
			Stream << "    object-fit: " << GetImageFitName(Node.ImageFit) << ";\n";
		}
		if (!Node.MaskImagePath.empty())
		{
			Stream << "    mask-image: url(" << Node.MaskImagePath << ");\n";
		}
		Stream << "}\n\n";
	}
}

URuntimeUILayoutAsset::URuntimeUILayoutAsset()
{
	ResetToDefault();
}

void URuntimeUILayoutAsset::Serialize(FArchive& Ar)
{
	int32 Version = CurrentPayloadVersion;
	Ar << Version;
	GRuntimeUILayoutSerializingPayloadVersion = Version;
	Ar << AssetPath;
	Ar << GeneratedRmlPath;
	Ar << GeneratedRcssPath;
	Ar << CanvasSize;
	Ar << Widgets;

	if (Ar.IsLoading())
	{
		RebuildChildrenFromParents();
	}
	GRuntimeUILayoutSerializingPayloadVersion = CurrentPayloadVersion;
}

void URuntimeUILayoutAsset::ResetToDefault()
{
	CanvasSize = FVector2(1920.0f, 1080.0f);
	Widgets.clear();

	const int32 CanvasIndex = AddWidget(ERuntimeUIWidgetType::Canvas, -1);
	FRuntimeUIWidgetNode* Canvas = GetMutableWidget(CanvasIndex);
	if (Canvas)
	{
		Canvas->Id = "root";
		Canvas->DisplayName = "Root Canvas";
		Canvas->Size = CanvasSize;
		Canvas->BackgroundColor = FVector4(0.025f, 0.029f, 0.038f, 1.0f);
	}

	const int32 PanelIndex = AddWidget(ERuntimeUIWidgetType::Panel, CanvasIndex);
	FRuntimeUIWidgetNode* Panel = GetMutableWidget(PanelIndex);
	if (Panel)
	{
		Panel->Id = "mainPanel";
		Panel->DisplayName = "Main Panel";
		Panel->Position = FVector2(660.0f, 330.0f);
		Panel->Size = FVector2(600.0f, 360.0f);
		Panel->BackgroundColor = FVector4(0.11f, 0.13f, 0.18f, 0.92f);
		Panel->BorderColor = FVector4(0.38f, 0.68f, 0.78f, 0.75f);
		Panel->BorderWidth = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		Panel->BorderRadius = 6.0f;
	}

	const int32 TitleIndex = AddWidget(ERuntimeUIWidgetType::Text, PanelIndex);
	FRuntimeUIWidgetNode* Title = GetMutableWidget(TitleIndex);
	if (Title)
	{
		Title->Id = "titleText";
		Title->DisplayName = "Title";
		Title->Position = FVector2(60.0f, 58.0f);
		Title->Size = FVector2(480.0f, 64.0f);
		Title->Text = "Runtime UI";
		Title->FontSize = 42.0f;
		Title->TextColor = FVector4(0.92f, 0.98f, 1.0f, 1.0f);
	}

	const int32 ButtonIndex = AddWidget(ERuntimeUIWidgetType::Button, PanelIndex);
	FRuntimeUIWidgetNode* Button = GetMutableWidget(ButtonIndex);
	if (Button)
	{
		Button->Id = "startButton";
		Button->DisplayName = "Start Button";
		Button->Position = FVector2(180.0f, 210.0f);
		Button->Size = FVector2(240.0f, 72.0f);
		Button->Text = "Start";
		Button->OnClickAction = "StartGame";
		Button->FontSize = 28.0f;
		Button->BackgroundColor = FVector4(0.22f, 0.46f, 0.58f, 1.0f);
		Button->BorderRadius = 5.0f;
	}
}

int32 URuntimeUILayoutAsset::AddWidget(ERuntimeUIWidgetType Type, int32 ParentIndex)
{
	FRuntimeUIWidgetNode Node;
	Node.Type = Type;
	Node.Id = MakeUniqueWidgetId(Type);
	Node.DisplayName = GetWidgetTypeName(Type);

	if (Type == ERuntimeUIWidgetType::Canvas)
	{
		Node.Size = CanvasSize;
	}
	else if (Type == ERuntimeUIWidgetType::Button)
	{
		Node.Text = "Button";
		Node.BackgroundColor = FVector4(0.22f, 0.36f, 0.56f, 0.95f);
	}
	else if (Type == ERuntimeUIWidgetType::Text)
	{
		Node.Text = "Text";
		Node.BackgroundColor = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	}
	else if (Type == ERuntimeUIWidgetType::Panel)
	{
		Node.BackgroundColor = FVector4(0.12f, 0.14f, 0.18f, 0.82f);
	}

	if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(Widgets.size()))
	{
		Node.ParentIndex = ParentIndex;
	}

	const int32 NewIndex = static_cast<int32>(Widgets.size());
	Widgets.push_back(Node);
	if (Node.ParentIndex >= 0)
	{
		Widgets[Node.ParentIndex].Children.push_back(NewIndex);
	}
	return NewIndex;
}

bool URuntimeUILayoutAsset::RemoveWidget(int32 WidgetIndex)
{
	if (WidgetIndex <= 0 || !IsValidWidgetIndex(WidgetIndex))
	{
		return false;
	}

	TArray<uint8> bRemove;
	bRemove.resize(Widgets.size(), 0);
	std::function<void(int32)> MarkSubtree = [&](int32 Index)
	{
		if (!IsValidWidgetIndex(Index) || bRemove[Index])
		{
			return;
		}
		bRemove[Index] = 1;
		for (const int32 ChildIndex : Widgets[Index].Children)
		{
			MarkSubtree(ChildIndex);
		}
	};
	MarkSubtree(WidgetIndex);

	TArray<int32> Remap;
	Remap.resize(Widgets.size(), -1);
	TArray<FRuntimeUIWidgetNode> NewWidgets;
	NewWidgets.reserve(Widgets.size());
	for (int32 Index = 0; Index < static_cast<int32>(Widgets.size()); ++Index)
	{
		if (bRemove[Index])
		{
			continue;
		}
		Remap[Index] = static_cast<int32>(NewWidgets.size());
		NewWidgets.push_back(Widgets[Index]);
		NewWidgets.back().Children.clear();
	}

	for (FRuntimeUIWidgetNode& Node : NewWidgets)
	{
		if (Node.ParentIndex >= 0 && Node.ParentIndex < static_cast<int32>(Remap.size()))
		{
			Node.ParentIndex = Remap[Node.ParentIndex];
		}
		else
		{
			Node.ParentIndex = -1;
		}
	}

	Widgets = std::move(NewWidgets);
	RebuildChildrenFromParents();
	return true;
}

FRuntimeUIWidgetNode* URuntimeUILayoutAsset::GetMutableWidget(int32 WidgetIndex)
{
	return IsValidWidgetIndex(WidgetIndex) ? &Widgets[WidgetIndex] : nullptr;
}

const FRuntimeUIWidgetNode* URuntimeUILayoutAsset::GetWidget(int32 WidgetIndex) const
{
	return IsValidWidgetIndex(WidgetIndex) ? &Widgets[WidgetIndex] : nullptr;
}

void URuntimeUILayoutAsset::SetCanvasSize(const FVector2& InCanvasSize)
{
	CanvasSize = FVector2(
		(std::max)(1.0f, InCanvasSize.X),
		(std::max)(1.0f, InCanvasSize.Y));
	if (!Widgets.empty() && Widgets.front().Type == ERuntimeUIWidgetType::Canvas)
	{
		Widgets.front().Size = CanvasSize;
	}
}

void URuntimeUILayoutAsset::SetGeneratedPaths(const FString& InRmlPath, const FString& InRcssPath)
{
	GeneratedRmlPath = FPaths::MakeProjectRelative(InRmlPath);
	GeneratedRcssPath = FPaths::MakeProjectRelative(InRcssPath);
}

bool URuntimeUILayoutAsset::ValidateForExport(FString* OutError) const
{
	if (Widgets.empty())
	{
		if (OutError) *OutError = "Runtime UI layout has no widgets.";
		return false;
	}
	if (Widgets.front().Type != ERuntimeUIWidgetType::Canvas)
	{
		if (OutError) *OutError = "Runtime UI layout root must be a Canvas.";
		return false;
	}
	if (!Widgets.front().bVisible)
	{
		if (OutError) *OutError = "Runtime UI layout root must be visible.";
		return false;
	}

	std::unordered_set<FString> UsedIds;
	std::unordered_set<FString> UsedCssIds;
	for (const FRuntimeUIWidgetNode& Node : Widgets)
	{
		if (!Node.bVisible)
		{
			continue;
		}
		if (Node.Id.empty())
		{
			if (OutError) *OutError = "Runtime UI widgets must have non-empty ids.";
			return false;
		}

		if (!UsedIds.insert(Node.Id).second)
		{
			if (OutError) *OutError = FString("Runtime UI widget id is duplicated: ") + Node.Id;
			return false;
		}

		const FString CssId = ToCssId(Node.Id);
		if (!UsedCssIds.insert(CssId).second)
		{
			if (OutError) *OutError = FString("Runtime UI widget ids collide after CSS sanitization: ") + CssId;
			return false;
		}
	}
	return true;
}

bool URuntimeUILayoutAsset::ExportRmlAndRcss(const FString& RmlPath, const FString& RcssPath, FString* OutError) const
{
	if (!ValidateForExport(OutError))
	{
		return false;
	}

	const std::filesystem::path AbsoluteRmlPath = ToAbsoluteProjectPath(RmlPath);
	const std::filesystem::path AbsoluteRcssPath = ToAbsoluteProjectPath(RcssPath);
	std::error_code Ec;
	std::filesystem::create_directories(AbsoluteRmlPath.parent_path(), Ec);
	if (Ec)
	{
		if (OutError) *OutError = "Failed to create RML directory.";
		return false;
	}
	std::filesystem::create_directories(AbsoluteRcssPath.parent_path(), Ec);
	if (Ec)
	{
		if (OutError) *OutError = "Failed to create RCSS directory.";
		return false;
	}

	std::ostringstream Rml;
	Rml << "<rml>\n<head>\n";
	Rml << "    <link type=\"text/rcss\" href=\"" << EscapeXml(MakeRelativeHref(AbsoluteRmlPath, AbsoluteRcssPath)) << "\" />\n";
	Rml << "</head>\n<body>\n";
	AppendRmlNode(Rml, Widgets, 0, 1);
	Rml << "</body>\n</rml>\n";

	std::ostringstream Rcss;
	Rcss << "body {\n";
	Rcss << "    margin: 0px;\n";
	Rcss << "    width: " << ToPx(CanvasSize.X) << ";\n";
	Rcss << "    height: " << ToPx(CanvasSize.Y) << ";\n";
	Rcss << "}\n\n";
	for (int32 Index = 0; Index < static_cast<int32>(Widgets.size()); ++Index)
	{
		AppendRcssNode(Rcss, Widgets[Index], Index == 0);
	}

	{
		std::ofstream File(AbsoluteRmlPath, std::ios::binary);
		if (!File)
		{
			if (OutError) *OutError = "Failed to write RML file.";
			return false;
		}
		File << Rml.str();
	}
	{
		std::ofstream File(AbsoluteRcssPath, std::ios::binary);
		if (!File)
		{
			if (OutError) *OutError = "Failed to write RCSS file.";
			return false;
		}
		File << Rcss.str();
	}
	return true;
}

FString URuntimeUILayoutAsset::MakeUniqueWidgetId(ERuntimeUIWidgetType Type) const
{
	const FString Base = GetWidgetTypeName(Type);
	int32 Suffix = 1;
	for (;;)
	{
		const FString Candidate = Base + std::to_string(Suffix);
		bool bExists = false;
		for (const FRuntimeUIWidgetNode& Node : Widgets)
		{
			if (Node.Id == Candidate)
			{
				bExists = true;
				break;
			}
		}
		if (!bExists)
		{
			return Candidate;
		}
		++Suffix;
	}
}

bool URuntimeUILayoutAsset::IsValidWidgetIndex(int32 WidgetIndex) const
{
	return WidgetIndex >= 0 && WidgetIndex < static_cast<int32>(Widgets.size());
}

void URuntimeUILayoutAsset::RebuildChildrenFromParents()
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
		else
		{
			Widgets[Index].ParentIndex = -1;
		}
	}
}

FArchive& operator<<(FArchive& Ar, FRuntimeUIWidgetNode& Node)
{
	Ar << Node.Id;
	Ar << Node.DisplayName;

	int32 Type = static_cast<int32>(Node.Type);
	Ar << Type;
	if (Ar.IsLoading())
	{
		Node.Type = static_cast<ERuntimeUIWidgetType>(Type);
	}

	Ar << Node.ParentIndex;
	Ar << Node.Children;
	Ar << Node.Position;
	Ar << Node.Size;
	if (GRuntimeUILayoutSerializingPayloadVersion >= 2)
	{
		Ar << Node.PositionPercent;
		Ar << Node.SizePercent;
	}
	Ar << Node.Text;
	Ar << Node.ImagePath;
	if (GRuntimeUILayoutSerializingPayloadVersion >= 4)
	{
		Ar << Node.MaskImagePath;
	}
	Ar << Node.StyleClass;
	Ar << Node.OnClickAction;
	Ar << Node.BackgroundColor;
	Ar << Node.TextColor;
	Ar << Node.BorderColor;
	Ar << Node.BorderWidth;
	Ar << Node.Padding;
	Ar << Node.BorderRadius;
	Ar << Node.FontSize;
	Ar << Node.Opacity;
	if (GRuntimeUILayoutSerializingPayloadVersion >= 2)
	{
		Ar << Node.Right;
		Ar << Node.Bottom;
		Ar << Node.JustifyContent;
		Ar << Node.AlignItems;
	}

	int32 ImageFit = static_cast<int32>(Node.ImageFit);
	Ar << ImageFit;
	if (Ar.IsLoading())
	{
		Node.ImageFit = static_cast<ERuntimeUIImageFit>(ImageFit);
	}

	Ar << Node.bVisible;
	if (GRuntimeUILayoutSerializingPayloadVersion >= 2)
	{
		Ar << Node.bUseLeftPercent;
		Ar << Node.bUseTopPercent;
		Ar << Node.bUseWidthPercent;
		Ar << Node.bUseHeightPercent;
		Ar << Node.bUseRight;
		Ar << Node.bUseBottom;
		Ar << Node.bUseFlexLayout;
	}
	if (GRuntimeUILayoutSerializingPayloadVersion >= 3)
	{
		Ar << Node.bLockAspectRatio;
	}
	return Ar;
}
