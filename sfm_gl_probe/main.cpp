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
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWindow>
#include <QSurfaceFormat>
#include <QVector4D>
#include <QWheelEvent>

#include "mdl_mesh.h"
#include "source_texture.h"

namespace
{
constexpr Vertex quad[] = {
    {{-0.75f, -0.75f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{ 0.75f, -0.75f, 0.0f, 1.0f}, {1.0f, 0.0f}},
    {{-0.75f,  0.75f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{ 0.75f,  0.75f, 0.0f, 1.0f}, {1.0f, 1.0f}},
};

class ShaderProbe final : public QOpenGLWindow, protected QOpenGLFunctions
{
public:
    ShaderProbe(QString vertexPath, QString fragmentPath, QString modelPath, MdlMesh mesh, bool check, bool checker)
        : QOpenGLWindow(PartialUpdateBlit), m_vertexPath(std::move(vertexPath)),
          m_fragmentPath(std::move(fragmentPath)), m_modelPath(std::move(modelPath)),
          m_mesh(std::move(mesh)), m_check(check), m_checker(checker)
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
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        if (!m_program.addShaderFromSourceFile(QOpenGLShader::Vertex, m_vertexPath) ||
            !m_program.addShaderFromSourceFile(QOpenGLShader::Fragment, m_fragmentPath) ||
            !m_program.link() || !m_program.bind())
        {
            qCritical().noquote() << "Shader setup failed:" << m_program.log();
            QCoreApplication::exit(1);
            return;
        }
        m_phong = m_program.uniformLocation("sampler3") >= 0 && m_program.uniformLocation("sampler7") >= 0;
        if (m_phong && (m_checker || !m_mesh.hasTangents || m_mesh.indices.empty()))
        {
            qCritical() << "Phong needs a textured MDL with VVD tangents";
            QCoreApplication::exit(1);
            return;
        }
        if (!m_mesh.indices.empty() && !m_checker)
        {
            const int srgbWrite = m_program.uniformLocation("flSRGBWrite");
            if (srgbWrite < 0)
            {
                qCritical() << "Regenerate the fragment GLSL with the current vcs2glsl for sRGB output";
                QCoreApplication::exit(1);
                return;
            }
            // Qt's partial-update framebuffer stores bytes linearly; encode after lighting, before presentation.
            m_program.setUniformValue(srgbWrite, 1.0f);
        }

        m_position = m_program.attributeLocation("v0");
        m_uv = m_program.attributeLocation(m_phong ? "v2" : "v3");
        if (m_position < 0 || m_uv < 0 || !m_vertices.create() || !m_vertices.bind())
        {
            qCritical() << "Could not bind geometry to the SFM shader";
            QCoreApplication::exit(1);
            return;
        }
        if (m_mesh.indices.empty())
            m_vertices.allocate(quad, sizeof(quad));
        else
        {
            const auto vertices = std::exchange(m_mesh.vertices, {});
            const auto indices = std::exchange(m_mesh.indices, {});
            m_vertices.allocate(vertices.data(), int(vertices.size() * sizeof(Vertex)));
            if (!m_indices.create() || !m_indices.bind())
            {
                qCritical() << "Could not bind MDL triangle indices";
                QCoreApplication::exit(1);
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
                    texture = std::make_unique<SourceMaterial>();
                    const auto path = LoadSourceMaterial(*texture, m_modelPath, m_mesh.textures[batch.texture],
                                                         m_mesh.materialPaths, m_phong);
                    if (glGetError() != GL_NO_ERROR)
                        throw std::runtime_error((path + QStringLiteral(": OpenGL texture upload failed")).toStdString());
                    qInfo().noquote() << "Loaded base texture:" << path << texture->base.width() << 'x' << texture->base.height();
                    if (m_phong)
                        qInfo() << "Phong exponent override:" << texture->exponent << "boost:" << texture->fresnelBoost.w();
                }
            }
            catch (const std::exception &error)
            {
                qCritical().noquote() << error.what();
                QCoreApplication::exit(1);
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

        // shortcut: bindings cover the SFM generic SM3 base-texture variants only; expand with VMT feature support.
        const int lightCount = m_program.uniformLocation("i0");
        m_vertexLit = lightCount >= 0;
        std::array<QVector4D, 97> vc{};
        vc[0] = {0, 1, 2, 0.5f}; // Source math constants, including the D3D-to-GL Z fixup.
        if (m_vertexLit || m_phong)
        {
            // HLMV defaults: six equal ambient faces and one white directional light at yaw 180°.
            std::fill_n(vc.begin() + 21, 6, QVector4D(0.3f, 0.3f, 0.3f, 1));
            vc[27] = {1, 1, 1, 1}; // RGB, directional flag.
            vc[28] = {-1, 0, 0, 0}; // Direction of travel, spot flag.
            vc[29] = {0, 0, 0, 1}; // Position (unused for directional lights).
            vc[30] = {0, 1, 1, 1}; // Non-spot defaults.
            vc[31] = {1, 0, 0, 0}; // Constant attenuation.
            if (m_vertexLit)
                m_program.setUniformValue(lightCount, 1);
            if (m_phong)
                m_program.setUniformValue("b0", 1);
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
        if (m_phong)
        {
            std::fill_n(pc.begin() + 4, 6, QVector4D(0.3f, 0.3f, 0.3f, 1));
            pc[20] = {1, 1, 1, 0};
            // Source's pixel shader packs a directional light as a distant point.
            pc[21] = QVector4D(m_mesh.center + QVector3D(10000, 0, 0), 0);
            m_program.setUniformValue("sampler3", 3);
            m_program.setUniformValue("sampler7", 7);
        }
        m_program.setUniformValueArray("vc", vc.data(), m_vertexLit ? int(vc.size()) : 67);
        m_program.setUniformValueArray("vcbones", bones, 3);
        m_program.setUniformValue("vcscreen", QVector4D{});
        m_program.setUniformValueArray("pc", pc.data(), pc.size());
        m_program.setUniformValue("sampler0", 0);
        m_program.release();
        m_ready = true;
    }

    void mousePressEvent(QMouseEvent *event) override { m_mouse = event->position(); }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_indexCount)
            return;
        const auto delta = event->position() - m_mouse;
        m_mouse = event->position();
        if (event->buttons() & Qt::LeftButton)
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
        m_zoom = std::clamp(m_zoom * std::exp(-event->angleDelta().y() / 960.0f), 0.1f, 10.0f);
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
        const auto eye = target + cameraDirection() * (3 * radius);
        const float aspect = float(width()) / float(height());
        const float halfWidth = radius * m_zoom * 1.1f * (aspect > 1 ? aspect : 1);
        const float halfHeight = radius * m_zoom * 1.1f * (aspect < 1 ? 1 / aspect : 1);
        QMatrix4x4 view, projection;
        view.lookAt(eye, target, QVector3D(0, 0, 1));
        projection.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, radius, 5 * radius);
        const auto mvp = projection * view;
        const QVector4D rows[] = {mvp.row(0), -mvp.row(1), (mvp.row(2) + mvp.row(3)) * 0.5f, mvp.row(3)};
        m_program.setUniformValue("vc[2]", QVector4D(eye, 0));
        m_program.setUniformValueArray("vc[8]", rows, 4);
        if (m_phong)
            m_program.setUniformValue("pc[11]", QVector4D(eye, 0));
    }

    void drawScene(bool specular)
    {
        glClearColor(0, 0, 0, 1);
        if (m_indexCount)
            glEnable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_program.bind();
        uploadCamera();
        m_vertices.bind();
        if (m_materialTextures.empty())
            m_texture.bind(0);
        m_program.enableAttributeArray(m_position);
        m_program.enableAttributeArray(m_uv);
        m_program.setAttributeBuffer(m_position, GL_FLOAT, offsetof(Vertex, position), 4, sizeof(Vertex));
        m_program.setAttributeBuffer(m_uv, GL_FLOAT, offsetof(Vertex, uv), 2, sizeof(Vertex));
        const int normal = m_program.attributeLocation("v1");
        if (normal >= 0)
        {
            if (m_indexCount)
            {
                m_program.enableAttributeArray(normal);
                m_program.setAttributeBuffer(normal, GL_FLOAT, offsetof(Vertex, normal), 3, sizeof(Vertex));
            }
            else
                m_program.setAttributeValue(normal, QVector4D{0, 0, 1, 0});
        }
        const int tangent = m_phong ? m_program.attributeLocation("v4") : -1;
        if (tangent >= 0)
        {
            m_program.enableAttributeArray(tangent);
            m_program.setAttributeBuffer(tangent, GL_FLOAT, offsetof(Vertex, tangent), 4, sizeof(Vertex));
        }
        if (!m_phong)
            if (const int color = m_program.attributeLocation("v2"); color >= 0)
                m_program.setAttributeValue(color, QVector4D{1, 1, 1, 1});
        if (m_indexCount)
        {
            m_indices.bind();
            if (m_materialTextures.empty())
                glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
            else
                for (const auto &batch : m_mesh.batches)
                {
                    auto &material = *m_materialTextures[batch.texture];
                    material.base.bind(0);
                    if (m_phong)
                    {
                        material.normal.bind(3);
                        material.exponentMap.bind(7);
                        auto fresnelBoost = material.fresnelBoost;
                        if (!specular)
                            fresnelBoost.setW(0);
                        m_program.setUniformValue("pc[10]", QVector4D(0, material.luminanceMask, material.exponent, 0));
                        m_program.setUniformValue("pc[19]", fresnelBoost);
                        m_program.setUniformValue("pc[26]", material.tint);
                        m_program.setUniformValue("pc[27]", QVector4D(material.alphaMask, 0, 1, 0));
                    }
                    glDrawElements(GL_TRIANGLES, GLsizei(batch.count), GL_UNSIGNED_INT,
                                   reinterpret_cast<const void *>(batch.first * sizeof(uint32_t)));
                }
            if (m_phong)
                glActiveTexture(GL_TEXTURE0);
            m_indices.release();
            if (normal >= 0)
                m_program.disableAttributeArray(normal);
            glDisable(GL_DEPTH_TEST);
        }
        else
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (tangent >= 0)
            m_program.disableAttributeArray(tangent);
        m_program.disableAttributeArray(m_uv);
        m_program.disableAttributeArray(m_position);
        if (m_materialTextures.empty())
            m_texture.release();
        m_vertices.release();
        m_program.release();
    }

    void paintGL() override
    {
        if (!m_ready)
            return;
        drawScene(true);

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
                    for (int y = 0; y < rendered.height(); ++y)
                        for (int x = 0; x < rendered.width(); ++x)
                        {
                            const auto *pixel = rendered.constScanLine(y) + 4 * x;
                            const auto shade = std::max({pixel[0], pixel[1], pixel[2]});
                            if (shade != 0)
                            {
                                ++covered;
                                shades.set(shade);
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
                    const bool pass = glGetError() == GL_NO_ERROR && covered > area / 100 &&
                        covered < 3 * area / 4 && shades.count() > 16 && sawSpecular;
                    qInfo() << (pass ? "PASS:" : "FAIL:") << "textured MDL pixels" << covered << "of" << area
                            << "with" << shades.count() << "nonzero shades";
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
                qCritical() << "FAIL: GL error" << Qt::hex << error << Qt::dec
                            << "viewport" << viewport[2] << viewport[3]
                            << "pixels (RGBA)" << left << right << outside;
            QCoreApplication::exit(pass ? 0 : 1);
        }
    }

private:
    QString m_vertexPath;
    QString m_fragmentPath;
    QString m_modelPath;
    MdlMesh m_mesh;
    bool m_check;
    bool m_checker;
    bool m_ready = false;
    bool m_vertexLit = false;
    bool m_phong = false;
    bool m_cameraDirty = true;
    float m_yaw = -0.915f;
    float m_pitch = 0.454f;
    float m_zoom = 1;
    QPointF m_mouse;
    QVector3D m_pan;
    int m_position = -1;
    int m_uv = -1;
    GLsizei m_indexCount = 0;
    QOpenGLShaderProgram m_program;
    QOpenGLBuffer m_vertices;
    QOpenGLBuffer m_indices{QOpenGLBuffer::IndexBuffer};
    QOpenGLTexture m_texture{QOpenGLTexture::Target2D};
    std::vector<std::unique_ptr<SourceMaterial>> m_materialTextures;
};
} // namespace

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(2, 1); // GLSL 1.20 uses compatibility built-ins.
    format.setAlphaBufferSize(8);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    auto args = app.arguments();
    const bool check = args.removeOne(QStringLiteral("--check"));
    const bool checker = args.removeOne(QStringLiteral("--checker"));
    const auto count = args.size();
    if ((count != 3 && count != 4) || (checker && count != 4))
    {
        qCritical() << "usage: sfm_gl_probe <vertex GLSL> <fragment GLSL> [model.mdl [--checker]] [--check]";
        return 2;
    }

    const QString path = count == 4 ? args[3] : QString();
    MdlMesh mesh{};
    try
    {
        if (!path.isEmpty())
            mesh = LoadMdlMesh(path);
    }
    catch (const std::exception &e)
    {
        qCritical().noquote() << e.what();
        return 1;
    }

    ShaderProbe window(args[1], args[2], path, std::move(mesh), check, checker);
    window.show();
    return app.exec();
}
