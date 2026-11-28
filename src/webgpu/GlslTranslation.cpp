#if defined(VOSC_WEBGPU_M1)

#include "GlslTranslation.hpp"

#include <nlohmann/json.hpp>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/evp.h>
#endif

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <locale>
#include <set>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace webgpu {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using ProfileStatus = GlslProfileResult::Status;
using JobStatus = GlslTranslationResult::Status;
using Json = nlohmann::json;

constexpr std::size_t MaxShaderBytes = 128 * 1024;
constexpr std::size_t MaxLogBytes = 16 * 1024;
constexpr std::size_t MaxControls = 128;
constexpr std::size_t MaxDiagnostics = 16;
constexpr auto GlslangVersion = "15.1.0";
constexpr auto TintVersion = "e4775a0d00343d02d32daecd45e1eb683224214b";

bool digit(char c) { return c >= '0' && c <= '9'; }
bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
bool newline(std::string_view source, std::size_t pos) {
    return source[pos] == '\n' || (source[pos] == '\r' && (pos + 1 == source.size() || source[pos + 1] != '\n'));
}

struct Token {
    std::string_view text;
    std::size_t begin;
    std::size_t end;
    std::size_t line;
};

GlslProfileResult profileFailure(ProfileStatus status, std::string message, std::size_t line = 0) {
    GlslProfileResult result;
    result.status = status;
    result.diagnostics.push_back({std::move(message), line});
    return result;
}

bool tokenize(std::string_view source, std::vector<Token>& tokens, std::size_t& line) {
    line = 1;
    for (std::size_t pos = 0; pos < source.size();) {
        const char c = source[pos];
        if (space(c)) {
            if (newline(source, pos)) ++line;
            ++pos;
            continue;
        }
        if (source.substr(pos, 2) == "//") {
            auto end = source.find_first_of("\r\n", pos + 2);
            pos = end == source.npos ? source.size() : end;
            continue;
        }
        if (source.substr(pos, 2) == "/*") {
            auto close = source.find("*/", pos + 2);
            if (close == source.npos) return false;
            for (; pos < close + 2; ++pos) {
                if (newline(source, pos)) ++line;
            }
            continue;
        }
        const auto begin = pos++;
        if (letter(c)) {
            while (pos < source.size() && (letter(source[pos]) || digit(source[pos]))) ++pos;
        } else if (digit(c) || (c == '.' && pos < source.size() && digit(source[pos]))) {
            const bool hex = source.substr(begin, 2) == "0x" || source.substr(begin, 2) == "0X";
            while (pos < source.size()) {
                const char next = source[pos];
                if ((next == '+' || next == '-') && !hex && (source[pos - 1] == 'e' || source[pos - 1] == 'E')) {
                    ++pos;
                } else if (letter(next) || digit(next) || next == '.') {
                    ++pos;
                } else {
                    break;
                }
            }
        }
        tokens.push_back({source.substr(begin, pos - begin), begin, pos, line});
    }
    return true;
}

std::size_t arity(std::string_view type) {
    if (type == "float") return 1;
    if (type == "vec2") return 2;
    if (type == "vec3") return 3;
    if (type == "vec4") return 4;
    return 0;
}

bool rendererUniform(std::string_view name) {
    // Keep these renderer-owned names in sync with Shader::isRendererUniform.
    constexpr std::string_view names[] = {"u_vosc_previous", "u_vosc_current", "random", "numLights", "lights",
        "offset", "layer", "globalColor", "usingTexture", "usingColors", "projectionMatrix", "modelViewMatrix",
        "modelViewProjectionMatrix", "textureMatrix"};
    return std::find(std::begin(names), std::end(names), name) != std::end(names);
}

bool opaqueType(std::string_view type) {
    if (type.starts_with("isampler") || type.starts_with("usampler")) type.remove_prefix(1);
    if (!type.starts_with("sampler")) return false;
    type.remove_prefix(7);
    constexpr std::string_view shapes[] = {"1D", "2D", "3D", "Cube", "2DRect", "1DArray", "2DArray",
        "Buffer", "2DMS", "2DMSArray", "1DShadow", "2DShadow", "CubeShadow", "2DRectShadow",
        "1DArrayShadow", "2DArrayShadow"};
    return std::find(std::begin(shapes), std::end(shapes), type) != std::end(shapes);
}

bool otherUniformType(std::string_view type) {
    constexpr std::string_view types[] = {"int", "uint", "bool", "bvec2", "bvec3", "bvec4", "ivec2",
        "ivec3", "ivec4", "uvec2", "uvec3", "uvec4", "mat2", "mat3", "mat4", "mat2x2", "mat2x3",
        "mat2x4", "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4"};
    return std::find(std::begin(types), std::end(types), type) != std::end(types);
}

bool numericLiteral(std::string_view value, float& result) {
    if (value.empty() || !(digit(value.front()) || value.front() == '.')) return false;
    const bool hex = value.starts_with("0x") || value.starts_with("0X");
    if (hex || value.find_first_of(".eEfF") == value.npos) {
        if (value.back() == 'u' || value.back() == 'U') value.remove_suffix(1);
        const int base = hex ? 16 : (value.size() > 1 && value.front() == '0' ? 8 : 10);
        if (hex) value.remove_prefix(2);
        unsigned int integer = 0;
        const auto read = std::from_chars(value.data(), value.data() + value.size(), integer, base);
        if (read.ec != std::errc{} || read.ptr != value.data() + value.size()) return false;
        result = static_cast<float>(integer);
        return true;
    }
    if (value.back() == 'f' || value.back() == 'F') value.remove_suffix(1);
    std::istringstream input{std::string(value)};
    input.imbue(std::locale::classic());
    return (input >> result) && input.eof() && std::isfinite(result);
}

struct Edit {
    std::size_t begin;
    std::size_t end;
    std::string_view replacement;
};

class Profile {
public:
    Profile(std::string_view source, const std::vector<Token>& tokens) : source(source), tokens(tokens) {}
    GlslProfileResult run();

private:
    std::string_view at(std::size_t index) const { return index < tokens.size() ? tokens[index].text : ""; }
    std::optional<GlslProfileResult> uniform(std::size_t& index);
    std::optional<GlslProfileResult> initializer(std::size_t& index, std::string_view type, GlslControl& field);
    std::string_view source;
    const std::vector<Token>& tokens;
    std::vector<Edit> edits;
    std::vector<GlslControl> controls;
    std::set<std::string_view> uniformNames;
    std::set<std::string_view> structNames;
    std::set<std::string_view> identifiers;
    bool hasTime = false;
    bool hasResolution = false;
};

std::optional<GlslProfileResult> Profile::initializer(std::size_t& index, std::string_view type, GlslControl& field) {
    const auto line = tokens[index - 1].line;
    auto literal = [&](float& value) {
        const auto sign = at(index);
        if (sign == "+" || sign == "-") ++index;
        if (!numericLiteral(at(index), value)) return false;
        if (sign == "-") value = -value;
        ++index;
        return true;
    };
    auto nonLiteral = [&] {
        const auto token = at(index);
        if (token.empty() || token == ";" || token == ")" || token == "," || token == "void" ||
            token == "uniform" || token == "out" || token == "in" || digit(token.front()) || token.front() == '.')
            return profileFailure(ProfileStatus::Error, "Expected a GLSL uniform initializer value", line);
        return profileFailure(ProfileStatus::Unsupported, "Uniform defaults must be finite numeric literals", line);
    };
    if (field.arity == 1) {
        if (!literal(field.defaultValue[0])) return nonLiteral();
    } else {
        if (at(index) != type || at(index + 1) != "(") return nonLiteral();
        index += 2;
        std::size_t components = 0;
        do {
            if (components == field.arity)
                return profileFailure(ProfileStatus::Error, "Too many components in the uniform default", line);
            if (!literal(field.defaultValue[components++])) return nonLiteral();
            if (at(index) != ",") break;
            ++index;
        } while (true);
        if (at(index) != ")") return nonLiteral();
        ++index;
        if (components == 1) {
            std::fill_n(field.defaultValue.begin(), field.arity, field.defaultValue[0]);
        } else if (components != field.arity) {
            return profileFailure(ProfileStatus::Error, "Wrong number of components in the uniform default", line);
        }
    }
    if (at(index) != "," && at(index) != ";") return nonLiteral();
    return std::nullopt;
}

std::optional<GlslProfileResult> Profile::uniform(std::size_t& index) {
    const auto begin = index++;
    const auto line = tokens[begin].line;
    const auto type = at(index++);
    if (type.empty() || !letter(type.front()))
        return profileFailure(ProfileStatus::Error, "Expected a uniform type", line);
    if (at(index) == "{")
        return profileFailure(ProfileStatus::Unsupported, "GLSL uniform blocks are outside the WebGPU profile", line);
    while (true) {
        const auto name = at(index++);
        if (name.empty() || !letter(name.front()))
            return profileFailure(ProfileStatus::Error, "Expected a uniform name", line);
        const auto next = at(index);
        if (next != "[" && next != "=" && next != "," && next != ";")
            return profileFailure(ProfileStatus::Error, "Expected a GLSL uniform declaration ending in ';'", line);
        if (!uniformNames.insert(name).second)
            return profileFailure(ProfileStatus::Error, "Duplicate uniform '" + std::string(name) + "'", line);
        const auto size = arity(type);
        const auto texture = opaqueType(type);
        if (size == 0 && !texture && !otherUniformType(type) && !structNames.count(type))
            return profileFailure(ProfileStatus::Error, "Unknown GLSL uniform type '" + std::string(type) + "'", line);
        if (next == "[")
            return profileFailure(ProfileStatus::Unsupported, "Array uniforms are outside the WebGPU profile", line);
        if (texture)
            return profileFailure(ProfileStatus::Unsupported, "Texture and feedback bindings are outside the WebGPU profile", line);
        if (name == "time" || name == "resolution") {
            if (next == "=")
                return profileFailure(ProfileStatus::Unsupported, "Renderer uniforms cannot declare defaults in the WebGPU profile", line);
            if (name == "time" && type == "float") hasTime = true;
            else if (name == "resolution" && type == "vec2") hasResolution = true;
            else return profileFailure(ProfileStatus::Unsupported, "Renderer uniforms require float time and vec2 resolution", line);
        } else {
            if (rendererUniform(name))
                return profileFailure(ProfileStatus::Unsupported, "Renderer uniform '" + std::string(name) + "' is outside the WebGPU profile", line);
            if (size == 0)
                return profileFailure(ProfileStatus::Unsupported, "Uniform type '" + std::string(type) + "' is outside the WebGPU profile", line);
            if (controls.size() == MaxControls)
                return profileFailure(ProfileStatus::Unsupported, "The WebGPU profile supports at most 128 control uniforms", line);
            GlslControl field{std::string(name), size, {}, controls.size() * 16};
            if (next == "=") {
                ++index;
                if (auto failure = initializer(index, type, field)) return failure;
            }
            controls.push_back(std::move(field));
        }
        if (at(index++) == ";") break;
    }
    edits.push_back({tokens[begin].begin, tokens[index - 1].end, {}});
    --index;
    return std::nullopt;
}

GlslProfileResult Profile::run() {
    bool version = false;
    bool main = false;
    bool output = false;
    bool explicitLocation = false;
    int braces = 0;
    int parens = 0;
    std::string coord;
    for (const auto& token : tokens) {
        if (letter(token.text.front())) identifiers.insert(token.text);
    }
    std::string prefix;
    for (int suffix = 0;; ++suffix) {
        prefix = "voscDraft" + std::to_string(suffix);
        const auto found = identifiers.lower_bound(prefix);
        if (found == identifiers.end() || !found->starts_with(prefix)) break;
    }
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const auto& token = tokens[index];
        const auto kind = token.text;
        const auto unsupported = [&](std::string message) {
            return profileFailure(ProfileStatus::Unsupported, std::move(message), token.line);
        };
        const auto invalid = [&](std::string message) {
            return profileFailure(ProfileStatus::Error, std::move(message), token.line);
        };
        if (kind == "#") {
            auto end = index + 1;
            while (end < tokens.size() && tokens[end].line == token.line) ++end;
            const auto directive = at(index + 1);
            if (directive != "version") {
                constexpr std::string_view directives[] = {"define", "undef", "if", "ifdef", "ifndef",
                    "else", "elif", "endif", "error", "pragma", "extension", "line", "include"};
                if (end == index + 1 ||
                    std::find(std::begin(directives), std::end(directives), directive) != std::end(directives))
                    return unsupported("Additional GLSL preprocessor directives are outside the WebGPU profile");
                return invalid("Unknown GLSL preprocessor directive");
            }
            if (version || index != 0) return invalid("#version must be the first GLSL directive");
            if (end < index + 3) return invalid("Expected a GLSL version number");
            const auto versionNumber = at(index + 2);
            if (!std::all_of(versionNumber.begin(), versionNumber.end(), digit))
                return invalid("Expected a GLSL version number");
            if (at(index + 2) != "330" || (end == index + 4 && at(index + 3) != "core"))
                return unsupported("The WebGPU profile requires desktop GLSL 330");
            if (end > index + 4) return invalid("Unexpected tokens after #version 330");
            edits.push_back({token.begin, tokens[end - 1].end, {}});
            version = true;
            index = end - 1;
            continue;
        }
        if (braces == 0 && parens == 0) {
            if (kind == "struct" && !at(index + 1).empty() && at(index + 2) == "{")
                structNames.insert(at(index + 1));
            if (kind == "layout") {
                if (at(index + 1) != "(") return invalid("Expected '(' after a GLSL layout qualifier");
                explicitLocation = at(index + 2) == "location" && at(index + 3) == "=" &&
                    at(index + 4) == "0" && at(index + 5) == ")" && at(index + 6) == "out";
                if (!explicitLocation) return unsupported("Only fragment output location 0 is supported by the WebGPU profile");
                index += 5;
                continue;
            }
            if (kind == "uniform") {
                if (auto failure = uniform(index)) return std::move(*failure);
                continue;
            }
            if (kind == "out") {
                if (at(index + 1).empty() || at(index + 2).empty() || !letter(at(index + 2).front()))
                    return invalid("Expected a GLSL fragment output declaration");
                const auto ending = at(index + 3);
                if (output || at(index + 1) != "vec4" || ending == "," || ending == "[")
                    return unsupported("The WebGPU profile supports one vec4 fragment output");
                if (ending != ";") return invalid("Expected a GLSL fragment output declaration ending in ';'");
                if (!explicitLocation) edits.push_back({token.begin, token.begin, "layout(location = 0) "});
                output = true;
                index += 3;
                continue;
            }
            if (kind == "in" || kind == "attribute" || kind == "varying")
                return unsupported("Vertex inputs and varyings are outside the WebGPU fragment profile");
            if (kind == "void" && at(index + 1) == "main" && at(index + 2) == "(") {
                const auto end = index + (at(index + 3) == "void" ? 4 : 3);
                if (at(end) != ")" || (at(end + 1) != "{" && at(end + 1) != ";"))
                    return invalid("Expected a void main() fragment entry point");
                if (at(end + 1) == "{") {
                    if (main) return invalid("Duplicate GLSL fragment entry point");
                    main = true;
                }
            }
        }
        if (kind == "gl_FragCoord") {
            if (at(index + 1) == "." && at(index + 2).empty()) return invalid("Expected a gl_FragCoord component");
            if (at(index + 1) != "." || (at(index + 2) != "xy" && at(index + 2) != "x" && at(index + 2) != "y"))
                return unsupported("Only gl_FragCoord.xy, .x and .y are supported by the WebGPU profile");
            if (at(index + 2) != "x") {
                if (coord.empty()) coord = prefix + "FragCoord()";
                edits.push_back({token.begin, token.end, coord});
            }
        } else if (kind == "gl_Position" || kind == "gl_FragColor" || kind == "gl_FragData" || kind == "gl_FragDepth" ||
                   kind == "gl_PointCoord" || kind == "gl_VertexID" || kind == "gl_InstanceID") {
            return unsupported(std::string(kind) + " is outside the WebGPU fragment profile");
        }
        if (kind == "{") ++braces;
        if (kind == "}" && --braces < 0) return invalid("Unmatched '}' in fragment shader");
        if (kind == "(") ++parens;
        if (kind == ")" && --parens < 0) return invalid("Unmatched ')' in fragment shader");
    }
    if (braces || parens)
        return profileFailure(ProfileStatus::Error, "Unclosed delimiter in fragment shader", tokens.back().line);
    if (!version) return profileFailure(ProfileStatus::Error, "Expected #version 330 at the start of the fragment shader", 1);
    if (!main) return profileFailure(ProfileStatus::Error, "Missing void main() fragment entry point");
    if (!output) return profileFailure(ProfileStatus::Error, "Missing out vec4 fragment output");

    const auto resolution = hasResolution ? "resolution" : prefix + "Resolution";
    const auto time = hasTime ? "time" : prefix + "Time";
    std::string adapted = "#version 450\nlayout(std140, set = 0, binding = 0) uniform " + prefix + "Frame {\n" +
        "    vec2 " + resolution + ";\n    float " + time + ";\n    float " + prefix + "Padding;\n};\n";
    if (!controls.empty()) {
        constexpr std::string_view types[] = {"float", "vec2", "vec3", "vec4"};
        adapted += "layout(std140, set = 0, binding = 1) uniform " + prefix + "Controls {\n";
        for (const auto& field : controls) {
            adapted += "    layout(offset = " + std::to_string(field.offset) + ") ";
            adapted += types[field.arity - 1];
            adapted += " " + field.name + ";\n";
        }
        adapted += "};\n";
    }
    if (!coord.empty()) adapted += "vec2 " + coord + " { return vec2(gl_FragCoord.x, " + resolution + ".y - gl_FragCoord.y); }\n";
    adapted += "#line 1 0\n";
    std::size_t pos = 0;
    for (const auto& edit : edits) {
        adapted += source.substr(pos, edit.begin - pos);
        adapted += edit.replacement;
        if (edit.replacement.empty()) {
            for (const auto c : source.substr(edit.begin, edit.end - edit.begin)) adapted += c == '\n' || c == '\r' ? c : ' ';
        }
        pos = edit.end;
    }
    adapted += source.substr(pos);
    if (adapted.size() > MaxShaderBytes)
        return profileFailure(ProfileStatus::Error, "Adapted GLSL exceeds the 128 KiB shader limit");
    GlslProfileResult result;
    result.status = ProfileStatus::Ready;
    result.adaptedSource = std::move(adapted);
    result.controls = std::move(controls);
    return result;
}

struct Failure {
    JobStatus status;
    std::vector<GlslDiagnostic> diagnostics;
};

[[noreturn]] void fail(JobStatus status, std::string message) {
    throw Failure{status, {{std::move(message), 0}}};
}

class ScratchDirectory {
public:
    ScratchDirectory() {
        auto pattern = (fs::temp_directory_path() / "vosc-glsl-XXXXXX").string();
        if (!mkdtemp(pattern.data())) throw std::runtime_error("Could not create private shader compiler directory");
        path = std::move(pattern);
    }
    ~ScratchDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

std::string readBounded(const fs::path& path, std::size_t limit) {
    const auto length = fs::file_size(path);
    if (length > limit) throw std::runtime_error("Shader compiler file exceeded its size limit");
    std::ifstream input(path, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(length), '\0');
    if (!input.read(bytes.data(), bytes.size()) || input.peek() != EOF)
        throw std::runtime_error("Could not read shader compiler file");
    return bytes;
}

void writeSource(const fs::path& path, const std::string& source) {
    std::ofstream output(path, std::ios::binary);
    output.write(source.data(), source.size());
    output.close();
    if (!output) throw std::runtime_error("Could not write shader compiler input");
}

class Sha256 {
public:
    Sha256() {
#ifdef __APPLE__
        CC_SHA256_Init(&context);
#else
        if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
            throw std::runtime_error("Could not initialize SHA-256");
#endif
    }
    void add(std::string_view data) {
#ifdef __APPLE__
        CC_SHA256_Update(&context, data.data(), static_cast<CC_LONG>(data.size()));
#else
        if (EVP_DigestUpdate(context.get(), data.data(), data.size()) != 1)
            throw std::runtime_error("Could not update SHA-256");
#endif
    }
    std::string finish() {
        std::array<unsigned char, 32> bytes{};
#ifdef __APPLE__
        CC_SHA256_Final(bytes.data(), &context);
#else
        if (EVP_DigestFinal_ex(context.get(), bytes.data(), nullptr) != 1)
            throw std::runtime_error("Could not finalize SHA-256");
#endif
        constexpr char hex[] = "0123456789abcdef";
        std::string digest;
        for (const auto value : bytes) {
            digest += hex[value >> 4];
            digest += hex[value & 0xf];
        }
        return digest;
    }

private:
#ifdef __APPLE__
    CC_SHA256_CTX context;
#else
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
#endif
};

class Compiler {
public:
    Compiler(const std::atomic<bool>& canceled, Clock::time_point deadline) : canceled(canceled), deadline(deadline) {}
    GlslTranslationResult run(const GlslTranslationTools& tools, const std::string& source) const;

private:
    void checkCanceled() const;
    std::string toolDigest(const fs::path& tool) const;
    void verifyTools(const fs::path& directory) const;
    void invoke(std::vector<std::string> arguments, const fs::path& log, const fs::path& output,
                std::string_view stage, std::string_view sourceName = {}) const;
    const std::atomic<bool>& canceled;
    Clock::time_point deadline;
};

void Compiler::checkCanceled() const {
    if (canceled.load(std::memory_order_relaxed)) fail(JobStatus::Canceled, "Shader translation canceled");
    if (Clock::now() >= deadline) fail(JobStatus::TimedOut, "Shader translation exceeded the prepare deadline");
}

std::string Compiler::toolDigest(const fs::path& path) const {
    constexpr std::uintmax_t MaxToolBytes = 128 * 1024 * 1024;
    if (!fs::is_regular_file(fs::symlink_status(path)) || fs::file_size(path) > MaxToolBytes)
        throw std::runtime_error("Compiler executable is missing or invalid");
    std::ifstream file(path, std::ios::binary);
    std::array<char, 64 * 1024> bytes;
    Sha256 hash;
    while (file) {
        checkCanceled();
        file.read(bytes.data(), bytes.size());
        hash.add(std::string_view(bytes.data(), static_cast<std::size_t>(file.gcount())));
    }
    if (!file.eof() || file.bad()) throw std::runtime_error("Could not read compiler executable");
    return hash.finish();
}

void Compiler::verifyTools(const fs::path& directory) const {
    try {
        if (!directory.is_absolute()) throw std::runtime_error("Compiler package directory must be absolute");
        const auto manifest = Json::parse(readBounded(directory / "manifest.json", MaxLogBytes));
        if (manifest.at("format") != "vosc.webgpu-tools" || !manifest.at("version").is_number_integer() ||
            manifest.at("version") != 1)
            throw std::runtime_error("Unrecognized WebGPU compiler manifest");
        for (const auto& [file, version] : {std::pair{"glslang", GlslangVersion}, std::pair{"tint", TintVersion}}) {
            const auto& tool = manifest.at(file);
            if (tool.at("file") != file || tool.at("version") != version)
                throw std::runtime_error(std::string(file) + " has an unsupported version or filename");
            if (tool.at("sha256") != toolDigest(directory / file))
                throw std::runtime_error(std::string(file) + " executable does not match its package SHA-256");
        }
    } catch (const std::exception& error) {
        fail(JobStatus::Unavailable, "WebGPU shader compiler tools unavailable. " + std::string(error.what()));
    }
}

std::vector<GlslDiagnostic> compilerDiagnostics(std::string_view log, std::string_view stage, std::string_view sourceName) {
    std::vector<GlslDiagnostic> diagnostics;
    while (!log.empty() && diagnostics.size() < MaxDiagnostics) {
        auto line = log.substr(0, log.find('\n'));
        log.remove_prefix(std::min(line.size() + 1, log.size()));
        if (line.empty() || (!sourceName.empty() && !line.starts_with("ERROR:"))) continue;
        std::size_t sourceLine = 0;
        if (!sourceName.empty()) {
            line.remove_prefix(6);
            while (line.starts_with(' ')) line.remove_prefix(1);
            if (line.starts_with(sourceName)) line.remove_prefix(sourceName.size());
            else if (line.starts_with("0:")) line.remove_prefix(1);
            if (line.starts_with(':')) {
                line.remove_prefix(1);
                if (const auto end = line.find(':'); end != line.npos) {
                    const auto read = std::from_chars(line.data(), line.data() + end, sourceLine);
                    if (read.ec == std::errc{} && read.ptr == line.data() + end) line.remove_prefix(end + 1);
                    else sourceLine = 0;
                }
            }
        }
        while (line.starts_with(' ') || line.starts_with('\t')) line.remove_prefix(1);
        std::string message(line.substr(0, 512));
        for (auto& c : message) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < ' ' && c != '\t') c = ' ';
            if (byte >= 127) c = '?';
        }
        if (!message.empty()) diagnostics.push_back({std::string(stage) + ": " + message, sourceLine});
    }
    if (diagnostics.empty()) diagnostics.push_back({std::string(stage) + " failed", 0});
    return diagnostics;
}

class Child {
public:
    Child(std::vector<std::string>& arguments, const fs::path& log) {
        std::vector<char*> argv;
        for (auto& argument : arguments) argv.push_back(argument.data());
        argv.push_back(nullptr);
        char locale[] = "LC_ALL=C";
        char* env[] = {locale, nullptr};

        posix_spawn_file_actions_t actions;
        auto error = posix_spawn_file_actions_init(&actions);
        if (error) throw std::system_error(error, std::system_category(), "Could not set up shader compiler process");
        posix_spawnattr_t attr;
        error = posix_spawnattr_init(&attr);
        if (!error) {
            error = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
            if (!error) error = posix_spawnattr_setpgroup(&attr, 0);
            if (!error) error = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
            if (!error) error = posix_spawn_file_actions_addopen(&actions, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (!error) error = posix_spawn_file_actions_adddup2(&actions, 1, 2);
            if (!error) error = posix_spawn(&pid, arguments.front().c_str(), &actions, &attr, argv.data(), env);
            posix_spawnattr_destroy(&attr);
        }
        posix_spawn_file_actions_destroy(&actions);
        if (error)
            fail(JobStatus::Unavailable, "Could not start the packaged shader compiler. " + std::system_category().message(error));
    }
    ~Child() {
        if (pid <= 0) return;
        kill(-pid, SIGKILL);
        while (waitpid(pid, nullptr, 0) == -1 && errno == EINTR) {}
    }
    std::optional<int> poll() {
        int status = 0;
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result == 0 || (result < 0 && errno == EINTR)) return std::nullopt;
        pid = -1;
        if (result < 0) throw std::runtime_error("Could not reap shader compiler process");
        return status;
    }

private:
    pid_t pid = -1;
};

void checkFileSize(const fs::path& path, std::size_t limit, std::string_view kind) {
    std::error_code error;
    const auto length = fs::file_size(path, error);
    if (!error && length > limit) fail(JobStatus::Error, "Shader compiler " + std::string(kind) + " exceeded its size limit");
    if (error && error != std::errc::no_such_file_or_directory)
        throw std::system_error(error, "Could not inspect shader compiler output");
}

void Compiler::invoke(std::vector<std::string> arguments, const fs::path& log, const fs::path& output,
                      std::string_view stage, std::string_view sourceName) const {
    checkCanceled();
    Child child(arguments, log);
    for (;;) {
        checkCanceled();
        checkFileSize(log, MaxLogBytes, "diagnostics");
        if (!output.empty()) checkFileSize(output, MaxShaderBytes, "output");
        if (const auto status = child.poll()) {
            if (!output.empty()) checkFileSize(output, MaxShaderBytes, "output");
            checkFileSize(log, MaxLogBytes, "diagnostics");
            if (WIFEXITED(*status) && WEXITSTATUS(*status) == 0) return;
            if (WIFEXITED(*status) && (WEXITSTATUS(*status) == 126 || WEXITSTATUS(*status) == 127))
                fail(JobStatus::Unavailable, "Could not run the packaged shader compiler");
            throw Failure{JobStatus::Error, compilerDiagnostics(readBounded(log, MaxLogBytes), stage, sourceName)};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
}

GlslTranslationResult Compiler::run(const GlslTranslationTools& tools, const std::string& source) const {
    checkCanceled();
    auto profile = classifyGlsl(source);
    if (profile.status != ProfileStatus::Ready) throw Failure{JobStatus::Error, std::move(profile.diagnostics)};
    verifyTools(tools.directory);
    checkCanceled();
    ScratchDirectory scratch;
    const auto original = (scratch.path / "source.frag").string();
    const auto adapted = (scratch.path / "adapted.frag").string();
    const auto log = scratch.path / "compiler.log";
    const auto spirv = (scratch.path / "shader.spv").string();
    const auto wgsl = (scratch.path / "shader.wgsl").string();
    const auto glslang = (tools.directory / "glslang").string();
    const auto tint = (tools.directory / "tint").string();
    writeSource(original, source);
    writeSource(adapted, profile.adaptedSource);
    invoke({glslang, "-S", "frag", original}, log, {}, "GLSL", original);
    invoke({glslang, "-V", "-S", "frag", "-o", spirv, adapted}, log, spirv, "GLSL to SPIR-V", adapted);
    invoke({tint, "--format", "wgsl", "--validate", "true", "-o", wgsl, spirv}, log, wgsl, "SPIR-V to WGSL");
    checkCanceled();
    GlslTranslationResult result;
    result.wgsl = readBounded(wgsl, MaxShaderBytes);
    if (result.wgsl.empty() || result.wgsl.find('\0') != result.wgsl.npos)
        fail(JobStatus::Error, "The shader compiler produced invalid WGSL output");
    Sha256 hash;
    hash.add(result.wgsl);
    result.artifactDigest = hash.finish();
    Sha256 identity;
    identity.add(std::string("glslang/") + GlslangVersion + "+tint/" + TintVersion);
    result.compilerIdentity = identity.finish();
    result.controls = std::move(profile.controls);
    result.status = JobStatus::Success;
    return result;
}

} // namespace

GlslProfileResult classifyGlsl(std::string_view source) {
    if (source.size() > MaxShaderBytes) return profileFailure(ProfileStatus::Error, "GLSL exceeds the 128 KiB shader limit");
    if (source.find('\0') != source.npos) return profileFailure(ProfileStatus::Error, "Unexpected NUL in GLSL source");
    std::vector<Token> tokens;
    std::size_t line;
    if (!tokenize(source, tokens, line)) return profileFailure(ProfileStatus::Error, "Unterminated GLSL block comment", line);
    return Profile(source, tokens).run();
}

GlslTranslationTools GlslTranslationTools::fromEnvironment() {
    const auto* path = std::getenv("VOSC_DRAFT_SHADER_TOOLS");
    if (!path) return {};
    fs::path directory(path);
    std::error_code error;
    if (!directory.is_absolute() || !fs::is_directory(directory, error)) return {};
    return {std::move(directory)};
}

struct GlslTranslationJob::State {
    std::atomic<bool> canceled = false;
    std::atomic<bool> done = false;
    GlslTranslationResult result;
};

GlslTranslationJob::~GlslTranslationJob() {
    cancel();
    if (worker.joinable()) worker.join();
}

bool GlslTranslationJob::start(GlslTranslationTools tools, std::string source, Clock::time_point deadline) {
    if (state) return false;
    auto next = std::make_shared<State>();
    worker = std::thread([next, tools = std::move(tools), source = std::move(source), deadline] {
        try {
            next->result = Compiler(next->canceled, deadline).run(tools, source);
        } catch (Failure& failure) {
            next->result.status = failure.status;
            next->result.diagnostics = std::move(failure.diagnostics);
        } catch (const std::exception& error) {
            next->result.status = JobStatus::Error;
            next->result.diagnostics.push_back({error.what(), 0});
        }
        next->done.store(true, std::memory_order_release);
    });
    state = std::move(next);
    return true;
}

void GlslTranslationJob::cancel() {
    if (state) state->canceled.store(true, std::memory_order_relaxed);
}

std::optional<GlslTranslationResult> GlslTranslationJob::take() {
    if (!state || !state->done.load(std::memory_order_acquire)) return std::nullopt;
    worker.detach();
    auto finished = std::move(state);
    if (finished->canceled.load(std::memory_order_relaxed) && finished->result.status != JobStatus::Canceled) {
        GlslTranslationResult canceled;
        canceled.status = JobStatus::Canceled;
        canceled.diagnostics.push_back({"Shader translation canceled", 0});
        return canceled;
    }
    return std::move(finished->result);
}

} // namespace webgpu

#endif
