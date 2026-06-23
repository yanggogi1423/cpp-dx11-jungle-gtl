#include "Physics/PhysXConstraintBuilder.h"
#include "Physics/PhysXConversion.h"

#include <PxPhysicsAPI.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float MinAngularLimitWidthRadians = 1.0e-4f;
    constexpr float HalfMinAngularLimitWidthRadians = MinAngularLimitWidthRadians * 0.5f;

    float DegreesToRadians(float Degrees)
    {
        return Degrees * Pi / 180.0f;
    }

    float FiniteRadiansOr(float Degrees, float FallbackRadians)
    {
        const float Radians = DegreesToRadians(Degrees);
        return std::isfinite(Radians) ? Radians : FallbackRadians;
    }

    physx::PxJointAngularLimitPair MakeTwistLimitPair(const FConstraintLimitDesc& Limits)
    {
        float Lower = FiniteRadiansOr(Limits.TwistLimitMinDegrees, -HalfMinAngularLimitWidthRadians);
        float Upper = FiniteRadiansOr(Limits.TwistLimitMaxDegrees, HalfMinAngularLimitWidthRadians);

        if (Upper < Lower)
        {
            std::swap(Lower, Upper);
        }

        // PhysX D6 twist limits must have a non-zero angular span. Treat a
        // zero-width authored limit as an effectively locked twist axis.
        if (Upper - Lower < MinAngularLimitWidthRadians)
        {
            const float Center = (Lower + Upper) * 0.5f;
            Lower = Center - HalfMinAngularLimitWidthRadians;
            Upper = Center + HalfMinAngularLimitWidthRadians;
        }

        return physx::PxJointAngularLimitPair(Lower, Upper);
    }

    float MakeConeLimitRadians(float Degrees)
    {
        const float Radians = FiniteRadiansOr(Degrees, MinAngularLimitWidthRadians);
        return (std::max)(Radians, MinAngularLimitWidthRadians);
    }

    physx::PxD6Motion::Enum ToPxD6Motion(EConstraintMotion Motion)
    {
        switch (Motion)
        {
        case EConstraintMotion::Free:
            return physx::PxD6Motion::eFREE;
        case EConstraintMotion::Limited:
            return physx::PxD6Motion::eLIMITED;
        case EConstraintMotion::Locked:
        default:
            return physx::PxD6Motion::eLOCKED;
        }
    }
}

physx::PxJoint* FPhysXConstraintBuilder::CreateD6Joint(
    physx::PxPhysics* Physics,
    physx::PxRigidActor* ParentActor,
    physx::PxRigidActor* ChildActor,
    const FConstraintCreationDesc& Desc
)
{
    if (!Physics || !ParentActor || !ChildActor)
    {
        return nullptr;
    }

    physx::PxD6Joint* Joint = physx::PxD6JointCreate(
        *Physics,
        ParentActor,
        ToPxTransform(Desc.ParentLocalFrame),
        ChildActor,
        ToPxTransform(Desc.ChildLocalFrame)
    );

    if (!Joint)
    {
        return nullptr;
    }

    const FConstraintLimitDesc& L = Desc.Limits;

    Joint->setMotion(physx::PxD6Axis::eX, ToPxD6Motion(L.LinearX));
    Joint->setMotion(physx::PxD6Axis::eY, ToPxD6Motion(L.LinearY));
    Joint->setMotion(physx::PxD6Axis::eZ, ToPxD6Motion(L.LinearZ));

    Joint->setMotion(physx::PxD6Axis::eTWIST, ToPxD6Motion(L.Twist));
    Joint->setMotion(physx::PxD6Axis::eSWING1, ToPxD6Motion(L.Swing1));
    Joint->setMotion(physx::PxD6Axis::eSWING2, ToPxD6Motion(L.Swing2));

    Joint->setTwistLimit(MakeTwistLimitPair(L));

    Joint->setSwingLimit(
        physx::PxJointLimitCone(
            MakeConeLimitRadians(L.Swing1LimitDegrees),
            MakeConeLimitRadians(L.Swing2LimitDegrees)
        )
    );

    Joint->setConstraintFlag(
        physx::PxConstraintFlag::eCOLLISION_ENABLED,
        !Desc.bDisableCollisionBetweenBodies
    );

    Joint->setConstraintFlag(
        physx::PxConstraintFlag::ePROJECTION,
        L.bEnableProjection
    );

    return Joint;
}
