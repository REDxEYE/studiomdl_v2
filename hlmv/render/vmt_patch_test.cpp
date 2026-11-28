#include "material_system.h"
#include "tier1/keyvalues.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstring>
#include <stdexcept>
#include <string>

namespace
{
void Check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}

std::string Read(const QString &text, const char *key, const char *block = nullptr)
{
    KeyValues kv("material");
    const auto utf8 = text.toUtf8();
    Check(kv.LoadFromBuffer("test.vmt", utf8.constData()), "patched VMT does not parse");
    auto *scope = block ? kv.FindKey(block) : &kv;
    Check(scope != nullptr, "block missing");
    return std::string(scope->GetString(key, "<absent>"));
}

void Write(const QString &path, const char *text)
{
    QFile file(path);
    Check(file.open(QIODevice::WriteOnly) && file.write(text) == qint64(std::strlen(text)), "could not write fixture");
}

QByteArray Contents(const QString &path)
{
    QFile file(path);
    Check(file.open(QIODevice::ReadOnly), "could not read fixture");
    return file.readAll();
}
}

int main()
{
    const QString vmt = QStringLiteral(
        "// keep me\r\n"
        "\"VertexLitGeneric\"\r\n"
        "{\r\n"
        "\t\"$basetexture\" \"models/a\" // base\r\n"
        "\t$phong 1\r\n"
        "\t\"$nocull\" \"1\"\r\n"
        "\t\">=DX90\"\r\n"
        "\t{\r\n"
        "\t\t\"$selfillum\" \"1\"\r\n"
        "\t}\r\n"
        "}\r\n");
    const auto out = PatchVmtText(vmt, {{QStringLiteral("$phong"), QStringLiteral("0")},
                                        {QStringLiteral("$selfillum"), QStringLiteral("0")},
                                        {QStringLiteral("$color2"), QStringLiteral("[1 0.5 0]")},
                                        {QStringLiteral("$nocull"), std::nullopt}});
    Check(out.startsWith(QStringLiteral("// keep me\r\n")) && out.contains(QStringLiteral("// base")), "comments lost");
    Check(Read(out, "$phong") == "0" && Read(out, "$color2") == "[1 0.5 0]", "root edits not written");
    Check(Read(out, "$selfillum", ">=DX90") == "0", "fallback block value was not edited where it wins");
    Check(Read(out, "$nocull") == "<absent>" && !out.contains(QStringLiteral("$nocull")), "removed line kept");
    Check(!out.contains(QStringLiteral("\r\n\n")) && out.contains(QStringLiteral("\t\"$color2\" \"[1 0.5 0]\"\r\n}")),
          "new parameter not indented with the file's line endings");

    const auto patch = PatchVmtText(QStringLiteral("patch\n{\n\tinclude \"materials/base.vmt\"\n}\n"),
                                    {{QStringLiteral("$alpha"), QStringLiteral("0.5")}});
    Check(std::stof(Read(patch, "$alpha", "insert")) == 0.5f && Read(patch, "include") == "materials/base.vmt",
          "patch edit did not go into a new insert block");

    bool rejected = false;
    try
    {
        PatchVmtText(vmt, {{QStringLiteral("$basetexture"), QStringLiteral("a\"b")}});
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }
    Check(rejected, "quote in value was written");

    // Sync: written through the material system's own reader, refused when the edit would not read back.
    QTemporaryDir temp;
    Check(temp.isValid() && QDir(temp.path()).mkpath(QStringLiteral("mod/materials")), "temp dir");
    const auto mod = temp.filePath(QStringLiteral("mod"));
    const auto model = mod + QStringLiteral("/models/test.mdl");
    const auto plain = mod + QStringLiteral("/materials/plain.vmt");
    Write(plain, "\"VertexLitGeneric\"\n{\n\t// tint\n\t\"$basetexture\" \"a\"\n}\n");
    SaveMaterialEdits(model, plain, {{QStringLiteral("$alpha"), QStringLiteral("0.25")}, {QStringLiteral("$color2"), QStringLiteral("[1 0 0]")}});
    Check(Contents(plain) == "\"VertexLitGeneric\"\n{\n\t// tint\n\t\"$basetexture\" \"a\"\n\t\"$alpha\" \"0.25\"\n\t\"$color2\" \"[1 0 0]\"\n}\n",
          "sync did not write the edit in place");

    Write(mod + QStringLiteral("/materials/base.vmt"), "VertexLitGeneric\n{\n\t>=DX90\n\t{\n\t\t$alpha 1\n\t}\n}\n");
    const auto patched = mod + QStringLiteral("/materials/patched.vmt");
    const char *patchText = "patch\n{\n\tinclude materials/base.vmt\n}\n";
    Write(patched, patchText);
    rejected = false;
    try
    {
        SaveMaterialEdits(model, patched, {{QStringLiteral("$alpha"), QStringLiteral("0.5")}});
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }
    Check(rejected && Contents(patched) == patchText, "sync wrote an edit the base VMT's fallback block overrides");
}
