#include "SceneFile.hpp"
#include "FreeImage.h"
#include "ofImage.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <set>
#include <stdexcept>

namespace scene {
namespace {
namespace fs = std::filesystem;
constexpr size_t KiB = 1024;
constexpr size_t MiB = 1024 * KiB;

std::string readBounded(const fs::path& path, size_t limit) {
    const auto size = fs::file_size(path);
    if (size == 0 || size > limit)
        throw std::runtime_error("file size is outside 1.." + std::to_string(limit) + " bytes: " + path.string());
    std::ifstream stream(path, std::ios::binary);
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!stream.read(bytes.data(), bytes.size()) || stream.peek() != EOF)
        throw std::runtime_error("cannot read file or file changed: " + path.string());
    return bytes;
}

bool validString(const std::string& value) {
    return value.size() <= 2048 && value.find('\0') == std::string::npos;
}

fs::path resolveAsset(const fs::path& root, const std::string& name) {
    const fs::path relative(name);
    if (name.empty() || relative.has_root_path() || name.find(':') != std::string::npos ||
        name.find('\\') != std::string::npos || std::find(relative.begin(), relative.end(), "..") != relative.end())
        throw std::runtime_error("expected a scene-relative asset path: " + name);
    const auto resolved = fs::canonical(root / relative);
    const auto relation = resolved.lexically_relative(root);
    if (relation.empty() || *relation.begin() == ".." || !fs::is_regular_file(resolved))
        throw std::runtime_error("asset must be a regular file inside the scene folder: " + name);
    return resolved;
}

void preflightImage(const fs::path& path, std::string& bytes, size_t& decodedBytes) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return std::tolower(ch); });
    const auto expected = extension == ".png" ? FIF_PNG :
        (extension == ".jpg" || extension == ".jpeg" ? FIF_JPEG : FIF_UNKNOWN);
    if (expected == FIF_UNKNOWN) throw std::runtime_error("expected a PNG or JPEG image");
    // The CPU preflight runs before openFrameworks first initializes its image loader.
    struct DecoderSession {
        DecoderSession() { FreeImage_Initialise(); }
        ~DecoderSession() { FreeImage_DeInitialise(); }
    } decoder;
    std::unique_ptr<FIMEMORY, decltype(&FreeImage_CloseMemory)> memory(
        FreeImage_OpenMemory(reinterpret_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())),
        FreeImage_CloseMemory);
    if (!FreeImage_FIFSupportsNoPixels(expected) || !memory || FreeImage_GetFileTypeFromMemory(memory.get(), 0) != expected ||
        !FreeImage_SeekMemory(memory.get(), 0, SEEK_SET))
        throw std::runtime_error("invalid image header");
    std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> header(
        FreeImage_LoadFromMemory(expected, memory.get(), FIF_LOAD_NOPIXELS), FreeImage_Unload);
    if (!header) throw std::runtime_error("invalid image header");
    const size_t width = FreeImage_GetWidth(header.get());
    const size_t height = FreeImage_GetHeight(header.get());
    if (width == 0 || height == 0 || width > MaxImageSide || height > MaxImageSide)
        throw std::runtime_error("image exceeds the " + std::to_string(MaxImageSide) + "-pixel per-side limit");
    if (width * height > MaxImagePixels)
        throw std::runtime_error("image exceeds the 10000000-pixel limit");
    // The decoder can allocate higher-depth rows before ofLoadImage converts them.
    const size_t decodedRowBytes = FreeImage_GetPitch(header.get());
    const size_t imageBytes = std::max(width * 4, decodedRowBytes) * height;
    if (imageBytes > MaxDecodedImageBytes - decodedBytes)
        throw std::runtime_error("scene exceeds the 64 MiB decoded image budget");
    ofPixels pixels;
    if (!ofLoadImage(pixels, ofBuffer(bytes.data(), bytes.size())) ||
        pixels.getWidth() != width || pixels.getHeight() != height ||
        pixels.size() > imageBytes)
        throw std::runtime_error("image could not be decoded within bounds");
    decodedBytes += imageBytes;
}

void preflightFragment(const fs::path& path, const std::string& bytes) {
    if (path.extension() != ".frag" || bytes.find('\0') != std::string::npos ||
        bytes.find_first_not_of(" \n\r\t") == std::string::npos)
        throw std::runtime_error("expected a nonempty standalone .frag file");
    (void)ofJson(bytes).dump(); // JSON serialization rejects malformed UTF-8.
    std::istringstream source(bytes);
    for (std::string line; std::getline(source, line);) {
        const auto start = line.find_first_not_of(" \t\r\v\f");
        if (start == std::string::npos || line[start] != '#') continue;
        std::istringstream directive(line.substr(start + 1));
        std::string token;
        directive >> token;
        if (token == "pragma") directive >> token;
        if (token == "include") throw std::runtime_error("shader includes are outside scene v1");
    }
    auto sibling = path;
    if (fs::exists(sibling.replace_extension(".vert")) || fs::exists(sibling.replace_extension(".geom")))
        throw std::runtime_error("custom shader stages are outside scene v1");
}

void checkJsonNumbers(const std::string& bytes) {
    // The JSON parser reports floats as doubles. Check numeric lexemes so that
    // a nonzero number below double range cannot arrive as an indistinguishable zero.
    bool inString = false;
    for (size_t i = 0; i < bytes.size(); ++i) {
        const char ch = bytes[i];
        if (ch == '"') {
            inString = !inString;
        } else if (inString && ch == '\\') {
            ++i;
        } else if (!inString && (ch == '-' || (ch >= '0' && ch <= '9'))) {
            const auto end = bytes.find_first_not_of("0123456789.eE+-", i);
            const auto token = bytes.substr(i, end - i);
            if (token.find_first_of(".eE") != std::string::npos) {
                const auto significand = token.substr(0, token.find_first_of("eE"));
                if (significand.find_first_of("123456789") != std::string::npos &&
                    std::strtod(token.c_str(), nullptr) == 0.0)
                    throw std::runtime_error("nonzero JSON number underflows float32");
            }
            if (end == std::string::npos) break;
            i = end - 1;
        }
    }
}

bool objectShape(const ofJson& value, std::initializer_list<const char*> keys) {
    if (!value.is_object() || value.size() != keys.size()) return false;
    return std::all_of(keys.begin(), keys.end(), [&](auto key) { return value.count(key) == 1; });
}

int assetIndex(const std::string& address) {
    if (address == "/shader" || address == "/tex") return 1;
    if (address == "/shader/texture") return 2;
    return -1;
}

int windowDimension(const ofJson& value) {
    if (!value.is_number_integer() || value < 64 || value > 2048)
        throw std::runtime_error("scene size must use integer dimensions in 64..2048");
    return value.get<int>();
}

} // namespace

std::string commandContext(size_t index, const ofxOscMessage& message) {
    return "command " + std::to_string(index + 1) + " " + message.getArgAsString(2);
}

bool appendOscValue(ofxOscMessage& message, const std::string& type, const ofJson& value) {
    if (type == "i" && value.is_number_integer()) {
        if (value.is_number_unsigned()) {
            if (value.get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) return false;
        } else if (value.get<int64_t>() < std::numeric_limits<int32_t>::min() ||
                   value.get<int64_t>() > std::numeric_limits<int32_t>::max()) return false;
        message.addInt32Arg(value.get<int32_t>());
    } else if (type == "f" && value.is_number_float()) {
        const auto number = value.get<double>();
        if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) return false;
        const auto rounded = static_cast<float>(number);
        if (number != 0.0 && rounded == 0.0f) return false;
        message.addFloatArg(rounded);
    } else if (type == "s" && value.is_string() && validString(value.get_ref<const std::string&>())) {
        message.addStringArg(value.get_ref<const std::string&>());
    } else return false;
    return true;
}

File load(const fs::path& path) {
    File result;
    result.path = fs::canonical(path);
    if (!fs::is_regular_file(result.path)) throw std::runtime_error("scene must be a regular file");
    const auto bytes = readBounded(result.path, MaxManifestBytes);
    if (bytes.find('\0') != std::string::npos) throw std::runtime_error("scene file contains NUL");
    checkJsonNumbers(bytes);
    std::vector<std::set<std::string>> objectKeys;
    const auto document = ofJson::parse(bytes, [&](int depth, ofJson::parse_event_t event, ofJson& item) {
        if (depth > 8) throw std::runtime_error("JSON nesting exceeds scene v1 bounds");
        if (event == ofJson::parse_event_t::object_start) objectKeys.emplace_back();
        if (event == ofJson::parse_event_t::object_end) objectKeys.pop_back();
        if (event == ofJson::parse_event_t::key && !objectKeys.back().insert(item.get<std::string>()).second)
            throw std::runtime_error("duplicate JSON key: " + item.get<std::string>());
        return true;
    });
    if (!objectShape(document, {"version", "title", "size", "commands"}) &&
        !objectShape(document, {"version", "title", "size", "commands", "onset_gate"}))
        throw std::runtime_error("expected version, title, size, commands and optional onset_gate");
    if (!document["version"].is_number_integer() || document["version"] != 1)
        throw std::runtime_error("unsupported scene version (expected 1)");
    if (!document["title"].is_string()) throw std::runtime_error("title must be a string");
    result.title = document["title"].get<std::string>();
    if (!validString(result.title) || result.title.find_first_not_of(" \t\r\n") == std::string::npos ||
        result.title.find_first_of("\r\n") != std::string::npos)
        throw std::runtime_error("title must be a nonempty single-line string");
    const auto& size = document["size"];
    if (!size.is_array() || size.size() != 2) throw std::runtime_error("size must contain width and height");
    result.width = windowDimension(size[0]);
    result.height = windowDimension(size[1]);
    if (document.count("onset_gate")) {
        const auto& gate = document["onset_gate"];
        if (!gate.is_number_integer() || gate < -1 || gate > 1)
            throw std::runtime_error("onset_gate must be an integer in -1..1");
        result.onsetGate = gate.get<int>();
    }
    const auto& commands = document["commands"];
    if (!commands.is_array() || commands.empty() || commands.size() > 2048)
        throw std::runtime_error("commands must contain 1..2048 entries");

    std::set<fs::path> assets;
    size_t shaderBytes = 0, imageBytes = 0, decodedImageBytes = 0;
    for (const auto& entry : commands) {
        const std::string prefix = "command " + std::to_string(result.commands.size() + 1);
        if (!objectShape(entry, {"address", "args"}) || !entry["address"].is_string() ||
            !entry["args"].is_array())
            throw std::runtime_error(prefix + ": expected address and args");
        const auto address = entry["address"].get<std::string>();
        try {
            const auto& args = entry["args"];
            if (args.size() > 6) throw std::runtime_error("too many arguments");
            Command command;
            command.message.setAddress("/scene/command");
            command.message.addStringArg("");
            command.message.addInt32Arg(static_cast<int32_t>(result.commands.size() + 1));
            command.message.addStringArg(address);
            const int pathIndex = assetIndex(address);
            for (size_t index = 0; index < args.size(); ++index) {
                const auto& arg = args[index];
                if (!objectShape(arg, {"type", "value"}) || !arg["type"].is_string())
                    throw std::runtime_error("argument " + std::to_string(index + 1) + " needs type and value");
                const auto type = arg["type"].get<std::string>();
                const auto& value = arg["value"];
                if (static_cast<int>(index) == pathIndex) {
                    if (type != "asset" || !value.is_string() || !validString(value.get<std::string>()))
                        throw std::runtime_error("expected a scene-relative asset argument");
                    command.asset = resolveAsset(result.path.parent_path(), value.get<std::string>());
                    command.feedback = address == "/tex" && command.asset.extension() == ".frag";
                    const auto source = (command.feedback ? "shader_pp:" : "") + value.get<std::string>();
                    assets.insert(command.asset);
                    if (assets.size() > 64) throw std::runtime_error("scene exceeds 64 distinct assets");
                    try {
                        const bool fragment = address == "/shader" || command.feedback;
                        auto& total = fragment ? shaderBytes : imageBytes;
                        const auto remaining = (fragment ? MiB : 64 * MiB) - total;
                        command.assetBytes = readBounded(command.asset,
                            std::min(remaining, fragment ? 128 * KiB : 16 * MiB));
                        total += command.assetBytes.size();
                        if (fragment) preflightFragment(command.asset, command.assetBytes);
                        else preflightImage(command.asset, command.assetBytes, decodedImageBytes);
                    } catch (const std::exception& error) {
                        throw std::runtime_error(command.asset.string() + ": " + error.what());
                    }
                    command.message.addStringArg(source);
                } else if (!appendOscValue(command.message, type, value)) {
                    throw std::runtime_error("invalid type or value for argument " + std::to_string(index + 1));
                }
            }
            result.commands.push_back(std::move(command));
        } catch (const std::exception& error) {
            throw std::runtime_error(prefix + " " + address + ": " + error.what());
        }
    }
    return result;
}

bool assetMatches(const Command& command) {
    try {
        return fs::canonical(command.asset) == command.asset &&
            fs::is_regular_file(command.asset) &&
            readBounded(command.asset, command.assetBytes.size()) == command.assetBytes;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace scene
