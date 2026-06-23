#include "LuaScriptManager.h"

#include "Animation/AnimInstance.h"
#include "Animation/Graph/AnimGraphInstance.h"
#include "Animation/Montage/AnimMontage.h"
#include "Animation/Sequence/AnimSequence.h"
#include "CameraShake/CameraShakeAsset.h"
#include "CameraShake/CameraShakeManager.h"
#include "Component/ActorComponent.h"
#include "Component/ActorSequenceComponent.h"
#include "Component/Camera/CameraComponent.h"
#include "Component/Camera/CineCameraComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Gameplay/BallisticBulletManagerComponent.h"
#include "Component/Gameplay/SniperDamageReceiverComponent.h"
#include "Component/Gameplay/SniperTypes.h"
#include "Component/Gameplay/SniperWeaponComponent.h"
#include "Component/Input/ActionComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Light/LightComponent.h"
#include "Component/Light/LightComponentBase.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/Movement/FloatingPawnMovementComponent.h"
#include "Component/Movement/MovementComponent.h"
#include "Component/Movement/PendulumMovementComponent.h"
#include "Component/Movement/ProjectileMovementComponent.h"
#include "Component/Movement/RotatingMovementComponent.h"
#include "Component/Movement/WheeledVehicleMovementComponent.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Component/Primitive/BillboardComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/Primitive/TextRenderComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Component/Script/LuaBlueprintComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "Component/ShapeComponent.h"
#include "Component/SoundComponent.h"
#include "Component/Vehicle/VehicleWheelPoseComponent.h"
#include "Core/Logging/Log.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Actor/ParticleSystemActor.h"
#include "GameFramework/Actor/SniperKillCamDirector.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Camera/CameraModifier.h"
#include "GameFramework/Camera/CameraShakeBase.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "GameFramework/GameMode/GameplayStatics.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Pawn/Character.h"
#include "GameFramework/Pawn/CombatCharacter.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/Pawn/SniperPawn.h"
#include "GameFramework/Pawn/WheeledVehiclePawn.h"
#include "GameFramework/World.h"
#include "Input/InputKeyCodes.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Static/StaticMesh.h"
#include "Object/Object.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Particle/Modules/ParticleModuleRequired.h"
#include "Particle/Modules/ParticleModuleSpawn.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemManager.h"
#include "Particle/TypeData/ParticleModuleTypeDataBase.h"
#include "Runtime/Engine.h"
#include "Serialization/PrefabManager.h"
#include "Texture/Texture2D.h"

#include <algorithm>
#include <cctype>

namespace
{
    bool LuaReadNumber(const sol::object& Object, double& OutValue)
    {
        if (!Object.valid() || Object == sol::nil || Object.get_type() != sol::type::number)
        {
            return false;
        }
        OutValue = Object.as<double>();
        return true;
    }

    bool LuaReadFloatField(const sol::table& Table, const char* Name, int Index, float& OutValue)
    {
        double Number = 0.0;
        sol::object Named = Table[Name];
        if (LuaReadNumber(Named, Number))
        {
            OutValue = static_cast<float>(Number);
            return true;
        }
        sol::object Indexed = Table[Index];
        if (LuaReadNumber(Indexed, Number))
        {
            OutValue = static_cast<float>(Number);
            return true;
        }
        return false;
    }

    bool LuaObjectToVector4(const sol::object& Object, FVector4& OutVector)
    {
        if (!Object.valid() || Object == sol::nil)
        {
            return false;
        }
        if (Object.is<FVector4>())
        {
            OutVector = Object.as<FVector4>();
            return true;
        }
        if (Object.get_type() != sol::type::table)
        {
            return false;
        }
        sol::table Table = Object.as<sol::table>();
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float W = 0.0f;
        LuaReadFloatField(Table, "X", 1, X);
        LuaReadFloatField(Table, "Y", 2, Y);
        LuaReadFloatField(Table, "Z", 3, Z);
        if (!LuaReadFloatField(Table, "W", 4, W))
        {
            LuaReadFloatField(Table, "A", 4, W);
        }
        OutVector = FVector4(X, Y, Z, W);
        return true;
    }

    sol::table LuaVector4ToTable(sol::this_state State, const FVector4& Value)
    {
        sol::state_view Lua(State);
        sol::table Table = Lua.create_table();
        Table["X"] = Value.X;
        Table["Y"] = Value.Y;
        Table["Z"] = Value.Z;
        Table["W"] = Value.W;
        Table["R"] = Value.R;
        Table["G"] = Value.G;
        Table["B"] = Value.B;
        Table["A"] = Value.A;
        return Table;
    }

    void AddLuaTagObject(const sol::object& Value, TArray<FName>& OutTags)
    {
        if (!Value.valid() || Value == sol::nil || Value.get_type() != sol::type::string)
        {
            return;
        }

        const FName Tag(Value.as<FString>());
        if (!Tag.IsValid() || std::find(OutTags.begin(), OutTags.end(), Tag) != OutTags.end())
        {
            return;
        }

        OutTags.push_back(Tag);
    }

    TArray<FName> LuaTagsFromArgs(sol::variadic_args Args)
    {
        TArray<FName> Tags;
        if (Args.size() == 1)
        {
            const sol::object FirstArg = Args[0];
            if (FirstArg.valid() && FirstArg.get_type() == sol::type::table)
            {
                const sol::table Table = FirstArg.as<sol::table>();
                for (const auto& Entry : Table)
                {
                    AddLuaTagObject(Entry.second, Tags);
                }
                return Tags;
            }
        }

        for (auto Arg : Args)
        {
            const sol::object Value = Arg;
            AddLuaTagObject(Value, Tags);
        }
        return Tags;
    }

    sol::table ComponentsToLuaTable(sol::this_state State, const TArray<UActorComponent*>& Components)
    {
        sol::state_view L(State);
        sol::table Result = L.create_table();
        int Index = 1;
        for (UActorComponent* Component : Components)
        {
            if (IsValid(Component))
            {
                Result[Index++] = Component;
            }
        }
        return Result;
    }
    bool EndsWithPrefabExtension(const FString& Value)
    {
        constexpr const char* Extension = ".prefab";
        constexpr size_t ExtensionLength = 7;
        if (Value.size() < ExtensionLength)
        {
            return false;
        }

        const size_t Offset = Value.size() - ExtensionLength;
        for (size_t Index = 0; Index < ExtensionLength; ++Index)
        {
            const char A = static_cast<char>(std::tolower(static_cast<unsigned char>(Value[Offset + Index])));
            if (A != Extension[Index])
            {
                return false;
            }
        }
        return true;
    }

    FString NormalizePrefabSlash(FString Value)
    {
        for (char& Ch : Value)
        {
            if (Ch == '\\')
            {
                Ch = '/';
            }
        }
        return Value;
    }

    FString BuildPrefabPathFromDirectoryAndName(FString Directory, FString PrefabName)
    {
        Directory = NormalizePrefabSlash(Directory);
        PrefabName = NormalizePrefabSlash(PrefabName);

        if (PrefabName.empty())
        {
            return FString();
        }
        if (!EndsWithPrefabExtension(PrefabName))
        {
            PrefabName += ".prefab";
        }

        while (!Directory.empty() && Directory.back() == '/')
        {
            Directory.pop_back();
        }

        if (Directory.empty())
        {
            return PrefabName;
        }
        return Directory + "/" + PrefabName;
    }

    void ApplyOptionalActorTransform(
        AActor* Actor,
        const sol::optional<FVector>& Location,
        const sol::optional<FVector>& Rotation,
        const sol::optional<FVector>& Scale)
    {
        if (!IsValid(Actor))
        {
            return;
        }
        if (Location)
        {
            Actor->SetActorLocation(Location.value());
        }
        if (Rotation)
        {
            Actor->SetActorRotation(Rotation.value());
        }
        if (Scale)
        {
            Actor->SetActorScale(Scale.value());
        }
    }
}
void FLuaScriptManager::RegisterActorBindings(sol::state& Lua)
{
    Lua.new_usertype<UTexture2D>(
        "Texture2D",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetSourcePath",
        &UTexture2D::GetSourcePath,
        "GetWidth",
        &UTexture2D::GetWidth,
        "GetHeight",
        &UTexture2D::GetHeight,
        "IsLoaded",
        &UTexture2D::IsLoaded
    );

    Lua.new_usertype<UCameraShakeBase>(
        "CameraShakeBase",
        sol::base_classes,
        sol::bases<UObject>(),
        "StopShake",
        [](UCameraShakeBase& S, sol::optional<bool> bImmediately)
        {
            S.StopShake(bImmediately.value_or(true));
        },
        "IsFinished",
        &UCameraShakeBase::IsFinished,
        "GetPlaySpace",
        [](UCameraShakeBase& S)
        {
            return static_cast<int32>(S.GetPlaySpace());
        }
    );

    Lua.new_usertype<UCameraModifier>(
        "CameraModifier",
        sol::base_classes,
        sol::bases<UObject>(),
        "Enable",
        &UCameraModifier::EnableModifier,
        "Disable",
        [](UCameraModifier& M, sol::optional<bool> bImmediate)
        {
            M.DisableModifier(bImmediate.value_or(false));
        },
        "IsDisabled",
        &UCameraModifier::IsDisabled
    );

    Lua.new_usertype<UCameraShakeAsset>(
        "CameraShakeAsset",
        sol::base_classes,
        sol::bases<UObject>(),
        "LoadFromFile",
        &UCameraShakeAsset::LoadFromFile,
        "SaveToFile",
        &UCameraShakeAsset::SaveToFile,
        "SetSourcePath",
        &UCameraShakeAsset::SetSourcePath,
        "GetSourcePath",
        &UCameraShakeAsset::GetSourcePath
    );

    Lua.new_usertype<APlayerCameraManager>(
        "PlayerCameraManager",
        sol::base_classes,
        sol::bases<AActor, UObject>(),
        "RegisterCamera",
        &APlayerCameraManager::RegisterCamera,
        "UnregisterCamera",
        &APlayerCameraManager::UnregisterCamera,
        "AutoPossessDefaultCamera",
        &APlayerCameraManager::AutoPossessDefaultCamera,
        "ToggleActiveCameraForActor",
        sol::overload(
            [](APlayerCameraManager& M, const FString& ActorName, sol::optional<float> BlendTime)
            {
                return M.ToggleActiveCameraForActor(ActorName, BlendTime.value_or(0.0f));
            },
            [](APlayerCameraManager& M, const AActor* Actor, sol::optional<float> BlendTime)
            {
                return M.ToggleActiveCameraForActor(Actor, BlendTime.value_or(0.0f));
            }
        ),
        "GetActiveCamera",
        &APlayerCameraManager::GetActiveCamera,
        "SetActiveCamera",
        &APlayerCameraManager::SetActiveCamera,
        "SetActiveCameraWithBlend",
        [](APlayerCameraManager& M, UCameraComponent* NewCamera, sol::optional<float> BlendTime)
        {
            if (IsValid(NewCamera)) M.SetActiveCameraWithBlend(NewCamera, BlendTime.value_or(0.0f));
        },
        "GetPossessedCamera",
        &APlayerCameraManager::GetPossessedCamera,
        "Possess",
        &APlayerCameraManager::Possess,
        "SetViewTarget",
        [](APlayerCameraManager& M, AActor* Target)
        {
            if (IsValid(Target)) M.SetViewTarget(Target);
        },
        "GetViewTarget",
        &APlayerCameraManager::GetViewTarget,
        "GetPendingViewTarget",
        &APlayerCameraManager::GetPendingViewTarget,
        "StartCameraShakeAssetByPath",
        [](APlayerCameraManager& M, const FString& Path, sol::optional<float> Scale)
        {
            return M.StartCameraShakeAsset(Path, Scale.value_or(1.0f));
        },
        "StartCameraShakeAsset",
        [](APlayerCameraManager& M, UCameraShakeAsset* Asset, sol::optional<float> Scale)
        {
            return IsValid(Asset) ? M.StartCameraShakeAsset(Asset, Scale.value_or(1.0f)) : nullptr;
        },
        "StopAllCameraShakes",
        [](APlayerCameraManager& M, sol::optional<bool> bImmediately)
        {
            M.StopAllCameraShakes(bImmediately.value_or(true));
        },
        "StartCameraFade",
        [](APlayerCameraManager& M, float FromAlpha, float ToAlpha, float Duration, sol::optional<bool> bHold)
        {
            M.StartCameraFade(FromAlpha, ToAlpha, Duration, FLinearColor::Black(), false, bHold.value_or(false));
        },
        "StopCameraFade",
        &APlayerCameraManager::StopCameraFade,
        "SetCameraVignette",
        [](APlayerCameraManager& M, float Intensity, float Radius, float Softness)
        {
            M.SetCameraVignette(Intensity, Radius, Softness, FLinearColor::Black());
        },
        "ClearCameraVignette",
        &APlayerCameraManager::ClearCameraVignette,
        "AddWorldShockWave",
        [](APlayerCameraManager& M, FVector WorldPosition, sol::optional<float> Duration, sol::optional<float> Radius, sol::optional<float> Strength)
        {
            return M.AddWorldShockWave(
                WorldPosition,
                FVector::ForwardVector,
                Duration.value_or(0.35f),
                Radius.value_or(0.12f),
                0.035f,
                Strength.value_or(0.02f),
                1.5f,
                0.0f);
        },
        "AddDirectedWorldShockWave",
        [](APlayerCameraManager& M, FVector WorldPosition, FVector WorldDirection, sol::optional<float> Duration, sol::optional<float> Radius, sol::optional<float> Width, sol::optional<float> Strength, sol::optional<float> Falloff, sol::optional<float> DirectionalStretch)
        {
            if (WorldDirection.IsNearlyZero())
            {
                WorldDirection = FVector::ForwardVector;
            }
            else
            {
                WorldDirection.Normalize();
            }
            return M.AddWorldShockWave(
                WorldPosition,
                WorldDirection,
                Duration.value_or(0.35f),
                Radius.value_or(0.12f),
                Width.value_or(0.035f),
                Strength.value_or(0.02f),
                Falloff.value_or(1.5f),
                DirectionalStretch.value_or(0.0f));
        },
        "UpdateWorldShockWave",
        [](APlayerCameraManager& M, int32 Handle, FVector WorldPosition, FVector WorldDirection, float Radius, float Width, float Strength, sol::optional<float> Falloff, sol::optional<float> DirectionalStretch)
        {
            if (WorldDirection.IsNearlyZero())
            {
                WorldDirection = FVector::ForwardVector;
            }
            else
            {
                WorldDirection.Normalize();
            }
            return M.UpdateWorldShockWave(
                Handle,
                WorldPosition,
                WorldDirection,
                Radius,
                Width,
                Strength,
                Falloff.value_or(1.5f),
                DirectionalStretch.value_or(0.0f));
        },
        "ClearWorldShockWave",
        &APlayerCameraManager::ClearWorldShockWave,
        "ClearAllWorldShockWaves",
        &APlayerCameraManager::ClearAllWorldShockWaves,
        "IsFadeEnabled",
        &APlayerCameraManager::IsFadeEnabled,
        "GetFadeAmount",
        &APlayerCameraManager::GetFadeAmount,
        "IsVignetteEnabled",
        &APlayerCameraManager::IsVignetteEnabled,
        "GetVignetteIntensity",
        &APlayerCameraManager::GetVignetteIntensity,
        "GetVignetteRadius",
        &APlayerCameraManager::GetVignetteRadius,
        "GetVignetteSoftness",
        &APlayerCameraManager::GetVignetteSoftness,
        "SetDepthOfField",
        &APlayerCameraManager::SetDepthOfField,
        "SetBokeh",
        &APlayerCameraManager::SetBokeh,
        "ClearDepthOfField",
        &APlayerCameraManager::ClearDepthOfField,
        "IsDepthOfFieldEnabled",
        &APlayerCameraManager::IsDepthOfFieldEnabled,
        "GetDoFFocusDistance",
        &APlayerCameraManager::GetDoFFocusDistance,
        "GetDoFFocusRange",
        &APlayerCameraManager::GetDoFFocusRange,
        "GetDoFMaxBlurRadius",
        &APlayerCameraManager::GetDoFMaxBlurRadius,
        "GetDoFBokehRadiusThreshold",
        &APlayerCameraManager::GetDoFBokehRadiusThreshold,
        "GetDoFBokehLumaThreshold",
        &APlayerCameraManager::GetDoFBokehLumaThreshold,
        "GetDoFBokehIntensity",
        &APlayerCameraManager::GetDoFBokehIntensity,
        "SetScopeLens",
        [](APlayerCameraManager& M, float Radius, float OuterBlurRadius, float ZoomFOV, sol::optional<float> Feather, sol::optional<float> EdgeBlurRadius, sol::optional<float> Intensity, sol::optional<float> LookSensitivityScale, sol::optional<float> BlendTime, sol::optional<float> CenterX, sol::optional<float> CenterY, sol::optional<float> CenterOffsetX, sol::optional<float> CenterOffsetY)
        {
            M.SetScopeLens(
                Radius,
                OuterBlurRadius,
                ZoomFOV,
                Feather.value_or(0.08f),
                EdgeBlurRadius.value_or(1.25f),
                Intensity.value_or(1.0f),
                LookSensitivityScale.value_or(0.275f),
                BlendTime.value_or(0.08f),
                CenterX.value_or(0.5f),
                CenterY.value_or(0.5f),
                CenterOffsetX.value_or(0.0f),
                CenterOffsetY.value_or(0.0f));
        },
        "SetScopeLensProfile",
        [](APlayerCameraManager& M, float Radius, float OuterBlurRadius, float ZoomFOV, sol::optional<float> Feather, sol::optional<float> EdgeBlurRadius, sol::optional<float> Intensity, sol::optional<float> LookSensitivityScale, sol::optional<float> BlendTime, sol::optional<float> CenterX, sol::optional<float> CenterY, sol::optional<float> CenterOffsetX, sol::optional<float> CenterOffsetY)
        {
            M.SetScopeLensProfile(
                Radius,
                OuterBlurRadius,
                ZoomFOV,
                Feather.value_or(0.08f),
                EdgeBlurRadius.value_or(1.25f),
                Intensity.value_or(1.0f),
                LookSensitivityScale.value_or(0.275f),
                BlendTime.value_or(0.08f),
                CenterX.value_or(0.5f),
                CenterY.value_or(0.5f),
                CenterOffsetX.value_or(0.0f),
                CenterOffsetY.value_or(0.0f));
        },
        "SetScopeZoomEnabled",
        &APlayerCameraManager::SetScopeZoomEnabled,
        "ClearScopeLens",
        &APlayerCameraManager::ClearScopeLens,
        "IsScopeLensEnabled",
        &APlayerCameraManager::IsScopeLensEnabled,
        "IsScopeZoomEnabled",
        &APlayerCameraManager::IsScopeZoomEnabled,
        "GetScopeLookSensitivityScale",
        &APlayerCameraManager::GetScopeLookSensitivityScale
    );

    // Broad engine/gameplay bindings. The generic Reflection/CallFunction path can call
    // UFUNCTIONs, but concrete usertypes make LuaBlueprint scripting discoverable and
    // usable without hand-writing reflection strings for every common gameplay task.
    Lua.new_usertype<UMovementComponent>(
        "MovementComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "SetUpdatedComponent",
        &UMovementComponent::SetUpdatedComponent,
        "GetUpdatedComponent",
        &UMovementComponent::GetUpdatedComponent,
        "HasValidUpdatedComponent",
        &UMovementComponent::HasValidUpdatedComponent,
        "GetUpdatedComponentDisplayName",
        &UMovementComponent::GetUpdatedComponentDisplayName,
        "ResolveUpdatedComponent",
        &UMovementComponent::ResolveUpdatedComponent
    );

    Lua.new_usertype<UCharacterMovementComponent>(
        "CharacterMovementComponent",
        sol::base_classes,
        sol::bases<UMovementComponent, UActorComponent, UObject>(),
        "AddInputVector",
        sol::overload(
            [](UCharacterMovementComponent& C, const FVector& Direction, float Scale)
            {
                C.AddInputVector(Direction, Scale);
            },
            [](UCharacterMovementComponent& C, const FVector& Direction)
            {
                C.AddInputVector(Direction, 1.0f);
            }
        ),
        "GetVelocity",
        &UCharacterMovementComponent::GetVelocityValue,
        "GetVelocityValue",
        &UCharacterMovementComponent::GetVelocityValue,
        "GetSpeed",
        &UCharacterMovementComponent::GetSpeed,
        "GetMovementMode",
        [](UCharacterMovementComponent& C)
        {
            return static_cast<int32>(C.GetMovementMode());
        },
        "SetMovementMode",
        [](UCharacterMovementComponent& C, int32 Mode)
        {
            C.SetMovementMode(static_cast<EMovementMode>(Mode));
        },
        "IsWalking",
        &UCharacterMovementComponent::IsWalking,
        "IsFalling",
        &UCharacterMovementComponent::IsFalling,
        "Jump",
        &UCharacterMovementComponent::Jump,
        "HasPendingRootMotion",
        &UCharacterMovementComponent::HasPendingRootMotion,
        "HasYawDrivenByRootMotion",
        &UCharacterMovementComponent::HasYawDrivenByRootMotion
    );

    Lua.new_usertype<UProjectileMovementComponent>(
        "ProjectileMovementComponent",
        sol::base_classes,
        sol::bases<UMovementComponent, UActorComponent, UObject>(),
        "SetVelocity",
        &UProjectileMovementComponent::SetVelocity,
        "GetVelocity",
        &UProjectileMovementComponent::GetVelocity,
        "SetInitialSpeed",
        &UProjectileMovementComponent::SetInitialSpeed,
        "GetInitialSpeed",
        &UProjectileMovementComponent::GetInitialSpeed,
        "GetMaxSpeed",
        &UProjectileMovementComponent::GetMaxSpeed,
        "GetPreviewVelocity",
        &UProjectileMovementComponent::GetPreviewVelocity,
        "StopSimulating",
        &UProjectileMovementComponent::StopSimulating
    );

    Lua.new_usertype<URotatingMovementComponent>(
        "RotatingMovementComponent",
        sol::base_classes,
        sol::bases<UMovementComponent, UActorComponent, UObject>(),
        "SetRotationRate",
        [](URotatingMovementComponent& C, const FVector& Rate)
        {
            C.SetRotationRate(FRotator(Rate));
        },
        "GetRotationRate",
        [](URotatingMovementComponent& C)
        {
            return C.GetRotationRate().ToVector();
        },
        "SetRotationInLocalSpace",
        &URotatingMovementComponent::SetRotationInLocalSpace,
        "IsRotationInLocalSpace",
        &URotatingMovementComponent::IsRotationInLocalSpace,
        "SetPivotTranslation",
        &URotatingMovementComponent::SetPivotTranslation,
        "GetPivotTranslation",
        &URotatingMovementComponent::GetPivotTranslation
    );

    Lua.new_usertype<UPendulumMovementComponent>(
        "PendulumMovementComponent",
        sol::base_classes,
        sol::bases<UMovementComponent, UActorComponent, UObject>()
    );

    Lua.new_usertype<UShapeComponent>(
        "ShapeComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "IsDrawOnlyIfSelected",
        &UShapeComponent::IsDrawOnlyIfSelected,
        "GetShapeColor",
        [](UShapeComponent& C, sol::this_state State)
        {
            return LuaVector4ToTable(State, C.GetShapeColorVec4());
        }
    );

    Lua.new_usertype<UBoxComponent>(
        "BoxComponent",
        sol::base_classes,
        sol::bases<UShapeComponent, UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetBoxExtent",
        &UBoxComponent::SetBoxExtent,
        "GetScaledBoxExtent",
        &UBoxComponent::GetScaledBoxExtent,
        "GetUnscaledBoxExtent",
        &UBoxComponent::GetUnscaledBoxExtent
    );

    Lua.new_usertype<USphereComponent>(
        "SphereComponent",
        sol::base_classes,
        sol::bases<UShapeComponent, UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetSphereRadius",
        &USphereComponent::SetSphereRadius,
        "GetScaledSphereRadius",
        &USphereComponent::GetScaledSphereRadius,
        "GetUnscaledSphereRadius",
        &USphereComponent::GetUnscaledSphereRadius
    );

    Lua.new_usertype<UCapsuleComponent>(
        "CapsuleComponent",
        sol::base_classes,
        sol::bases<UShapeComponent, UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetCapsuleSize",
        &UCapsuleComponent::SetCapsuleSize,
        "GetScaledCapsuleRadius",
        &UCapsuleComponent::GetScaledCapsuleRadius,
        "GetScaledCapsuleHalfHeight",
        &UCapsuleComponent::GetScaledCapsuleHalfHeight,
        "GetUnscaledCapsuleRadius",
        &UCapsuleComponent::GetUnscaledCapsuleRadius,
        "GetUnscaledCapsuleHalfHeight",
        &UCapsuleComponent::GetUnscaledCapsuleHalfHeight
    );

    Lua.new_usertype<ULightComponentBase>(
        "LightComponentBase",
        sol::base_classes,
        sol::bases<USceneComponent, UActorComponent, UObject>(),
        "GetIntensity",
        &ULightComponentBase::GetIntensity,
        "SetIntensity",
        &ULightComponentBase::SetIntensity,
        "GetLightColor",
        [](ULightComponentBase& C, sol::this_state State)
        {
            return LuaVector4ToTable(State, C.GetLightColor());
        },
        "SetLightColor",
        [](ULightComponentBase& C, float R, float G, float B, sol::optional<float> A)
        {
            C.SetLightColor(FVector4(R, G, B, A.value_or(1.0f)));
        },
        "IsVisible",
        &ULightComponentBase::IsVisible,
        "CastShadows",
        &ULightComponentBase::CastShadows,
        "GetLightType",
        [](ULightComponentBase& C)
        {
            return static_cast<int32>(C.GetLightType());
        },
        "PushToScene",
        &ULightComponentBase::PushToScene,
        "DestroyFromScene",
        &ULightComponentBase::DestroyFromScene
    );

    Lua.new_usertype<ULightComponent>(
        "LightComponent",
        sol::base_classes,
        sol::bases<ULightComponentBase, USceneComponent, UActorComponent, UObject>(),
        "GetShadowResolutionScale",
        &ULightComponent::GetShadowResolutionScale,
        "GetShadowBias",
        &ULightComponent::GetShadowBias,
        "SetShadowBias",
        &ULightComponent::SetShadowBias,
        "GetShadowSlopeBias",
        &ULightComponent::GetShadowSlopeBias,
        "SetShadowSlopeBias",
        &ULightComponent::SetShadowSlopeBias,
        "GetShadowNormalBias",
        &ULightComponent::GetShadowNormalBias,
        "SetShadowNormalBias",
        &ULightComponent::SetShadowNormalBias,
        "GetShadowSharpen",
        &ULightComponent::GetShadowSharpen,
        "SetShadowSharpen",
        &ULightComponent::SetShadowSharpen
    );

    Lua.new_usertype<UAmbientLightComponent>("AmbientLightComponent", sol::base_classes, sol::bases<ULightComponent, ULightComponentBase, USceneComponent, UActorComponent, UObject>());
    Lua.new_usertype<UDirectionalLightComponent>("DirectionalLightComponent", sol::base_classes, sol::bases<ULightComponent, ULightComponentBase, USceneComponent, UActorComponent, UObject>());
    Lua.new_usertype<UPointLightComponent>(
        "PointLightComponent",
        sol::base_classes,
        sol::bases<ULightComponent, ULightComponentBase, USceneComponent, UActorComponent, UObject>(),
        "GetAttenuationRadius",
        &UPointLightComponent::GetAttenuationRadius,
        "SetAttenuationRadius",
        &UPointLightComponent::SetAttenuationRadius
    );
    Lua.new_usertype<USpotLightComponent>(
        "SpotLightComponent",
        sol::base_classes,
        sol::bases<UPointLightComponent, ULightComponent, ULightComponentBase, USceneComponent, UActorComponent, UObject>(),
        "GetOuterConeAngle",
        &USpotLightComponent::GetOuterConeAngle
    );

    Lua.new_usertype<UTextRenderComponent>(
        "TextRenderComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetText",
        &UTextRenderComponent::SetText,
        "GetText",
        &UTextRenderComponent::GetText,
        "SetFont",
        [](UTextRenderComponent& C, const FString& FontName)
        {
            C.SetFont(FName(FontName));
        },
        "GetFontName",
        [](UTextRenderComponent& C)
        {
            return C.GetFontName().ToString();
        },
        "SetColor",
        [](UTextRenderComponent& C, float R, float G, float B, sol::optional<float> A)
        {
            C.SetColor(FVector4(R, G, B, A.value_or(1.0f)));
        },
        "GetColor",
        [](UTextRenderComponent& C, sol::this_state State)
        {
            return LuaVector4ToTable(State, C.GetColor());
        },
        "SetFontSize",
        &UTextRenderComponent::SetFontSize,
        "GetFontSize",
        &UTextRenderComponent::GetFontSize,
        "SetRenderSpace",
        [](UTextRenderComponent& C, int32 Space)
        {
            C.SetRenderSpace(static_cast<ETextRenderSpace>(Space));
        },
        "GetRenderSpace",
        [](UTextRenderComponent& C)
        {
            return static_cast<int32>(C.GetRenderSpace());
        },
        "SetScreenPosition",
        &UTextRenderComponent::SetScreenPosition,
        "GetScreenX",
        &UTextRenderComponent::GetScreenX,
        "GetScreenY",
        &UTextRenderComponent::GetScreenY,
        "SetHorizontalAlignment",
        [](UTextRenderComponent& C, int32 Align)
        {
            C.SetHorizontalAlignment(static_cast<ETextHAlign>(Align));
        },
        "GetHorizontalAlignment",
        [](UTextRenderComponent& C)
        {
            return static_cast<int32>(C.GetHorizontalAlignment());
        },
        "SetVerticalAlignment",
        [](UTextRenderComponent& C, int32 Align)
        {
            C.SetVerticalAlignment(static_cast<ETextVAlign>(Align));
        },
        "GetVerticalAlignment",
        [](UTextRenderComponent& C)
        {
            return static_cast<int32>(C.GetVerticalAlignment());
        }
    );

    Lua.new_usertype<UBillboardComponent>(
        "BillboardComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetBillboardEnabled",
        &UBillboardComponent::SetBillboardEnabled
    );

    Lua.new_usertype<USpringArmComponent>(
        "SpringArmComponent",
        sol::base_classes,
        sol::bases<USceneComponent, UActorComponent, UObject>()
    );
    Lua.new_usertype<UCineCameraComponent>(
        "CineCameraComponent",
        sol::base_classes,
        sol::bases<UCameraComponent, USceneComponent, UActorComponent, UObject>(),
        "SetLetterboxEnabled",
        &UCineCameraComponent::SetLetterboxEnabled,
        "SetLetterboxAmount",
        &UCineCameraComponent::SetLetterboxAmount,
        "SetLetterboxThickness",
        &UCineCameraComponent::SetLetterboxThickness,
        "SetLetterboxColor",
        [](UCineCameraComponent& C, float R, float G, float B, sol::optional<float> A)
        {
            C.SetLetterboxColor(FLinearColor(R, G, B, A.value_or(1.0f)));
        }
    );

    Lua.new_usertype<UMaterial>(
        "Material",
        sol::base_classes,
        sol::bases<UObject>(),
        "SetScalarParameter",
        &UMaterial::SetScalarParameter,
        "SetVector3Parameter",
        &UMaterial::SetVector3Parameter,
        "SetVector4Parameter",
        &UMaterial::SetVector4Parameter,
        "SetTextureParameter",
        &UMaterial::SetTextureParameter,
        "GetScalarParameterValue",
        &UMaterial::GetScalarParameterValue,
        "GetVector3ParameterValue",
        &UMaterial::GetVector3ParameterValue,
        "IsMaterialInstance",
        &UMaterial::IsMaterialInstance,
        "IsDynamicMaterialInstance",
        &UMaterial::IsDynamicMaterialInstance,
        "IsGraphMaterial",
        &UMaterial::IsGraphMaterial,
        "EnableGraphMaterial",
        &UMaterial::EnableGraphMaterial,
        "DisableGraphMaterial",
        &UMaterial::DisableGraphMaterial,
        "GetAssetPathFileName",
        &UMaterial::GetAssetPathFileName,
        "SetAssetPathFileName",
        &UMaterial::SetAssetPathFileName
    );

    {
        sol::object MaterialLibraryObject = Lua["MaterialLibrary"];
        sol::object MaterialTypeObject = Lua["Material"];
        if (MaterialLibraryObject.is<sol::table>() && MaterialTypeObject.is<sol::table>())
        {
            sol::table MaterialLibrary = MaterialLibraryObject.as<sol::table>();
            sol::table MaterialType = MaterialTypeObject.as<sol::table>();
            const char* LibraryFunctions[] = {
                    "Load",
                    "GetOrCreate",
                    "Create",
                    "CreateGraph",
                    "GetComponentMaterial",
                    "SetComponentMaterial",
                    "SetComponentMaterialByPath",
                    "CreateDynamicInstance",
                    "CreateDynamicInstanceForComponent",
                    "Save",
                    "SetShader",
                    "SetScalarParameter",
                    "SetVectorParameter",
                    "SetColorParameter",
                    "SetTextureParameter"
            };
            for (const char* FunctionName : LibraryFunctions)
            {
                sol::object Function = MaterialLibrary[FunctionName];
                MaterialType[FunctionName] = Function;
            }
        }
    }

    Lua.new_usertype<UMaterialInstanceDynamic>(
        "MaterialInstanceDynamic",
        sol::base_classes,
        sol::bases<UMaterial, UObject>(),
        "SetScalarParameterValue",
        &UMaterialInstanceDynamic::SetScalarParameterValue,
        "SetVector3ParameterValue",
        &UMaterialInstanceDynamic::SetVector3ParameterValue,
        "SetVectorParameterValue",
        &UMaterialInstanceDynamic::SetVectorParameterValue,
        "SetTextureParameterValue",
        &UMaterialInstanceDynamic::SetTextureParameterValue,
        "GetOwnerObject",
        &UMaterialInstanceDynamic::GetOwnerObject
    );

    Lua.new_usertype<UAnimSequence>(
        "AnimSequence",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetNumberOfFrames",
        &UAnimSequence::GetNumberOfFrames,
        "TimeToFrame",
        &UAnimSequence::TimeToFrame,
        "FrameToTime",
        &UAnimSequence::FrameToTime,
        "GetAssetPathFileName",
        &UAnimSequence::GetAssetPathFileName,
        "SetAssetPathFileName",
        &UAnimSequence::SetAssetPathFileName,
        "GetForceRootLock",
        &UAnimSequence::GetForceRootLock,
        "SetForceRootLock",
        &UAnimSequence::SetForceRootLock,
        "GetEnableRootMotion",
        &UAnimSequence::GetEnableRootMotion,
        "SetEnableRootMotion",
        &UAnimSequence::SetEnableRootMotion,
        "GetRootMotionBoneName",
        &UAnimSequence::GetRootMotionBoneName,
        "SetRootMotionBoneName",
        &UAnimSequence::SetRootMotionBoneName
    );

    Lua.new_usertype<UAnimMontage>(
        "AnimMontage",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetSourceSequence",
        &UAnimMontage::GetSourceSequence,
        "SetSourceSequence",
        &UAnimMontage::SetSourceSequence,
        "GetBlendInTime",
        &UAnimMontage::GetBlendInTime,
        "SetBlendInTime",
        &UAnimMontage::SetBlendInTime,
        "GetBlendOutTime",
        &UAnimMontage::GetBlendOutTime,
        "SetBlendOutTime",
        &UAnimMontage::SetBlendOutTime,
        "GetAssetPathFileName",
        &UAnimMontage::GetAssetPathFileName,
        "SetAssetPathFileName",
        &UAnimMontage::SetAssetPathFileName,
        "GetSourceSequencePath",
        &UAnimMontage::GetSourceSequencePath,
        "EnsureDefaultSection",
        &UAnimMontage::EnsureDefaultSection
    );

    Lua.new_usertype<USkeletalMesh>(
        "SkeletalMesh",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetAssetPathFileName",
        &USkeletalMesh::GetAssetPathFileName,
        "SetAssetPathFileName",
        &USkeletalMesh::SetAssetPathFileName,
        "GetPhysicsAssetPath",
        &USkeletalMesh::GetPhysicsAssetPath
    );

    Lua.new_usertype<UAnimInstance>(
        "AnimInstance",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetOwningComponent",
        &UAnimInstance::GetOwningComponent,
        "GetSkeletalMesh",
        &UAnimInstance::GetSkeletalMesh,
        "TryGetPawnOwner",
        &UAnimInstance::TryGetPawnOwner,
        "GetRootMotionMode",
        [](UAnimInstance& I)
        {
            return static_cast<int32>(I.GetRootMotionMode());
        },
        "SetRootMotionMode",
        [](UAnimInstance& I, int32 Mode)
        {
            I.SetRootMotionMode(static_cast<ERootMotionMode>(Mode));
        },
        "PlayMontage",
        [](UAnimInstance& I, UAnimMontage* M, sol::optional<FString> Section, sol::optional<float> Rate)
        {
            if (IsValid(M)) I.PlayMontage(M, Section ? FName(Section.value()) : FName::None, Rate.value_or(1.0f));
        },
        "StopMontage",
        [](UAnimInstance& I, sol::optional<float> BlendOut, sol::optional<FString> Slot)
        {
            I.StopMontage(BlendOut.value_or(-1.0f), Slot ? FName(Slot.value()) : FName::None);
        },
        "Montage_JumpToSection",
        [](UAnimInstance& I, const FString& Section, sol::optional<FString> Slot)
        {
            I.Montage_JumpToSection(FName(Section), Slot ? FName(Slot.value()) : FName::None);
        },
        "Montage_SetNextSection",
        [](UAnimInstance& I, const FString& From, const FString& To, sol::optional<FString> Slot)
        {
            I.Montage_SetNextSection(FName(From), FName(To), Slot ? FName(Slot.value()) : FName::None);
        },
        "IsMontagePlaying",
        [](UAnimInstance& I, sol::optional<UAnimMontage*> M, sol::optional<FString> Slot)
        {
            return I.IsMontagePlaying(M.value_or(nullptr), Slot ? FName(Slot.value()) : FName::None);
        },
        "IsAnimGraphInstance",
        [](UAnimInstance& I)
        {
            return Cast<UAnimGraphInstance>(&I) != nullptr;
        },
        "SetGraphVariableFloat",
        [](UAnimInstance& I, const FString& VariableName, float Value)
        {
            if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->SetGraphVariableFloat(FName(VariableName), Value);
            }
            return false;
        },
        "SetGraphVariableBool",
        [](UAnimInstance& I, const FString& VariableName, bool bValue)
        {
            if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->SetGraphVariableBool(FName(VariableName), bValue);
            }
            return false;
        },
        "SetGraphVariableInt",
        [](UAnimInstance& I, const FString& VariableName, int32 Value)
        {
            if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->SetGraphVariableInt(FName(VariableName), Value);
            }
            return false;
        },
        "SetGraphVariableTrigger",
        [](UAnimInstance& I, const FString& VariableName)
        {
            if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->SetGraphVariableTrigger(FName(VariableName));
            }
            return false;
        },
        "HasGraphVariableFloat",
        [](UAnimInstance& I, const FString& VariableName)
        {
            float Value = 0.0f;
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->GetGraphVariableFloat(FName(VariableName), Value);
            }
            return false;
        },
        "HasGraphVariableBool",
        [](UAnimInstance& I, const FString& VariableName)
        {
            bool bValue = false;
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->GetGraphVariableBool(FName(VariableName), bValue);
            }
            return false;
        },
        "HasGraphVariableInt",
        [](UAnimInstance& I, const FString& VariableName)
        {
            int32 Value = 0;
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->GetGraphVariableInt(FName(VariableName), Value);
            }
            return false;
        },
        "GetGraphVariableFloat",
        [](UAnimInstance& I, const FString& VariableName, sol::optional<float> DefaultValue)
        {
            float Value = DefaultValue.value_or(0.0f);
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                float RuntimeValue = Value;
                if (Graph->GetGraphVariableFloat(FName(VariableName), RuntimeValue))
                {
                    return RuntimeValue;
                }
            }
            return Value;
        },
        "GetGraphVariableBool",
        [](UAnimInstance& I, const FString& VariableName, sol::optional<bool> DefaultValue)
        {
            bool bValue = DefaultValue.value_or(false);
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                bool bRuntimeValue = bValue;
                if (Graph->GetGraphVariableBool(FName(VariableName), bRuntimeValue))
                {
                    return bRuntimeValue;
                }
            }
            return bValue;
        },
        "GetGraphVariableInt",
        [](UAnimInstance& I, const FString& VariableName, sol::optional<int32> DefaultValue)
        {
            int32 Value = DefaultValue.value_or(0);
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                int32 RuntimeValue = Value;
                if (Graph->GetGraphVariableInt(FName(VariableName), RuntimeValue))
                {
                    return RuntimeValue;
                }
            }
            return Value;
        }
    );

    Lua.new_usertype<ACharacter>(
        "Character",
        sol::base_classes,
        sol::bases<APawn, AActor, UObject>(),
        "AddMovementInput",
        sol::overload(
            [](ACharacter& C, const FVector& Direction, float Scale)
            {
                C.AddMovementInput(Direction, Scale);
            },
            [](ACharacter& C, const FVector& Direction)
            {
                C.AddMovementInput(Direction, 1.0f);
            }
        ),
        "Jump",
        &ACharacter::Jump,
        "GetCapsuleComponent",
        &ACharacter::GetCapsuleComponent,
        "GetMesh",
        &ACharacter::GetMesh,
        "GetCharacterMovement",
        &ACharacter::GetCharacterMovement
    );

    Lua.new_usertype<UActionComponent>(
        "ActionComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "HitStop",
        &UActionComponent::HitStop,
        "HitSquash",
        &UActionComponent::HitSquash,
        "Knockback",
        &UActionComponent::Knockback,
        "Slomo",
        &UActionComponent::Slomo,
        "StopHitStop",
        &UActionComponent::StopHitStop,
        "StopHitSquash",
        &UActionComponent::StopHitSquash,
        "StopKnockback",
        &UActionComponent::StopKnockback,
        "StopSlomo",
        &UActionComponent::StopSlomo,
        "StopAllActions",
        &UActionComponent::StopAllActions
    );

    Lua.new_usertype<UFloatingPawnMovementComponent>(
        "FloatingPawnMovementComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "SetMoveInput",
        &UFloatingPawnMovementComponent::SetMoveInput,
        "SetLookInput",
        &UFloatingPawnMovementComponent::SetLookInput
    );

    Lua.new_usertype<UWheeledVehicleMovementComponent>(
        "WheeledVehicleMovementComponent",
        sol::base_classes,
        sol::bases<UMovementComponent, UActorComponent, UObject>(),
        "SetThrottleInput",
        &UWheeledVehicleMovementComponent::SetThrottleInput,
        "SetBrakeInput",
        &UWheeledVehicleMovementComponent::SetBrakeInput,
        "SetSteeringInput",
        &UWheeledVehicleMovementComponent::SetSteeringInput,
        "SetHandbrakeInput",
        &UWheeledVehicleMovementComponent::SetHandbrakeInput,
        "ResetVehicle",
        &UWheeledVehicleMovementComponent::ResetVehicle,
        "GetForwardSpeed",
        &UWheeledVehicleMovementComponent::GetForwardSpeed,
        "IsVehicleCreated",
        &UWheeledVehicleMovementComponent::IsVehicleCreated
    );

    Lua.new_usertype<UVehicleWheelPoseComponent>(
        "VehicleWheelPoseComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "SetVehicleMovement",
        &UVehicleWheelPoseComponent::SetVehicleMovement,
        "GetVehicleMovement",
        &UVehicleWheelPoseComponent::GetVehicleMovement
    );

    Lua.new_usertype<UParticleModule>(
        "ParticleModule",
        sol::base_classes,
        sol::bases<UObject>(),
        "IsEnabled",
        &UParticleModule::IsEnabled,
        "SetEnabled",
        &UParticleModule::SetEnabled,
        "GetDisplayName",
        &UParticleModule::GetDisplayName,
        "GetCategory",
        [](UParticleModule& Module)
        {
            return static_cast<int32>(Module.GetCategory());
        },
        "IsUnique",
        &UParticleModule::IsUnique
    );

    Lua.new_usertype<UParticleLODLevel>(
        "ParticleLODLevel",
        sol::base_classes,
        sol::bases<UObject>(),
        "Level",
        sol::property(
            [](UParticleLODLevel& LOD)
            {
                return LOD.Level;
            },
            [](UParticleLODLevel& LOD, int32 Level)
            {
                LOD.Level = Level;
            }
        ),
        "Enabled",
        sol::property(
            [](UParticleLODLevel& LOD)
            {
                return LOD.bEnabled;
            },
            [](UParticleLODLevel& LOD, bool bEnabled)
            {
                LOD.bEnabled = bEnabled;
            }
        ),
        "GetRequiredModule",
        [](UParticleLODLevel& LOD) -> UParticleModuleRequired*
        {
            return LOD.RequiredModule;
        },
        "GetSpawnModule",
        [](UParticleLODLevel& LOD) -> UParticleModuleSpawn*
        {
            return LOD.SpawnModule;
        },
        "GetTypeDataModule",
        [](UParticleLODLevel& LOD) -> UParticleModule*
        {
            return LOD.TypeDataModule;
        },
        "GetModuleCount",
        [](UParticleLODLevel& LOD)
        {
            return static_cast<int32>(LOD.Modules.size());
        },
        "GetModule",
        [](UParticleLODLevel& LOD, int32 Index) -> UParticleModule*
        {
            return (Index >= 0 && Index < static_cast<int32>(LOD.Modules.size())) ? LOD.Modules[Index] : nullptr;
        },
        "GetModules",
        [](UParticleLODLevel& LOD, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            int32           Idx    = 1;
            for (UParticleModule* Module : LOD.Modules) if (IsValid(Module)) Result[Idx++] = Module;
            return Result;
        },
        "AddModule",
        &UParticleLODLevel::AddModule,
        "RemoveModule",
        &UParticleLODLevel::RemoveModule,
        "AddModuleByClass",
        [](UParticleLODLevel& LOD, const FString& ClassName) -> UParticleModule*
        {
            UObject*         Obj    = FObjectFactory::Get().Create(ClassName, &LOD);
            UParticleModule* Module = Cast<UParticleModule>(Obj);
            if (!IsValid(Module))
            {
                if (IsValid(Obj)) UObjectManager::Get().DestroyObject(Obj);
                return nullptr;
            }
            if (!LOD.AddModule(Module))
            {
                UObjectManager::Get().DestroyObject(Module);
                return nullptr;
            }
            return Module;
        },
        "ValidateModules",
        &UParticleLODLevel::ValidateModules,
        "UpdateFromLOD0",
        &UParticleLODLevel::UpdateFromLOD0
    );

    Lua.new_usertype<UParticleEmitter>(
        "ParticleEmitter",
        sol::base_classes,
        sol::bases<UObject>(),
        "Name",
        sol::property(
            [](UParticleEmitter& Emitter)
            {
                return Emitter.EmitterName;
            },
            [](UParticleEmitter& Emitter, const FString& Name)
            {
                Emitter.EmitterName = Name;
            }
        ),
        "Enabled",
        sol::property(&UParticleEmitter::IsEnabled, &UParticleEmitter::SetEnabled),
        "IsEnabled",
        &UParticleEmitter::IsEnabled,
        "SetEnabled",
        &UParticleEmitter::SetEnabled,
        "GetQualityLevelSpawnRateMult",
        &UParticleEmitter::GetQualityLevelSpawnRateMult,
        "SetQualityLevelSpawnRateMult",
        &UParticleEmitter::SetQualityLevelSpawnRateMult,
        "InitializeDefaultLODLevel",
        &UParticleEmitter::InitializeDefaultLODLevel,
        "EnsureLODCoreModules",
        &UParticleEmitter::EnsureLODCoreModules,
        "CreateLODLevel",
        &UParticleEmitter::CreateLODLevel,
        "RemoveLODLevel",
        &UParticleEmitter::RemoveLODLevel,
        "GetLODLevel",
        &UParticleEmitter::GetLODLevel,
        "GetCurrentLODLevel",
        &UParticleEmitter::GetCurrentLODLevel,
        "GetLODCount",
        &UParticleEmitter::GetLODCount,
        "CacheEmitterModuleInfo",
        &UParticleEmitter::CacheEmitterModuleInfo,
        "GetParticleSize",
        &UParticleEmitter::GetParticleSize,
        "GetReqInstanceBytes",
        &UParticleEmitter::GetReqInstanceBytes
    );

    Lua.new_usertype<UParticleSystem>(
        "ParticleSystem",
        sol::base_classes,
        sol::bases<UObject>(),
        "Looping",
        sol::property(
            [](UParticleSystem& System)
            {
                return System.bLooping;
            },
            [](UParticleSystem& System, bool bLooping)
            {
                System.bLooping = bLooping;
            }
        ),
        "UpdateTimeFPS",
        sol::property(
            [](UParticleSystem& System)
            {
                return System.UpdateTimeFPS;
            },
            [](UParticleSystem& System, float FPS)
            {
                System.UpdateTimeFPS = FPS;
            }
        ),
        "AddEmitter",
        &UParticleSystem::AddEmitter,
        "RemoveEmitter",
        &UParticleSystem::RemoveEmitter,
        "MoveEmitter",
        &UParticleSystem::MoveEmitter,
        "GetEmitterCount",
        &UParticleSystem::GetEmitterCount,
        "GetEmitter",
        &UParticleSystem::GetEmitter,
        "GetMaxLODCount",
        &UParticleSystem::GetMaxLODCount,
        "EnsureLODDistances",
        &UParticleSystem::EnsureLODDistances,
        "GetLODIndexForDistance",
        &UParticleSystem::GetLODIndexForDistance,
        "GetLODDistance",
        &UParticleSystem::GetLODDistance,
        "SetLODDistance",
        &UParticleSystem::SetLODDistance,
        "BuildEmitters",
        &UParticleSystem::BuildEmitters,
        "GetSourcePath",
        &UParticleSystem::GetSourcePath,
        "SetSourcePath",
        &UParticleSystem::SetSourcePath
    );

    Lua.new_usertype<UParticleSystemComponent>(
        "ParticleSystemComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetTemplate",
        &UParticleSystemComponent::SetTemplate,
        "SetTemplateByPath",
        [](UParticleSystemComponent& Component, const FString& Path)
        {
            Component.SetTemplate(FParticleSystemManager::Get().Load(Path));
        },
        "GetTemplate",
        &UParticleSystemComponent::GetTemplate,
        "Activate",
        &UParticleSystemComponent::Activate,
        "Deactivate",
        &UParticleSystemComponent::Deactivate,
        "ResetParticles",
        &UParticleSystemComponent::ResetParticles,
        "IsActive",
        &UParticleSystemComponent::IsActive,
        "GetEmitterInstanceCount",
        &UParticleSystemComponent::GetEmitterInstanceCount,
        "GetCurrentLODIndex",
        &UParticleSystemComponent::GetCurrentLODIndex,
        "SetCurrentLODIndex",
        &UParticleSystemComponent::SetCurrentLODIndex,
        "RebuildInstances",
        &UParticleSystemComponent::RebuildInstances,
        "GetTemplatePath",
        &UParticleSystemComponent::GetTemplatePath
    );

    Lua.new_usertype<AParticleSystemActor>(
        "ParticleSystemActor",
        sol::base_classes,
        sol::bases<AActor, UObject>(),
        "GetParticleSystemComponent",
        &AParticleSystemActor::GetParticleSystemComponent
    );

    sol::table Particle = Lua.create_named_table("Particle");
    Particle.set_function(
        "LoadSystem",
        [](const FString& Path) -> UParticleSystem*
        {
            return FParticleSystemManager::Get().Load(Path);
        }
    );
    Particle.set_function(
        "FindSystem",
        [](const FString& Path) -> UParticleSystem*
        {
            return FParticleSystemManager::Get().Find(Path);
        }
    );
    Particle.set_function(
        "SpawnEmitterAtLocation",
        [](const FString& Path, const FVector& Location, sol::optional<FVector> Rotation, sol::optional<bool> bActivate) -> UParticleSystemComponent*
        {
            if (!GEngine || Path.empty() || Path == "None")
            {
                return nullptr;
            }

            UWorld* World = GEngine->GetWorld();
            if (!World)
            {
                return nullptr;
            }

            return FGameplayStatics::SpawnEmitterAtLocation(
                World,
                Path,
                Location,
                FRotator(Rotation.value_or(FVector(0.0f, 0.0f, 0.0f))),
                bActivate.value_or(true));
        }
    );
    Particle.set_function(
        "SaveSystem",
        [](UParticleSystem* System)
        {
            return FParticleSystemManager::Get().Save(System);
        }
    );
    Particle.set_function(
        "NewSystem",
        []() -> UParticleSystem*
        {
            return UObjectManager::Get().CreateObject<UParticleSystem>();
        }
    );
    Particle.set_function(
        "NewEmitter",
        [](UObject* Outer) -> UParticleEmitter*
        {
            return UObjectManager::Get().CreateObject<UParticleEmitter>(Outer);
        }
    );
    Particle.set_function(
        "NewModule",
        [](const FString& ClassName, UObject* Outer) -> UParticleModule*
        {
            UObject* Obj = FObjectFactory::Get().Create(ClassName, Outer);
            return Cast<UParticleModule>(Obj);
        }
    );
    Particle.set_function(
        "AddEmitter",
        [](UParticleSystem* System) -> UParticleEmitter*
        {
            return IsValid(System) ? System->AddEmitter() : nullptr;
        }
    );
    Particle.set_function(
        "AddModule",
        [](UParticleLODLevel* LOD, const FString& ClassName) -> UParticleModule*
        {
            if (!IsValid(LOD)) return nullptr;
            UObject*         Obj    = FObjectFactory::Get().Create(ClassName, LOD);
            UParticleModule* Module = Cast<UParticleModule>(Obj);
            if (!IsValid(Module))
            {
                if (IsValid(Obj)) UObjectManager::Get().DestroyObject(Obj);
                return nullptr;
            }
            if (!LOD->AddModule(Module))
            {
                UObjectManager::Get().DestroyObject(Module);
                return nullptr;
            }
            return Module;
        }
    );
    Particle.set_function(
        "SetComponentTemplate",
        [](UParticleSystemComponent* Component, UParticleSystem* System)
        {
            if (IsValid(Component)) Component->SetTemplate(System);
        }
    );
    Particle.set_function(
        "SetComponentTemplateByPath",
        [](UParticleSystemComponent* Component, const FString& Path)
        {
            if (IsValid(Component)) Component->SetTemplate(FParticleSystemManager::Get().Load(Path));
        }
    );
    Particle.set_function(
        "SpawnEmitterAtLocation",
        [](const FString& Path, const FVector& Location, sol::optional<FVector> Rotation, sol::optional<FVector> Scale, sol::optional<bool> bActivate, sol::optional<bool> bTickWhenPaused) -> AActor*
        {
            if (!GEngine || !GEngine->GetWorld()) return nullptr;
            UParticleSystemComponent* Component = FGameplayStatics::SpawnEmitterAtLocation(
                GEngine->GetWorld(),
                Path,
                Location,
                FRotator(Rotation.value_or(FVector(0.0f, 0.0f, 0.0f))),
                bActivate.value_or(true)
            );
            if (!IsValid(Component)) return nullptr;

            Component->PrimaryComponentTick.bTickEvenWhenPaused = bTickWhenPaused.value_or(false);

            AActor* Owner = Component->GetOwner();
            if (IsValid(Owner))
            {
                Owner->SetActorScale(Scale.value_or(FVector(1.0f, 1.0f, 1.0f)));
            }
            return Owner;
        }
    );

    Lua.new_usertype<USceneComponent>(
        "SceneComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "Location",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetWorldLocation();
            },
            [](USceneComponent& Component, const FVector& Location)
            {
                Component.SetWorldLocation(Location);
            }
        ),
        "Rotation",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetRelativeRotation().ToVector();
            },
            [](USceneComponent& Component, const FVector& Rotation)
            {
                Component.SetRelativeRotation(Rotation);
            }
        ),
        "Forward",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetForwardVector();
            }
        ),
        "Right",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetRightVector();
            }
        ),
        "Up",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetUpVector();
            }
        ),
        "GetLocation",
        [](USceneComponent& Component)
        {
            return Component.GetWorldLocation();
        },
        "SetLocation",
        [](USceneComponent& Component, const FVector& Location)
        {
            Component.SetWorldLocation(Location);
        },
        "GetRotation",
        [](USceneComponent& Component)
        {
            return Component.GetRelativeRotation().ToVector();
        },
        "SetRotation",
        [](USceneComponent& Component, const FVector& Rotation)
        {
            Component.SetRelativeRotation(Rotation);
        },

        // 부모 기준 상대 위치 — 동일한 메시를 4개 깐 바퀴 같은 케이스에서 앞/뒤 구분 등
        // 위치 기반 필터링에 쓰인다. 월드 위치는 위 "Location" 프로퍼티 참고.
        "RelativeLocation",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetRelativeLocation();
            },
            [](USceneComponent& Component, const FVector& V)
            {
                Component.SetRelativeLocation(V);
            }
        )
    );

    Lua.new_usertype<UPrimitiveComponent>(
        "PrimitiveComponent",
        sol::base_classes,
        sol::bases<USceneComponent, UActorComponent, UObject>(),
        "IsValid",
        [](UPrimitiveComponent* Component)
        {
            return IsValid(Component);
        },
        "SetSimulatePhysics",
        [](UPrimitiveComponent* Component, bool bSimulate)
        {
            if (IsValid(Component)) Component->SetSimulatePhysics(bSimulate);
        },
        "GetSimulatePhysics",
        [](UPrimitiveComponent* Component) -> bool
        {
            return IsValid(Component) ? Component->GetSimulatePhysics() : false;
        },
        "SetVisibility",
        [](UPrimitiveComponent* Component, bool bVisible)
        {
            if (IsValid(Component)) Component->SetVisibility(bVisible);
        },
        "IsVisible",
        [](UPrimitiveComponent* Component) -> bool
        {
            return IsValid(Component) ? Component->IsVisible() : false;
        },
        "AddForce",
        [](UPrimitiveComponent* Component, const FVector& Force)
        {
            if (IsValid(Component)) Component->AddForce(Force);
        },
        "AddForceAtLocation",
        [](UPrimitiveComponent* Component, const FVector& Force, const FVector& Location)
        {
            if (IsValid(Component)) Component->AddForceAtLocation(Force, Location);
        },
        "AddTorque",
        [](UPrimitiveComponent* Component, const FVector& Torque)
        {
            if (IsValid(Component)) Component->AddTorque(Torque);
        },
        "AddImpulse",
        [](UPrimitiveComponent* Component, const FVector& Impulse)
        {
            if (IsValid(Component)) Component->AddImpulse(Impulse);
        },
        "GetLinearVelocity",
        [](UPrimitiveComponent* Component) -> FVector
        {
            return IsValid(Component) ? Component->GetLinearVelocity() : FVector::ZeroVector;
        },
        "SetLinearVelocity",
        [](UPrimitiveComponent* Component, const FVector& Vel)
        {
            if (IsValid(Component)) Component->SetLinearVelocity(Vel);
        },
        "GetAngularVelocity",
        [](UPrimitiveComponent* Component) -> FVector
        {
            return IsValid(Component) ? Component->GetAngularVelocity() : FVector::ZeroVector;
        },
        "SetAngularVelocity",
        [](UPrimitiveComponent* Component, const FVector& Vel)
        {
            if (IsValid(Component)) Component->SetAngularVelocity(Vel);
        },
        "GetMass",
        [](UPrimitiveComponent* Component) -> float
        {
            return IsValid(Component) ? Component->GetMass() : 0.0f;
        },
        "SetMass",
        [](UPrimitiveComponent* Component, float Mass)
        {
            if (IsValid(Component)) Component->SetMass(Mass);
        },
        "GetGenerateOverlapEvents",
        [](UPrimitiveComponent* Component) -> bool
        {
            return IsValid(Component) ? Component->GetGenerateOverlapEvents() : false;
        }
    );

    Lua.new_usertype<UStaticMesh>(
        "StaticMesh",
        sol::base_classes,
        sol::bases<UObject>(),
        "AssetPath",
        sol::property(
            [](UStaticMesh& Mesh)
            {
                return Mesh.GetAssetPathFileName();
            }
        ),
        "GetAssetPath",
        [](UStaticMesh& Mesh)
        {
            return Mesh.GetAssetPathFileName();
        }
    );

    // 메시 에셋 경로로 컴포넌트 식별 가능하게 노출. 자동 생성된 FName ("UStaticMeshComponent_41")
    // 은 월드 초기화 순서에 따라 카운터가 달라져 빌드별로 매칭이 깨질 수 있다. 메시 경로는
    // 씬 파일에 명시 저장되므로 deterministic.
    Lua.new_usertype<UStaticMeshComponent>(
        "StaticMeshComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "MeshPath",
        sol::property(
            [](UStaticMeshComponent& C)
            {
                return C.GetStaticMeshPath();
            }
        ),
        "GetMeshPath",
        [](UStaticMeshComponent& C)
        {
            return C.GetStaticMeshPath();
        },
        "SetStaticMesh",
        &UStaticMeshComponent::SetStaticMesh,
        "SetStaticMeshByPath",
        &UStaticMeshComponent::SetStaticMeshByPath,
        "ClearStaticMesh",
        &UStaticMeshComponent::ClearStaticMesh,
        "GetStaticMesh",
        &UStaticMeshComponent::GetStaticMesh,
        "SetMaterialByPath",
        &UStaticMeshComponent::SetMaterialByPath,
        "SetMaterial",
        &UStaticMeshComponent::SetMaterial,
        "GetMaterial",
        &UStaticMeshComponent::GetMaterial,
        "GetMaterialPath",
        &UStaticMeshComponent::GetMaterialPath,
        "GetMaterialSlotCount",
        &UStaticMeshComponent::GetMaterialSlotCount
    );

    Lua.new_usertype<USkinnedMeshComponent>(
        "SkinnedMeshComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetSkeletalMeshByPath",
        &USkinnedMeshComponent::SetSkeletalMeshByPath,
        "ClearSkeletalMesh",
        &USkinnedMeshComponent::ClearSkeletalMesh,
        "GetSkeletalMesh",
        &USkinnedMeshComponent::GetSkeletalMesh,
        "GetSkeletalMeshPathValue",
        &USkinnedMeshComponent::GetSkeletalMeshPathValue,
        "SetMaterialByPath",
        &USkinnedMeshComponent::SetMaterialByPath,
        "SetMaterial",
        &USkinnedMeshComponent::SetMaterial,
        "GetMaterial",
        &USkinnedMeshComponent::GetMaterial,
        "GetMaterialPath",
        &USkinnedMeshComponent::GetMaterialPath,
        "GetMaterialSlotCount",
        &USkinnedMeshComponent::GetMaterialSlotCount
    );

    Lua.new_usertype<USkeletalMeshComponent>(
        "SkeletalMeshComponent",
        sol::base_classes,
        sol::bases<USkinnedMeshComponent, UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "PlayAnimationByPath",
        &USkeletalMeshComponent::PlayAnimationByPath,
        "StopAnimation",
        &USkeletalMeshComponent::StopAnimation,
        "SetAnimationByPath",
        &USkeletalMeshComponent::SetAnimationByPath,
        "SetPlayRate",
        &USkeletalMeshComponent::SetPlayRate,
        "SetLooping",
        &USkeletalMeshComponent::SetLooping,
        "SetPlaying",
        &USkeletalMeshComponent::SetPlaying,
        "GetAnimInstance",
        &USkeletalMeshComponent::GetAnimInstance,
        "GetAnimationMode",
        &USkeletalMeshComponent::GetAnimationMode,
        "GetAnimation",
        &USkeletalMeshComponent::GetAnimation,
        "ResetClothSimulation",
        &USkeletalMeshComponent::ResetClothSimulation,
        "SetClothPreviewWindOverride",
        sol::overload(
            [](USkeletalMeshComponent& Component, bool bEnable, const FVector& WindVelocity)
            {
                Component.SetClothPreviewWindOverride(bEnable, WindVelocity);
            },
            [](USkeletalMeshComponent& Component, bool bEnable, float X, float Y, float Z)
            {
                Component.SetClothPreviewWindOverride(bEnable, FVector(X, Y, Z));
            }
        ),
        "SetClothWindOverride",
        sol::overload(
            [](USkeletalMeshComponent& Component, const FVector& WindVelocity)
            {
                Component.SetClothPreviewWindOverride(true, WindVelocity);
            },
            [](USkeletalMeshComponent& Component, float X, float Y, float Z)
            {
                Component.SetClothPreviewWindOverride(true, FVector(X, Y, Z));
            }
        ),
        "ClearClothWindOverride",
        [](USkeletalMeshComponent& Component)
        {
            Component.SetClothPreviewWindOverride(false, FVector::ZeroVector);
        }
    );

    Lua.new_usertype<FHitResult>(
        "HitResult",
        "HitComponent",
        sol::property(
            [](const FHitResult& Hit) -> UPrimitiveComponent*
            {
                return IsValid(Hit.HitComponent) ? Hit.HitComponent : nullptr;
            }
        ),
        "HitActor",
        sol::property(
            [](const FHitResult& Hit) -> AActor*
            {
                return IsValid(Hit.HitActor) ? Hit.HitActor : nullptr;
            }
        ),
        "GetHitComponent",
        [](const FHitResult& Hit) -> UPrimitiveComponent*
        {
            return IsValid(Hit.HitComponent) ? Hit.HitComponent : nullptr;
        },
        "GetHitActor",
        [](const FHitResult& Hit) -> AActor*
        {
            return IsValid(Hit.HitActor) ? Hit.HitActor : nullptr;
        },
        "Distance",
        &FHitResult::Distance,
        "PenetrationDepth",
        &FHitResult::PenetrationDepth,
        "WorldHitLocation",
        &FHitResult::WorldHitLocation,
        "WorldNormal",
        &FHitResult::WorldNormal,
        "ImpactNormal",
        &FHitResult::ImpactNormal,
        "FaceIndex",
        &FHitResult::FaceIndex,
        "bHit",
        &FHitResult::bHit
    );

    Lua.new_usertype<UCameraComponent>(
        "CameraComponent",
        sol::base_classes,
        sol::bases<USceneComponent, UActorComponent, UObject>(),
        "LookAt",
        &UCameraComponent::LookAt,
        "SetFOV",
        &UCameraComponent::SetFOV,
        "GetFOV",
        &UCameraComponent::GetFOV,
        "SetAspectRatio",
        &UCameraComponent::SetAspectRatio,
        "GetAspectRatio",
        &UCameraComponent::GetAspectRatio,
        "SetNearPlane",
        &UCameraComponent::SetNearPlane,
        "GetNearPlane",
        &UCameraComponent::GetNearPlane,
        "SetFarPlane",
        &UCameraComponent::SetFarPlane,
        "GetFarPlane",
        &UCameraComponent::GetFarPlane,
        "SetOrthoWidth",
        &UCameraComponent::SetOrthoWidth,
        "GetOrthoWidth",
        &UCameraComponent::GetOrthoWidth,
        "SetOrthographic",
        &UCameraComponent::SetOrthographic,
        "IsOrthographic",
        &UCameraComponent::IsOrthogonal,
        "SetLetterbox",
        [](UCameraComponent& Camera, bool bEnabled, sol::optional<float> Amount, sol::optional<float> Thickness, sol::optional<sol::object> Color)
        {
            Camera.SetLetterboxEnabled(bEnabled);
            if (Amount) Camera.SetLetterboxAmount(Amount.value());
            if (Thickness) Camera.SetLetterboxThickness(Thickness.value());
            if (Color)
            {
                FVector4 ColorValue;
                if (LuaObjectToVector4(Color.value(), ColorValue))
                {
                    Camera.SetLetterboxColor(FLinearColor(ColorValue.X, ColorValue.Y, ColorValue.Z, ColorValue.W));
                }
            }
        },
        "ClearLetterbox",
        [](UCameraComponent& Camera)
        {
            Camera.SetLetterboxEnabled(false);
        },
        "OnResize",
        &UCameraComponent::OnResize
    );

    Lua.new_usertype<USoundComponent>(
        "SoundComponent",
        sol::base_classes,
        sol::bases<USceneComponent, UActorComponent, UObject>(),
        "Play",
        &USoundComponent::Play,
        "Stop",
        &USoundComponent::Stop,
        "IsPlaying",
        &USoundComponent::IsPlaying,
        "SetSoundPath",
        &USoundComponent::SetSoundPath,
        "GetSoundPath",
        &USoundComponent::GetSoundPath,
        "SetVolume",
        &USoundComponent::SetVolume,
        "GetVolume",
        &USoundComponent::GetVolume,
        "SetPitch",
        &USoundComponent::SetPitch,
        "GetPitch",
        &USoundComponent::GetPitch,
        "SetLooping",
        &USoundComponent::SetLooping,
        "IsLooping",
        &USoundComponent::IsLooping,
        "SetPlayOnBeginPlay",
        &USoundComponent::SetPlayOnBeginPlay,
        "ShouldPlayOnBeginPlay",
        &USoundComponent::ShouldPlayOnBeginPlay,
        "SetSpatialized",
        &USoundComponent::SetSpatialized,
        "IsSpatialized",
        &USoundComponent::IsSpatialized,
        "Set3DMinMaxDistance",
        &USoundComponent::Set3DMinMaxDistance,
        "GetMinDistance",
        &USoundComponent::GetMinDistance,
        "GetMaxDistance",
        &USoundComponent::GetMaxDistance,
        "GetActiveHandle",
        &USoundComponent::GetActiveHandle
    );

    Lua.new_usertype<AActor>(
        "Actor",
        sol::base_classes,
        sol::bases<UObject>(),
        "Location",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetActorLocation();
            },
            [](AActor& Actor, const FVector& Location)
            {
                Actor.SetActorLocation(Location);
            }
        ),
        "Rotation",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetActorRotation().ToVector();
            },
            [](AActor& Actor, const FVector& Rotation)
            {
                Actor.SetActorRotation(Rotation);
            }
        ),

        "Scale",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetActorScale();
            },
            [](AActor& Actor, const FVector& Scale)
            {
                Actor.SetActorScale(Scale);
            }
        ),

        "Forward",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetActorForward();
            }
        ),

        "Right",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetActorRight();
            }
        ),

        "AddWorldOffset",
        [](AActor& Actor, const FVector& Offset)
        {
            Actor.AddActorWorldOffset(Offset);
        },

        "Destroy",
        [](AActor& Actor)
        {
            // World->DestroyActor가 EndPlay + 정리. Lua는 호출 후 해당 액터를 더 참조하지 말 것.
            if (UWorld* W = Actor.GetWorld()) W->DestroyActor(&Actor);
        },

        "IsValid",
        [](AActor* Actor)
        {
            // Lua가 보유한 actor 핸들이 cpp 측에서 destroy됐는지 확인. nil/destroyed면 false.
            return IsValid(Actor);
        },

        "AsSniperPawn",
        [](AActor& Actor) -> ASniperPawn*
        {
            return Cast<ASniperPawn>(&Actor);
        },

        "HasTag",
        [](AActor& Actor, const FString& Tag)
        {
            return Actor.HasTag(FName(Tag));
        },
        "AddTag",
        [](AActor& Actor, const FString& Tag)
        {
            Actor.AddTag(FName(Tag));
        },
        "RemoveTag",
        [](AActor& Actor, const FString& Tag)
        {
            Actor.RemoveTag(FName(Tag));
        },
        "GetTags",
        [](AActor& Actor) -> sol::table
        {
            sol::table Result = FLuaScriptManager::GetState().create_table();
            int        Index  = 1;
            for (const FName& Tag : Actor.GetTags())
            {
                Result[Index++] = Tag.ToString();
            }
            return Result;
        },
        "SetTags",
        [](AActor& Actor, sol::table Tags)
        {
            TArray<FName> Names;
            for (auto& Entry : Tags)
            {
                sol::object Value = Entry.second;
                if (Value.is<std::string>()) Names.push_back(FName(FString(Value.as<std::string>())));
            }
            Actor.SetTags(Names);
        },
        "GetComponents",
        [](AActor& Actor) -> sol::table
        {
            sol::table Result = FLuaScriptManager::GetState().create_table();
            int        Index  = 1;
            for (UActorComponent* Component : Actor.GetComponents())
            {
                if (IsValid(Component)) Result[Index++] = Component;
            }
            return Result;
        },
        "FindComponentByTag",
        [](AActor& Actor, const FString& Tag) -> UActorComponent*
        {
            return Actor.FindComponentByTag(FName(Tag));
        },
        "GetComponentByTag",
        [](AActor& Actor, const FString& Tag) -> UActorComponent*
        {
            return Actor.FindComponentByTag(FName(Tag));
        },
        "FindComponentsByTag",
        [](AActor& Actor, const FString& Tag, sol::this_state State) -> sol::table
        {
            return ComponentsToLuaTable(State, Actor.FindComponentsByTag(FName(Tag)));
        },
        "GetComponentsByTag",
        [](AActor& Actor, const FString& Tag, sol::this_state State) -> sol::table
        {
            return ComponentsToLuaTable(State, Actor.FindComponentsByTag(FName(Tag)));
        },
        "FindComponentByTags",
        [](AActor& Actor, sol::variadic_args Args) -> UActorComponent*
        {
            return Actor.FindComponentByTags(LuaTagsFromArgs(Args));
        },
        "GetComponentByTags",
        [](AActor& Actor, sol::variadic_args Args) -> UActorComponent*
        {
            return Actor.FindComponentByTags(LuaTagsFromArgs(Args));
        },
        "FindComponentsByTags",
        [](AActor& Actor, sol::variadic_args Args, sol::this_state State) -> sol::table
        {
            return ComponentsToLuaTable(State, Actor.FindComponentsByTags(LuaTagsFromArgs(Args)));
        },
        "GetComponentsByTags",
        [](AActor& Actor, sol::variadic_args Args, sol::this_state State) -> sol::table
        {
            return ComponentsToLuaTable(State, Actor.FindComponentsByTags(LuaTagsFromArgs(Args)));
        },

        "GetFloatingPawnMovement",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UFloatingPawnMovementComponent>();
        },

        "GetVehicleMovement",
        [](AActor& Actor) -> UWheeledVehicleMovementComponent*
        {
            if (UWheeledVehicleMovementComponent* Movement = Actor.GetComponentByClass<UWheeledVehicleMovementComponent>())
            {
                return Movement;
            }
            return nullptr;
        },

        "GetStaticMeshComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UStaticMeshComponent>();
        },

        "InitStaticMeshActor",
        [](AActor& Actor, const FString& MeshPath) -> bool
        {
            AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(&Actor);
            if (!IsValid(StaticMeshActor))
            {
                return false;
            }

            if (!IsValid(StaticMeshActor->GetStaticMeshComponent()))
            {
                StaticMeshActor->InitDefaultComponents(MeshPath);
            }
            else if (UStaticMeshComponent* StaticMeshComponent = StaticMeshActor->GetStaticMeshComponent())
            {
                StaticMeshComponent->SetStaticMeshByPath(MeshPath);
            }
            return IsValid(StaticMeshActor->GetStaticMeshComponent());
        },

        "GetCamera",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UCameraComponent>();
        },

        "GetSkeletalMeshComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<USkeletalMeshComponent>();
        },

        "GetSkinnedMeshComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<USkinnedMeshComponent>();
        },

        "GetLuaBlueprintComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<ULuaBlueprintComponent>();
        },

        "GetLuaScriptComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<ULuaScriptComponent>();
        },

        "GetSniperWeaponComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<USniperWeaponComponent>();
        },

        "GetBallisticBulletManagerComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UBallisticBulletManagerComponent>();
        },

        "GetSniperDamageReceiverComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<USniperDamageReceiverComponent>();
        },

        "GetSoundComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<USoundComponent>();
        },

        "GetCombatGunfireSoundComponent",
        [](AActor& Actor) -> USoundComponent*
        {
            if (ACombatCharacter* CombatCharacter = Cast<ACombatCharacter>(&Actor))
            {
                return CombatCharacter->GetCombatGunfireSoundComponent();
            }
            return Actor.GetComponentByClass<USoundComponent>();
        },

        "GetActorSequenceComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UActorSequenceComponent>();
        },

        "GetParticleSystemComponent",
        [](AActor& Actor) -> UParticleSystemComponent*
        {
            if (AParticleSystemActor* ParticleActor = Cast<AParticleSystemActor>(&Actor))
            {
                return ParticleActor->GetParticleSystemComponent();
            }
            return Actor.GetComponentByClass<UParticleSystemComponent>();
        },

        "GetActionComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UActionComponent>();
        },

        "GetRootComponent",
        [](AActor& Actor) -> USceneComponent*
        {
            return Actor.GetRootComponent();
        },

        "GetRootPrimitiveComponent",
        [](AActor& Actor) -> UPrimitiveComponent*
        {
            return Cast<UPrimitiveComponent>(Actor.GetRootComponent());
        },

        "AddForceToRoot",
        [](AActor& Actor, const FVector& Force)
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            if (IsValid(Root)) Root->AddForce(Force);
        },
        "AddTorqueToRoot",
        [](AActor& Actor, const FVector& Torque)
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            if (IsValid(Root)) Root->AddTorque(Torque);
        },
        "AddImpulseToRoot",
        [](AActor& Actor, const FVector& Impulse)
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            if (IsValid(Root)) Root->AddImpulse(Impulse);
        },
        "GetRootLinearVelocity",
        [](AActor& Actor) -> FVector
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            return IsValid(Root) ? Root->GetLinearVelocity() : FVector::ZeroVector;
        },
        "SetRootLinearVelocity",
        [](AActor& Actor, const FVector& Velocity)
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            if (IsValid(Root)) Root->SetLinearVelocity(Velocity);
        },
        "SetRootSimulatePhysics",
        [](AActor& Actor, bool bSimulate)
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            if (IsValid(Root)) Root->SetSimulatePhysics(bSimulate);
        },

        "GetPrimitiveComponent",
        [](AActor& Actor) -> UPrimitiveComponent*
        {
            return Actor.GetComponentByClass<UPrimitiveComponent>();
        },

        "GetPrimitiveComponentByName",
        [](AActor& Actor, const FString& ComponentName) -> UPrimitiveComponent*
        {
            for (UActorComponent* Component : Actor.GetComponents())
            {
                UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);
                if (PrimitiveComponent && PrimitiveComponent->GetFName().ToString() == ComponentName)
                {
                    return PrimitiveComponent;
                }
            }
            return nullptr;
        },

        "GetComponentByName",
        [](AActor& Actor, const FString& ComponentName) -> USceneComponent*
        {
            for (UActorComponent* Component : Actor.GetComponents())
            {
                USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
                if (SceneComponent && SceneComponent->GetFName().ToString() == ComponentName)
                {
                    return SceneComponent;
                }
            }
            return nullptr;
        },

        "UUID",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetUUID();
            }
        ),

        "Name",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetFName().ToString();
            }
        )
    );

    Lua.new_usertype<APlayerController>(
        "PlayerController",
        sol::base_classes,
        sol::bases<AActor, UObject>(),
        "Possess",
        &APlayerController::Possess,
        "UnPossess",
        &APlayerController::UnPossess,
        "GetPossessedPawn",
        &APlayerController::GetPossessedPawn,
        "GetPlayerCameraManager",
        &APlayerController::GetPlayerCameraManager,
        "SetInputModeGameOnly",
        [](APlayerController& Self)
        {
            Self.SetInputModeGameOnly();
        },
        "SetInputModeUIOnly",
        [](APlayerController& Self)
        {
            Self.SetInputModeUIOnly();
        },
        "SetInputModeGameAndUI",
        [](APlayerController& Self)
        {
            Self.SetInputModeGameAndUI();
        },
        "SetViewTargetWithBlend",
        [](APlayerController& Self, AActor* Target, sol::optional<float> BlendTime)
        {
            if (IsValid(Target))
            {
                Self.SetViewTargetWithBlend(Target, BlendTime.value_or(0.0f));
            }
        }
    );

    Lua.new_usertype<APawn>(
        "Pawn",
        sol::base_classes,
        sol::bases<AActor, UObject>(),
        "GetController",
        &APawn::GetController,
        "IsPossessed",
        &APawn::IsPossessed,
        "SetAutoPossessPlayer",
        &APawn::SetAutoPossessPlayer,
        "GetAutoPossessPlayer",
        &APawn::GetAutoPossessPlayer,
        "GetInputComponent",
        &APawn::GetInputComponent,
        "GetControlRotation",
        &APawn::GetControlRotation,
        "SetControlRotation",
        &APawn::SetControlRotation,
        "AddYawInput",
        &APawn::AddYawInput,
        "AddPitchInput",
        &APawn::AddPitchInput
    );

    Lua.new_usertype<ASniperPawn>(
        "SniperPawn",
        sol::base_classes,
        sol::bases<APawn, AActor, UObject>(),
        "GetSniperRoot",
        &ASniperPawn::GetSniperRoot,
        "GetCamera",
        &ASniperPawn::GetCamera,
        "GetSniperWeaponComponent",
        &ASniperPawn::GetSniperWeaponComponent,
        "GetBallisticBulletManagerComponent",
        &ASniperPawn::GetBallisticBulletManagerComponent,
        "IsScoped",
        &ASniperPawn::IsScoped,
        "IsReloading",
        &ASniperPawn::IsReloading,
        "GetReloadRemaining",
        &ASniperPawn::GetReloadRemaining,
        "GetReloadProgress",
        &ASniperPawn::GetReloadProgress,
        "ForceScopeReleased",
        &ASniperPawn::ForceScopeReleased,
        "GetScopeBlendAlpha",
        &ASniperPawn::GetScopeBlendAlpha,
        "GetCurrentScopeFOV",
        &ASniperPawn::GetCurrentScopeFOV,
        "GetCurrentScopeZoomMagnification",
        &ASniperPawn::GetCurrentScopeZoomMagnification,
        "GetMinScopeZoomMagnification",
        &ASniperPawn::GetMinScopeZoomMagnification,
        "GetMaxScopeZoomMagnification",
        &ASniperPawn::GetMaxScopeZoomMagnification,
        "GetCurrentScopeSensitivity",
        &ASniperPawn::GetCurrentScopeSensitivity,
        "GetMouseSensitivityMultiplier",
        &ASniperPawn::GetMouseSensitivityMultiplier,
        "SetMouseSensitivityMultiplier",
        &ASniperPawn::SetMouseSensitivityMultiplier,
        "GetGamepadLookSensitivityMultiplier",
        &ASniperPawn::GetGamepadLookSensitivityMultiplier,
        "SetGamepadLookSensitivityMultiplier",
        &ASniperPawn::SetGamepadLookSensitivityMultiplier,
        "IsHoldBreathActive",
        &ASniperPawn::IsHoldBreathActive,
        "GetHoldBreathGauge",
        &ASniperPawn::GetHoldBreathGauge,
        "GetMaxHoldBreathGauge",
        &ASniperPawn::GetMaxHoldBreathGauge,
        "IsHoldBreathInputHeld",
        &ASniperPawn::IsHoldBreathInputHeld,
        "IsHoldBreathRecovering",
        &ASniperPawn::IsHoldBreathRecovering,
        "IsHoldBreathReleaseRequired",
        &ASniperPawn::IsHoldBreathReleaseRequired,
        "IsHoldBreathOnCooldown",
        &ASniperPawn::IsHoldBreathOnCooldown,
        "GetHoldBreathCooldownRemaining",
        &ASniperPawn::GetHoldBreathCooldownRemaining,
        "GetHoldBreathDuration",
        &ASniperPawn::GetHoldBreathDuration,
        "GetHoldBreathGaugeRatio",
        &ASniperPawn::GetHoldBreathGaugeRatio
    );

    Lua.new_usertype<AWheeledVehiclePawn>(
        "WheeledVehiclePawn",
        sol::base_classes,
        sol::bases<APawn, AActor, UObject>(),
        "GetMesh",
        &AWheeledVehiclePawn::GetMesh,
        "GetVehicleMovement",
        &AWheeledVehiclePawn::GetVehicleMovement,
        "GetWheelPoseComponent",
        &AWheeledVehiclePawn::GetWheelPoseComponent,
        "GetSpringArm",
        &AWheeledVehiclePawn::GetSpringArm,
        "GetCamera",
        &AWheeledVehiclePawn::GetCamera
    );

    // UInputComponent — Pawn::GetInputComponent 로 얻어 lua 에서 직접 매핑/binding 추가 가능.
    // 예 (BeginPlay 안):
    //   local input = obj:AsPawn():GetInputComponent()
    //   input:AddActionMapping("Jump", "Space")
    //   input:BindAction("Jump", "Pressed", function() print("jump!") end)
    Lua.new_usertype<UInputComponent>(
        "InputComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "AddAxisMapping",
        sol::overload(
            [](UInputComponent& Self, const FString& Name, const FString& KeyName, float Scale)
            {
                Self.AddAxisMapping(Name, ResolveInputKeyCode(KeyName), Scale);
            },
            [](UInputComponent& Self, const FString& Name, const FString& KeyName)
            {
                Self.AddAxisMapping(Name, ResolveInputKeyCode(KeyName), 1.0f);
            },
            [](UInputComponent& Self, const FString& Name, int32 KeyCode, float Scale)
            {
                Self.AddAxisMapping(Name, KeyCode, Scale);
            },
            [](UInputComponent& Self, const FString& Name, int32 KeyCode)
            {
                Self.AddAxisMapping(Name, KeyCode, 1.0f);
            }
        ),
        "AddMouseAxisMapping",
        sol::overload(
            [](UInputComponent& Self, const FString& Name, const FString& AxisName, float Scale)
            {
                EInputAxisSourceType Axis = EInputAxisSourceType::MouseX;
                if (AxisName == "MouseY") Axis = EInputAxisSourceType::MouseY;
                else if (AxisName == "MouseWheel") Axis = EInputAxisSourceType::MouseWheel;
                Self.AddMouseAxisMapping(Name, Axis, Scale);
            },
            [](UInputComponent& Self, const FString& Name, const FString& AxisName)
            {
                EInputAxisSourceType Axis = EInputAxisSourceType::MouseX;
                if (AxisName == "MouseY") Axis = EInputAxisSourceType::MouseY;
                else if (AxisName == "MouseWheel") Axis = EInputAxisSourceType::MouseWheel;
                Self.AddMouseAxisMapping(Name, Axis, 1.0f);
            }
        ),
        "AddActionMapping",
        sol::overload(
            [](UInputComponent& Self, const FString& Name, const FString& KeyName)
            {
                Self.AddActionMapping(Name, ResolveInputKeyCode(KeyName));
            },
            [](UInputComponent& Self, const FString& Name, int32 KeyCode)
            {
                Self.AddActionMapping(Name, KeyCode);
            }
        ),
        "BindAxis",
        [](UInputComponent& Self, const FString& Name, sol::protected_function Cb)
        {
            Self.BindAxis(
                Name,
                [Cb](float V)
                {
                    FScopedGarbageCollectionBlocker GCBlocker;
                    auto                            R = Cb(V);
                    if (!R.valid())
                    {
                        sol::error e = R;
                        UE_LOG("[Lua] BindAxis cb error: %s", e.what());
                    }
                }
            );
        },
        "BindAction",
        [](UInputComponent& Self, const FString& Name, const FString& EventStr, sol::protected_function Cb)
        {
            const EInputEvent Ev = (EventStr == "Released") ? EInputEvent::Released : EInputEvent::Pressed;
            Self.BindAction(
                Name,
                Ev,
                [Cb]()
                {
                    FScopedGarbageCollectionBlocker GCBlocker;
                    auto                            R = Cb();
                    if (!R.valid())
                    {
                        sol::error e = R;
                        UE_LOG("[Lua] BindAction cb error: %s", e.what());
                    }
                }
            );
        },
        "ClearBindings",
        &UInputComponent::ClearBindings
    );

    // --- World binding — 런타임 액터 spawn 용 (Engine 일반 기능) ---
    sol::table World = Lua.create_named_table("World");
    World.set_function(
        "GetFirstPlayerController",
        []() -> APlayerController*
        {
            return (GEngine && GEngine->GetWorld()) ? GEngine->GetWorld()->GetFirstPlayerController() : nullptr;
        }
    );
    World.set_function(
        "SpawnActor",
        [](const FString& ClassName, sol::optional<FVector> Location, sol::optional<FVector> Rotation, sol::optional<FVector> Scale) -> AActor*
        {
            if (!GEngine) return nullptr;
            UWorld* W = GEngine->GetWorld();
            if (!W) return nullptr;
            UClass* Cls = UClass::FindByName(ClassName.c_str());
            if (!Cls) return nullptr;
            AActor* Actor = W->SpawnActorByClass(Cls);
            if (IsValid(Actor))
            {
                Actor->SetActorLocation(Location.value_or(FVector(0, 0, 0)));
                Actor->SetActorRotation(Rotation.value_or(FVector(0, 0, 0)));
                Actor->SetActorScale(Scale.value_or(FVector(1, 1, 1)));
            }
            return Actor;
        }
    );
    World.set_function(
        "SpawnPawn",
        [](const FString& ClassName, sol::optional<FVector> Location, sol::optional<FVector> Rotation, sol::optional<FVector> Scale, sol::optional<bool> bPossess) -> APawn*
        {
            if (!GEngine) return nullptr;
            UWorld* W = GEngine->GetWorld();
            if (!W) return nullptr;
            UClass* Cls = UClass::FindByName(ClassName.c_str());
            if (!Cls) return nullptr;
            AActor* Actor = W->SpawnActorByClass(Cls);
            APawn*  Pawn  = Cast<APawn>(Actor);
            if (!IsValid(Pawn))
            {
                if (IsValid(Actor)) W->DestroyActor(Actor);
                return nullptr;
            }
            Pawn->SetActorLocation(Location.value_or(FVector(0, 0, 0)));
            Pawn->SetActorRotation(Rotation.value_or(FVector(0, 0, 0)));
            Pawn->SetActorScale(Scale.value_or(FVector(1, 1, 1)));
            if (bPossess.value_or(false))
            {
                if (APlayerController* PC = W->GetFirstPlayerController()) PC->Possess(Pawn);
            }
            return Pawn;
        }
    );
    World.set_function(
        "SpawnActorFromPrefab",
        [](const FString& Path) -> AActor*
        {
            if (!GEngine) return nullptr;
            UWorld* W = GEngine->GetWorld();
            if (!W) return nullptr;
            return FPrefabManager::SpawnActorFromPrefab(W, Path);
        }
    );
    World.set_function(
        "SpawnActorFromPrefabAt",
        [](const FString& Path, sol::optional<FVector> Location, sol::optional<FVector> Rotation, sol::optional<FVector> Scale) -> AActor*
        {
            if (!GEngine) return nullptr;
            UWorld* W = GEngine->GetWorld();
            if (!W) return nullptr;
            AActor* Actor = FPrefabManager::SpawnActorFromPrefab(W, Path);
            ApplyOptionalActorTransform(Actor, Location, Rotation, Scale);
            return Actor;
        }
    );
    World.set_function(
        "SpawnActorFromPrefabByName",
        [](const FString& Directory, const FString& PrefabName, sol::optional<FVector> Location, sol::optional<FVector> Rotation, sol::optional<FVector> Scale) -> AActor*
        {
            if (!GEngine) return nullptr;
            UWorld* W = GEngine->GetWorld();
            if (!W) return nullptr;
            const FString Path = BuildPrefabPathFromDirectoryAndName(Directory, PrefabName);
            if (Path.empty()) return nullptr;
            AActor* Actor = FPrefabManager::SpawnActorFromPrefab(W, Path);
            ApplyOptionalActorTransform(Actor, Location, Rotation, Scale);
            return Actor;
        }
    );
    World.set_function(
        "FindActorByName",
        [](const FString& ActorName) -> AActor*
        {
            if (!GEngine || !GEngine->GetWorld()) return nullptr;
            UWorld* W = GEngine->GetWorld();
            for (AActor* Actor : W->GetActors())
            {
                if (IsValid(Actor) && Actor->GetFName().ToString() == ActorName)
                {
                    return Actor;
                }
            }
            return nullptr;
        }
    );
    World.set_function(
        "FindFirstActorByClass",
        [](const FString& ClassName) -> AActor*
        {
            if (!GEngine || !GEngine->GetWorld()) return nullptr;
            UWorld* W   = GEngine->GetWorld();
            UClass* Cls = UClass::FindByName(ClassName.c_str());
            if (!Cls) return nullptr;
            for (AActor* Actor : W->GetActors())
            {
                if (IsValid(Actor) && Actor->GetClass()->IsA(Cls))
                {
                    return Actor;
                }
            }
            return nullptr;
        }
    );
    World.set_function(
        "FindFirstSniperPawn",
        []() -> ASniperPawn*
        {
            if (!GEngine || !GEngine->GetWorld()) return nullptr;
            UWorld* W = GEngine->GetWorld();
            for (AActor* Actor : W->GetActors())
            {
                if (ASniperPawn* SniperPawn = Cast<ASniperPawn>(Actor))
                {
                    if (IsValid(SniperPawn))
                    {
                        return SniperPawn;
                    }
                }
            }
            return nullptr;
        }
    );
    World.set_function(
        "FindFirstActorByTag",
        [](const FString& Tag) -> AActor*
        {
            return FGameplayStatics::FindFirstActorByTag(
                GEngine ? GEngine->GetWorld() : nullptr,
                FName(Tag)
            );
        }
    );
    World.set_function(
        "FindActorsByTag",
        [](const FString& Tag) -> sol::table
        {
            sol::table            Result = FLuaScriptManager::GetState().create_table();
            const TArray<AActor*> Found  = FGameplayStatics::FindActorsByTag(
                GEngine ? GEngine->GetWorld() : nullptr,
                FName(Tag)
            );
            int Idx = 1; // Lua arrays are 1-indexed
            for (AActor* Actor : Found)
            {
                Result[Idx++] = Actor;
            }
            return Result;
        }
    );
    // LuaBlueprint ForEachActorByClass 노드용 — 동일 패턴(table 반환)으로 노출.
    World.set_function(
        "FindActorsByClass",
        [](const FString& ClassName) -> sol::table
        {
            sol::table Result = FLuaScriptManager::GetState().create_table();
            if (!GEngine || !GEngine->GetWorld()) return Result;
            UClass* Cls = UClass::FindByName(ClassName.c_str());
            if (!Cls)
            {
                static TSet<FString> WarnedUnknownClasses;
                if (WarnedUnknownClasses.find(ClassName) == WarnedUnknownClasses.end())
                {
                    WarnedUnknownClasses.insert(ClassName);
                    UE_LOG(
                        "World.FindActorsByClass: 등록되지 않은 액터 클래스 '%s' — 빈 리스트 반환 "
                        "(클래스 이름 오타/미설정 확인)",
                        ClassName.c_str()
                    );
                }
                return Result;
            }
            int Idx = 1;
            for (AActor* Actor : GEngine->GetWorld()->GetActors())
            {
                if (IsValid(Actor) && Actor->GetClass() && Actor->GetClass()->IsA(Cls))
                {
                    Result[Idx++] = Actor;
                }
            }
            return Result;
        }
    );
    World.set_function(
        "GetGameTime",
        []() -> float
        {
            UWorld* CurrentWorld = GEngine ? GEngine->GetWorld() : nullptr;
            return CurrentWorld ? CurrentWorld->GetGameTimeSeconds() : 0.0f;
        }
    );
    World.set_function(
        "LineTrace",
        [](const FVector& Start, const FVector& End, sol::optional<AActor*> IgnoreActor) -> sol::table
        {
            sol::table Result   = FLuaScriptManager::GetState().create_table();
            Result["Hit"]       = false;
            Result["Actor"]     = static_cast<AActor*>(nullptr);
            Result["Component"] = static_cast<UPrimitiveComponent*>(nullptr);
            Result["Location"]  = FVector(0, 0, 0);
            Result["Normal"]    = FVector(0, 0, 0);
            Result["Distance"]  = 0.0f;

            UWorld* CurrentWorld = GEngine ? GEngine->GetWorld() : nullptr;
            if (!CurrentWorld)
            {
                return Result;
            }

            FVector     Delta       = End - Start;
            const float MaxDistance = Delta.Length();
            if (MaxDistance <= 0.0001f)
            {
                return Result;
            }
            const FVector Direction = Delta / MaxDistance;
            FHitResult    Hit;
            if (CurrentWorld->PhysicsRaycast(Start, Direction, MaxDistance, Hit, ECollisionChannel::WorldStatic, IgnoreActor.value_or(nullptr)))
            {
                Result["Hit"]       = true;
                Result["Actor"]     = Hit.HitActor;
                Result["Component"] = Hit.HitComponent;
                Result["Location"]  = Hit.WorldHitLocation;
                Result["Normal"]    = Hit.WorldNormal;
                Result["Distance"]  = Hit.Distance;
            }
            return Result;
        }
    );
    World.set_function(
        "LineTraceGameplay",
        [](const FVector& Start, const FVector& End, sol::optional<AActor*> IgnoreActor) -> sol::table
        {
            sol::table Result   = FLuaScriptManager::GetState().create_table();
            Result["Hit"]       = false;
            Result["Actor"]     = static_cast<AActor*>(nullptr);
            Result["Component"] = static_cast<UPrimitiveComponent*>(nullptr);
            Result["Location"]  = FVector(0, 0, 0);
            Result["Normal"]    = FVector(0, 0, 0);
            Result["Distance"]  = 0.0f;

            UWorld* CurrentWorld = GEngine ? GEngine->GetWorld() : nullptr;
            if (!CurrentWorld)
            {
                return Result;
            }

            FVector Delta = End - Start;
            const float MaxDistance = Delta.Length();
            if (MaxDistance <= 0.0001f)
            {
                return Result;
            }

            const FVector Direction = Delta / MaxDistance;
            const uint32 ObjectMask =
                ObjectTypeBit(ECollisionChannel::WorldStatic) |
                ObjectTypeBit(ECollisionChannel::WorldDynamic) |
                ObjectTypeBit(ECollisionChannel::Pawn);

            FHitResult Hit;
            if (CurrentWorld->PhysicsRaycastByObjectTypes(Start, Direction, MaxDistance, Hit, ObjectMask, IgnoreActor.value_or(nullptr)))
            {
                Result["Hit"]       = true;
                Result["Actor"]     = Hit.HitActor;
                Result["Component"] = Hit.HitComponent;
                Result["Location"]  = Hit.WorldHitLocation;
                Result["Normal"]    = Hit.WorldNormal;
                Result["Distance"]  = Hit.Distance;
            }

            return Result;
        }
    );

    Lua.new_usertype<ULuaBlueprintComponent>(
        "LuaBlueprintComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "ReloadBlueprint",
        &ULuaBlueprintComponent::ReloadBlueprint,
        "CallFunction",
        &ULuaBlueprintComponent::CallFunction,
        "CallLuaBlueprintFileFunction",
        &ULuaBlueprintComponent::CallLuaBlueprintFileFunction,
        "CallLuaScriptFileFunction",
        &ULuaBlueprintComponent::CallLuaScriptFileFunction,
        "GetBlueprintPath",
        &ULuaBlueprintComponent::GetBlueprintPath,
        "SetBlueprintPath",
        &ULuaBlueprintComponent::SetBlueprintPath
    );

    Lua.new_usertype<ULuaScriptComponent>(
        "LuaScriptComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "ReloadScript",
        &ULuaScriptComponent::ReloadScript,
        "CallFunction",
        &ULuaScriptComponent::CallFunction,
        "GetScriptFile",
        &ULuaScriptComponent::GetScriptFile,
        "SetScriptFile",
        &ULuaScriptComponent::SetScriptFile,
        "GetInitialGameStateName",
        &ULuaScriptComponent::GetInitialGameStateName
    );

    Lua.new_usertype<USniperWeaponComponent>(
        "SniperWeaponComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "GetCurrentAmmoType",
        &USniperWeaponComponent::GetCurrentAmmoType,
        "SetCurrentAmmoType",
        &USniperWeaponComponent::SetCurrentAmmoType,
        "IsZeroingEnabled",
        &USniperWeaponComponent::IsZeroingEnabled,
        "GetZeroingEnabled",
        &USniperWeaponComponent::IsZeroingEnabled,
        "SetZeroingEnabled",
        &USniperWeaponComponent::SetZeroingEnabled,
        "GetZeroRangeMeters",
        &USniperWeaponComponent::GetZeroRangeMeters,
        "SetZeroRangeMeters",
        &USniperWeaponComponent::SetZeroRangeMeters,
        "CanFire",
        &USniperWeaponComponent::CanFire,
        "RequestFire",
        &USniperWeaponComponent::RequestFire,
        "RequestReload",
        &USniperWeaponComponent::RequestReload,
        "CancelReload",
        &USniperWeaponComponent::CancelReload,
        "IsReloading",
        &USniperWeaponComponent::IsReloading,
        "GetReloadRemaining",
        &USniperWeaponComponent::GetReloadRemaining,
        "GetReloadProgress",
        &USniperWeaponComponent::GetReloadProgress,
        "GetAmmoInMagazine",
        &USniperWeaponComponent::GetAmmoInMagazine,
        "GetMagazineCapacity",
        &USniperWeaponComponent::GetMagazineCapacity,
        "GetFireCooldownRemaining",
        &USniperWeaponComponent::GetFireCooldownRemaining,
        "GetBulletManagerComponent",
        &USniperWeaponComponent::GetBulletManagerComponent
    );

    Lua.new_usertype<UBallisticBulletManagerComponent>(
        "BallisticBulletManagerComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "GetAliveBulletCount",
        &UBallisticBulletManagerComponent::GetAliveBulletCount,
        "IsWindEnabled",
        &UBallisticBulletManagerComponent::IsWindEnabled,
        "GetWindEnabled",
        &UBallisticBulletManagerComponent::IsWindEnabled,
        "SetWindEnabled",
        &UBallisticBulletManagerComponent::SetWindEnabled,
        "GetWindAcceleration",
        &UBallisticBulletManagerComponent::GetWindAcceleration,
        "SetWindAcceleration",
        &UBallisticBulletManagerComponent::SetWindAcceleration,
        "GetBulletSnapshotById",
        &UBallisticBulletManagerComponent::GetBulletSnapshotById,
        "GetLatestBulletSnapshot",
        &UBallisticBulletManagerComponent::GetLatestBulletSnapshot,
        "GetWeaponComponent",
        &UBallisticBulletManagerComponent::GetWeaponComponent
    );

    Lua.new_usertype<FBulletCinematicSnapshot>(
        "BulletCinematicSnapshot",
        "BulletId",
        &FBulletCinematicSnapshot::BulletId,
        "Position",
        &FBulletCinematicSnapshot::Position,
        "PreviousPosition",
        &FBulletCinematicSnapshot::PreviousPosition,
        "Velocity",
        &FBulletCinematicSnapshot::Velocity,
        "TraveledDistance",
        &FBulletCinematicSnapshot::TraveledDistance,
        "LifeTime",
        &FBulletCinematicSnapshot::LifeTime,
        "AmmoType",
        &FBulletCinematicSnapshot::AmmoType,
        "Owner",
        sol::property(
            [](const FBulletCinematicSnapshot& Snapshot) -> AActor*
            {
                return IsValid(Snapshot.Owner) ? Snapshot.Owner : nullptr;
            }
        ),
        "bIsAlive",
        &FBulletCinematicSnapshot::bIsAlive,
        "bWasScopedShot",
        &FBulletCinematicSnapshot::bWasScopedShot
    );

    Lua.new_usertype<ASniperKillCamDirector>(
        "SniperKillCamDirector",
        sol::base_classes,
        sol::bases<AActor, UObject>(),
        "StartForBulletId",
        &ASniperKillCamDirector::StartForBulletId,
        "StopKillCam",
        &ASniperKillCamDirector::StopKillCam,
        "IsPlaying",
        &ASniperKillCamDirector::IsPlaying,
        "GetActiveBulletId",
        &ASniperKillCamDirector::GetActiveBulletId,
        "SetRailRigScalar",
        &ASniperKillCamDirector::SetRailRigScalar,
        "GetRailRigScalar",
        [](ASniperKillCamDirector& Director, const FString& PropertyName, sol::optional<float> DefaultValue)
        {
            return Director.GetRailRigScalar(PropertyName, DefaultValue.value_or(0.0f));
        },
        "SetScalar",
        &ASniperKillCamDirector::SetKillCamScalar,
        "GetScalar",
        [](ASniperKillCamDirector& Director, const FString& PropertyName, sol::optional<float> DefaultValue)
        {
            return Director.GetKillCamScalar(PropertyName, DefaultValue.value_or(0.0f));
        },
        "SetString",
        &ASniperKillCamDirector::SetKillCamString,
        "GetString",
        [](ASniperKillCamDirector& Director, const FString& PropertyName, sol::optional<FString> DefaultValue)
        {
            return Director.GetKillCamString(PropertyName, DefaultValue.value_or(""));
        },
        "SetVector",
        &ASniperKillCamDirector::SetKillCamVector,
        "GetVector",
        [](ASniperKillCamDirector& Director, const FString& PropertyName, sol::optional<FVector> DefaultValue)
        {
            return Director.GetKillCamVector(PropertyName, DefaultValue.value_or(FVector::ZeroVector));
        },
        "SetRotator",
        [](ASniperKillCamDirector& Director, const FString& PropertyName, const FVector& PitchYawRoll)
        {
            return Director.SetKillCamRotator(PropertyName, FRotator(PitchYawRoll.X, PitchYawRoll.Y, PitchYawRoll.Z));
        },
        "GetRotator",
        [](ASniperKillCamDirector& Director, const FString& PropertyName, sol::optional<FVector> DefaultValue)
        {
            const FVector Fallback = DefaultValue.value_or(FVector::ZeroVector);
            const FRotator Value = Director.GetKillCamRotator(
                PropertyName,
                FRotator(Fallback.X, Fallback.Y, Fallback.Z));
            return FVector(Value.Pitch, Value.Yaw, Value.Roll);
        }
    );

    Lua.new_usertype<USniperDamageReceiverComponent>(
        "SniperDamageReceiverComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "GetMaxHP",
        &USniperDamageReceiverComponent::GetMaxHP,
        "GetCurrentHP",
        &USniperDamageReceiverComponent::GetCurrentHP,
        "IsFriendly",
        &USniperDamageReceiverComponent::IsFriendly,
        "HasArmor",
        &USniperDamageReceiverComponent::HasArmor,
        "GetArmorStrength",
        &USniperDamageReceiverComponent::GetArmorStrength,
        "AllowsRicochet",
        &USniperDamageReceiverComponent::AllowsRicochet,
        "CanRagdoll",
        &USniperDamageReceiverComponent::CanRagdoll,
        "IsDead",
        &USniperDamageReceiverComponent::IsDead,
        "CanReceiveSniperHit",
        &USniperDamageReceiverComponent::CanReceiveSniperHit,
        "ResolveSniperHit",
        &USniperDamageReceiverComponent::ResolveSniperHit,
        "ResetHealth",
        &USniperDamageReceiverComponent::ResetHealth,
        "ApplySniperHit",
        &USniperDamageReceiverComponent::ApplySniperHit,
        "ApplyResolvedSniperHit",
        &USniperDamageReceiverComponent::ApplyResolvedSniperHit
    );

    Lua.new_usertype<FSniperHitInfo>(
        "SniperHitInfo",
        "BulletId",
        &FSniperHitInfo::BulletId,
        "HitActor",
        sol::property(
            [](const FSniperHitInfo& HitInfo) -> AActor*
            {
                return IsValid(HitInfo.HitActor) ? HitInfo.HitActor : nullptr;
            }
        ),
        "Shooter",
        sol::property(
            [](const FSniperHitInfo& HitInfo) -> AActor*
            {
                return IsValid(HitInfo.Shooter) ? HitInfo.Shooter : nullptr;
            }
        ),
        "HitLocation",
        &FSniperHitInfo::HitLocation,
        "HitNormal",
        &FSniperHitInfo::HitNormal,
        "ShotDirection",
        &FSniperHitInfo::ShotDirection,
        "Damage",
        &FSniperHitInfo::Damage,
        "TravelDistance",
        &FSniperHitInfo::TravelDistance,
        "ImpactSpeed",
        &FSniperHitInfo::ImpactSpeed,
        "RagdollImpulseStrength",
        &FSniperHitInfo::RagdollImpulseStrength,
        "AmmoType",
        &FSniperHitInfo::AmmoType,
        "HitOutcome",
        &FSniperHitInfo::HitOutcome,
        "HitRegion",
        &FSniperHitInfo::HitRegion,
        "bIsScopedShot",
        &FSniperHitInfo::bIsScopedShot,
        "bIsHeadshot",
        &FSniperHitInfo::bIsHeadshot,
        "bIsArmorPiercing",
        &FSniperHitInfo::bIsArmorPiercing,
        "bShouldRagdoll",
        &FSniperHitInfo::bShouldRagdoll,
        "bKilled",
        &FSniperHitInfo::bKilled,
        "bFriendlyTarget",
        &FSniperHitInfo::bFriendlyTarget,
        "RegionDamageMultiplier",
        &FSniperHitInfo::RegionDamageMultiplier,
        "TargetCurrentHP",
        &FSniperHitInfo::TargetCurrentHP,
        "TargetMaxHP",
        &FSniperHitInfo::TargetMaxHP,
        "HitBoneName",
        &FSniperHitInfo::HitBoneName,
        "HitBoneNameString",
        sol::property(
            [](const FSniperHitInfo& HitInfo) -> FString
            {
                return HitInfo.HitBoneName.IsValid() && HitInfo.HitBoneName != FName::None
                    ? HitInfo.HitBoneName.ToString()
                    : FString();
            }
        ),
        "HitBodyName",
        &FSniperHitInfo::HitBodyName,
        "HitRegionName",
        &FSniperHitInfo::HitRegionName,
        "HitRegionDisplayName",
        &FSniperHitInfo::HitRegionDisplayName,
        "HitScoreMultiplier",
        &FSniperHitInfo::HitScoreMultiplier,
        "HitScoreValue",
        &FSniperHitInfo::HitScoreValue,
        "bHasHitBodyCenterDistance",
        &FSniperHitInfo::bHasHitBodyCenterDistance,
        "HitBodyCenterLocation",
        &FSniperHitInfo::HitBodyCenterLocation,
        "HitBodyCenterDistance",
        &FSniperHitInfo::HitBodyCenterDistance
    );

    sol::table SniperAmmoType = Lua.create_named_table("SniperAmmoType");
    SniperAmmoType["Normal"] = ESniperAmmoType::Normal;
    SniperAmmoType["AntiMaterial"] = ESniperAmmoType::AntiMaterial;

    sol::table SniperHitOutcome = Lua.create_named_table("SniperHitOutcome");
    SniperHitOutcome["Normal"] = ESniperHitOutcome::Normal;
    SniperHitOutcome["Blocked"] = ESniperHitOutcome::Blocked;
    SniperHitOutcome["Ricochet"] = ESniperHitOutcome::Ricochet;
    SniperHitOutcome["Penetrated"] = ESniperHitOutcome::Penetrated;

    sol::table SniperHitRegion = Lua.create_named_table("SniperHitRegion");
    SniperHitRegion["Unknown"] = ESniperHitRegion::Unknown;
    SniperHitRegion["Head"] = ESniperHitRegion::Head;
    SniperHitRegion["Torso"] = ESniperHitRegion::Torso;
    SniperHitRegion["Arm"] = ESniperHitRegion::Arm;
    SniperHitRegion["Leg"] = ESniperHitRegion::Leg;

    // 게임 특화 usertype/enum/global(GetGameState 등) 은 Game 모듈의
    // RegisterGameLuaBindings 가 등록한다. 호출 순서는 GameEngine/EditorEngine::Init
    // 에서 UEngine::Init() 직후.
}


