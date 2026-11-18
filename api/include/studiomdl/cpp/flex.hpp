// studiomdl C++ wrapper: smdl_flex.h
#pragma once

#include <cstdint>
#include <string_view>

#include "common.hpp"
#include "shapes.hpp"
#include "../c/smdl_flex.h"

// MeshFlexApi is a base of smdl::Mesh (body.hpp): mesh() is its mesh, ownerModel() its model.
namespace smdl {

using EyeballId = uint32_t;  // per mesh, call order
using FlexKeyId = uint32_t;  // per mesh, call order (the key index of addFlex / addFlexPair)
using EyelidId = uint32_t;   // per mesh, call order (addEyelid; addDmxEyelid has its own numbering)

enum class BoneFlexComponent : int { TX = SMDL_BONE_FLEX_TX, TY = SMDL_BONE_FLEX_TY, TZ = SMDL_BONE_FLEX_TZ };
enum class EyelidKind : int { Upper = SMDL_EYELID_UPPER, Lower = SMDL_EYELID_LOWER };
enum class EyelidState : int { Lowerer = SMDL_EYELID_LOWERER, Neutral = SMDL_EYELID_NEUTRAL, Raiser = SMDL_EYELID_RAISER };

namespace detail {
    // A base of smdl::Model (model.hpp); model() is the model handle.
        class ModelFlexApi {
    public:
        // flexcontroller <group> range <min> <max> <name>
        FlexControllerId addFlexController(std::string_view group, std::string_view name, float min = 0.0f,
                                           float max = 1.0f);

        // Throws Error(SMDL_ERROR_NOT_FOUND) for an unknown name
        FlexControllerId findFlexController(std::string_view name) const;

        uint32_t flexControllerCount() const;

        // localvar: a flex without vertex data that rules can read
        FlexId addLocalVar(std::string_view name);

        FlexId findFlex(std::string_view name) const;

        uint32_t flexCount() const;

        // %<flex> = <expression>; throws Error(SMDL_ERROR_SYNTAX) / Error(SMDL_ERROR_NOT_FOUND), see smdl_flex.h
        void addFlexRule(std::string_view flex, std::string_view expression);

        // Same, the target flex by id (findFlex, Mesh::flexId / flexPairIds, addLocalVar)
        void addFlexRule(FlexId flex, std::string_view expression);

        // mouth <index> <flex> <bone> <x> <y> <z>
        void setMouth(uint32_t index, std::string_view flex, BoneId bone, const Vec3 &forward);

        // $boneflexdriver <bone> <tx|ty|tz> <controller> <min> <max>
        void addBoneFlexDriver(BoneId bone, BoneFlexComponent component, FlexControllerId controller, float min,
                               float max);

    private:
        smdl_model *model() const; // api/src/cpp_api.cpp
    };

        class MeshFlexApi {
    public:
        // eyeball <name> <bone> <x> <y> <z> <material> <diameter> <z angle> <iris material> <pupil scale>
        EyeballId addEyeball(std::string_view name, BoneId bone, const Vec3 &position, std::string_view material,
                             float diameter, float zAngleDegrees = 0.0f, float pupilScale = 1.0f) const;

        // flex <name> frame <shape> (ShapeId, or NoId for the base shape)
        FlexKeyId addFlex(std::string_view name, ShapeId shape) const;

        // flexpair <name> <split> frame <shape>
        FlexKeyId addFlexPair(std::string_view name, float split, ShapeId shape) const;

        void setFlexPosition(FlexKeyId key, float position) const;
        void setFlexDecay(FlexKeyId key, float decay) const;
        void setFlexSplit(FlexKeyId key, float split) const;

        // The flex of a `flex` key (throws for a pair: use flexPairIds)
        FlexId flexId(FlexKeyId key) const;

        // The {right, left} flexes of a `flexpair` key (<name>R, <name>L)
        std::pair<FlexId, FlexId> flexPairIds(FlexKeyId key) const;

        // defaultflex frame <shape>
        void setDefaultFlex(ShapeId shape) const;

        // eyelid ...: set the three states with setEyelidState
        EyelidId addEyelid(std::string_view name, EyelidKind kind) const;

        void setEyelidState(EyelidId eyelid, EyelidState state, ShapeId shape, float target) const;

        void setEyelidSplit(EyelidId eyelid, float split) const;

        // `eyeball <name>`: only this eyeball (NoId: every eyeball)
        void setEyelidEyeball(EyelidId eyelid, EyeballId eyeball) const;

        // dmxeyelid ...: set the three states with setDmxEyelidState
        uint32_t addDmxEyelid(EyelidKind kind, EyeballId rightEyeball, EyeballId leftEyeball) const;

        void setDmxEyelidState(uint32_t dmxEyelid, EyelidState state, ShapeId shape, float target) const;

        // spherenormals <material> <x> <y> <z>
        void addSphereNormals(std::string_view material, const Vec3 &position) const;

        // noautodmxrules
        void setNoAutoDmxRules(bool enabled = true) const;

    private:
        smdl_mesh *mesh() const; // api/src/cpp_api.cpp
        smdl_model *ownerModel() const; // api/src/cpp_api.cpp
    };
}

} // namespace smdl
