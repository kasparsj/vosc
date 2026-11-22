#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

// Requires desktop OpenGL 3.3 declarations. The renderer owns the current context.
class LocalizedDockBlur {
public:
    LocalizedDockBlur() = default;
    LocalizedDockBlur(const LocalizedDockBlur&) = delete;
    LocalizedDockBlur& operator=(const LocalizedDockBlur&) = delete;

    // The dock is in viewport-relative backing pixels, measured from the top left.
    // The source and final destination are the color draw buffer currently bound by the host.
    bool draw(const GLint* viewport, float x, float y, float w, float h, float radius, float blurRadius) {
        if (viewport[2] <= 0 || viewport[3] <= 0 || w <= 0 || h <= 0 || blurRadius <= 0) return true;

        const float left = viewport[0] + x;
        const float bottom = viewport[1] + viewport[3] - y - h;
        const float pad = std::ceil(blurRadius) + 2;
        const auto clipX = [&](float edge) {
            return static_cast<GLint>(std::clamp(edge, float(viewport[0]), float(viewport[0] + viewport[2])));
        };
        const auto clipY = [&](float edge) {
            return static_cast<GLint>(std::clamp(edge, float(viewport[1]), float(viewport[1] + viewport[3])));
        };
        const GLint x0 = clipX(std::floor(left - pad));
        const GLint y0 = clipY(std::floor(bottom - pad));
        const GLint x1 = clipX(std::ceil(left + w + pad));
        const GLint y1 = clipY(std::ceil(bottom + h + pad));
        if (x1 <= x0 || y1 <= y0) return true;

        const std::array<GLsizei, 2> size{x1 - x0, y1 - y0};
        if (initializationFailed || size == failedSize) return false;
        State state;
        if (!program && !init()) {
            initializationFailed = true;
            return false;
        }
        if (width != size[0] || height != size[1]) {
            if (!resize(size[0], size[1])) {
                width = height = 0;
                failedSize = size;
                return false;
            }
            failedSize = {};
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, state.drawFramebuffer);
        GLint previousReadBuffer = 0;
        glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
        glReadBuffer(state.drawBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[0]);
        // Preserve dimensions when resolving the multisampled window backbuffer.
        glBlitFramebuffer(x0, y0, x1, y1, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glReadBuffer(previousReadBuffer);

        glUseProgram(program);
        glBindVertexArray(vao);
        glUniform2f(uniforms.inverseSize, 1.f / width, 1.f / height);
        const float stride = blurRadius / 3.230769f;
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, textures[0]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures[0]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
        glViewport(0, 0, width, height);
        glUniform2f(uniforms.origin, 0, 0);
        glUniform2f(uniforms.step, stride / width, 0);
        glUniform1i(uniforms.composite, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindTexture(GL_TEXTURE_2D, textures[1]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state.drawFramebuffer);
        glViewport(x0, y0, width, height);
        glUniform2f(uniforms.origin, float(x0), float(y0));
        glUniform2f(uniforms.step, 0, stride / height);
        glUniform1i(uniforms.composite, 1);
        glUniform4f(uniforms.dock, left + w / 2, bottom + h / 2, w / 2, h / 2);
        glUniform1f(uniforms.radius, std::clamp(radius, 0.f, std::min(w, h) / 2));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        return true;
    }

    void shutdown() {
        glDeleteFramebuffers(2, framebuffers.data());
        glDeleteTextures(2, textures.data());
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
        framebuffers = {};
        textures = {};
        vao = program = 0;
        width = height = 0;
    }

private:
    struct State {
        static constexpr GLenum caps[] = {
            GL_BLEND, GL_CLIP_DISTANCE0, GL_COLOR_LOGIC_OP, GL_CULL_FACE, GL_DEPTH_TEST,
            GL_DITHER, GL_FRAMEBUFFER_SRGB, GL_RASTERIZER_DISCARD, GL_SCISSOR_TEST,
            GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE, GL_SAMPLE_MASK, GL_STENCIL_TEST,
        };
        std::array<GLboolean, sizeof(caps) / sizeof(caps[0])> enabled{};
        std::array<GLboolean, 4> colorMask{};
        GLint activeTexture = 0, program = 0, vao = 0, unpackBuffer = 0;
        GLint readFramebuffer = 0, drawFramebuffer = 0, drawBuffer = 0;
        std::array<GLint, 2> textures{}, samplers{}, polygonMode{};
        std::array<GLint, 4> viewport{};

        State() {
            for (std::size_t i = 0; i < enabled.size(); ++i) {
                enabled[i] = glIsEnabled(caps[i]);
                if (enabled[i]) glDisable(caps[i]);
            }
            glGetBooleanv(GL_COLOR_WRITEMASK, colorMask.data());
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glGetIntegerv(GL_POLYGON_MODE, polygonMode.data());
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
            glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
            glGetIntegerv(GL_VIEWPORT, viewport.data());
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);

            glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
            for (GLuint i = 0; i < textures.size(); ++i) {
                glActiveTexture(GL_TEXTURE0 + i);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
                glGetIntegerv(GL_SAMPLER_BINDING, &samplers[i]);
                glBindSampler(i, 0);
            }
            glActiveTexture(GL_TEXTURE0);
        }

        ~State() {
            glUseProgram(program);
            glBindVertexArray(vao);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

            glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);
            glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
            for (std::size_t i = 0; i < enabled.size(); ++i)
                if (enabled[i]) glEnable(caps[i]);

            for (GLuint i = 0; i < textures.size(); ++i) {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, textures[i]);
                glBindSampler(i, samplers[i]);
            }
            glActiveTexture(activeTexture);
        }
    };

    static GLuint compile(GLenum type, const char* text) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &text, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok) return shader;
        char message[1024]{};
        glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
        std::fprintf(stderr, "Localized dock blur shader failed: %s\n", message);
        glDeleteShader(shader);
        return 0;
    }

    bool init() {
        constexpr auto vertex = R"GLSL(#version 330 core
void main() {
    const vec2 corners[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    gl_Position = vec4(corners[gl_VertexID], 0, 1);
})GLSL";
        constexpr auto fragment = R"GLSL(#version 330 core
uniform sampler2D u_source;
uniform sampler2D u_sharp;
uniform vec2 u_origin;
uniform vec2 u_inverse_size;
uniform vec2 u_step;
uniform vec4 u_dock;
uniform float u_radius;
uniform bool u_composite;
out vec4 color;
void main() {
    float coverage = 1.0;
    if (u_composite) {
        vec2 q = abs(gl_FragCoord.xy - u_dock.xy) - u_dock.zw + u_radius;
        float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_radius;
        coverage = 1.0 - smoothstep(-0.5, 0.5, distance);
        if (coverage <= 0.0) discard;
    }
    vec2 uv = (gl_FragCoord.xy - u_origin) * u_inverse_size;
    vec2 nearTap = u_step * 1.384615;
    vec2 farTap = u_step * 3.230769;
    vec4 soften = texture(u_source, uv) * .227027
                + (texture(u_source, uv - nearTap) + texture(u_source, uv + nearTap)) * .316216
                + (texture(u_source, uv - farTap) + texture(u_source, uv + farTap)) * .070270;
    if (u_composite) {
        vec4 sharp = texture(u_sharp, uv);
        color = vec4(mix(sharp.rgb, soften.rgb, coverage), sharp.a);
    } else {
        color = soften;
    }
})GLSL";
        const GLuint vs = compile(GL_VERTEX_SHADER, vertex);
        const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
        if (!vs || !fs) {
            glDeleteShader(vs);
            glDeleteShader(fs);
            return false;
        }
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char message[1024]{};
            glGetProgramInfoLog(program, sizeof(message), nullptr, message);
            std::fprintf(stderr, "Localized dock blur program failed: %s\n", message);
            glDeleteProgram(program);
            program = 0;
            return false;
        }
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "u_source"), 0);
        glUniform1i(glGetUniformLocation(program, "u_sharp"), 1);
        uniforms = {
            glGetUniformLocation(program, "u_origin"), glGetUniformLocation(program, "u_inverse_size"),
            glGetUniformLocation(program, "u_step"), glGetUniformLocation(program, "u_dock"),
            glGetUniformLocation(program, "u_radius"), glGetUniformLocation(program, "u_composite"),
        };
        glGenVertexArrays(1, &vao);
        glGenFramebuffers(2, framebuffers.data());
        glGenTextures(2, textures.data());
        return true;
    }

    bool resize(GLsizei newWidth, GLsizei newHeight) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        for (std::size_t i = 0; i < textures.size(); ++i) {
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, newWidth, newHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[i]);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[i], 0);
            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::fprintf(stderr, "Localized dock blur framebuffer is incomplete\n");
                return false;
            }
        }
        width = newWidth;
        height = newHeight;
        return true;
    }

    GLuint program = 0, vao = 0;
    std::array<GLuint, 2> framebuffers{}, textures{};
    GLsizei width = 0, height = 0;
    bool initializationFailed = false;
    std::array<GLsizei, 2> failedSize{};
    struct Locations { GLint origin, inverseSize, step, dock, radius, composite; } uniforms{};
};
