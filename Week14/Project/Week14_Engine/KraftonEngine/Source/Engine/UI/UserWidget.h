#pragma once

#include "Object/Object.h"
#include "Core/Logging/Log.h"
#include "Object/GarbageCollection.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Source/Engine/UI/UserWidget.generated.h"
#include <sol/sol.hpp>
#include <utility>

#ifdef GetNextSibling
#undef GetNextSibling
#endif
#ifdef GetFirstChild
#undef GetFirstChild
#endif
#include <RmlUi/Core.h>

class APlayerController;
class UUserWidget;
class FWidgetClickEventListener;
class FWidgetActionEventListener;
namespace Rml { class ElementDocument; }

class FWidgetClickEventListener final : public Rml::EventListener
{
public:
	FWidgetClickEventListener(FString InElementId, sol::protected_function InCallback)
		: ElementId(std::move(InElementId))
		, Callback(std::move(InCallback))
	{
	}

	void ProcessEvent(Rml::Event& /*Event*/) override
	{
		if (!Callback.valid())
		{
			return;
		}

		FScopedGarbageCollectionBlocker GCBlocker;
		sol::protected_function_result Result = Callback();
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("[Lua] UI click callback error: %s", Err.what());
		}
	}

	const FString& GetElementId() const { return ElementId; }

private:
	FString ElementId;
	sol::protected_function Callback;
};

class FWidgetActionEventListener final : public Rml::EventListener
{
public:
	explicit FWidgetActionEventListener(UUserWidget* InOwner)
		: Owner(InOwner)
	{
	}

	void ProcessEvent(Rml::Event& Event) override;

private:
	UUserWidget* Owner = nullptr;
};

UCLASS()
class UUserWidget : public UObject
{
public:
	GENERATED_BODY()
	UUserWidget() = default;
	~UUserWidget() override = default;
	void BeginDestroy() override;

	void Initialize(APlayerController* InOwningPlayer, const FString& InDocumentPath);
	UFUNCTION(Callable, Category="UI")
	void AddToViewport(int32 InZOrder = 0);
	UFUNCTION(Callable, Category="UI")
	void RemoveFromParent();
	void BindClick(const FString& ElementId, sol::protected_function Callback);
	void RegisterEventListeners();
	void ClearEventListeners();
	void ReleaseLuaCallbacks();
	UFUNCTION(Callable, Category="UI")
	void SetText(const FString& ElementId, const FString& Text);
	UFUNCTION(Pure, Category="UI")
	FString GetText(const FString& ElementId) const;
	UFUNCTION(Callable, Category="UI")
	bool SetProperty(const FString& ElementId, const FString& PropertyName, const FString& Value);
	UFUNCTION(Pure, Category="UI")
	bool HasElement(const FString& ElementId) const;
	UFUNCTION(Pure, Category="UI")
	FString GetElementValue(const FString& ElementId) const;
	UFUNCTION(Callable, Category="UI")
	bool SetElementValue(const FString& ElementId, const FString& Value);
	UFUNCTION(Callable, Category="UI")
	bool SetElementClass(const FString& ElementId, const FString& ClassName, bool bEnabled);
	UFUNCTION(Pure, Category="UI")
	bool HasElementClass(const FString& ElementId, const FString& ClassName) const;
	UFUNCTION(Pure, Category="UI")
	FString GetElementClassNames(const FString& ElementId) const;
	UFUNCTION(Callable, Category="UI")
	bool SetElementClassNames(const FString& ElementId, const FString& ClassNames);
	UFUNCTION(Pure, Category="UI")
	bool HasElementAttribute(const FString& ElementId, const FString& AttributeName) const;
	UFUNCTION(Pure, Category="UI")
	FString GetElementAttribute(const FString& ElementId, const FString& AttributeName) const;
	UFUNCTION(Callable, Category="UI")
	bool SetElementAttribute(const FString& ElementId, const FString& AttributeName, const FString& Value);
	UFUNCTION(Callable, Category="UI")
	bool RemoveElementAttribute(const FString& ElementId, const FString& AttributeName);
	UFUNCTION(Pure, Category="UI")
	FString GetElementStyle(const FString& ElementId, const FString& StyleName) const;
	UFUNCTION(Callable, Category="UI")
	bool SetElementStyle(const FString& ElementId, const FString& StyleName, const FString& Value);
	UFUNCTION(Callable, Category="UI")
	bool RemoveElementStyle(const FString& ElementId, const FString& StyleName);
	UFUNCTION(Callable, Category="UI")
	bool FocusElement(const FString& ElementId, bool bFocusVisible = false);
	UFUNCTION(Callable, Category="UI")
	bool BlurElement(const FString& ElementId);
	UFUNCTION(Pure, Category="UI")
	bool IsElementFocused(const FString& ElementId) const;
	UFUNCTION(Callable, Category="UI")
	bool ClickElement(const FString& ElementId);
	UFUNCTION(Callable, Category="UI")
	bool SetElementVisible(const FString& ElementId, bool bVisible);
	UFUNCTION(Callable, Category="UI")
	bool SetElementEnabled(const FString& ElementId, bool bEnabled);
	UFUNCTION(Callable, Category="UI")
	bool SetActionEvent(const FString& ElementId, const FString& EventName);
	TArray<FString> PollActionEvents();
	void EnqueueActionEvent(const FString& EventName);

	UFUNCTION(Pure, Category="UI")
	APlayerController* GetOwningPlayer() const { return OwningPlayer; }
	UFUNCTION(Pure, Category="UI")
	const FString& GetDocumentPath() const { return DocumentPath; }
	UFUNCTION(Pure, Category="UI")
	int32 GetZOrder() const { return ZOrder; }
	UFUNCTION(Pure, Category="UI")
	bool IsInViewport() const { return bInViewport; }
	UFUNCTION(Pure, Category="UI")
	bool IsDocumentLoaded() const { return bDocumentLoaded; }
	Rml::ElementDocument* GetDocument() const { return Document; }

	// 메뉴/대화창처럼 사용자가 클릭/포인팅을 해야 하는 widget 은 true 로 설정.
	// UUIManager 가 viewport 에 올라온 widget 중 하나라도 이 값이 true 면 GameViewportClient
	// 에 알려 시스템 커서를 보이고 raw mouse / clip 을 해제하도록 한다. HUD 처럼 비대화형
	// 오버레이는 false 유지.
	UFUNCTION(Callable, Category="UI")
	void SetWantsMouse(bool bInWantsMouse) { bWantsMouse = bInWantsMouse; }
	UFUNCTION(Pure, Category="UI")
	bool WantsMouse() const { return bWantsMouse; }

	UFUNCTION(Callable, Category="UI")
	void SetWantsKeyboard(bool bInWantsKeyboard) { bWantsKeyboard = bInWantsKeyboard; }
	UFUNCTION(Pure, Category="UI")
	bool WantsKeyboard() const { return bWantsKeyboard; }

	UFUNCTION(Callable, Category="UI")
	void SetWantsTextInput(bool bInWantsTextInput) { bWantsTextInput = bInWantsTextInput; }
	UFUNCTION(Pure, Category="UI")
	bool WantsTextInput() const { return bWantsTextInput; }

	UFUNCTION(Callable, Category="UI")
	void SetBlocksGameInput(bool bInBlocksGameInput) { bBlocksGameInput = bInBlocksGameInput; }
	UFUNCTION(Pure, Category="UI")
	bool BlocksGameInput() const { return bBlocksGameInput; }

	UFUNCTION(Callable, Category="UI")
	void SetBlocksGameKeyboard(bool bInBlocksGameKeyboard) { bBlocksGameKeyboard = bInBlocksGameKeyboard; }
	UFUNCTION(Pure, Category="UI")
	bool BlocksGameKeyboard() const { return bBlocksGameKeyboard; }

	UFUNCTION(Callable, Category="UI")
	void SetBlocksGameMouseLook(bool bInBlocksGameMouseLook) { bBlocksGameMouseLook = bInBlocksGameMouseLook; }
	UFUNCTION(Pure, Category="UI")
	bool BlocksGameMouseLook() const { return bBlocksGameMouseLook; }

	void MarkDocumentLoaded(Rml::ElementDocument* InDocument) { Document = InDocument; bDocumentLoaded = Document != nullptr; }
	void MarkRemovedFromViewport() { bInViewport = false; }
	void ClearDocument() { Document = nullptr; bDocumentLoaded = false; PendingActionEvents.clear(); }

private:
	TWeakObjectPtr<APlayerController> OwningPlayer;
	Rml::ElementDocument* Document = nullptr;
	FString DocumentPath;
	TArray<std::pair<FString, sol::protected_function>> PendingClickBindings;
	TArray<FString> PendingActionEvents;
	TArray<FWidgetClickEventListener*> ClickListeners;
	FWidgetActionEventListener* ActionListener = nullptr;
	int32 ZOrder = 0;
	bool bInViewport = false;
	bool bDocumentLoaded = false;
	bool bWantsMouse = false;
	bool bWantsKeyboard = false;
	bool bWantsTextInput = false;
	bool bBlocksGameInput = false;
	bool bBlocksGameKeyboard = false;
	bool bBlocksGameMouseLook = false;
};
