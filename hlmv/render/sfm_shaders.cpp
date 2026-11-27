// SFM stdshader_dx9 port: VertexLitGeneric/UnlitGeneric (incl. $phong), Eyes and EyeRefract. Static combo
// layouts come from tools/sfm_shader_combos.py (sfm_shader_combos.inc) and hand-derived rows
// (sfm_shader_combos_manual.inc); combo selection and constant registers follow the SFM DLL's draw functions.
// SFM runs at dxlevel 95, so every shader takes its SM3 (vs30/ps30) branch.

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
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
// Eyes_dx9 (0x1001AD10) VS dynamic: DYNAMIC_LIGHT weight 8 (COMPRESSED_VERTS 1, SKINNING 2, fog 4 below it).
constexpr uint32_t eyesVsDynamicLight = 8;
// EyeRefract_dx9 (0x10018420) VS dynamic: DYNAMIC_LIGHT weight 8, NUM_LIGHTS weight 16; PS dynamic NUM_LIGHTS weight 2.
constexpr uint32_t eyeRefractVsOneLight = 8 + 16;
constexpr uint32_t eyeRefractPsOneLight = 2;

struct GenericMaterial : Material
{
    QVector4D modulation{1, 1, 1, 1}; // pixel shader c1: linear($color * $color2), $alpha
};

struct PhongMaterial : GenericMaterial
{
    QOpenGLTexture normal{QOpenGLTexture::Target2D};
    QOpenGLTexture exponentMap{QOpenGLTexture::Target2D};
    QVector4D fresnelBoost{0, 0.5f, 1, 1};
    QVector4D tint{1, 1, 1, 4};
    float exponent = 0;
    bool alphaMask = false;
    bool luminanceMask = false;
};

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

enum class Inputs { Generic, Phong, Eye };

// Vertex inputs and the constants every SFM program starts with: Source math constants, an identity
// view-projection for the probe quad, HLMV's default lights, and sampler units.
void InitStandard(ShaderProgram &state, Inputs inputs, bool lights, std::initializer_list<int> samplers)
{
    auto &program = state.gl;
    const bool phong = inputs == Inputs::Phong, eye = inputs == Inputs::Eye;
    state.position = program.attributeLocation("v0");
    state.uv = program.attributeLocation(phong ? "v2" : eye ? "v1" : "v3");
    state.normal = eye ? -1 : program.attributeLocation("v1");
    state.tangent = phong ? program.attributeLocation("v4") : -1;
    state.color = inputs == Inputs::Generic ? program.attributeLocation("v2") : -1;
    state.delta = eye ? program.attributeLocation("v2") : -1;
    if (state.position < 0 || state.uv < 0 || (phong && state.tangent < 0))
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
        if (phong)
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
    pc[30] = {1, 0, 0, 0}; // Identity output scale; fog is disabled by zero VS fog constants.
    program.setUniformValueArray("vc", vc.data(), state.lit ? int(vc.size()) : 67);
    program.setUniformValueArray("vcbones", bones, 3);
    program.setUniformValue("vcscreen", QVector4D{});
    program.setUniformValueArray("pc", pc.data(), pc.size());
    program.setUniformValue("sampler0", 0);
    for (const int unit : samplers)
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

class GenericShader : public Shader
{
public:
    void InitProgram(ShaderProgram &program, const ProgramContext &) const override
    {
        InitStandard(program, Inputs::Generic, false, {});
    }

    void SetFrame(ShaderProgram &program, const FrameState &frame) const override { SetStandardFrame(program, frame); }

    void SetDraw(ShaderProgram &program, Material &material, const FrameState &, const EyeState &) const override
    {
        material.base.bind(0);
        program.gl.setUniformValue("pc[1]", static_cast<GenericMaterial &>(material).modulation);
    }
};

class PhongShader : public GenericShader
{
public:
    void InitProgram(ShaderProgram &program, const ProgramContext &context) const override
    {
        if (!context.textured || !context.hasTangents)
            throw std::runtime_error("Phong needs a textured MDL with VVD tangents");
        InitStandard(program, Inputs::Phong, true, {3, 7});
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

    void SetDraw(ShaderProgram &state, Material &base, const FrameState &frame, const EyeState &eye) const override
    {
        GenericShader::SetDraw(state, base, frame, eye);
        auto &material = static_cast<PhongMaterial &>(base);
        auto &program = state.gl;
        material.normal.bind(3);
        material.exponentMap.bind(7);
        auto fresnelBoost = material.fresnelBoost;
        if (!frame.specular)
            fresnelBoost.setW(0);
        program.setUniformValue("pc[10]", QVector4D(0, material.luminanceMask, material.exponent, 0));
        program.setUniformValue("pc[19]", fresnelBoost);
        program.setUniformValue("pc[26]", material.tint);
        program.setUniformValue("pc[27]", QVector4D(material.alphaMask, 0, 1, 0));
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
        InitStandard(program, Inputs::Eye, false, {1, 2});
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
        material.base.bind(0);
        material.iris.bind(1);
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
        InitStandard(program, Inputs::Eye, true, {1, 2, 3, 4, 8});
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
        material.cornea.bind(0);
        material.iris.bind(1);
        material.envmap.bind(2);
        material.ambientOccl.bind(3);
        (material.lightwarp.isCreated() ? material.lightwarp : m_constants.white).bind(4);
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

class SfmShaderSet final : public ShaderSet
{
public:
    std::unique_ptr<Material> CreateMaterial(MaterialParams &params) const override
    {
        if (params.isShader("Eyes") || params.isShader("EyeRefract"))
            return CreateEyeMaterial(params, params.isShader("EyeRefract"));
        const bool unlit = params.isShader("UnlitGeneric");
        if (!unlit && !params.isShader("VertexLitGeneric"))
            params.fail(QStringLiteral("unsupported material shader %1").arg(params.shaderName()));
        return CreateGenericMaterial(params, unlit);
    }

    const Shader &FallbackShader() const override { return m_generic; }

private:
    std::unique_ptr<Material> CreateGenericMaterial(MaterialParams &params, bool unlit) const
    {
        const bool phong = !unlit && params.getInt("$phong");
        std::unique_ptr<GenericMaterial> out = phong ? std::make_unique<PhongMaterial>() : std::make_unique<GenericMaterial>();
        out->shader = phong ? static_cast<const Shader *>(&m_phong) : &m_generic;
        // Source binds its white texture when $basetexture is absent (SFM HLMV renders such meshes white).
        params.getTexture(out->base, "$basetexture", Qt::white, true);
        params.accept({"$model", "$surfaceprop", "$nocull"});

        // Pixel shader c1 (SetModulationPixelShaderDynamicState_LinearColorSpace): GammaToLinear($color * $color2), $alpha.
        QVector4D color{1, 1, 1, 1}, color2{1, 1, 1, 1};
        params.getVector("$color", color);
        params.getVector("$color2", color2);
        const auto linear = [](float gamma) {
            gamma = std::clamp(gamma, 0.f, 1.f);
            return gamma <= 0.04045f ? gamma / 12.92f : std::pow((gamma + 0.055f) / 1.055f, 2.4f);
        };
        const auto tint = color * color2;
        out->modulation = {linear(tint.x()), linear(tint.y()), linear(tint.z()), std::clamp(params.getFloat("$alpha", 1), 0.f, 1.f)};

        const bool translucent = params.getInt("$translucent") != 0, additive = params.getInt("$additive") != 0;
        const bool alphaTest = params.getInt("$alphatest") != 0;
        out->blend = additive ? (translucent ? MaterialBlend::TranslucentAdditive : MaterialBlend::Additive)
            : translucent ? MaterialBlend::Translucent
            : alphaTest ? MaterialBlend::AlphaTest : MaterialBlend::Opaque;
        if (const float reference = params.getFloat("$alphatestreference"); reference > 0 && reference <= 1)
            out->alphaTestReference = reference;

        const bool halfLambert = params.getInt("$halflambert") != 0;
        const bool pixelFog = params.getInt("$vertexfog") == 0; // !( $flags & MATERIAL_VAR_VERTEXFOG )
        auto &combo = out->combo;
        if (!phong)
        {
            combo.vs = "vertexlit_and_unlit_generic_vs30";
            combo.ps = "vertexlit_and_unlit_generic_ps30";
            combo.vsStatic = StaticComboId(combo.vs, {{"HALFLAMBERT", halfLambert && !unlit}, {"DOPIXELFOG", pixelFog}});
            combo.vsDynamic = unlit ? 0 : genericVsDynamicLight;
            combo.psStatic = StaticComboId(combo.ps, {{"DIFFUSELIGHTING", !unlit}, {"DOPIXELFOG", pixelFog}});
            return out;
        }

        auto &material = static_cast<PhongMaterial &>(*out);
        // SFM phong draw (0x100380F0): bPhongHalfLambert is true unless $phongdisablehalflambert is set.
        const bool phongHalfLambert = params.getInt("$phongdisablehalflambert") == 0;
        combo.vs = "phong_vs30";
        combo.ps = "phong_ps30";
        combo.vsStatic = StaticComboId(combo.vs, {{"DOPIXELFOG", pixelFog}});
        combo.psStatic = StaticComboId(combo.ps, {{"PHONG_HALFLAMBERT", phongHalfLambert}});
        combo.psDynamic = phongPsOneLight;
        material.exponent = params.getFloat("$phongexponent");
        material.fresnelBoost.setW(params.getFloat("$phongboost", 1));
        if (!std::isfinite(material.exponent) || material.exponent < 0 || !std::isfinite(material.fresnelBoost.w()))
            params.fail(QStringLiteral("invalid Phong exponent or boost"));
        params.getVector("$phongfresnelranges", material.fresnelBoost);
        params.getVector("$phongtint", material.tint);
        material.alphaMask = params.getInt("$basemapalphaphongmask") != 0;
        material.luminanceMask = params.getInt("$basemapluminancephongmask") != 0;
        params.getTexture(material.normal, "$bumpmap", QColor(128, 128, 255), false);
        params.getTexture(material.exponentMap, "$phongexponenttexture", Qt::white, false);
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
        // HLMV binds hlmv/cubemap as the local cubemap that env_cubemap resolves to.
        auto envmap = params.getString("$envmap");
        if (envmap.isEmpty() || envmap.compare(QStringLiteral("env_cubemap"), Qt::CaseInsensitive) == 0)
            envmap = QStringLiteral("hlmv/cubemap");
        params.loadTexture(out->envmap, envmap, true);
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
    PhongShader m_phong;
    EyesShader m_eyes{m_constants};
    EyeRefractShader m_eyeRefract{m_constants};
};
} // namespace

std::unique_ptr<ShaderSet> CreateSfmShaderSet() { return std::make_unique<SfmShaderSet>(); }
