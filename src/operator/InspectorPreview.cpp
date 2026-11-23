#include "InspectorPreview.hpp"

#if VOSC_OPERATOR_UI
#include "../Buffer.hpp"
#include "../Geom.h"
#include "../Texture.h"
#include "../input/OSCInput.h"
#include "../tex/FboPingPongTex.h"
#include "../tex/FboTex.h"
#include "../tex/VideoTex.h"
#include "../tex/WebcamTex.h"
#include "../utils.h"
#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace vosc::operator_ui {
namespace {

struct Rect {
    float x, y, w, h;
};

Rect overlap(Rect a, Rect b) {
    const float left = std::max(a.x, b.x);
    const float top = std::max(a.y, b.y);
    return {left, top, std::min(a.x + a.w, b.x + b.w) - left,
            std::min(a.y + a.h, b.y + b.h) - top};
}

Rect fit(Rect rect, float width, float height) {
    const float scale = std::min(rect.w / width, rect.h / height);
    const float w = width * scale, h = height * scale;
    return {rect.x + (rect.w - w) * .5f, rect.y + (rect.h - h) * .5f, w, h};
}

bool hasArea(Rect rect) {
    return rect.w >= 1.f && rect.h >= 1.f && std::isfinite(rect.x) && std::isfinite(rect.y)
        && std::isfinite(rect.w) && std::isfinite(rect.h);
}

// Resolving an MSAA output can blit. Do not clip that engine operation to the
// preview slot or let it replace the host's framebuffer bindings.
struct OutputAccess {
    OutputAccess() {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_READ_BUFFER, &readBuffer);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        if (scissor) glDisable(GL_SCISSOR_TEST);
    }
    ~OutputAccess() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
        glReadBuffer(readBuffer);
        if (scissor) glEnable(GL_SCISSOR_TEST);
    }
    GLint read = 0, draw = 0, readBuffer = 0;
    GLboolean scissor = GL_FALSE;
};

const ofTexture* allocated(const ofTexture& texture) {
    const auto& data = texture.getTextureData();
    return texture.isAllocated() && data.textureID != 0 && data.width > 0 && data.height > 0
        ? &texture : nullptr;
}

const ofTexture* fboOutput(const Texture& texture) {
    const OutputAccess state;
    return allocated(texture.getTexture());
}

struct PaintState {
    static constexpr GLenum caps[] = {
        GL_BLEND, GL_CLIP_DISTANCE0, GL_COLOR_LOGIC_OP, GL_CULL_FACE, GL_DEPTH_TEST,
        GL_FRAMEBUFFER_SRGB, GL_LINE_SMOOTH, GL_PROGRAM_POINT_SIZE, GL_RASTERIZER_DISCARD,
        GL_SCISSOR_TEST, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE, GL_SAMPLE_MASK,
        GL_STENCIL_TEST
    };
    static constexpr auto textures = std::array{
        GL_TEXTURE_2D, GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_BUFFER,
#if ALLOW_TEX_2D_ARRAY
        GL_TEXTURE_2D_ARRAY,
#endif
    };
    static constexpr auto bindings = std::array{
        GL_TEXTURE_BINDING_2D, GL_TEXTURE_BINDING_RECTANGLE_ARB, GL_TEXTURE_BINDING_BUFFER,
#if ALLOW_TEX_2D_ARRAY
        GL_TEXTURE_BINDING_2D_ARRAY,
#endif
    };
    std::array<GLboolean, std::size(caps)> enabled{};
    std::array<GLint, textures.size()> boundTextures{};
    std::array<GLint, 4> viewport{}, scissor{}, blend{};
    std::array<GLboolean, 4> mask{};
    std::array<GLint, 2> polygon{};
    GLint blendRgb = 0, blendAlpha = 0, program = 0, vao = 0, buffer = 0;
    GLint active = 0, sampler = 0;
    GLfloat lineWidth = 1;

    PaintState() {
        glGetIntegerv(GL_VIEWPORT, viewport.data());
        glGetIntegerv(GL_SCISSOR_BOX, scissor.data());
        glGetIntegerv(GL_POLYGON_MODE, polygon.data());
        glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
        glGetFloatv(GL_LINE_WIDTH, &lineWidth);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glActiveTexture(GL_TEXTURE0);
        for (std::size_t i = 0; i < bindings.size(); ++i)
            glGetIntegerv(bindings[i], &boundTextures[i]);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
        glBindSampler(0, 0);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blend[0]);
        glGetIntegerv(GL_BLEND_DST_RGB, &blend[1]);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend[2]);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blend[3]);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendAlpha);
        for (std::size_t i = 0; i < std::size(caps); ++i) {
            enabled[i] = glIsEnabled(caps[i]);
            if (enabled[i]) glDisable(caps[i]);
        }
        glEnable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glLineWidth(1);
    }

    ~PaintState() {
        glUseProgram(program);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        for (std::size_t i = 0; i < textures.size(); ++i) glBindTexture(textures[i], boundTextures[i]);
        glBindSampler(0, sampler);
        glActiveTexture(active);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        glColorMask(mask[0], mask[1], mask[2], mask[3]);
        glPolygonMode(GL_FRONT_AND_BACK, polygon[0]);
        glLineWidth(lineWidth);
        glBlendEquationSeparate(blendRgb, blendAlpha);
        glBlendFuncSeparate(blend[0], blend[1], blend[2], blend[3]);
        for (std::size_t i = 0; i < std::size(caps); ++i) {
            if (enabled[i]) glEnable(caps[i]);
            else glDisable(caps[i]);
        }
    }
};

constexpr auto quadVertex = R"GLSL(#version 330 core
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

constexpr auto solidVertex = R"GLSL(#version 330 core
layout(location = 0) in vec2 position;
void main() {
    gl_Position = vec4(position * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
    gl_PointSize = 2.0;
}
)GLSL";

constexpr auto solidFragment = R"GLSL(#version 330 core
uniform vec4 tint;
out vec4 fragColor;
void main() { fragColor = tint; }
)GLSL";

constexpr auto bufferFragment = R"GLSL(#version 330 core
uniform samplerBuffer source;
uniform int texels;
in vec2 uv;
out vec4 fragColor;
void main() {
    int count = min(texels, textureSize(source));
    if (count < 1) discard;
    int index = clamp(int(uv.x * float(count)), 0, count - 1);
    fragColor = vec4(clamp(texelFetch(source, index).rgb, 0.0, 1.0), 1.0);
}
)GLSL";

std::string imageFragment(const char* sampler, const char* coord) {
    return std::string("#version 330 core\nuniform ") + sampler + R"GLSL( source;
uniform mat4 textureMatrix;
uniform vec2 extent;
uniform bool flip;
uniform int slice;
in vec2 uv;
out vec4 fragColor;
void main() {
    vec2 pos = (textureMatrix * vec4(vec2(uv.x, flip ? 1.0 - uv.y : uv.y) * extent, 0.0, 1.0)).xy;
    fragColor = texture(source, )GLSL" + coord + ");\n}";
}

GLuint compile(GLenum type, const char* source) {
    const auto shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled) return shader;
    char detail[1024]{};
    glGetShaderInfoLog(shader, sizeof(detail), nullptr, detail);
    ofLogWarning("InspectorPreview") << "Could not compile preview shader: " << detail;
    glDeleteShader(shader);
    return 0;
}

struct Program {
    GLuint id = 0;
    bool attempted = false;
    GLint matrix = -1, extent = -1, flip = -1, slice = -1, texels = -1, tint = -1;

    ~Program() {
        if (id) glDeleteProgram(id);
    }

    bool use(const char* vertex, const char* fragment) {
        if (!attempted) {
            attempted = true;
            const auto vert = compile(GL_VERTEX_SHADER, vertex);
            if (!vert) return false;
            const auto frag = compile(GL_FRAGMENT_SHADER, fragment);
            if (!frag) {
                glDeleteShader(vert);
                return false;
            }
            id = glCreateProgram();
            glAttachShader(id, vert);
            glAttachShader(id, frag);
            glLinkProgram(id);
            glDeleteShader(vert);
            glDeleteShader(frag);
            GLint linked = 0;
            glGetProgramiv(id, GL_LINK_STATUS, &linked);
            if (!linked) {
                char detail[1024]{};
                glGetProgramInfoLog(id, sizeof(detail), nullptr, detail);
                ofLogWarning("InspectorPreview") << "Could not link preview shader: " << detail;
                glDeleteProgram(id);
                id = 0;
                return false;
            }
            matrix = glGetUniformLocation(id, "textureMatrix");
            extent = glGetUniformLocation(id, "extent");
            flip = glGetUniformLocation(id, "flip");
            slice = glGetUniformLocation(id, "slice");
            texels = glGetUniformLocation(id, "texels");
            tint = glGetUniformLocation(id, "tint");
        }
        if (!id) return false;
        glUseProgram(id);
        return true;
    }
};

bool integerFormat(GLint format) {
    switch (format) {
        case GL_R8I: case GL_R8UI: case GL_R16I: case GL_R16UI: case GL_R32I: case GL_R32UI:
        case GL_RG8I: case GL_RG8UI: case GL_RG16I: case GL_RG16UI: case GL_RG32I: case GL_RG32UI:
        case GL_RGB8I: case GL_RGB8UI: case GL_RGB16I: case GL_RGB16UI: case GL_RGB32I: case GL_RGB32UI:
        case GL_RGBA8I: case GL_RGBA8UI: case GL_RGBA16I: case GL_RGBA16UI: case GL_RGBA32I: case GL_RGBA32UI:
        case GL_RGB10_A2UI:
            return true;
        default: return false;
    }
}

float normalized(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
}

// Keep mesh preview work independent of the size and instancing of the live scene.
GLenum projectWireframe(Geom& geometry, std::size_t budget, std::vector<glm::vec2>& lines) {
    lines.clear();
    if (!geometry.hasMesh()) return GL_LINES;
    const ofMesh& mesh = geometry.getMesh();
    const auto& indices = mesh.getIndices();
    const auto& vertices = mesh.getVertices();
    const auto count = std::min(budget, indices.empty() ? vertices.size() : indices.size());
    const bool spatial = geometry.getSize().z > 1.e-5f;
    glm::vec2 min(std::numeric_limits<float>::max()), max(-std::numeric_limits<float>::max());

    const auto append = [&](std::size_t i) {
        const auto index = indices.empty() ? i : indices[i];
        if (index >= vertices.size()) return false;
        const auto& v = vertices[index];
        const glm::vec2 point = spatial
            ? glm::vec2(.82f * v.x - .55f * v.z, .91f * v.y + .21f * v.x + .33f * v.z)
            : glm::vec2(v.x, v.y);
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
        min = glm::min(min, point);
        max = glm::max(max, point);
        lines.push_back(point);
        return true;
    };
    const auto edge = [&](std::size_t a, std::size_t b) {
        const auto start = lines.size();
        if (!append(a) || !append(b)) lines.resize(start);
    };
    const auto triangle = [&](std::size_t a, std::size_t b, std::size_t c) {
        edge(a, b);
        edge(b, c);
        edge(c, a);
    };

    GLenum mode = GL_LINES;
    switch (mesh.getMode()) {
        case OF_PRIMITIVE_TRIANGLES:
            for (std::size_t i = 2; i < count; i += 3) triangle(i - 2, i - 1, i);
            break;
        case OF_PRIMITIVE_TRIANGLE_STRIP:
            for (std::size_t i = 2; i < count; ++i) triangle(i - 2, i - 1, i);
            break;
        case OF_PRIMITIVE_TRIANGLE_FAN:
            for (std::size_t i = 2; i < count; ++i) triangle(0, i - 1, i);
            break;
        case OF_PRIMITIVE_LINES:
            for (std::size_t i = 1; i < count; i += 2) edge(i - 1, i);
            break;
        case OF_PRIMITIVE_LINE_STRIP:
        case OF_PRIMITIVE_LINE_LOOP:
            for (std::size_t i = 1; i < count; ++i) edge(i - 1, i);
            if (mesh.getMode() == OF_PRIMITIVE_LINE_LOOP && count > 2) edge(count - 1, 0);
            break;
        case OF_PRIMITIVE_POINTS:
            for (std::size_t i = 0; i < count; ++i) append(i);
            mode = GL_POINTS;
            break;
        default: break;
    }
    if (lines.empty()) return mode;
    const auto span = max - min;
    const auto scale = .90f / std::max({span.x, span.y, 1.e-5f});
    for (auto& p : lines) p = ((p - min) - span * .5f) * scale + .5f;
    return mode;
}

} // namespace

const ofTexture* inspectorTextureOutput(const Texture& texture) {
    if (!texture.isLoaded() || texture.feedbackSetupPending) return nullptr;
    if (texture.numFrames > 1 || texture.hasLooper()) {
        if (texture.numFrames > 1 && (texture.frames.empty() || texture.curFbo < 0)) return nullptr;
        if (texture.getNumTextures() < 1) return nullptr;
        return fboOutput(texture);
    }
    if (!texture.passes.empty()) {
        const auto& pass = texture.passes.back();
        if (!pass || !pass->getShader() || !pass->isAllocated()) return nullptr;
        return fboOutput(texture);
    }
    const auto* backing = texture.tex.get();
    if (const auto* fbo = dynamic_cast<const FboTex*>(backing)) {
        return fbo->fbo.isAllocated() && fbo->fbo.getNumTextures() > 0 ? fboOutput(texture) : nullptr;
    }
    if (const auto* pingPong = dynamic_cast<const FboPingPongTex*>(backing)) {
        const auto& source = pingPong->fbo.source();
        return source.isAllocated() && source.getNumTextures() > 0 ? fboOutput(texture) : nullptr;
    }
    if ((dynamic_cast<const WebcamTex*>(backing) || dynamic_cast<const VideoTex*>(backing))
        && !backing->startupFrameReady()) return nullptr;
    try {
        return allocated(texture.getTexture());
    } catch (const char*) {
        // Optional backends can reject the const texture interface.
        return nullptr;
    }
}

int inspectorArrayDepth(const ofTexture& output) {
#if ALLOW_TEX_2D_ARRAY
    return output.isAllocated() && output.getTextureData().textureTarget == GL_TEXTURE_2D_ARRAY
        ? getTextureDepth(output) : 0;
#else
    (void)output;
    return 0;
#endif
}

bool inspectorCanPreviewTexture(const ofTexture& output) {
    const auto& data = output.getTextureData();
    return !integerFormat(data.glInternalFormat)
        && (data.textureTarget == GL_TEXTURE_2D || data.textureTarget == GL_TEXTURE_RECTANGLE_ARB
            || inspectorArrayDepth(output) > 0);
}

std::size_t inspectorBufferTexels(const Buffer& buffer) {
    const auto& data = buffer.getTexture().getTextureData();
    if (!data.bAllocated || !data.textureID || data.textureTarget != GL_TEXTURE_BUFFER
        || data.glInternalFormat != GL_RGBA32F) return 0;
    return std::max<GLsizeiptr>(0, buffer.buf.size()) / (sizeof(float) * 4);
}

struct InspectorPreview::Renderer {
    GLuint vao = 0, vbo = 0, sampler = 0;
    Program image, rectangle, sampleBuffer, solid;
#if ALLOW_TEX_2D_ARRAY
    Program array;
#endif
    std::vector<glm::vec2> vertices;

    Renderer() {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
        glGenSamplers(1, &sampler);
        glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    ~Renderer() {
        glDeleteSamplers(1, &sampler);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }

    void drawTexture(const ofTexture& texture, int layer) {
        const auto& data = texture.getTextureData();
        if (!inspectorCanPreviewTexture(texture)) return;
        Program* current = nullptr;
        if (data.textureTarget == GL_TEXTURE_2D) {
            static const auto fragment = imageFragment("sampler2D", "pos");
            if (image.use(quadVertex, fragment.c_str())) current = &image;
        } else if (data.textureTarget == GL_TEXTURE_RECTANGLE_ARB) {
            static const auto fragment = imageFragment("sampler2DRect", "pos");
            if (rectangle.use(quadVertex, fragment.c_str())) current = &rectangle;
#if ALLOW_TEX_2D_ARRAY
        } else if (const int depth = inspectorArrayDepth(texture); depth > 0) {
            static const auto fragment = imageFragment("sampler2DArray", "vec3(pos, float(slice))");
            if (array.use(quadVertex, fragment.c_str())) {
                current = &array;
                glUniform1i(current->slice, std::clamp(layer, 0, depth - 1));
            }
#endif
        }
#if !ALLOW_TEX_2D_ARRAY
        (void)layer;
#endif
        if (!current) return;
        const auto uv = texture.getCoordFromPercent(1, 1);
        const auto matrix = texture.isUsingTextureMatrix() ? texture.getTextureMatrix() : glm::mat4(1);
        glBindSampler(0, sampler);
        glBindTexture(data.textureTarget, data.textureID);
        glUniformMatrix4fv(current->matrix, 1, GL_FALSE, &matrix[0][0]);
        glUniform2f(current->extent, uv.x, uv.y);
        glUniform1i(current->flip, data.bFlipTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void drawBuffer(const Buffer& buffer, std::size_t count) {
        if (!sampleBuffer.use(quadVertex, bufferFragment)) return;
        glBindSampler(0, 0);
        glBindTexture(GL_TEXTURE_BUFFER, buffer.getTexture().getTextureData().textureID);
        const auto maxTexels = static_cast<std::size_t>(std::numeric_limits<GLint>::max());
        glUniform1i(sampleBuffer.texels, static_cast<GLint>(std::min(count, maxTexels)));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void drawSolid(GLenum mode, const glm::vec4& color) {
        if (vertices.empty() || !solid.use(solidVertex, solidFragment)) return;
        glUniform4fv(solid.tint, 1, &color[0]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec2), vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(mode, 0, static_cast<GLsizei>(vertices.size()));
    }

    void drawGeometry(Geom& geometry, Rect size) {
        const auto mode = projectWireframe(geometry, size.w >= 100.f ? 3000 : 300, vertices);
        drawSolid(mode, {.69f, .82f, .94f, 1.f});
    }

    void drawInput(const OSCInput& input, Rect size) {
        const float amplitude = normalized(input.ampScaled);
        const float radius = amplitude * std::min(.16f * size.w, .38f * size.h);
        vertices.clear();
        if (radius >= .5f) {
            vertices.emplace_back(.2f, .5f);
            for (int i = 0; i <= 32; ++i) {
                const float angle = static_cast<float>(i) * 6.2831853f / 32;
                vertices.emplace_back(.2f + std::cos(angle) * radius / size.w,
                                      .5f + std::sin(angle) * radius / size.h);
            }
            drawSolid(GL_TRIANGLE_FAN, {.69f, .82f, .94f, 1.f});
        }
        const auto count = input.ampHist.size();
        if (count < 2) return;
        vertices.clear();
        for (std::size_t i = 0; i < count; ++i)
            vertices.emplace_back(.42f + .55f * i / (count - 1),
                                  .90f - .8f * normalized(input.ampHist[i]));
        drawSolid(GL_LINE_STRIP, {.95f, .57f, .74f, 1.f});
    }
};

InspectorPreview::InspectorPreview() = default;
InspectorPreview::~InspectorPreview() = default;

void InspectorPreview::paint(Rml::Element& content, const std::vector<InspectorPreviewSlot>& slots,
                             const int* viewport) {
    if (slots.empty() || viewport[2] <= 0 || viewport[3] <= 0) return;
    Rml::ElementList elements;
    content.GetElementsByClassName(elements, "inspect-preview");
    const auto origin = content.GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rect window{0, 0, static_cast<float>(viewport[2]), static_cast<float>(viewport[3])};
    const Rect clip = overlap(window, {origin.x + content.GetClientLeft(), origin.y + content.GetClientTop(),
                                      content.GetClientWidth(), content.GetClientHeight()});
    if (!hasArea(clip)) return;

    const PaintState state;
    if (!renderer) renderer = std::make_unique<Renderer>();
    glBindVertexArray(renderer->vao);
    for (auto* element : elements) {
        const auto index = element->GetAttribute<int>("data-preview", -1);
        if (index < 0 || static_cast<std::size_t>(index) >= slots.size() || !element->IsVisible(true)) continue;
        const auto position = element->GetAbsoluteOffset(Rml::BoxArea::Content);
        const auto size = element->GetBox().GetSize(Rml::BoxArea::Content);
        const Rect area{position.x, position.y, size.x, size.y};
        if (!hasArea(overlap(clip, area))) continue;

        const auto setArea = [&](Rect rect) {
            const auto visible = overlap(clip, rect);
            if (!hasArea(visible)) return false;
            const auto left = static_cast<GLint>(std::ceil(visible.x));
            const auto top = static_cast<GLint>(std::ceil(visible.y));
            const auto right = static_cast<GLint>(std::floor(visible.x + visible.w));
            const auto bottom = static_cast<GLint>(std::floor(visible.y + visible.h));
            if (right <= left || bottom <= top) return false;
            glScissor(viewport[0] + left, viewport[1] + viewport[3] - bottom, right - left, bottom - top);
            glViewport(viewport[0] + static_cast<GLint>(std::lround(rect.x)),
                       viewport[1] + viewport[3] - static_cast<GLint>(std::lround(rect.y + rect.h)),
                       static_cast<GLsizei>(std::lround(rect.w)), static_cast<GLsizei>(std::lround(rect.h)));
            return true;
        };
        const auto& slot = slots[index];
        if (const auto texture = slot.texture.lock()) {
            const auto* output = inspectorTextureOutput(*texture);
            if (output && setArea(fit(area, output->getWidth(), output->getHeight())))
                renderer->drawTexture(*output, slot.slice);
        } else if (const auto buffer = slot.buffer.lock()) {
            const auto count = inspectorBufferTexels(*buffer);
            if (count && setArea(fit(area, 4.f, 1.f))) renderer->drawBuffer(*buffer, count);
        } else if (const auto geometry = slot.geometry.lock()) {
            if (const auto square = fit(area, 1.f, 1.f); setArea(square)) renderer->drawGeometry(*geometry, square);
        } else if (const auto input = slot.input.lock()) {
            if (setArea(area)) renderer->drawInput(*input, area);
        }
    }
}

} // namespace vosc::operator_ui
#endif
