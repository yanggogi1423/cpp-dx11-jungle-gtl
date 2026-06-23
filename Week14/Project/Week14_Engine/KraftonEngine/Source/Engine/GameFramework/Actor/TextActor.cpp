#include "TextActor.h"

#include "Component/Primitive/TextRenderComponent.h"

ATextActor::ATextActor()
{
	bNeedsTick = false;
	bTickInEditor = false;
}

void ATextActor::InitDefaultComponents()
{
	if (TextRenderComponent)
	{
		return;
	}

	TextRenderComponent = AddComponent<UTextRenderComponent>();
	if (!TextRenderComponent)
	{
		return;
	}

	TextRenderComponent->SetText("Text");
	TextRenderComponent->SetFont(FName("Default"));
	TextRenderComponent->SetFontSize(1.0f);
	TextRenderComponent->SetColor(FVector4(1.0f, 1.0f, 1.0f, 1.0f));
	TextRenderComponent->SetOpacity(1.0f);
	TextRenderComponent->SetDepthTestEnabled(false);
	SetRootComponent(TextRenderComponent);
}
