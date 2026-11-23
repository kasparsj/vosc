#include "BrowseCatalog.hpp"

#if VOSC_OPERATOR_UI
#include "../shader/Shader.h"
#include "ofFileUtils.h"
#include "ofJson.h"
#include "ofUtils.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>

namespace vosc::operator_ui {
namespace {
namespace fs = std::filesystem;

bool localName(const std::string& name) {
    const fs::path path(name);
    if (name.empty() || path.has_root_path() || name.find(':') != std::string::npos
        || name.find('\0') != std::string::npos)
        return false;
    return std::none_of(path.begin(), path.end(), [](const auto& part) {
        return part.empty() || part == "." || part == "..";
    });
}

AssetUse parseUse(const std::string& name) {
    if (name == "image") return AssetUse::Image;
    if (name == "shader-texture") return AssetUse::ShaderTexture;
    if (name == "layer-program") return AssetUse::LayerProgram;
    if (name == "model") return AssetUse::Model;
    if (name == "box") return AssetUse::Box;
    throw std::runtime_error("Unknown Browse asset use");
}

bool validUse(const Asset& asset, AssetUse use) {
    const fs::path source(asset.source);
    const auto extension = source.extension().string();
    switch (use) {
        case AssetUse::Image:
            return asset.kind == "Image" && !source.has_parent_path()
                && (extension == ".png" || extension == ".jpg" || extension == ".jpeg");
        case AssetUse::ShaderTexture:
        case AssetUse::LayerProgram:
            return asset.kind == "Fragment shader" && source.begin()->string() == "shaders"
                && extension == ".frag";
        case AssetUse::Model:
            return asset.kind == "Model" && source.begin()->string() == "models" && extension == ".obj";
        case AssetUse::Box:
            return asset.kind == "Built-in" && asset.source == "box";
    }
    return false;
}

bool matchesStage(const std::string& resolved, const std::string& expected) {
    if (expected.empty()) return resolved.empty();
    const auto file = stagedBrowseFile(expected);
    if (!file || resolved.empty()) return false;
    std::error_code error;
    return fs::canonical(ofToDataPath(resolved, true), error) == *file && !error;
}

std::string scString(const std::string& text) {
    std::string result = "\"";
    for (const auto character : text) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            default: result += character; break;
        }
    }
    return result + '"';
}

Asset readAsset(const ofJson& item) {
    Asset asset;
    asset.id = item.at("id").get<std::string>();
    asset.name = item.at("name").get<std::string>();
    asset.kind = item.at("kind").get<std::string>();
    asset.source = item.at("source").get<std::string>();
    asset.origin = item.at("origin").get<std::string>();
    asset.requirements = item.at("requirements").get<std::string>();
    asset.preview = item.value("preview", "");
    asset.vertex = item.value("vertex", "");
    asset.geometry = item.value("geometry", "");
    asset.dependencies = item.value("dependencies", std::vector<std::string>{});
    if (asset.id.empty() || asset.name.empty() || !localName(asset.source)
        || (!asset.preview.empty() && !localName(asset.preview))
        || (!asset.vertex.empty() && !localName(asset.vertex))
        || (!asset.geometry.empty() && !localName(asset.geometry))
        || asset.dependencies.size() > 16
        || !std::all_of(asset.dependencies.begin(), asset.dependencies.end(), localName))
        throw std::runtime_error("Invalid Browse asset metadata");

    const auto uses = item.at("uses").get<std::vector<std::string>>();
    if (uses.empty() || uses.size() > 2) throw std::runtime_error("Invalid Browse asset uses");
    for (const auto& name : uses) {
        const auto use = parseUse(name);
        if (!validUse(asset, use) || std::find(asset.uses.begin(), asset.uses.end(), use) != asset.uses.end())
            throw std::runtime_error("Incompatible Browse asset use");
        asset.uses.push_back(use);
    }
    return asset;
}

std::string fileKind(const fs::path& path) {
    auto extension = path.extension().string();
    for (auto& ch : extension)
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    if (extension == ".jpg" || extension == ".jpeg" || extension == ".png") return "Image";
    if (extension == ".frag") return "Fragment shader";
    if (extension == ".vert" || extension == ".geom" || extension == ".glsl") return "Shader source";
    if (extension == ".obj" || extension == ".ply" || extension == ".dae"
        || extension == ".3ds" || extension == ".stl") return "Model";
    return "File";
}

fs::path browseRoot(const fs::path& staged) {
    const auto products = staged.parent_path();
    const auto checkout = products.parent_path();
    std::error_code error;
    // Xcode's normal bin/data layout has a source library beside bin.
    if (products.filename() == "bin" && fs::is_regular_file(checkout / "vosc.xcodeproj/project.pbxproj", error)
        && fs::is_directory(fs::symlink_status(checkout / "assets", error)))
        return checkout / "assets";
    return staged;
}

std::vector<Asset> readMetadata() {
    const auto path = stagedBrowseFile("catalog/browse-assets.json");
    std::error_code error;
    if (!path || fs::file_size(*path, error) > 64 * 1024 || error) return {};
    std::ifstream stream(*path, std::ios::binary);
    try {
        const auto manifest = ofJson::parse(stream);
        if (!manifest.is_array() || manifest.size() > 64) return {};
        std::vector<Asset> result;
        for (const auto& item : manifest) result.push_back(readAsset(item));
        return result;
    } catch (const std::exception&) {
        // The file tree is useful even when optional command metadata is invalid.
        return {};
    }
}

}

std::optional<fs::path> stagedBrowseFile(const std::string& name) {
    if (!localName(name)) return std::nullopt;
    std::error_code error;
    const auto root = fs::canonical(ofToDataPath("", true), error);
    if (error) return std::nullopt;
    const auto path = fs::canonical(root / name, error);
    if (error) return std::nullopt;
    const auto relative = path.lexically_relative(root);
    if (relative.empty() || relative.begin()->string() == ".."
        || !fs::is_regular_file(path, error) || error) return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    if (file.peek() == std::ifstream::traits_type::eof()) return std::nullopt;
    return path;
}

std::string Asset::sourceLabel() const {
    if (!path.empty()) return path;
    if (kind == "Built-in") return source;
    if (kind == "Image") return "images/" + source;
    return source;
}

const char* Asset::useLabel(AssetUse use) const {
    switch (use) {
        case AssetUse::Image: return "Image texture";
        case AssetUse::ShaderTexture: return "Shader texture";
        case AssetUse::LayerProgram: return vertex.empty() ? "Layer program" : "Paired layer program";
        case AssetUse::Model: return "Model geometry";
        case AssetUse::Box: return "Box geometry";
    }
    return "";
}

bool Asset::available(AssetUse use) const {
    if (use == AssetUse::Box) return true;
    if (!stagedBrowseFile(sourceLabel())) return false;
    for (const auto& dependency : dependencies)
        if (!stagedBrowseFile(dependency)) return false;
    if (use == AssetUse::ShaderTexture || use == AssetUse::LayerProgram) {
        const auto paths = Shader::getShaderPaths(source);
        return matchesStage(paths.fragPath, source)
            && matchesStage(paths.vertPath, vertex.empty() ? "shaders/passthru.vert" : vertex)
            && matchesStage(paths.geomPath, geometry);
    }
    return true;
}

AssetCommand assetCommand(const Asset& asset, AssetUse use, int32_t layer) {
    const char* address = "/tex";
    if (use == AssetUse::LayerProgram) address = "/shader";
    if (use == AssetUse::Model || use == AssetUse::Box) address = "/geom";
    AssetCommand command{address, {layer, asset.source}};
    if (use == AssetUse::Box) {
        for (int i = 0; i < 3; ++i) command.arguments.emplace_back(int32_t{200});
    }
    return command;
}

std::string AssetCommand::snippet() const {
    std::string text = "~visuals.sendMsg('" + std::string(address) + "'";
    for (const auto& arg : arguments) {
        text += ", ";
        if (const auto* string = std::get_if<std::string>(&arg)) text += scString(*string);
        else text += std::to_string(std::get<int32_t>(arg));
    }
    return text + ");";
}

void BrowseCatalog::load() {
    std::error_code error;
    const auto staged = fs::canonical(ofToDataPath("", true), error);
    if (error) return;
    const auto root = browseRoot(staged);
    std::vector<Asset> discovered;
    std::map<std::string, Asset> metadata;
    for (auto& asset : readMetadata()) {
        if (asset.kind == "Built-in") {
            asset.id = "builtin-" + ofToHex(asset.id);
            discovered.push_back(std::move(asset));
        } else {
            const auto source = asset.sourceLabel();
            metadata.emplace(source, std::move(asset));
        }
    }

    fs::recursive_directory_iterator file(root, fs::directory_options::skip_permission_denied, error);
    for (const fs::recursive_directory_iterator end; !error && file != end; file.increment(error)) {
        const auto path = file->path().lexically_relative(root);
        const auto name = path.filename().string();
        const auto relative = path.generic_string();
        if (!name.empty() && name.front() == '.') {
            file.disable_recursion_pending();
            continue;
        }
        const auto status = file->symlink_status(error);
        if (error) break;
        if (!fs::is_directory(status) && !fs::is_regular_file(status)) continue;

        Asset asset;
        if (const auto known = metadata.find(relative); known != metadata.end()) asset = known->second;
        asset.folder = fs::is_directory(status);
        asset.id = (asset.folder ? "folder-" : "file-") + ofToHex(relative);
        asset.path = relative;
        asset.parent = path.parent_path().generic_string();
        asset.name = name;
        if (asset.kind.empty()) asset.kind = asset.folder ? "Folder" : fileKind(path);
        if (asset.origin.empty()) asset.origin = "VOSC shared library";
        if (asset.kind == "Image" && asset.preview.empty()) asset.preview = relative;
        discovered.push_back(std::move(asset));
    }
    if (error) return;

    std::sort(discovered.begin(), discovered.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.folder != rhs.folder) return lhs.folder;
        return lhs.path == rhs.path ? lhs.id < rhs.id : lhs.path < rhs.path;
    });
    entries = std::move(discovered);
    loaded = true;
}

} // namespace vosc::operator_ui
#endif
