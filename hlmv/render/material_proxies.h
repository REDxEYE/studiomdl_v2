#pragma once

// VMT "Proxies" evaluation (materialsystem proxies from source-sdk-2013 mathproxy/functionproxy/matrixproxy/
// texturescroll/baseanimatedtexture). Proxies run in file order, duplicates included, on a table of material
// variables seeded from the VMT. Entity-dependent TF proxies are not run unless the SDK defines their result without
// an entity (YellowLevel, BurnLevel, ...); the rest are reported through unsupported(), and every proxy that reads
// what they would have written is skipped and reported as well, so nothing is faked.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <QMatrix4x4>
#include <QString>
#include <QStringList>
#include <QVector4D>

class KeyValues;

class MaterialProxies
{
public:
    // `frameCount("$basetexture")` = animation frames of that texture variable, 0 when unknown (AnimatedTexture on
    // such a variable is reported unsupported).
    MaterialProxies(KeyValues &vmt, const std::function<int(const QString &)> &frameCount);

    // True when a Proxies block exists in the VMT (even when every proxy is unsupported).
    bool declared() const { return m_declared; }
    // Runs every proxy for `seconds` (gpGlobals->curtime). Cheap no-op when nothing is time-varying or time is unchanged.
    void update(double seconds);
    bool timeVarying() const { return m_timeVarying; }
    // A variable as an integer (0 when absent): the VMT value, or the last value a proxy wrote. Texture frames are
    // "$frame", "$detailframe", "$bumpframe", ...
    int intValue(const char *key) const;
    bool writes(const char *key) const; // does a running proxy target `key`
    // Value a proxy wrote to `key` (case-insensitive): scalars replicated to xyzw; vectors as xyz (w = 1 below size 4).
    std::optional<QVector4D> value(const char *key) const;
    // Same layout as VMatrix: row-major, UV translation in column 3.
    std::optional<QMatrix4x4> transform(const char *key) const;
    std::optional<float> alpha() const; // a proxy-written `$alpha`
    // "proxy:<Name>[ (reason)]", one per unsupported or skipped proxy.
    const QStringList &unsupported() const { return m_unsupported; }

private:
    enum class Op;
    struct Var
    {
        std::string name; // lower case
        enum Type { Undefined, Scalar, Vec, Matrix } type = Undefined;
        QVector4D v;
        int size = 0; // Vec components
        QMatrix4x4 m;
        bool written = false;
    };
    struct In
    {
        int var = -1, comp = -1;
        float literal = 0;
    };
    struct Step
    {
        Op op;
        int res = -1, resComp = -1, a = -1, b = -1, less = -1, greater = -1;
        In p[4];
        int center = -1, scale = -1, rotate = -1, translate = -1; // TextureTransform
        float constant[4] = {};
        int constantSize = 0;
        int frames = 0;
        bool noWrap = false;
        std::vector<int> reads;
    };
    struct Kind
    {
        bool vec;
        int size;
    };

    int find(const QString &name, int *comp = nullptr);
    int create(const std::string &name);
    void build(KeyValues &block, const std::function<int(const QString &)> &frameCount);
    float number(const In &in) const;
    float scalarOf(int var) const;
    QVector4D vecOf(int var, int size) const;
    Kind kindOf(const Step &s) const;
    void setScalar(const Step &s, float value);
    void setVec(const Step &s, QVector4D value, int size);
    void run(const Step &s, float time);
    float random();

    std::vector<Var> m_vars;
    std::vector<Step> m_steps;
    std::vector<bool> m_tainted;
    QStringList m_unsupported;
    int m_alphaVar = -1;
    bool m_declared = false, m_timeVarying = false, m_ran = false;
    double m_last = 0;
    unsigned m_rng = 0x9e3779b9u; // shortcut: fixed-seed noise so renders repeat; Source seeds from the engine RNG
};
