#include "mdl_mesh.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <span>

#include <QFileInfo>
#include <QMatrix4x4>
#include <QQuaternion>

#include "game_filesystem.h"
#include "studiomdl/optimize.h"

namespace {
    using namespace OptimizedModel;
    static_assert(std::endian::native == std::endian::little);
    static_assert(sizeof(mstudiovertex_t) == 48 && sizeof(mstudiomodel_t) == 148 && sizeof(mstudiomesh_t) == 116);
    static_assert(sizeof(StripGroupHeader_t) == 33 && sizeof(StripHeader_t) == 35);
    static_assert(sizeof(mstudiotexture_t) == 64 && sizeof(mstudiobbox_t) == 68);
    static_assert(sizeof(mstudiobone_t) == 216 && sizeof(mstudioboneweight_t) == 16);
    static_assert(
        sizeof(mstudioflexcontroller_t) == 20 && sizeof(mstudioflexrule_t) == 12 && sizeof(mstudioflexop_t) == 8);
    static_assert(
        sizeof(mstudioflex_t) == 60 && sizeof(mstudiovertanim_t) == 16 && sizeof(mstudiovertanim_wrinkle_t) == 18);

    // One model file read whole into 16-byte aligned memory, so Valve's in-place structs (studiohdr_t,
    // vertexFileHeader_t, OptimizedModel::FileHeader_t) are traversed with their own accessors, as the engine does.
    // The file is untrusted: every record an accessor returns passes Check before it is read.
    class StudioFile {
    public:
        explicit StudioFile(const QString &path) : m_path(path) {
            const auto bytes = ReadAssetFile(path, 128 * 1024 * 1024);
            m_size = size_t(bytes.size());
            m_data.reset(static_cast<std::byte*>(::operator new[](m_size, std::align_val_t(16))));
            std::memcpy(m_data.get(), bytes.constData(), m_size);
        }

        template<class T>
        T* header() const { return Check(reinterpret_cast<T*>(m_data.get())); }

        // `count` records starting at `p`, which must lie inside the file.
        template<class T>
        T* Check(T* p, const int count = 1) const {
            const auto offset = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(m_data.get());
            if (count < 0 || offset > m_size || size_t(count) > (m_size - offset) / sizeof(T))
                Fail(QStringLiteral("record outside file bounds"));
            return p;
        }

        // `count` records at at(0)..at(count - 1); accessors are only called for a non-empty table (they assert i < count).
        template<class At>
        void CheckTable(const int count, At at) const {
            if (count)
                Check(at(0), count);
        }

        QString String(const char* s) const {
            const auto offset = reinterpret_cast<uintptr_t>(s) - reinterpret_cast<uintptr_t>(m_data.get());
            if (offset >= m_size)
                Fail(QStringLiteral("string outside file bounds"));
            const auto* end = static_cast<const char*>(std::memchr(s, 0, m_size - offset));
            if (!end)
                Fail(QStringLiteral("unterminated string"));
            return QString::fromUtf8(s, end - s);
        }

        // Names of a checked table's records.
        template<class At, class Name>
        QStringList Names(const int count, At at, Name name) const {
            CheckTable(count, at);
            QStringList names;
            for (int i = 0; i < count; ++i)
                names.append(String(name(at(i))));
            return names;
        }

        [[noreturn]] void Fail(const QString &reason) const { ThrowAssetError(m_path, reason); }

    private:
        struct AlignedDelete {
            void operator()(std::byte* p) const { ::operator delete[](p, std::align_val_t(16)); }
        };

        QString m_path;
        std::unique_ptr<std::byte[], AlignedDelete> m_data;
        size_t m_size = 0;
    };

    // Every float of a Vector, QAngle, Quaternion, matrix3x4_t, ...
    template<class T>
    bool Finite(const T &value) {
        static_assert(sizeof(T) % sizeof(float) == 0);
        const auto* f = reinterpret_cast<const float*>(&value);
        return std::all_of(f, f + sizeof(T) / sizeof(float), [](const float x) { return std::isfinite(x); });
    }

    QVector3D ToQt(const Vector &v) { return {v.x, v.y, v.z}; }

    QMatrix4x4 ToQt(const matrix3x4_t &m) {
        return QMatrix4x4(m[0][0], m[0][1], m[0][2], m[0][3],
                          m[1][0], m[1][1], m[1][2], m[1][3],
                          m[2][0], m[2][1], m[2][2], m[2][3],
                          0, 0, 0, 1);
    }

    struct Skeleton {
        std::vector<QMatrix4x4> boneToModel, modelToBone;
        std::vector<int> parents;
    };

    // Rest pose from mstudiobone_t pos/quat, plus the bind pose (poseToBone).
    Skeleton ReadSkeleton(const StudioFile &mdl, const studiohdr_t* hdr) {
        const int count = hdr->numbones;
        if (count < 1 || count > int(MAXSTUDIOBONES))
            mdl.Fail(QStringLiteral("invalid studio bone count"));
        mdl.CheckTable(count, [&](int i) { return hdr->pBone(i); });
        Skeleton skeleton;
        auto &[boneToModel, modelToBone, parents] = skeleton;
        boneToModel.resize(count);
        modelToBone.resize(count);
        parents.resize(count);
        for (int i = 0; i < count; ++i) {
            const auto* bone = hdr->pBone(i);
            if (bone->parent < -1 || bone->parent >= i)
                mdl.Fail(QStringLiteral("invalid studio bone parent"));
            parents[i] = bone->parent;
            const QQuaternion rotation(bone->quat.w, bone->quat.x, bone->quat.y, bone->quat.z);
            const float length = rotation.lengthSquared();
            if (!Finite(bone->pos) || !Finite(bone->poseToBone) || !std::isfinite(length) || length < 1e-8f)
                mdl.Fail(QStringLiteral("invalid studio bone transform"));

            auto &world = boneToModel[i];
            world.translate(ToQt(bone->pos));
            world.rotate(rotation.normalized());
            if (bone->parent >= 0)
                world = boneToModel[bone->parent] * world;
            modelToBone[i] = ToQt(bone->poseToBone);
            if (!std::ranges::all_of(std::span((world * modelToBone[i]).constData(), 16),
                                     [](float x) { return std::isfinite(x); }))
                mdl.Fail(QStringLiteral("studio bone transform overflow"));
        }
        return skeleton;
    }

    std::vector<Vertex> ReadVertices(const StudioFile &vvd, const vertexFileHeader_t* header, const int boneCount,
                                     std::vector<VertexWeights> &weightsOut) {
        const int count = header->numLODVertexes[0];
        const auto* data = vvd.Check(header->GetVertexData(), count);
        const auto* tangents = header->tangentDataStart ? vvd.Check(header->GetTangentData(), count) : nullptr;
        std::vector<Vertex> vertices;
        vertices.reserve(count);
        const auto append = [&](const int first, const int length) {
            if (first < 0 || length < 0 || first > count || length > count - first ||
                size_t(length) > size_t(count) - vertices.size())
                vvd.Fail(QStringLiteral("invalid LOD 0 vertex fixup"));
            for (int i = first; i < first + length; ++i) {
                const auto &in = data[i];
                const Vector4D tangent = tangents ? tangents[i] : Vector4D(0, 0, 0, 0);
                if (boneCount) {
                    const auto &weights = in.m_BoneWeights;
                    if (weights.numbones == 0 || weights.numbones > MAX_NUM_BONES_PER_VERT)
                        vvd.Fail(QStringLiteral("invalid vertex bone count"));
                    VertexWeights out{};
                    float total = 0;
                    for (int j = 0; j < weights.numbones; ++j) {
                        const auto bone = weights.bone[j];
                        const auto weight = weights.weight[j];
                        // studiomdl writes some single-bone weights as 1.0000001
                        if (bone >= boneCount || !std::isfinite(weight) || weight < 0 || weight > 1.001f)
                            vvd.Fail(QStringLiteral("invalid vertex bone influence"));
                        if (weight == 0)
                            continue;
                        out.bone[out.count] = bone;
                        out.weight[out.count++] = weight;
                        total += weight;
                    }
                    if (std::abs(total - 1) > 0.01f)
                        vvd.Fail(QStringLiteral("vertex bone weights do not sum to one"));
                    for (int j = 0; j < out.count; ++j)
                        out.weight[j] /= total;
                    weightsOut.push_back(out);
                }
                if (!Finite(in.m_vecPosition) || !Finite(in.m_vecNormal) || !Finite(in.m_vecTexCoord) || !Finite(
                        tangent))
                    vvd.Fail(QStringLiteral("non-finite vertex data"));
                const auto &p = in.m_vecPosition, &n = in.m_vecNormal;
                vertices.push_back({
                    {p.x, p.y, p.z, 1}, {in.m_vecTexCoord.x, in.m_vecTexCoord.y}, {n.x, n.y, n.z},
                    {tangent.x, tangent.y, tangent.z, tangent.w}
                });
            }
        };
        if (!header->numFixups)
            append(0, count);
        else {
            const auto* fixups = vvd.Check(reinterpret_cast<const vertexFileFixup_t*>(
                                               reinterpret_cast<const std::byte*>(header) + header->fixupTableStart),
                                           header->numFixups);
            for (int i = 0; i < header->numFixups; ++i) {
                if (fixups[i].lod < 0 || fixups[i].lod >= header->numLODs)
                    vvd.Fail(QStringLiteral("invalid fixup LOD"));
                append(fixups[i].sourceVertexID, fixups[i].numVertexes);
            }
            if (vertices.size() != size_t(count))
                vvd.Fail(QStringLiteral("fixups do not cover LOD 0"));
        }
        return vertices;
    }

    void AppendMeshIndices(const StudioFile &vtx, const MeshHeader_t* mesh, const uint32_t firstVertex,
                           const int meshVertexCount, std::vector<uint32_t> &indices) {
        vtx.CheckTable(mesh->numStripGroups, [&](int i) { return mesh->pStripGroup(i); });
        for (int groupID = 0; groupID < mesh->numStripGroups; ++groupID) {
            const auto* group = mesh->pStripGroup(groupID);
            vtx.CheckTable(group->numVerts, [&](int i) { return group->pVertex(i); });
            vtx.CheckTable(group->numIndices, [&](int i) { return group->pIndex(i); });
            vtx.CheckTable(group->numStrips, [&](int i) { return group->pStrip(i); });
            if (group->numTopologyIndices != 0)
                vtx.Fail(QStringLiteral("subdivision topology is not supported"));
            for (int stripID = 0; stripID < group->numStrips; ++stripID) {
                const auto* strip = group->pStrip(stripID);
                if (strip->flags != STRIP_IS_TRILIST || strip->numIndices < 0 || strip->numIndices % 3 ||
                    strip->indexOffset < 0 || strip->indexOffset > group->numIndices ||
                    strip->numIndices > group->numIndices - strip->indexOffset)
                    vtx.Fail(QStringLiteral("invalid or unsupported VTX triangle list"));
                for (int i = 0; i < strip->numIndices; ++i) {
                    const auto index = *group->pIndex(strip->indexOffset + i);
                    if (index >= group->numVerts)
                        vtx.Fail(QStringLiteral("index outside strip group's vertex table"));
                    const auto vertex = group->pVertex(index)->origMeshVertID;
                    if (vertex >= meshVertexCount)
                        vtx.Fail(QStringLiteral("vertex outside MDL mesh"));
                    indices.push_back(firstVertex + vertex);
                }
            }
        }
    }

    MdlInfo ReadInfo(const StudioFile &mdl, const studiohdr_t* hdr) {
        MdlInfo info;
        const auto bone = [&](int i) { return hdr->pBone(i); };
        info.bones = mdl.Names(hdr->numbones, bone, [](auto* b) { return b->pszName(); });
        info.surfaceProps = mdl.Names(hdr->numbones, bone, [](auto* b) { return b->pszSurfaceProp(); });
        info.attachments = mdl.Names(hdr->numlocalattachments, [&](int i) { return hdr->pLocalAttachment(i); },
                                     [](auto* a) { return a->pszName(); });
        info.bodyParts = mdl.Names(hdr->numbodyparts, [&](int i) { return hdr->pBodypart(i); },
                                   [](auto* b) { return b->pszName(); });
        info.sequences = mdl.Names(hdr->numlocalseq, [&](int i) { return hdr->pLocalSeqdesc(i); },
                                   [](auto* s) { return s->pszLabel(); });
        info.autoHitboxes = hdr->flags & STUDIOHDR_FLAGS_AUTOGENERATED_HITBOX;
        for (int part = 0; part < hdr->numbodyparts; ++part) {
            const auto* body = hdr->pBodypart(part);
            mdl.CheckTable(body->nummodels, [&](int i) { return body->pModel(i); });
            auto &names = info.bodyModels.emplace_back();
            for (int i = 0; i < body->nummodels; ++i) {
                const auto* model = body->pModel(i);
                names.append(QString::fromUtf8(model->name, qsizetype(strnlen(model->name, sizeof(model->name)))));
            }
        }
        info.boneControllers = hdr->numbonecontrollers;
        info.skinFamilies = hdr->numskinfamilies;
        info.hitboxSets = hdr->numhitboxsets;
        mdl.CheckTable(hdr->numhitboxsets, [&](int i) { return hdr->pHitboxSet(i); });
        for (int i = 0; i < hdr->numhitboxsets; ++i)
            info.hitboxes += hdr->pHitboxSet(i)->numhitboxes;
        info.hullMin = ToQt(hdr->hull_min);
        info.hullMax = ToQt(hdr->hull_max);
        return info;
    }

    // Attachments and hitbox sets in the space the vertices were baked into.
    void ReadOverlays(const StudioFile &mdl, const studiohdr_t* hdr, const int boneCount, MdlMesh &result) {
        mdl.CheckTable(hdr->numlocalattachments, [&](int i) { return hdr->pLocalAttachment(i); });
        for (int i = 0; i < hdr->numlocalattachments; ++i) {
            const auto* attachment = hdr->pLocalAttachment(i);
            if (attachment->localbone < 0 || attachment->localbone >= boneCount)
                mdl.Fail(QStringLiteral("invalid attachment"));
            // Some shipped models (L4D2 survivor_coach) carry NaN rotations from studiomdl; the engine uses them as is,
            // so only that attachment is broken there. Here the bad entries take the identity's value instead.
            auto local = ToQt(attachment->local);
            for (int k = 0; k < 16; ++k)
                if (!std::isfinite(local.data()[k]))
                    local.data()[k] = k % 5 == 0 ? 1.f : 0.f;
            result.attachments.push_back({attachment->localbone, local});
        }

        mdl.CheckTable(hdr->numhitboxsets, [&](int i) { return hdr->pHitboxSet(i); });
        for (int s = 0; s < hdr->numhitboxsets; ++s) {
            const auto* set = hdr->pHitboxSet(s);
            auto &hitboxSet = result.hitboxSets.emplace_back();
            hitboxSet.name = mdl.String(set->pszName());
            mdl.CheckTable(set->numhitboxes, [&](int i) { return set->pHitbox(i); });
            for (int i = 0; i < set->numhitboxes; ++i) {
                const auto* bbox = set->pHitbox(i);
                if (bbox->bone < 0 || bbox->bone >= boneCount || !Finite(bbox->bbmin) || !Finite(bbox->bbmax) ||
                    !Finite(bbox->angOffsetOrientation) || !std::isfinite(bbox->flCapsuleRadius))
                    mdl.Fail(QStringLiteral("invalid hitbox"));
                const auto &angles = bbox->angOffsetOrientation;
                MdlHitbox box{
                    bbox->bone, bbox->group, ToQt(bbox->bbmin), ToQt(bbox->bbmax), {angles.x, angles.y, angles.z},
                    bbox->flCapsuleRadius
                };
                if (bbox->szhitboxnameindex)
                    box.name = mdl.String(bbox->pszHitboxName());
                hitboxSet.boxes.push_back(std::move(box));
            }
        }
    }

    // Flex descriptors, controllers and rules (studiohdr_t), with every rule's stack use checked up front so
    // RunFlexRules can index without bounds checks except for stack-supplied controller indices.
    MdlFlexData ReadFlexHeader(const StudioFile &mdl, const studiohdr_t* hdr) {
        MdlFlexData flex;
        flex.descs = mdl.Names(hdr->numflexdesc, [&](int i) { return hdr->pFlexdesc(i); },
                               [](auto* d) { return d->pszFACS(); });
        const int controllerCount = hdr->numflexcontrollers;
        const auto controllerAt = [&](int i) { return hdr->pFlexcontroller(LocalFlexController_t(i)); };
        mdl.CheckTable(controllerCount, controllerAt);
        for (int i = 0; i < controllerCount; ++i) {
            const auto* record = controllerAt(i);
            if (!std::isfinite(record->min) || !std::isfinite(record->max))
                mdl.Fail(QStringLiteral("invalid flex controller range"));
            flex.controllers.push_back({
                mdl.String(record->pszName()), mdl.String(record->pszType()), record->min, record->max
            });
        }
        mdl.CheckTable(hdr->numflexrules, [&](int i) { return hdr->pFlexRule(i); });
        for (int i = 0; i < hdr->numflexrules; ++i) {
            const auto* record = hdr->pFlexRule(i);
            FlexRule rule;
            rule.flex = record->flex;
            mdl.CheckTable(record->numops, [&](int j) { return record->iFlexOp(j); });
            if (rule.flex < 0 || rule.flex >= flex.descs.size())
                mdl.Fail(QStringLiteral("flex rule writes outside the flex descriptors"));
            int depth = 0;
            const auto require = [&](const bool ok) {
                if (!ok)
                    mdl.Fail(QStringLiteral("malformed flex rule"));
            };
            for (int j = 0; j < record->numops; ++j) {
                const auto* opRecord = record->iFlexOp(j);
                const FlexOp op{.op = opRecord->op, .index = opRecord->d.index, .value = opRecord->d.value};
                const bool controllerIndex = op.index >= 0 && op.index < controllerCount;
                switch (op.op) {
                    case StudioOpcode::Const: require(depth < 32 && std::isfinite(op.value));
                        ++depth;
                        break;
                    case StudioOpcode::Fetch1:
                    case StudioOpcode::TwoWay0:
                    case StudioOpcode::TwoWay1:
                        require(depth < 32 && controllerIndex);
                        ++depth;
                        break;
                    case StudioOpcode::Fetch2:
                        require(depth < 32 && op.index >= 0 && op.index < flex.descs.size());
                        ++depth;
                        break;
                    case StudioOpcode::Add:
                    case StudioOpcode::Sub:
                    case StudioOpcode::Mul:
                    case StudioOpcode::Div:
                    case StudioOpcode::Max:
                    case StudioOpcode::Min:
                        require(depth >= 2);
                        --depth;
                        break;
                    case StudioOpcode::Neg:
                        require(depth >= 1);
                        break;
                    case StudioOpcode::Combo:
                        require(op.index >= 1 && depth >= op.index);
                        depth -= op.index - 1;
                        break;
                    case StudioOpcode::Dominate:
                        require(op.index >= 1 && depth > op.index);
                        depth -= op.index;
                        break;
                    case StudioOpcode::NWay:
                        require(controllerIndex && depth >= 5);
                        depth -= 4;
                        break;
                    case StudioOpcode::DmeLowerEyelid:
                    case StudioOpcode::DmeUpperEyelid:
                        require(controllerIndex && depth >= 3);
                        depth -= 2;
                        break;
                    default: break; // Exp/Open/Close/Comma are skipped by Source's evaluator too
                }
                rule.ops.push_back(op);
            }
            flex.rules.push_back(std::move(rule));
        }
        return flex;
    }

    // One mesh's vertex animations; vertex indices are mesh-relative, like studiorender's mesh vertex data.
    void ReadMeshFlexes(const StudioFile &mdl, const mstudiomesh_t* mesh, const size_t firstVertex,
                        const int meshVertexCount, MdlFlexData &flex) {
        mdl.CheckTable(mesh->numflexes, [&](int i) { return mesh->pFlex(i); });
        for (int i = 0; i < mesh->numflexes; ++i) {
            const auto* record = mesh->pFlex(i);
            MeshFlex meshFlex{
                record->flexdesc, record->flexpair, {record->target0, record->target1, record->target2, record->target3}
            };
            const bool wrinkle = record->vertanimtype == StudioVertAnimType_t::Wrinkle;
            if (meshFlex.desc < 0 || meshFlex.desc >= flex.descs.size() || meshFlex.pair < 0 ||
                meshFlex.pair >= flex.descs.size() || !Finite(meshFlex.target) ||
                (!wrinkle && record->vertanimtype != StudioVertAnimType_t::Normal))
                mdl.Fail(QStringLiteral("invalid flex"));
            // Wrinkle records extend mstudiovertanim_t, so either table yields the shared fields.
            const auto anim = [&](int j) -> mstudiovertanim_t* {
                return wrinkle ? record->pVertanimWrinkle(j) : record->pVertanim(j);
            };
            if (wrinkle)
                mdl.CheckTable(record->numverts, [&](int j) { return record->pVertanimWrinkle(j); });
            else
                mdl.CheckTable(record->numverts, [&](int j) { return record->pVertanim(j); });
            for (int j = 0; j < record->numverts; ++j) {
                auto* in = anim(j);
                // On disk the deltas are float16 (studiorender converts them to fixed point at load time).
                const auto delta = in->GetDeltaFloat(), normal = in->GetNDeltaFloat();
                if (in->index >= meshVertexCount || !Finite(delta) || !Finite(normal))
                    mdl.Fail(QStringLiteral("invalid flex vertex"));
                meshFlex.vertices.push_back({
                    uint32_t(firstVertex + in->index), {delta.x, delta.y, delta.z}, {normal.x, normal.y, normal.z},
                    in->speed / 255.f, in->side / 255.f
                });
            }
            flex.flexes.push_back(std::move(meshFlex));
        }
    }
} // namespace

MdlMesh LoadMdlMesh(const QString &path) {
    const StudioFile mdl(path);
    const QFileInfo file(path);
    const auto base = file.absolutePath() + QLatin1Char('/') + file.completeBaseName();
    const StudioFile vvd(base + QStringLiteral(".vvd"));
    const StudioFile vtx(base + QStringLiteral(".dx90.vtx"));
    const auto* hdr = mdl.header<studiohdr_t>();
    const auto* vertexHeader = vvd.header<vertexFileHeader_t>();
    const auto* indexHeader = vtx.header<FileHeader_t>();
    if (hdr->id != IDSTUDIOHEADER || hdr->version != STUDIO_VERSION)
        mdl.Fail(QStringLiteral("expected IDST MDL v49"));
    if (vertexHeader->id != MODEL_VERTEX_FILE_ID || vertexHeader->version != MODEL_VERTEX_FILE_VERSION ||
        vertexHeader->checksum != hdr->checksum || vertexHeader->numLODs <= 0 || vertexHeader->numLODs > MAX_NUM_LODS)
        vvd.Fail(QStringLiteral("invalid VVD header or MDL checksum mismatch"));
    if (indexHeader->version != OPTIMIZED_MODEL_FILE_VERSION || indexHeader->checkSum != hdr->checksum ||
        indexHeader->numLODs <= 0)
        vtx.Fail(QStringLiteral("invalid VTX header or MDL checksum mismatch"));
    const bool rigid = hdr->flags & STUDIOHDR_FLAGS_STATIC_PROP;
    const auto skeleton = ReadSkeleton(mdl, hdr);

    MdlMesh result{};
    result.hasTangents = vertexHeader->tangentDataStart != 0;
    // Static props keep bind-space vertices, so their bones sit at the inverse bind pose.
    if (rigid)
        for (const auto &bind: skeleton.modelToBone)
            result.bones.push_back(bind.inverted());
    else {
        result.bones = skeleton.boneToModel;
        result.poseToBone = skeleton.modelToBone;
    }
    result.boneParents = skeleton.parents;
    const int boneCount = int(result.bones.size());
    ReadOverlays(mdl, hdr, boneCount, result);
    result.vertices = ReadVertices(vvd, vertexHeader, rigid ? 0 : boneCount, result.weights);
    result.flex = ReadFlexHeader(mdl, hdr);
    const int textureCount = hdr->numtextures;
    result.textures = mdl.Names(textureCount, [&](int i) { return hdr->pTexture(i); },
                                [](auto* t) { return t->pszName(); });
    // pCdtexture reads its offset from this int table.
    mdl.Check(reinterpret_cast<const int*>(reinterpret_cast<const std::byte*>(hdr) + hdr->cdtextureindex),
              hdr->numcdtextures);
    for (int i = 0; i < hdr->numcdtextures; ++i)
        result.materialPaths.append(mdl.String(hdr->pCdtexture(i)));
    result.materialPaths.append(QString());
    // Skin family 0 is the default; mesh materials index into this remap table, one row per family.
    const int skinCount = hdr->numskinref;
    const int familyCount = hdr->numskinfamilies;
    if (familyCount < 1)
        mdl.Fail(QStringLiteral("missing default skin"));
    const auto* skins = mdl.Check(hdr->pSkinref(0), familyCount * skinCount);
    for (int i = 0; i < familyCount * skinCount; ++i) {
        if (skins[i] < 0 || skins[i] >= textureCount)
            mdl.Fail(QStringLiteral("skin family entry outside texture table"));
        result.skinTable.push_back(skins[i]);
    }
    result.skinRefs = skinCount;
    const int bodyCount = hdr->numbodyparts;
    if (bodyCount != indexHeader->numBodyParts)
        vtx.Fail(QStringLiteral("body part count does not match MDL"));
    mdl.CheckTable(bodyCount, [&](int i) { return hdr->pBodypart(i); });
    vtx.CheckTable(bodyCount, [&](int i) { return indexHeader->pBodyPart(i); });
    for (int bodyID = 0; bodyID < bodyCount; ++bodyID) {
        const auto* body = hdr->pBodypart(bodyID);
        const auto* vtxBody = indexHeader->pBodyPart(bodyID);
        const int modelCount = body->nummodels;
        if (modelCount <= 0 || modelCount != vtxBody->numModels)
            vtx.Fail(QStringLiteral("model count does not match MDL"));
        mdl.CheckTable(modelCount, [&](int i) { return body->pModel(i); });
        vtx.CheckTable(modelCount, [&](int i) { return vtxBody->pModel(i); });
        for (int modelID = 0; modelID < modelCount; ++modelID) {
            // shortcut: LOD 0 only; read the other LODs when the LOD choice is wired.
            auto* model = body->pModel(modelID);
            const auto* vtxModel = vtxBody->pModel(modelID);
            if (vtxModel->numLODs <= 0)
                vtx.Fail(QStringLiteral("missing LOD 0"));
            const auto* lod = vtx.Check(vtxModel->pLOD(0), vtxModel->numLODs);
            const int meshCount = model->nummeshes;
            const int modelVertices = model->numvertices;
            const size_t modelFirst = size_t(model->vertexindex) / sizeof(mstudiovertex_t);
            if (meshCount != lod->numMeshes || model->vertexindex < 0 || model->vertexindex % int(
                    sizeof(mstudiovertex_t)) ||
                modelVertices < 0 || modelFirst > result.vertices.size() ||
                size_t(modelVertices) > result.vertices.size() - modelFirst)
                mdl.Fail(QStringLiteral("invalid model vertex or mesh range"));
            mdl.CheckTable(meshCount, [&](int i) { return model->pMesh(i); });
            vtx.CheckTable(meshCount, [&](int i) { return lod->pMesh(i); });
            // studiorender's eyeball path: meshes with materialtype 1 draw eyeball[materialparam] of their model.
            const int eyeballCount = model->numeyeballs;
            mdl.CheckTable(eyeballCount, [&](int i) { return model->pEyeball(i); });
            const int firstEyeball = int(result.eyeballs.size());
            for (int i = 0; i < eyeballCount; ++i) {
                const auto* eyeball = model->pEyeball(i);
                if (eyeball->bone < 0 || eyeball->bone >= boneCount || !Finite(eyeball->org) || !Finite(eyeball->up) ||
                    !Finite(eyeball->forward) || !std::isfinite(eyeball->zoffset) || !std::isfinite(eyeball->radius) ||
                    !std::isfinite(eyeball->iris_scale) || eyeball->iris_scale == 0)
                    mdl.Fail(QStringLiteral("invalid eyeball"));
                result.eyeballs.push_back({
                    eyeball->bone, ToQt(eyeball->org), ToQt(eyeball->up), ToQt(eyeball->forward), eyeball->zoffset,
                    eyeball->radius, eyeball->iris_scale
                });
            }
            for (int meshID = 0; meshID < meshCount; ++meshID) {
                const auto* mesh = model->pMesh(meshID);
                const int first = mesh->vertexoffset;
                const int count = mesh->numvertices;
                if (first < 0 || count < 0 || first > modelVertices || count > modelVertices - first)
                    mdl.Fail(QStringLiteral("mesh vertices outside model"));
                const auto indexStart = result.indices.size();
                AppendMeshIndices(vtx, lod->pMesh(meshID), modelFirst + first, count, result.indices);
                ReadMeshFlexes(mdl, mesh, modelFirst + first, count, result.flex);
                if (result.indices.size() != indexStart) {
                    const int slot = mesh->material;
                    if (slot < 0 || slot >= skinCount)
                        mdl.Fail(QStringLiteral("material slot outside default skin"));
                    const bool eyeballMesh = mesh->materialtype == 1;
                    if (eyeballMesh && (mesh->materialparam < 0 || mesh->materialparam >= eyeballCount))
                        mdl.Fail(QStringLiteral("eyeball mesh refers to a missing eyeball"));
                    result.batches.push_back({
                        indexStart, result.indices.size() - indexStart, skins[slot], slot, bodyID, modelID, 0,
                        eyeballMesh ? firstEyeball + mesh->materialparam : -1
                    });
                }
            }
        }
    }
    if (result.indices.empty())
        mdl.Fail(QStringLiteral("no drawable triangles"));

    // Frame the rest pose; the viewer reframes once it has posed the first sequence.
    std::vector<Vertex> rest;
    if (!result.weights.empty())
        SkinVertices(result, result.vertices, result.bones, rest);
    if (!FrameVertices(result, result.weights.empty() ? result.vertices : rest))
        mdl.Fail(QStringLiteral("invalid or degenerate mesh bounds"));
    if (hdr->studiohdr2index)
        if (const float deflection = mdl.Check(hdr->pStudioHdr2())->flMaxEyeDeflection;
            std::isfinite(deflection) && deflection != 0.0f)
            result.maxEyeDeflection = deflection;
    mdl.CheckTable(hdr->nummouths, [&](int i) { return hdr->pMouth(i); });
    for (int i = 0; i < hdr->nummouths; ++i) {
        const auto *mouth = hdr->pMouth(i);
        if (mouth->bone < 0 || mouth->bone >= boneCount || !Finite(mouth->forward) || mouth->flexdesc < 0 ||
            size_t(mouth->flexdesc) >= result.flex.descs.size())
            mdl.Fail(QStringLiteral("invalid mouth"));
        result.mouths.push_back({mouth->bone, ToQt(mouth->forward), mouth->flexdesc});
    }
    result.info = ReadInfo(mdl, hdr);
    return result;
}

bool FrameVertices(MdlMesh &mesh, const std::span<const Vertex> vertices) {
    if (mesh.indices.empty())
        return false;
    std::array<float, 3> mins{}, maxs{};
    std::copy_n(vertices[mesh.indices.front()].position, 3, mins.begin());
    maxs = mins;
    for (const auto index: mesh.indices)
        for (int axis = 0; axis < 3; ++axis) {
            const auto value = vertices[index].position[axis];
            mins[axis] = std::min(mins[axis], value);
            maxs[axis] = std::max(maxs[axis], value);
        }
    const QVector3D lo(mins[0], mins[1], mins[2]);
    const QVector3D hi(maxs[0], maxs[1], maxs[2]);
    const float radius = (0.5f * hi - 0.5f * lo).length();
    if (!std::isfinite(radius) || radius < 1e-6f)
        return false;
    mesh.center = 0.5f * lo + 0.5f * hi;
    mesh.radius = radius;
    return true;
}

void SkinVertices(const MdlMesh &mesh, const std::span<const Vertex> base,
                  const std::span<const QMatrix4x4> boneToModel,
                  std::vector<Vertex> &out) {
    std::vector<QMatrix4x4> skin(boneToModel.size());
    for (size_t i = 0; i < skin.size() && i < mesh.poseToBone.size(); ++i)
        skin[i] = boneToModel[i] * mesh.poseToBone[i];
    out.resize(base.size());
    for (size_t v = 0; v < base.size(); ++v) {
        const auto &in = base[v];
        const auto &weights = mesh.weights[v];
        const QVector3D p(in.position[0], in.position[1], in.position[2]), n(in.normal[0], in.normal[1], in.normal[2]),
                t(in.tangent[0], in.tangent[1], in.tangent[2]);
        QVector3D position, normal, tangent;
        for (int j = 0; j < weights.count; ++j) {
            const auto &matrix = skin[weights.bone[j]];
            position += weights.weight[j] * matrix.map(p);
            normal += weights.weight[j] * matrix.mapVector(n);
            tangent += weights.weight[j] * matrix.mapVector(t);
        }
        normal.normalize();
        tangent.normalize();
        out[v] = {
            .position = {position.x(), position.y(), position.z(), 1},
            .uv = {in.uv[0], in.uv[1]},
            .normal = {normal.x(), normal.y(), normal.z()},
            .tangent = {tangent.x(), tangent.y(), tangent.z(), in.tangent[3]}
        };
    }
}
