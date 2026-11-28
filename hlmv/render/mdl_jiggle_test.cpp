#include "mdl_jiggle.h"
#include "studio.h"

#include <cstring>
#include <stdexcept>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace
{
void Check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}

studiohdr_t *Hdr(QByteArray &mdl) { return reinterpret_cast<studiohdr_t *>(mdl.data()); }
mstudiobone_t *Bone(QByteArray &mdl, int i) { return const_cast<mstudiobone_t *>(Hdr(mdl)->pBone(i)); }

// Three bones: 0 jiggles (length 5), 1 is plain, 2 has another procedural rule.
QByteArray Fixture()
{
    const qsizetype bones = sizeof(studiohdr_t), rule = bones + 3 * sizeof(mstudiobone_t);
    QByteArray mdl(rule + sizeof(JiggleParams), '\0');
    auto *hdr = Hdr(mdl);
    hdr->id = IDSTUDIOHEADER;
    hdr->version = STUDIO_VERSION;
    hdr->length = int(mdl.size());
    hdr->numbones = 3;
    hdr->boneindex = int(bones);
    JiggleParams jiggle;
    jiggle.length = 5;
    std::memcpy(mdl.data() + rule, &jiggle, sizeof(jiggle));
    Bone(mdl, 0)->proctype = ProcBoneType::Jiggle;
    Bone(mdl, 0)->procindex = int(rule - bones);
    Bone(mdl, 0)->flags = BoneFlags::AlwaysProcedural;
    Bone(mdl, 2)->proctype = ProcBoneType::AxisInterp;
    Bone(mdl, 2)->procindex = -int(bones);
    hdr->checksum = MdlChecksum(mdl);
    return mdl;
}

void Write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    Check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "could not write fixture");
}

QByteArray Read(const QString &path)
{
    QFile file(path);
    Check(file.open(QIODevice::ReadOnly), "could not read fixture");
    return file.readAll();
}

int32_t Field(const QByteArray &bytes, int offset)
{
    int32_t value;
    std::memcpy(&value, bytes.constData() + offset, 4);
    return value;
}

QByteArray Companion(int size, int offset, int32_t checksum)
{
    QByteArray bytes(size, '\0');
    std::memcpy(bytes.data() + offset, &checksum, 4);
    return bytes;
}
}

int main()
{
    const auto mdl = Fixture();
    const auto rules = ReadJiggleBones(mdl);
    Check(rules[0] && rules[0]->length == 5 && !rules[1] && !rules[2], "existing rule not read");

    // Moving the rule from bone 0 to bone 1 reuses its slot: same size, new checksum.
    JiggleParams moved;
    moved.length = 7;
    moved.flags = JiggleParams::Rigid | JiggleParams::LengthConstraint;
    auto rebuilt = RebuildMdlJiggles(mdl, {{0, std::nullopt}, {1, moved}});
    Check(rebuilt.size() == mdl.size(), "freed slot was not reused");
    const auto after = ReadJiggleBones(rebuilt);
    Check(!after[0] && after[1] == moved, "rules not moved");
    Check(!HasFlag(Bone(rebuilt, 0)->flags, BoneFlags::AlwaysProcedural) && HasFlag(Bone(rebuilt, 1)->flags, BoneFlags::AlwaysProcedural),
          "always-procedural flags not moved");
    Check(Hdr(rebuilt)->checksum == MdlChecksum(rebuilt) && Hdr(rebuilt)->checksum != Hdr(const_cast<QByteArray &>(mdl))->checksum,
          "checksum not recomputed");

    // A new rule beside the existing one is appended.
    auto grown = RebuildMdlJiggles(mdl, {{1, moved}});
    Check(grown.size() == mdl.size() + qsizetype(sizeof(JiggleParams)) && Hdr(grown)->length == grown.size(), "rule not appended");
    Check(ReadJiggleBones(grown)[0]->length == 5 && ReadJiggleBones(grown)[1] == moved, "appended rule wrong");

    bool rejected = false;
    try
    {
        RebuildMdlJiggles(mdl, {{2, moved}});
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }
    Check(rejected, "replaced another procedural rule");

    // Saving moves the checksum into the companions that matched, skips stale ones, and needs the .vvd.
    QTemporaryDir temp;
    Check(temp.isValid(), "temp dir");
    const QDir dir(temp.path());
    const auto old = Hdr(const_cast<QByteArray &>(mdl))->checksum;
    Write(dir.filePath("test.mdl"), mdl);
    Write(dir.filePath("test.vvd"), Companion(64, 8, old));
    Write(dir.filePath("Test.DX90.vtx"), Companion(36, 16, old));
    Write(dir.filePath("test.sw.vtx"), Companion(36, 16, old + 1));
    Write(dir.filePath("test.phy"), Companion(16, 12, old));
    const auto notes = SaveMdlJiggles(dir.filePath("test.mdl"), {{1, moved}});
    const auto saved = Read(dir.filePath("test.mdl"));
    const auto checksum = Field(saved, 8);
    Check(saved == grown && checksum == MdlChecksum(saved), "MDL not rebuilt");
    Check(Field(Read(dir.filePath("test.vvd")), 8) == checksum && Field(Read(dir.filePath("Test.DX90.vtx")), 16) == checksum &&
          Field(Read(dir.filePath("test.phy")), 12) == checksum, "companion checksums not updated");
    Check(Field(Read(dir.filePath("test.sw.vtx")), 16) == old + 1 && notes.contains(QStringLiteral("skipped stale test.sw.vtx")),
          "stale companion touched");

    QFile::remove(dir.filePath("test.vvd"));
    rejected = false;
    try
    {
        SaveMdlJiggles(dir.filePath("test.mdl"), {{0, std::nullopt}});
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }
    Check(rejected && Read(dir.filePath("test.mdl")) == saved, "saved without a .vvd");
}
