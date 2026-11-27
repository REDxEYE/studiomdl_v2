#pragma once

// Game-agnostic shader layer between the renderer (viewport.cpp) and one game's shader DLL port
// (sfm_shaders.cpp for SFM's stdshader_dx9). A ShaderSet turns VMTs into materials and owns the Shader
// objects that know a shader's VCS combos and constant registers; the renderer only links programs per
// combo and calls the Shader hooks with engine-level state (camera, lights, eyeballs).

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <QOpenGLShaderProgram>
#include <QVector3D>
#include <QVector4D>

class Material;
class MaterialParams;

// VCS file stems and combo indices of one vertex/pixel shader pair.
struct ShaderCombo
{
    std::string vs, ps;
    uint32_t vsStatic = 0, vsDynamic = 0, psStatic = 0, psDynamic = 0;
    auto operator<=>(const ShaderCombo &) const = default;
};

// One linked program; the Shader that initialised it fills the vertex input locations (-1 = unused).
struct ShaderProgram
{
    QOpenGLShaderProgram gl;
    ShaderCombo combo;
    int position = -1, uv = -1, normal = -1, tangent = -1, color = -1;
    int delta = -1, deltaNormal = -1; // POSITION1 / NORMAL1 flex delta streams, fed a constant zero
    bool lit = false;      // vertex lighting applied (the probe's lighting check)
    bool specular = false; // output depends on FrameState::specular (the probe's highlight check)
};

// Mesh facts a shader may require of the program it initialises.
struct ProgramContext
{
    bool textured = false; // real MDL geometry and materials, not the probe quad or checker
    bool hasTangents = false;
    QVector3D center;      // model centre, where distant lights are packed around
};

// Per-frame engine state: HLMV's one directional light and uniform ambient cube.
struct FrameState
{
    QVector3D camera;         // model space
    QVector3D lightDirection; // direction of travel
    QVector3D center;
    QVector4D light{1, 1, 1, 1}, ambient{0.3f, 0.3f, 0.3f, 1};
    bool specular = true;
    // studiorender's R_MouthSetupVertexShader for mouth 0: -forward in model space and the mouth flex weight
    // (Teeth's $forward, $illumfactor); unset for models without mouths, where the VMT values apply.
    std::optional<QVector4D> mouth;
};

// studiorender's per-eyeball state (R_StudioEyeballPosition) as SetEyeMaterialVars hands it to eye shaders.
struct EyeState
{
    QVector4D origin, up, irisU, irisV;
};

class Shader
{
public:
    virtual ~Shader() = default;
    // Vertex inputs, static constants and sampler units of a freshly linked, bound program. Throws
    // std::runtime_error when the program or mesh does not fit the shader.
    virtual void InitProgram(ShaderProgram &program, const ProgramContext &context) const = 0;
    // Camera and light constants; the renderer uploads the view-projection rows itself.
    virtual void SetFrame(ShaderProgram &program, const FrameState &frame) const = 0;
    // Textures and constants of one draw call with `material`, whose shader is this one.
    virtual void SetDraw(ShaderProgram &program, Material &material, const FrameState &frame,
                         const EyeState &eye) const = 0;
};

// One game's shader DLL.
class ShaderSet
{
public:
    virtual ~ShaderSet() = default;
    // The material for a parsed VMT; throws std::runtime_error for shaders the set does not port.
    virtual std::unique_ptr<Material> CreateMaterial(MaterialParams &params) const = 0;
    // Initialises programs not tied to a material (the probe's supplied GLSL without model materials).
    virtual const Shader &FallbackShader() const = 0;
};

std::unique_ptr<ShaderSet> CreateSfmShaderSet();
