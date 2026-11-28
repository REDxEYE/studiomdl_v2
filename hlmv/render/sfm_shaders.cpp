// SFM stdshader_dx9 port: VertexLitGeneric/UnlitGeneric (incl. $phong), Eyes and EyeRefract. Static combo
// layouts come from tools/sfm_shader_combos.py (sfm_shader_combos.inc) and hand-derived rows
// (sfm_shader_combos_manual.inc); combo selection and constant registers follow the SFM DLL's draw functions.
// SFM runs at dxlevel 95, so every shader takes its SM3 (vs30/ps30) branch.

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <vector>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "material_system.h"
#include "texture_upload.h"

namespace
{
struct ComboSlot
{
    const char *name;
    uint32_t weight;
    uint32_t count;
};

struct ComboLayout
{
    const char *shader;
    uint32_t staticCount;
    uint32_t dynamicCount;
    std::span<const ComboSlot> combos; // not "slots": Qt defines that as a macro
};

#include "sfm_shader_combos.inc"
#include "sfm_shader_combos_manual.inc"

// Every layout must cover exactly its VCS static count with weights that are running products of counts.
constexpr bool Consistent(const ComboLayout &layout)
{
    uint32_t weight = 1;
    for (const auto &slot : layout.combos)
    {
        if (slot.weight != weight || slot.count == 0)
            return false;
        weight *= slot.count;
    }
    return weight == layout.staticCount;
}
static_assert(std::ranges::all_of(k_extractedLayouts, Consistent) && std::ranges::all_of(k_manualLayouts, Consistent));

const ComboLayout *FindLayout(const std::string_view shader)
{
    for (const auto &layouts : {std::span<const ComboLayout>(k_extractedLayouts), std::span<const ComboLayout>(k_manualLayouts)})
        if (const auto it = std::ranges::find(layouts, shader, &ComboLayout::shader); it != layouts.end())
            return &*it;
    return nullptr;
}

// Static combo id of `shader` with the named combos set; unnamed combos are 0.
uint32_t StaticComboId(std::string_view shader, std::initializer_list<std::pair<std::string_view, uint32_t>> values)
{
    const auto *layout = FindLayout(shader);
    if (!layout)
        throw std::runtime_error(std::format("no combo layout for {}", shader));
    uint32_t id = 0;
    for (const auto &[name, value] : values)
    {
        const auto slot = std::ranges::find_if(layout->combos, [&](const ComboSlot &s) { return s.name && name == s.name; });
        if (slot == layout->combos.end())
            throw std::runtime_error(std::format("{} has no combo {}", shader, name));
        if (value >= slot->count)
            throw std::runtime_error(std::format("{} combo {} = {} is outside 0..{}", shader, name, value, slot->count - 1));
        id += slot->weight * value;
    }
    return id;
}

// Dynamic combos for the viewport's draws: one directional light, no flashlight, no fog, uncompressed
// pre-skinned vertices. Values from the same DLL's dynamic GetIndex functions:
// vertexlit_and_unlit_generic_vs30 (0x10054190): COMPRESSED_VERTS 1, DYNAMIC_LIGHT 2, SKINNING 4, ...
// phong_ps30: NUM_LIGHTS has weight 4 (one light => 4).
constexpr uint32_t genericVsDynamicLight = 2;
constexpr uint32_t phongPsOneLight = 4;
constexpr uint32_t bumpPsOneLightAmbient = 2 + 10;
// Eyes_dx9 (0x1001AD10) VS dynamic: DYNAMIC_LIGHT weight 8 (COMPRESSED_VERTS 1, SKINNING 2, fog 4 below it).
constexpr uint32_t eyesVsDynamicLight = 8;
// EyeRefract_dx9 (0x10018420) VS dynamic: DYNAMIC_LIGHT weight 8, NUM_LIGHTS weight 16; PS dynamic NUM_LIGHTS weight 2.
constexpr uint32_t eyeRefractVsOneLight = 8 + 16;
constexpr uint32_t eyeRefractPsOneLight = 2;
// Teeth_DX9 (0x10049FF0): teeth_vs30 DYNAMIC_LIGHT weight 8 (COMPRESSED_VERTS 1, fog 2, SKINNING 4, STATIC_LIGHT 16,
// MORPHING 32); teeth_bump_ps30 NUM_LIGHTS weight 2 and AMBIENT_LIGHT weight 10 (fog 1, WRITE_DEPTH_TO_DESTALPHA 20).
constexpr uint32_t teethVsDynamicLight = 8;
constexpr uint32_t teethBumpPsOneLightAmbient = 2 + 10;

// What a VertexLitGeneric/UnlitGeneric/Phong material binds on every draw, in the registers the SFM shader reads.
struct GenericMaterial : Material
{
    std::vector<std::pair<int, std::unique_ptr<QOpenGLTexture>>> textures; // sampler unit, texture
    std::map<int, QVector4D> psConstants, vsConstants;                   // pc / vc register, value
    bool opacityFromBaseAlpha = false;
    // c1's inputs before GammaToLinear, so proxies can replace $color/$color2/$alpha per frame
    QVector4D color{1, 1, 1, 1}, color2{1, 1, 1, 1};
    float alpha = 1;
    bool hasDetail = false;

    QOpenGLTexture &addTexture(int unit, QOpenGLTexture::Target target = QOpenGLTexture::Target2D)
    {
        return *textures.emplace_back(unit, std::make_unique<QOpenGLTexture>(target)).second;
    }
};

float GammaToLinear(float gamma)
{
    gamma = std::clamp(gamma, 0.f, 1.f);
    return gamma <= 0.04045f ? gamma / 12.92f : std::pow((gamma + 0.055f) / 1.055f, 2.4f);
}

QVector4D GammaToLinear(const QVector4D &color)
{
    return {GammaToLinear(color.x()), GammaToLinear(color.y()), GammaToLinear(color.z()), color.w()};
}

// $envmap into `texture`; HLMV binds hlmv/cubemap as the local cubemap env_cubemap resolves to. False without $envmap
// unless `required` (EyeRefract always samples one).
bool LoadEnvmap(MaterialParams &params, QOpenGLTexture &texture, bool required = false)
{
    auto name = params.getString("$envmap");
    if (name.isEmpty() && !required)
        return false;
    if (name.isEmpty() || name.compare(QStringLiteral("env_cubemap"), Qt::CaseInsensitive) == 0)
        name = QStringLiteral("hlmv/cubemap");
    params.loadTexture(texture, name, true);
    return true;
}

// BaseVSShader::EvaluateBlendRequirements(BASETEXTURE): blend for alpha modulation (`alpha` < 1), $vertexalpha, or
// base alpha that means translucency (CBaseShader::TextureIsTranslucent: an alpha channel, $translucent or $alphatest,
// alpha not used for self-illumination or envmap masking) unless alpha tested; $additive picks the additive pair.
bool EvaluateBlend(MaterialParams &params, Material &material, bool baseAlpha, float alpha)
{
    const bool alphaTest = params.getInt("$alphatest") != 0;
    const bool alphaUsedElsewhere =
        (params.getInt("$selfillum") && !params.has("$selfillummask")) || params.getInt("$basealphaenvmapmask");
    const bool textureTranslucent = baseAlpha && !params.getInt("$opaquetexture") && !alphaUsedElsewhere &&
                                    (params.getInt("$translucent") || alphaTest);
    const bool blend = alpha < 1 || params.getInt("$vertexalpha") || (textureTranslucent && !alphaTest);
    material.blend = params.getInt("$additive") ? (blend ? MaterialBlend::BlendAdd : MaterialBlend::Add)
                     : blend ? MaterialBlend::Blend : MaterialBlend::None;
    material.alphaTest = alphaTest;
    if (const float reference = params.getFloat("$alphatestreference"); reference > 0)
        material.alphaTestReference = reference;
    return textureTranslucent;
}

// A colour parameter written as [r g b], {r g b} or one float for all channels (Source's material var parser
// accepts both); keeps `value.w`.
QVector4D GetColor(MaterialParams &params, const char *key, QVector4D value)
{
    const auto text = params.getString(key).trimmed();
    if (text.isEmpty())
        return value;
    if (text.front() == '[' || text.front() == '{')
    {
        params.getVector(key, value);
        return value;
    }
    const float scalar = params.getFloat(key);
    return {scalar, scalar, scalar, value.w()};
}

// $detailscale as one float or [x y]; Source's default is 4.
QVector2D DetailScale(MaterialParams &params)
{
    auto text = params.getString("$detailscale").trimmed();
    if (text.isEmpty())
        return {4, 4};
    if (text.front() == '[' || text.front() == '{')
        text = text.mid(1, text.size() - 2);
    const auto parts = text.split(' ', Qt::SkipEmptyParts);
    bool okX = false, okY = true;
    const float x = parts.value(0).toFloat(&okX);
    const float y = parts.size() > 1 ? parts[1].toFloat(&okY) : x;
    if (!okX || !okY || parts.size() > 2 || !std::isfinite(x) || !std::isfinite(y))
        params.fail(QStringLiteral("invalid $detailscale"));
    return {x, y};
}

// Eyes_dx9 0x1001AD10, EyeRefract_dx9 0x10018420
struct EyeMaterial : Material
{
    QOpenGLTexture iris{QOpenGLTexture::Target2D};
    QOpenGLTexture cornea{QOpenGLTexture::Target2D};      // EyeRefract sampler 0
    QOpenGLTexture ambientOccl{QOpenGLTexture::Target2D}; // EyeRefract sampler 3
    QOpenGLTexture lightwarp{QOpenGLTexture::Target2D};   // EyeRefract sampler 4, with $lightwarptexture
    QOpenGLTexture envmap{QOpenGLTexture::TargetCubeMap}; // EyeRefract sampler 2
    QVector4D const0;                                     // EyeRefract c0: $dilation, $glossiness, 0, $corneabumpstrength
    QVector4D ambientOcclColor;                           // EyeRefract c5: $ambientocclcolor, $ambientocclusion
    QVector4D const6;                                     // EyeRefract c6: 1, $eyeballradius, 1, $parallaxstrength
};

// HLMV has no procedural eye glint here and no SSAO buffer; the eye shaders bind these instead.
struct ConstantTextures
{
    QOpenGLTexture white{QOpenGLTexture::Target2D}, black{QOpenGLTexture::Target2D};

    void create()
    {
        if (!white.isCreated())
            UploadSolidColor(white, Qt::white);
        if (!black.isCreated())
            UploadSolidColor(black, Qt::black);
    }
};

enum class Inputs { Generic, Phong, Bump, Eye, Teeth, TeethBump };

// Vertex inputs and the constants every SFM program starts with: Source math constants, an identity
// view-projection for the probe quad, HLMV's default lights, and sampler units.
void InitStandard(ShaderProgram &state, Inputs inputs, bool lights)
{
    auto &program = state.gl;
    const bool phong = inputs == Inputs::Phong, eye = inputs == Inputs::Eye;
    const bool tangents = phong || inputs == Inputs::Bump; // same vertex layout: uv v2, normal v1, tangent v4
    const bool teeth = inputs == Inputs::Teeth || inputs == Inputs::TeethBump, teethBump = inputs == Inputs::TeethBump;
    state.position = program.attributeLocation("v0");
    state.uv = program.attributeLocation(tangents ? "v2" : eye ? "v1" : teeth ? "v2" : "v3");
    state.normal = eye ? -1 : program.attributeLocation("v1");
    state.tangent = tangents ? program.attributeLocation("v4") : teethBump ? program.attributeLocation("v3") : -1;
    state.color = inputs == Inputs::Generic ? program.attributeLocation("v2") : -1;
    // Flex delta streams (POSITION1, NORMAL1): vertices arrive already flexed.
    state.delta = eye ? program.attributeLocation("v2") : teeth ? program.attributeLocation(teethBump ? "v4" : "v3") : -1;
    state.deltaNormal = teeth ? program.attributeLocation(teethBump ? "v5" : "v4") : -1;
    if (inputs == Inputs::Bump)
    {
        state.delta = program.attributeLocation("v5");
        state.deltaNormal = program.attributeLocation("v6");
    }
    if (state.position < 0 || state.uv < 0 || ((tangents || teethBump) && state.tangent < 0))
        throw std::runtime_error("Shader does not have the expected SFM vertex inputs");

    const int lightCount = program.uniformLocation("i0");
    state.lit = lightCount >= 0;
    state.specular = phong;
    std::array<QVector4D, 97> vc{};
    vc[0] = {0, 1, 2, 0.5f}; // Source math constants, including the D3D-to-GL Z fixup.
    if (lights || state.lit)
    {
        // HLMV defaults: six equal ambient faces and one white directional light at yaw 180°.
        std::fill_n(vc.begin() + 21, 6, QVector4D(0.3f, 0.3f, 0.3f, 1));
        // eye_refract_vs30 evaluates four light slots unconditionally: give the unused ones no colour.
        for (int light = 0; light < 4; ++light)
        {
            const int base = 27 + 5 * light;
            vc[base] = light ? QVector4D(0, 0, 0, 1) : QVector4D(1, 1, 1, 1); // RGB, directional flag.
            vc[base + 1] = {-1, 0, 0, 0}; // Direction of travel, spot flag.
            vc[base + 2] = {0, 0, 0, 1};  // Position (unused for directional lights).
            vc[base + 3] = {0, 1, 1, 1};  // Non-spot defaults.
            vc[base + 4] = {1, 0, 0, 0};  // Constant attenuation.
        }
        if (state.lit)
            program.setUniformValue(lightCount, 1);
        if (program.uniformLocation("b0") >= 0) // static control flow light switches: one light
            program.setUniformValue("b0", 1);
    }
    vc[2] = {0, 0, 2, 0}; // Camera.
    vc[8] = {1, 0, 0, 0};
    vc[9] = {0, 1, 0, 0};
    vc[10] = {0, 0, 0, 0.5f}; // Put the quad halfway through the D3D clip depth range.
    vc[11] = {0, 0, 0, 1};
    vc[48] = {1, 0, 0, 0}; // Identity base-texture transform.
    vc[49] = {0, 1, 0, 0};
    vc[52] = vc[48];
    vc[53] = vc[49];
    constexpr QVector4D bones[] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    std::array<QVector4D, 31> pc{};
    pc[1] = {1, 1, 1, 1};  // White modulation, opaque alpha; Eyes: glint damping (ambient luminance above 0.01).
    pc[30] = {1, 1, 1, 1}; // LDR tone-mapping scales (x output, z envmap); fog is disabled by zero VS fog constants.
    program.setUniformValueArray("vc", vc.data(), state.lit ? int(vc.size()) : 67);
    program.setUniformValueArray("vcbones", bones, 3);
    program.setUniformValue("vcscreen", QVector4D{});
    program.setUniformValueArray("pc", pc.data(), pc.size());
    for (int unit = 0; unit < 16; ++unit) // units match register numbers; unused names resolve to no uniform
        program.setUniformValue(QStringLiteral("sampler%1").arg(unit).toLatin1().constData(), unit);
}

// Source's pixel shaders pack a directional light as a distant point.
QVector4D PackedLight(const FrameState &frame) { return QVector4D(frame.center - frame.lightDirection * 10000, 0); }

void SetStandardFrame(ShaderProgram &state, const FrameState &frame)
{
    auto &program = state.gl;
    program.setUniformValue("vc[2]", QVector4D(frame.camera, 0));
    program.setUniformValue("vc[28]", QVector4D(frame.lightDirection, 0));
    const std::array<QVector4D, 6> cube{frame.ambient, frame.ambient, frame.ambient, frame.ambient, frame.ambient, frame.ambient};
    program.setUniformValueArray("vc[21]", cube.data(), 6);
    program.setUniformValue("vc[27]", frame.light);
}

// Proxy outputs that replace static values (Material::UpdateProxies ran for this frame): $color/$color2/$alpha rebuild
// c1 (SetModulationPixelShaderDynamicState_LinearColorSpace) and $basetexturetransform replaces vc48/vc49
// (SetVertexShaderTextureTransform). Only for the generic/phong/bump vertex shaders, whose vc48 is that transform.
void ApplyProxies(QOpenGLShaderProgram &program, GenericMaterial &material, int detailRegister)
{
    if (!material.proxies)
        return;
    auto color = material.color, color2 = material.color2;
    float alpha = material.alpha;
    bool modulated = false;
    for (const auto &[key, target] : {std::pair{"$color", &color}, std::pair{"$color2", &color2}})
        if (const auto value = material.ProxyValue(key))
        {
            *target = {value->x(), value->y(), value->z(), 1};
            modulated = true;
        }
    if (const auto value = material.ProxyValue("$alpha"))
    {
        alpha = std::clamp(value->x(), 0.f, 1.f);
        modulated = true;
    }
    if (modulated)
        program.setUniformValue("pc[1]", GammaToLinear(QVector4D((color * color2).toVector3D(), alpha)));
    if (const auto transform = material.ProxyTransform("$basetexturetransform"))
    {
        program.setUniformValue("vc[48]", transform->row(0));
        program.setUniformValue("vc[49]", transform->row(1));
    }
    if (material.hasDetail)
        if (const auto factor = material.ProxyValue("$detailblendfactor"))
        {
            auto color = material.psConstants.at(detailRegister);
            color.setW(factor->x());
            program.setUniformValue(detailRegister == 0 ? "pc[0]" : "pc[4]", color);
        }
}

void BindMaterial(QOpenGLShaderProgram &program, GenericMaterial &material)
{
    material.BindBaseTexture();
    for (auto &[unit, texture] : material.textures)
        material.BindTexture(*texture, unit);
    for (const auto &[reg, value] : material.psConstants)
        program.setUniformValue(QStringLiteral("pc[%1]").arg(reg).toLatin1().constData(), value);
    for (const auto &[reg, value] : material.vsConstants)
        program.setUniformValue(QStringLiteral("vc[%1]").arg(reg).toLatin1().constData(), value);
}

class GenericShader : public Shader
{
public:
    void InitProgram(ShaderProgram &program, const ProgramContext &) const override
    {
        InitStandard(program, Inputs::Generic, false);
    }

    void SetFrame(ShaderProgram &program, const FrameState &frame) const override { SetStandardFrame(program, frame); }

    void SetDraw(ShaderProgram &program, Material &material, const FrameState &frame, const EyeState &) const override
    {
        auto &generic = static_cast<GenericMaterial &>(material);
        BindMaterial(program.gl, generic);
        ApplyProxies(program.gl, generic, 4);
        // The generic pixel shader uses c20.w to select texture alpha instead of 1 before modulation.
        program.gl.setUniformValue("pc[20]", QVector4D(frame.camera, generic.opacityFromBaseAlpha));
    }
};

class PhongShader : public Shader
{
public:
    void InitProgram(ShaderProgram &program, const ProgramContext &context) const override
    {
        if (!context.textured || !context.hasTangents)
            throw std::runtime_error("Phong needs a textured MDL with VVD tangents");
        InitStandard(program, Inputs::Phong, true);
    }

    void SetFrame(ShaderProgram &state, const FrameState &frame) const override
    {
        SetStandardFrame(state, frame);
        auto &program = state.gl;
        const std::array<QVector4D, 6> cube{frame.ambient, frame.ambient, frame.ambient, frame.ambient, frame.ambient, frame.ambient};
        program.setUniformValueArray("pc[4]", cube.data(), 6);
        program.setUniformValue("pc[11]", QVector4D(frame.camera, 0)); // GetWorldSpaceCameraPosition
        program.setUniformValue("pc[20]", QVector4D(frame.light.toVector3D(), 0));
        program.setUniformValue("pc[21]", PackedLight(frame));
    }

    void SetDraw(ShaderProgram &state, Material &base, const FrameState &frame, const EyeState &) const override
    {
        auto &material = static_cast<GenericMaterial &>(base);
        BindMaterial(state.gl, material);
        ApplyProxies(state.gl, material, 0);
        if (!frame.specular) // the probe's highlight check: Phong boost off
            state.gl.setUniformValue("pc[19]", QVector4D(material.psConstants[19].toVector3D(), 0));
    }
};

// vertexlit_and_unlit_generic_bump_vs30/ps30: VertexLitGeneric with a $bumpmap or $lightwarptexture. The pixel shader
// lights per pixel: ambient cube c5..c10, one light as colour c13 and packed position c14, eye c20, fog c21 (off).
class BumpShader : public Shader
{
public:
    void InitProgram(ShaderProgram &program, const ProgramContext &context) const override
    {
        if (!context.textured || !context.hasTangents)
            throw std::runtime_error("Bumped VertexLitGeneric needs a textured MDL with VVD tangents");
        InitStandard(program, Inputs::Bump, true);
    }

    void SetFrame(ShaderProgram &state, const FrameState &frame) const override
    {
        SetStandardFrame(state, frame);
        auto &program = state.gl;
        const std::array<QVector4D, 6> cube{frame.ambient, frame.ambient, frame.ambient, frame.ambient, frame.ambient, frame.ambient};
        program.setUniformValueArray("pc[5]", cube.data(), 6);
        program.setUniformValue("pc[13]", QVector4D(frame.light.toVector3D(), 0));
        program.setUniformValue("pc[14]", PackedLight(frame));
    }

    void SetDraw(ShaderProgram &state, Material &base, const FrameState &frame, const EyeState &) const override
    {
        auto &material = static_cast<GenericMaterial &>(base);
        BindMaterial(state.gl, material);
        ApplyProxies(state.gl, material, 4);
        state.gl.setUniformValue("pc[20]", QVector4D(frame.camera, material.opacityFromBaseAlpha));
    }
};

// Eyes_dx9 / EyeRefract_dx9 dynamic state for one eyeball mesh (CStudioRender::SetEyeMaterialVars values).
void SetEyeVertexState(QOpenGLShaderProgram &program, const EyeState &eye)
{
    program.setUniformValue("vc[48]", eye.origin);
    program.setUniformValue("vc[50]", eye.irisU);
    program.setUniformValue("vc[51]", eye.irisV);
}

class EyesShader : public Shader
{
public:
    explicit EyesShader(ConstantTextures &constants) : m_constants(constants) {}

    void InitProgram(ShaderProgram &program, const ProgramContext &) const override
    {
        m_constants.create();
        InitStandard(program, Inputs::Eye, false);
    }

    void SetFrame(ShaderProgram &state, const FrameState &frame) const override
    {
        SetStandardFrame(state, frame);
        state.gl.setUniformValue("pc[11]", QVector4D(frame.camera, 0));
    }

    void SetDraw(ShaderProgram &state, Material &base, const FrameState &, const EyeState &eye) const override
    {
        auto &material = static_cast<EyeMaterial &>(base);
        auto &program = state.gl;
        SetEyeVertexState(program, eye);
        material.BindBaseTexture();
        material.BindTexture(material.iris, 1);
        m_constants.black.bind(2); // shortcut: no procedural glint (studiorender's R_StudioEyeballGlint render target)
        program.setUniformValue("vc[49]", eye.up);
        program.setUniformValue("vc[52]", QVector4D()); // $glintu/$glintv project onto the unused glint
        program.setUniformValue("vc[53]", QVector4D());
        program.setUniformValue("pc[1]", QVector4D(1, 1, 1, 1)); // glint damping
    }

private:
    ConstantTextures &m_constants;
};

class EyeRefractShader : public Shader
{
public:
    explicit EyeRefractShader(ConstantTextures &constants) : m_constants(constants) {}

    void InitProgram(ShaderProgram &program, const ProgramContext &) const override
    {
        m_constants.create();
        InitStandard(program, Inputs::Eye, true);
    }

    void SetFrame(ShaderProgram &state, const FrameState &frame) const override
    {
        SetStandardFrame(state, frame);
        auto &program = state.gl;
        program.setUniformValue("pc[4]", QVector4D(frame.camera, 0)); // EyeRefract's camera register
        program.setUniformValue("pc[20]", QVector4D(frame.light.toVector3D(), 0));
        program.setUniformValue("pc[21]", PackedLight(frame));
        // PI_SetPixelShaderAmbientLightCubeLuminance(10): mean Rec.601 luminance of the cube, saturated.
        const auto &a = frame.ambient;
        const float luminance = std::clamp(0.3f * a.x() + 0.59f * a.y() + 0.11f * a.z(), 0.f, 1.f);
        program.setUniformValue("pc[10]", QVector4D(luminance, luminance, luminance, luminance));
    }

    void SetDraw(ShaderProgram &state, Material &base, const FrameState &, const EyeState &eye) const override
    {
        auto &material = static_cast<EyeMaterial &>(base);
        auto &program = state.gl;
        SetEyeVertexState(program, eye);
        material.BindTexture(material.cornea, 0);
        material.BindTexture(material.iris, 1);
        material.BindTexture(material.envmap, 2);
        material.BindTexture(material.ambientOccl, 3);
        if (auto &lightwarp = material.Resolve(material.lightwarp); lightwarp.isCreated())
            lightwarp.bind(4);
        else
            m_constants.white.bind(4);
        m_constants.white.bind(8); // no screen-space ambient occlusion buffer
        program.setUniformValue("pc[0]", material.const0);
        program.setUniformValue("pc[1]", eye.origin);
        program.setUniformValue("pc[2]", eye.irisU);
        program.setUniformValue("pc[3]", eye.irisV);
        program.setUniformValue("pc[5]", material.ambientOcclColor);
        program.setUniformValue("pc[6]", material.const6);
    }

private:
    ConstantTextures &m_constants;
};

// Teeth_DX9 (0x10049FF0), SM3 branch: teeth_vs30/ps30, or teeth_bump_vs30/ps30 with $bumpmap (per-pixel lighting
// and specular). vc48 = $forward, $illumfactor from studiorender's mouth; ps c11 = camera, bump $phongexponent.
class TeethShader : public Shader
{
public:
    void InitProgram(ShaderProgram &program, const ProgramContext &context) const override
    {
        const bool bump = program.combo.vs == "teeth_bump_vs30";
        if (bump && !context.hasTangents)
            throw std::runtime_error("Bumped Teeth needs VVD tangents");
        InitStandard(program, bump ? Inputs::TeethBump : Inputs::Teeth, true);
    }

    void SetFrame(ShaderProgram &state, const FrameState &frame) const override
    {
        SetStandardFrame(state, frame);
        auto &program = state.gl;
        // The bumped pixel shader lights per pixel: ambient cube c4..c9, light c20/c21 (PI_SetPixelShader*).
        const std::array<QVector4D, 6> cube{frame.ambient, frame.ambient, frame.ambient, frame.ambient, frame.ambient, frame.ambient};
        program.setUniformValueArray("pc[4]", cube.data(), 6);
        program.setUniformValue("pc[20]", QVector4D(frame.light.toVector3D(), 0));
        program.setUniformValue("pc[21]", PackedLight(frame));
    }

    void SetDraw(ShaderProgram &state, Material &base, const FrameState &frame, const EyeState &) const override
    {
        auto &material = static_cast<GenericMaterial &>(base);
        auto &program = state.gl;
        BindMaterial(program, material);
        program.setUniformValue("vc[48]", frame.mouth.value_or(material.vsConstants[48]));
        program.setUniformValue("pc[11]", QVector4D(frame.camera, material.psConstants[11].w()));
    }
};

class SfmShaderSet final : public ShaderSet
{
public:
    std::unique_ptr<Material> CreateMaterial(MaterialParams &params) const override
    {
        if (params.isShader("Eyes") || params.isShader("EyeRefract"))
            return CreateEyeMaterial(params, params.isShader("EyeRefract"));
        if (params.isShader("Teeth"))
            return CreateTeethMaterial(params);
        const bool unlit = params.isShader("UnlitGeneric");
        if (!unlit && !params.isShader("VertexLitGeneric"))
            params.fail(QStringLiteral("unsupported material shader %1").arg(params.shaderName()));
        return CreateGenericMaterial(params, unlit);
    }

    const Shader &FallbackShader() const override { return m_generic; }

private:
    // Teeth_DX9 (0x10049FF0) static/dynamic state; the shader's own params are $illumfactor, $forward, $bumpmap and
    // $phongexponent (studiorender overwrites the first two for models with a mouth).
    std::unique_ptr<Material> CreateTeethMaterial(MaterialParams &params) const
    {
        auto out = std::make_unique<GenericMaterial>();
        out->shader = &m_teeth;
        bool baseAlpha = false;
        params.getTexture(out->base, "$basetexture", Qt::white, true, &baseAlpha);
        params.accept({"$model", "$surfaceprop", "$nodecal", "$clientshader"});
        const bool bump = params.has("$bumpmap");
        if (bump)
            params.getTexture(out->addTexture(1), "$bumpmap", QColor(128, 128, 255), false);
        QVector4D forward{1, 0, 0, 0};
        params.getVector("$forward", forward);
        out->vsConstants[48] = QVector4D(forward.toVector3D(), params.getFloat("$illumfactor", 1));
        out->psConstants[11] = {0, 0, 0, bump ? params.getFloat("$phongexponent", 100) : 0};
        EvaluateBlend(params, *out, baseAlpha, std::clamp(params.getFloat("$alpha", 1), 0.f, 1.f));
        auto &combo = out->combo;
        combo.vs = bump ? "teeth_bump_vs30" : "teeth_vs30";
        combo.ps = bump ? "teeth_bump_ps30" : "teeth_ps30";
        combo.vsStatic = StaticComboId(combo.vs, {{"DOPIXELFOG", 1}});
        combo.vsDynamic = bump ? 0 : teethVsDynamicLight;
        combo.psDynamic = bump ? teethBumpPsOneLightAmbient : 0;
        return out;
    }

    // VertexLitGeneric/UnlitGeneric and $phong. Registers and samplers follow the SFM shaders (SDK 2013 layout,
    // checked against the translated GLSL); parameter handling ports CS:GO's vertexlitgeneric_dx9_helper.cpp and
    // phong_dx9_helper.cpp. No flashlight, seamless mapping or distance alpha.
    std::unique_ptr<Material> CreateGenericMaterial(MaterialParams &params, bool unlit) const
    {
        const bool phong = !unlit && params.getInt("$phong");
        auto out = std::make_unique<GenericMaterial>();
        const bool bump = !phong && !unlit && params.has("$bumpmap");
        const bool diffuseWarp = !phong && !unlit && params.has("$lightwarptexture");
        out->shader = phong ? static_cast<const Shader *>(&m_phong) : bump || diffuseWarp ? static_cast<const Shader *>(&m_bump) : &m_generic;
        auto &ps = out->psConstants;
        // Source binds its white texture when $basetexture is absent (SFM HLMV renders such meshes white).
        bool baseAlpha = false;
        params.getTexture(out->base, "$basetexture", Qt::white, true, &baseAlpha);
        params.accept({"$model", "$surfaceprop", "$nodecal"});

        // c1 (SetModulationPixelShaderDynamicState_LinearColorSpace): GammaToLinear($color * $color2), $alpha.
        out->color = GetColor(params, "$color", {1, 1, 1, 1});
        out->color2 = GetColor(params, "$color2", {1, 1, 1, 1});
        out->alpha = std::clamp(params.getFloat("$alpha", 1), 0.f, 1.f);
        const auto modulation = QVector4D((out->color * out->color2).toVector3D(), out->alpha);
        // Materials sharing a program must reset the transform after another draw's TextureScroll.
        out->vsConstants[48] = {1, 0, 0, 0};
        out->vsConstants[49] = {0, 1, 0, 0};
        ps[1] = GammaToLinear(modulation);

        out->opacityFromBaseAlpha = EvaluateBlend(params, *out, baseAlpha, modulation.w());

        const auto texture = [&](int unit, const char *key, QColor fallback, bool srgb) {
            if (!params.has(key))
                return false;
            params.getTexture(out->addTexture(unit), key, fallback, srgb);
            return true;
        };
        // $detail on sampler `unit`; $detailscale scales the base UVs into vc[52..53]
        // (SetVertexShaderTextureScaledTransform). Returns the blend mode, or -1 without a detail texture.
        const auto detail = [&](int unit, int modes) {
            if (!params.has("$detail"))
                return -1;
            const int mode = params.getInt("$detailblendmode");
            if (mode < 0 || mode >= modes)
                params.fail(QStringLiteral("unsupported $detailblendmode %1").arg(mode));
            // IsSRGBDetailTexture: detail over base, fade and base over detail blend colours.
            texture(unit, "$detail", Qt::gray, mode == 2 || mode == 3 || mode == 4);
            const auto scale = DetailScale(params);
            out->vsConstants[52] = {scale.x(), 0, 0, 0};
            out->vsConstants[53] = {0, scale.y(), 0, 0};
            out->hasDetail = true;
            return mode;
        };
        const bool halfLambert = params.getInt("$halflambert") != 0;
        const bool pixelFog = params.getInt("$vertexfog") == 0; // !( $flags & MATERIAL_VAR_VERTEXFOG )
        const bool selfIllum = params.getInt("$selfillum") != 0;
        const auto selfIllumTint = GetColor(params, "$selfillumtint", {1, 1, 1, 0});
        const float tintFactor = params.getInt("$notint") ? -1.f : params.getInt("$blendtintbybasealpha") ? 0.f : 1.f;
        const auto loadEnvmap = [&](int unit) {
            return params.has("$envmap") && LoadEnvmap(params, out->addTexture(unit, QOpenGLTexture::TargetCubeMap));
        };
        auto &combo = out->combo;
        if (bump || diffuseWarp)
        {
            const int detailMode = detail(2, 7);
            const bool selfIllumFresnel = selfIllum && detailMode < 0 && params.getInt("$selfillumfresnel") != 0;
            const bool envmapFromNormal = bump && !selfIllumFresnel && params.getInt("$normalmapalphaenvmapmask") != 0;
            // An explicit or base-alpha mask cannot be sampled by the bump variant; normal alpha takes precedence.
            const bool envmap = (!bump || envmapFromNormal ||
                                 (!params.getInt("$basealphaenvmapmask") && !params.has("$envmapmask"))) && loadEnvmap(1);
            const bool lightwarp = diffuseWarp && !selfIllumFresnel && texture(9, "$lightwarptexture", Qt::white, false);
            params.getTexture(out->addTexture(3), "$bumpmap", QColor(128, 128, 255), false);
            if (envmap)
            {
                ps[0] = GammaToLinear(GetColor(params, "$envmaptint", {1, 1, 1, 0}));
                const float contrast = params.getFloat("$envmapcontrast");
                ps[2] = {contrast, contrast, contrast, 0};
                ps[3] = GetColor(params, "$envmapsaturation", {1, 1, 1, 0});
            }
            ps[4] = QVector4D(selfIllumTint.toVector3D(), detailMode >= 0 ? params.getFloat("$detailblendfactor", 1) : 1);
            if (selfIllumFresnel)
            {
                QVector4D minMaxExp{0, 1, 1, 0};
                params.getVector("$selfillumfresnelminmaxexp", minMaxExp);
                const float bias = minMaxExp.y() != 0 ? minMaxExp.x() / minMaxExp.y() : 0;
                ps[11] = {1 - bias, bias, minMaxExp.z(), minMaxExp.y()};
            }
            ps[12] = {tintFactor, 0, 0, 0};
            combo.vs = "vertexlit_and_unlit_generic_bump_vs30";
            combo.ps = "vertexlit_and_unlit_generic_bump_ps30";
            combo.vsStatic = StaticComboId(combo.vs, {{"HALFLAMBERT", halfLambert}, {"USE_WITH_2B", 1},
                                                       {"DECAL", params.getInt("$decal") != 0}, {"DOPIXELFOG", pixelFog}});
            combo.psStatic = StaticComboId(combo.ps, {
                {"CUBEMAP", envmap}, {"DIFFUSELIGHTING", 1}, {"LIGHTWARPTEXTURE", lightwarp},
                {"SELFILLUM", selfIllum && (baseAlpha || selfIllumFresnel)}, {"SELFILLUMFRESNEL", selfIllumFresnel},
                {"NORMALMAPALPHAENVMAPMASK", envmap && envmapFromNormal}, {"HALFLAMBERT", halfLambert},
                {"DETAILTEXTURE", detailMode >= 0}, {"DETAIL_BLEND_MODE", uint32_t(std::max(detailMode, 0))}, {"DOPIXELFOG", pixelFog}});
            combo.psDynamic = bumpPsOneLightAmbient;
            return out;
        }
        if (!phong)
        {
            const int detailMode = detail(2, 10);
            const bool envmap = loadEnvmap(1);
            const bool envmapMask = texture(4, "$envmapmask", Qt::white, false);
            const bool selfIllumMask = selfIllum && texture(11, "$selfillummask", Qt::white, false);
            const bool selfIllumInEnvmapMask = params.getInt("$selfillum_envmapmask_alpha") != 0 && envmapMask;
            const bool baseAlphaEnvmapMask = params.getInt("$basealphaenvmapmask") != 0;
            if (envmap)
            {
                ps[0] = GammaToLinear(GetColor(params, "$envmaptint", {1, 1, 1, 0}));
                const float contrast = params.getFloat("$envmapcontrast");
                ps[2] = {contrast, contrast, contrast, 0};
            }
            ps[3] = QVector4D(GetColor(params, "$envmapsaturation", {1, 1, 1, 0}).toVector3D(), selfIllumMask);
            ps[4] = QVector4D(selfIllumTint.toVector3D(), detailMode >= 0 ? params.getFloat("$detailblendfactor", 1) : 1);
            if (detailMode >= 0)
                ps[10] = GammaToLinear(GetColor(params, "$detailtint", {1, 1, 1, 0}));
            ps[11] = {params.getFloat("$selfillummaskscale", 1), 0, 0, 0};
            ps[12] = {tintFactor, 0, 0, 0}; // shader controls: tint by base alpha, no depth/fog alpha, no vertex alpha
            combo.vs = "vertexlit_and_unlit_generic_vs30";
            combo.ps = "vertexlit_and_unlit_generic_ps30";
            combo.vsStatic = StaticComboId(combo.vs, {{"CUBEMAP", envmap}, {"HALFLAMBERT", halfLambert && !unlit}, {"DOPIXELFOG", pixelFog}});
            combo.vsDynamic = unlit ? 0 : genericVsDynamicLight;
            combo.psStatic = StaticComboId(combo.ps, {
                {"DETAILTEXTURE", detailMode >= 0}, {"DETAIL_BLEND_MODE", uint32_t(std::max(detailMode, 0))}, {"CUBEMAP", envmap},
                {"DIFFUSELIGHTING", !unlit}, {"ENVMAPMASK", envmapMask && (envmap || selfIllumInEnvmapMask)},
                {"BASEALPHAENVMAPMASK", baseAlphaEnvmapMask && envmap}, {"SELFILLUM", selfIllum},
                {"SELFILLUM_ENVMAPMASK_ALPHA", selfIllumInEnvmapMask}, {"DOPIXELFOG", pixelFog}});
            return out;
        }

        // SFM phong draw (0x100380F0): bPhongHalfLambert is true unless $phongdisablehalflambert is set.
        const bool phongHalfLambert = params.getInt("$phongdisablehalflambert") == 0;
        const bool exponentTexture = params.has("$phongexponenttexture");
        params.getTexture(out->addTexture(7), "$phongexponenttexture", Qt::white, false);
        const bool lightwarp = texture(2, "$lightwarptexture", Qt::white, false);
        const bool phongWarp = texture(1, "$phongwarptexture", Qt::white, false);
        const int detailMode = detail(13, 7);
        const bool envmap = loadEnvmap(8);
        const bool normalAlphaEnvmapMask = envmap && params.getInt("$normalmapalphaenvmapmask") != 0;
        const bool baseAlphaPhongMask = params.getInt("$basemapalphaphongmask") != 0;
        const bool baseLuminancePhongMask = params.getInt("$basemapluminancephongmask") != 0;
        // SFM reads the Phong mask from normal alpha by default. TF materials that disable it get opaque normal alpha when
        // nothing else reads that channel and GL can swizzle.
        bool opaqueNormalAlpha = false;
        if (params.getInt("$bumpmapalphaphongmask", 1) == 0 && !baseAlphaPhongMask && !baseLuminancePhongMask && params.has("$bumpmap"))
        {
            if (!normalAlphaEnvmapMask && QOpenGLTexture::hasFeature(QOpenGLTexture::Swizzle))
                opaqueNormalAlpha = true;
            else
                out->ignored.append(QStringLiteral("$bumpmapalphaphongmask (SFM cannot separate Phong and envmap normal alpha, or GL lacks swizzle)"));
        }
        params.getTexture(out->addTexture(3), "$bumpmap", QColor(128, 128, 255), false, nullptr, opaqueNormalAlpha);
        const bool selfIllumMask = selfIllum && texture(14, "$selfillummask", Qt::white, false);
        if (selfIllum && !selfIllumMask)
            UploadSolidColor(out->addTexture(14), Qt::black);
        const bool selfIllumFresnel = selfIllum && params.getInt("$selfillumfresnel") != 0;
        const bool rimLight = params.getInt("$rimlight") != 0;

        const float exponent = params.getFloat("$phongexponent");
        QVector4D fresnelBoost{0, 0.5f, 1, params.getFloat("$phongboost", 1)};
        if (!std::isfinite(exponent) || exponent < 0 || !std::isfinite(fresnelBoost.w()))
            params.fail(QStringLiteral("invalid Phong exponent or boost"));
        params.getVector("$phongfresnelranges", fresnelBoost);
        auto tint = GetColor(params, "$phongtint", {1, 1, 1, 4});
        if (rimLight)
            tint.setW(std::max(params.getFloat("$rimlightexponent", 4), 1.f));
        const bool albedoTint = params.getInt("$phongalbedotint") != 0;
        if (tint.toVector3D().isNull()) // no constant tint: the exponent map's albedo tint, or white
            tint = albedoTint && exponentTexture ? QVector4D(-1, 0, 0, tint.w()) : QVector4D(1, 1, 1, tint.w());

        ps[0] = QVector4D(selfIllumTint.toVector3D(),
                          detailMode >= 0 ? params.getFloat("$detailblendfactor", 1) : params.getFloat("$phongalbedoboost", 1));
        if (envmap)
            ps[2] = QVector4D(GetColor(params, "$envmaptint", {1, 1, 1, 0}).toVector3D(), normalAlphaEnvmapMask);
        if (selfIllumFresnel)
        {
            QVector4D minMaxExp{0, 1, 1, 0};
            params.getVector("$selfillumfresnelminmaxexp", minMaxExp);
            const float bias = minMaxExp.y() != 0 ? minMaxExp.x() / minMaxExp.y() : 0;
            ps[3] = {1 - bias, bias, minMaxExp.z(), minMaxExp.y()};
        }
        ps[10] = {envmap ? params.getFloat("$envmapfresnel") : 0, float(baseLuminancePhongMask), exponent,
                  float(selfIllumMask)};
        if (rimLight)
            ps[13] = {float(exponentTexture && params.getInt("$rimmask") != 0), 0, 0, 0};
        ps[14] = {1, 1, 1, rimLight ? params.getFloat("$rimlightboost", 1) : 1};
        ps[19] = fresnelBoost;
        ps[26] = tint;
        ps[27] = {float(baseAlphaPhongMask), 0, tintFactor, float(params.getInt("$invertphongmask") != 0)};
        combo.vs = "phong_vs30";
        combo.ps = "phong_ps30";
        combo.vsStatic = StaticComboId(combo.vs, {{"DOPIXELFOG", pixelFog}});
        combo.psStatic = StaticComboId(combo.ps, {
            {"CUBEMAP", envmap}, {"SELFILLUM", selfIllum}, {"SELFILLUMFRESNEL", selfIllumFresnel}, {"LIGHTWARPTEXTURE", lightwarp},
            {"PHONGWARPTEXTURE", phongWarp}, {"DETAIL_BLEND_MODE", uint32_t(std::max(detailMode, 0))}, {"DETAILTEXTURE", detailMode >= 0},
            {"RIMLIGHT", rimLight}, {"PHONG_HALFLAMBERT", phongHalfLambert}});
        combo.psDynamic = phongPsOneLight;
        return out;
    }

    // SM3 branches only; no intro, flashlight or world-normal pass.
    std::unique_ptr<Material> CreateEyeMaterial(MaterialParams &params, bool refract) const
    {
        auto out = std::make_unique<EyeMaterial>();
        out->shader = refract ? static_cast<const Shader *>(&m_eyeRefract) : &m_eyes;
        params.getTexture(out->base, "$basetexture", Qt::white, true);
        params.getTexture(out->iris, "$iris", Qt::white, true);
        params.accept({"$model", "$surfaceprop", "$nodecal"});
        const bool halfLambert = params.getInt("$halflambert") != 0;
        auto &combo = out->combo;
        if (!refract)
        {
            combo.vs = "eyes_vs30";
            combo.ps = "eyes_ps30";
            combo.vsStatic = StaticComboId(combo.vs, {{"HALFLAMBERT", halfLambert}, {"DOPIXELFOG", 1}});
            combo.vsDynamic = eyesVsDynamicLight;
            return out;
        }

        params.getTexture(out->cornea, "$corneatexture", QColor(128, 128, 0, 0), false);
        params.getTexture(out->ambientOccl, "$ambientoccltexture", Qt::white, true);
        LoadEnvmap(params, out->envmap, true);
        const bool lightwarp = params.has("$lightwarptexture");
        if (lightwarp)
            params.getTexture(out->lightwarp, "$lightwarptexture", Qt::white, false);
        const bool sphereTexKill = params.getInt("$spheretexkillcombo") != 0;
        const bool raytraceSphere = params.getInt("$raytracesphere") != 0;
        // EyeRefract_dx9 dynamic state (0x10018420) and its parameter defaults.
        out->const0 = {params.getFloat("$dilation", 0.5f), params.getFloat("$glossiness", 1), 0, params.getFloat("$corneabumpstrength", 1)};
        out->ambientOcclColor = {0.33f, 0.33f, 0.33f, std::clamp(params.getFloat("$ambientocclusion", 0), 0.f, 1.f)};
        params.getVector("$ambientocclcolor", out->ambientOcclColor);
        out->const6 = {1, params.getFloat("$eyeballradius", 0.5f), 1, params.getFloat("$parallaxstrength", 0.25f)};
        combo.vs = "eye_refract_vs30";
        combo.ps = "eye_refract_ps30";
        combo.vsStatic = StaticComboId(combo.vs, {{"HALFLAMBERT", halfLambert}, {"LIGHTWARPTEXTURE", lightwarp}, {"DOPIXELFOG", 1}});
        combo.vsDynamic = eyeRefractVsOneLight;
        combo.psStatic = StaticComboId(combo.ps, {{"LIGHTWARPTEXTURE", lightwarp}, {"SPHERETEXKILLCOMBO", sphereTexKill},
                                                  {"RAYTRACESPHERE", raytraceSphere}});
        combo.psDynamic = eyeRefractPsOneLight;
        return out;
    }

    mutable ConstantTextures m_constants;
    GenericShader m_generic;
    BumpShader m_bump;
    PhongShader m_phong;
    EyesShader m_eyes{m_constants};
    EyeRefractShader m_eyeRefract{m_constants};
    TeethShader m_teeth;
};
} // namespace

std::unique_ptr<ShaderSet> CreateSfmShaderSet() { return std::make_unique<SfmShaderSet>(); }
