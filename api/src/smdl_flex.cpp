// smdl_flex.h: flex controllers, flexes, rules, local vars, eyeballs, eyelids, mouths (store and validate at call time;
// the compile applies them in ApplyMeshFlex / ApplyFlex, apply_flex.cpp)
#include "studiomdl/c/smdl_flex.h"
#include "studiomdl/studiomdl.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

#include "common/scriplib.h"
#include "core_util.hpp"
#include "flex_rule_check.hpp"
#include "studiomdl/apply_flex.hpp"
#include "studio.h"
#include "studiomdl/studiomdl_constants.hpp"

using namespace smdl_api;

// ---------------------------------------------------------------------------------------------------------------
// FlexDesc
// ---------------------------------------------------------------------------------------------------------------
int FlexDesc::findFlex(std::string_view name) const {
    for (size_t i = 0; i < flexNames.size(); ++i) {
        if (IEquals(flexNames[i], name))
            return static_cast<int>(i);
    }
    return -1;
}

int FlexDesc::findController(std::string_view name) const {
    for (size_t i = 0; i < controllers.size(); ++i) {
        if (IEquals(controllers[i].name, name))
            return static_cast<int>(i);
    }
    return -1;
}

uint32_t FlexDesc::defineFlex(std::string_view name) {
    if (const int existing = findFlex(name); existing >= 0)
        return static_cast<uint32_t>(existing);
    flexNames.emplace_back(name);
    return static_cast<uint32_t>(flexNames.size() - 1);
}

namespace {
    constexpr uint32_t kMaxMouths = 64;

    smdl_status CheckName(const smdl_model &model, const char *name, const char *what) {
        if (!ValidName(name))
            return Fail(&model, SMDL_ERROR_INVALID_ARGUMENT, "{} is null or empty", what);
        if (std::strlen(name) >= MAXSTUDIONAME)
            return Fail(&model, SMDL_ERROR_LIMIT, "{} '{}' is too long (at most {} characters)", what, name,
                        MAXSTUDIONAME - 1);
        return SMDL_OK;
    }

    // Defines every name (the model's flex list) unless that would pass the limit; nothing is defined on failure
    smdl_status DefineFlexNames(smdl_model &model, std::initializer_list<std::string> names, uint32_t *firstId = nullptr) {
        FlexDesc &flex = model.flex;
        size_t added = 0;
        for (const std::string &name: names) {
            if (flex.findFlex(name) < 0)
                ++added;
        }
        if (flex.flexNames.size() + added > MAXSTUDIOFLEXDESC)
            return Fail(&model, SMDL_ERROR_LIMIT, "too many flexes (at most {})", MAXSTUDIOFLEXDESC);
        bool first = true;
        for (const std::string &name: names) {
            const uint32_t id = flex.defineFlex(name);
            if (first && firstId)
                *firstId = id;
            first = false;
        }
        return SMDL_OK;
    }

    MeshFlexDesc &MeshOf(smdl_mesh &mesh) { return mesh.model->flex.meshes[&mesh]; }

    // Flex keys the mesh items will add (the compile checks the exact limit again)
    smdl_status CheckKeyRoom(const smdl_model &model, size_t newKeys) {
        size_t total = newKeys;
        for (const auto &[mesh, desc]: model.flex.meshes) {
            total += desc.flexKeys.size() + 3 * desc.eyelids.size() + 3 * desc.dmxEyelids.size();
        }
        if (total > MAXSTUDIOFLEXKEYS)
            return Fail(&model, SMDL_ERROR_LIMIT, "too many flex keys (at most {})", MAXSTUDIOFLEXKEYS);
        return SMDL_OK;
    }

    // A shape index of the mesh's geometry; SMDL_NO_ID (the base shape) when allowed
    smdl_status CheckShape(const smdl_mesh &mesh, uint32_t shape, bool allowBase, const char *what) {
        if (shape == SMDL_NO_ID && allowBase)
            return SMDL_OK;
        const size_t count = mesh.geometry->shapes.size();
        if (shape >= count)
            return Fail(mesh.model, SMDL_ERROR_OUT_OF_RANGE, "mesh '{}': {}: shape {} does not exist ({} shapes)",
                        mesh.name, what, shape, count);
        return SMDL_OK;
    }

    smdl_status CheckTarget(const smdl_mesh &mesh, float value, const char *what) {
        if (!std::isfinite(value))
            return Fail(mesh.model, SMDL_ERROR_INVALID_ARGUMENT, "mesh '{}': {} is not finite", mesh.name, what);
        return SMDL_OK;
    }

    bool OwnedMesh(const smdl_mesh *mesh) { return mesh && mesh->model && mesh->geometry; }

    template<class T>
    T *At(std::vector<T> &items, uint32_t index) {
        return index < items.size() ? &items[index] : nullptr;
    }

    // Flex keys of addFlex / addFlexPair; the body of a RunBody
    smdl_status AddFlexKey(smdl_mesh *mesh, const char *name, bool pair, float split, uint32_t shape,
                           uint32_t *out_flex_key) {
        smdl_model *model = mesh->model;
        if (const auto status = CheckName(*model, name, "flex name"))
            return status;
        if (pair && (!std::isfinite(split) || split == 0.0f))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT,
                        "mesh '{}': flex pair '{}': the split must be a non-zero number", mesh->name, name);
        if (const auto status = CheckShape(*mesh, shape, true, name))
            return status;
        if (const auto status = CheckKeyRoom(*model, 1))
            return status;
        const std::string base = name;
        const auto status = pair ? DefineFlexNames(*model, {base + "R", base + "L"})
                                 : DefineFlexNames(*model, {base});
        if (status)
            return status;
        MeshFlexDesc &desc = MeshOf(*mesh);
        desc.flexKeys.push_back({base, pair, pair ? split : 0.0f, shape, 1.0f, 1.0f});
        desc.items.push_back({MeshFlexItem::Kind::Flex, static_cast<uint32_t>(desc.flexKeys.size() - 1)});
        if (out_flex_key)
            *out_flex_key = static_cast<uint32_t>(desc.flexKeys.size() - 1);
        return Ok(model);
    }

    // The body of setFlexPosition / setFlexDecay / setFlexSplit
    template<class F>
    smdl_status SetFlexKey(smdl_mesh *mesh, uint32_t flexKey, float value, const char *what, F &&assign) {
        smdl_model *model = mesh->model;
        FlexKeyDesc *key = At(MeshOf(*mesh).flexKeys, flexKey);
        if (!key)
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "mesh '{}': flex key {} does not exist", mesh->name, flexKey);
        if (!std::isfinite(value))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "mesh '{}': flex '{}': {} is not finite", mesh->name,
                        key->name, what);
        assign(*key, value);
        return Ok(model);
    }

    template<class Desc>
    smdl_status SetLidState(smdl_mesh *mesh, std::vector<Desc> &lids, uint32_t lid, smdl_eyelid_state state,
                            uint32_t shape, float target, bool allowBase, const char *what) {
        smdl_model *model = mesh->model;
        Desc *desc = At(lids, lid);
        if (!desc)
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "mesh '{}': {} {} does not exist", mesh->name, what, lid);
        if (state < SMDL_EYELID_LOWERER || state > SMDL_EYELID_RAISER)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "mesh '{}': {}: state {} is not lowerer, neutral or raiser",
                        mesh->name, what, static_cast<int>(state));
        if (const auto status = CheckShape(*mesh, shape, allowBase, what))
            return status;
        if (const auto status = CheckTarget(*mesh, target, "eyelid target"))
            return status;
        desc->set[state] = true;
        desc->shape[state] = shape;
        desc->target[state] = target;
        return Ok(model);
    }

    // The ids of a flex key's flex(es); flexId / flexPairIds and the C function check which kind it is
    void GetFlexIds(const smdl_mesh *mesh, uint32_t flex_key, smdl_flex_id *out_flex, smdl_flex_id *out_left) {
        const smdl_model *model = mesh->model;
        RunBody(model, [&] {
            const auto it = model->flex.meshes.find(mesh);
            if (it == model->flex.meshes.end() || flex_key >= it->second.flexKeys.size())
                return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "mesh '{}': flex key {} does not exist", mesh->name, flex_key);
            const FlexKeyDesc &key = it->second.flexKeys[flex_key];
            // the names were defined when the key was added, so the lookups can't fail
            const int first = model->flex.findFlex(key.pair ? key.name + "R" : key.name);
            if (out_flex)
                *out_flex = static_cast<smdl_flex_id>(first);
            if (key.pair && out_left)
                *out_left = static_cast<smdl_flex_id>(model->flex.findFlex(key.name + "L"));
            return Ok(model);
        });
    }
}

namespace smdl {
namespace detail {

// ---------------------------------------------------------------------------------------------------------------
// Model-wide
// ---------------------------------------------------------------------------------------------------------------
FlexControllerId ModelFlexApi::addFlexController(std::string_view groupView, std::string_view nameView, float min,
                                                 float max) {
    const std::string groupStorage(groupView);
    const char *group = groupStorage.c_str();
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_flex_controller_id result = SMDL_NO_ID;
    smdl_flex_controller_id *out_id = &result;
    RunBody(model, [&] {
        if (const auto status = CheckName(*model, name, "flex controller name"))
            return status;
        if (const auto status = CheckName(*model, group, "flex controller group"))
            return status;
        if (!std::isfinite(min) || !std::isfinite(max))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "flex controller '{}': range is not finite", name);
        FlexDesc &flex = model->flex;
        if (flex.findController(name) >= 0)
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "flex controller '{}' exists already", name);
        if (flex.controllers.size() >= MAXSTUDIOFLEXCTRL)
            return Fail(model, SMDL_ERROR_LIMIT, "too many flex controllers (at most {})", MAXSTUDIOFLEXCTRL);
        flex.controllers.push_back({group, name, min, max});
        if (out_id)
            *out_id = static_cast<uint32_t>(flex.controllers.size() - 1);
        return Ok(model);
    });
    return result;
}

FlexControllerId ModelFlexApi::findFlexController(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_flex_controller_id result = SMDL_NO_ID;
    smdl_flex_controller_id *out_id = &result;
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "flex controller name is null or empty");
        const int index = model->flex.findController(name);
        if (index < 0)
            return Fail(model, SMDL_ERROR_NOT_FOUND, "no flex controller named '{}'", name);
        if (out_id)
            *out_id = static_cast<uint32_t>(index);
        return Ok(model);
    });
    return result;
}

uint32_t ModelFlexApi::flexControllerCount() const { return static_cast<uint32_t>(model()->flex.controllers.size()); }

FlexId ModelFlexApi::addLocalVar(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_flex_id result = SMDL_NO_ID;
    smdl_flex_id *out_id = &result;
    RunBody(model, [&] {
        if (const auto status = CheckName(*model, name, "local variable name"))
            return status;
        uint32_t id = SMDL_NO_ID;
        if (const auto status = DefineFlexNames(*model, {name}, &id))
            return status;
        model->flex.localVars.emplace_back(name);
        if (out_id)
            *out_id = id;
        return Ok(model);
    });
    return result;
}

FlexId ModelFlexApi::findFlex(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_flex_id result = SMDL_NO_ID;
    smdl_flex_id *out_id = &result;
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "flex name is null or empty");
        const int index = model->flex.findFlex(name);
        if (index < 0)
            return Fail(model, SMDL_ERROR_NOT_FOUND, "no flex named '{}'", name);
        if (out_id)
            *out_id = static_cast<uint32_t>(index);
        return Ok(model);
    });
    return result;
}

uint32_t ModelFlexApi::flexCount() const { return static_cast<uint32_t>(model()->flex.flexNames.size()); }

void ModelFlexApi::addFlexRule(std::string_view flexView, std::string_view expressionView) {
    const std::string flexStorage(flexView);
    const char *flex = flexStorage.c_str();
    const std::string expressionStorage(expressionView);
    const char *expression = expressionStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (!ValidName(flex))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "flex rule: flex name is null or empty");
        if (!expression)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "flex rule for '{}': expression is null", flex);
        FlexDesc &desc = model->flex;
        const int flexIndex = desc.findFlex(flex);
        if (flexIndex < 0)
            return Fail(model, SMDL_ERROR_NOT_FOUND, "flex rule: no flex named '{}'", flex);
        for (const FlexRuleDesc &rule: desc.rules) {
            if (IEquals(rule.flex, flex))
                return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "flex '{}' has a rule already", flex);
        }
        if (desc.rules.size() >= MAXSTUDIOFLEXRULES)
            return Fail(model, SMDL_ERROR_LIMIT, "too many flex rules (at most {})", MAXSTUDIOFLEXRULES);

        const FlexRuleCheck check = CheckFlexRule(desc, desc.flexNames[flexIndex], flexIndex, expression);
        if (check.status != SMDL_OK)
            return Fail(model, check.status, "{}", check.message);
        desc.rules.push_back({desc.flexNames[flexIndex], expression});
        return Ok(model);
    });
}

void ModelFlexApi::addFlexRule(FlexId flex, std::string_view expression) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (flex >= model->flex.flexNames.size())
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "flex rule: flex {} does not exist ({} flexes)", flex,
                        model->flex.flexNames.size());
        return Ok(model);
    });
    const std::string name = model->flex.flexNames[flex]; // copied: the rule call may grow flexNames
    addFlexRule(std::string_view(name), expression);
}

void ModelFlexApi::setMouth(uint32_t index, std::string_view flexView, BoneId bone, const Vec3 &forwardVec) {
    const std::string flexStorage(flexView);
    const char *flex = flexStorage.c_str();
    const float *forward = forwardVec.data();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (index >= kMaxMouths)
            return Fail(model, SMDL_ERROR_LIMIT, "mouth index {} is not valid (at most {})", index, kMaxMouths - 1);
        if (const auto status = CheckName(*model, flex, "mouth flex name"))
            return status;
        if (!model->skeleton.valid(bone))
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "mouth {}: bone id {} is not a bone of the model ({} bones)",
                        index, bone, model->skeleton.bones.size());
        if (!forward || !AllFinite(forward, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "mouth {}: forward vector is null or not finite", index);
        if (const auto status = DefineFlexNames(*model, {flex}))
            return status;
        MouthDesc mouth{index, flex, bone, ToVector(forward)};
        for (MouthDesc &existing: model->flex.mouths) {
            if (existing.index == index) {
                existing = std::move(mouth);
                return Ok(model);
            }
        }
        model->flex.mouths.push_back(std::move(mouth));
        return Ok(model);
    });
}

void ModelFlexApi::addBoneFlexDriver(BoneId bone, BoneFlexComponent componentEnum, FlexControllerId controller,
                                     float min, float max) {
    const smdl_bone_flex_component component = static_cast<smdl_bone_flex_component>(componentEnum);
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (!model->skeleton.valid(bone))
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE,
                        "bone flex driver: bone id {} is not a bone of the model ({} bones)", bone,
                        model->skeleton.bones.size());
        if (component < SMDL_BONE_FLEX_TX || component > SMDL_BONE_FLEX_TZ)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bone flex driver: component {} is not tx, ty or tz",
                        static_cast<int>(component));
        if (controller >= model->flex.controllers.size())
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE,
                        "bone flex driver: flex controller id {} does not exist ({} controllers)", controller,
                        model->flex.controllers.size());
        if (!std::isfinite(min) || !std::isfinite(max))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bone flex driver: range is not finite");
        BoneFlexDriverDesc driver{bone, static_cast<int>(component), controller, min, max};
        for (BoneFlexDriverDesc &existing: model->flex.boneDrivers) {
            if (existing.bone == bone && existing.controller == controller) {
                existing = driver;
                return Ok(model);
            }
        }
        model->flex.boneDrivers.push_back(driver);
        return Ok(model);
    });
}

// ---------------------------------------------------------------------------------------------------------------
// Per mesh
// ---------------------------------------------------------------------------------------------------------------
EyeballId MeshFlexApi::addEyeball(std::string_view nameView, BoneId bone, const Vec3 &positionVec,
                                  std::string_view materialView, float diameter, float zAngleDegrees,
                                  float pupilScale) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    const std::string materialStorage(materialView);
    const char *material = materialStorage.c_str();
    const float *position = positionVec.data();
    const float z_angle_degrees = zAngleDegrees;
    const float pupil_scale = pupilScale;
    smdl_mesh *mesh = this->mesh();
    smdl_model *model = mesh->model;
    uint32_t result = SMDL_NO_ID;
    uint32_t *out_eyeball = &result;
    RunBody(model, [&] {
        if (const auto status = CheckName(*model, name, "eyeball name"))
            return status;
        if (!ValidName(material))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "mesh '{}': eyeball '{}': material is null or empty",
                        mesh->name, name);
        if (!model->skeleton.valid(bone))
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE,
                        "mesh '{}': eyeball '{}': bone id {} is not a bone of the model ({} bones)", mesh->name, name,
                        bone, model->skeleton.bones.size());
        if (!position || !AllFinite(position, 3) || !std::isfinite(diameter) || !std::isfinite(z_angle_degrees) ||
            !std::isfinite(pupil_scale))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "mesh '{}': eyeball '{}': a value is null or not finite",
                        mesh->name, name);
        if (pupil_scale == 0.0f)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "mesh '{}': eyeball '{}': pupil scale is 0", mesh->name,
                        name);
        MeshFlexDesc &desc = MeshOf(*mesh);
        if (desc.eyeballs.size() >= 4)
            return Fail(model, SMDL_ERROR_LIMIT, "mesh '{}': too many eyeballs (at most 4)", mesh->name);
        desc.eyeballs.push_back({name, bone, ToVector(position), material, diameter, z_angle_degrees, pupil_scale});
        desc.items.push_back({MeshFlexItem::Kind::Eyeball, static_cast<uint32_t>(desc.eyeballs.size() - 1)});
        if (out_eyeball)
            *out_eyeball = static_cast<uint32_t>(desc.eyeballs.size() - 1);
        return Ok(model);
    });
    return result;
}

FlexKeyId MeshFlexApi::addFlex(std::string_view nameView, ShapeId shape) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_mesh *mesh = this->mesh();
    uint32_t result = SMDL_NO_ID;
    RunBody(mesh->model, [&] { return AddFlexKey(mesh, name, false, 0.0f, shape, &result); });
    return result;
}

FlexKeyId MeshFlexApi::addFlexPair(std::string_view nameView, float split, ShapeId shape) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_mesh *mesh = this->mesh();
    uint32_t result = SMDL_NO_ID;
    RunBody(mesh->model, [&] { return AddFlexKey(mesh, name, true, split, shape, &result); });
    return result;
}

void MeshFlexApi::setFlexPosition(FlexKeyId flex_key, float position) const {
    smdl_mesh *mesh = this->mesh();
    RunBody(mesh->model, [&] {
        return SetFlexKey(mesh, flex_key, position, "position", [](FlexKeyDesc &key, float v) { key.position = v; });
    });
}

void MeshFlexApi::setFlexDecay(FlexKeyId flex_key, float decay) const {
    smdl_mesh *mesh = this->mesh();
    RunBody(mesh->model, [&] {
        return SetFlexKey(mesh, flex_key, decay, "decay", [](FlexKeyDesc &key, float v) { key.decay = v; });
    });
}

void MeshFlexApi::setFlexSplit(FlexKeyId flex_key, float split) const {
    smdl_mesh *mesh = this->mesh();
    RunBody(mesh->model, [&] {
        return SetFlexKey(mesh, flex_key, split, "split", [](FlexKeyDesc &key, float v) { key.split = v; });
    });
}

FlexId MeshFlexApi::flexId(FlexKeyId key) const {
    FlexId flex = NoId, left = NoId;
    GetFlexIds(mesh(), key, &flex, &left);
    if (left != NoId)
        throw Error(SMDL_ERROR_INVALID_ARGUMENT, "flex key is a flexpair: use flexPairIds");
    return flex;
}

std::pair<FlexId, FlexId> MeshFlexApi::flexPairIds(FlexKeyId key) const {
    FlexId right = NoId, left = NoId;
    GetFlexIds(mesh(), key, &right, &left);
    if (left == NoId)
        throw Error(SMDL_ERROR_INVALID_ARGUMENT, "flex key is not a flexpair: use flexId");
    return {right, left};
}

void MeshFlexApi::setDefaultFlex(ShapeId shape) const {
    smdl_mesh *mesh = this->mesh();
    smdl_model *model = mesh->model;
    RunBody(model, [&] {
        if (const auto status = CheckShape(*mesh, shape, true, "default flex"))
            return status;
        if (model->flex.hasDefaultFlex)
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "the model has a default flex already");
        if (const auto status = CheckKeyRoom(*model, 1))
            return status;
        if (const auto status = DefineFlexNames(*model, {"default"}))
            return status;
        MeshFlexDesc &desc = MeshOf(*mesh);
        desc.hasDefaultFlex = true;
        desc.defaultShape = shape;
        model->flex.hasDefaultFlex = true;
        desc.items.push_back({MeshFlexItem::Kind::DefaultFlex, 0});
        return Ok(model);
    });
}

EyelidId MeshFlexApi::addEyelid(std::string_view nameView, EyelidKind kindEnum) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    const smdl_eyelid_kind kind = static_cast<smdl_eyelid_kind>(kindEnum);
    smdl_mesh *mesh = this->mesh();
    smdl_model *model = mesh->model;
    uint32_t result = SMDL_NO_ID;
    uint32_t *out_eyelid = &result;
    RunBody(model, [&] {
        // the longest derived name is "<name>_neutral" (8 characters more)
        if (const auto status = CheckName(*model, name, "eyelid name"))
            return status;
        if (std::strlen(name) + 8 >= MAXSTUDIONAME)
            return Fail(model, SMDL_ERROR_LIMIT, "eyelid name '{}' is too long (at most {} characters)", name,
                        MAXSTUDIONAME - 9);
        if (kind != SMDL_EYELID_UPPER && kind != SMDL_EYELID_LOWER)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "eyelid '{}': kind {} is not upper or lower", name,
                        static_cast<int>(kind));
        if (const auto status = CheckKeyRoom(*model, 3))
            return status;
        const std::string base = name;
        if (const auto status = DefineFlexNames(*model, {base, base + "_lowerer", base + "_neutral", base + "_raiser"}))
            return status;
        MeshFlexDesc &desc = MeshOf(*mesh);
        EyelidDesc eyelid;
        eyelid.name = base;
        eyelid.kind = static_cast<int>(kind);
        desc.eyelids.push_back(std::move(eyelid));
        desc.items.push_back({MeshFlexItem::Kind::Eyelid, static_cast<uint32_t>(desc.eyelids.size() - 1)});
        if (out_eyelid)
            *out_eyelid = static_cast<uint32_t>(desc.eyelids.size() - 1);
        return Ok(model);
    });
    return result;
}

void MeshFlexApi::setEyelidState(EyelidId eyelid, EyelidState stateEnum, ShapeId shape, float target) const {
    const smdl_eyelid_state state = static_cast<smdl_eyelid_state>(stateEnum);
    smdl_mesh *mesh = this->mesh();
    RunBody(mesh->model, [&] {
        return SetLidState(mesh, MeshOf(*mesh).eyelids, eyelid, state, shape, target, true, "eyelid");
    });
}

void MeshFlexApi::setEyelidSplit(EyelidId eyelid, float split) const {
    smdl_mesh *mesh = this->mesh();
    smdl_model *model = mesh->model;
    RunBody(model, [&] {
        EyelidDesc *desc = At(MeshOf(*mesh).eyelids, eyelid);
        if (!desc)
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "mesh '{}': eyelid {} does not exist", mesh->name, eyelid);
        if (const auto status = CheckTarget(*mesh, split, "eyelid split"))
            return status;
        desc->split = split;
        return Ok(model);
    });
}

void MeshFlexApi::setEyelidEyeball(EyelidId eyelid, EyeballId eyeball) const {
    smdl_mesh *mesh = this->mesh();
    smdl_model *model = mesh->model;
    RunBody(model, [&] {
        MeshFlexDesc &mesh_desc = MeshOf(*mesh);
        EyelidDesc *desc = At(mesh_desc.eyelids, eyelid);
        if (!desc)
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "mesh '{}': eyelid {} does not exist", mesh->name, eyelid);
        if (eyeball != SMDL_NO_ID && eyeball >= mesh_desc.eyeballs.size())
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "mesh '{}': eyeball {} does not exist ({} eyeballs)",
                        mesh->name, eyeball, mesh_desc.eyeballs.size());
        desc->eyeball = eyeball;
        return Ok(model);
    });
}

uint32_t MeshFlexApi::addDmxEyelid(EyelidKind kindEnum, EyeballId right_eyeball, EyeballId left_eyeball) const {
    const smdl_eyelid_kind kind = static_cast<smdl_eyelid_kind>(kindEnum);
    smdl_mesh *mesh = this->mesh();
    smdl_model *model = mesh->model;
    uint32_t result = SMDL_NO_ID;
    uint32_t *out_dmx_eyelid = &result;
    RunBody(model, [&] {
        if (kind != SMDL_EYELID_UPPER && kind != SMDL_EYELID_LOWER)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "dmx eyelid: kind {} is not upper or lower",
                        static_cast<int>(kind));
        MeshFlexDesc &desc = MeshOf(*mesh);
        for (const uint32_t eyeball: {right_eyeball, left_eyeball}) {
            if (eyeball >= desc.eyeballs.size())
                return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "mesh '{}': dmx eyelid: eyeball {} does not exist ({} eyeballs)",
                            mesh->name, eyeball, desc.eyeballs.size());
        }
        if (right_eyeball == left_eyeball)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "mesh '{}': dmx eyelid: the right and the left eyeball are the same",
                        mesh->name);
        if (const auto status = CheckKeyRoom(*model, 3))
            return status;
        const std::string type = kind == SMDL_EYELID_UPPER ? "upper" : "lower";
        const std::string right = type + "_right";
        const std::string left = type + "_left";
        if (const auto status = DefineFlexNames(*model, {right, right + "_lowerer", right + "_neutral",
                                                         right + "_raiser", left, left + "_lowerer",
                                                         left + "_neutral", left + "_raiser"}))
            return status;
        DmxEyelidDesc eyelid;
        eyelid.kind = static_cast<int>(kind);
        eyelid.rightEyeball = right_eyeball;
        eyelid.leftEyeball = left_eyeball;
        desc.dmxEyelids.push_back(eyelid);
        desc.items.push_back({MeshFlexItem::Kind::DmxEyelid, static_cast<uint32_t>(desc.dmxEyelids.size() - 1)});
        if (out_dmx_eyelid)
            *out_dmx_eyelid = static_cast<uint32_t>(desc.dmxEyelids.size() - 1);
        return Ok(model);
    });
    return result;
}

void MeshFlexApi::setDmxEyelidState(uint32_t dmx_eyelid, EyelidState stateEnum, ShapeId shape, float target) const {
    const smdl_eyelid_state state = static_cast<smdl_eyelid_state>(stateEnum);
    smdl_mesh *mesh = this->mesh();
    RunBody(mesh->model, [&] {
        return SetLidState(mesh, MeshOf(*mesh).dmxEyelids, dmx_eyelid, state, shape, target, false, "dmx eyelid");
    });
}

void MeshFlexApi::addSphereNormals(std::string_view materialView, const Vec3 &positionVec) const {
    const std::string materialStorage(materialView);
    const char *material = materialStorage.c_str();
    const float *position = positionVec.data();
    smdl_mesh *mesh = this->mesh();
    smdl_model *model = mesh->model;
    RunBody(model, [&] {
        if (!ValidName(material))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "mesh '{}': sphere normals: material is null or empty",
                        mesh->name);
        if (!position || !AllFinite(position, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "mesh '{}': sphere normals: position is null or not finite",
                        mesh->name);
        MeshFlexDesc &desc = MeshOf(*mesh);
        desc.sphereNormals.push_back({material, ToVector(position)});
        desc.items.push_back({MeshFlexItem::Kind::SphereNormals, static_cast<uint32_t>(desc.sphereNormals.size() - 1)});
        return Ok(model);
    });
}

void MeshFlexApi::setNoAutoDmxRules(bool enabled) const {
    smdl_mesh *mesh = this->mesh();
    smdl_model *model = mesh->model;
    RunBody(model, [&] {
        MeshFlexDesc &desc = MeshOf(*mesh);
        // a call keeps its place among the mesh's items: the first enabling adds the item
        if (enabled && !desc.noAutoDmxRules)
            desc.items.push_back({MeshFlexItem::Kind::NoAutoDmxRules, 0});
        else if (!enabled && desc.noAutoDmxRules)
            std::erase_if(desc.items, [](const MeshFlexItem &item) { return item.kind == MeshFlexItem::Kind::NoAutoDmxRules; });
        desc.noAutoDmxRules = enabled != 0;
        return Ok(model);
    });
}

} // namespace detail
} // namespace smdl

// The C API: argument checks that need C (null pointers, out-parameters), then the C++ method. A null string is passed
// as "" (rejected like null), a null vector as NaNs (rejected like null, after the checks that precede it).
namespace {
    smdl::Vec3 VecArg(const float *v) {
        constexpr float nan = std::numeric_limits<float>::quiet_NaN();
        return v ? smdl::Vec3{v[0], v[1], v[2]} : smdl::Vec3{nan, nan, nan};
    }

    smdl::Mesh MeshView(const smdl_mesh *mesh) { return smdl::Mesh(const_cast<smdl_mesh *>(mesh), mesh->model); }
}

extern "C" {

SMDL_API smdl_status smdl_model_add_flex_controller(smdl_model *model, const char *group, const char *name, float min,
                                                    float max, smdl_flex_controller_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const uint32_t id =
            smdl::detail::BorrowModel(model).addFlexController(group ? group : "", name ? name : "", min, max);
        if (out_id)
            *out_id = id;
    });
}

SMDL_API smdl_status smdl_model_find_flex_controller(const smdl_model *model, const char *name,
                                                     smdl_flex_controller_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const uint32_t id = smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findFlexController(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API uint32_t smdl_model_flex_controller_count(const smdl_model *model) {
    return model ? static_cast<uint32_t>(model->flex.controllers.size()) : 0;
}

SMDL_API smdl_status smdl_model_add_local_var(smdl_model *model, const char *name, smdl_flex_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const uint32_t id = smdl::detail::BorrowModel(model).addLocalVar(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API smdl_status smdl_model_find_flex(const smdl_model *model, const char *name, smdl_flex_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const uint32_t id = smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findFlex(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API uint32_t smdl_model_flex_count(const smdl_model *model) {
    return model ? static_cast<uint32_t>(model->flex.flexNames.size()) : 0;
}

SMDL_API smdl_status smdl_model_add_flex_rule(smdl_model *model, const char *flex, const char *expression) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    // a null expression is an error of its own, after the flex name check
    if (ValidName(flex) && !expression)
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "flex rule for '{}': expression is null", flex);
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).addFlexRule(std::string_view(flex ? flex : ""), expression ? expression : ""); });
}

SMDL_API smdl_status smdl_model_add_flex_rule_id(smdl_model *model, smdl_flex_id flex, const char *expression) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (flex >= model->flex.flexNames.size())
        return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "flex rule: flex {} does not exist ({} flexes)", flex,
                    model->flex.flexNames.size());
    if (!expression)
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "flex rule for '{}': expression is null",
                    model->flex.flexNames[flex]);
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).addFlexRule(flex, expression); });
}

SMDL_API smdl_status smdl_model_set_mouth(smdl_model *model, uint32_t index, const char *flex, smdl_bone_id bone,
                                          const float forward[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        smdl::detail::BorrowModel(model).setMouth(index, flex ? flex : "", bone, VecArg(forward));
    });
}

SMDL_API smdl_status smdl_model_add_bone_flex_driver(smdl_model *model, smdl_bone_id bone,
                                                     smdl_bone_flex_component component,
                                                     smdl_flex_controller_id controller, float min, float max) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        smdl::detail::BorrowModel(model).addBoneFlexDriver(bone, static_cast<smdl::BoneFlexComponent>(component),
                                                           controller, min, max);
    });
}

SMDL_API smdl_status smdl_mesh_add_eyeball(smdl_mesh *mesh, const char *name, smdl_bone_id bone,
                                           const float position[3], const char *material, float diameter,
                                           float z_angle_degrees, float pupil_scale, uint32_t *out_eyeball) {
    if (out_eyeball)
        *out_eyeball = SMDL_NO_ID;
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] {
        const uint32_t id = MeshView(mesh).addEyeball(name ? name : "", bone, VecArg(position), material ? material : "",
                                                      diameter, z_angle_degrees, pupil_scale);
        if (out_eyeball)
            *out_eyeball = id;
    });
}

SMDL_API smdl_status smdl_mesh_add_flex(smdl_mesh *mesh, const char *name, uint32_t shape, uint32_t *out_flex_key) {
    if (out_flex_key)
        *out_flex_key = SMDL_NO_ID;
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] {
        const uint32_t key = MeshView(mesh).addFlex(name ? name : "", shape);
        if (out_flex_key)
            *out_flex_key = key;
    });
}

SMDL_API smdl_status smdl_mesh_add_flex_pair(smdl_mesh *mesh, const char *name, float split, uint32_t shape,
                                             uint32_t *out_flex_key) {
    if (out_flex_key)
        *out_flex_key = SMDL_NO_ID;
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] {
        const uint32_t key = MeshView(mesh).addFlexPair(name ? name : "", split, shape);
        if (out_flex_key)
            *out_flex_key = key;
    });
}

SMDL_API smdl_status smdl_mesh_set_flex_position(smdl_mesh *mesh, uint32_t flex_key, float position) {
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] { MeshView(mesh).setFlexPosition(flex_key, position); });
}

SMDL_API smdl_status smdl_mesh_set_flex_decay(smdl_mesh *mesh, uint32_t flex_key, float decay) {
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] { MeshView(mesh).setFlexDecay(flex_key, decay); });
}

SMDL_API smdl_status smdl_mesh_set_flex_split(smdl_mesh *mesh, uint32_t flex_key, float split) {
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] { MeshView(mesh).setFlexSplit(flex_key, split); });
}

SMDL_API smdl_status smdl_mesh_get_flex_ids(const smdl_mesh *mesh, uint32_t flex_key, smdl_flex_id *out_flex,
                                            smdl_flex_id *out_left) {
    if (out_flex)
        *out_flex = SMDL_NO_ID;
    if (out_left)
        *out_left = SMDL_NO_ID;
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] { GetFlexIds(mesh, flex_key, out_flex, out_left); });
}

SMDL_API smdl_status smdl_mesh_set_default_flex(smdl_mesh *mesh, uint32_t shape) {
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] { MeshView(mesh).setDefaultFlex(shape); });
}

SMDL_API smdl_status smdl_mesh_add_eyelid(smdl_mesh *mesh, const char *name, smdl_eyelid_kind kind,
                                          uint32_t *out_eyelid) {
    if (out_eyelid)
        *out_eyelid = SMDL_NO_ID;
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] {
        const uint32_t id = MeshView(mesh).addEyelid(name ? name : "", static_cast<smdl::EyelidKind>(kind));
        if (out_eyelid)
            *out_eyelid = id;
    });
}

SMDL_API smdl_status smdl_mesh_set_eyelid_state(smdl_mesh *mesh, uint32_t eyelid, smdl_eyelid_state state,
                                                uint32_t shape, float target) {
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] {
        MeshView(mesh).setEyelidState(eyelid, static_cast<smdl::EyelidState>(state), shape, target);
    });
}

SMDL_API smdl_status smdl_mesh_set_eyelid_split(smdl_mesh *mesh, uint32_t eyelid, float split) {
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] { MeshView(mesh).setEyelidSplit(eyelid, split); });
}

SMDL_API smdl_status smdl_mesh_set_eyelid_eyeball(smdl_mesh *mesh, uint32_t eyelid, uint32_t eyeball) {
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] { MeshView(mesh).setEyelidEyeball(eyelid, eyeball); });
}

SMDL_API smdl_status smdl_mesh_add_dmx_eyelid(smdl_mesh *mesh, smdl_eyelid_kind kind, uint32_t right_eyeball,
                                              uint32_t left_eyeball, uint32_t *out_dmx_eyelid) {
    if (out_dmx_eyelid)
        *out_dmx_eyelid = SMDL_NO_ID;
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] {
        const uint32_t id =
            MeshView(mesh).addDmxEyelid(static_cast<smdl::EyelidKind>(kind), right_eyeball, left_eyeball);
        if (out_dmx_eyelid)
            *out_dmx_eyelid = id;
    });
}

SMDL_API smdl_status smdl_mesh_set_dmx_eyelid_state(smdl_mesh *mesh, uint32_t dmx_eyelid, smdl_eyelid_state state,
                                                    uint32_t shape, float target) {
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] {
        MeshView(mesh).setDmxEyelidState(dmx_eyelid, static_cast<smdl::EyelidState>(state), shape, target);
    });
}

SMDL_API smdl_status smdl_mesh_add_sphere_normals(smdl_mesh *mesh, const char *material, const float position[3]) {
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] { MeshView(mesh).addSphereNormals(material ? material : "", VecArg(position)); });
}

SMDL_API smdl_status smdl_mesh_set_no_auto_dmx_rules(smdl_mesh *mesh, int enabled) {
    if (!OwnedMesh(mesh))
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] { MeshView(mesh).setNoAutoDmxRules(enabled != 0); });
}

} // extern "C"
