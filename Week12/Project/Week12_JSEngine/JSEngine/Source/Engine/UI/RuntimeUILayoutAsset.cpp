#include "UI/RuntimeUILayoutAsset.h"

#include "Asset/AssetFile.h"
#include "Core/Paths.h"
#include "Core/Guid.h"
#include "Math/Color.h"
#include "Math/Matrix.h"
#include "Serialization/Archive.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
int32 GRuntimeUILayoutSerializePayloadVersion = URuntimeUILayoutAsset::CurrentPayloadVersion;

bool IsRuntimeUILayoutAssetPath(const FString& Path)
{
	std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	FString Extension = FPaths::ToUtf8(FsPath.extension().wstring());
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char Ch)
	{
		return static_cast<char>(std::tolower(Ch));
	});
	return Extension == ".uasset";
}

FString GetRuntimeUILayoutDisplayName(const FString& Path)
{
	const std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	return FPaths::ToUtf8(FsPath.stem().wstring());
}

const char* GetWidgetTypeName(ERuntimeUIWidgetType Type)
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

const char* GetImageFitLayoutName(ERuntimeUIImageFit ImageFit)
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

const char* GetLayoutSizeRuleName(ERuntimeUILayoutSizeRule SizeRule)
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

const char* GetRmlTagName(ERuntimeUIWidgetType Type)
{
	switch (Type)
	{
	case ERuntimeUIWidgetType::Canvas:
	case ERuntimeUIWidgetType::Panel:
		return "div";
	case ERuntimeUIWidgetType::Text:
		return "div";
	case ERuntimeUIWidgetType::Image:
		return "img";
	case ERuntimeUIWidgetType::Button:
		return "button";
	default:
		return "div";
	}
}

FString EscapeXml(const FString& Text)
{
	FString Result;
	Result.reserve(Text.size());
	for (const char Ch : Text)
	{
		switch (Ch)
		{
		case '&':
			Result += "&amp;";
			break;
		case '<':
			Result += "&lt;";
			break;
		case '>':
			Result += "&gt;";
			break;
		case '"':
			Result += "&quot;";
			break;
		case '\'':
			Result += "&apos;";
			break;
		default:
			Result.push_back(Ch);
			break;
		}
	}
	return Result;
}

FString EscapeRcssString(const FString& Text)
{
	FString Result;
	Result.reserve(Text.size());
	for (const char Ch : Text)
	{
		if (Ch == '\\' || Ch == '"')
		{
			Result.push_back('\\');
		}
		Result.push_back(Ch);
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
	return Result.empty() ? "Widget" : Result;
}

const char* GetImageFitName(ERuntimeUIImageFit ImageFit)
{
	switch (ImageFit)
	{
	case ERuntimeUIImageFit::Contain:
		return "contain";
	case ERuntimeUIImageFit::Cover:
		return "cover";
	case ERuntimeUIImageFit::Stretch:
	default:
		return "stretch";
	}
}

const char* GetObjectFitName(ERuntimeUIImageFit ImageFit)
{
	switch (ImageFit)
	{
	case ERuntimeUIImageFit::Contain:
		return "contain";
	case ERuntimeUIImageFit::Cover:
		return "cover";
	case ERuntimeUIImageFit::Stretch:
	default:
		return "fill";
	}
}

bool IsLayoutContainer(const FRuntimeUIWidgetNode& Node)
{
	return (Node.Type == ERuntimeUIWidgetType::Canvas || Node.Type == ERuntimeUIWidgetType::Panel)
		&& Node.LayoutMode != ERuntimeUILayoutMode::Free;
}

const char* GetLayoutDirectionName(ERuntimeUILayoutMode LayoutMode)
{
	switch (LayoutMode)
	{
	case ERuntimeUILayoutMode::Horizontal:
		return "row";
	case ERuntimeUILayoutMode::Vertical:
		return "column";
	case ERuntimeUILayoutMode::Free:
	default:
		return "row";
	}
}

const char* GetLayoutAlignItemsName(ERuntimeUILayoutAlignment Alignment)
{
	switch (Alignment)
	{
	case ERuntimeUILayoutAlignment::Center:
		return "center";
	case ERuntimeUILayoutAlignment::End:
		return "flex-end";
	case ERuntimeUILayoutAlignment::Stretch:
		return "stretch";
	case ERuntimeUILayoutAlignment::Start:
	default:
		return "flex-start";
	}
}

FVector4 AdjustRuntimeUIColor(const FVector4& Color, float RgbScale, float AlphaScale = 1.0f)
{
	return FVector4(
		std::clamp(Color.X * RgbScale, 0.0f, 1.0f),
		std::clamp(Color.Y * RgbScale, 0.0f, 1.0f),
		std::clamp(Color.Z * RgbScale, 0.0f, 1.0f),
		std::clamp(Color.W * AlphaScale, 0.0f, 1.0f));
}

FRuntimeUIButtonStateStyle MakeButtonStateStyle(const FRuntimeUIWidgetNode& Node, float BackgroundScale, float TextScale, float BorderScale, float AlphaScale = 1.0f)
{
	FRuntimeUIButtonStateStyle Style;
	Style.BackgroundColor = AdjustRuntimeUIColor(Node.BackgroundColor, BackgroundScale, AlphaScale);
	Style.TextColor = AdjustRuntimeUIColor(Node.TextColor, TextScale, AlphaScale);
	Style.BorderColor = AdjustRuntimeUIColor(Node.BorderColor, BorderScale, AlphaScale);
	return Style;
}

void DeriveButtonStateStyles(FRuntimeUIWidgetNode& Node)
{
	Node.bUseButtonStateStyle = true;
	Node.ButtonHoverStyle = MakeButtonStateStyle(Node, 1.14f, 1.0f, 1.12f);
	Node.ButtonPressedStyle = MakeButtonStateStyle(Node, 0.82f, 0.94f, 0.88f);
	Node.ButtonDisabledStyle = MakeButtonStateStyle(Node, 0.52f, 0.62f, 0.45f, 0.58f);
}

void ApplyDefaultStyle(FRuntimeUIWidgetNode& Node)
{
	Node.bVisible = true;
	Node.bLocked = false;
	Node.BackgroundColor = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	Node.TextColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	Node.FontSize = Node.Type == ERuntimeUIWidgetType::Button ? 22.0f : 24.0f;
	Node.FontFamily = "Malgun Gothic";
	Node.FontWeight = Node.Type == ERuntimeUIWidgetType::Button ? 600 : 400;
	Node.LineHeight = 0.0f;
	Node.LetterSpacing = 0.0f;
	Node.bTextWrap = false;
	Node.TextAlign = "center";
	Node.ImageTint = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	Node.ImageFit = ERuntimeUIImageFit::Stretch;
	Node.Opacity = 1.0f;
	Node.BorderColor = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
	Node.BorderWidth = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	Node.BorderRadius = 0.0f;
	Node.LayoutMode = ERuntimeUILayoutMode::Free;
	Node.LayoutAlignment = ERuntimeUILayoutAlignment::Start;
	Node.LayoutSizeRule = ERuntimeUILayoutSizeRule::Auto;
	Node.LayoutPadding = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	Node.LayoutGap = 8.0f;
	Node.LayoutFillWeight = 1.0f;
	if (Node.Type == ERuntimeUIWidgetType::Panel)
	{
		Node.BackgroundColor = FVector4(0.125f, 0.141f, 0.173f, 0.82f);
	}
	else if (Node.Type == ERuntimeUIWidgetType::Button)
	{
		Node.BackgroundColor = FVector4(0.235f, 0.329f, 0.502f, 0.9f);
	}
	DeriveButtonStateStyles(Node);
}

FString ToRcssColor(const FVector4& Color)
{
	const int32 R = static_cast<int32>(std::round(std::clamp(Color.X, 0.0f, 1.0f) * 255.0f));
	const int32 G = static_cast<int32>(std::round(std::clamp(Color.Y, 0.0f, 1.0f) * 255.0f));
	const int32 B = static_cast<int32>(std::round(std::clamp(Color.Z, 0.0f, 1.0f) * 255.0f));
	const int32 A = static_cast<int32>(std::round(std::clamp(Color.W, 0.0f, 1.0f) * 255.0f));
	return "rgba(" + std::to_string(R) + ", " + std::to_string(G) + ", " + std::to_string(B) + ", " + std::to_string(A) + ")";
}

FString ToRcssLength(float Pixels)
{
	if (std::abs(Pixels) < 0.001f)
	{
		return "0px";
	}
	return std::to_string(Pixels) + "px";
}

FString ToRcssPercent(float Value)
{
	if (std::abs(Value) < 0.0001f)
	{
		return "0%";
	}
	if (std::abs(Value - 1.0f) < 0.0001f)
	{
		return "100%";
	}
	return std::to_string(Value * 100.0f) + "%";
}

FString ToRcssCalc(float Percent01, float PixelOffset)
{
	if (std::abs(PixelOffset) < 0.001f)
	{
		return ToRcssPercent(Percent01);
	}
	if (std::abs(Percent01) < 0.0001f)
	{
		return ToRcssLength(PixelOffset);
	}

	return "calc(" + ToRcssPercent(Percent01)
		+ (PixelOffset >= 0.0f ? " + " : " - ")
		+ ToRcssLength(std::abs(PixelOffset)) + ")";
}

FVector2 GetAuthoringParentSize(const TArray<FRuntimeUIWidgetNode>& Widgets, const FRuntimeUIWidgetNode& Node)
{
	if (Node.ParentIndex >= 0 && Node.ParentIndex < static_cast<int32>(Widgets.size()))
	{
		return Widgets[Node.ParentIndex].Size;
	}
	return Node.Size;
}

const char* GetDefaultRuntimeUIFontFamily()
{
	return "Malgun Gothic";
}

FString ToRcssFontFamily(const FString& FontFamily)
{
	const FString Family = FontFamily.empty() ? GetDefaultRuntimeUIFontFamily() : FontFamily;
	return "\"" + EscapeRcssString(Family) + "\"";
}

std::filesystem::path ToAbsoluteProjectPath(const FString& Path)
{
	std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	if (!FsPath.is_absolute())
	{
		FsPath = std::filesystem::path(FPaths::RootDir()) / FsPath;
	}
	return FsPath.lexically_normal();
}

json::JSON Vector2ToJson(const FVector2& Value)
{
	return json::Array(Value.X, Value.Y);
}

json::JSON Vector4ToJson(const FVector4& Value)
{
	return json::Array(Value.X, Value.Y, Value.Z, Value.W);
}

float JsonToFloat(const json::JSON& Value, float DefaultValue = 0.0f)
{
	if (Value.JSONType() == json::JSON::Class::Floating)
	{
		return static_cast<float>(Value.ToFloat());
	}
	if (Value.JSONType() == json::JSON::Class::Integral)
	{
		return static_cast<float>(Value.ToInt());
	}
	return DefaultValue;
}

int32 JsonToInt(const json::JSON& Value, int32 DefaultValue = 0)
{
	if (Value.JSONType() == json::JSON::Class::Integral)
	{
		return static_cast<int32>(Value.ToInt());
	}
	if (Value.JSONType() == json::JSON::Class::Floating)
	{
		return static_cast<int32>(Value.ToFloat());
	}
	return DefaultValue;
}

bool JsonToBool(const json::JSON& Value, bool DefaultValue = false)
{
	return Value.JSONType() == json::JSON::Class::Boolean ? Value.ToBool() : DefaultValue;
}

FString JsonToString(const json::JSON& Value, const FString& DefaultValue = FString())
{
	return Value.JSONType() == json::JSON::Class::String ? Value.ToString() : DefaultValue;
}

const json::JSON* FindJsonField(const json::JSON& Object, const char* Key)
{
	if (Object.JSONType() != json::JSON::Class::Object || !Object.hasKey(Key))
	{
		return nullptr;
	}
	return &Object.at(Key);
}

FString ReadJsonString(const json::JSON& Object, const char* Key, const FString& DefaultValue = FString())
{
	const json::JSON* Field = FindJsonField(Object, Key);
	return Field ? JsonToString(*Field, DefaultValue) : DefaultValue;
}

int32 ReadJsonInt(const json::JSON& Object, const char* Key, int32 DefaultValue = 0)
{
	const json::JSON* Field = FindJsonField(Object, Key);
	return Field ? JsonToInt(*Field, DefaultValue) : DefaultValue;
}

float ReadJsonFloat(const json::JSON& Object, const char* Key, float DefaultValue = 0.0f)
{
	const json::JSON* Field = FindJsonField(Object, Key);
	return Field ? JsonToFloat(*Field, DefaultValue) : DefaultValue;
}

bool ReadJsonBool(const json::JSON& Object, const char* Key, bool DefaultValue = false)
{
	const json::JSON* Field = FindJsonField(Object, Key);
	return Field ? JsonToBool(*Field, DefaultValue) : DefaultValue;
}

FVector2 ReadJsonVector2(const json::JSON& Object, const char* Key, const FVector2& DefaultValue = FVector2(0.0f, 0.0f))
{
	const json::JSON* Field = FindJsonField(Object, Key);
	if (!Field || Field->JSONType() != json::JSON::Class::Array || Field->length() < 2)
	{
		return DefaultValue;
	}
	return FVector2(JsonToFloat(Field->at(0), DefaultValue.X), JsonToFloat(Field->at(1), DefaultValue.Y));
}

FVector4 ReadJsonVector4(const json::JSON& Object, const char* Key, const FVector4& DefaultValue = FVector4(0.0f, 0.0f, 0.0f, 0.0f))
{
	const json::JSON* Field = FindJsonField(Object, Key);
	if (!Field || Field->JSONType() != json::JSON::Class::Array || Field->length() < 4)
	{
		return DefaultValue;
	}
	return FVector4(
		JsonToFloat(Field->at(0), DefaultValue.X),
		JsonToFloat(Field->at(1), DefaultValue.Y),
		JsonToFloat(Field->at(2), DefaultValue.Z),
		JsonToFloat(Field->at(3), DefaultValue.W));
}

ERuntimeUIWidgetType WidgetTypeFromString(const FString& Value, ERuntimeUIWidgetType DefaultValue = ERuntimeUIWidgetType::Panel)
{
	if (Value == "Canvas") return ERuntimeUIWidgetType::Canvas;
	if (Value == "Panel") return ERuntimeUIWidgetType::Panel;
	if (Value == "Text") return ERuntimeUIWidgetType::Text;
	if (Value == "Image") return ERuntimeUIWidgetType::Image;
	if (Value == "Button") return ERuntimeUIWidgetType::Button;
	return DefaultValue;
}

ERuntimeUIImageFit ImageFitFromString(const FString& Value, ERuntimeUIImageFit DefaultValue = ERuntimeUIImageFit::Stretch)
{
	if (Value == "Contain") return ERuntimeUIImageFit::Contain;
	if (Value == "Cover") return ERuntimeUIImageFit::Cover;
	if (Value == "Stretch") return ERuntimeUIImageFit::Stretch;
	return DefaultValue;
}

ERuntimeUILayoutMode LayoutModeFromString(const FString& Value, ERuntimeUILayoutMode DefaultValue = ERuntimeUILayoutMode::Free)
{
	if (Value == "Horizontal") return ERuntimeUILayoutMode::Horizontal;
	if (Value == "Vertical") return ERuntimeUILayoutMode::Vertical;
	if (Value == "Free") return ERuntimeUILayoutMode::Free;
	return DefaultValue;
}

ERuntimeUILayoutAlignment LayoutAlignmentFromString(const FString& Value, ERuntimeUILayoutAlignment DefaultValue = ERuntimeUILayoutAlignment::Start)
{
	if (Value == "Center") return ERuntimeUILayoutAlignment::Center;
	if (Value == "End") return ERuntimeUILayoutAlignment::End;
	if (Value == "Stretch") return ERuntimeUILayoutAlignment::Stretch;
	if (Value == "Start") return ERuntimeUILayoutAlignment::Start;
	return DefaultValue;
}

ERuntimeUILayoutSizeRule LayoutSizeRuleFromString(const FString& Value, ERuntimeUILayoutSizeRule DefaultValue = ERuntimeUILayoutSizeRule::Auto)
{
	if (Value == "Fill") return ERuntimeUILayoutSizeRule::Fill;
	if (Value == "Auto") return ERuntimeUILayoutSizeRule::Auto;
	return DefaultValue;
}

json::JSON ButtonStateStyleToJson(const FRuntimeUIButtonStateStyle& Style)
{
	json::JSON Object = json::Object();
	Object["BackgroundColor"] = Vector4ToJson(Style.BackgroundColor);
	Object["TextColor"] = Vector4ToJson(Style.TextColor);
	Object["BorderColor"] = Vector4ToJson(Style.BorderColor);
	return Object;
}

FRuntimeUIButtonStateStyle ButtonStateStyleFromJson(const json::JSON& Object, const FRuntimeUIButtonStateStyle& DefaultValue)
{
	FRuntimeUIButtonStateStyle Style = DefaultValue;
	if (Object.JSONType() != json::JSON::Class::Object)
	{
		return Style;
	}
	Style.BackgroundColor = ReadJsonVector4(Object, "BackgroundColor", Style.BackgroundColor);
	Style.TextColor = ReadJsonVector4(Object, "TextColor", Style.TextColor);
	Style.BorderColor = ReadJsonVector4(Object, "BorderColor", Style.BorderColor);
	return Style;
}

json::JSON WidgetNodeToJson(const FRuntimeUIWidgetNode& Node)
{
	json::JSON Object = json::Object();
	Object["Id"] = Node.Id;
	Object["DisplayName"] = Node.DisplayName;
	Object["Type"] = GetWidgetTypeName(Node.Type);
	Object["ParentIndex"] = Node.ParentIndex;
	json::JSON Children = json::Array();
	for (const int32 ChildIndex : Node.Children)
	{
		Children.append(ChildIndex);
	}
	Object["Children"] = Children;
	Object["AnchorMin"] = Vector2ToJson(Node.AnchorMin);
	Object["AnchorMax"] = Vector2ToJson(Node.AnchorMax);
	Object["Pivot"] = Vector2ToJson(Node.Pivot);
	Object["Position"] = Vector2ToJson(Node.Position);
	Object["Size"] = Vector2ToJson(Node.Size);
	Object["Rotation"] = Node.Rotation;
	Object["Scale"] = Vector2ToJson(Node.Scale);
	Object["Text"] = Node.Text;
	Object["ImagePath"] = Node.ImagePath;
	Object["StyleClass"] = Node.StyleClass;
	Object["Visible"] = Node.bVisible;
	Object["Locked"] = Node.bLocked;
	Object["BackgroundColor"] = Vector4ToJson(Node.BackgroundColor);
	Object["TextColor"] = Vector4ToJson(Node.TextColor);
	Object["FontSize"] = Node.FontSize;
	Object["FontFamily"] = Node.FontFamily;
	Object["FontWeight"] = Node.FontWeight;
	Object["LineHeight"] = Node.LineHeight;
	Object["LetterSpacing"] = Node.LetterSpacing;
	Object["TextWrap"] = Node.bTextWrap;
	Object["TextAlign"] = Node.TextAlign;
	Object["ImageTint"] = Vector4ToJson(Node.ImageTint);
	Object["ImageFit"] = GetImageFitLayoutName(Node.ImageFit);
	Object["Opacity"] = Node.Opacity;
	Object["BorderColor"] = Vector4ToJson(Node.BorderColor);
	Object["BorderWidth"] = Vector4ToJson(Node.BorderWidth);
	Object["BorderRadius"] = Node.BorderRadius;
	Object["LayoutMode"] = GetLayoutDirectionName(Node.LayoutMode);
	Object["LayoutAlignment"] = GetLayoutAlignItemsName(Node.LayoutAlignment);
	Object["LayoutSizeRule"] = GetLayoutSizeRuleName(Node.LayoutSizeRule);
	Object["LayoutPadding"] = Vector4ToJson(Node.LayoutPadding);
	Object["LayoutGap"] = Node.LayoutGap;
	Object["LayoutFillWeight"] = Node.LayoutFillWeight;
	Object["UseButtonStateStyle"] = Node.bUseButtonStateStyle;
	Object["ButtonHoverStyle"] = ButtonStateStyleToJson(Node.ButtonHoverStyle);
	Object["ButtonPressedStyle"] = ButtonStateStyleToJson(Node.ButtonPressedStyle);
	Object["ButtonDisabledStyle"] = ButtonStateStyleToJson(Node.ButtonDisabledStyle);
	Object["UseNineSlice"] = Node.bUseNineSlice;
	Object["NineSliceBorder"] = Vector4ToJson(Node.NineSliceBorder);
	Object["OnClickAction"] = Node.OnClickAction;
	return Object;
}

FRuntimeUIWidgetNode WidgetNodeFromJson(const json::JSON& Object)
{
	FRuntimeUIWidgetNode Node;
	ApplyDefaultStyle(Node);
	if (Object.JSONType() != json::JSON::Class::Object)
	{
		return Node;
	}

	Node.Id = ReadJsonString(Object, "Id", Node.Id);
	Node.DisplayName = ReadJsonString(Object, "DisplayName", Node.DisplayName);
	if (const json::JSON* TypeField = FindJsonField(Object, "Type"))
	{
		Node.Type = TypeField->JSONType() == json::JSON::Class::String
			? WidgetTypeFromString(TypeField->ToString(), Node.Type)
			: static_cast<ERuntimeUIWidgetType>(JsonToInt(*TypeField, static_cast<int32>(Node.Type)));
	}
	Node.ParentIndex = ReadJsonInt(Object, "ParentIndex", Node.ParentIndex);
	Node.Children.clear();
	if (const json::JSON* Children = FindJsonField(Object, "Children"))
	{
		if (Children->JSONType() == json::JSON::Class::Array)
		{
			for (int32 Index = 0; Index < Children->length(); ++Index)
			{
				Node.Children.push_back(JsonToInt(Children->at(Index), -1));
			}
		}
	}
	Node.AnchorMin = ReadJsonVector2(Object, "AnchorMin", Node.AnchorMin);
	Node.AnchorMax = ReadJsonVector2(Object, "AnchorMax", Node.AnchorMax);
	Node.Pivot = ReadJsonVector2(Object, "Pivot", Node.Pivot);
	Node.Position = ReadJsonVector2(Object, "Position", Node.Position);
	Node.Size = ReadJsonVector2(Object, "Size", Node.Size);
	Node.Rotation = ReadJsonFloat(Object, "Rotation", Node.Rotation);
	Node.Scale = ReadJsonVector2(Object, "Scale", Node.Scale);
	Node.Text = ReadJsonString(Object, "Text", Node.Text);
	Node.ImagePath = ReadJsonString(Object, "ImagePath", Node.ImagePath);
	Node.StyleClass = ReadJsonString(Object, "StyleClass", Node.StyleClass);
	Node.bVisible = ReadJsonBool(Object, "Visible", Node.bVisible);
	Node.bLocked = ReadJsonBool(Object, "Locked", Node.bLocked);
	Node.BackgroundColor = ReadJsonVector4(Object, "BackgroundColor", Node.BackgroundColor);
	Node.TextColor = ReadJsonVector4(Object, "TextColor", Node.TextColor);
	Node.FontSize = ReadJsonFloat(Object, "FontSize", Node.FontSize);
	Node.FontFamily = ReadJsonString(Object, "FontFamily", Node.FontFamily);
	Node.FontWeight = ReadJsonInt(Object, "FontWeight", Node.FontWeight);
	Node.LineHeight = ReadJsonFloat(Object, "LineHeight", Node.LineHeight);
	Node.LetterSpacing = ReadJsonFloat(Object, "LetterSpacing", Node.LetterSpacing);
	Node.bTextWrap = ReadJsonBool(Object, "TextWrap", Node.bTextWrap);
	Node.TextAlign = ReadJsonString(Object, "TextAlign", Node.TextAlign);
	Node.ImageTint = ReadJsonVector4(Object, "ImageTint", Node.ImageTint);
	if (const json::JSON* ImageFit = FindJsonField(Object, "ImageFit"))
	{
		Node.ImageFit = ImageFit->JSONType() == json::JSON::Class::String
			? ImageFitFromString(ImageFit->ToString(), Node.ImageFit)
			: static_cast<ERuntimeUIImageFit>(JsonToInt(*ImageFit, static_cast<int32>(Node.ImageFit)));
	}
	Node.Opacity = ReadJsonFloat(Object, "Opacity", Node.Opacity);
	Node.BorderColor = ReadJsonVector4(Object, "BorderColor", Node.BorderColor);
	Node.BorderWidth = ReadJsonVector4(Object, "BorderWidth", Node.BorderWidth);
	Node.BorderRadius = ReadJsonFloat(Object, "BorderRadius", Node.BorderRadius);
	if (const json::JSON* LayoutMode = FindJsonField(Object, "LayoutMode"))
	{
		Node.LayoutMode = LayoutMode->JSONType() == json::JSON::Class::String
			? LayoutModeFromString(LayoutMode->ToString(), Node.LayoutMode)
			: static_cast<ERuntimeUILayoutMode>(JsonToInt(*LayoutMode, static_cast<int32>(Node.LayoutMode)));
	}
	if (const json::JSON* LayoutAlignment = FindJsonField(Object, "LayoutAlignment"))
	{
		Node.LayoutAlignment = LayoutAlignment->JSONType() == json::JSON::Class::String
			? LayoutAlignmentFromString(LayoutAlignment->ToString(), Node.LayoutAlignment)
			: static_cast<ERuntimeUILayoutAlignment>(JsonToInt(*LayoutAlignment, static_cast<int32>(Node.LayoutAlignment)));
	}
	if (const json::JSON* LayoutSizeRule = FindJsonField(Object, "LayoutSizeRule"))
	{
		Node.LayoutSizeRule = LayoutSizeRule->JSONType() == json::JSON::Class::String
			? LayoutSizeRuleFromString(LayoutSizeRule->ToString(), Node.LayoutSizeRule)
			: static_cast<ERuntimeUILayoutSizeRule>(JsonToInt(*LayoutSizeRule, static_cast<int32>(Node.LayoutSizeRule)));
	}
	Node.LayoutPadding = ReadJsonVector4(Object, "LayoutPadding", Node.LayoutPadding);
	Node.LayoutGap = ReadJsonFloat(Object, "LayoutGap", Node.LayoutGap);
	Node.LayoutFillWeight = ReadJsonFloat(Object, "LayoutFillWeight", Node.LayoutFillWeight);
	Node.bUseButtonStateStyle = ReadJsonBool(Object, "UseButtonStateStyle", Node.bUseButtonStateStyle);
	if (const json::JSON* Style = FindJsonField(Object, "ButtonHoverStyle"))
	{
		Node.ButtonHoverStyle = ButtonStateStyleFromJson(*Style, Node.ButtonHoverStyle);
	}
	if (const json::JSON* Style = FindJsonField(Object, "ButtonPressedStyle"))
	{
		Node.ButtonPressedStyle = ButtonStateStyleFromJson(*Style, Node.ButtonPressedStyle);
	}
	if (const json::JSON* Style = FindJsonField(Object, "ButtonDisabledStyle"))
	{
		Node.ButtonDisabledStyle = ButtonStateStyleFromJson(*Style, Node.ButtonDisabledStyle);
	}
	Node.bUseNineSlice = ReadJsonBool(Object, "UseNineSlice", Node.bUseNineSlice);
	Node.NineSliceBorder = ReadJsonVector4(Object, "NineSliceBorder", Node.NineSliceBorder);
	Node.OnClickAction = ReadJsonString(Object, "OnClickAction", Node.OnClickAction);
	return Node;
}

void AppendIndent(std::ostringstream& Stream, int32 Depth)
{
	for (int32 Index = 0; Index < Depth; ++Index)
	{
		Stream << "    ";
	}
}

bool IsWidgetExported(const TArray<FRuntimeUIWidgetNode>& Widgets, int32 WidgetIndex)
{
	int32 CurrentIndex = WidgetIndex;
	while (CurrentIndex >= 0 && CurrentIndex < static_cast<int32>(Widgets.size()))
	{
		const FRuntimeUIWidgetNode& Node = Widgets[CurrentIndex];
		if (!Node.bVisible)
		{
			return false;
		}
		CurrentIndex = Node.ParentIndex;
	}
	return true;
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
	const FString SafeId = ToCssId(Node.Id);
	const char* TagName = GetRmlTagName(Node.Type);

	AppendIndent(Stream, Depth);
	Stream << "<" << TagName << " id=\"" << EscapeXml(SafeId) << "\"";
	Stream << " data-ui-type=\"" << EscapeXml(GetWidgetTypeName(Node.Type)) << "\"";
	if (!Node.DisplayName.empty())
	{
		Stream << " data-ui-name=\"" << EscapeXml(Node.DisplayName) << "\"";
	}
	if (!Node.StyleClass.empty())
	{
		Stream << " class=\"" << EscapeXml(Node.StyleClass) << "\"";
	}
	if (Node.Type == ERuntimeUIWidgetType::Image && !Node.ImagePath.empty())
	{
		Stream << " data-ui-image=\"" << EscapeXml(Node.ImagePath) << "\"";
		Stream << " data-ui-fit=\"" << EscapeXml(GetImageFitName(Node.ImageFit)) << "\"";
		Stream << " src=\"" << EscapeXml(Node.ImagePath) << "\"";
	}
	if (!Node.OnClickAction.empty())
	{
		Stream << " data-action=\"" << EscapeXml(Node.OnClickAction) << "\"";
	}
	if (Node.Type == ERuntimeUIWidgetType::Image)
	{
		Stream << " />\n";
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

bool HasFollowingVisibleLayoutSibling(const TArray<FRuntimeUIWidgetNode>& Widgets, const FRuntimeUIWidgetNode& Node)
{
	if (Node.ParentIndex < 0 || Node.ParentIndex >= static_cast<int32>(Widgets.size()))
	{
		return false;
	}

	const FRuntimeUIWidgetNode& ParentNode = Widgets[Node.ParentIndex];
	if (!IsLayoutContainer(ParentNode))
	{
		return false;
	}

	bool bFoundSelf = false;
	for (const int32 ChildIndex : ParentNode.Children)
	{
		if (ChildIndex < 0 || ChildIndex >= static_cast<int32>(Widgets.size()))
		{
			continue;
		}

		const FRuntimeUIWidgetNode& ChildNode = Widgets[ChildIndex];
		if (&ChildNode == &Node)
		{
			bFoundSelf = true;
			continue;
		}
		if (bFoundSelf && ChildNode.bVisible)
		{
			return true;
		}
	}
	return false;
}

void AppendRcssNode(std::ostringstream& Stream, const TArray<FRuntimeUIWidgetNode>& Widgets, const FRuntimeUIWidgetNode& Node)
{
	if (!Node.bVisible)
	{
		return;
	}

	const FString SafeId = ToCssId(Node.Id);
	const FRuntimeUIWidgetNode* ParentNode = Node.ParentIndex >= 0 && Node.ParentIndex < static_cast<int32>(Widgets.size())
		? &Widgets[Node.ParentIndex]
		: nullptr;
	const bool bManagedByParent = ParentNode && IsLayoutContainer(*ParentNode);

	Stream << "#" << SafeId << " {\n";
	Stream << "    position: " << (bManagedByParent ? "relative" : "absolute") << ";\n";
	Stream << "    display: " << (IsLayoutContainer(Node) ? "flex" : "block") << ";\n";
	Stream << "    box-sizing: border-box;\n";
	Stream << "    margin: 0px;\n";
	Stream << "    padding: 0px;\n";
	Stream << "    overflow: hidden;\n";
	Stream << "    transform-origin: " << (Node.Pivot.X * 100.0f) << "% " << (Node.Pivot.Y * 100.0f) << "%;\n";
	if (IsLayoutContainer(Node))
	{
		Stream << "    flex-direction: " << GetLayoutDirectionName(Node.LayoutMode) << ";\n";
		Stream << "    align-items: " << GetLayoutAlignItemsName(Node.LayoutAlignment) << ";\n";
		Stream << "    padding: "
			<< Node.LayoutPadding.Y << "px "
			<< Node.LayoutPadding.Z << "px "
			<< Node.LayoutPadding.W << "px "
			<< Node.LayoutPadding.X << "px;\n";
	}
	if (bManagedByParent)
	{
		const bool bFill = Node.LayoutSizeRule == ERuntimeUILayoutSizeRule::Fill;
		const bool bHasFollowingSibling = HasFollowingVisibleLayoutSibling(Widgets, Node);
		if (bFill)
		{
			Stream << "    flex: " << std::max(0.01f, Node.LayoutFillWeight) << " 1 0px;\n";
		}
		else
		{
			Stream << "    flex: 0 0 auto;\n";
		}
		if (ParentNode->LayoutMode == ERuntimeUILayoutMode::Horizontal)
		{
			if (ParentNode->LayoutAlignment == ERuntimeUILayoutAlignment::Stretch)
			{
				Stream << "    height: auto;\n";
			}
			else
			{
				Stream << "    height: " << Node.Size.Y << "px;\n";
			}
			if (!bFill)
			{
				Stream << "    width: " << Node.Size.X << "px;\n";
			}
			if (bHasFollowingSibling)
			{
				Stream << "    margin-right: " << ParentNode->LayoutGap << "px;\n";
			}
		}
		else
		{
			if (ParentNode->LayoutAlignment == ERuntimeUILayoutAlignment::Stretch)
			{
				Stream << "    width: auto;\n";
			}
			else
			{
				Stream << "    width: " << Node.Size.X << "px;\n";
			}
			if (!bFill)
			{
				Stream << "    height: " << Node.Size.Y << "px;\n";
			}
			if (bHasFollowingSibling)
			{
				Stream << "    margin-bottom: " << ParentNode->LayoutGap << "px;\n";
			}
		}
	}
	else if (Node.Type == ERuntimeUIWidgetType::Canvas)
	{
		Stream << "    left: 0px;\n";
		Stream << "    top: 0px;\n";
		Stream << "    width: 100%;\n";
		Stream << "    height: 100%;\n";
	}
	else
	{
		const FVector2 ParentSize = GetAuthoringParentSize(Widgets, Node);
		const float LeftOffset = Node.Position.X - ParentSize.X * Node.AnchorMin.X;
		const float TopOffset = Node.Position.Y - ParentSize.Y * Node.AnchorMin.Y;
		const float RightOffset = ParentSize.X * Node.AnchorMax.X - (Node.Position.X + Node.Size.X);
		const float BottomOffset = ParentSize.Y * Node.AnchorMax.Y - (Node.Position.Y + Node.Size.Y);
		const bool bStretchX = std::abs(Node.AnchorMax.X - Node.AnchorMin.X) > 0.0001f;
		const bool bStretchY = std::abs(Node.AnchorMax.Y - Node.AnchorMin.Y) > 0.0001f;

		Stream << "    left: " << ToRcssCalc(Node.AnchorMin.X, LeftOffset) << ";\n";
		Stream << "    top: " << ToRcssCalc(Node.AnchorMin.Y, TopOffset) << ";\n";
		if (bStretchX)
		{
			Stream << "    right: " << ToRcssCalc(1.0f - Node.AnchorMax.X, RightOffset) << ";\n";
		}
		else
		{
			Stream << "    width: " << Node.Size.X << "px;\n";
		}
		if (bStretchY)
		{
			Stream << "    bottom: " << ToRcssCalc(1.0f - Node.AnchorMax.Y, BottomOffset) << ";\n";
		}
		else
		{
			Stream << "    height: " << Node.Size.Y << "px;\n";
		}
	}
	if (std::abs(Node.Rotation) > 0.001f || std::abs(Node.Scale.X - 1.0f) > 0.001f || std::abs(Node.Scale.Y - 1.0f) > 0.001f)
	{
		Stream << "    transform: rotate(" << Node.Rotation << "deg) scale(" << Node.Scale.X << ", " << Node.Scale.Y << ");\n";
	}
	if (Node.BackgroundColor.W > 0.0f)
	{
		Stream << "    background-color: " << ToRcssColor(Node.BackgroundColor) << ";\n";
	}
	if (Node.Opacity < 0.999f)
	{
		Stream << "    opacity: " << std::clamp(Node.Opacity, 0.0f, 1.0f) << ";\n";
	}
	const float TotalBorderWidth = Node.BorderWidth.X + Node.BorderWidth.Y + Node.BorderWidth.Z + Node.BorderWidth.W;
	if (TotalBorderWidth > 0.0f)
	{
		Stream << "    border-width: "
			<< Node.BorderWidth.X << "px "
			<< Node.BorderWidth.Y << "px "
			<< Node.BorderWidth.Z << "px "
			<< Node.BorderWidth.W << "px;\n";
		Stream << "    border-color: " << ToRcssColor(Node.BorderColor) << ";\n";
	}
	if (Node.BorderRadius > 0.0f)
	{
		Stream << "    border-radius: " << Node.BorderRadius << "px;\n";
	}
	if (Node.Type == ERuntimeUIWidgetType::Text || Node.Type == ERuntimeUIWidgetType::Button)
	{
		const float EffectiveLineHeight = Node.LineHeight > 0.0f ? Node.LineHeight : std::max(1.0f, Node.Size.Y);
		Stream << "    color: " << ToRcssColor(Node.TextColor) << ";\n";
		Stream << "    font-size: " << std::max(1.0f, Node.FontSize) << "px;\n";
		Stream << "    font-family: " << ToRcssFontFamily(Node.FontFamily) << ";\n";
		Stream << "    font-weight: " << std::clamp(Node.FontWeight, 100, 900) << ";\n";
		Stream << "    text-align: " << (Node.TextAlign.empty() ? "center" : Node.TextAlign) << ";\n";
		Stream << "    line-height: " << std::max(1.0f, EffectiveLineHeight) << "px;\n";
		if (std::abs(Node.LetterSpacing) > 0.001f)
		{
			Stream << "    letter-spacing: " << Node.LetterSpacing << "px;\n";
		}
		Stream << "    white-space: " << (Node.bTextWrap ? "normal" : "nowrap") << ";\n";
	}
	if (Node.Type == ERuntimeUIWidgetType::Image)
	{
		Stream << "    image-color: " << ToRcssColor(Node.ImageTint) << ";\n";
		Stream << "    object-fit: " << GetObjectFitName(Node.ImageFit) << ";\n";
	}
	if (Node.bUseNineSlice && Node.NineSliceBorder.X + Node.NineSliceBorder.Y + Node.NineSliceBorder.Z + Node.NineSliceBorder.W > 0.0f)
	{
		if (TotalBorderWidth <= 0.0f)
		{
			Stream << "    border-width: "
				<< Node.NineSliceBorder.X << "px "
				<< Node.NineSliceBorder.Y << "px "
				<< Node.NineSliceBorder.Z << "px "
				<< Node.NineSliceBorder.W << "px;\n";
		}
	}
	Stream << "}\n\n";

	if (Node.Type == ERuntimeUIWidgetType::Button && Node.bUseButtonStateStyle)
	{
		auto AppendState = [&Stream, &SafeId](const char* PseudoClass, const FRuntimeUIButtonStateStyle& Style)
		{
			Stream << "#" << SafeId << ":" << PseudoClass << " {\n";
			if (Style.BackgroundColor.W > 0.0f)
			{
				Stream << "    background-color: " << ToRcssColor(Style.BackgroundColor) << ";\n";
			}
			Stream << "    color: " << ToRcssColor(Style.TextColor) << ";\n";
			if (Style.BorderColor.W > 0.0f)
			{
				Stream << "    border-color: " << ToRcssColor(Style.BorderColor) << ";\n";
			}
			Stream << "}\n\n";
		};

		AppendState("hover", Node.ButtonHoverStyle);
		AppendState("active", Node.ButtonPressedStyle);
		AppendState("disabled", Node.ButtonDisabledStyle);
	}
}
}

URuntimeUILayoutAsset::URuntimeUILayoutAsset()
{
	ResetToDefault();
}

void URuntimeUILayoutAsset::Serialize(FArchive& Ar)
{
	UObject::Serialize(Ar);
	Ar << "AssetPath" << AssetPath;
	Ar << "GeneratedRmlPath" << GeneratedRmlPath;
	Ar << "GeneratedRcssPath" << GeneratedRcssPath;
	Ar << "CanvasSize" << CanvasSize;
	Ar << "Widgets" << Widgets;

	if (Ar.IsLoading())
	{
		RebuildChildrenFromParents();
		if (Widgets.empty())
		{
			ResetToDefault();
		}
	}
}

void URuntimeUILayoutAsset::ResetToDefault()
{
	Widgets.clear();

	FRuntimeUIWidgetNode Canvas;
	Canvas.Id = "RootCanvas";
	Canvas.DisplayName = "Root Canvas";
	Canvas.Type = ERuntimeUIWidgetType::Canvas;
	Canvas.ParentIndex = -1;
	Canvas.AnchorMin = FVector2(0.0f, 0.0f);
	Canvas.AnchorMax = FVector2(1.0f, 1.0f);
	Canvas.Pivot = FVector2(0.0f, 0.0f);
	Canvas.Position = FVector2(0.0f, 0.0f);
	Canvas.Size = CanvasSize;
	ApplyDefaultStyle(Canvas);
	Widgets.push_back(Canvas);

	AddWidget(ERuntimeUIWidgetType::Button, 0);
	FRuntimeUIWidgetNode* Button = GetMutableWidget(1);
	if (Button)
	{
		Button->Id = "StartButton";
		Button->DisplayName = "Start Button";
		Button->Position = FVector2(120.0f, 120.0f);
		Button->Size = FVector2(240.0f, 64.0f);
		Button->Text = "Start";
		Button->OnClickAction = "StartGame";
	}
}

int32 URuntimeUILayoutAsset::AddWidget(ERuntimeUIWidgetType Type, int32 ParentIndex)
{
	if (!IsValidWidgetIndex(ParentIndex))
	{
		ParentIndex = Widgets.empty() ? -1 : 0;
	}

	FRuntimeUIWidgetNode Node;
	Node.Type = Type;
	Node.ParentIndex = ParentIndex;
	Node.Id = MakeUniqueWidgetId(Type);
	Node.DisplayName = GetWidgetTypeName(Type);
	Node.Text = Type == ERuntimeUIWidgetType::Text ? "Text" : (Type == ERuntimeUIWidgetType::Button ? "Button" : "");
	Node.Size = Type == ERuntimeUIWidgetType::Text ? FVector2(160.0f, 32.0f) : FVector2(180.0f, 56.0f);
	ApplyDefaultStyle(Node);

	const int32 NewIndex = static_cast<int32>(Widgets.size());
	Widgets.push_back(Node);
	if (IsValidWidgetIndex(ParentIndex))
	{
		Widgets[ParentIndex].Children.push_back(NewIndex);
	}
	return NewIndex;
}

int32 URuntimeUILayoutAsset::DuplicateWidget(int32 WidgetIndex)
{
	if (!IsValidWidgetIndex(WidgetIndex) || WidgetIndex == 0)
	{
		return -1;
	}

	const int32 ParentIndex = IsValidWidgetIndex(Widgets[WidgetIndex].ParentIndex) ? Widgets[WidgetIndex].ParentIndex : 0;
	auto DuplicateSubtree = [this](auto&& Self, int32 SourceIndex, int32 NewParentIndex, bool bOffsetRoot) -> int32
	{
		if (!IsValidWidgetIndex(SourceIndex))
		{
			return -1;
		}

		FRuntimeUIWidgetNode Clone = Widgets[SourceIndex];
		const TArray<int32> SourceChildren = Clone.Children;
		Clone.Id = MakeUniqueWidgetId(Clone.Type);
		Clone.DisplayName += " Copy";
		Clone.ParentIndex = NewParentIndex;
		Clone.Children.clear();
		if (bOffsetRoot)
		{
			Clone.Position += FVector2(20.0f, 20.0f);
		}

		const int32 NewIndex = static_cast<int32>(Widgets.size());
		Widgets.push_back(Clone);
		if (IsValidWidgetIndex(NewParentIndex))
		{
			Widgets[NewParentIndex].Children.push_back(NewIndex);
		}

		for (const int32 ChildIndex : SourceChildren)
		{
			Self(Self, ChildIndex, NewIndex, false);
		}

		return NewIndex;
	};

	return DuplicateSubtree(DuplicateSubtree, WidgetIndex, ParentIndex, true);
}

bool URuntimeUILayoutAsset::RemoveWidget(int32 WidgetIndex)
{
	if (!IsValidWidgetIndex(WidgetIndex) || WidgetIndex == 0)
	{
		return false;
	}

	TArray<bool> bRemove;
	bRemove.resize(Widgets.size(), false);
	auto MarkForRemoval = [this, &bRemove](auto&& Self, int32 Index) -> void
	{
		if (!IsValidWidgetIndex(Index) || bRemove[Index])
		{
			return;
		}
		bRemove[Index] = true;
		for (const int32 ChildIndex : Widgets[Index].Children)
		{
			Self(Self, ChildIndex);
		}
	};
	MarkForRemoval(MarkForRemoval, WidgetIndex);

	TArray<FRuntimeUIWidgetNode> NewWidgets;
	NewWidgets.reserve(Widgets.size());
	TArray<int32> Remap;
	Remap.resize(Widgets.size(), -1);
	for (int32 Index = 0; Index < static_cast<int32>(Widgets.size()); ++Index)
	{
		if (bRemove[Index])
		{
			continue;
		}
		Remap[Index] = static_cast<int32>(NewWidgets.size());
		NewWidgets.push_back(Widgets[Index]);
	}

	for (FRuntimeUIWidgetNode& Node : NewWidgets)
	{
		Node.ParentIndex = Node.ParentIndex >= 0 && Node.ParentIndex < static_cast<int32>(Remap.size())
			? Remap[Node.ParentIndex]
			: -1;
	}

	Widgets = std::move(NewWidgets);
	RebuildChildrenFromParents();
	return true;
}

bool URuntimeUILayoutAsset::SetWidgetParent(int32 WidgetIndex, int32 NewParentIndex)
{
	if (!IsValidWidgetIndex(WidgetIndex) || WidgetIndex == 0 || !IsValidWidgetIndex(NewParentIndex))
	{
		return false;
	}
	if (WouldCreateParentCycle(WidgetIndex, NewParentIndex))
	{
		return false;
	}

	Widgets[WidgetIndex].ParentIndex = NewParentIndex;
	RebuildChildrenFromParents();
	return true;
}

int32 URuntimeUILayoutAsset::MoveWidgetWithinParent(int32 WidgetIndex, int32 Direction)
{
	if (!IsValidWidgetIndex(WidgetIndex) || WidgetIndex == 0 || Direction == 0)
	{
		return WidgetIndex;
	}

	const int32 ParentIndex = Widgets[WidgetIndex].ParentIndex;
	if (!IsValidWidgetIndex(ParentIndex))
	{
		return WidgetIndex;
	}

	const TArray<int32>& Siblings = Widgets[ParentIndex].Children;
	const auto FoundIt = std::find(Siblings.begin(), Siblings.end(), WidgetIndex);
	if (FoundIt == Siblings.end())
	{
		return WidgetIndex;
	}

	const int32 CurrentSibling = static_cast<int32>(FoundIt - Siblings.begin());
	const int32 TargetSibling = Direction < 0 ? CurrentSibling - 1 : CurrentSibling + 1;
	if (TargetSibling < 0 || TargetSibling >= static_cast<int32>(Siblings.size()))
	{
		return WidgetIndex;
	}

	const int32 TargetWidgetIndex = Siblings[TargetSibling];
	SwapWidgetIndices(WidgetIndex, TargetWidgetIndex);
	RebuildChildrenFromParents();
	return TargetWidgetIndex;
}

int32 URuntimeUILayoutAsset::MoveWidgetRelativeToSibling(int32 WidgetIndex, int32 TargetSiblingIndex, bool bAfter)
{
	if (!IsValidWidgetIndex(WidgetIndex) || WidgetIndex == 0 || !IsValidWidgetIndex(TargetSiblingIndex) || WidgetIndex == TargetSiblingIndex)
	{
		return WidgetIndex;
	}

	const int32 ParentIndex = Widgets[WidgetIndex].ParentIndex;
	if (!IsValidWidgetIndex(ParentIndex) || Widgets[TargetSiblingIndex].ParentIndex != ParentIndex)
	{
		return WidgetIndex;
	}

	TArray<int32> NewOrder;
	NewOrder.reserve(Widgets.size());
	for (int32 Index = 0; Index < static_cast<int32>(Widgets.size()); ++Index)
	{
		if (Index != WidgetIndex)
		{
			NewOrder.push_back(Index);
		}
	}

	auto TargetIt = std::find(NewOrder.begin(), NewOrder.end(), TargetSiblingIndex);
	if (TargetIt == NewOrder.end())
	{
		return WidgetIndex;
	}
	if (bAfter)
	{
		++TargetIt;
	}
	NewOrder.insert(TargetIt, WidgetIndex);

	TArray<int32> OldToNew;
	OldToNew.resize(Widgets.size(), -1);
	for (int32 NewIndex = 0; NewIndex < static_cast<int32>(NewOrder.size()); ++NewIndex)
	{
		OldToNew[NewOrder[NewIndex]] = NewIndex;
	}

	TArray<FRuntimeUIWidgetNode> ReorderedWidgets;
	ReorderedWidgets.reserve(Widgets.size());
	for (const int32 OldIndex : NewOrder)
	{
		ReorderedWidgets.push_back(Widgets[OldIndex]);
	}

	for (FRuntimeUIWidgetNode& Node : ReorderedWidgets)
	{
		if (Node.ParentIndex >= 0 && Node.ParentIndex < static_cast<int32>(OldToNew.size()))
		{
			Node.ParentIndex = OldToNew[Node.ParentIndex];
		}
	}

	Widgets = std::move(ReorderedWidgets);
	RebuildChildrenFromParents();
	return OldToNew[WidgetIndex];
}

FRuntimeUIWidgetNode* URuntimeUILayoutAsset::GetMutableWidget(int32 WidgetIndex)
{
	return IsValidWidgetIndex(WidgetIndex) ? &Widgets[WidgetIndex] : nullptr;
}

const FRuntimeUIWidgetNode* URuntimeUILayoutAsset::GetWidget(int32 WidgetIndex) const
{
	return IsValidWidgetIndex(WidgetIndex) ? &Widgets[WidgetIndex] : nullptr;
}

bool URuntimeUILayoutAsset::SaveToFile(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!IsRuntimeUILayoutAssetPath(NormalizedPath))
	{
		return false;
	}

	AssetPath = NormalizedPath;

	FAssetMetaData MetaData;
	FAssetMetaData ExistingMetaData;
	MetaData.AssetGuid = FAssetFile::LoadMetadataOnly(NormalizedPath, ExistingMetaData) && !ExistingMetaData.AssetGuid.empty()
		? ExistingMetaData.AssetGuid
		: FGuid::NewGuid().ToString();
	MetaData.ClassName = "RuntimeUILayout";
	MetaData.DisplayName = GetRuntimeUILayoutDisplayName(NormalizedPath);
	MetaData.PayloadVersion = CurrentPayloadVersion;

	return FAssetFile::Save(NormalizedPath, MetaData, [this](FArchive& Ar)
	{
		const int32 PreviousPayloadVersion = GRuntimeUILayoutSerializePayloadVersion;
		GRuntimeUILayoutSerializePayloadVersion = CurrentPayloadVersion;
		Serialize(Ar);
		GRuntimeUILayoutSerializePayloadVersion = PreviousPayloadVersion;
		return true;
	});
}

bool URuntimeUILayoutAsset::LoadFromFile(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!IsRuntimeUILayoutAssetPath(NormalizedPath))
	{
		return false;
	}

	FAssetMetaData MetaData;
	const bool bLoaded = FAssetFile::Load(NormalizedPath, MetaData, [this, &MetaData](FArchive& Ar)
	{
		if (!MetaData.ClassName.empty() && MetaData.ClassName != "RuntimeUILayout")
		{
			return false;
		}
		const int32 PreviousPayloadVersion = GRuntimeUILayoutSerializePayloadVersion;
		GRuntimeUILayoutSerializePayloadVersion = MetaData.PayloadVersion > 0 ? MetaData.PayloadVersion : CurrentPayloadVersion;
		Serialize(Ar);
		GRuntimeUILayoutSerializePayloadVersion = PreviousPayloadVersion;
		return true;
	});
	if (bLoaded)
	{
		AssetPath = NormalizedPath;
		return true;
	}

	return false;
}

bool URuntimeUILayoutAsset::ValidateForExport(FString* OutError) const
{
	if (Widgets.empty())
	{
		if (OutError)
		{
			*OutError = "Runtime UI layout has no widgets.";
		}
		return false;
	}

	TArray<FString> ExportedIds;
	ExportedIds.reserve(Widgets.size());
	for (int32 Index = 0; Index < static_cast<int32>(Widgets.size()); ++Index)
	{
		if (!IsWidgetExported(Widgets, Index))
		{
			continue;
		}

		const FRuntimeUIWidgetNode& Node = Widgets[Index];
		if (Node.Id.empty())
		{
			if (OutError)
			{
				const FString Label = Node.DisplayName.empty() ? GetWidgetTypeName(Node.Type) : Node.DisplayName;
				*OutError = "Runtime UI widget has empty id: " + Label;
			}
			return false;
		}

		const FString SafeId = ToCssId(Node.Id);
		if (std::find(ExportedIds.begin(), ExportedIds.end(), SafeId) != ExportedIds.end())
		{
			if (OutError)
			{
				*OutError = "Runtime UI widget id collides after export: " + SafeId;
			}
			return false;
		}
		ExportedIds.push_back(SafeId);
	}
	return true;
}

bool URuntimeUILayoutAsset::ExportRmlAndRcss(const FString& RmlPath, const FString& RcssPath, FString* OutError) const
{
	if (!ValidateForExport(OutError))
	{
		return false;
	}

	std::ostringstream Rml;
	Rml << "<rml>\n";
	Rml << "<head>\n";
	Rml << "    <link type=\"text/rcss\" href=\"" << EscapeXml(RcssPath) << "\" />\n";
	Rml << "</head>\n";
	Rml << "<body>\n";
	AppendRmlNode(Rml, Widgets, 0, 1);
	Rml << "</body>\n";
	Rml << "</rml>\n";

	std::ostringstream Rcss;
	Rcss << "/* Generated from Runtime UI Layout Asset. Do not edit by hand. */\n\n";
	Rcss << "body {\n";
	Rcss << "    margin: 0px;\n";
	Rcss << "    width: 100%;\n";
	Rcss << "    height: 100%;\n";
	Rcss << "    font-family: " << ToRcssFontFamily(GetDefaultRuntimeUIFontFamily()) << ";\n";
	Rcss << "    color: #ffffff;\n";
	Rcss << "}\n\n";
	for (int32 Index = 0; Index < static_cast<int32>(Widgets.size()); ++Index)
	{
		if (IsWidgetExported(Widgets, Index))
		{
			AppendRcssNode(Rcss, Widgets, Widgets[Index]);
		}
	}

	const std::filesystem::path AbsoluteRmlPath = ToAbsoluteProjectPath(RmlPath);
	const std::filesystem::path AbsoluteRcssPath = ToAbsoluteProjectPath(RcssPath);
	std::error_code Ec;
	std::filesystem::create_directories(AbsoluteRmlPath.parent_path(), Ec);
	if (Ec)
	{
		if (OutError)
		{
			*OutError = "Failed to create RML export directory.";
		}
		return false;
	}
	std::filesystem::create_directories(AbsoluteRcssPath.parent_path(), Ec);
	if (Ec)
	{
		if (OutError)
		{
			*OutError = "Failed to create RCSS export directory.";
		}
		return false;
	}

	std::ofstream RmlFile(AbsoluteRmlPath, std::ios::binary | std::ios::trunc);
	std::ofstream RcssFile(AbsoluteRcssPath, std::ios::binary | std::ios::trunc);
	if (!RmlFile || !RcssFile)
	{
		if (OutError)
		{
			*OutError = "Failed to open generated RML/RCSS files.";
		}
		return false;
	}

	const std::string RmlText = Rml.str();
	const std::string RcssText = Rcss.str();
	RmlFile.write(RmlText.data(), static_cast<std::streamsize>(RmlText.size()));
	RcssFile.write(RcssText.data(), static_cast<std::streamsize>(RcssText.size()));
	return RmlFile.good() && RcssFile.good();
}

FString URuntimeUILayoutAsset::MakeUniqueWidgetId(ERuntimeUIWidgetType Type) const
{
	const FString Base = GetWidgetTypeName(Type);
	for (int32 Index = 1; Index < 10000; ++Index)
	{
		const FString Candidate = Base + std::to_string(Index);
		const bool bExists = std::any_of(
			Widgets.begin(),
			Widgets.end(),
			[&Candidate](const FRuntimeUIWidgetNode& Node)
			{
				return Node.Id == Candidate;
			});
		if (!bExists)
		{
			return Candidate;
		}
	}
	return Base;
}

bool URuntimeUILayoutAsset::IsValidWidgetIndex(int32 WidgetIndex) const
{
	return WidgetIndex >= 0 && WidgetIndex < static_cast<int32>(Widgets.size());
}

bool URuntimeUILayoutAsset::WouldCreateParentCycle(int32 WidgetIndex, int32 NewParentIndex) const
{
	if (WidgetIndex == NewParentIndex)
	{
		return true;
	}

	int32 CurrentIndex = NewParentIndex;
	while (IsValidWidgetIndex(CurrentIndex))
	{
		if (CurrentIndex == WidgetIndex)
		{
			return true;
		}
		CurrentIndex = Widgets[CurrentIndex].ParentIndex;
	}
	return false;
}

void URuntimeUILayoutAsset::SwapWidgetIndices(int32 FirstIndex, int32 SecondIndex)
{
	if (!IsValidWidgetIndex(FirstIndex) || !IsValidWidgetIndex(SecondIndex) || FirstIndex == SecondIndex)
	{
		return;
	}

	std::swap(Widgets[FirstIndex], Widgets[SecondIndex]);
	for (FRuntimeUIWidgetNode& Node : Widgets)
	{
		if (Node.ParentIndex == FirstIndex)
		{
			Node.ParentIndex = SecondIndex;
		}
		else if (Node.ParentIndex == SecondIndex)
		{
			Node.ParentIndex = FirstIndex;
		}

		for (int32& ChildIndex : Node.Children)
		{
			if (ChildIndex == FirstIndex)
			{
				ChildIndex = SecondIndex;
			}
			else if (ChildIndex == SecondIndex)
			{
				ChildIndex = FirstIndex;
			}
		}
	}
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
		else if (Index == 0)
		{
			Widgets[Index].ParentIndex = -1;
		}
	}
}

FArchive& operator<<(FArchive& Ar, FRuntimeUIWidgetNode& Node)
{
	Ar << "Id" << Node.Id;
	Ar << "DisplayName" << Node.DisplayName;

	int32 Type = static_cast<int32>(Node.Type);
	Ar << "Type" << Type;
	if (Ar.IsLoading())
	{
		Node.Type = static_cast<ERuntimeUIWidgetType>(Type);
	}

	Ar << "ParentIndex" << Node.ParentIndex;
	Ar << "Children" << Node.Children;
	Ar << "AnchorMin" << Node.AnchorMin;
	Ar << "AnchorMax" << Node.AnchorMax;
	Ar << "Pivot" << Node.Pivot;
	Ar << "Position" << Node.Position;
	Ar << "Size" << Node.Size;
	Ar << "Rotation" << Node.Rotation;
	Ar << "Scale" << Node.Scale;
	Ar << "Text" << Node.Text;
	Ar << "ImagePath" << Node.ImagePath;
	Ar << "StyleClass" << Node.StyleClass;
	if (Ar.IsLoading() && GRuntimeUILayoutSerializePayloadVersion < 4)
	{
		Node.bVisible = true;
		Node.bLocked = false;
	}
	else
	{
		Ar << "Visible" << Node.bVisible;
		Ar << "Locked" << Node.bLocked;
	}
	if (Ar.IsLoading() && GRuntimeUILayoutSerializePayloadVersion < 2)
	{
		ApplyDefaultStyle(Node);
	}
	else
	{
		Ar << "BackgroundColor" << Node.BackgroundColor;
		Ar << "TextColor" << Node.TextColor;
		Ar << "FontSize" << Node.FontSize;
		Ar << "TextAlign" << Node.TextAlign;
	}
	if (Ar.IsLoading() && GRuntimeUILayoutSerializePayloadVersion < 5)
	{
		Node.FontFamily = GetDefaultRuntimeUIFontFamily();
		Node.ImageTint = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else
	{
		Ar << "FontFamily" << Node.FontFamily;
		Ar << "ImageTint" << Node.ImageTint;
	}
	if (Ar.IsLoading() && GRuntimeUILayoutSerializePayloadVersion < 9)
	{
		Node.FontWeight = Node.Type == ERuntimeUIWidgetType::Button ? 600 : 400;
		Node.LineHeight = 0.0f;
		Node.LetterSpacing = 0.0f;
		Node.bTextWrap = false;
	}
	else
	{
		Ar << "FontWeight" << Node.FontWeight;
		Ar << "LineHeight" << Node.LineHeight;
		Ar << "LetterSpacing" << Node.LetterSpacing;
		Ar << "TextWrap" << Node.bTextWrap;
	}
	if (Ar.IsLoading() && GRuntimeUILayoutSerializePayloadVersion < 6)
	{
		Node.ImageFit = ERuntimeUIImageFit::Stretch;
	}
	else
	{
		int32 ImageFit = static_cast<int32>(Node.ImageFit);
		Ar << "ImageFit" << ImageFit;
		if (Ar.IsLoading())
		{
			Node.ImageFit = static_cast<ERuntimeUIImageFit>(ImageFit);
		}
	}
	if (Ar.IsLoading() && GRuntimeUILayoutSerializePayloadVersion < 3)
	{
		Node.Opacity = 1.0f;
		Node.BorderColor = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
		Node.BorderWidth = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		Node.BorderRadius = 0.0f;
	}
	else
	{
		Ar << "Opacity" << Node.Opacity;
		Ar << "BorderColor" << Node.BorderColor;
		Ar << "BorderWidth" << Node.BorderWidth;
		Ar << "BorderRadius" << Node.BorderRadius;
	}
	if (Ar.IsLoading() && GRuntimeUILayoutSerializePayloadVersion < 8)
	{
		Node.LayoutMode = ERuntimeUILayoutMode::Free;
		Node.LayoutAlignment = ERuntimeUILayoutAlignment::Start;
		Node.LayoutSizeRule = ERuntimeUILayoutSizeRule::Auto;
		Node.LayoutPadding = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		Node.LayoutGap = 8.0f;
		Node.LayoutFillWeight = 1.0f;
	}
	else
	{
		int32 LayoutMode = static_cast<int32>(Node.LayoutMode);
		int32 LayoutAlignment = static_cast<int32>(Node.LayoutAlignment);
		Ar << "LayoutMode" << LayoutMode;
		Ar << "LayoutAlignment" << LayoutAlignment;
		Ar << "LayoutPadding" << Node.LayoutPadding;
		Ar << "LayoutGap" << Node.LayoutGap;
		if (Ar.IsLoading())
		{
			Node.LayoutMode = static_cast<ERuntimeUILayoutMode>(LayoutMode);
			Node.LayoutAlignment = static_cast<ERuntimeUILayoutAlignment>(LayoutAlignment);
		}
	}
	if (Ar.IsLoading() && GRuntimeUILayoutSerializePayloadVersion < 10)
	{
		Node.LayoutSizeRule = ERuntimeUILayoutSizeRule::Auto;
		Node.LayoutFillWeight = 1.0f;
	}
	else
	{
		int32 LayoutSizeRule = static_cast<int32>(Node.LayoutSizeRule);
		Ar << "LayoutSizeRule" << LayoutSizeRule;
		Ar << "LayoutFillWeight" << Node.LayoutFillWeight;
		if (Ar.IsLoading())
		{
			Node.LayoutSizeRule = static_cast<ERuntimeUILayoutSizeRule>(LayoutSizeRule);
		}
	}
	if (Ar.IsLoading() && GRuntimeUILayoutSerializePayloadVersion < 7)
	{
		DeriveButtonStateStyles(Node);
	}
	else
	{
		Ar << "UseButtonStateStyle" << Node.bUseButtonStateStyle;
		Ar << "ButtonHoverStyle" << Node.ButtonHoverStyle;
		Ar << "ButtonPressedStyle" << Node.ButtonPressedStyle;
		Ar << "ButtonDisabledStyle" << Node.ButtonDisabledStyle;
	}
	Ar << "UseNineSlice" << Node.bUseNineSlice;
	Ar << "NineSliceBorder" << Node.NineSliceBorder;
	Ar << "OnClickAction" << Node.OnClickAction;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FRuntimeUIButtonStateStyle& Style)
{
	Ar << "BackgroundColor" << Style.BackgroundColor;
	Ar << "TextColor" << Style.TextColor;
	Ar << "BorderColor" << Style.BorderColor;
	return Ar;
}
