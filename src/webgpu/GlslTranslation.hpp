#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace webgpu {

struct GlslDiagnostic {
    std::string message;
    std::size_t line = 0;
};

struct GlslControl {
    std::string name;
    std::size_t arity;
    std::array<float, 4> defaultValue{};
    std::size_t offset;
};

struct GlslProfileResult {
    enum class Status { Ready, Unsupported, Error };
    Status status = Status::Error;
    std::vector<GlslDiagnostic> diagnostics;
    std::vector<GlslControl> controls;
    std::string adaptedSource;
};

GlslProfileResult classifyGlsl(std::string_view source);

struct GlslTranslationTools {
    std::filesystem::path directory;
    static GlslTranslationTools fromEnvironment();
};

struct GlslTranslationResult {
    enum class Status { Success, Unavailable, Error, Canceled, TimedOut };
    Status status = Status::Error;
    std::vector<GlslDiagnostic> diagnostics;
    std::vector<GlslControl> controls;
    std::string wgsl;
    std::string artifactDigest;
    std::string compilerIdentity;
};

class GlslTranslationJob {
public:
    GlslTranslationJob() = default;
    ~GlslTranslationJob();
    GlslTranslationJob(const GlslTranslationJob&) = delete;
    GlslTranslationJob& operator=(const GlslTranslationJob&) = delete;

    // A canceled result must still be collected before another job can start.
    bool start(GlslTranslationTools tools, std::string source,
               std::chrono::steady_clock::time_point deadline);
    void cancel();
    std::optional<GlslTranslationResult> take();
    bool busy() const { return static_cast<bool>(state); }

private:
    struct State;
    std::shared_ptr<State> state;
    std::thread worker;
};

} // namespace webgpu
