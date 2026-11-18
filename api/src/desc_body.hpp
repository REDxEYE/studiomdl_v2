// Description of the body groups part (Core). The groups and meshes themselves are smdl_bodygroup / smdl_mesh
// (desc_shared.hpp, owned by smdl_model::bodygroups / meshes).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "desc_shared.hpp"

// $bodygrouppreset <name> { <bodygroup> <value> ... }
struct BodyGroupPresetDesc {
    std::string name;
    std::vector<std::pair<const smdl_bodygroup *, uint32_t>> values; // bodygroup, choice index
};

struct BodyDesc {
    std::vector<BodyGroupPresetDesc> presets;
};
