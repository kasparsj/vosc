#include "PerformanceTelemetry.hpp"
#include "ofMain.h"

#include <array>

namespace control {
namespace {

class OpenGlQueryBackend final : public GpuQueryBackend {
public:
    bool initialize() override {
        GLint bits = 0;
        for (int attempt = 0; attempt < 16 && glGetError() != GL_NO_ERROR; ++attempt) {}
        glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
        if (glGetError() != GL_NO_ERROR || bits <= 0) {
            why = "GL_TIME_ELAPSED queries are unsupported or report zero counter bits";
            return false;
        }
        glGenQueries(static_cast<GLsizei>(queries.size()), queries.data());
        if (glGetError() != GL_NO_ERROR) {
            why = "OpenGL could not allocate the bounded timer query pool";
            glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
            queries.fill(0);
            return false;
        }
        enabled = true;
        why.clear();
        return true;
    }

    void shutdown() override {
        if (queryActive) {
            glEndQuery(GL_TIME_ELAPSED);
            queryActive = false;
        }
        if (queries[0] != 0) glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
        queries.fill(0);
        enabled = false;
    }

    bool supported() const override { return enabled; }
    std::string reason() const override { return why; }

    bool begin(size_t slot) override {
        if (!enabled || queryActive || slot >= queries.size()) return false;
        // Isolate this operation from unrelated errors already queued by renderer code.
        for (int attempt = 0; attempt < 16 && glGetError() != GL_NO_ERROR; ++attempt) {}
        glBeginQuery(GL_TIME_ELAPSED, queries[slot]);
        if (glGetError() != GL_NO_ERROR) {
            // The pre-drain isolates this error to glBeginQuery. A failed begin does
            // not own the target, so never end a query that might belong elsewhere.
            why = "OpenGL rejected a timer query begin operation";
            enabled = false;
            return false;
        }
        queryActive = true;
        return true;
    }

    void end() override {
        if (!queryActive) return;
        glEndQuery(GL_TIME_ELAPSED);
        queryActive = false;
    }

    bool available(size_t slot) override {
        GLint ready = GL_FALSE;
        glGetQueryObjectiv(queries.at(slot), GL_QUERY_RESULT_AVAILABLE, &ready);
        return ready == GL_TRUE;
    }

    uint64_t resultNanoseconds(size_t slot) override {
        GLuint64 value = 0;
        glGetQueryObjectui64v(queries.at(slot), GL_QUERY_RESULT, &value);
        return static_cast<uint64_t>(value);
    }

private:
    std::array<GLuint, Capacity> queries{};
    bool enabled = false;
    bool queryActive = false;
    std::string why = "GPU timer query backend has not been initialized";
};

} // namespace

std::unique_ptr<GpuQueryBackend> makeOpenGlQueryBackend() {
    return std::make_unique<OpenGlQueryBackend>();
}

} // namespace control
