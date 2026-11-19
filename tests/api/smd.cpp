// SMD / VTA reference reader and the vertex data the builder is fed from it (semantics: harness.hpp)
#include "harness.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <tuple>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <optional>

namespace {
    constexpr int kMaxExtras = 14; // (MAXSTUDIOTEXCOORDS - 1) * 2 in the compiler

    bool IEquals(const char *a, const char *b) {
        for (; *a && *b; ++a, ++b) {
            if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
                return false;
        }
        return *a == *b;
    }

    // Line source with GetLineInput's rules: `//` lines are skipped, a CR before the LF is dropped
    class Lines {
    public:
        explicit Lines(const std::filesystem::path &path) : m_Path(path) {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                throw TestFailure(std::format("cannot open {}", path.string()));
            m_Text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        bool next(std::string &line) {
            while (m_Pos < m_Text.size()) {
                const size_t end = m_Text.find('\n', m_Pos);
                const size_t stop = end == std::string::npos ? m_Text.size() : end;
                line.assign(m_Text, m_Pos, stop - m_Pos);
                m_Pos = end == std::string::npos ? m_Text.size() : end + 1;
                ++m_Number;
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.size() >= 2 && line[0] == '/' && line[1] == '/')
                    continue;
                return true;
            }
            return false;
        }

        [[noreturn]] void error(std::string_view what, const std::string &line) const {
            throw TestFailure(std::format("{}:{}: {}: '{}'", m_Path.string(), m_Number, what, line));
        }

    private:
        std::filesystem::path m_Path;
        std::string m_Text;
        size_t m_Pos = 0;
        int m_Number = 0;
    };

    // `end` followed only by whitespace (IsEnd)
    bool IsEnd(const std::string &line) {
        if (line.compare(0, 3, "end") != 0)
            return false;
        for (size_t i = 3; i < line.size(); ++i) {
            if (!std::isspace(static_cast<unsigned char>(line[i])))
                return false;
        }
        return true;
    }

    // Grab_Nodes
    std::vector<SmdNode> ReadNodes(Lines &lines) {
        std::vector<SmdNode> nodes;
        std::string line;
        while (lines.next(line)) {
            int index = 0, parent = 0;
            char name[1024];
            if (std::sscanf(line.c_str(), "%d \"%1023[^\"]\" %d", &index, name, &parent) == 3) {
                if (index < 0 || index >= 1024)
                    lines.error("bad node index", line);
                if (static_cast<size_t>(index) >= nodes.size())
                    nodes.resize(static_cast<size_t>(index) + 1, SmdNode{std::string(), -1});
                nodes[index].name = name;
                nodes[index].parent = parent;
            } else {
                for (size_t i = 0; i < nodes.size(); ++i) {
                    if (nodes[i].name.empty())
                        lines.error("node indices are not contiguous", line);
                    if (nodes[i].parent >= static_cast<int>(i))
                        lines.error("a node's parent must come before it", line);
                }
                return nodes;
            }
        }
        lines.error("unexpected end of file in nodes", line);
    }

    // Grab_Animation: frames with the previous frame's values duplicated into each new one
    std::vector<SmdFrame> ReadSkeleton(Lines &lines, size_t boneCount) {
        std::vector<std::optional<SmdFrame>> frames;
        int start = -1, last = -1000000000;
        int t = -99999999;
        std::string line;
        while (lines.next(line)) {
            int index = 0;
            float p[3], r[3];
            if (std::sscanf(line.c_str(), "%d %f %f %f %f %f %f", &index, &p[0], &p[1], &p[2], &r[0], &r[1], &r[2]) == 7) {
                if (start < 0)
                    lines.error("missing frame start", line);
                if (index < 0 || static_cast<size_t>(index) >= boneCount)
                    lines.error("bone index out of range", line);
                SmdBonePose &pose = frames[t]->bones[index];
                pose.position = {p[0], p[1], p[2]};
                pose.euler = {r[0], r[1], r[2]};
                continue;
            }
            char cmd[1024];
            if (std::sscanf(line.c_str(), "%1023s %d", cmd, &index) == 0)
                lines.error("bad skeleton line", line);
            if (IEquals(cmd, "time")) {
                t = index;
                if (start == -1)
                    start = t;
                if (t < start)
                    lines.error("frame before the first frame", line);
                last = std::max(last, t);
                t -= start;
                if (static_cast<size_t>(t) >= frames.size())
                    frames.resize(static_cast<size_t>(t) + 1);
                if (frames[t])
                    continue;
                SmdFrame &frame = frames[t].emplace();
                frame.time = start + t;
                frame.bones.resize(boneCount);
                if (t > 0 && frames[t - 1])
                    frame.bones = frames[t - 1]->bones;
                continue;
            }
            if (IEquals(cmd, "end")) {
                std::vector<SmdFrame> result;
                for (size_t i = 0; i < frames.size(); ++i) {
                    if (!frames[i])
                        lines.error(std::format("missing frame {}", start + static_cast<int>(i)), line);
                    result.push_back(std::move(*frames[i]));
                }
                return result;
            }
            lines.error("bad skeleton line", line);
        }
        lines.error("unexpected end of file in skeleton", line);
    }

    // GetNextFaceItem
    const char *NextFaceItem(const char *current) {
        if (!current)
            return nullptr;
        const char *p = current;
        while (*p == ' ')
            ++p;
        p = std::strchr(p, ' ');
        if (!p)
            return nullptr;
        while (*p == ' ')
            ++p;
        if (*p == 0 || *p == '\n')
            return nullptr;
        return p;
    }

    // The corner part of ParseFaceData
    SmdCorner ReadCorner(Lines &lines, const std::string &line, int version, size_t boneCount) {
        SmdCorner corner;
        float p[3], n[3], t[2];
        int bone = 0;
        if (std::sscanf(line.c_str(), "%d %f %f %f %f %f %f %f %f", &bone, &p[0], &p[1], &p[2], &n[0], &n[1], &n[2],
                        &t[0], &t[1]) < 9)
            lines.error("bad vertex line", line);
        if (bone < 0 || static_cast<size_t>(bone) >= boneCount)
            lines.error("bogus bone index", line);
        corner.bone = bone;
        corner.position = {p[0], p[1], p[2]};
        corner.normal = {n[0], n[1], n[2]};
        corner.uv = {t[0], t[1]};

        const char *item = line.c_str();
        for (int k = 0; k < 9; ++k)
            item = NextFaceItem(item);
        if (item) {
            const int count = std::atoi(item);
            if (count > 0) {
                for (int k = 0; k < count && k < 1024; ++k) {
                    item = NextFaceItem(item);
                    if (!item)
                        lines.error(std::format("bone id {} not found", k), line);
                    SmdLink link;
                    link.bone = std::atoi(item);
                    item = NextFaceItem(item);
                    if (!item)
                        lines.error(std::format("bone weight {} not found", k), line);
                    link.weight = static_cast<float>(std::atof(item));
                    corner.links.push_back(link);
                }
            }
            if (version >= 3) {
                item = NextFaceItem(item);
                if (item) {
                    int extras = std::atoi(item);
                    if (extras > 0) {
                        extras = std::min(extras, kMaxExtras);
                        for (int e = 0; e < extras; ++e) {
                            item = NextFaceItem(item);
                            if (!item)
                                lines.error(std::format("extra data item {} not found", e), line);
                            corner.extras.push_back(static_cast<float>(std::atof(item)));
                        }
                    }
                }
            }
        }
        for (const SmdLink &link: corner.links) {
            if (link.bone < 0 || static_cast<size_t>(link.bone) >= boneCount)
                lines.error("bogus bone index in a weight link", line);
        }
        return corner;
    }

    // Grab_Triangles (the reading half)
    void ReadTriangles(Lines &lines, SmdFile &smd) {
        std::string line;
        while (lines.next(line)) {
            if (IsEnd(line))
                return;
            std::string material = line;
            while (!material.empty() && !std::isgraph(static_cast<unsigned char>(material.back())))
                material.pop_back();
            SmdTriangle triangle;
            triangle.material = material;
            std::string cornerLine;
            for (int c = 0; c < 3; ++c) {
                if (!lines.next(cornerLine))
                    lines.error("unexpected end of file in triangles", line);
                if (material.empty())
                    continue; // the loader skips triangles without a material name
                triangle.corners[c] = ReadCorner(lines, cornerLine, smd.version, smd.nodes.size());
            }
            if (!material.empty())
                smd.triangles.push_back(std::move(triangle));
        }
    }

    void SkipToEnd(Lines &lines) {
        std::string line;
        while (lines.next(line)) {
            if (IsEnd(line))
                return;
        }
    }
} // namespace

SmdFile ReadSmd(const std::filesystem::path &path) {
    Lines lines(path);
    SmdFile smd;
    smd.path = path;
    smd.version = 1;

    std::string line;
    while (lines.next(line)) {
        char cmd[1024];
        int option = 0;
        const int read = std::sscanf(line.c_str(), "%1023s %d", cmd, &option);
        if (read <= 0)
            continue;
        if (IEquals(cmd, "version")) {
            if (read < 2 || option < 1 || option > 3)
                lines.error("bad version", line);
            smd.version = option;
        } else if (IEquals(cmd, "nodes")) {
            smd.nodes = ReadNodes(lines);
        } else if (IEquals(cmd, "skeleton")) {
            smd.frames = ReadSkeleton(lines, smd.nodes.size());
        } else if (IEquals(cmd, "triangles")) {
            ReadTriangles(lines, smd);
        } else if (IEquals(cmd, "vertexanimation")) {
            SkipToEnd(lines);
        }
        // anything else: the loader warns and goes on
    }
    return smd;
}

// ---------------------------------------------------------------------------------------------------------------
// Geometry data
// ---------------------------------------------------------------------------------------------------------------

namespace {
    // The corner's influences as the loader would use them: its links, or its bone at weight 1
    std::vector<SmdLink> EffectiveLinks(const SmdCorner &corner) {
        if (corner.links.empty())
            return {SmdLink{corner.bone, 1.0f}};
        return corner.links;
    }

    struct PoolKey {
        smdl::Vec3 position;
        std::vector<std::pair<int, float>> links;
        bool operator<(const PoolKey &other) const {
            return std::tie(position, links) < std::tie(other.position, other.links);
        }
    };
} // namespace

GeometryData BuildGeometryData(const SmdFile &smd, VertexMode mode) {
    GeometryData data;

    // extras must be uniform: the unification of the loader treats a missing pair as "not set"
    size_t extraFloats = 0;
    bool first = true;
    uint32_t influences = 1;
    for (const SmdTriangle &triangle: smd.triangles) {
        for (const SmdCorner &corner: triangle.corners) {
            if (first)
                extraFloats = corner.extras.size();
            else if (corner.extras.size() != extraFloats)
                throw TestFailure(std::format("{}: corners with different extra uv counts are not supported",
                                              smd.path.string()));
            first = false;
            influences = std::max<uint32_t>(influences, static_cast<uint32_t>(EffectiveLinks(corner).size()));
        }
    }
    if (extraFloats % 2)
        throw TestFailure(std::format("{}: odd number of extra uv floats", smd.path.string()));
    const size_t uvSets = 1 + extraFloats / 2;
    data.influencesPerVertex = influences;
    data.uvs.resize(uvSets);
    if (mode == VertexMode::Corners)
        data.cornerUvs.resize(uvSets);

    auto uvOf = [](const SmdCorner &corner, size_t set) {
        if (set == 0)
            return corner.uv;
        return smdl::Vec2{corner.extras[(set - 1) * 2], corner.extras[(set - 1) * 2 + 1]};
    };

    auto addVertex = [&](const SmdCorner &corner) {
        data.positions.push_back(corner.position);
        data.normals.push_back(corner.normal);
        for (size_t set = 0; set < uvSets; ++set)
            data.uvs[set].push_back(uvOf(corner, set));
        const std::vector<SmdLink> links = EffectiveLinks(corner);
        for (uint32_t k = 0; k < influences; ++k) {
            const bool real = k < links.size();
            data.weightNodes.push_back(static_cast<uint32_t>(real ? links[k].bone : links[0].bone));
            data.weights.push_back(real ? links[k].weight : 0.0f);
        }
        return static_cast<uint32_t>(data.positions.size() - 1);
    };

    std::map<std::string, uint32_t> materialIds;
    std::map<PoolKey, uint32_t> pool;
    for (const SmdTriangle &triangle: smd.triangles) {
        std::string materialKey = triangle.material;
        for (char &c: materialKey)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto material = materialIds.find(materialKey);
        if (material == materialIds.end()) {
            material = materialIds.emplace(materialKey, static_cast<uint32_t>(data.materials.size())).first;
            data.materials.push_back(triangle.material);
        }
        data.triangleMaterials.push_back(material->second);

        for (const SmdCorner &corner: triangle.corners) {
            if (mode == VertexMode::Indexed) {
                data.indices.push_back(addVertex(corner));
                continue;
            }
            PoolKey key{corner.position, {}};
            for (const SmdLink &link: EffectiveLinks(corner))
                key.links.emplace_back(link.bone, link.weight);
            auto found = pool.find(key);
            if (found == pool.end())
                found = pool.emplace(std::move(key), addVertex(corner)).first;
            data.indices.push_back(found->second);
            data.cornerNormals.push_back(corner.normal);
            for (size_t set = 0; set < uvSets; ++set)
                data.cornerUvs[set].push_back(uvOf(corner, set));
        }
    }
    return data;
}

// ---------------------------------------------------------------------------------------------------------------
// VTA
// ---------------------------------------------------------------------------------------------------------------

const VtaFrame *VtaFile::findFrame(int frame) const {
    for (const VtaFrame &candidate: frames) {
        if (candidate.frame == frame)
            return &candidate;
    }
    return nullptr;
}

VtaFile ReadVta(const std::filesystem::path &path) {
    Lines lines(path);
    VtaFile vta;
    vta.path = path;

    std::string line;
    while (lines.next(line)) {
        char cmd[1024];
        int option = 0;
        const int read = std::sscanf(line.c_str(), "%1023s %d", cmd, &option);
        if (read <= 0)
            continue;
        if (IEquals(cmd, "version")) {
            if (read < 2 || option != 1)
                lines.error("bad version", line);
        } else if (IEquals(cmd, "nodes")) {
            vta.nodes = ReadNodes(lines);
        } else if (IEquals(cmd, "skeleton")) {
            vta.skeleton = ReadSkeleton(lines, vta.nodes.size());
        } else if (IEquals(cmd, "vertexanimation")) {
            // Grab_Vertexanimation: `time N` starts a frame (N relative to the skeleton's first time), vertex lines
            // `index px py pz nx ny nz` follow, `end` stops
            const int startFrame = vta.skeleton.empty() ? 0 : vta.skeleton.front().time;
            const int endFrame = vta.skeleton.empty() ? 0 : vta.skeleton.back().time;
            int t = -1;
            while (lines.next(line)) {
                int index = 0;
                float p[3], n[3];
                if (std::sscanf(line.c_str(), "%d %f %f %f %f %f %f", &index, &p[0], &p[1], &p[2], &n[0], &n[1], &n[2]) == 7) {
                    if (t < 0)
                        lines.error("vertex line before the first frame", line);
                    if (index < 0)
                        lines.error("negative vertex index", line);
                    VtaVertex vertex;
                    vertex.index = index;
                    vertex.position = {p[0], p[1], p[2]};
                    vertex.normal = {n[0], n[1], n[2]};
                    vta.frames.back().vertices.push_back(vertex);
                    vta.vertexCount = std::max(vta.vertexCount, index + 1);
                    continue;
                }
                char word[1024];
                if (std::sscanf(line.c_str(), "%1023s %d", word, &index) == 0)
                    lines.error("bad vertexanimation line", line);
                if (IEquals(word, "time")) {
                    if (index < startFrame || index > endFrame)
                        lines.error("frame out of the skeleton's range", line);
                    t = index - startFrame;
                    VtaFrame frame;
                    frame.time = index;
                    frame.frame = t;
                    vta.frames.push_back(std::move(frame));
                } else if (IEquals(word, "end")) {
                    break;
                } else {
                    lines.error("bad vertexanimation line", line);
                }
            }
        }
    }
    if (!vta.frames.empty() && vta.frames.front().frame != 0)
        throw TestFailure(std::format("{}: the first vertexanimation frame must be time 0 of the skeleton", path.string()));
    return vta;
}

std::vector<VtaDelta> VtaFrameDeltas(const VtaFile &vta, int frameNumber) {
    const VtaFrame *base = vta.findFrame(0);
    const VtaFrame *frame = vta.findFrame(frameNumber);
    if (!base || !frame)
        throw TestFailure(std::format("{}: no vertex animation frame {}", vta.path.string(), frameNumber));
    std::vector<const VtaVertex *> baseByIndex(static_cast<size_t>(vta.vertexCount), nullptr);
    for (const VtaVertex &vertex: base->vertices)
        baseByIndex[vertex.index] = &vertex;

    std::vector<VtaDelta> deltas;
    for (const VtaVertex &vertex: frame->vertices) {
        const VtaVertex *origin = baseByIndex[vertex.index];
        if (!origin)
            throw TestFailure(std::format("{}: vertex {} is not in frame 0", vta.path.string(), vertex.index));
        VtaDelta delta;
        delta.vertex = vertex.index;
        for (int i = 0; i < 3; ++i) {
            delta.positionDelta[i] = vertex.position[i] - origin->position[i];
            delta.normalDelta[i] = vertex.normal[i] - origin->normal[i];
        }
        deltas.push_back(delta);
    }
    return deltas;
}

VtaVertexMap MapVtaToGeometry(const VtaFile &vta, std::span<const smdl::Vec3> positions,
                              std::span<const smdl::Vec3> normals) {
    constexpr float kMaxDist = 0.3873f; // MAX_VANIM_DIST
    const VtaFrame *base = vta.findFrame(0);
    if (!base)
        throw TestFailure(std::format("{}: no frame 0", vta.path.string()));
    if (positions.size() != normals.size())
        throw TestFailure("MapVtaToGeometry: positions and normals differ in size");

    std::vector<const VtaVertex *> byIndex(static_cast<size_t>(vta.vertexCount), nullptr);
    for (const VtaVertex &vertex: base->vertices)
        byIndex[vertex.index] = &vertex;

    std::vector<float> bestDist(positions.size(), 1e30f);
    std::vector<float> bestDot(positions.size(), -1.0f);
    std::vector<int> modelToVta(positions.size(), -1);
    VtaVertexMap map;
    map.geometryVertices.resize(byIndex.size());

    auto lengthSqr = [](const smdl::Vec3 &a, const smdl::Vec3 &b) {
        const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
        return x * x + y * y + z * z;
    };

    float searchRadius = kMaxDist;
    std::vector<size_t> candidates;
    for (size_t j = 0; j < byIndex.size(); ++j) {
        if (!byIndex[j])
            continue; // the compiler would read an uninitialised vanim entry: fixtures list every vertex
        const smdl::Vec3 &center = byIndex[j]->position;
        const smdl::Vec3 &vtaNormal = byIndex[j]->normal;
        auto query = [&](float radius) {
            candidates.clear();
            const float radiusSqr = radius * radius;
            for (size_t k = 0; k < positions.size(); ++k) {
                if (lengthSqr(center, positions[k]) <= radiusSqr)
                    candidates.push_back(k);
            }
        };
        query(searchRadius);
        while (candidates.empty() && searchRadius < kMaxDist) {
            searchRadius = std::min(kMaxDist, searchRadius * 2);
            query(searchRadius);
        }
        searchRadius = std::max(0.001f * kMaxDist, searchRadius * 0.95f);

        float minDist = 1e30f;
        for (const size_t k: candidates) {
            const float dist = lengthSqr(positions[k], center);
            const float dot = normals[k][0] * vtaNormal[0] + normals[k][1] * vtaNormal[1] + normals[k][2] * vtaNormal[2];
            minDist = std::min(minDist, dist);
            if (dist < bestDist[k] || (dist == bestDist[k] && dot > bestDot[k])) {
                bestDist[k] = dist;
                bestDot[k] = dot;
                modelToVta[k] = static_cast<int>(j);
            }
        }
        if (minDist > 0.01f)
            ++map.unmatched;
    }

    for (size_t k = 0; k < modelToVta.size(); ++k) {
        if (modelToVta[k] != -1)
            map.geometryVertices[static_cast<size_t>(modelToVta[k])].push_back(static_cast<uint32_t>(k));
    }
    return map;
}
