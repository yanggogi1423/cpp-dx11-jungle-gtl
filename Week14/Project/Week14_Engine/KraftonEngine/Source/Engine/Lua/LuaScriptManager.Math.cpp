#include "LuaScriptManager.h"

#include "Math/Vector.h"

#include <algorithm>

void FLuaScriptManager::RegisterMathBindings(sol::state& Lua)
{
    Lua.new_usertype<FVector>(
        "Vector",
        sol::constructors<FVector(), FVector(float, float, float)>(),
        "X",
        &FVector::X,
        "Y",
        &FVector::Y,
        "Z",
        &FVector::Z,
        "Length",
        &FVector::Length,
        "Normalize",
        &FVector::Normalize,
        "Normalized",
        &FVector::Normalized,
        "Dot",
        &FVector::Dot,
        "Cross",
        sol::overload(
            static_cast<FVector(FVector::*)(const FVector&) const>(&FVector::Cross),
            static_cast<FVector(*)(const FVector&, const FVector&)>(&FVector::Cross)
        ),
        "Distance",
        &FVector::Distance,
        "DistSquared",
        &FVector::DistSquared,
        "Lerp",
        &FVector::Lerp,
        sol::meta_function::addition,
        sol::overload(
            static_cast<FVector(FVector::*)(const FVector&) const>(&FVector::operator+),
            static_cast<FVector(FVector::*)(float) const>(&FVector::operator+)
        ),
        sol::meta_function::subtraction,
        sol::overload(
            static_cast<FVector(FVector::*)(const FVector&) const>(&FVector::operator-),
            static_cast<FVector(FVector::*)(float) const>(&FVector::operator-)
        ),
        sol::meta_function::multiplication,
        static_cast<FVector(FVector::*)(float) const>(&FVector::operator*),
        sol::meta_function::division,
        &FVector::operator/,
        "Zero",
        []()
        {
            return FVector::ZeroVector;
        },
        "One",
        []()
        {
            return FVector::OneVector;
        },
        "Up",
        []()
        {
            return FVector::UpVector;
        },
        "Down",
        []()
        {
            return FVector::DownVector;
        },
        "Forward",
        []()
        {
            return FVector::ForwardVector;
        },
        "Backward",
        []()
        {
            return FVector::BackwardVector;
        },
        "Right",
        []()
        {
            return FVector::RightVector;
        },
        "Left",
        []()
        {
            return FVector::LeftVector;
        },
        "XAxis",
        []()
        {
            return FVector::XAxisVector;
        },
        "YAxis",
        []()
        {
            return FVector::YAxisVector;
        },
        "ZAxis",
        []()
        {
            return FVector::ZAxisVector;
        }
    );

    Lua.set_function(
        "Vec3",
        [](sol::optional<float> X, sol::optional<float> Y, sol::optional<float> Z)
        {
            return FVector(X.value_or(0.0f), Y.value_or(0.0f), Z.value_or(0.0f));
        }
    );

    sol::table Math = Lua.create_named_table("Math");
    Math.set_function(
        "Vector",
        [](sol::optional<float> X, sol::optional<float> Y, sol::optional<float> Z)
        {
            return FVector(X.value_or(0.0f), Y.value_or(0.0f), Z.value_or(0.0f));
        }
    );
    Math.set_function(
        "Clamp",
        [](float Value, float Min, float Max)
        {
            return std::clamp(Value, Min, Max);
        }
    );
    Math.set_function(
        "Lerp",
        [](float A, float B, float Alpha)
        {
            return A + (B - A) * Alpha;
        }
    );
    Math.set_function(
        "Distance",
        [](const FVector& A, const FVector& B)
        {
            return (A - B).Length();
        }
    );
    Math.set_function(
        "Normalize",
        [](const FVector& V)
        {
            return V.Length() > 0.000001f ? V.Normalized() : FVector::ZeroVector;
        }
    );
    Math.set_function(
        "Dot",
        [](const FVector& A, const FVector& B)
        {
            return A.Dot(B);
        }
    );
    Math.set_function(
        "Cross",
        [](const FVector& A, const FVector& B)
        {
            return A.Cross(B);
        }
    );
}
