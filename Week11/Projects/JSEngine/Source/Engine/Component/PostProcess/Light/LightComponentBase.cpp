#include "LightComponentBase.h"
#include "Object/ObjectFactory.h"

void ULightComponentBase::PostDuplicate(UObject* Original)
{
    USceneComponent::PostDuplicate(Original);
    const ULightComponentBase* Orig = Cast<ULightComponentBase>(Original);

    LightColor = Orig->LightColor;
}

void ULightComponentBase::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    USceneComponent::GetEditableProperties(OutProps);
}

void ULightComponentBase::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);
	Ar << "Color" << LightColor;
	Ar << "Intensity" << Intensity;
	Ar << "CastShadows" << bCastShadows;
}
