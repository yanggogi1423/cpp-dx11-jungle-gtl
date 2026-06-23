#include "UI/UserWidget.h"

#include "Object/Reflection/ObjectFactory.h"
#include "UI/UIManager.h"

#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementProgress.h>

#include <cstdlib>

namespace
{
	Rml::Element* FindElement(Rml::ElementDocument* Document, const FString& ElementId)
	{
		return Document ? Document->GetElementById(ElementId) : nullptr;
	}

	bool TryParseFloat(const FString& Text, float& OutValue)
	{
		char* End = nullptr;
		const float Value = std::strtof(Text.c_str(), &End);
		if (End == Text.c_str())
		{
			return false;
		}

		OutValue = Value;
		return true;
	}
}

void FWidgetActionEventListener::ProcessEvent(Rml::Event& Event)
{
	if (!Owner)
	{
		return;
	}

	const Rml::String& EventType = Event.GetType();
	const bool bHoverEvent = EventType == "mouseover";
	const char* PrimaryAttribute = bHoverEvent ? "data-hover-action" : "data-action";
	const char* FallbackAttribute = bHoverEvent ? "hover-action" : "action";

	Rml::Element* Element = Event.GetTargetElement();
	while (Element)
	{
		Rml::String Action = Element->GetAttribute<Rml::String>(PrimaryAttribute, "");
		if (Action.empty())
		{
			Action = Element->GetAttribute<Rml::String>(FallbackAttribute, "");
		}

		if (!Action.empty())
		{
			Owner->EnqueueActionEvent(Action);
			return;
		}

		Element = Element->GetParentNode();
	}
}

void UUserWidget::BeginDestroy()
{
	// Rml::ElementDocument and Rml event listeners are external runtime resources,
	// not UObject references. GC can destroy a widget without going through the
	// regular UIManager shutdown path, so detach listeners and release the document
	// handle before the UObject enters PendingKill/Garbage state.
	ReleaseLuaCallbacks();
	if (Document)
	{
		Document->Close();
		ClearDocument();
	}
	bInViewport = false;
	UObject::BeginDestroy();
}


void UUserWidget::Initialize(APlayerController* InOwningPlayer, const FString& InDocumentPath)
{
	OwningPlayer = InOwningPlayer;
	DocumentPath = InDocumentPath;
}

void UUserWidget::AddToViewport(int32 InZOrder)
{
	ZOrder = InZOrder;
	bInViewport = true;
	UUIManager::Get().AddToViewport(this, InZOrder);
}

void UUserWidget::RemoveFromParent()
{
	UUIManager::Get().RemoveFromViewport(this);
	bInViewport = false;
}

void UUserWidget::BindClick(const FString& ElementId, sol::protected_function Callback)
{
	PendingClickBindings.push_back({ ElementId, Callback });
	if (IsDocumentLoaded())
	{
		RegisterEventListeners();
	}
}

void UUserWidget::RegisterEventListeners()
{
	if (!Document)
	{
		return;
	}

	ClearEventListeners();

	for (const auto& Binding : PendingClickBindings)
	{
		Rml::Element* Element = Document->GetElementById(Binding.first);
		if (!Element)
		{
			UE_LOG("[RmlUi] Click target not found: %s", Binding.first.c_str());
			continue;
		}

		auto* Listener = new FWidgetClickEventListener(Binding.first, Binding.second);
		Element->AddEventListener("click", Listener);
		ClickListeners.push_back(Listener);
	}

	ActionListener = new FWidgetActionEventListener(this);
	Document->AddEventListener("click", ActionListener);
	Document->AddEventListener("mouseover", ActionListener);
	Document->AddEventListener("change", ActionListener);
	Document->AddEventListener("submit", ActionListener);
}

void UUserWidget::ClearEventListeners()
{
	if (Document && ActionListener)
	{
		Document->RemoveEventListener("click", ActionListener);
		Document->RemoveEventListener("mouseover", ActionListener);
		Document->RemoveEventListener("change", ActionListener);
		Document->RemoveEventListener("submit", ActionListener);
	}

	if (Document)
	{
		for (FWidgetClickEventListener* Listener : ClickListeners)
		{
			if (!Listener)
			{
				continue;
			}

			Rml::Element* Element = Document->GetElementById(Listener->GetElementId());
			if (Element)
			{
				Element->RemoveEventListener("click", Listener);
			}
		}
	}

	for (FWidgetClickEventListener* Listener : ClickListeners)
	{
		delete Listener;
	}
	ClickListeners.clear();

	delete ActionListener;
	ActionListener = nullptr;
}

void UUserWidget::ReleaseLuaCallbacks()
{
	ClearEventListeners();
	PendingClickBindings.clear();
}

void UUserWidget::SetText(const FString& ElementId, const FString& Text)
{
	if (!Document)
	{
		return;
	}

	Rml::Element* Element = Document->GetElementById(ElementId);
	if (!Element)
	{
		UE_LOG("[RmlUi] Text target not found: %s", ElementId.c_str());
		return;
	}

	Element->SetInnerRML(Text.c_str());
}

FString UUserWidget::GetText(const FString& ElementId) const
{
	if (!Document)
	{
		return {};
	}

	Rml::Element* Element = Document->GetElementById(ElementId);
	return Element ? Element->GetInnerRML() : FString();
}

bool UUserWidget::SetProperty(const FString& ElementId, const FString& PropertyName, const FString& Value)
{
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(ElementId);
	if (!Element)
	{
		UE_LOG("[RmlUi] Property target not found: %s", ElementId.c_str());
		return false;
	}

	return Element->SetProperty(PropertyName.c_str(), Value.c_str());
}

bool UUserWidget::HasElement(const FString& ElementId) const
{
	return Document && Document->GetElementById(ElementId) != nullptr;
}

FString UUserWidget::GetElementValue(const FString& ElementId) const
{
	const Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return {};
	}

	if (const auto* Control = rmlui_dynamic_cast<const Rml::ElementFormControl*>(Element))
	{
		return Control->GetValue();
	}

	if (const auto* Progress = rmlui_dynamic_cast<const Rml::ElementProgress*>(Element))
	{
		return std::to_string(Progress->GetValue());
	}

	return Element->GetAttribute<Rml::String>("value", "");
}

bool UUserWidget::SetElementValue(const FString& ElementId, const FString& Value)
{
	Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return false;
	}

	if (auto* Control = rmlui_dynamic_cast<Rml::ElementFormControl*>(Element))
	{
		Control->SetValue(Value);
		return true;
	}

	if (auto* Progress = rmlui_dynamic_cast<Rml::ElementProgress*>(Element))
	{
		float ParsedValue = 0.0f;
		if (!TryParseFloat(Value, ParsedValue))
		{
			return false;
		}

		Progress->SetValue(ParsedValue);
		return true;
	}

	Element->SetAttribute("value", Value);
	return true;
}

bool UUserWidget::SetElementClass(const FString& ElementId, const FString& ClassName, bool bEnabled)
{
	Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return false;
	}

	Element->SetClass(ClassName, bEnabled);
	return true;
}

bool UUserWidget::HasElementClass(const FString& ElementId, const FString& ClassName) const
{
	const Rml::Element* Element = FindElement(Document, ElementId);
	return Element && Element->IsClassSet(ClassName);
}

FString UUserWidget::GetElementClassNames(const FString& ElementId) const
{
	const Rml::Element* Element = FindElement(Document, ElementId);
	return Element ? Element->GetClassNames() : FString();
}

bool UUserWidget::SetElementClassNames(const FString& ElementId, const FString& ClassNames)
{
	Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return false;
	}

	Element->SetClassNames(ClassNames);
	return true;
}

bool UUserWidget::HasElementAttribute(const FString& ElementId, const FString& AttributeName) const
{
	const Rml::Element* Element = FindElement(Document, ElementId);
	return Element && Element->HasAttribute(AttributeName);
}

FString UUserWidget::GetElementAttribute(const FString& ElementId, const FString& AttributeName) const
{
	const Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return {};
	}

	const Rml::Variant* Attribute = Element->GetAttribute(AttributeName);
	return Attribute ? Attribute->Get<Rml::String>() : FString();
}

bool UUserWidget::SetElementAttribute(const FString& ElementId, const FString& AttributeName, const FString& Value)
{
	Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return false;
	}

	Element->SetAttribute(AttributeName, Value);
	return true;
}

bool UUserWidget::RemoveElementAttribute(const FString& ElementId, const FString& AttributeName)
{
	Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return false;
	}

	Element->RemoveAttribute(AttributeName);
	return true;
}

FString UUserWidget::GetElementStyle(const FString& ElementId, const FString& StyleName) const
{
	Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return {};
	}

	const Rml::Property* Property = Element->GetProperty(StyleName);
	return Property ? Property->ToString() : FString();
}

bool UUserWidget::SetElementStyle(const FString& ElementId, const FString& StyleName, const FString& Value)
{
	return SetProperty(ElementId, StyleName, Value);
}

bool UUserWidget::RemoveElementStyle(const FString& ElementId, const FString& StyleName)
{
	Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return false;
	}

	Element->RemoveProperty(StyleName);
	return true;
}

bool UUserWidget::FocusElement(const FString& ElementId, bool bFocusVisible)
{
	Rml::Element* Element = FindElement(Document, ElementId);
	return Element && Element->Focus(bFocusVisible);
}

bool UUserWidget::BlurElement(const FString& ElementId)
{
	Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return false;
	}

	Element->Blur();
	return true;
}

bool UUserWidget::IsElementFocused(const FString& ElementId) const
{
	const Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return false;
	}

	const Rml::Element* FocusLeaf = Document ? Document->GetFocusLeafNode() : nullptr;
	return FocusLeaf == Element || Element->IsPseudoClassSet("focus");
}

bool UUserWidget::ClickElement(const FString& ElementId)
{
	Rml::Element* Element = FindElement(Document, ElementId);
	if (!Element)
	{
		return false;
	}

	Element->Click();
	return true;
}

bool UUserWidget::SetElementVisible(const FString& ElementId, bool bVisible)
{
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(ElementId);
	if (!Element)
	{
		return false;
	}

	const Rml::Property* Display = Element->GetProperty("display");
	const bool bCurrentlyHidden = Display && Display->ToString() == "none";
	if (bVisible)
	{
		if (bCurrentlyHidden)
		{
			Element->RemoveProperty("display");
		}
		return true;
	}

	if (!bCurrentlyHidden)
	{
		Element->SetProperty("display", "none");
	}
	return true;
}

bool UUserWidget::SetElementEnabled(const FString& ElementId, bool bEnabled)
{
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(ElementId);
	if (!Element)
	{
		return false;
	}

	if (bEnabled)
	{
		Element->RemoveAttribute("disabled");
		Element->SetClass("disabled", false);
	}
	else
	{
		Element->SetAttribute("disabled", "disabled");
		Element->SetClass("disabled", true);
	}
	return true;
}

bool UUserWidget::SetActionEvent(const FString& ElementId, const FString& EventName)
{
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(ElementId);
	if (!Element)
	{
		return false;
	}

	if (EventName.empty())
	{
		Element->RemoveAttribute("data-action");
	}
	else
	{
		Element->SetAttribute("data-action", EventName);
	}
	return true;
}

TArray<FString> UUserWidget::PollActionEvents()
{
	TArray<FString> Events = PendingActionEvents;
	PendingActionEvents.clear();
	return Events;
}

void UUserWidget::EnqueueActionEvent(const FString& EventName)
{
	if (!EventName.empty())
	{
		PendingActionEvents.push_back(EventName);
	}
}
