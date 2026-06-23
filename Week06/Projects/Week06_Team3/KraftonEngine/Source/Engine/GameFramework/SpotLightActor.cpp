#include "GameFramework/SpotLightActor.h"
#include "Components/BillboardComponent.h"
#include "Components/DecalComponent.h"

ASpotLightActor::ASpotLightActor() : ADecalActor()
{
	SetActorRotation(FRotator(90.0f, 0.0f, 0.0f));


	Decal->SetDecalTexture(FName("light"));
	SpriteComponent->SetTexture(FName("SpotLightIcon"));
}
