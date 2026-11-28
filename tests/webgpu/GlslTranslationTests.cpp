#include "webgpu/GlslTranslation.hpp"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;
using webgpu::GlslProfileResult;
using webgpu::GlslTranslationJob;
using webgpu::GlslTranslationResult;
using webgpu::GlslTranslationTools;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using ProfileStatus = GlslProfileResult::Status;
using JobStatus = GlslTranslationResult::Status;

namespace {

constexpr auto Shader = R"glsl(
// uniform sampler2D ignored;
#version 330 core
uniform vec2 resolution;
uniform
    float time, scalar = -.25e+1;
uniform vec4 unset;
uniform vec3 tri = vec3(1, -.25, 3.0);
uniform vec2 pair = vec2(0.5);
layout(location = 0) out vec4 color;
vec4 fn(in vec2 uv, out float x) { x = uv.x; return vec4(uv, 1.0, 1.0); }
void main() {
    vec2 resolution = vec2(1.0);
    float x;
    vec2 uv = gl_FragCoord /* preserve comments */ .xy / resolution;
    color = fn(uv, x) + vec4(unset.r + scalar, tri) + vec4(pair, gl_FragCoord.x, gl_FragCoord.y) + time;
}
)glsl";

const std::string Prefix = "#version 330\nout vec4 color;\n";
const std::string Draw = "void main() { color = vec4(1.0); }\n";

std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    assert(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

GlslTranslationResult collect(GlslTranslationJob& job) {
    const auto wait = Clock::now() + 8s;
    while (Clock::now() < wait) {
        if (auto result = job.take()) return std::move(*result);
        std::this_thread::sleep_for(2ms);
    }
    std::cerr << "Compiler worker failed to settle\n";
    std::abort();
}

GlslTranslationResult run(const fs::path& toolPackage, std::string source, Clock::time_point deadline = Clock::now() + 3s) {
    GlslTranslationJob job;
    assert(job.start({toolPackage}, std::move(source), deadline));
    assert(!job.start({toolPackage}, Prefix + Draw, deadline));
    return collect(job);
}

void testProfile() {
    const auto adapted = webgpu::classifyGlsl(Shader);
    assert(adapted.status == ProfileStatus::Ready);
    assert(adapted.controls.size() == 4);
    assert(adapted.controls[0].name == "scalar" && adapted.controls[0].offset == 0 && adapted.controls[0].arity == 1);
    assert((adapted.controls[0].defaultValue == std::array<float, 4>{-2.5, 0, 0, 0}));
    assert(adapted.controls[1].name == "unset" && adapted.controls[1].offset == 16 && adapted.controls[1].arity == 4);
    assert((adapted.controls[1].defaultValue == std::array<float, 4>{0, 0, 0, 0}));
    assert(adapted.controls[2].name == "tri" && adapted.controls[2].offset == 32 && adapted.controls[2].arity == 3);
    assert((adapted.controls[2].defaultValue == std::array<float, 4>{1, -.25, 3, 0}));
    assert(adapted.controls[3].name == "pair" && adapted.controls[3].offset == 48 && adapted.controls[3].arity == 2);
    assert((adapted.controls[3].defaultValue == std::array<float, 4>{.5, .5, 0, 0}));
    assert(adapted.adaptedSource.find("resolution.y - gl_FragCoord.y") != std::string::npos);
    assert(adapted.adaptedSource.find("FragCoord() /* preserve comments */ .xy") != std::string::npos);
    assert(adapted.adaptedSource.find("layout(offset = 48) vec2 pair;") != std::string::npos);
    assert(adapted.adaptedSource.find("#line 1 0\n") != std::string::npos);
    assert(webgpu::classifyGlsl(Prefix + Draw).status == ProfileStatus::Ready);

    for (const auto* feature : {"uniform sampler2D previous;", "uniform float values[2];", "uniform mat3 rotation;",
                                "uniform float offset;", "#define extra 1\n", "in vec2 uv;", "out vec4 second;"}) {
        const auto unsupported = webgpu::classifyGlsl(Prefix + feature + Draw);
        assert(unsupported.status == ProfileStatus::Unsupported);
        assert(unsupported.diagnostics.front().line == 3);
    }
    assert(webgpu::classifyGlsl(Prefix + "void main() { color = gl_FragCoord; }").status == ProfileStatus::Unsupported);
    assert(webgpu::classifyGlsl(Prefix + "uniform vec3 bad = vec3(1, 2);" + Draw).status == ProfileStatus::Error);
    assert(webgpu::classifyGlsl(Prefix + "uniform float bad = ;" + Draw).status == ProfileStatus::Error);
    assert(webgpu::classifyGlsl(Prefix + "uniform float bad = 1e999;" + Draw).status == ProfileStatus::Error);
    assert(webgpu::classifyGlsl(Prefix + "uniform floatt bad;" + Draw).status == ProfileStatus::Error);
    assert(webgpu::classifyGlsl(Prefix + "uniform float bad" + Draw).status == ProfileStatus::Error);
    assert(webgpu::classifyGlsl(Prefix + "#inef typo\n" + Draw).status == ProfileStatus::Error);
    assert(webgpu::classifyGlsl(Prefix + "/*" + Draw).status == ProfileStatus::Error);
    const auto crlf = webgpu::classifyGlsl("// comment\r\n#version 330\r\nout vec4 color;\r\n" + Draw);
    assert(crlf.status == ProfileStatus::Ready);
    assert(webgpu::classifyGlsl(std::string(128 * 1024 + 1, '\n')).status == ProfileStatus::Error);
}

void requireSuccess(const GlslTranslationResult& result) {
    for (const auto& item : result.diagnostics) std::cerr << item.line << ' ' << item.message << '\n';
    assert(result.status == JobStatus::Success);
    assert(result.compilerIdentity.size() == 64 && result.artifactDigest.size() == 64);
    assert(result.wgsl.find("@fragment") != result.wgsl.npos);
    assert(result.wgsl.find("fn main(") != result.wgsl.npos);
}

void testCompilers(const fs::path& toolPackage, const fs::path& fixtureDirectory) {
    assert(setenv("VOSC_DRAFT_SHADER_TOOLS", "relative", 1) == 0);
    assert(GlslTranslationTools::fromEnvironment().directory.empty());
    assert(setenv("VOSC_DRAFT_SHADER_TOOLS", (toolPackage / "missing").c_str(), 1) == 0);
    assert(GlslTranslationTools::fromEnvironment().directory.empty());
    assert(setenv("VOSC_DRAFT_SHADER_TOOLS", toolPackage.c_str(), 1) == 0);
    assert(GlslTranslationTools::fromEnvironment().directory == toolPackage);

    const auto result = run(toolPackage, Shader);
    requireSuccess(result);
    assert(result.controls.size() == 4);
    assert(result.wgsl.find("@group(0u) @binding(0u)") != result.wgsl.npos);
    assert(result.wgsl.find("@group(0u) @binding(1u)") != result.wgsl.npos);
    const auto repeated = run(toolPackage, Shader);
    requireSuccess(repeated);
    assert(result.artifactDigest == repeated.artifactDigest && result.compilerIdentity == repeated.compilerIdentity);

    const auto error = run(toolPackage, Prefix + "uniform float value;\nvoid main() {\n    color = undefinedName;\n}\n");
    assert(error.status == JobStatus::Error && !error.diagnostics.empty() && error.diagnostics[0].line == 5);
    assert(error.diagnostics[0].message.find("undefinedName") != std::string::npos);
    assert(run(toolPackage / "missing", Prefix + Draw).status == JobStatus::Unavailable);
    assert(run(toolPackage, Prefix + Draw, Clock::now()).status == JobStatus::TimedOut);

    for (const auto& item : fs::directory_iterator(fixtureDirectory)) {
        if (item.path().extension() != ".frag") continue;
        requireSuccess(run(toolPackage, read(item)));
        std::cout << "Translated " << item.path().filename() << '\n';
    }
}

int fakeCompiler(const fs::path& program) {
    const auto directory = program.parent_path();
    if (directory.filename() == "large") {
        std::cout << std::string(20 * 1024, 'x');
        return 0;
    }
    std::ofstream started(directory / "child");
    started << getpid() << '\n';
    started.close();
    std::this_thread::sleep_for(6s);
    return 0;
}

void testChild(const fs::path& scratch) {
    GlslTranslationJob job;
    const auto slow = scratch / "slow";
    const auto marker = slow / "child";
    assert(job.start({slow}, Prefix + Draw, Clock::now() + 3s));
    const auto wait = Clock::now() + 2s;
    while ((!fs::exists(marker) || !fs::file_size(marker)) && Clock::now() < wait) std::this_thread::sleep_for(2ms);
    assert(fs::exists(marker));
    const auto pid = std::stoi(read(marker));
    assert(kill(pid, 0) == 0);
    job.cancel();
    assert(!job.start({slow}, Prefix + Draw, Clock::now() + 3s));
    assert(collect(job).status == JobStatus::Canceled);
    assert(kill(pid, 0) == -1 && errno == ESRCH);
    const auto timeout = run(slow, Prefix + Draw, Clock::now() + 300ms);
    assert(timeout.status == JobStatus::TimedOut);
    assert(kill(std::stoi(read(marker)), 0) == -1 && errno == ESRCH);
    const auto large = run(scratch / "large", Prefix + Draw);
    assert(large.status == JobStatus::Error);
    assert(large.diagnostics[0].message.find("size limit") != std::string::npos);
    assert(run(scratch / "mismatch", Prefix + Draw).status == JobStatus::Unavailable);
}

} // namespace

int main(int argc, char** argv) {
    const fs::path program = argv[0];
    if (program.filename() == "glslang") return fakeCompiler(program);
    assert(argc == 4);
    testProfile();
    testCompilers(argv[1], argv[2]);
    testChild(argv[3]);
    std::cout << "GLSL profile and compiler worker checks passed\n";
}
