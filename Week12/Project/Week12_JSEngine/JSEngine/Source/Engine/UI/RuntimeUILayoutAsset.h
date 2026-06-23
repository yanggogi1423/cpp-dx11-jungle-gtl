#pragma once

#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Math/Vector4.h"
#include "Object/Object.h"

struct FArchive;

enum class ERuntimeUIWidgetType : uint8
{
	Canvas = 0,
	Panel,
	Text,
	Image,
	Button,
};

enum class ERuntimeUIImageFit : uint8
{
	Stretch = 0,
	Contain,
	Cover,
};

enum class ERuntimeUILayoutMode : uint8
{
	Free = 0,
	Horizontal,
	Vertical,
};

enum class ERuntimeUILayoutAlignment : uint8
{
	Start = 0,
	Center,
	End,
	Stretch,
};

enum class ERuntimeUILayoutSizeRule : uint8
{
	Auto = 0,
	Fill,
};

struct FRuntimeUIButtonStateStyle
{
	FVector4 BackgroundColor = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4 TextColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	FVector4 BorderColor = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
};

struct FRuntimeUIWidgetNode
{
	FString Id;
	FString DisplayName;
	ERuntimeUIWidgetType Type = ERuntimeUIWidgetType::Panel;

	int32 ParentIndex = -1;
	TArray<int32> Children;

	FVector2 AnchorMin = FVector2(0.0f, 0.0f);
	FVector2 AnchorMax = FVector2(0.0f, 0.0f);
	FVector2 Pivot = FVector2(0.5f, 0.5f);
	FVector2 Position = FVector2(0.0f, 0.0f);
	FVector2 Size = FVector2(160.0f, 48.0f);
	float Rotation = 0.0f;
	FVector2 Scale = FVector2(1.0f, 1.0f);

	FString Text;
	FString ImagePath;
	FString StyleClass;
	bool bVisible = true;
	bool bLocked = false;

	FVector4 BackgroundColor = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4 TextColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	float FontSize = 24.0f;
	FString FontFamily = "Malgun Gothic";
	int32 FontWeight = 400;
	float LineHeight = 0.0f;
	float LetterSpacing = 0.0f;
	bool bTextWrap = false;
	FString TextAlign = "center";
	FVector4 ImageTint = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	ERuntimeUIImageFit ImageFit = ERuntimeUIImageFit::Stretch;
	float Opacity = 1.0f;
	FVector4 BorderColor = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
	FVector4 BorderWidth = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	float BorderRadius = 0.0f;

	ERuntimeUILayoutMode LayoutMode = ERuntimeUILayoutMode::Free;
	ERuntimeUILayoutAlignment LayoutAlignment = ERuntimeUILayoutAlignment::Start;
	ERuntimeUILayoutSizeRule LayoutSizeRule = ERuntimeUILayoutSizeRule::Auto;
	FVector4 LayoutPadding = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	float LayoutGap = 8.0f;
	float LayoutFillWeight = 1.0f;

	bool bUseButtonStateStyle = true;
	FRuntimeUIButtonStateStyle ButtonHoverStyle;
	FRuntimeUIButtonStateStyle ButtonPressedStyle;
	FRuntimeUIButtonStateStyle ButtonDisabledStyle;

	bool bUseNineSlice = false;
	FVector4 NineSliceBorder = FVector4(0.0f, 0.0f, 0.0f, 0.0f);

	FString OnClickAction;
};

class URuntimeUILayoutAsset : public UObject
{
public:
	static constexpr int32 CurrentPayloadVersion = 10;

	URuntimeUILayoutAsset();

	void Serialize(FArchive& Ar) override;

	void ResetToDefault();
	int32 AddWidget(ERuntimeUIWidgetType Type, int32 ParentIndex);
	int32 DuplicateWidget(int32 WidgetIndex);
	bool RemoveWidget(int32 WidgetIndex);
	bool SetWidgetParent(int32 WidgetIndex, int32 NewParentIndex);
	int32 MoveWidgetWithinParent(int32 WidgetIndex, int32 Direction);
	int32 MoveWidgetRelativeToSibling(int32 WidgetIndex, int32 TargetSiblingIndex, bool bAfter);

	FRuntimeUIWidgetNode* GetMutableWidget(int32 WidgetIndex);
	const FRuntimeUIWidgetNode* GetWidget(int32 WidgetIndex) const;
	TArray<FRuntimeUIWidgetNode>& GetMutableWidgets() { return Widgets; }
	const TArray<FRuntimeUIWidgetNode>& GetWidgets() const { return Widgets; }

	void SetAssetPath(const FString& InPath) { AssetPath = InPath; }
	const FString& GetAssetPath() const { return AssetPath; }
	void SetGeneratedPaths(const FString& InRmlPath, const FString& InRcssPath)
	{
		GeneratedRmlPath = InRmlPath;
		GeneratedRcssPath = InRcssPath;
	}

	bool SaveToFile(const FString& Path);
	bool LoadFromFile(const FString& Path);
	bool ValidateForExport(FString* OutError = nullptr) const;
	bool ExportRmlAndRcss(const FString& RmlPath, const FString& RcssPath, FString* OutError = nullptr) const;

private:
	FString MakeUniqueWidgetId(ERuntimeUIWidgetType Type) const;
	bool IsValidWidgetIndex(int32 WidgetIndex) const;
	bool WouldCreateParentCycle(int32 WidgetIndex, int32 NewParentIndex) const;
	void SwapWidgetIndices(int32 FirstIndex, int32 SecondIndex);
	void RebuildChildrenFromParents();

private:
	FString AssetPath;
	FString GeneratedRmlPath;
	FString GeneratedRcssPath;
	FVector2 CanvasSize = FVector2(1920.0f, 1080.0f);
	TArray<FRuntimeUIWidgetNode> Widgets;
};

FArchive& operator<<(FArchive& Ar, FRuntimeUIWidgetNode& Node);
FArchive& operator<<(FArchive& Ar, FRuntimeUIButtonStateStyle& Style);
