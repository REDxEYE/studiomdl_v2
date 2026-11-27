#include <array>
#include <algorithm>
#include <optional>
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
#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_2_1>
#include <QPainter>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLShaderProgram>
#include <QQuaternion>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVector4D>
#include <QWheelEvent>

#include "viewport.h"
#include "material_system.h"
#include "studio_anim.h"
#include "togl/vcs.h"

// Render system: geometry, skinning, camera, passes, overlays and the probe self-checks. Material and shader
// knowledge stays behind material_system.h / shader_api.h; this file links one program per ShaderCombo and
// feeds the Shader hooks engine state.

namespace
{
constexpr float defaultYaw = -0.915f;
constexpr float defaultPitch = 0.454f;
constexpr float defaultFov = 65.0f; // HLMV default; frames the model at zoom 1

static float halfFovTan(float degrees) { return std::tan(qDegreesToRadians(degrees) * 0.5f); }

constexpr Vertex quad[] = {
    {{-0.75f, -0.75f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{ 0.75f, -0.75f, 0.0f, 1.0f}, {1.0f, 0.0f}},
    {{-0.75f,  0.75f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{ 0.75f,  0.75f, 0.0f, 1.0f}, {1.0f, 1.0f}},
};

// A linked program and the Shader that drives it; no shader means the missing-material checker.
struct RenderProgram
{
    ShaderProgram program;
    const Shader *shader = nullptr;
};

// A QOpenGLWidget, not a QOpenGLWindow in a window container: on Wayland with NVIDIA's EGL a desktop-GL QWindow
// never reaches the screen, while the widget's FBO is composed by the widget stack.
class ModelViewport final : public QOpenGLWidget, protected QOpenGLFunctions
{
public:
    ModelViewport(QString vertexPath, QString fragmentPath, QString modelPath, MdlMesh mesh, bool check, bool checker,
                  ViewportSettings *settings, QString shaderDirectory = {})
        : m_vertexPath(std::move(vertexPath)),
          m_fragmentPath(std::move(fragmentPath)), m_modelPath(std::move(modelPath)),
          m_shaderDirectory(std::move(shaderDirectory)), m_mesh(std::move(mesh)), m_check(check), m_checker(checker), m_settings(settings)
    {
        setWindowTitle(m_modelPath.isEmpty() ? QStringLiteral("SFM shader probe") :
            QStringLiteral("SFM model preview — ") + QFileInfo(m_modelPath).fileName());
        resize(640, 480);
    }

    ~ModelViewport() override
    {
        if (context())
        {
            makeCurrent();
            m_materials.clear();
            m_shaders.reset();
            m_texture.destroy();
            doneCurrent();
        }
    }

protected:
    // The self-checks finish in paintGL, which the widget stack may run synchronously on first show, before
    // app.exec() starts; a direct QCoreApplication::exit would then be ignored, so queue it.
    static void QuitWith(int code)
    {
        QMetaObject::invokeMethod(qApp, [code] { QCoreApplication::exit(code); }, Qt::QueuedConnection);
    }

    void reportFailure(const QString &error)
    {
        qCritical().noquote() << error;
        if (m_settings)
            m_settings->reportError(error);
        else
            QuitWith(1);
    }

    void markMissing(std::unique_ptr<Material> &material, const QString &name, const char *error)
    {
        qWarning().noquote() << "Using pink/black checker for" << name << ':' << error;
        material = std::make_unique<Material>();
    }

    void initializeGL() override
    {
        initializeOpenGLFunctions();
        glFrontFace(GL_CW); // Source culls D3D-style: counter-clockwise triangles are back faces
        if (!m_vertices.create() || !m_vertices.bind())
        {
            reportFailure(QStringLiteral("Could not allocate SFM geometry"));
            return;
        }
        if (m_mesh.indices.empty())
            m_vertices.allocate(quad, sizeof(quad));
        else
        {
            // Skinned or flexed models rebuild the buffer from the bind pose every pose change; normal overlays read it.
            m_pose = m_mesh.bones;
            const bool skinned = !m_mesh.weights.empty();
            // The engine draws sequence poses, not mstudiobone_t's default pose, so frame what is drawn.
            if (skinned && m_settings && m_settings->animation && m_settings->animation->BoneCount() == int(m_pose.size()))
                m_settings->animation->SetUpBones(m_pose);
            if (skinned)
            {
                SkinVertices(m_mesh, m_mesh.vertices, m_pose, m_skinnedVertices);
                FrameVertices(m_mesh, m_skinnedVertices);
            }
            const auto &vertices = skinned ? m_skinnedVertices : m_mesh.vertices;
            const auto &indices = m_mesh.indices; // kept to list the selected submodels' vertices for normal overlays
            if (skinned || !m_mesh.flex.flexes.empty())
                m_vertices.setUsagePattern(QOpenGLBuffer::DynamicDraw);
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
                m_materials.resize(m_mesh.textures.size());
                // The viewer loads every skin family up front so the Skin choice never reloads; the probe needs only family 0.
                const int families = m_settings && m_mesh.skinRefs ? int(m_mesh.skinTable.size()) / m_mesh.skinRefs : 1;
                std::vector<int> used;
                for (const auto &batch : m_mesh.batches)
                    for (int family = 0; family < families; ++family)
                        used.push_back(m_mesh.Texture(batch, family));
                for (const int index : used)
                {
                    auto &material = m_materials[index];
                    if (material)
                        continue;
                    try
                    {
                        material = LoadMaterial(*m_shaders, m_modelPath, m_mesh.textures[index], m_mesh.materialPaths);
                        if (glGetError() != GL_NO_ERROR)
                            throw std::runtime_error((m_mesh.textures[index] + QStringLiteral(": OpenGL texture upload failed")).toStdString());
                        qInfo().noquote() << "Loaded material:" << m_mesh.textures[index] << "base" << material->base.width() << 'x'
                                          << material->base.height();
                        if (!material->ignored.isEmpty())
                            qWarning().noquote() << "Not applied in" << m_mesh.textures[index] << ':'
                                                 << material->ignored.join(QStringLiteral(", "));
                    }
                    catch (const std::runtime_error &error)
                    {
                        if (m_shaderDirectory.isEmpty())
                            throw;
                        markMissing(material, m_mesh.textures[index], error.what());
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
            m_materialProgram.assign(m_materials.size(), -1);
            if (m_shaderDirectory.isEmpty())
            {
                // Standalone probe: one program from the given GLSL files, driven by the materials' common shader.
                const auto first = std::ranges::find_if(m_materials, [](const auto &m) { return m != nullptr; });
                const Shader *shader = first != m_materials.end() ? (*first)->shader : &m_shaders->FallbackShader();
                m_programs.push_back(std::make_unique<RenderProgram>());
                initProgram(*m_programs.back(), shader, nullptr);
                for (size_t i = 0; i < m_materials.size(); ++i)
                    if (const auto &material = m_materials[i])
                    {
                        if (material->shader != shader)
                            throw std::runtime_error("Materials need different shaders; the probe drives one supplied shader pair");
                        m_materialProgram[i] = 0;
                    }
            }
            else
            {
                // One program per distinct combo; a combo that fails to load turns its materials into the checker.
                std::vector<std::pair<ShaderCombo, std::string>> failed;
                for (size_t i = 0; i < m_materials.size(); ++i)
                {
                    auto &material = m_materials[i];
                    if (!material || !material->shader)
                        continue;
                    const auto existing = std::ranges::find_if(m_programs, [&](const auto &p) { return p->program.combo == material->combo; });
                    if (existing != m_programs.end())
                    {
                        m_materialProgram[i] = int(existing - m_programs.begin());
                        continue;
                    }
                    auto error = std::ranges::find(failed, material->combo, &std::pair<ShaderCombo, std::string>::first);
                    if (error == failed.end())
                        try
                        {
                            auto program = std::make_unique<RenderProgram>();
                            initProgram(*program, material->shader, &material->combo);
                            m_materialProgram[i] = int(m_programs.size());
                            m_programs.push_back(std::move(program));
                            continue;
                        }
                        catch (const std::runtime_error &e)
                        {
                            // emplace_back invalidates `error`; point it at the new entry
                            failed.emplace_back(material->combo, e.what());
                            error = failed.end() - 1;
                        }
                    markMissing(material, m_mesh.textures[i], error->second.c_str());
                }
                for (size_t i = 0; i < m_materials.size(); ++i)
                    if (m_materials[i] && !m_materials[i]->shader)
                    {
                        if (m_checkerProgram < 0)
                        {
                            m_checkerProgram = int(m_programs.size());
                            m_programs.push_back(std::make_unique<RenderProgram>());
                            initProgram(*m_programs.back(), nullptr, nullptr);
                        }
                        m_materialProgram[i] = m_checkerProgram;
                    }
            }
            for (const auto &material : m_materials)
                if (material && material->HasTimeVaryingProxies())
                    m_animatedMaterials.push_back(material.get());
            if (!m_check && !m_animatedMaterials.empty())
            {
                m_materialClock.start();
                if (!m_settings || !m_settings->animation)
                {
                    auto *timer = new QTimer(this);
                    connect(timer, &QTimer::timeout, this, qOverload<>(&ModelViewport::update));
                    timer->start(16);
                }
            }
            m_ready = true;
        }
        catch (const std::exception &error)
        {
            reportFailure(QString::fromUtf8(error.what()));
        }
    }

    // `shader` null: the missing-material checker; `combo` null: the probe's supplied GLSL files.
    void initProgram(RenderProgram &state, const Shader *shader, const ShaderCombo *combo)
    {
        auto &program = state.program.gl;
        state.shader = shader;
        if (combo)
            state.program.combo = *combo;
        if (!shader)
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
        if (!shader)
        {
            state.program.position = program.attributeLocation("v0");
            state.program.uv = program.attributeLocation("v3");
            program.release();
            return;
        }
        const bool textured = m_indexCount && !m_checker;
        if (textured)
        {
            const int srgbWrite = program.uniformLocation("flSRGBWrite");
            if (srgbWrite < 0)
                throw std::runtime_error("Regenerate the fragment GLSL with the current vcs2glsl for sRGB output");
            program.setUniformValue(srgbWrite, 1.0f);
        }
        shader->InitProgram(state.program, {textured, m_mesh.hasTangents, m_mesh.center});
        m_phong |= state.program.specular;
        m_vertexLit |= state.program.lit;
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
            // World units per pixel at the orbit target, so the model follows the cursor.
            const float scale = 2.2f * m_mesh.radius * m_zoom * halfFovTan(m_fov) / halfFovTan(defaultFov) /
                                std::max(1, std::min(width(), height()));
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
        // HLMV's PerspectiveX: the FOV spans the viewport width.
        const float tanX = halfFovTan(m_fov);
        const float tanY = tanX * float(height()) / float(width());
        // Like HLMV, the distance ignores the FOV: a wider lens shows the model smaller.
        const float distance = 1.1f * radius * m_zoom / (halfFovTan(defaultFov) * std::min(1.0f, float(height()) / float(width())));
        const auto eye = target + cameraDirection() * distance;
        const float reach = m_pan.length() + 2 * radius;
        const float zNear = std::max(distance - reach, distance * 0.01f);
        QMatrix4x4 view, projection;
        view.lookAt(eye, target, QVector3D(0, 0, 1));
        projection.frustum(-zNear * tanX, zNear * tanX, -zNear * tanY, zNear * tanY, zNear, distance + reach);
        m_mvp = projection * view;
        m_view = view;
        const QVector4D rows[] = {m_mvp.row(0), -m_mvp.row(1), (m_mvp.row(2) + m_mvp.row(3)) * 0.5f, m_mvp.row(3)};
        for (const auto &slot : m_programs)
        {
            auto &program = slot->program.gl;
            program.bind();
            program.setUniformValueArray("vc[8]", rows, 4); // shaderapi's model-view-projection rows
            program.release();
        }
        m_eye = eye;
    }

    void drawScene(bool specular)
    {
        m_drawCalls = 0;
        const auto background = m_settings ? m_settings->background : QColor(Qt::black);
        glClearColor(background.redF(), background.greenF(), background.blueF(), 1);
        if (m_indexCount)
            glEnable(GL_DEPTH_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // Source shaders write depth or material alpha to dest alpha (eye shaders: c29.w * depth); the viewer
        // shows the window opaque, and premultiplied readback (screenshots) would turn those pixels black.
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        auto *gl21 = m_settings && m_settings->wireframe
            ? QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(context()) : nullptr;
        if (gl21)
            gl21->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        uploadCamera();
        updateEyes();
        // studiorender's R_StudioRenderModel: opaque meshes first (grouped by program here), then the translucent ones
        // in mesh order, switching programs between runs.
        const auto translucent = [&](size_t b) {
            const int texture = textureOf(m_mesh.batches[b]);
            return visible(m_mesh.batches[b]) && m_materials[texture]->translucent();
        };
        const auto programOf = [&](size_t b) { return m_materialProgram[textureOf(m_mesh.batches[b])]; };
        for (size_t i = 0; i < m_programs.size(); ++i)
            drawPass(*m_programs[i], int(i), specular, [&](size_t b) {
                return visible(m_mesh.batches[b]) && programOf(b) == int(i) && !m_materials[textureOf(m_mesh.batches[b])]->translucent();
            });
        for (size_t first = 0; !m_materials.empty() && first < m_mesh.batches.size();)
        {
            if (!translucent(first) || programOf(first) < 0)
            {
                ++first;
                continue;
            }
            const int program = programOf(first);
            size_t last = first + 1;
            while (last < m_mesh.batches.size() && (!translucent(last) || programOf(last) == program))
                ++last;
            drawPass(*m_programs[program], program, specular,
                     [&](size_t b) { return b >= first && b < last && translucent(b) && programOf(b) == program; });
            first = last;
        }
        if (gl21)
            gl21->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if (m_indexCount && m_settings && anyOverlay())
            if (auto *debugGl = gl21 ? gl21 : QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(context()))
                drawOverlays(*debugGl);
        if (m_indexCount)
            glDisable(GL_DEPTH_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    bool anyOverlay() const
    {
        const auto &s = *m_settings;
        return (s.wireframeOverlay && !s.wireframe) || s.showOriginAxis || s.showBones || s.showAttachments ||
               s.showHitboxes || s.showNormals || (s.showTangents && m_mesh.hasTangents) || s.highlightBone >= 0 ||
               s.highlightHitbox >= 0;
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
            drawVisibleBatches();
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

        const auto &s = *m_settings;
        if (s.showNormals || (s.showTangents && m_mesh.hasTangents))
            updateDrawnVertices();
        const auto vertex = [&](const QVector3D &p) { gl.glVertex3f(p.x(), p.y(), p.z()); };
        const auto line = [&](const QVector3D &from, const QVector3D &to) { vertex(from); vertex(to); };
        // HLMV's drawTransform: the matrix axes in red, green and blue.
        const auto axes = [&](const QMatrix4x4 &m, float length) {
            const auto origin = m.column(3).toVector3D();
            for (int axis = 0; axis < 3; ++axis)
            {
                gl.glColor3f(axis == 0, axis == 1, axis == 2);
                line(origin, origin + m.column(axis).toVector3D().normalized() * length);
            }
        };

        if (s.showNormals || (s.showTangents && m_mesh.hasTangents))
        {
            // shortcut: immediate-mode lines every frame; cache a line buffer if dense models get slow.
            const auto &vertices = currentVertices();
            const float length = s.originAxisLength * 0.1f;
            gl.glBegin(GL_LINES);
            for (const auto index : m_drawnVertices)
            {
                const auto &v = vertices[index];
                const QVector3D position(v.position[0], v.position[1], v.position[2]);
                const QVector3D normal(v.normal[0], v.normal[1], v.normal[2]);
                if (s.showNormals)
                {
                    gl.glColor3f(0, 0, 1);
                    line(position, position + normal * length);
                }
                if (s.showTangents && m_mesh.hasTangents)
                {
                    const QVector3D tangent(v.tangent[0], v.tangent[1], v.tangent[2]);
                    gl.glColor3f(1, 0, 0);
                    line(position, position + tangent * length);
                    gl.glColor3f(0, 1, 0);
                    line(position, position + QVector3D::crossProduct(normal, tangent) * (v.tangent[3] * length));
                }
            }
            gl.glEnd();
        }

        // Skeleton overlays show through the mesh, like HLMV's debug materials.
        glDisable(GL_DEPTH_TEST);
        gl.glBegin(GL_LINES);
        const auto &sets = m_settings->hitboxSets;
        const int highlight = s.highlightHitbox;
        if ((s.showHitboxes || highlight >= 0) && s.hitboxSet >= 0 && size_t(s.hitboxSet) < sets.size())
        {
            static constexpr float hullColor[8][3] = {{1, 1, 1}, {1, .5f, .5f}, {.5f, 1, .5f}, {1, 1, .5f},
                                                      {.5f, .5f, 1}, {1, .5f, 1}, {.5f, 1, 1}, {1, 1, 1}};
            const auto &boxes = sets[s.hitboxSet].boxes;
            for (size_t i = 0; i < boxes.size(); ++i)
            {
                const auto &box = boxes[i];
                if (box.bone < 0 || size_t(box.bone) >= m_pose.size())
                    continue;
                // HLMV dims every box but the highlighted one.
                const float dim = highlight >= 0 && size_t(highlight) != i ? 0.6f : 1.0f;
                const auto *color = hullColor[(box.group % 8 + 8) % 8];
                gl.glColor3f(color[0] * dim, color[1] * dim, color[2] * dim);
                // Source AngleMatrix: yaw about Z, then pitch about Y, then roll about X.
                QMatrix4x4 transform = m_pose[box.bone];
                transform.rotate(box.angles.y(), 0, 0, 1);
                transform.rotate(box.angles.x(), 0, 1, 0);
                transform.rotate(box.angles.z(), 1, 0, 0);
                // shortcut: capsules draw as their bounds grown by the radius; draw true capsules if CS:GO models need it.
                const QVector3D grow(std::max(box.radius, 0.0f), std::max(box.radius, 0.0f), std::max(box.radius, 0.0f));
                const auto min = box.min - grow, max = box.max + grow;
                const auto corner = [&](int c) {
                    return transform.map(QVector3D(c & 1 ? max.x() : min.x(), c & 2 ? max.y() : min.y(), c & 4 ? max.z() : min.z()));
                };
                for (int c = 0; c < 8; ++c)
                    for (int bit = 1; bit < 8; bit <<= 1)
                        if (!(c & bit))
                            line(corner(c), corner(c | bit));
            }
        }
        // HLMV's DrawBones: a highlighted bone shows alone, with yellow links to its children.
        const int highlightBone = s.highlightBone;
        if (s.showBones || highlightBone >= 0)
            for (size_t i = 0; i < m_pose.size(); ++i)
            {
                const auto &bone = m_pose[i];
                if (const int parent = m_mesh.boneParents[i]; parent >= 0 && (highlightBone < 0 || parent == highlightBone))
                {
                    axes(bone, s.originAxisLength * 0.1f);
                    gl.glColor3f(highlightBone >= 0, 1, highlightBone < 0);
                    line(m_pose[parent].column(3).toVector3D(), bone.column(3).toVector3D());
                }
                if (highlightBone < 0 || size_t(highlightBone) == i)
                    axes(bone, s.originAxisLength * 0.4f);
            }
        if (s.showAttachments)
            for (const auto &attachment : m_mesh.attachments)
                axes(m_pose[attachment.bone] * attachment.local, s.originAxisLength * 0.4f);
        gl.glEnd();
        glEnable(GL_DEPTH_TEST);

        gl.glColor3f(1, 1, 1);
        glDepthMask(GL_TRUE);
        gl.glPopMatrix();
        gl.glMatrixMode(GL_PROJECTION);
        gl.glPopMatrix();
        gl.glMatrixMode(GL_MODELVIEW);
    }

    void drawBoneNames()
    {
        const auto &s = *m_settings;
        QPainter painter(this);
        painter.setPen(Qt::white);
        for (qsizetype i = 0; i < m_mesh.info.bones.size() && size_t(i) < m_pose.size(); ++i)
        {
            if (s.highlightBone >= 0 && i != s.highlightBone)
                continue;
            const auto clip = m_mvp * QVector4D(m_pose[i].column(3).toVector3D(), 1);
            if (clip.w() <= 0)
                continue;
            const QPointF at((clip.x() / clip.w() + 1) * 0.5 * width(), (1 - clip.y() / clip.w()) * 0.5 * height());
            painter.drawText(at, QStringLiteral("~") + m_mesh.info.bones[i]); // HLMV's "silly bone icon"
        }
    }

    // The material's shadow state (CBaseShader): blending without depth writes (EnableAlphaBlending), D3D9's
    // fixed-function alpha test (the GL compatibility profile still applies it after the shader), and
    // SetInitialShadowState's $nocull, $decal and $ignorez. Null restores the defaults used by overlays.
    void applyMaterialState(const Material *material)
    {
        auto *gl21 = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(context());
        const auto blend = material ? material->effectiveBlend() : MaterialBlend::None;
        if (blend == MaterialBlend::None)
            glDisable(GL_BLEND);
        else
        {
            glEnable(GL_BLEND);
            switch (blend)
            {
            case MaterialBlend::Blend: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
            case MaterialBlend::Add: glBlendFunc(GL_ONE, GL_ONE); break;
            default: glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
            }
        }
        const bool decal = material && material->decal, ignoreZ = material && material->ignoreZ;
        glDepthMask(blend == MaterialBlend::None && !decal && !ignoreZ ? GL_TRUE : GL_FALSE);
        if (ignoreZ)
            glDisable(GL_DEPTH_TEST);
        else
            glEnable(GL_DEPTH_TEST);
        if (decal) // SHADER_POLYOFFSET_DECAL
        {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1, -1);
        }
        else
            glDisable(GL_POLYGON_OFFSET_FILL);
        if (material && !material->noCull)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);
        if (gl21 && material && material->alphaTest)
        {
            gl21->glEnable(GL_ALPHA_TEST);
            gl21->glAlphaFunc(GL_GEQUAL, material->alphaTestReference);
        }
        else if (gl21)
            gl21->glDisable(GL_ALPHA_TEST);
    }

    // HLMV's Skin choice remaps each mesh's material slot through the selected skin family.
    int textureOf(const MeshBatch &batch) const { return m_mesh.Texture(batch, m_settings ? m_settings->skin : 0); }

    // Bodygroups: each body part draws only its selected submodel (HLMV's SetBodygroup); default submodel 0.
    bool visible(const MeshBatch &batch) const
    {
        const auto *groups = m_settings ? &m_settings->bodygroups : nullptr;
        return batch.model == (groups && size_t(batch.bodyPart) < groups->size() ? (*groups)[batch.bodyPart] : 0);
    }

    // SFM HLMV's Model tab stats: draw calls, the visible submodels' vertices and indexes, and translucency.
    void reportStats()
    {
        if (!m_indexCount || !m_settings || !m_settings->reportStats)
            return;
        updateDrawnVertices();
        ViewportStats stats{m_drawCalls, int(m_drawnVertices.size())};
        for (const auto &batch : m_mesh.batches)
            if (visible(batch))
            {
                stats.indices += int(batch.count);
                if (const int texture = textureOf(batch); size_t(texture) < m_materials.size() && m_materials[texture])
                    stats.transparent |= m_materials[texture]->translucent();
            }
        if (stats != m_stats)
            m_settings->reportStats(m_stats = stats);
    }

    void drawVisibleBatches()
    {
        for (const auto &batch : m_mesh.batches)
            if (visible(batch))
            {
                glDrawElements(GL_TRIANGLES, GLsizei(batch.count), GL_UNSIGNED_INT,
                               reinterpret_cast<const void *>(batch.first * sizeof(uint32_t)));
                ++m_drawCalls;
            }
    }

    void updateDrawnVertices()
    {
        if (m_drawnFor == m_settings->bodygroups)
            return;
        m_drawnFor = m_settings->bodygroups;
        m_drawnVertices.clear();
        for (const auto &batch : m_mesh.batches)
            if (visible(batch))
                m_drawnVertices.insert(m_drawnVertices.end(), m_mesh.indices.begin() + batch.first,
                                       m_mesh.indices.begin() + batch.first + batch.count);
        std::ranges::sort(m_drawnVertices);
        m_drawnVertices.erase(std::ranges::unique(m_drawnVertices).begin(), m_drawnVertices.end());
    }

    // HLMV's StudioModel::SetViewTarget: look 32 units ahead of the "eyes" attachment, or at the camera with
    // Enable Eye Look, deflected by the eyes_updown / eyes_rightleft flex controllers and clamped to the
    // model's max eye deflection.
    QVector3D eyeViewTarget() const
    {
        const auto name = std::ranges::find_if(m_mesh.info.attachments, [](const QString &n) { return n.compare(QLatin1String("eyes"), Qt::CaseInsensitive) == 0; });
        if (name == m_mesh.info.attachments.end())
            return m_eye; // shortcut: no "eyes" attachment, so look at the camera; studiorender keeps its last target
        const auto &attachment = m_mesh.attachments[name - m_mesh.info.attachments.begin()];
        const auto attToWorld = m_pose[attachment.bone] * attachment.local;
        QVector3D local = m_settings && m_settings->eyeLook ? attToWorld.inverted().map(m_eye) : QVector3D(32, 0, 0);
        const float distance = local.length();
        local.normalize();
        const auto controller = [&](QLatin1String key) {
            const auto &controllers = m_mesh.flex.controllers;
            for (size_t i = 0; i < controllers.size(); ++i)
                if (controllers[i].name.compare(key, Qt::CaseInsensitive) == 0)
                {
                    const float raw = m_settings && i < m_settings->flexRaw.size() ? m_settings->flexRaw[i] : 0.f;
                    return raw * (controllers[i].max - controllers[i].min) + controllers[i].min;
                }
            return 0.f;
        };
        // AngleVectors(QAngle(updown, rightleft, 0)) forward with x dropped, scaled by local.x^2
        const float pitch = qDegreesToRadians(controller(QLatin1String("eyes_updown")));
        const float yaw = qDegreesToRadians(controller(QLatin1String("eyes_rightleft")));
        local += QVector3D(0, std::cos(pitch) * std::sin(yaw), -std::sin(pitch)) * (local.x() * local.x());
        local.normalize();
        const float maxDeflection = m_mesh.maxEyeDeflection;
        if (local.x() < maxDeflection)
        {
            const float d2 = local.lengthSquared() - local.x() * local.x();
            if (d2 > 0)
            {
                const float d = std::sqrt((1 - maxDeflection * maxDeflection) / d2);
                local = {maxDeflection, local.y() * d, local.z() * d};
            }
            else
                local = {1, 0, 0};
        }
        return attToWorld.map(local * distance);
    }

    // studiorender's R_StudioEyeballPosition for every eyeball (r_studioflex.cpp), as SetEyeMaterialVars hands it
    // to the eye shaders.
    void updateEyes()
    {
        m_eyeStates.resize(m_mesh.eyeballs.size());
        if (m_mesh.eyeballs.empty())
            return;
        const auto target = eyeViewTarget();
        for (size_t i = 0; i < m_mesh.eyeballs.size(); ++i)
        {
            const auto &eyeball = m_mesh.eyeballs[i];
            const auto &bone = m_pose[eyeball.bone];
            // HLMV's studiorender config: no eye shift, eyes move, no extra eye size.
            const auto org = bone.map(eyeball.org);
            auto up = bone.mapVector(eyeball.up);
            auto forward = (target - org).normalized();
            auto right = QVector3D::crossProduct(forward, up).normalized();
            forward = (forward + (eyeball.zoffset + eyeball.zoffset) * right).normalized(); // shift N degrees off the target
            right = QVector3D::crossProduct(forward, up).normalized();
            up = QVector3D::crossProduct(right, forward).normalized();
            float scale = 1.0f / eyeball.irisScale;
            if (scale > 0)
                scale = 1.0f / scale;
            const auto u = right * -scale, v = up * -scale;
            // $eyeorigin is the unshifted eyeball origin; $eyeup is mstudioeyeball_t::up as stored.
            m_eyeStates[i] = {QVector4D(org, 0), QVector4D(eyeball.up, 0), QVector4D(u, -QVector3D::dotProduct(org, u) + 0.5f),
                              QVector4D(v, -QVector3D::dotProduct(org, v) + 0.5f)};
        }
    }

    // Draws the batches `draws` selects with one program; without model materials, every visible batch.
    void drawPass(RenderProgram &state, int programIndex, bool specular, const std::function<bool(size_t)> &draws)
    {
        if (!m_materials.empty())
        {
            bool any = false;
            for (size_t b = 0; b < m_mesh.batches.size() && !any; ++b)
                any = draws(b);
            if (!any)
                return;
        }
        else if (programIndex != 0)
            return;
        auto &inputs = state.program;
        auto &program = inputs.gl;
        program.bind();
        FrameState frame;
        frame.camera = m_eye;
        frame.lightDirection = m_lightDirection;
        frame.center = m_mesh.center;
        frame.specular = specular;
        if (m_settings)
        {
            // HLMV's Options → Light/Ambient Color: one directional light plus a uniform ambient cube.
            frame.light = {float(m_settings->light.redF()), float(m_settings->light.greenF()), float(m_settings->light.blueF()), 1};
            frame.ambient = {float(m_settings->ambient.redF()), float(m_settings->ambient.greenF()), float(m_settings->ambient.blueF()), 1};
        }
        // studiorender's R_MouthSetupVertexShader, mouth 0: forward rotated by its bone and negated, flex weight clamped.
        if (!m_mesh.mouths.empty())
        {
            const auto &mouth = m_mesh.mouths.front();
            const float weight = size_t(mouth.flexdesc) < m_flexWeights.size() ? m_flexWeights[mouth.flexdesc] : 0.f;
            const auto forward = size_t(mouth.bone) < m_pose.size() ? m_pose[mouth.bone].mapVector(mouth.forward) : mouth.forward;
            frame.mouth = QVector4D(-forward, std::clamp(weight, 0.f, 1.f));
        }
        // The probe quad keeps the program's static camera and lights.
        if (state.shader && m_indexCount)
            state.shader->SetFrame(inputs, frame);
        m_vertices.bind();
        if (m_materials.empty())
            m_texture.bind(0);
        const auto attribute = [&](int location, size_t offset, int count) {
            program.enableAttributeArray(location);
            program.setAttributeBuffer(location, GL_FLOAT, int(offset), count, sizeof(Vertex));
        };
        attribute(inputs.position, offsetof(Vertex, position), 4);
        attribute(inputs.uv, offsetof(Vertex, uv), 2);
        if (inputs.normal >= 0)
        {
            if (m_indexCount)
                attribute(inputs.normal, offsetof(Vertex, normal), 3);
            else
                program.setAttributeValue(inputs.normal, QVector4D{0, 0, 1, 0});
        }
        if (inputs.tangent >= 0)
            attribute(inputs.tangent, offsetof(Vertex, tangent), 4);
        if (inputs.color >= 0)
            program.setAttributeValue(inputs.color, QVector4D{1, 1, 1, 1});
        for (const int delta : {inputs.delta, inputs.deltaNormal})
            if (delta >= 0)
                program.setAttributeValue(delta, QVector4D{});
        if (m_indexCount)
        {
            m_indices.bind();
            if (m_materials.empty())
                drawVisibleBatches();
            else
                for (size_t b = 0; b < m_mesh.batches.size(); ++b)
                {
                    if (!draws(b))
                        continue;
                    const auto &batch = m_mesh.batches[b];
                    auto &material = *m_materials[textureOf(batch)];
                    applyMaterialState(&material);
                    if (state.shader)
                    {
                        const bool eyeball = batch.eyeball >= 0 && size_t(batch.eyeball) < m_eyeStates.size();
                        state.shader->SetDraw(inputs, material, frame, eyeball ? m_eyeStates[batch.eyeball] : EyeState{});
                    }
                    glDrawElements(GL_TRIANGLES, GLsizei(batch.count), GL_UNSIGNED_INT,
                                   reinterpret_cast<const void *>(batch.first * sizeof(uint32_t)));
                    ++m_drawCalls;
                }
            glActiveTexture(GL_TEXTURE0);
            m_indices.release();
            if (!m_materials.empty())
                applyMaterialState(nullptr); // restore defaults for the next pass, overlays and readback
            if (inputs.normal >= 0)
                program.disableAttributeArray(inputs.normal);
        }
        else
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (inputs.tangent >= 0)
            program.disableAttributeArray(inputs.tangent);
        program.disableAttributeArray(inputs.uv);
        program.disableAttributeArray(inputs.position);
        if (m_materials.empty())
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
        m_flexWeights = RunFlexRules(flex, controllers);
        ApplyFlexes(flex, m_flexWeights, m_mesh.vertices, m_flexedVertices);
        m_geometryDirty = true;
    }

    // Vertices as drawn: skinned to the current pose, else flexed, else as loaded.
    const std::vector<Vertex> &currentVertices() const
    {
        if (!m_mesh.weights.empty())
            return m_skinnedVertices;
        return m_flexedVertices.empty() ? m_mesh.vertices : m_flexedVertices;
    }

    // HLMV's SetUpBones + software skinning: pose the skeleton, then rebuild the vertex buffer.
    void updateGeometry()
    {
        applyFlexes();
        if (m_settings && m_settings->animation && !m_mesh.weights.empty() &&
            m_settings->animation->BoneCount() == int(m_mesh.bones.size()))
        {
            m_settings->animation->SetUpBones(m_pose, m_view);
            m_geometryDirty = true;
        }
        if (!m_geometryDirty)
            return;
        m_geometryDirty = false;
        const auto &base = m_flexedVertices.empty() ? m_mesh.vertices : m_flexedVertices;
        if (!m_mesh.weights.empty())
            SkinVertices(m_mesh, base, m_pose, m_skinnedVertices);
        const auto &vertices = currentVertices();
        m_vertices.bind();
        m_vertices.write(0, vertices.data(), int(vertices.size() * sizeof(Vertex)));
        m_vertices.release();
    }

    void paintGL() override
    {
        if (!m_ready)
            return;
        if (m_indexCount)
            updateGeometry();
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
        if (m_settings && m_settings->fov != m_fov)
        {
            m_fov = m_settings->fov;
            m_cameraDirty = true;
        }
        if (m_materialClock.isValid())
        {
            const double seconds = double(m_materialClock.nsecsElapsed()) * 1e-9;
            for (auto *material : m_animatedMaterials)
                material->UpdateProxies(seconds);
        }
        drawScene(!m_settings || m_settings->specular);
        reportStats();
        if (m_indexCount && m_settings && m_settings->showBoneNames && (m_settings->showBones || m_settings->highlightBone >= 0))
            drawBoneNames();

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
                    QuitWith(1);
                    return;
                }
                glReadPixels(0, 0, rendered.width(), rendered.height(), GL_RGBA, GL_UNSIGNED_BYTE, rendered.bits());
                if (!m_materials.empty())
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
                    QuitWith(pass ? 0 : 1);
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
                QuitWith(pass ? 0 : 1);
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
            QuitWith(pass ? 0 : 1);
        }
    }

private:
    QString m_vertexPath;
    QString m_fragmentPath;
    QString m_modelPath;
    QString m_shaderDirectory;
    MdlMesh m_mesh;
    std::vector<Vertex> m_flexedVertices;
    std::vector<Vertex> m_skinnedVertices;
    std::vector<QMatrix4x4> m_pose; // bone-to-model for the drawn pose
    bool m_geometryDirty = false;
    std::vector<uint32_t> m_drawnVertices; // vertices of the selected submodels, for normal overlays
    std::optional<std::vector<int>> m_drawnFor; // bodygroups m_drawnVertices was built for
    int m_drawCalls = 0;                        // glDrawElements calls in the last drawScene
    ViewportStats m_stats;                      // last reported
    int m_flexRevision = -1;
    std::vector<float> m_flexWeights; // flex descriptor weights from the last RunFlexRules
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
    float m_fov = defaultFov;
    QVector3D m_lightDirection{-1, 0, 0}; // HLMV default lightrot (0, 180, 0).
    QPointF m_mouse;
    QVector3D m_pan;
    QMatrix4x4 m_mvp;
    QMatrix4x4 m_view; // model to camera; jiggle bones treat camera motion as model motion, like upstream
    QVector3D m_eye;                   // camera position in model space
    std::vector<EyeState> m_eyeStates; // per MdlMesh::eyeballs, for the current pose and camera
    GLsizei m_indexCount = 0;
    std::vector<std::unique_ptr<RenderProgram>> m_programs;
    std::vector<int> m_materialProgram; // program index per MDL texture
    int m_checkerProgram = -1;
    QOpenGLBuffer m_vertices;
    QOpenGLBuffer m_indices{QOpenGLBuffer::IndexBuffer};
    QOpenGLTexture m_texture{QOpenGLTexture::Target2D};
    // shortcut: always SFM's stdshader_dx9; choose the set per game (e.g. a Garry's Mod port) once one exists.
    std::unique_ptr<ShaderSet> m_shaders = CreateSfmShaderSet(); // outlives m_materials, which point at its shaders
    std::vector<std::unique_ptr<Material>> m_materials;
    std::vector<Material *> m_animatedMaterials; // owned by m_materials
    QElapsedTimer m_materialClock;
};
} // namespace

void ConfigureSfmViewport()
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(2, 1); // GLSL 1.20 uses compatibility built-ins.
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
}

std::unique_ptr<QOpenGLWidget> CreateSfmViewport(QString vertexShader, QString fragmentShader, QString modelPath,
                                           MdlMesh mesh, bool check, bool checker, ViewportSettings *settings)
{
    return std::make_unique<ModelViewport>(std::move(vertexShader), std::move(fragmentShader), std::move(modelPath),
                                           std::move(mesh), check, checker, settings);
}

std::unique_ptr<QOpenGLWidget> CreateSfmVcsViewport(QString shaderDirectory, QString modelPath, MdlMesh mesh,
                                                   bool check, ViewportSettings &settings)
{
    return std::make_unique<ModelViewport>(QString(), QString(), std::move(modelPath), std::move(mesh),
                                           check, false, &settings, std::move(shaderDirectory));
}
