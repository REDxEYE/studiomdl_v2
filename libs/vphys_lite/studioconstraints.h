#pragma once

#include "mathlib/mathlib.h"

// StudioMDL only needs the serialized ragdoll constraint parameter layout.
// Keep these local so the compiler does not depend on vphysics_interface.h.
struct constraint_breakableparams_t
{
    float strength;
    float forceLimit;
    float torqueLimit;
    float bodyMassScale[2];
    bool isActive;

    void Defaults()
    {
        forceLimit = 0.0f;
        torqueLimit = 0.0f;
        strength = 1.0f;
        bodyMassScale[0] = bodyMassScale[1] = 1.0f;
        isActive = true;
    }
};

struct constraint_axislimit_t
{
    float minRotation;
    float maxRotation;
    float angularVelocity;
    float torque;

    void SetAxisFriction( float rmin, float rmax, float friction )
    {
        minRotation = rmin;
        maxRotation = rmax;
        angularVelocity = 0.0f;
        torque = friction;
    }
    void Defaults() { SetAxisFriction( 0.0f, 0.0f, 0.0f ); }
};

struct constraint_ragdollparams_t
{
    constraint_breakableparams_t constraint;
    matrix3x4_t constraintToReference;
    matrix3x4_t constraintToAttached;
    int parentIndex;
    int childIndex;
    constraint_axislimit_t axes[3];
    bool onlyAngularLimits;
    bool isActive;
    bool useClockwiseRotations;

    void Defaults()
    {
        constraint.Defaults();
        isActive = true;
        SetIdentityMatrix( constraintToReference );
        SetIdentityMatrix( constraintToAttached );
        parentIndex = -1;
        childIndex = -1;
        axes[0].Defaults(); axes[1].Defaults(); axes[2].Defaults();
        onlyAngularLimits = false;
        useClockwiseRotations = false;
    }
};
