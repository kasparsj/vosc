#include "SceneFile.hpp"
#include "../Config.h"
#include "FreeImage.h"
#include "ofImage.h"
#include "ofXml.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <set>
#include <regex>
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

fs::path resolveAsset(const fs::path& root, const std::string& name, const char* kind) {
    const fs::path relative(name);
    if (name.empty() || relative.has_root_path() || name.find(':') != std::string::npos ||
        name.find('\\') != std::string::npos || std::find(relative.begin(), relative.end(), "..") != relative.end())
        throw std::runtime_error("expected a scene-relative asset path: " + name);
    const auto candidate = (root / relative).lexically_normal();
    std::error_code error;
    const auto resolved = fs::canonical(candidate, error);
    if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)
        throw std::runtime_error(std::string("Missing ") + kind + " '" + name + "' at " + candidate.string());
    if (error)
        throw std::runtime_error(std::string("Unable to resolve ") + kind + " '" + name + "' at " +
                                 candidate.string() + " (" + error.message() + ")");
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

bool identifier(const std::string& name) {
    static const std::regex pattern("[A-Za-z_][A-Za-z0-9_]{0,63}");
    return std::regex_match(name, pattern);
}

std::string stringValue(const ofJson& value) {
    if (!value.is_string() || !validString(value.get_ref<const std::string&>()) ||
        value.get_ref<const std::string&>().empty())
        throw std::runtime_error("expected bounded nonempty string");
    return value.get<std::string>();
}

int boundedInteger(const ofJson& value, int minimum, int maximum) {
    if (!value.is_number_integer() || value < minimum || value > maximum)
        throw std::runtime_error("expected integer in " + std::to_string(minimum) + ".." + std::to_string(maximum));
    return value.get<int>();
}

class NativeDecoder {
public:
    NativeDecoder(File& file, const ofJson& declarations, const Inputs& overrides)
        : file(file), root(file.path.parent_path()) {
        if (!declarations.is_object() || declarations.size() > 1)
            throw std::runtime_error("expected at most one media input declaration");
        for (const auto& item : overrides)
            if (!declarations.contains(item.first)) throw std::runtime_error("unknown scene input: " + item.first);
        for (auto it = declarations.begin(); it != declarations.end(); ++it) {
            try {
                if (!identifier(it.key())) throw std::runtime_error("invalid input name");
                const auto& declaration = it.value();
                const auto kind = stringValue(declaration.at("kind"));
                const auto override = overrides.find(it.key());
                std::string value;
                if (kind == "video" && objectShape(declaration, {"kind"})) {
                    if (override == overrides.end()) throw std::runtime_error("requires --scene-input " + it.key() + "=<MOV/MP4>");
                    const fs::path path = fs::canonical(override->second);
                    const auto extension = ofToLower(path.extension().string());
                    if (!fs::is_regular_file(path) || !std::ifstream(path, std::ios::binary) ||
                        path.string().find(':') != std::string::npos || (extension != ".mov" && extension != ".mp4"))
                        throw std::runtime_error("expected a readable local MOV or MP4");
                    value = path.string();
                } else if (kind == "camera" && objectShape(declaration, {"kind", "default"})) {
                    const int device = boundedInteger(declaration["default"], 0, 63);
                    value = override == overrides.end() ? std::to_string(device) : override->second;
                    if (!std::regex_match(value, std::regex("[0-9]{1,2}")) || std::stoi(value) > 63)
                        throw std::runtime_error("expected camera device index 0..63");
                    value = "webcam:" + value;
                } else if (kind == "html" && objectShape(declaration, {"kind", "default"})) {
                    const auto defaultUrl = checkedUrl(stringValue(declaration["default"]));
                    value = checkedUrl(override == overrides.end() ? defaultUrl : override->second);
#if !USE_OFX_ULTRALIGHT
                    throw std::runtime_error("HTML textures require a build with USE_OFX_ULTRALIGHT");
#endif
                } else {
                    throw std::runtime_error("invalid input declaration");
                }
                if (!validString(value)) throw std::runtime_error("invalid input value");
                external.emplace(it.key(), std::make_pair(kind, value));
                file.mediaInput = it.key();
            } catch (const std::exception& error) {
                throw std::runtime_error("scene input " + it.key() + ": " + error.what());
            }
        }
    }

    void decode(const ofJson& commands) {
        for (const auto& entry : commands) {
            try {
                decodeCommand(entry);
            } catch (const std::exception& error) {
                throw std::runtime_error("command " + std::to_string(file.nativeCommands.size() + 1) +
                    " " + (entry.is_object() && entry.contains("address") ? entry["address"].dump() : "") +
                    ": " + error.what());
            }
        }
        if (!external.empty() && !usedExternal) throw std::runtime_error("media input is declared but unused");
    }

private:
    File& file;
    fs::path root;
    std::map<std::string, std::pair<std::string, std::string>> external;
    std::map<std::string, std::string> textures, models, buffers;
    std::vector<bool> shaders, geometry;
    std::vector<std::string> layerTextures;
    std::set<fs::path> includeStack, checkedStages;
    std::map<fs::path, std::string> assetKinds;
    size_t decodedImageBytes = 0, encodedImages = 0, shaderBytes = 0;
    int layers = 0;
    bool usedExternal = false;

    static std::string checkedUrl(const std::string& url) {
        static const std::regex http(R"(https?://[^/?#\s@]+[^\s]*)");
        if (!validString(url) || !std::regex_match(url, http)) throw std::runtime_error("expected HTTP(S) URL");
        return url;
    }

    fs::path asset(const std::string& name, const std::string& kind) {
        const auto path = resolveAsset(root, name, kind.c_str());
        if (auto previous = assetKinds.find(path); previous != assetKinds.end()) {
            if (previous->second != kind) throw std::runtime_error("asset used with conflicting kinds: " + name);
            return path;
        }
        if (file.nativeAssets.size() >= 64) throw std::runtime_error("scene exceeds 64 distinct assets");
        const bool image = kind == "image";
        const bool model = kind == "model";
        auto bytes = readBounded(path, image ? 16 * MiB : model ? 4 * MiB : 128 * KiB);
        if (image) {
            encodedImages += bytes.size();
            if (encodedImages > 64 * MiB) throw std::runtime_error("encoded image budget exceeded");
            preflightImage(path, bytes, decodedImageBytes);
        } else {
            if (bytes.find('\0') != std::string::npos) throw std::runtime_error("asset contains NUL");
            if (!model) {
                shaderBytes += bytes.size();
                if (shaderBytes > MiB) throw std::runtime_error("shader byte budget exceeded");
            }
            (void)ofJson(bytes).dump();
        }
        assetKinds.emplace(path, kind);
        file.nativeAssets.emplace(path, std::move(bytes));
        return path;
    }

    fs::path dependency(const fs::path& parent, const std::string& name) const {
        const fs::path relative(name);
        if (relative.empty() || relative.has_root_path() || name.find(':') != std::string::npos ||
            name.find('\\') != std::string::npos)
            throw std::runtime_error("expected local dependency: " + name);
        const auto local = (parent.parent_path() / relative).lexically_normal().lexically_relative(root);
        if (local.empty() || *local.begin() == "..")
            throw std::runtime_error("dependency escapes scene folder: " + name);
        return local;
    }

    fs::path stage(const std::string& name, const std::string& extension, size_t depth = 0) {
        auto path = asset(name, "shader");
        if (path.extension() != extension || depth >= 8 || includeStack.count(path))
            throw std::runtime_error("invalid shader stage or include cycle: " + name);
        if (checkedStages.count(path)) return path;
        includeStack.insert(path);
        static const std::regex includes(R"(^\s*#\s*(?:pragma\s+)?include\b)");
        static const std::regex directive(R"re(^\s*#\s*(?:pragma\s+)?include\s+"([^"\s]+)"\s*$)re");
        std::istringstream stream(file.nativeAssets.at(path));
        for (std::string line; std::getline(stream, line);) {
            std::smatch match;
            if (!std::regex_search(line, includes)) continue;
            if (!std::regex_match(line, match, directive))
                throw std::runtime_error("invalid shader include: " + line);
            stage(dependency(path, match[1].str()).string(), ".glsl", depth + 1);
        }
        includeStack.erase(path);
        checkedStages.insert(path);
        return path;
    }

    std::string source(const std::string& address, const ofJson& value, NativeCommand& command) {
        const auto kind = stringValue(value.at("kind"));
        command.sourceKind = kind;
        if (address == "/tex") {
            if (objectShape(value, {"kind", "path"})) {
                const auto path = stringValue(value["path"]);
                if (kind == "image") return asset(path, kind).string();
                if (kind == "fragment" || kind == "feedback") {
                    auto fragment = stage(path, ".frag");
                    if (kind == "fragment") preflightFragment(fragment, file.nativeAssets.at(fragment));
                    command.stages.emplace("fragment", fragment);
                    return std::string(kind == "feedback" ? "shader_pp:" : "shader:") + fragment.string();
                }
            }
            if (kind == "sketch" && objectShape(value, {"kind", "name"})) {
                const auto name = stringValue(value["name"]);
                if (name == "Spiral" || name == "NoisySpirals" || name == "WaveClock") return name;
            }
            if (kind == "color" && objectShape(value, {"kind", "value"})) {
                const auto color = stringValue(value["value"]);
                if (std::regex_match(color, std::regex("0x[0-9a-fA-F]{6}"))) return color;
            }
            if (kind == "grid-plane" && objectShape(value, {"kind"})) return "2d:gridplane";
            if (objectShape(value, {"kind", "input"})) {
                auto input = external.find(stringValue(value["input"]));
                if (input != external.end() && input->second.first == kind && !usedExternal) {
                    usedExternal = true;
                    return input->second.second;
                }
            }
        }
        if (address == "/geom" && kind == "model" && objectShape(value, {"kind", "path"})) {
            auto path = asset(stringValue(value["path"]), kind);
            if (path.extension() != ".dae") throw std::runtime_error("expected DAE model");
            const auto& document = file.nativeAssets.at(path);
            ofXml xml;
            if (document.find("<!DOCTYPE") != std::string::npos || !xml.parse(document) ||
                !xml.getChild("COLLADA"))
                throw std::runtime_error("expected a self-contained COLLADA document");
            size_t visited = 0;
            std::function<void(const ofXml&, size_t)> inspect = [&](const ofXml& node, size_t depth) {
                if (++visited > 10000 || depth > 32) throw std::runtime_error("COLLADA tree exceeds bounds");
                for (const auto& attribute : node.getAttributes()) {
                    if ((attribute.getName() == "url" || attribute.getName() == "source") &&
                        attribute.getValue().rfind("#", 0) != 0)
                        throw std::runtime_error("external COLLADA reference");
                }
                if (node.getName() == "image") {
                    const auto reference = node.getChild("init_from").getValue();
                    if (reference.find_first_of("%&#?") != std::string::npos)
                        throw std::runtime_error("unsupported COLLADA image reference");
                    asset(dependency(path, reference).string(), "image");
                }
                for (const auto& child : node.getChildren()) inspect(child, depth + 1);
            };
            inspect(xml.getChild("COLLADA"), 0);
            return path.string();
        }
        if (address == "/shader" && kind == "stages" && objectShape(value, {"kind", "stages"})) {
            const auto& stages = value["stages"];
            if (!stages.is_object() || !stages.contains("fragment"))
                throw std::runtime_error("shader needs an explicit fragment stage");
            for (auto it = stages.begin(); it != stages.end(); ++it) {
                const auto extension = it.key() == "fragment" ? ".frag" : it.key() == "vertex" ? ".vert" :
                    it.key() == "geometry" ? ".geom" : nullptr;
                if (!extension) throw std::runtime_error("unknown shader stage: " + it.key());
                command.stages.emplace(it.key(), stage(stringValue(it.value()), extension));
            }
            return command.stages.at("fragment").string();
        }
        throw std::runtime_error("unsupported or invalid source descriptor");
    }

    static ofJson scalarVector(const ofJson& values) {
        if (!values.is_array() || values.empty() || values.size() > 4)
            throw std::runtime_error("expected vector with 1..4 typed components");
        auto result = ofJson::array();
        for (const auto& component : values) {
            ofxOscMessage probe;
            if (!objectShape(component, {"type", "value"}) ||
                !appendOscValue(probe, stringValue(component["type"]), component["value"]))
                throw std::runtime_error("invalid vector component");
            result.push_back(component["value"]);
        }
        return result;
    }

    void decodeCommand(const ofJson& entry);
};

void NativeDecoder::decodeCommand(const ofJson& entry) {
    if (!objectShape(entry, {"address", "args"}) || !entry["args"].is_array() || entry["args"].size() > 10)
        throw std::runtime_error("expected address and bounded args array");
    const auto address = stringValue(entry["address"]);
    const auto& args = entry["args"];
    for (const auto& arg : args)
        if (!objectShape(arg, {"type", "value"})) throw std::runtime_error("expected typed argument");
    auto type = [&](size_t i) { return stringValue(args.at(i)["type"]); };
    auto val = [&](size_t i) -> const ofJson& { return args.at(i)["value"]; };
    auto word = [&](size_t i) { return stringValue(val(i)); };
    auto integers = [&](size_t i, int min, int max) { return boundedInteger(val(i), min, max); };
    auto require = [](bool good, const char* detail) {
        if (!good) throw std::runtime_error(detail);
    };
    auto signature = [&](std::initializer_list<std::string> codes) {
        if (args.size() != codes.size()) return false;
        size_t index = 0;
        for (const auto& code : codes) {
            const auto actual = "|" + type(index++) + "|";
            if (("|" + code + "|").find(actual) == std::string::npos) return false;
        }
        return true;
    };

    NativeCommand command;
    command.message.setAddress(address);
    for (size_t i = 0; i < args.size(); ++i) {
        const auto kind = type(i);
        const auto& value = val(i);
        if (kind == "source" && i == 1) {
            command.message.addStringArg(source(address, value, command));
        } else if (kind == "layer" && i == 0) {
            if (value == "*") command.message.addStringArg("*");
            else command.message.addInt32Arg(boundedInteger(value, 0, layers - 1));
        } else if (kind == "ref") {
            const auto name = stringValue(value);
            require(identifier(name), "invalid resource reference");
            command.message.addStringArg(name);
        } else if (kind == "vector") {
            const auto array = scalarVector(value);
            if (address == "/shader/var") {
                for (const auto& component : value)
                    appendOscValue(command.message, component["type"], component["value"]);
            } else {
                command.message.addStringArg(array.dump());
            }
        } else {
            require(appendOscValue(command.message, kind, value), "invalid scalar argument");
        }
    }

    const auto nameTarget = [&] { return type(0) == "s" || type(0) == "ref"; };
    auto eachLayer = [&](auto action) {
        if (val(0) == "*") {
            for (int i = 0; i < layers; ++i) action(i);
        } else {
            action(val(0).get<int>());
        }
    };
    auto hasTexture = [&] {
        if (nameTarget()) return textures.count(word(0)) != 0;
        return val(0) != "*" && !layerTextures[val(0).get<int>()].empty();
    };
    auto declare = [&](auto& registry, const std::string& name, const std::string& kind) {
        require(identifier(name) && registry.size() < 8 && registry.emplace(name, kind).second,
                "invalid, excessive or duplicate resource declaration");
    };

    if (file.nativeCommands.empty()) {
        require(address == "/layers/reset" && signature({"i", "s"}), "scene must begin with layer reset and layout");
        layers = integers(0, 1, 32);
        require(val(1) == "stack" || val(1) == "grid", "unsupported layout");
        shaders.resize(layers);
        geometry.resize(layers);
        layerTextures.resize(layers);
    } else if (address == "/tex") {
        require(args.size() >= 2 && (type(0) == "layer" || type(0) == "s"), "invalid texture target");
        const auto kind = command.sourceKind;
        if (type(1) == "ref") {
            require(type(0) == "layer" && args.size() == 2 && textures.count(word(1)), "undefined texture reference");
            eachLayer([&](int layer) { layerTextures[layer] = textures.at(word(1)); });
        } else {
            require(type(1) == "source", "expected explicit texture source");
            if (kind == "grid-plane") {
                require(signature({"layer", "source", "i", "i", "i"}) &&
                    val(2) == 1 && val(3) == 21 && val(4) == 0, "unsupported grid plane configuration");
            } else if (kind == "camera") {
                require(signature({"layer|s", "source", "i", "i"}) && val(2) == 640 && val(3) == 480,
                        "camera requires 640 x 480");
            } else {
                require(args.size() == 2, "unexpected texture source parameters");
            }
            require(type(0) != "layer" || val(0) != "*", "declare a shared resource before binding all layers");
            if (type(0) == "s") declare(textures, word(0), kind);
            else eachLayer([&](int layer) { layerTextures[layer] = kind; });
        }
    } else if (address == "/tex/var") {
        require(signature({"layer|s|ref", "s", "i|f|s|vector"}) && hasTexture() &&
                std::set<std::string>{"glow", "strokeAlpha", "timeStep", "particleMaxAge", "noisePositionScale",
                    "noiseTimeScale", "noisePersistence", "noiseMagnitude", "baseSpeed"}.count(word(1)),
                "unsupported texture property or target");
    } else if (address == "/tex/size") {
        require(signature({"s|ref", "i", "i"}) && hasTexture() && textures.at(word(0)) == "feedback" &&
                val(1) == 128 && val(2) == 128, "feedback dimensions must be 128 x 128");
    } else if (address == "/tex/data") {
        require(signature({"s|ref", "vector", "i"}) && hasTexture() && textures.at(word(0)) == "feedback" &&
                scalarVector(val(1)).size() == 4 && val(2) == 16384, "invalid feedback seed shape");
    } else if (address == "/tex/set") {
        require(signature({"layer|s|ref", "s", "i"}) && hasTexture(), "invalid texture setting");
        static const std::map<std::string, std::set<int>> properties{
            {"wrapMode", {10497, 33071}}, {"filter", {9728}}, {"internalformat", {34836}},
            {"textureTarget", {3553}}, {"blendMode", {0}}, {"numFrames", {120}}, {"aspectRatio", {0}}};
        const auto found = properties.find(word(1));
        require(found != properties.end() && found->second.count(integers(2, 0, 65535)), "unsupported texture setting");
        const auto kind = nameTarget() ? textures.at(word(0)) : layerTextures[val(0).get<int>()];
        const bool valid = word(1) == "numFrames" || word(1) == "aspectRatio" ? kind == "camera" :
            (kind == "feedback" && !(word(1) == "wrapMode" && val(2) != 33071)) ||
            (kind == "image" && word(1) == "wrapMode" && val(2) == 10497);
        require(valid, "setting is invalid for this source");
    } else if (address == "/tex/color") {
        require(signature({"layer", "vector"}) && hasTexture() && scalarVector(val(1)).size() == 3,
                "invalid texture color");
    } else if (address == "/geom") {
        require(args.size() >= 2 && (type(0) == "s" || type(0) == "layer"), "invalid geometry target");
        if (type(0) == "s") {
            require(signature({"s", "source"}) && command.sourceKind == "model", "named geometry requires a model");
            declare(models, word(0), "model");
        } else if (type(1) == "ref") {
            require(args.size() == 2 && models.count(word(1)), "undefined model reference");
        } else if (val(1) == "box") {
            require(signature({"layer", "s"}) || signature({"layer", "s", "i|f", "i|f", "i|f"}) ||
                (signature({"layer", "s", "vector", "vector", "vector", "i"}) && val(5) == 20),
                "unsupported box parameters");
        } else {
            require(val(1) == "grass" && signature({"layer", "s", "i", "i", "i|f", "i"}) &&
                val(2) == 60 && val(3) == 2 && val(4) == 1 && val(5) == 0, "unsupported geometry");
        }
        if (type(0) == "layer") eachLayer([&](int layer) { geometry[layer] = true; });
    } else if (address == "/geom/set") {
        require(signature({"layer", "s", "i"}) && val(0) != "*" && geometry[val(0).get<int>()], "undefined geometry target");
        require((val(1) == "drawInstanced" && (val(2) == 4356 || val(2) == 16384)) ||
                (val(1) == "meshMode" && val(2) == 0), "unsupported geometry setting");
    } else if (address == "/shader") {
        require(signature({"layer", "source"}) && val(0) != "*" && !shaders[val(0).get<int>()], "invalid shader declaration");
        shaders[val(0).get<int>()] = true;
    } else if (address == "/shader/set") {
        require(signature({"layer", "s", "i"}) && val(0) != "*" &&
                ((val(1) == "geomInputType" && val(2) == 1) ||
                 (val(1) == "geomOutputType" && val(2) == 5) ||
                 (val(1) == "geomOutputCount" && val(2) == 14)), "invalid geometry stage setting");
    } else if (address == "/shader/var") {
        require(signature({"layer", "s", "i|f|vector"}) && val(0) != "*" && shaders[val(0).get<int>()] &&
                identifier(word(1)), "uniform requires a preceding shader and typed value");
    } else if (address == "/shader/buffer") {
        require(signature({"layer", "s", "ref"}) && val(0) != "*" && shaders[val(0).get<int>()] &&
                buffers.count(word(2)) && (val(1) == "colors" || val(1) == "tex"), "buffer requires a declared shader and buffer");
    } else if (address == "/var") {
        require(signature({"s", "vector", "vector", "i|f|s", "i"}) && val(4) == 4356 &&
                scalarVector(val(1)).size() == 3 && scalarVector(val(2)).size() == 3, "unsupported node buffer");
        declare(buffers, word(0), "nodes");
    } else if (address == "/var/colors/scheme") {
        require(signature({"s", "s", "s", "i"}) && val(1) == "tetrad" &&
                std::regex_match(word(2), std::regex("(?:#|0x)?[0-9a-fA-F]{6,8}")) && val(3) == 54, "unsupported color buffer");
        declare(buffers, word(0), "palette");
    } else if (address == "/cam") {
        require(signature({"s"}) && (val(0) == "default" || val(0) == "easy"), "unsupported camera");
    } else if (address == "/cam/pos" || address == "/cam/look") {
        require(signature({"i|f", "i|f", "i|f"}) || (signature({"vector"}) && scalarVector(val(0)).size() == 3),
                "expected camera position or look vector");
    } else if (address == "/cam/set") {
        require(signature({"s", "i|f"}) && (val(0) == "nearClip" || val(0) == "farClip" || val(0) == "distance") &&
                val(1) > 0 && val(1) <= 15000, "invalid camera setting");
    } else if (address == "/cam/orbit") {
        require(signature({"i|f"}) && val(0) == 20, "unsupported orbit");
    } else if (address == "/light") {
        require((signature({"i", "i|f", "i|f", "i|f"}) ||
                (signature({"i", "vector"}) && scalarVector(val(1)).size() == 3)) && val(0) == 0,
                "expected light 0 and a position");
    } else if (address == "/shading") {
        require(signature({"s"}) && val(0) == "deferred", "unsupported shading mode");
#if !USE_OFX_DEFERRED_SHADING
        throw std::runtime_error("this scene requires a renderer with deferred shading");
#endif
    } else if (address == "/shading/passes") {
        require(signature({"s", "s", "s", "s", "s", "s"}) &&
                val(0) == "bg" && val(1) == "edge" && val(2) == "ssao" && val(3) == "shadowlight" &&
                val(4) == "pointlight" && val(5) == "bloom", "unsupported startup passes");
    } else if (address == "/layer/alpha") {
        require(signature({"layer", "i|f"}) && val(1) >= 0 && val(1) <= 1, "invalid alpha");
    } else if (address == "/layer/rot" || address == "/layer/tint" || address == "/layer/pivot") {
        require(signature({"layer", "vector"}) && scalarVector(val(1)).size() == 3, "expected three layer components");
    } else if (address == "/layer/delay") {
        require(signature({"layer", "i"}) && val(0) != "*" && layerTextures[val(0).get<int>()] == "camera" &&
                integers(1, 0, 104) == val(0).get<int>() * 13, "unsupported camera history offset");
    } else {
        throw std::runtime_error("address is outside native scene v2");
    }
    file.nativeCommands.push_back(std::move(command));
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

File load(const fs::path& path, const Inputs& inputs) {
    File result;
    result.path = fs::canonical(path);
    if (!fs::is_regular_file(result.path)) throw std::runtime_error("scene must be a regular file");
    result.manifestBytes = readBounded(result.path, MaxManifestBytes);
    const auto& bytes = result.manifestBytes;
    if (bytes.find('\0') != std::string::npos) throw std::runtime_error("scene file contains NUL");
    checkJsonNumbers(bytes);
    std::vector<std::set<std::string>> objectKeys;
    const auto document = ofJson::parse(bytes, [&](int depth, ofJson::parse_event_t event, ofJson& item) {
        if (depth > 12) throw std::runtime_error("JSON nesting exceeds scene bounds");
        if (event == ofJson::parse_event_t::object_start) objectKeys.emplace_back();
        if (event == ofJson::parse_event_t::object_end) objectKeys.pop_back();
        if (event == ofJson::parse_event_t::key && !objectKeys.back().insert(item.get<std::string>()).second)
            throw std::runtime_error("duplicate JSON key: " + item.get<std::string>());
        return true;
    });
    if (!document.is_object() || !document.contains("version") || !document["version"].is_number_integer() ||
        (document["version"] != 1 && document["version"] != 2))
        throw std::runtime_error("unsupported scene version (expected 1 or 2)");
    result.version = document["version"];
    if (result.version == 2 && bytes.size() > MiB)
        throw std::runtime_error("native v2 scene exceeds the 1 MiB manifest limit");
    if (!objectShape(document, {"version", "title", "size", "commands"}) &&
        !objectShape(document, {"version", "title", "size", "commands", "onset_gate"}) &&
        !(result.version == 2 &&
            (objectShape(document, {"version", "title", "size", "commands", "inputs"}) ||
             objectShape(document, {"version", "title", "size", "commands", "onset_gate", "inputs"}))))
        throw std::runtime_error("expected scene metadata, commands and optional onset_gate or v2 inputs");
    if (result.version == 1 && !inputs.empty()) throw std::runtime_error("--scene-input requires native scene v2");
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

    if (result.version == 2) {
        NativeDecoder(result, document.value("inputs", ofJson::object()), inputs).decode(commands);
        return result;
    }

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
                    const auto reference = value.get<std::string>();
                    const bool fragmentAsset = address == "/shader" ||
                        (address == "/tex" && fs::path(reference).extension() == ".frag");
                    command.asset = resolveAsset(result.path.parent_path(), reference,
                                                 fragmentAsset ? "shader" : "asset");
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

void verifyManifest(const File& file, const fs::path& requestedPath) {
    if (fs::canonical(requestedPath) != file.path || !fs::is_regular_file(file.path) ||
        readBounded(file.path, file.manifestBytes.size()) != file.manifestBytes)
        throw std::runtime_error("scene manifest changed while loading");
}

void verifyNativeAssets(const File& file) {
    for (const auto& item : file.nativeAssets) {
        if (fs::canonical(item.first) != item.first ||
            readBounded(item.first, item.second.size()) != item.second)
            throw std::runtime_error("asset changed since preflight: " + item.first.string());
    }
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
