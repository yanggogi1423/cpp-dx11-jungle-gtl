#include "Component/Camera/CineCameraComponent.h"

#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"

const char* UCineCameraComponent::GetEditorVisualizationMaterialPath() const
{
	return "Content/Material/Editor/EditorCineCamera_Black.uasset";
}
