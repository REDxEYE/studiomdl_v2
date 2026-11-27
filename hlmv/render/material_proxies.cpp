#include "material_proxies.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <limits>
#include <numbers>

#include "tier1/keyvalues.h"

enum class MaterialProxies::Op
{
    NoOp, Const, Add, Subtract, Multiply, Divide, Equals, Abs, Frac, Int, Clamp, LessOrEqual, SelectFirst, WrapMinMax,
    Exponential, Sine, LinearRamp, CurrentTime, UniformNoise, GaussianNoise, Heartbeat, TextureScroll, TextureTransform,
    AnimatedTexture
};

// The proxy table needs the private Op, so it lives in build().
void MaterialProxies::build(KeyValues &block, const std::function<int(const QString &)> &frameCount)
{
    using enum Op;
    struct Def
    {
        Op op;
        float constant[4];
        int size; // Const: 0 = scalar
    };
    static const std::map<std::string, Def> table = {
        {"add", {Add}}, {"subtract", {Subtract}}, {"multiply", {Multiply}}, {"divide", {Divide}}, {"equals", {Equals}},
        {"abs", {Abs}}, {"frac", {Frac}}, {"int", {Int}}, {"clamp", {Clamp}}, {"lessorequal", {LessOrEqual}},
        {"selectfirstifnonzero", {SelectFirst}}, {"wrapminmax", {WrapMinMax}}, {"exponential", {Exponential}},
        {"sine", {Sine}}, {"linearramp", {LinearRamp}}, {"currenttime", {CurrentTime}},
        {"uniformnoise", {UniformNoise}}, {"gaussiannoise", {GaussianNoise}}, {"texturescroll", {TextureScroll}},
        {"texturetransform", {TextureTransform}}, {"animatedtexture", {AnimatedTexture}}, {"empty", {NoOp}},
        // c_tf_player.cpp OnBind with no entity: InvulnLevel 1, BurnLevel 0, YellowLevel/ModelGlowColor (1 1 1),
        // CommunityWeapon 0, ShieldFalloff (1 1 1 1), HeartbeatScale from the clock.
        {"invulnlevel", {Const, {1}, 0}}, {"burnlevel", {Const, {0}, 0}}, {"communityweapon", {Const, {0}, 0}},
        {"yellowlevel", {Const, {1, 1, 1}, 3}}, {"modelglowcolor", {Const, {1, 1, 1}, 3}},
        {"shieldfalloff", {Const, {1, 1, 1, 1}, 4}}, {"heartbeatscale", {Heartbeat}},
        // These return before writing anything without an entity / local player / item.
        {"benefactorlevel", {NoOp}}, {"buildingrescuelevel", {NoOp}}, {"wheatlyeyeglow", {NoOp}},
        {"animatedweaponsheen", {NoOp}},
    };
    const auto name = QString::fromUtf8(block.GetName());
    const auto lower = name.toLower().toStdString();
    const auto it = table.find(lower);
    if (it == table.end())
    {
        m_unsupported.append(QStringLiteral("proxy:") + name);
        // Whatever the proxy would write is unknown: taint its result variables (not its sources).
        for (auto *v = block.GetFirstValue(); v; v = v->GetNextValue())
        {
            const auto key = QString::fromUtf8(v->GetName()).toLower();
            if (key == QLatin1String("resultvar") || (key.endsWith(QLatin1String("var")) && !key.startsWith(QLatin1String("srcvar"))))
                if (const int var = find(QString::fromUtf8(v->GetString())); var >= 0)
                    m_tainted[var] = true;
        }
        return;
    }
    if (it->second.op == NoOp)
        return;

    Step s;
    s.op = it->second.op;
    std::copy(std::begin(it->second.constant), std::end(it->second.constant), s.constant);
    s.constantSize = it->second.size;
    QString missing;
    // A variable reference: found (or built in) or the proxy's Init fails and Source drops it.
    const auto ref = [&](const char *key, int &var, int *comp, bool required, bool read) {
        const auto text = QString::fromUtf8(block.GetString(key)).trimmed();
        if (text.isEmpty())
        {
            if (required)
                missing = QString::fromLatin1(key);
            return;
        }
        var = find(text, comp);
        if (var < 0)
            missing = text;
        else if (read)
            s.reads.push_back(var);
    };
    // CFloatInput: a number or a variable (optionally "[n]" component); `fallback` when absent.
    const auto input = [&](int slot, const char *key, float fallback) {
        const auto text = QString::fromUtf8(block.GetString(key)).trimmed();
        auto &in = s.p[slot];
        in.literal = fallback;
        if (text.isEmpty())
            return;
        float number;
        if (std::sscanf(text.toUtf8().constData(), "%f", &number) == 1)
            in.literal = number;
        else if ((in.var = find(text, &in.comp)) < 0)
            missing = text;
        else
            s.reads.push_back(in.var);
    };
    const auto needsResult = [&](bool yes) { if (yes) ref("resultVar", s.res, &s.resComp, true, false); };
    switch (s.op)
    {
    case Add: case Subtract: case Multiply: case Divide: case LessOrEqual: case SelectFirst:
        needsResult(true);
        ref("srcVar1", s.a, nullptr, true, true);
        ref("srcVar2", s.b, nullptr, true, true);
        if (s.op == LessOrEqual)
        {
            ref("lessEqualVar", s.less, nullptr, true, true);
            ref("greaterVar", s.greater, nullptr, true, true);
        }
        break;
    case Equals: case Abs: case Frac: case Int: case Clamp: case WrapMinMax: case Exponential:
        needsResult(true);
        ref("srcVar1", s.a, nullptr, true, true);
        if (s.op == Clamp) { input(0, "min", 0); input(1, "max", 1); }
        if (s.op == WrapMinMax) { input(0, "minVal", 0); input(1, "maxVal", 1); }
        if (s.op == Exponential) { input(0, "scale", 1); input(1, "offset", 0); input(2, "minVal", -3.4e38f); input(3, "maxVal", 3.4e38f); }
        break;
    case Sine:
        needsResult(true);
        input(0, "sinePeriod", 1); input(1, "sineMax", 1); input(2, "sineMin", 0); input(3, "timeOffset", 0);
        break;
    case LinearRamp: needsResult(true); input(0, "rate", 1); input(1, "initialValue", 0); break;
    case UniformNoise: needsResult(true); input(0, "minVal", 0); input(1, "maxVal", 1); break;
    case GaussianNoise:
        needsResult(true);
        input(0, "mean", 0); input(1, "halfwidth", 1); input(2, "minVal", -3.4e38f); input(3, "maxVal", 3.4e38f);
        break;
    case CurrentTime: case Heartbeat: case Const: needsResult(true); break;
    case TextureScroll:
        ref("textureScrollVar", s.res, nullptr, true, false);
        input(0, "textureScrollRate", 1); input(1, "textureScrollAngle", 0); input(2, "textureScale", 1);
        break;
    case TextureTransform:
        needsResult(true);
        ref("centerVar", s.center, nullptr, false, true);
        ref("scaleVar", s.scale, nullptr, false, true);
        ref("rotateVar", s.rotate, nullptr, false, true);
        ref("translateVar", s.translate, nullptr, false, true);
        break;
    case AnimatedTexture:
    {
        const auto texture = QString::fromUtf8(block.GetString("animatedTextureVar")).trimmed();
        ref("animatedTextureFrameNumVar", s.res, nullptr, true, false);
        s.frames = missing.isEmpty() ? frameCount(texture.toLower()) : 0;
        if (missing.isEmpty() && s.frames <= 0)
        {
            m_unsupported.append(QStringLiteral("proxy:%1 (frames of %2 not loaded)").arg(name, texture));
            return;
        }
        s.p[0].literal = block.GetFloat("animatedTextureFrameRate", 15);
        s.noWrap = block.GetInt("animationNoWrap", 0) != 0;
        break;
    }
    default: break;
    }
    if (!missing.isEmpty())
    {
        m_unsupported.append(QStringLiteral("proxy:%1 (missing variable %2)").arg(name, missing));
        return;
    }
    if (std::ranges::any_of(s.reads, [&](int var) { return m_tainted[var]; }))
    {
        m_unsupported.append(QStringLiteral("proxy:%1 (depends on an unsupported proxy)").arg(name));
        if (s.res >= 0)
            m_tainted[s.res] = true;
        return;
    }
    switch (s.op)
    {
    case Sine: case LinearRamp: case CurrentTime: case UniformNoise: case GaussianNoise: case Heartbeat: case TextureScroll:
        m_timeVarying = true;
        break;
    case AnimatedTexture: m_timeVarying |= s.frames > 1; break;
    default: break;
    }
    m_steps.push_back(std::move(s));
}

namespace
{
// CMaterialVar's string forms: "[x y z]" / "{r g b}" (0..255), the texture-transform matrix text, or a number.
bool ParseVec(const QString &text, QVector4D &out, int &size)
{
    const bool bytes = text.startsWith('{');
    if (!(bytes || text.startsWith('[')) || text.size() < 3 || text.back() != (bytes ? '}' : ']'))
        return false;
    const auto parts = text.mid(1, text.size() - 2).split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 2 || parts.size() > 4)
        return false;
    out = {};
    for (int i = 0; i < parts.size(); ++i)
        out[i] = parts[i].toFloat() * (bytes ? 1.f / 255.f : 1.f);
    size = int(parts.size());
    return true;
}

// "center .5 .5 scale 1 1 rotate 0 translate 0 0" in any order, composed like TextureTransform.
bool ParseMatrix(const QString &text, QMatrix4x4 &out)
{
    const auto words = text.split(' ', Qt::SkipEmptyParts);
    float center[2] = {.5f, .5f}, scale[2] = {1, 1}, rotate = 0, translate[2] = {0, 0};
    bool any = false;
    for (int i = 0; i < words.size(); ++i)
    {
        const auto word = words[i].toLower();
        float *dst = word == QLatin1String("center") ? center : word == QLatin1String("scale") ? scale
                   : word == QLatin1String("translate") ? translate : word == QLatin1String("rotate") ? &rotate : nullptr;
        if (!dst)
            continue;
        any = true;
        for (int c = 0; c < (dst == &rotate ? 1 : 2) && i + 1 < words.size(); ++c)
            dst[c] = words[++i].toFloat();
    }
    if (!any)
        return false;
    out.setToIdentity();
    out.translate(translate[0], translate[1]);
    out.translate(center[0], center[1]);
    out.rotate(rotate, 0, 0, 1);
    out.scale(scale[0], scale[1], 1);
    out.translate(-center[0], -center[1]);
    return true;
}

int ProxyInt(double value)
{
    if (!std::isfinite(value))
        return 0;
    return int(std::clamp(value, double(std::numeric_limits<int>::min()), double(std::numeric_limits<int>::max())));
}
} // namespace

int MaterialProxies::create(const std::string &name)
{
    m_vars.push_back({.name = name});
    m_tainted.push_back(false);
    return int(m_vars.size()) - 1;
}

// CMaterial::FindVar: VMT variables plus the ones every shader declares (the type decides how proxies write them).
int MaterialProxies::find(const QString &text, int *comp)
{
    auto name = text.toLower();
    if (comp)
        *comp = -1;
    if (const auto bracket = name.indexOf('['); bracket >= 0)
    {
        if (comp)
            *comp = name.mid(bracket + 1).toInt();
        name.truncate(bracket);
    }
    const auto key = name.toStdString();
    for (size_t i = 0; i < m_vars.size(); ++i)
        if (m_vars[i].name == key)
            return int(i);
    if (!name.startsWith('$'))
        return -1;
    const int var = create(key);
    auto &v = m_vars[var];
    if (name.endsWith(QLatin1String("transform")))
        v.type = Var::Matrix;
    else if (name.endsWith(QLatin1String("frame")))
        v.type = Var::Scalar;
    else if (name == QLatin1String("$alpha"))
        v.type = Var::Scalar, v.v = {1, 0, 0, 0};
    else if (name == QLatin1String("$color") || name == QLatin1String("$color2") || name == QLatin1String("$envmaptint") ||
             name == QLatin1String("$selfillumtint") || name == QLatin1String("$phongtint"))
        v.type = Var::Vec, v.size = 3, v.v = {1, 1, 1, 0};
    else
    {
        m_vars.pop_back();
        m_tainted.pop_back();
        return -1;
    }
    return var;
}

MaterialProxies::MaterialProxies(KeyValues &vmt, const std::function<int(const QString &)> &frameCount)
{
    for (auto *kv = vmt.GetFirstValue(); kv; kv = kv->GetNextValue())
    {
        const auto name = QString::fromUtf8(kv->GetName()).toLower();
        if (!name.startsWith('$'))
            continue;
        const auto text = QString::fromUtf8(kv->GetString()).trimmed();
        const int var = create(name.toStdString());
        auto &v = m_vars[var];
        bool number = false;
        const float f = text.toFloat(&number);
        if (ParseVec(text, v.v, v.size))
            v.type = Var::Vec;
        else if (ParseMatrix(text, v.m))
            v.type = Var::Matrix;
        else if (number)
            v.type = Var::Scalar, v.v = {f, 0, 0, 0};
        else if (name.endsWith(QLatin1String("transform")) && text.isEmpty())
            v.type = Var::Matrix;
    }
    const int frameVar = find(QStringLiteral("$frame"));
    m_alphaVar = find(QStringLiteral("$alpha"));
    // A non-numeric "$frame"/"$alpha" is not a number: Source leaves the shader default.
    if (m_vars[frameVar].type != Var::Scalar)
        m_vars[frameVar].type = Var::Scalar, m_vars[frameVar].v = {};
    if (m_vars[m_alphaVar].type != Var::Scalar)
        m_vars[m_alphaVar].type = Var::Scalar, m_vars[m_alphaVar].v = {1, 0, 0, 0};
    if (auto *proxies = vmt.FindKey("Proxies"))
    {
        m_declared = true;
        for (auto *block = proxies->GetFirstTrueSubKey(); block; block = block->GetNextTrueSubKey())
            build(*block, frameCount);
    }
    m_unsupported.removeDuplicates();
}

float MaterialProxies::number(const In &in) const
{
    if (in.var < 0)
        return in.literal;
    const auto &v = m_vars[in.var];
    if (in.comp < 0)
        return scalarOf(in.var);
    return v.type == Var::Vec && in.comp < v.size ? v.v[in.comp] : 0;
}

float MaterialProxies::scalarOf(int var) const
{
    const auto &v = m_vars[var];
    return v.type == Var::Scalar || v.type == Var::Vec ? v.v.x() : 0;
}

QVector4D MaterialProxies::vecOf(int var, int size) const
{
    const auto &v = m_vars[var];
    if (v.type == Var::Vec)
    {
        QVector4D out;
        for (int i = 0; i < size; ++i)
            out[i] = i < v.size ? v.v[i] : 0;
        return out;
    }
    QVector4D out;
    for (int i = 0; i < size; ++i)
        out[i] = scalarOf(var);
    return out;
}

// CFunctionProxy::ComputeResultType: the result variable's type, else the first source's, else the second's.
MaterialProxies::Kind MaterialProxies::kindOf(const Step &s) const
{
    const auto &r = m_vars[s.res];
    if (r.type == Var::Vec)
        return {s.resComp < 0, r.size};
    if (r.type == Var::Undefined && s.a >= 0)
    {
        const auto &a = m_vars[s.a];
        if (a.type == Var::Vec)
            return {true, a.size};
        if (a.type == Var::Undefined && s.b >= 0 && m_vars[s.b].type == Var::Vec)
            return {true, m_vars[s.b].size};
    }
    return {false, 0};
}

// CResultProxy::SetFloatResult
void MaterialProxies::setScalar(const Step &s, float value)
{
    auto &r = m_vars[s.res];
    r.written = true;
    if (r.type == Var::Vec)
    {
        if (s.resComp >= 0)
            r.v[std::clamp(s.resComp, 0, 3)] = value;
        else
            for (int i = 0; i < r.size; ++i)
                r.v[i] = value;
        return;
    }
    r.type = Var::Scalar;
    r.v = {value, 0, 0, 0};
}

void MaterialProxies::setVec(const Step &s, QVector4D value, int size)
{
    auto &r = m_vars[s.res];
    r.written = true;
    r.type = Var::Vec;
    r.size = size;
    r.v = value;
}

float MaterialProxies::random() // xorshift32 -> [0, 1)
{
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return float(m_rng >> 8) / float(1 << 24);
}

void MaterialProxies::run(const Step &s, float time)
{
    using enum Op;
    constexpr float pi = std::numbers::pi_v<float>;
    const auto clampRange = [&](float value, float lo, float hi) { return std::clamp(value, std::min(lo, hi), std::max(lo, hi)); };
    switch (s.op)
    {
    case Add: case Subtract: case Multiply: case Divide:
    {
        const auto apply = [&](float a, float b) {
            return s.op == Add ? a + b : s.op == Subtract ? a - b : s.op == Multiply ? a * b : b != 0 ? a / b : a;
        };
        const auto kind = kindOf(s);
        if (!kind.vec)
            return setScalar(s, apply(scalarOf(s.a), scalarOf(s.b)));
        const auto a = vecOf(s.a, kind.size), b = vecOf(s.b, kind.size);
        QVector4D out;
        for (int i = 0; i < kind.size; ++i)
            out[i] = apply(a[i], b[i]);
        return setVec(s, out, kind.size);
    }
    case Equals: case Frac: case Int: case Clamp:
    {
        const auto apply = [&](float a) {
            return s.op == Equals ? a : s.op == Frac ? a - std::trunc(a) : s.op == Int ? std::trunc(a)
                                                                                         : clampRange(a, number(s.p[0]), number(s.p[1]));
        };
        const auto kind = kindOf(s);
        if (!kind.vec)
            return setScalar(s, apply(scalarOf(s.a)));
        auto a = vecOf(s.a, kind.size);
        for (int i = 0; i < kind.size; ++i)
            a[i] = apply(a[i]);
        return setVec(s, a, kind.size);
    }
    case Abs: return setScalar(s, std::fabs(scalarOf(s.a)));
    case Exponential:
        return setScalar(s, clampRange(number(s.p[0]) * std::exp(scalarOf(s.a) + number(s.p[1])), number(s.p[2]), number(s.p[3])));
    case WrapMinMax:
    {
        const float lo = number(s.p[0]), hi = number(s.p[1]);
        if (hi <= lo)
            return setScalar(s, lo);
        float t = (scalarOf(s.a) - lo) / (hi - lo);
        t -= std::floor(t);
        return setScalar(s, t * (hi - lo) + lo);
    }
    case LessOrEqual: case SelectFirst:
    {
        const auto &r = m_vars[s.res];
        const auto kind = kindOf(s);
        const int source = s.op == LessOrEqual ? (scalarOf(s.a) <= scalarOf(s.b) ? s.less : s.greater) : -1;
        if (s.op == LessOrEqual && r.type == Var::Undefined && m_vars[source].type == Var::Vec)
            return setVec(s, vecOf(source, m_vars[source].size), m_vars[source].size);
        if (s.op == LessOrEqual)
            return kind.vec ? setVec(s, vecOf(source, kind.size), kind.size) : setScalar(s, scalarOf(source));
        if (kind.vec)
        {
            const auto a = vecOf(s.a, kind.size);
            const bool first = a.x() != 0 || (kind.size > 1 && a.y() != 0) || (kind.size > 2 && a.z() != 0) || (kind.size > 3 && a.w() != 0);
            return setVec(s, first ? a : vecOf(s.b, kind.size), kind.size);
        }
        return setScalar(s, scalarOf(s.a) != 0 ? scalarOf(s.a) : scalarOf(s.b));
    }
    case Sine:
    {
        float period = number(s.p[0]);
        if (period == 0)
            period = 1;
        const float t = std::sin(2 * pi * (time - number(s.p[3])) / period) * .5f + .5f;
        return setScalar(s, (number(s.p[1]) - number(s.p[2])) * t + number(s.p[2]));
    }
    case LinearRamp: return setScalar(s, number(s.p[0]) * time + number(s.p[1]));
    case CurrentTime: return setScalar(s, time);
    case UniformNoise:
    {
        const float lo = number(s.p[0]), hi = number(s.p[1]);
        return setScalar(s, lo + (hi - lo) * random());
    }
    case GaussianNoise:
    {
        // Box-Muller; RandomGaussianFloat(mean, stddev) with halfwidth as the deviation.
        const float u = std::max(random(), 1e-7f), v = random();
        const float value = number(s.p[0]) + number(s.p[1]) * std::sqrt(-2 * std::log(u)) * std::cos(2 * pi * v);
        return setScalar(s, clampRange(value, number(s.p[2]), number(s.p[3])));
    }
    case Heartbeat:
    {
        const auto pulse = [](float t) {
            float x = std::clamp(std::sin(t * 2 * pi), 0.f, 1.f);
            x *= x;
            x *= x;
            return (std::clamp(x, .5f, 1.f) - .5f) * 2;
        };
        const float beat = std::max(pulse(time), pulse(time + .25f));
        return setScalar(s, .6f + (.8f * .6f - .6f) * beat);
    }
    case Const:
        if (s.constantSize == 0)
            return setScalar(s, s.constant[0]);
        return setVec(s, {s.constant[0], s.constant[1], s.constant[2], s.constant[3]}, s.constantSize);
    case TextureScroll:
    {
        const float rate = number(s.p[0]), angle = number(s.p[1]), scale = number(s.p[2]);
        float u = time * std::cos(angle * (pi / 180)) * rate, v = time * std::sin(angle * (pi / 180)) * rate;
        u -= std::floor(u);
        v -= std::floor(v);
        auto &r = m_vars[s.res];
        r.written = true;
        if (r.type == Var::Matrix)
            r.m = QMatrix4x4(scale, 0, 0, u, 0, scale, 0, v, 0, 0, 1, 0, 0, 0, 0, 1);
        else
            r.type = Var::Vec, r.size = 3, r.v = {u, v, 0, 0};
        return;
    }
    case TextureTransform:
    {
        float center[2] = {.5f, .5f}, translation[2] = {};
        if (s.center >= 0)
            center[0] = vecOf(s.center, 2).x(), center[1] = vecOf(s.center, 2).y();
        QMatrix4x4 m;
        if (s.translate >= 0)
            translation[0] = vecOf(s.translate, 2).x(), translation[1] = vecOf(s.translate, 2).y();
        m.translate(translation[0], translation[1]);
        m.translate(center[0], center[1]);
        if (s.rotate >= 0)
            m.rotate(scalarOf(s.rotate), 0, 0, 1);
        if (s.scale >= 0)
            m.scale(vecOf(s.scale, 2).x(), vecOf(s.scale, 2).y(), 1);
        m.translate(-center[0], -center[1]);
        auto &r = m_vars[s.res];
        r.written = true;
        r.type = Var::Matrix;
        r.m = m;
        return;
    }
    case AnimatedTexture:
    {
        // CBaseAnimatedTextureProxy::OnBind with a start time of 0. shortcut: without animationNoWrap it wraps; with it
        // the last frame holds (the SDK's per-frame wrap detection needs the previous frame's time).
        const double frame = double(s.p[0].literal) * std::max(time, 0.f);
        const int index = s.noWrap ? ProxyInt(std::clamp(frame, 0.0, double(s.frames - 1))) :
                                    ProxyInt(std::fmod(std::trunc(frame), s.frames));
        return setScalar(s, float(index));
    }
    case NoOp: return;
    }
}

void MaterialProxies::update(double seconds)
{
    if (m_ran && (!m_timeVarying || seconds == m_last))
        return;
    m_ran = true;
    m_last = seconds;
    for (const auto &step : m_steps)
        run(step, float(seconds));
}

int MaterialProxies::intValue(const char *key) const
{
    for (const auto &v : m_vars)
        if (qstricmp(v.name.c_str(), key) == 0)
            return v.type == Var::Scalar || v.type == Var::Vec ? ProxyInt(v.v.x()) : 0;
    return 0;
}

bool MaterialProxies::writes(const char *key) const
{
    return std::ranges::any_of(m_steps, [&](const Step &s) { return s.res >= 0 && qstricmp(m_vars[s.res].name.c_str(), key) == 0; });
}

std::optional<float> MaterialProxies::alpha() const
{
    const auto &v = m_vars[m_alphaVar];
    return v.written ? std::optional(v.v.x()) : std::nullopt;
}

std::optional<QVector4D> MaterialProxies::value(const char *key) const
{
    for (const auto &v : m_vars)
        if (v.written && qstricmp(v.name.c_str(), key) == 0)
        {
            if (v.type == Var::Scalar)
                return QVector4D(v.v.x(), v.v.x(), v.v.x(), v.v.x());
            if (v.type == Var::Vec)
                return QVector4D(v.v.x(), v.v.y(), v.size > 2 ? v.v.z() : 0, v.size > 3 ? v.v.w() : 1);
            return std::nullopt;
        }
    return std::nullopt;
}

std::optional<QMatrix4x4> MaterialProxies::transform(const char *key) const
{
    for (const auto &v : m_vars)
        if (v.written && v.type == Var::Matrix && qstricmp(v.name.c_str(), key) == 0)
            return v.m;
    return std::nullopt;
}
