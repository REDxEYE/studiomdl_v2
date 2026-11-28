#pragma once

// Text edits of a VMT that keep its layout and comments: what the Materials tab's Sync writes.

#include <map>
#include <optional>

#include <QString>

// VMT parameter changes by lower-case name; nullopt removes the parameter.
using MaterialEdits = std::map<QString, std::optional<QString>>;

// cmaterial.cpp FindBuiltinFallbackBlock at SFM's dxlevel 95: the first block named "<condition>" or
// "<shader>_<condition>" overrides the root parameters.
inline constexpr const char *kVmtFallbackConditions[] = {">=DX90_20b", ">=DX90", ">DX90", "dx9", "DX9"};

// `text` with `edits` applied where the material system reads them: the active fallback block or the root, or, for a
// "patch" VMT, its replace/insert blocks (new parameters go to insert). Throws std::runtime_error for VMT text it cannot
// parse and for names or values that cannot be written as quoted VMT tokens.
QString PatchVmtText(const QString &text, const MaterialEdits &edits);
