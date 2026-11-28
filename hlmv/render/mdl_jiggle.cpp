#include "mdl_jiggle.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "studio.h"

static_assert(sizeof(JiggleParams) == sizeof(mstudiojigglebone_t));

namespace
{
[[noreturn]] void Fail(const QString &message) { throw std::runtime_error(message.toStdString()); }

const studiohdr_t *Header(const QByteArray &mdl)
{
    if (mdl.size() < qsizetype(sizeof(studiohdr_t)))
        Fail(QStringLiteral("truncated MDL header"));
    const auto *hdr = reinterpret_cast<const studiohdr_t *>(mdl.constData());
    if (hdr->id != IDSTUDIOHEADER || hdr->version != STUDIO_VERSION || hdr->length != mdl.size() || mdl.size() % 4)
        Fail(QStringLiteral("expected an intact IDST MDL v49"));
    if (hdr->numbones < 0 || hdr->numbones > MAXSTUDIOBONES || hdr->boneindex < 0 ||
        hdr->boneindex + qint64(hdr->numbones) * qint64(sizeof(mstudiobone_t)) > mdl.size())
        Fail(QStringLiteral("bone table outside the MDL"));
    return hdr;
}

size_t BoneOffset(const studiohdr_t *hdr, int bone) { return size_t(hdr->boneindex) + size_t(bone) * sizeof(mstudiobone_t); }

bool IsJiggle(const mstudiobone_t &bone) { return bone.procindex != 0 && bone.proctype == ProcBoneType::Jiggle; }
} // namespace

std::vector<JiggleState> ReadJiggleStates(const QByteArray &mdl)
{
    const auto *hdr = Header(mdl);
    std::vector<JiggleState> states(hdr->numbones);
    for (int i = 0; i < hdr->numbones; ++i)
        if (const auto *bone = hdr->pBone(i); bone->procindex)
            states[i] = IsJiggle(*bone) ? JiggleState::Jiggle : JiggleState::OtherProcedural;
    return states;
}

std::vector<std::optional<JiggleParams>> ReadJiggleBones(const QByteArray &mdl)
{
    const auto *hdr = Header(mdl);
    std::vector<std::optional<JiggleParams>> rules(hdr->numbones);
    for (int i = 0; i < hdr->numbones; ++i)
        if (const auto *bone = hdr->pBone(i); IsJiggle(*bone))
        {
            const auto offset = qint64(BoneOffset(hdr, i)) + bone->procindex;
            if (offset < 0 || offset + qint64(sizeof(JiggleParams)) > mdl.size())
                Fail(QStringLiteral("jiggle rule of bone %1 outside the MDL").arg(i));
            std::memcpy(&rules[i].emplace(), mdl.constData() + offset, sizeof(JiggleParams));
        }
    return rules;
}

bool ApplyJiggle(std::byte *mdl, JiggleStorage &storage, int bone, const std::optional<JiggleParams> &params)
{
    auto *hdr = reinterpret_cast<studiohdr_t *>(mdl);
    if (bone < 0 || bone >= hdr->numbones)
        Fail(QStringLiteral("bone %1 out of range").arg(bone));
    auto &target = const_cast<mstudiobone_t &>(*hdr->pBone(bone));
    const auto boneOffset = BoneOffset(hdr, bone);
    const bool jiggle = IsJiggle(target);
    if (target.procindex && !jiggle)
        return false;
    // studiomdl marks every procedural bone so bone setup always computes it.
    const auto procedural = [&](bool on) {
        const auto set = [&](BoneFlags &flags) { flags = on ? flags | BoneFlags::AlwaysProcedural : flags & ~BoneFlags::AlwaysProcedural; };
        set(target.flags);
        if (auto *linear = hdr->pLinearBones())
            set(*linear->pflags(bone));
    };
    if (!params)
    {
        if (jiggle)
        {
            const size_t slot = boneOffset + target.procindex;
            std::memset(mdl + slot, 0, sizeof(JiggleParams));
            storage.freed.push_back(slot);
            target.proctype = {};
            target.procindex = 0;
            procedural(false);
        }
        return true;
    }
    size_t slot;
    if (jiggle)
        slot = boneOffset + target.procindex;
    else if (!storage.freed.empty())
    {
        slot = storage.freed.back();
        storage.freed.pop_back();
    }
    else
    {
        if (storage.end + sizeof(JiggleParams) > storage.capacity)
            Fail(QStringLiteral("no room for another jiggle bone"));
        slot = storage.end;
        storage.end += sizeof(JiggleParams);
    }
    std::memcpy(mdl + slot, &*params, sizeof(JiggleParams));
    target.proctype = ProcBoneType::Jiggle;
    target.procindex = int(qint64(slot) - qint64(boneOffset));
    procedural(true);
    return true;
}

int32_t MdlChecksum(const QByteArray &mdl)
{
    constexpr size_t field = 8; // studiohdr_t::checksum, after id and version
    uint32_t sum = 0;
    for (size_t offset = 0; offset + 4 <= size_t(mdl.size()); offset += 4)
    {
        uint32_t word = sum; // studiomdl updates the field every step, so its own word reads the running sum
        if (offset != field)
            std::memcpy(&word, mdl.constData() + offset, sizeof(word));
        sum = (sum << 1) + ((sum & 0x8000000) ? 1 : 0) + word;
    }
    return int32_t(sum);
}

QByteArray RebuildMdlJiggles(QByteArray mdl, const JiggleEdits &edits)
{
    const auto states = ReadJiggleStates(mdl);
    qint64 added = 0;
    for (const auto &[bone, params] : edits)
    {
        if (bone < 0 || size_t(bone) >= states.size())
            Fail(QStringLiteral("bone %1 out of range").arg(bone));
        if (states[bone] == JiggleState::OtherProcedural)
            Fail(QStringLiteral("bone %1 already has another procedural rule").arg(bone));
        added += params && states[bone] == JiggleState::None ? 1 : !params && states[bone] == JiggleState::Jiggle ? -1 : 0;
    }
    const auto length = mdl.size();
    if (added > 0)
        mdl.resize(length + added * qsizetype(sizeof(JiggleParams)), '\0');
    JiggleStorage storage{size_t(length), size_t(mdl.size()), {}};
    auto *data = reinterpret_cast<std::byte *>(mdl.data());
    for (bool removing : {true, false}) // removals first, so their slots take the new rules
        for (const auto &[bone, params] : edits)
            if (params.has_value() != removing)
                ApplyJiggle(data, storage, bone, params);
    auto *hdr = reinterpret_cast<studiohdr_t *>(data);
    hdr->length = int(mdl.size());
    hdr->checksum = MdlChecksum(mdl);
    return mdl;
}

QStringList SaveMdlJiggles(const QString &mdlPath, const JiggleEdits &edits)
{
    struct Output
    {
        QString path;
        QByteArray bytes;
    };
    const auto read = [](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            Fail(path + QStringLiteral(": ") + file.errorString());
        return file.readAll();
    };
    const auto original = read(mdlPath);
    const auto oldChecksum = Header(original)->checksum;
    std::vector<Output> outputs{{mdlPath, RebuildMdlJiggles(original, edits)}};
    const auto newChecksum = reinterpret_cast<const studiohdr_t *>(outputs.front().bytes.constData())->checksum;

    // Companion files beside the model, matched case-insensitively like the SDK filesystem.
    const QFileInfo info(mdlPath);
    const auto stem = info.completeBaseName();
    QStringList notes;
    struct Companion
    {
        const char *suffix;
        qsizetype offset; // of the checksum field
        bool required;
    };
    const Companion companions[] = {
        {".vvd", 8, true}, {".dx90.vtx", 16, true}, {".dx80.vtx", 16, false}, // vertexFileHeader_t / FileHeader_t
        {".sw.vtx", 16, false}, {".vtx", 16, false}, {".xbox.vtx", 16, false}, {".phy", 12, false}};
    const auto entries = info.absoluteDir().entryList(QDir::Files);
    for (const auto &companion : companions)
    {
        const auto wanted = stem + QLatin1String(companion.suffix);
        const auto match = std::ranges::find_if(entries, [&](const QString &name) { return name.compare(wanted, Qt::CaseInsensitive) == 0; });
        if (match == entries.end())
        {
            if (companion.required)
                Fail(wanted + QStringLiteral(" is missing"));
            continue;
        }
        const auto path = info.absoluteDir().filePath(*match);
        auto bytes = read(path);
        int32_t checksum = 0;
        if (bytes.size() >= companion.offset + 4)
            std::memcpy(&checksum, bytes.constData() + companion.offset, 4);
        if (bytes.size() < companion.offset + 4 || checksum != oldChecksum)
        {
            if (companion.required)
                Fail(*match + QStringLiteral(" does not match the MDL checksum; not written"));
            notes.append(QStringLiteral("skipped stale ") + *match);
            continue;
        }
        std::memcpy(bytes.data() + companion.offset, &newChecksum, 4);
        outputs.push_back({path, std::move(bytes)});
    }

    // Everything is staged before the first commit, so a failure while preparing leaves every file as it was.
    std::vector<std::unique_ptr<QSaveFile>> files;
    for (const auto &output : outputs)
    {
        auto &file = files.emplace_back(std::make_unique<QSaveFile>(output.path));
        if (!file->open(QIODevice::WriteOnly) || file->write(output.bytes) != output.bytes.size())
            Fail(output.path + QStringLiteral(": ") + file->errorString());
    }
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (!files[i]->commit())
            Fail(outputs[i].path + QStringLiteral(": ") + files[i]->errorString());
        notes.prepend(QStringLiteral("wrote ") + QFileInfo(outputs[i].path).fileName());
    }
    return notes;
}
