#include <array>
#include <algorithm>
#include <cstddef>
#include <exception>
#include <bitset>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_2_1>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLShaderProgram>
#include <QQuaternion>
#include <QOpenGLTexture>
#include <QOpenGLWindow>
#include <QSurfaceFormat>
#include <QVector4D>
#include <QWheelEvent>

#include "viewport.h"
#include "source_texture.h"
#include "togl/vcs.h"

namespace
{
constexpr float defaultYaw = -0.915f;
constexpr float defaultPitch = 0.454f;

constexpr Vertex quad[] = {
    {{-0.75f, -0.75f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{ 0.75f, -0.75f, 0.0f, 1.0f}, {1.0f, 0.0f}},
    {{-0.75f,  0.75f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{ 0.75f,  0.75f, 0.0f, 1.0f}, {1.0f, 1.0f}},
};

struct ModelProgram
{
    QOpenGLShaderProgram gl;
    ShaderCombo combo;
    bool missing = false;
    bool phong = false;
    int position = -1, uv = -1, normal = -1, tangent = -1, color = -1;
};

class ShaderProbe final : public QOpenGLWindow, protected QOpenGLFunctions
{
public:
    ShaderProbe(QString vertexPath, QString fragmentPath, QString modelPath, MdlMesh mesh, bool check, bool checker,
                ViewportSettings *settings, QString shaderDirectory = {})
        : QOpenGLWindow(PartialUpdateBlit), m_vertexPath(std::move(vertexPath)),
          m_fragmentPath(std::move(fragmentPath)), m_modelPath(std::move(modelPath)),
          m_shaderDirectory(std::move(shaderDirectory)), m_mesh(std::move(mesh)), m_check(check), m_checker(checker), m_settings(settings)
    {
        setTitle(m_modelPath.isEmpty() ? QStringLiteral("SFM shader probe") :
            QStringLiteral("SFM model preview — ") + QFileInfo(m_modelPath).fileName());
        resize(640, 480);
    }

    ~ShaderProbe() override
    {
        if (context())
        {
            makeCurrent();
            m_materialTextures.clear();
            m_texture.destroy();
            doneCurrent();
        }
    }

protected:
    void reportFailure(const QString &error)
    {
        qCritical().noquote() << error;
        if (m_settings)
            m_settings->reportError(error);
        else
            QCoreApplication::exit(1);
    }

    void markMissing(std::unique_ptr<SourceMaterial> &material, const QString &name, const char *error)
    {
        qWarning().noquote() << "Using pink/black checker for" << name << ':' << error;
        material = std::make_unique<SourceMaterial>();
        material->shader = MaterialShader::Missing;
        material->combo = {};
    }

    void initializeGL() override
    {
        initializeOpenGLFunctions();
        if (!m_vertices.create() || !m_vertices.bind())
        {
            reportFailure(QStringLiteral("Could not allocate SFM geometry"));
            return;
        }
        if (m_mesh.indices.empty())
            m_vertices.allocate(quad, sizeof(quad));
        else
        {
            // Flexed models keep the unflexed vertices to rebuild the buffer whenever controller values change.
            const bool flexed = !m_mesh.flex.flexes.empty();
            const auto vertices = flexed ? m_mesh.vertices : std::exchange(m_mesh.vertices, {});
            if (flexed)
                m_vertices.setUsagePattern(QOpenGLBuffer::DynamicDraw);
            const auto indices = std::exchange(m_mesh.indices, {});
            m_vertices.allocate(vertices.data(), int(vertices.size() * sizeof(Vertex)));
            if (!m_indices.create() || !m_indices.bind())
            {
                reportFailure(QStringLiteral("Could not bind MDL triangle indices"));
                return;
            }
            m_indices.allocate(indices.data(), int(indices.size() * sizeof(uint32_t)));
            m_indices.release();
            m_indexCount = GLsizei(indices.size());
            qInfo() << "Loaded MDL:" << vertices.size() << "vertices," << m_indexCount / 3 << "triangles";
        }
        m_vertices.release();

        if (m_indexCount && !m_checker)
        {
            try
            {
                m_materialTextures.resize(m_mesh.textures.size());
                for (const auto &batch : m_mesh.batches)
                {
                    auto &texture = m_materialTextures[batch.texture];
                    if (texture)
                        continue;
                    try
                    {
                        texture = std::make_unique<SourceMaterial>();
                        const auto path = LoadSourceMaterial(*texture, m_modelPath, m_mesh.textures[batch.texture],
                                                             m_mesh.materialPaths);
                        if (glGetError() != GL_NO_ERROR)
                            throw std::runtime_error((path + QStringLiteral(": OpenGL texture upload failed")).toStdString());
                        qInfo().noquote() << "Loaded base texture:" << path << texture->base.width() << 'x' << texture->base.height();
                        if (texture->shader == MaterialShader::Phong)
                            qInfo() << "Phong exponent override:" << texture->exponent << "boost:" << texture->fresnelBoost.w();
                        if (!texture->ignored.isEmpty())
                            qWarning().noquote() << "Not applied in" << m_mesh.textures[batch.texture] << ':'
                                                 << texture->ignored.join(QStringLiteral(", "));
                    }
                    catch (const std::runtime_error &error)
                    {
                        if (m_shaderDirectory.isEmpty())
                            throw;
                        markMissing(texture, m_mesh.textures[batch.texture], error.what());
                    }
                }
            }
            catch (const std::exception &error)
            {
                reportFailure(QString::fromUtf8(error.what()));
                return;
            }
        }
        else
        {
            QImage image(m_indexCount ? 64 : 2, m_indexCount ? 64 : 1, QImage::Format_RGBA8888);
            if (m_indexCount)
            {
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                        image.setPixelColor(x, y, ((x / 8) + (y / 8)) % 2 ? QColor(225, 170, 85) : QColor(94, 126, 154));
            }
            else
            {
                image.setPixelColor(0, 0, Qt::red);
                image.setPixelColor(1, 0, Qt::green);
            }
            m_texture.setData(image, QOpenGLTexture::DontGenerateMipMaps);
            m_texture.setMinMagFilters(QOpenGLTexture::Nearest, QOpenGLTexture::Nearest);
            m_texture.setWrapMode(m_indexCount ? QOpenGLTexture::Repeat : QOpenGLTexture::ClampToEdge);
        }

        try
        {
            m_materialProgram.assign(m_materialTextures.size(), -1);
            if (m_shaderDirectory.isEmpty())
            {
                // Standalone probe: one program from the given GLSL files for every material.
                m_programs.push_back(std::make_unique<ModelProgram>());
                initProgram(*m_programs.back(), nullptr, false);
                for (size_t i = 0; i < m_materialTextures.size(); ++i)
                    if (const auto &material = m_materialTextures[i])
                    {
                        if ((material->shader == MaterialShader::Phong) != m_programs.back()->phong)
                            throw std::runtime_error("Material $phong does not match the supplied shader pair");
                        m_materialProgram[i] = 0;
                    }
            }
            else
            {
                // One program per distinct combo; a combo that fails to load turns its materials into the checker.
                std::vector<std::pair<ShaderCombo, std::string>> failed;
                for (size_t i = 0; i < m_materialTextures.size(); ++i)
                {
                    auto &material = m_materialTextures[i];
                    if (!material || material->shader == MaterialShader::Missing)
                        continue;
                    const auto existing = std::ranges::find_if(m_programs, [&](const auto &p) { return p->combo == material->combo; });
                    if (existing != m_programs.end())
                    {
                        m_materialProgram[i] = int(existing - m_programs.begin());
                        continue;
                    }
                    const auto error = std::ranges::find(failed, material->combo, &std::pair<ShaderCombo, std::string>::first);
                    if (error == failed.end())
                        try
                        {
                            auto program = std::make_unique<ModelProgram>();
                            initProgram(*program, &material->combo, false);
                            m_materialProgram[i] = int(m_programs.size());
                            m_programs.push_back(std::move(program));
                            continue;
                        }
                        catch (const std::runtime_error &e)
                        {
                            failed.emplace_back(material->combo, e.what());
                        }
                    markMissing(material, m_mesh.textures[i], (error == failed.end() ? failed.back() : *error).second.c_str());
                }
                for (size_t i = 0; i < m_materialTextures.size(); ++i)
                    if (m_materialTextures[i] && m_materialTextures[i]->shader == MaterialShader::Missing)
                    {
                        if (m_checkerProgram < 0)
                        {
                            m_checkerProgram = int(m_programs.size());
                            m_programs.push_back(std::make_unique<ModelProgram>());
                            initProgram(*m_programs.back(), nullptr, true);
                        }
                        m_materialProgram[i] = m_checkerProgram;
                    }
            }
            m_ready = true;
        }
        catch (const std::exception &error)
        {
            reportFailure(QString::fromUtf8(error.what()));
        }
    }

    void initProgram(ModelProgram &state, const ShaderCombo *combo, bool missing)
    {
        auto &program = state.gl;
        state.missing = missing;
        if (combo)
            state.combo = *combo;
        if (missing)
        {
            // Independent of the VCS archive: even a failed shader pair can show the affected mesh.
            program.addShaderFromSourceCode(QOpenGLShader::Vertex,
                "#version 120\n"
                "attribute vec4 v0; attribute vec2 v3; varying vec2 uv; uniform vec4 vc[12];\n"
                "void main() { float w = dot(vc[11], v0); uv = v3;\n"
                "gl_Position = vec4(dot(vc[8], v0), -dot(vc[9], v0), 2.0 * dot(vc[10], v0) - w, w); }\n");
            program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                "#version 120\n"
                "varying vec2 uv;\n"
                "void main() { vec2 cell = floor(uv * 2.0); float pink = mod(cell.x + cell.y, 2.0);\n"
                "gl_FragColor = vec4(pink, 0.0, pink, 1.0); }\n");
        }
        else if (!combo)
        {
            program.addShaderFromSourceFile(QOpenGLShader::Vertex, m_vertexPath);
            program.addShaderFromSourceFile(QOpenGLShader::Fragment, m_fragmentPath);
        }
        else
        {
            const auto add = [&](QOpenGLShader::ShaderType stage, const std::string &name, uint32_t s, uint32_t d) {
                const auto path = QDir(m_shaderDirectory).filePath(QString::fromStdString(name + ".vcs"));
                const auto text = togl::LoadVcsShader(std::filesystem::path(path.toStdU16String()), s, d);
                if (!program.addShaderFromSourceCode(stage, text.c_str()))
                    throw std::runtime_error(program.log().toStdString());
                qInfo().noquote() << "Loaded VCS:" << QString::fromStdString(name) << "static" << s << "dynamic" << d;
            };
            add(QOpenGLShader::Vertex, combo->vs, combo->vsStatic, combo->vsDynamic);
            add(QOpenGLShader::Fragment, combo->ps, combo->psStatic, combo->psDynamic);
        }
        if (program.shaders().size() != 2 || !program.link() || !program.bind())
            throw std::runtime_error(("Shader setup failed: " + program.log()).toStdString());
        const bool phong = program.uniformLocation("sampler3") >= 0 && program.uniformLocation("sampler7") >= 0;
        state.phong = phong;
        if (phong && (m_checker || !m_mesh.hasTangents || !m_indexCount))
            throw std::runtime_error("Phong needs a textured MDL with VVD tangents");
        if (!missing && m_indexCount && !m_checker)
        {
            const int srgbWrite = program.uniformLocation("flSRGBWrite");
            if (srgbWrite < 0)
                throw std::runtime_error("Regenerate the fragment GLSL with the current vcs2glsl for sRGB output");
            program.setUniformValue(srgbWrite, 1.0f);
        }
        state.position = program.attributeLocation("v0");
        state.uv = program.attributeLocation(phong ? "v2" : "v3");
        state.normal = program.attributeLocation("v1");
        state.tangent = phong ? program.attributeLocation("v4") : -1;
        state.color = phong ? -1 : program.attributeLocation("v2");
        if (state.position < 0 || state.uv < 0 || (phong && state.tangent < 0))
            throw std::runtime_error("Shader does not have the expected SFM vertex inputs");
        if (missing)
        {
            program.release();
            return;
        }
        m_phong |= phong;

        // shortcut: bindings cover the SFM generic SM3 base-texture variants only; expand with VMT feature support.
        const int lightCount = program.uniformLocation("i0");
        const bool vertexLit = lightCount >= 0;
        m_vertexLit |= vertexLit;
        std::array<QVector4D, 97> vc{};
        vc[0] = {0, 1, 2, 0.5f}; // Source math constants, including the D3D-to-GL Z fixup.
        if (vertexLit || phong)
        {
            // HLMV defaults: six equal ambient faces and one white directional light at yaw 180°.
            std::fill_n(vc.begin() + 21, 6, QVector4D(0.3f, 0.3f, 0.3f, 1));
            vc[27] = {1, 1, 1, 1}; // RGB, directional flag.
            vc[28] = {-1, 0, 0, 0}; // Direction of travel, spot flag.
            vc[29] = {0, 0, 0, 1}; // Position (unused for directional lights).
            vc[30] = {0, 1, 1, 1}; // Non-spot defaults.
            vc[31] = {1, 0, 0, 0}; // Constant attenuation.
            if (vertexLit)
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
        pc[1] = {1, 1, 1, 1}; // White modulation, opaque alpha.
        pc[30] = {1, 0, 0, 0}; // Identity output scale; fog is disabled by zero VS fog constants.
        if (phong)
        {
            std::fill_n(pc.begin() + 4, 6, QVector4D(0.3f, 0.3f, 0.3f, 1));
            pc[20] = {1, 1, 1, 0};
            // Source's pixel shader packs a directional light as a distant point.
            pc[21] = QVector4D(m_mesh.center + QVector3D(10000, 0, 0), 0);
            program.setUniformValue("sampler3", 3);
            program.setUniformValue("sampler7", 7);
        }
        program.setUniformValueArray("vc", vc.data(), vertexLit ? int(vc.size()) : 67);
        program.setUniformValueArray("vcbones", bones, 3);
        program.setUniformValue("vcscreen", QVector4D{});
        program.setUniformValueArray("pc", pc.data(), pc.size());
        program.setUniformValue("sampler0", 0);
        program.release();
    }

    void mousePressEvent(QMouseEvent *event) override { m_mouse = event->position(); }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_indexCount)
            return;
        const auto delta = event->position() - m_mouse;
        m_mouse = event->position();
        if ((event->buttons() & Qt::LeftButton) && (event->modifiers() & Qt::ControlModifier))
        {
            // HLMV Ctrl+LMB: one degree per pixel, yaw around up, pitch around the screen's horizontal axis.
            const auto right = QVector3D::crossProduct(QVector3D(0, 0, 1), cameraDirection()).normalized();
            m_lightDirection = (QQuaternion::fromAxisAndAngle(right, -float(delta.y())) *
                                QQuaternion::fromAxisAndAngle(0, 0, 1, float(delta.x()))).rotatedVector(m_lightDirection).normalized();
        }
        else if (event->buttons() & Qt::LeftButton)
        {
            m_yaw = std::remainder(m_yaw - float(delta.x()) * 0.01f, 6.2831853f);
            m_pitch = std::clamp(m_pitch + float(delta.y()) * 0.01f, -1.55f, 1.55f);
        }
        else if (event->buttons() & (Qt::MiddleButton | Qt::RightButton))
        {
            const auto offset = cameraDirection();
            const auto right = QVector3D::crossProduct(QVector3D(0, 0, 1), offset).normalized();
            const auto up = QVector3D::crossProduct(offset, right);
            const float scale = 2.2f * m_mesh.radius * m_zoom / std::max(1, std::min(width(), height()));
            m_pan += (right * -float(delta.x()) + up * float(delta.y())) * scale;
        }
        else
            return;
        updateCamera();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (!m_indexCount)
            return;
        // Uncapped; only reject values the projection cannot represent.
        if (const float zoom = m_zoom * std::exp(-event->angleDelta().y() / 960.0f); std::isnormal(zoom) && std::isnormal(m_mesh.radius * zoom))
            m_zoom = zoom;
        updateCamera();
    }

    void resizeGL(int, int) override { m_cameraDirty = true; }

    QVector3D cameraDirection() const
    {
        const auto horizontal = std::cos(m_pitch);
        return {std::cos(m_yaw) * horizontal, std::sin(m_yaw) * horizontal, std::sin(m_pitch)};
    }

    void updateCamera()
    {
        m_cameraDirty = true;
        update();
    }

    void uploadCamera()
    {
        if (!m_cameraDirty || !m_indexCount || width() <= 0 || height() <= 0)
            return;
        m_cameraDirty = false;
        const auto radius = m_mesh.radius;
        const auto target = m_mesh.center + m_pan;
        constexpr float orbitDistanceInRadii = 3.0f;
        const auto eye = target + cameraDirection() * (orbitDistanceInRadii * radius);
        const float aspect = float(width()) / float(height());
        // Scale the near plane so zoom and pan retain their scale at the orbit target.
        const float halfNear = radius * m_zoom * (1.1f / orbitDistanceInRadii);
        const float halfWidth = halfNear * std::max(1.0f, aspect);
        const float halfHeight = halfNear * std::max(1.0f, 1.0f / aspect);
        QMatrix4x4 view, projection;
        view.lookAt(eye, target, QVector3D(0, 0, 1));
        projection.frustum(-halfWidth, halfWidth, -halfHeight, halfHeight, radius, 5 * radius);
        m_mvp = projection * view;
        const QVector4D rows[] = {m_mvp.row(0), -m_mvp.row(1), (m_mvp.row(2) + m_mvp.row(3)) * 0.5f, m_mvp.row(3)};
        for (const auto &slot : m_programs)
        {
            auto &program = slot->gl;
            program.bind();
            program.setUniformValueArray("vc[8]", rows, 4);
            if (!slot->missing)
            {
                program.setUniformValue("vc[2]", QVector4D(eye, 0));
                program.setUniformValue("vc[28]", QVector4D(m_lightDirection, 0));
                if (slot->phong)
                {
                    program.setUniformValue("pc[11]", QVector4D(eye, 0));
                    // Source's pixel shader packs a directional light as a distant point.
                    program.setUniformValue("pc[21]", QVector4D(m_mesh.center - m_lightDirection * 10000, 0));
                }
            }
            program.release();
        }
    }

    void drawScene(bool specular)
    {
        const auto background = m_settings ? m_settings->background : QColor(Qt::black);
        glClearColor(background.redF(), background.greenF(), background.blueF(), 1);
        if (m_indexCount)
            glEnable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        auto *gl21 = m_settings && m_settings->wireframe
            ? QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(context()) : nullptr;
        if (gl21)
            gl21->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        uploadCamera();
        // Opaque and alpha-tested meshes first, then blended ones without depth writes, as Source sorts them.
        for (const bool blended : {false, true})
            for (size_t i = 0; i < m_programs.size(); ++i)
                drawPass(*m_programs[i], int(i), specular, blended);
        if (gl21)
            gl21->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if (m_indexCount && m_settings && ((m_settings->wireframeOverlay && !m_settings->wireframe) || m_settings->showOriginAxis))
            if (auto *debugGl = gl21 ? gl21 : QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(context()))
                drawOverlays(*debugGl);
        if (m_indexCount)
            glDisable(GL_DEPTH_TEST);
    }

    void drawOverlays(QOpenGLFunctions_2_1 &gl)
    {
        gl.glMatrixMode(GL_PROJECTION);
        gl.glPushMatrix();
        gl.glLoadMatrixf(m_mvp.constData());
        gl.glMatrixMode(GL_MODELVIEW);
        gl.glPushMatrix();
        gl.glLoadIdentity();
        glDepthMask(GL_FALSE);

        if (m_settings->wireframeOverlay && !m_settings->wireframe)
        {
            m_vertices.bind();
            m_indices.bind();
            gl.glEnableClientState(GL_VERTEX_ARRAY);
            gl.glVertexPointer(4, GL_FLOAT, sizeof(Vertex), nullptr);
            gl.glColor3f(1, 1, 1);
            // Pull coplanar lines forward without showing edges behind the model.
            gl.glPolygonOffset(-1, -1);
            gl.glEnable(GL_POLYGON_OFFSET_LINE);
            gl.glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
            gl.glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            gl.glDisable(GL_POLYGON_OFFSET_LINE);
            gl.glDisableClientState(GL_VERTEX_ARRAY);
            m_indices.release();
            m_vertices.release();
        }

        if (m_settings->showOriginAxis)
        {
            const float length = m_settings->originAxisLength;
            gl.glBegin(GL_LINES);
            gl.glColor3f(1, 0, 0);
            gl.glVertex3f(0, 0, 0);
            gl.glVertex3f(length, 0, 0);
            gl.glColor3f(0, 1, 0);
            gl.glVertex3f(0, 0, 0);
            gl.glVertex3f(0, length, 0);
            gl.glColor3f(0, 0, 1);
            gl.glVertex3f(0, 0, 0);
            gl.glVertex3f(0, 0, length);
            gl.glEnd();
        }

        gl.glColor3f(1, 1, 1);
        glDepthMask(GL_TRUE);
        gl.glPopMatrix();
        gl.glMatrixMode(GL_PROJECTION);
        gl.glPopMatrix();
        gl.glMatrixMode(GL_MODELVIEW);
    }

    static bool IsBlended(MaterialBlend blend)
    {
        return blend == MaterialBlend::Translucent || blend == MaterialBlend::Additive || blend == MaterialBlend::TranslucentAdditive;
    }

    void applyBlend(const SourceMaterial &material)
    {
        auto *gl21 = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(context());
        glDepthMask(IsBlended(material.blend) ? GL_FALSE : GL_TRUE);
        if (IsBlended(material.blend))
        {
            glEnable(GL_BLEND);
            switch (material.blend)
            {
            case MaterialBlend::Translucent: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
            case MaterialBlend::Additive: glBlendFunc(GL_ONE, GL_ONE); break;
            default: glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
            }
        }
        else
            glDisable(GL_BLEND);
        // D3D9 alpha test is fixed-function state; the GL compatibility profile still applies it after the shader.
        if (gl21 && material.blend == MaterialBlend::AlphaTest)
        {
            gl21->glEnable(GL_ALPHA_TEST);
            gl21->glAlphaFunc(GL_GEQUAL, material.alphaTestReference);
        }
        else if (gl21)
            gl21->glDisable(GL_ALPHA_TEST);
    }

    void drawPass(ModelProgram &state, int programIndex, bool specular, bool blended)
    {
        if (!m_materialTextures.empty() &&
            std::ranges::none_of(m_mesh.batches, [&](const MeshBatch &batch) {
                const auto &material = m_materialTextures[batch.texture];
                return m_materialProgram[batch.texture] == programIndex && IsBlended(material->blend) == blended;
            }))
            return;
        if (m_materialTextures.empty() && blended)
            return;
        auto &program = state.gl;
        program.bind();
        if (m_settings && !state.missing)
        {
            // HLMV's Options → Light/Ambient Color: one directional light plus a uniform ambient cube.
            const QVector4D light(m_settings->light.redF(), m_settings->light.greenF(), m_settings->light.blueF(), 1);
            const QVector4D ambient(m_settings->ambient.redF(), m_settings->ambient.greenF(), m_settings->ambient.blueF(), 1);
            const std::array<QVector4D, 6> cube{ambient, ambient, ambient, ambient, ambient, ambient};
            if (program.uniformLocation("i0") >= 0)
            {
                program.setUniformValueArray("vc[21]", cube.data(), 6);
                program.setUniformValue("vc[27]", light);
            }
            if (state.phong)
            {
                program.setUniformValueArray("pc[4]", cube.data(), 6);
                program.setUniformValue("pc[20]", QVector4D(light.toVector3D(), 0));
            }
        }
        m_vertices.bind();
        if (m_materialTextures.empty())
            m_texture.bind(0);
        const auto attribute = [&](int location, size_t offset, int count) {
            program.enableAttributeArray(location);
            program.setAttributeBuffer(location, GL_FLOAT, int(offset), count, sizeof(Vertex));
        };
        attribute(state.position, offsetof(Vertex, position), 4);
        attribute(state.uv, offsetof(Vertex, uv), 2);
        if (state.normal >= 0)
        {
            if (m_indexCount)
                attribute(state.normal, offsetof(Vertex, normal), 3);
            else
                program.setAttributeValue(state.normal, QVector4D{0, 0, 1, 0});
        }
        if (state.tangent >= 0)
            attribute(state.tangent, offsetof(Vertex, tangent), 4);
        if (state.color >= 0)
            program.setAttributeValue(state.color, QVector4D{1, 1, 1, 1});
        if (m_indexCount)
        {
            m_indices.bind();
            if (m_materialTextures.empty())
                glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
            else
                for (const auto &batch : m_mesh.batches)
                {
                    auto &material = *m_materialTextures[batch.texture];
                    if (m_materialProgram[batch.texture] != programIndex || IsBlended(material.blend) != blended)
                        continue;
                    applyBlend(material);
                    if (!state.missing)
                    {
                        material.base.bind(0);
                        program.setUniformValue("pc[1]", material.modulation);
                    }
                    if (state.phong)
                    {
                        material.normal.bind(3);
                        material.exponentMap.bind(7);
                        auto fresnelBoost = material.fresnelBoost;
                        if (!specular)
                            fresnelBoost.setW(0);
                        program.setUniformValue("pc[10]", QVector4D(0, material.luminanceMask, material.exponent, 0));
                        program.setUniformValue("pc[19]", fresnelBoost);
                        program.setUniformValue("pc[26]", material.tint);
                        program.setUniformValue("pc[27]", QVector4D(material.alphaMask, 0, 1, 0));
                    }
                    glDrawElements(GL_TRIANGLES, GLsizei(batch.count), GL_UNSIGNED_INT,
                                   reinterpret_cast<const void *>(batch.first * sizeof(uint32_t)));
                }
            if (state.phong)
                glActiveTexture(GL_TEXTURE0);
            m_indices.release();
            if (!m_materialTextures.empty())
                applyBlend(SourceMaterial{}); // restore opaque state for the next pass and readback
            if (state.normal >= 0)
                program.disableAttributeArray(state.normal);
        }
        else
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (state.tangent >= 0)
            program.disableAttributeArray(state.tangent);
        program.disableAttributeArray(state.uv);
        program.disableAttributeArray(state.position);
        if (m_materialTextures.empty())
            m_texture.release();
        m_vertices.release();
        program.release();
    }

    // HLMV stores controller values normalized to 0..1 (StudioModel::m_flexweight); RunFlexRules wants them in
    // controller units, as StudioModel::RunFlexRules remaps.
    void applyFlexes()
    {
        const auto &flex = m_mesh.flex;
        if (!m_settings || flex.flexes.empty() || m_flexRevision == m_settings->flexRevision ||
            m_settings->flexRaw.size() != flex.controllers.size())
            return;
        m_flexRevision = m_settings->flexRevision;
        std::vector<float> controllers(flex.controllers.size());
        for (size_t i = 0; i < controllers.size(); ++i)
            controllers[i] = m_settings->flexRaw[i] * (flex.controllers[i].max - flex.controllers[i].min) + flex.controllers[i].min;
        ApplyFlexes(flex, RunFlexRules(flex, controllers), m_mesh.vertices, m_mesh.skinLinear, m_flexedVertices);
        m_vertices.bind();
        m_vertices.write(0, m_flexedVertices.data(), int(m_flexedVertices.size() * sizeof(Vertex)));
        m_vertices.release();
    }

    void paintGL() override
    {
        if (!m_ready)
            return;
        applyFlexes();
        if (m_settings && std::exchange(m_settings->centerView, false))
        {
            m_yaw = defaultYaw;
            m_pitch = defaultPitch;
            m_zoom = 1;
            m_settings->centerVerts = true;
        }
        if (m_settings && std::exchange(m_settings->centerVerts, false))
        {
            m_pan = {};
            m_cameraDirty = true;
        }
        drawScene(!m_settings || m_settings->specular);

        if (m_check)
        {
            m_check = false;
            GLint viewport[4];
            glGetIntegerv(GL_VIEWPORT, viewport);
            if (m_indexCount)
            {
                QImage rendered(viewport[2], viewport[3], QImage::Format_RGBA8888);
                if (rendered.isNull())
                {
                    qCritical() << "Could not allocate framebuffer readback";
                    QCoreApplication::exit(1);
                    return;
                }
                glReadPixels(0, 0, rendered.width(), rendered.height(), GL_RGBA, GL_UNSIGNED_BYTE, rendered.bits());
                if (!m_materialTextures.empty())
                {
                    std::bitset<256> shades;
                    int covered = 0;
                    int pinkPixels = 0;
                    const auto background = m_settings ? m_settings->background : QColor(Qt::black);
                    for (int y = 0; y < rendered.height(); ++y)
                        for (int x = 0; x < rendered.width(); ++x)
                        {
                            const auto *pixel = rendered.constScanLine(y) + 4 * x;
                            pinkPixels += pixel[0] == 255 && !pixel[1] && pixel[2] == 255;
                            if (pixel[0] != background.red() || pixel[1] != background.green() || pixel[2] != background.blue())
                            {
                                ++covered;
                                shades.set(std::max({pixel[0], pixel[1], pixel[2]}));
                            }
                        }
                    const int area = rendered.width() * rendered.height();
                    bool sawSpecular = !m_phong;
                    if (m_phong)
                    {
                        drawScene(false);
                        QImage diffuse(rendered.size(), QImage::Format_RGBA8888);
                        if (!diffuse.isNull())
                        {
                            glReadPixels(0, 0, diffuse.width(), diffuse.height(), GL_RGBA, GL_UNSIGNED_BYTE, diffuse.bits());
                            sawSpecular = rendered != diffuse;
                        }
                    }
                    const bool onlyChecker = m_checkerProgram >= 0 && m_programs.size() == 1;
                    const bool pass = glGetError() == GL_NO_ERROR && covered > area / 100 &&
                        covered < 3 * area / 4 && (onlyChecker ? pinkPixels > 0 && shades.test(0) : shades.count() > 16) &&
                        sawSpecular;
                    qInfo() << (pass ? "PASS:" : "FAIL:") << "textured MDL pixels" << covered << "of" << area
                            << "with" << shades.count() << "shades";
                    if (m_checkerProgram >= 0)
                        qInfo() << "Missing material checker: " << pinkPixels << "visible magenta pixels";
                    if (m_phong)
                        qInfo() << "Phong highlight changes framebuffer:" << sawSpecular;
                    QCoreApplication::exit(pass ? 0 : 1);
                    return;
                }
                int light = 0, dark = 0;
                int ambient = 255, highlight = 0;
                for (int y = 0; y < rendered.height(); ++y)
                    for (int x = 0; x < rendered.width(); ++x)
                    {
                        const auto *pixel = rendered.constScanLine(y) + 4 * x;
                        if (m_vertexLit)
                        {
                            const bool gold = pixel[0] > pixel[1] && pixel[1] > pixel[2];
                            const bool blue = pixel[2] > pixel[1] && pixel[1] > pixel[0];
                            light += gold;
                            dark += blue;
                            if (gold)
                            {
                                ambient = std::min(ambient, int(pixel[0]));
                                highlight = std::max(highlight, int(pixel[0]));
                            }
                        }
                        else
                        {
                            light += pixel[0] == 225 && pixel[1] == 170 && pixel[2] == 85;
                            dark += pixel[0] == 94 && pixel[1] == 126 && pixel[2] == 154;
                        }
                    }
                const int area = rendered.width() * rendered.height();
                GLint depthBits = 0;
                glGetIntegerv(GL_DEPTH_BITS, &depthBits);
                const bool pass = glGetError() == GL_NO_ERROR && depthBits > 0 &&
                    light > area / 100 && dark > area / 100 && light + dark < 3 * area / 4 &&
                    (!m_vertexLit || (ambient >= 65 && ambient <= 70 && highlight > 200));
                qInfo() << (pass ? "PASS:" : "FAIL:") << "MDL checker pixels" << light << dark << "of" << area
                        << "with" << depthBits << "depth bits";
                if (m_vertexLit)
                    qInfo() << "HLMV lighting: gold-channel ambient/highlight" << ambient << highlight;
                QCoreApplication::exit(pass ? 0 : 1);
                return;
            }
            const auto pixel = [&](int x) {
                std::array<GLubyte, 4> rgba{};
                glReadPixels(x, viewport[3] / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
                return rgba;
            };
            const auto left = pixel(viewport[2] / 4);
            const auto right = pixel(3 * viewport[2] / 4);
            const auto outside = pixel(viewport[2] / 16);
            const GLenum error = glGetError();
            const bool pass = error == GL_NO_ERROR && left == std::array<GLubyte, 4>{255, 0, 0, 255} &&
                right == std::array<GLubyte, 4>{0, 255, 0, 255} && outside == std::array<GLubyte, 4>{0, 0, 0, 255};
            qInfo() << "GL renderer:" << reinterpret_cast<const char *>(glGetString(GL_RENDERER));
            if (pass)
                qInfo() << "PASS: red and green texels, black background";
            else
                qCritical("FAIL: GL error %x viewport %d %d pixels (RGBA) (%d, %d, %d, %d) (%d, %d, %d, %d) (%d, %d, %d, %d)",
                          unsigned(error), viewport[2], viewport[3],
                          left[0], left[1], left[2], left[3],
                          right[0], right[1], right[2], right[3],
                          outside[0], outside[1], outside[2], outside[3]);
            QCoreApplication::exit(pass ? 0 : 1);
        }
    }

private:
    QString m_vertexPath;
    QString m_fragmentPath;
    QString m_modelPath;
    QString m_shaderDirectory;
    MdlMesh m_mesh;
    std::vector<Vertex> m_flexedVertices;
    int m_flexRevision = -1;
    bool m_check;
    bool m_checker;
    ViewportSettings *m_settings;
    bool m_ready = false;
    bool m_vertexLit = false;
    bool m_phong = false;
    bool m_cameraDirty = true;
    float m_yaw = defaultYaw;
    float m_pitch = defaultPitch;
    float m_zoom = 1;
    QVector3D m_lightDirection{-1, 0, 0}; // HLMV default lightrot (0, 180, 0).
    QPointF m_mouse;
    QVector3D m_pan;
    QMatrix4x4 m_mvp;
    GLsizei m_indexCount = 0;
    std::vector<std::unique_ptr<ModelProgram>> m_programs;
    std::vector<int> m_materialProgram; // program index per MDL texture
    int m_checkerProgram = -1;
    QOpenGLBuffer m_vertices;
    QOpenGLBuffer m_indices{QOpenGLBuffer::IndexBuffer};
    QOpenGLTexture m_texture{QOpenGLTexture::Target2D};
    std::vector<std::unique_ptr<SourceMaterial>> m_materialTextures;
};
} // namespace

void ConfigureSfmViewport()
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(2, 1); // GLSL 1.20 uses compatibility built-ins.
    format.setAlphaBufferSize(8);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
}

std::unique_ptr<QOpenGLWindow> CreateSfmViewport(QString vertexShader, QString fragmentShader, QString modelPath,
                                           MdlMesh mesh, bool check, bool checker, ViewportSettings *settings)
{
    return std::make_unique<ShaderProbe>(std::move(vertexShader), std::move(fragmentShader), std::move(modelPath),
                                         std::move(mesh), check, checker, settings);
}

std::unique_ptr<QOpenGLWindow> CreateSfmVcsViewport(QString shaderDirectory, QString modelPath, MdlMesh mesh,
                                                   bool check, ViewportSettings &settings)
{
    return std::make_unique<ShaderProbe>(QString(), QString(), std::move(modelPath), std::move(mesh),
                                         check, false, &settings, std::move(shaderDirectory));
}
