#include "mdl_mesh.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>

#include <QFile>
#include <QFileInfo>
#include <QFloat16>
#include <QMatrix4x4>
#include <QQuaternion>

#include "studiomdl/optimize.h"

// The SDK replaces offsetof with a pointer expression; use the compiler's layout constant in the file reader.
#if defined(__GNUC__) || defined(__clang__)
#undef offsetof
#define offsetof(type, field) __builtin_offsetof(type, field)
static_assert(offsetof(studiohdr_t, numbodyparts) == 232);
#endif

namespace
{
using namespace OptimizedModel;
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(mstudiovertex_t) == 48 && sizeof(mstudiomodel_t) == 148 && sizeof(mstudiomesh_t) == 116);
static_assert(sizeof(StripGroupHeader_t) == 33 && sizeof(StripHeader_t) == 35);
static_assert(sizeof(mstudiotexture_t) == 64);
static_assert(sizeof(mstudiobone_t) == 216 && sizeof(mstudioboneweight_t) == 16);

class ModelFile
{
public:
    explicit ModelFile(const QString &path) : m_path(path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            fail(file.errorString());
        const auto size = file.size();
        if (size < 0 || size > 128 * 1024 * 1024)
            fail(QStringLiteral("outside the preview's 128 MiB file limit"));
        m_data = file.read(size);
        if (m_data.size() != size)
            fail(QStringLiteral("incomplete read"));
    }

    size_t size() const { return m_data.size(); }

    template<class T>
    T read(size_t offset) const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (offset > size() || sizeof(T) > size() - offset)
            fail(QStringLiteral("truncated record"));
        T value;
        std::memcpy(&value, m_data.constData() + offset, sizeof(T));
        return value;
    }

    size_t table(size_t base, int offset, int count, size_t stride) const
    {
        if (base > size() || offset < 0 || size_t(offset) > size() - base || count < 0 ||
            size_t(count) > (size() - base - offset) / stride)
            fail(QStringLiteral("table outside file bounds"));
        return base + offset;
    }

    QString string(size_t offset) const
    {
        if (offset >= size())
            fail(QStringLiteral("string outside file bounds"));
        const auto *start = m_data.constData() + offset;
        const auto *end = static_cast<const char *>(std::memchr(start, 0, size() - offset));
        if (!end)
            fail(QStringLiteral("unterminated string"));
        return QString::fromUtf8(start, end - start);
    }

    [[noreturn]] void fail(const QString &reason) const
    {
        throw std::runtime_error((m_path + QStringLiteral(": ") + reason).toStdString());
    }

private:
    QString m_path;
    QByteArray m_data;
};

constexpr auto finite = [](float value) { return std::isfinite(value); };

std::vector<QMatrix4x4> ReadDefaultSkinMatrices(const ModelFile &mdl)
{
    const int count = mdl.read<int>(offsetof(studiohdr_t, numbones));
    if (count < 1 || count > int(MAXSTUDIOBONES))
        mdl.fail(QStringLiteral("invalid studio bone count"));
    const auto bones = mdl.table(0, mdl.read<int>(offsetof(studiohdr_t, boneindex)), count, sizeof(mstudiobone_t));
    std::vector<QMatrix4x4> boneToModel(count), skinToModel(count);
    for (int i = 0; i < count; ++i)
    {
        const auto bone = bones + i * sizeof(mstudiobone_t);
        const int parent = mdl.read<int>(bone + offsetof(mstudiobone_t, parent));
        if (parent < -1 || parent >= i)
            mdl.fail(QStringLiteral("invalid studio bone parent"));
        const auto pos = mdl.read<std::array<float, 3>>(bone + offsetof(mstudiobone_t, pos));
        const auto quat = mdl.read<std::array<float, 4>>(bone + offsetof(mstudiobone_t, quat));
        const auto inverseBind = mdl.read<std::array<float, 12>>(bone + offsetof(mstudiobone_t, poseToBone));
        const QQuaternion rotation(quat[3], quat[0], quat[1], quat[2]);
        const float length = rotation.lengthSquared();
        if (!std::ranges::all_of(pos, finite) || !std::ranges::all_of(inverseBind, finite) ||
            !std::isfinite(length) || length < 1e-8f)
            mdl.fail(QStringLiteral("invalid studio bone transform"));

        auto &world = boneToModel[i];
        world.translate(pos[0], pos[1], pos[2]);
        world.rotate(rotation.normalized());
        if (parent >= 0)
            world = boneToModel[parent] * world;
        QMatrix4x4 bind;
        for (int row = 0; row < 3; ++row)
        {
            const auto *r = inverseBind.data() + 4 * row;
            bind.setRow(row, QVector4D(r[0], r[1], r[2], r[3]));
        }
        skinToModel[i] = world * bind;
        if (!std::ranges::all_of(std::span(skinToModel[i].constData(), 16), finite))
            mdl.fail(QStringLiteral("studio bone transform overflow"));
    }
    return skinToModel;
}

std::vector<Vertex> ReadVertices(const ModelFile &vvd, const vertexFileHeader_t &header,
                                 std::span<const QMatrix4x4> skinMatrices, std::vector<std::array<float, 9>> &skinLinear)
{
    const int count = header.numLODVertexes[0];
    const auto data = vvd.table(0, header.vertexDataStart, count, sizeof(mstudiovertex_t));
    const auto tangents = header.tangentDataStart ?
        vvd.table(0, header.tangentDataStart, count, sizeof(float) * 4) : 0;
    std::vector<Vertex> vertices;
    vertices.reserve(count);
    const auto append = [&](int first, int length) {
        if (first < 0 || length < 0 || first > count || length > count - first ||
            size_t(length) > size_t(count) - vertices.size())
            vvd.fail(QStringLiteral("invalid LOD 0 vertex fixup"));
        for (int i = first; i < first + length; ++i)
        {
            const auto offset = data + i * sizeof(mstudiovertex_t);
            auto pos = vvd.read<std::array<float, 3>>(offset + offsetof(mstudiovertex_t, m_vecPosition));
            auto normal = vvd.read<std::array<float, 3>>(offset + offsetof(mstudiovertex_t, m_vecNormal));
            const auto uv = vvd.read<std::array<float, 2>>(offset + offsetof(mstudiovertex_t, m_vecTexCoord));
            auto tangent = tangents ? vvd.read<std::array<float, 4>>(tangents + i * sizeof(float) * 4) : std::array<float, 4>{};
            if (!skinMatrices.empty())
            {
                const auto weights = vvd.read<mstudioboneweight_t>(offset + offsetof(mstudiovertex_t, m_BoneWeights));
                if (weights.numbones == 0 || weights.numbones > MAX_NUM_BONES_PER_VERT)
                    vvd.fail(QStringLiteral("invalid vertex bone count"));
                const QVector3D p(pos[0], pos[1], pos[2]), n(normal[0], normal[1], normal[2]), t(tangent[0], tangent[1], tangent[2]);
                QVector3D skinnedPos, skinnedNormal, skinnedTangent;
                float total = 0;
                std::array<float, 9> linear{};
                for (int j = 0; j < weights.numbones; ++j)
                {
                    const auto bone = weights.bone[j];
                    const auto weight = weights.weight[j];
                    if (bone >= skinMatrices.size() || !std::isfinite(weight) || weight < 0 || weight > 1)
                        vvd.fail(QStringLiteral("invalid vertex bone influence"));
                    if (weight == 0)
                        continue;
                    const auto &matrix = skinMatrices[bone];
                    skinnedPos += weight * matrix.map(p);
                    skinnedNormal += weight * matrix.mapVector(n);
                    if (tangents)
                        skinnedTangent += weight * matrix.mapVector(t);
                    total += weight;
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c)
                            linear[r * 3 + c] += weight * matrix(r, c);
                }
                if (std::abs(total - 1) > 0.01f)
                    vvd.fail(QStringLiteral("vertex bone weights do not sum to one"));
                skinnedPos /= total;
                for (auto &value : linear)
                    value /= total;
                skinLinear.push_back(linear); // moves bind-space flex deltas into the baked pose
                skinnedNormal.normalize();
                skinnedTangent.normalize();
                pos = {skinnedPos.x(), skinnedPos.y(), skinnedPos.z()};
                normal = {skinnedNormal.x(), skinnedNormal.y(), skinnedNormal.z()};
                tangent = {skinnedTangent.x(), skinnedTangent.y(), skinnedTangent.z(), tangent[3]};
            }
            if (!std::ranges::all_of(pos, finite) || !std::ranges::all_of(normal, finite) ||
                !std::ranges::all_of(uv, finite) || !std::ranges::all_of(tangent, finite))
                vvd.fail(QStringLiteral("non-finite vertex data"));
            vertices.push_back({{pos[0], pos[1], pos[2], 1}, {uv[0], uv[1]}, {normal[0], normal[1], normal[2]},
                                {tangent[0], tangent[1], tangent[2], tangent[3]}});
        }
    };
    if (!header.numFixups)
        append(0, count);
    else
    {
        const auto fixups = vvd.table(0, header.fixupTableStart, header.numFixups, sizeof(vertexFileFixup_t));
        for (int i = 0; i < header.numFixups; ++i)
        {
            const auto fixup = vvd.read<vertexFileFixup_t>(fixups + i * sizeof(vertexFileFixup_t));
            if (fixup.lod < 0 || fixup.lod >= header.numLODs)
                vvd.fail(QStringLiteral("invalid fixup LOD"));
            append(fixup.sourceVertexID, fixup.numVertexes);
        }
        if (vertices.size() != size_t(count))
            vvd.fail(QStringLiteral("fixups do not cover LOD 0"));
    }
    return vertices;
}

void AppendMeshIndices(const ModelFile &vtx, size_t mesh, uint32_t firstVertex, int meshVertexCount,
                       std::vector<uint32_t> &indices)
{
    const auto meshHeader = vtx.read<MeshHeader_t>(mesh);
    const auto groups = vtx.table(mesh, meshHeader.stripGroupHeaderOffset, meshHeader.numStripGroups, sizeof(StripGroupHeader_t));
    for (int groupID = 0; groupID < meshHeader.numStripGroups; ++groupID)
    {
        const auto group = groups + groupID * sizeof(StripGroupHeader_t);
        const auto info = vtx.read<StripGroupHeader_t>(group);
        const auto vertices = vtx.table(group, info.vertOffset, info.numVerts, sizeof(Vertex_t));
        const auto sourceIndices = vtx.table(group, info.indexOffset, info.numIndices, sizeof(uint16_t));
        const auto strips = vtx.table(group, info.stripOffset, info.numStrips, sizeof(StripHeader_t));
        if (info.numTopologyIndices != 0)
            vtx.fail(QStringLiteral("subdivision topology is not supported"));
        for (int stripID = 0; stripID < info.numStrips; ++stripID)
        {
            const auto strip = vtx.read<StripHeader_t>(strips + stripID * sizeof(StripHeader_t));
            if (strip.flags != STRIP_IS_TRILIST || strip.numIndices < 0 || strip.numIndices % 3 ||
                strip.indexOffset < 0 || strip.indexOffset > info.numIndices ||
                strip.numIndices > info.numIndices - strip.indexOffset ||
                size_t(strip.numIndices) > vtx.size() / sizeof(uint16_t) - indices.size())
                vtx.fail(QStringLiteral("invalid or unsupported VTX triangle list"));
            for (int i = 0; i < strip.numIndices; ++i)
            {
                const auto index = vtx.read<uint16_t>(sourceIndices + (strip.indexOffset + i) * sizeof(uint16_t));
                if (index >= info.numVerts)
                    vtx.fail(QStringLiteral("index outside strip group's vertex table"));
                const auto vertex = vtx.read<Vertex_t>(vertices + index * sizeof(Vertex_t));
                if (vertex.origMeshVertID >= meshVertexCount)
                    vtx.fail(QStringLiteral("vertex outside MDL mesh"));
                indices.push_back(firstVertex + vertex.origMeshVertID);
            }
        }
    }
}

// Names of `count` records of `stride` bytes at `index`, each naming itself via an int offset at
// `nameField` relative to the record.
QStringList ReadNames(const ModelFile &mdl, int count, int index, size_t stride, size_t nameField)
{
    const auto table = mdl.table(0, index, count, stride);
    QStringList names;
    for (int i = 0; i < count; ++i)
    {
        const auto record = table + i * stride;
        names.append(mdl.string(mdl.table(record, mdl.read<int>(record + nameField), 1, 1)));
    }
    return names;
}

MdlInfo ReadInfo(const ModelFile &mdl)
{
    static_assert(sizeof(mstudioattachment_t) == 92 && sizeof(mstudioseqdesc_t) == 212 &&
                  sizeof(mstudiohitboxset_t) == 12 && sizeof(mstudiobodyparts_t) == 16);
    const auto field = [&](size_t offset) { return mdl.read<int>(offset); };
    MdlInfo info;
    info.bones = ReadNames(mdl, field(offsetof(studiohdr_t, numbones)), field(offsetof(studiohdr_t, boneindex)),
                           sizeof(mstudiobone_t), offsetof(mstudiobone_t, sznameindex));
    info.attachments = ReadNames(mdl, field(offsetof(studiohdr_t, numlocalattachments)),
                                 field(offsetof(studiohdr_t, localattachmentindex)), sizeof(mstudioattachment_t),
                                 offsetof(mstudioattachment_t, sznameindex));
    info.bodyParts = ReadNames(mdl, field(offsetof(studiohdr_t, numbodyparts)), field(offsetof(studiohdr_t, bodypartindex)),
                               sizeof(mstudiobodyparts_t), offsetof(mstudiobodyparts_t, sznameindex));
    info.sequences = ReadNames(mdl, field(offsetof(studiohdr_t, numlocalseq)), field(offsetof(studiohdr_t, localseqindex)),
                               sizeof(mstudioseqdesc_t), offsetof(mstudioseqdesc_t, szlabelindex));
    info.boneControllers = field(offsetof(studiohdr_t, numbonecontrollers));
    info.skinFamilies = field(offsetof(studiohdr_t, numskinfamilies));
    info.hitboxSets = field(offsetof(studiohdr_t, numhitboxsets));
    const auto sets = mdl.table(0, field(offsetof(studiohdr_t, hitboxsetindex)), info.hitboxSets, sizeof(mstudiohitboxset_t));
    for (int i = 0; i < info.hitboxSets; ++i)
        info.hitboxes += mdl.read<int>(sets + i * sizeof(mstudiohitboxset_t) + offsetof(mstudiohitboxset_t, numhitboxes));
    const auto vec = [&](size_t offset) {
        const auto v = mdl.read<std::array<float, 3>>(offset);
        return QVector3D(v[0], v[1], v[2]);
    };
    info.hullMin = vec(offsetof(studiohdr_t, hull_min));
    info.hullMax = vec(offsetof(studiohdr_t, hull_max));
    return info;
}

// Flex descriptors, controllers and rules (studiohdr_t), with every rule's stack use checked up front so
// RunFlexRules can index without bounds checks except for stack-supplied controller indices.
MdlFlexData ReadFlexHeader(const ModelFile &mdl)
{
    static_assert(sizeof(mstudioflexcontroller_t) == 20 && sizeof(mstudioflexrule_t) == 12 && sizeof(mstudioflexop_t) == 8);
    const auto field = [&](size_t offset) { return mdl.read<int>(offset); };
    MdlFlexData flex;
    flex.descs = ReadNames(mdl, field(offsetof(studiohdr_t, numflexdesc)), field(offsetof(studiohdr_t, flexdescindex)),
                           sizeof(mstudioflexdesc_t), offsetof(mstudioflexdesc_t, szFACSindex));
    const int controllerCount = field(offsetof(studiohdr_t, numflexcontrollers));
    const auto controllers = mdl.table(0, field(offsetof(studiohdr_t, flexcontrollerindex)), controllerCount,
                                       sizeof(mstudioflexcontroller_t));
    for (int i = 0; i < controllerCount; ++i)
    {
        const auto record = controllers + i * sizeof(mstudioflexcontroller_t);
        FlexController controller;
        controller.name = mdl.string(mdl.table(record, mdl.read<int>(record + offsetof(mstudioflexcontroller_t, sznameindex)), 1, 1));
        controller.type = mdl.string(mdl.table(record, mdl.read<int>(record + offsetof(mstudioflexcontroller_t, sztypeindex)), 1, 1));
        controller.min = mdl.read<float>(record + offsetof(mstudioflexcontroller_t, min));
        controller.max = mdl.read<float>(record + offsetof(mstudioflexcontroller_t, max));
        if (!std::isfinite(controller.min) || !std::isfinite(controller.max))
            mdl.fail(QStringLiteral("invalid flex controller range"));
        flex.controllers.push_back(controller);
    }
    const int ruleCount = field(offsetof(studiohdr_t, numflexrules));
    const auto rules = mdl.table(0, field(offsetof(studiohdr_t, flexruleindex)), ruleCount, sizeof(mstudioflexrule_t));
    for (int i = 0; i < ruleCount; ++i)
    {
        const auto record = rules + i * sizeof(mstudioflexrule_t);
        FlexRule rule;
        rule.flex = mdl.read<int>(record + offsetof(mstudioflexrule_t, flex));
        const int opCount = mdl.read<int>(record + offsetof(mstudioflexrule_t, numops));
        const auto ops = mdl.table(record, mdl.read<int>(record + offsetof(mstudioflexrule_t, opindex)), opCount, sizeof(mstudioflexop_t));
        if (rule.flex < 0 || rule.flex >= flex.descs.size())
            mdl.fail(QStringLiteral("flex rule writes outside the flex descriptors"));
        int depth = 0;
        const auto require = [&](bool ok) {
            if (!ok)
                mdl.fail(QStringLiteral("malformed flex rule"));
        };
        for (int j = 0; j < opCount; ++j)
        {
            const auto opRecord = ops + j * sizeof(mstudioflexop_t);
            FlexOp op{mdl.read<uint32_t>(opRecord), mdl.read<int32_t>(opRecord + 4), mdl.read<float>(opRecord + 4)};
            const bool controllerIndex = op.index >= 0 && op.index < controllerCount;
            switch (StudioOpcode(op.op))
            {
            case StudioOpcode::Const: require(depth < 32 && std::isfinite(op.value)); ++depth; break;
            case StudioOpcode::Fetch1:
            case StudioOpcode::TwoWay0:
            case StudioOpcode::TwoWay1: require(depth < 32 && controllerIndex); ++depth; break;
            case StudioOpcode::Fetch2: require(depth < 32 && op.index >= 0 && op.index < flex.descs.size()); ++depth; break;
            case StudioOpcode::Add:
            case StudioOpcode::Sub:
            case StudioOpcode::Mul:
            case StudioOpcode::Div:
            case StudioOpcode::Max:
            case StudioOpcode::Min: require(depth >= 2); --depth; break;
            case StudioOpcode::Neg: require(depth >= 1); break;
            case StudioOpcode::Combo: require(op.index >= 1 && depth >= op.index); depth -= op.index - 1; break;
            case StudioOpcode::Dominate: require(op.index >= 1 && depth > op.index); depth -= op.index; break;
            case StudioOpcode::NWay: require(controllerIndex && depth >= 5); depth -= 4; break;
            case StudioOpcode::DmeLowerEyelid:
            case StudioOpcode::DmeUpperEyelid: require(controllerIndex && depth >= 3); depth -= 2; break;
            default: break; // Exp/Open/Close/Comma are skipped by Source's evaluator too
            }
            rule.ops.push_back(op);
        }
        flex.rules.push_back(std::move(rule));
    }
    return flex;
}

// One mesh's vertex animations; vertex indices are mesh-relative, like studiorender's mesh vertex data.
void ReadMeshFlexes(const ModelFile &mdl, size_t mesh, size_t firstVertex, int meshVertexCount, MdlFlexData &flex)
{
    static_assert(sizeof(mstudioflex_t) == 60 && sizeof(mstudiovertanim_t) == 16 && sizeof(mstudiovertanim_wrinkle_t) == 18);
    const int count = mdl.read<int>(mesh + offsetof(mstudiomesh_t, numflexes));
    const auto flexes = mdl.table(mesh, mdl.read<int>(mesh + offsetof(mstudiomesh_t, flexindex)), count, sizeof(mstudioflex_t));
    for (int i = 0; i < count; ++i)
    {
        const auto record = flexes + i * sizeof(mstudioflex_t);
        MeshFlex meshFlex;
        meshFlex.desc = mdl.read<int>(record + offsetof(mstudioflex_t, flexdesc));
        meshFlex.pair = mdl.read<int>(record + offsetof(mstudioflex_t, flexpair));
        const auto targets = mdl.read<std::array<float, 4>>(record + offsetof(mstudioflex_t, target0));
        std::copy(targets.begin(), targets.end(), meshFlex.target);
        const auto type = mdl.read<uint8_t>(record + offsetof(mstudioflex_t, vertanimtype));
        if (meshFlex.desc < 0 || meshFlex.desc >= flex.descs.size() || meshFlex.pair < 0 || meshFlex.pair >= flex.descs.size() ||
            !std::ranges::all_of(targets, finite) || type > 1)
            mdl.fail(QStringLiteral("invalid flex"));
        const size_t stride = type ? sizeof(mstudiovertanim_wrinkle_t) : sizeof(mstudiovertanim_t);
        const int vertexCount = mdl.read<int>(record + offsetof(mstudioflex_t, numverts));
        const auto anims = mdl.table(record, mdl.read<int>(record + offsetof(mstudioflex_t, vertindex)), vertexCount, stride);
        for (int j = 0; j < vertexCount; ++j)
        {
            const auto anim = anims + j * stride;
            const auto index = mdl.read<uint16_t>(anim);
            // On disk the deltas are float16 (studiorender converts them to fixed point at load time).
            const auto halves = mdl.read<std::array<qfloat16, 6>>(anim + 4);
            FlexVertex vertex{uint32_t(firstVertex + index), {halves[0], halves[1], halves[2]}, {halves[3], halves[4], halves[5]},
                              mdl.read<uint8_t>(anim + 2) / 255.f, mdl.read<uint8_t>(anim + 3) / 255.f};
            if (index >= meshVertexCount || !std::ranges::all_of(vertex.position, finite) || !std::ranges::all_of(vertex.normal, finite))
                mdl.fail(QStringLiteral("invalid flex vertex"));
            meshFlex.vertices.push_back(vertex);
        }
        flex.flexes.push_back(std::move(meshFlex));
    }
}
} // namespace

MdlMesh LoadMdlMesh(const QString &path)
{
    const ModelFile mdl(path);
    const QFileInfo file(path);
    const auto base = file.absolutePath() + QLatin1Char('/') + file.completeBaseName();
    const ModelFile vvd(base + QStringLiteral(".vvd"));
    const ModelFile vtx(base + QStringLiteral(".dx90.vtx"));
    const auto vertexHeader = vvd.read<vertexFileHeader_t>(0);
    const auto indexHeader = vtx.read<FileHeader_t>(0);
    const auto checksum = mdl.read<int>(offsetof(studiohdr_t, checksum));
    if (mdl.read<uint32_t>(0) != 0x54534449 || mdl.read<int>(4) != STUDIO_VERSION)
        mdl.fail(QStringLiteral("expected IDST MDL v49"));
    if (vertexHeader.id != MODEL_VERTEX_FILE_ID || vertexHeader.version != MODEL_VERTEX_FILE_VERSION ||
        vertexHeader.checksum != checksum || vertexHeader.numLODs <= 0 || vertexHeader.numLODs > MAX_NUM_LODS)
        vvd.fail(QStringLiteral("invalid VVD header or MDL checksum mismatch"));
    if (indexHeader.version != OPTIMIZED_MODEL_FILE_VERSION || indexHeader.checkSum != checksum || indexHeader.numLODs <= 0)
        vtx.fail(QStringLiteral("invalid VTX header or MDL checksum mismatch"));
    const bool rigid = mdl.read<uint32_t>(offsetof(studiohdr_t, flags)) & STUDIOHDR_FLAGS_STATIC_PROP;
    // shortcut: bake the default pose once; retain skeletal data when adding sequence playback.
    const auto skinMatrices = rigid ? std::vector<QMatrix4x4>{} : ReadDefaultSkinMatrices(mdl);

    MdlMesh result{};
    result.hasTangents = vertexHeader.tangentDataStart != 0;
    result.vertices = ReadVertices(vvd, vertexHeader, skinMatrices, result.skinLinear);
    result.flex = ReadFlexHeader(mdl);
    const int textureCount = mdl.read<int>(offsetof(studiohdr_t, numtextures));
    const auto textures = mdl.table(0, mdl.read<int>(offsetof(studiohdr_t, textureindex)), textureCount, sizeof(mstudiotexture_t));
    const int pathCount = mdl.read<int>(offsetof(studiohdr_t, numcdtextures));
    const auto paths = mdl.table(0, mdl.read<int>(offsetof(studiohdr_t, cdtextureindex)), pathCount, sizeof(int));
    for (int i = 0; i < pathCount; ++i)
        result.materialPaths.append(mdl.string(mdl.read<int>(paths + i * sizeof(int))));
    result.materialPaths.append(QString());
    for (int i = 0; i < textureCount; ++i)
    {
        const auto record = textures + i * sizeof(mstudiotexture_t);
        result.textures.append(mdl.string(mdl.table(record, mdl.read<int>(record), 1, 1)));
    }
    // Skin family 0 is the default; mesh materials index into this remap table.
    const int skinCount = mdl.read<int>(offsetof(studiohdr_t, numskinref));
    if (mdl.read<int>(offsetof(studiohdr_t, numskinfamilies)) < 1)
        mdl.fail(QStringLiteral("missing default skin"));
    const auto skins = mdl.table(0, mdl.read<int>(offsetof(studiohdr_t, skinindex)), skinCount, sizeof(int16_t));
    const int bodyCount = mdl.read<int>(offsetof(studiohdr_t, numbodyparts));
    if (bodyCount != indexHeader.numBodyParts)
        vtx.fail(QStringLiteral("body part count does not match MDL"));
    const auto bodies = mdl.table(0, mdl.read<int>(offsetof(studiohdr_t, bodypartindex)), bodyCount, sizeof(mstudiobodyparts_t));
    const auto vtxBodies = vtx.table(0, indexHeader.bodyPartOffset, bodyCount, sizeof(BodyPartHeader_t));
    for (int bodyID = 0; bodyID < bodyCount; ++bodyID)
    {
        const auto body = bodies + bodyID * sizeof(mstudiobodyparts_t);
        const auto vtxBody = vtxBodies + bodyID * sizeof(BodyPartHeader_t);
        const auto bodyHeader = vtx.read<BodyPartHeader_t>(vtxBody);
        const auto modelCount = mdl.read<int>(body + offsetof(mstudiobodyparts_t, nummodels));
        if (modelCount <= 0 || modelCount != bodyHeader.numModels)
            vtx.fail(QStringLiteral("model count does not match MDL"));
        // shortcut: preview bodygroup selection 0 and LOD 0; add selectors when the viewer exposes those controls.
        const auto model = mdl.table(body, mdl.read<int>(body + offsetof(mstudiobodyparts_t, modelindex)), modelCount, sizeof(mstudiomodel_t));
        const auto vtxModel = vtx.table(vtxBody, bodyHeader.modelOffset, modelCount, sizeof(ModelHeader_t));
        const auto modelHeader = vtx.read<ModelHeader_t>(vtxModel);
        if (modelHeader.numLODs <= 0)
            vtx.fail(QStringLiteral("missing LOD 0"));
        const auto lod = vtx.read<ModelLODHeader_t>(vtx.table(vtxModel, modelHeader.lodOffset, modelHeader.numLODs, sizeof(ModelLODHeader_t)));
        const int meshCount = mdl.read<int>(model + offsetof(mstudiomodel_t, nummeshes));
        const int vertexBytes = mdl.read<int>(model + offsetof(mstudiomodel_t, vertexindex));
        const int modelVertices = mdl.read<int>(model + offsetof(mstudiomodel_t, numvertices));
        const size_t modelFirst = size_t(vertexBytes) / sizeof(mstudiovertex_t);
        if (meshCount != lod.numMeshes || vertexBytes < 0 || vertexBytes % int(sizeof(mstudiovertex_t)) ||
            modelVertices < 0 || modelFirst > result.vertices.size() || size_t(modelVertices) > result.vertices.size() - modelFirst)
            mdl.fail(QStringLiteral("invalid model vertex or mesh range"));
        const auto meshes = mdl.table(model, mdl.read<int>(model + offsetof(mstudiomodel_t, meshindex)), meshCount, sizeof(mstudiomesh_t));
        const auto vtxMeshes = vtx.table(vtxModel + modelHeader.lodOffset, lod.meshOffset, meshCount, sizeof(MeshHeader_t));
        for (int meshID = 0; meshID < meshCount; ++meshID)
        {
            const auto mesh = meshes + meshID * sizeof(mstudiomesh_t);
            const int first = mdl.read<int>(mesh + offsetof(mstudiomesh_t, vertexoffset));
            const int count = mdl.read<int>(mesh + offsetof(mstudiomesh_t, numvertices));
            if (first < 0 || count < 0 || first > modelVertices || count > modelVertices - first)
                mdl.fail(QStringLiteral("mesh vertices outside model"));
            const auto indexStart = result.indices.size();
            AppendMeshIndices(vtx, vtxMeshes + meshID * sizeof(MeshHeader_t), modelFirst + first, count, result.indices);
            ReadMeshFlexes(mdl, mesh, modelFirst + first, count, result.flex);
            if (result.indices.size() != indexStart)
            {
                const int slot = mdl.read<int>(mesh + offsetof(mstudiomesh_t, material));
                if (slot < 0 || slot >= skinCount)
                    mdl.fail(QStringLiteral("material slot outside default skin"));
                const int texture = mdl.read<int16_t>(skins + slot * sizeof(int16_t));
                if (texture < 0 || texture >= textureCount)
                    mdl.fail(QStringLiteral("material outside texture table"));
                result.batches.push_back({indexStart, result.indices.size() - indexStart, texture});
            }
        }
    }
    if (result.indices.empty())
        mdl.fail(QStringLiteral("no drawable triangles"));

    std::array<float, 3> mins, maxs;
    std::copy_n(result.vertices[result.indices.front()].position, 3, mins.begin());
    maxs = mins;
    for (const auto index : result.indices)
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto value = result.vertices[index].position[axis];
            mins[axis] = std::min(mins[axis], value);
            maxs[axis] = std::max(maxs[axis], value);
        }
    const QVector3D lo(mins[0], mins[1], mins[2]);
    const QVector3D hi(maxs[0], maxs[1], maxs[2]);
    result.center = 0.5f * lo + 0.5f * hi;
    result.radius = (0.5f * hi - 0.5f * lo).length();
    if (!std::isfinite(result.radius) || result.radius < 1e-6f)
        mdl.fail(QStringLiteral("invalid or degenerate mesh bounds"));
    result.info = ReadInfo(mdl);
    return result;
}
