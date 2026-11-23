#include "ExampleCatalog.hpp"

#if VOSC_OPERATOR_UI
#include "../scene/SceneFile.hpp"
#include "ofFileUtils.h"
#include <algorithm>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace vosc::operator_ui {
namespace {

namespace fs = std::filesystem;
constexpr size_t catalogLimit = 64 * 1024;

bool plainRelative(const fs::path& path) {
    const auto name = path.generic_string();
    return !path.empty() && !path.has_root_path()
        && name.find_first_of("\\\r\n") == std::string::npos && name.find('\0') == std::string::npos
        && std::none_of(path.begin(), path.end(), [](const auto& part) {
            return part.empty() || part.string().front() == '.';
        });
}

std::string readFile(const fs::path& path, size_t limit) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error || !size || size > limit) throw std::runtime_error("Invalid file size");
    std::ifstream input(path, std::ios::binary);
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!input.read(bytes.data(), bytes.size()) || input.peek() != std::ifstream::traits_type::eof())
        throw std::runtime_error("File changed or could not be read");
    return bytes;
}

std::string readOptional(const fs::path& root, const char* name) {
    try {
        if (const auto path = resolveExampleFile(root, name)) return readFile(*path, catalogLimit);
    } catch (const std::exception&) {
        // Optional presentation files do not control scene discovery.
    }
    return {};
}

std::unordered_map<std::string, size_t> indexOrder(const fs::path& root) {
    const auto contents = readOptional(root, "README.md");
    static const std::regex link(R"(\]\(([^\s()]+?)/scene\.vosc\.json\))");
    std::unordered_map<std::string, size_t> order;
    for (auto it = std::sregex_iterator(contents.begin(), contents.end(), link);
         it != std::sregex_iterator(); ++it)
        order.emplace((*it)[1].str(), order.size());
    return order;
}

std::string stableId(std::string_view relativeFolder) {
    if (relativeFolder.empty()) return "_root";
    std::string id;
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : relativeFolder) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-') {
            id += static_cast<char>(ch);
        } else {
            id += '_';
            id += hex[ch >> 4];
            id += hex[ch & 15];
        }
    }
    return id;
}

std::string text(const ofJson& value, size_t limit) {
    const auto& str = value.get_ref<const std::string&>();
    if (str.size() > limit || str.find_first_of("\r\n") != std::string::npos ||
        str.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid example text");
    return str;
}

void readDisplay(ExampleEntry& entry, const ofJson& record) {
    if (!record.is_object()) return;
    try {
        auto category = text(record.at("category"), 64);
        auto summary = text(record.at("summary"), 300);
        auto requirements = text(record.at("requirements"), 300);
        auto cues = text(record.at("cues"), 300);
        auto attach = record.value("attach", "");
        entry.category = std::move(category);
        entry.summary = std::move(summary);
        entry.requirements = std::move(requirements);
        entry.cues = std::move(cues);
        if (attach == "attach" || attach == "replay" || attach == "audio")
            entry.attach = std::move(attach);
    } catch (const std::exception&) {
        // A missing or malformed optional display record uses manifest details.
    }
}

void readInput(ExampleEntry& entry, const ofJson& manifest) {
    const auto input = manifest.find("inputs");
    if (input == manifest.end()) return;
    if (!input->is_object() || input->size() > 1) throw std::runtime_error("Invalid input declaration");
    if (input->empty()) return;
    const auto slot = input->begin();
    entry.inputName = slot.key();
    entry.inputKind = text(slot->at("kind"), 16);
    if (entry.inputKind == "camera") {
        const auto& value = slot->at("default");
        if (!value.is_number_integer() || value < 0 || value > 63)
            throw std::runtime_error("Invalid camera index");
        entry.inputDefault = std::to_string(value.get<int>());
    } else if (entry.inputKind == "html") {
        entry.inputDefault = text(slot->at("default"), 2048);
#if !USE_OFX_ULTRALIGHT
        entry.blocked = "HTML textures require a build with USE_OFX_ULTRALIGHT.";
#endif
    } else if (entry.inputKind != "video") {
        throw std::runtime_error("Unsupported example input");
    }
}

void useManifestDetails(ExampleEntry& entry) {
    if (entry.category.empty()) entry.category = entry.inputKind.empty() ? "Recipe" : "Media & capture";
    if (entry.summary.empty()) entry.summary = "Opens with the setup defined in its scene manifest.";
    if (!entry.requirements.empty()) return;
    if (entry.inputKind == "video") entry.requirements = "Choose a readable local MOV or MP4.";
    if (entry.inputKind == "camera") entry.requirements = "Requires a connected camera and Camera permission for VOSC.";
    if (entry.inputKind == "html") entry.requirements = "Requires USE_OFX_ULTRALIGHT, matching runtime libraries and a reachable page.";
}

void checkAsset(ExampleEntry& entry, const fs::path& root, const fs::path& folder, const ofJson& value) {
    if (!entry.blocked.empty()) return;
    const auto name = text(value, 2048);
    if (!plainRelative(name) || !resolveExampleFile(root, folder / name))
        entry.blocked = "Missing scene asset " + name + ". Restore it beside the manifest.";
}

void checkManifestAssets(ExampleEntry& entry, const ExampleDirectories& roots,
                         const fs::path& folder, const ofJson& commands) {
    if (!commands.is_array()) throw std::runtime_error("Invalid manifest command list");
    for (const auto& command : commands) {
        const auto& args = command.at("args");
        if (!args.is_array()) throw std::runtime_error("Invalid manifest argument list");
        if (command.at("address") == "/shading" && !args.empty() && args[0].at("value") == "deferred") {
            if (entry.category.empty()) entry.category = "Geometry & rendering";
            if (entry.requirements.empty()) entry.requirements = "Requires OpenGL 4.1, deferred shading and its staged shaders.";
#if !USE_OFX_DEFERRED_SHADING
            entry.blocked = "Deferred Shading requires a build with deferred shading.";
#else
            if (!resolveExampleFile(roots.data, "shaders/deferred/gbuffer.frag"))
                entry.blocked = "Missing deferred shaders. Restage them from the renderer addon SDK.";
#endif
        }
        for (const auto& arg : args) {
            if (arg.at("type") == "asset") checkAsset(entry, roots.examples, folder, arg.at("value"));
            else if (arg.at("type") == "source") {
                const auto& source = arg.at("value");
                if (source.contains("path")) checkAsset(entry, roots.examples, folder, source.at("path"));
                if (source.contains("stages"))
                    for (const auto& stage : source.at("stages")) checkAsset(entry, roots.examples, folder, stage);
            }
        }
        if (!entry.blocked.empty()) return;
    }
}

void readManifest(ExampleEntry& entry, const ExampleDirectories& roots, const fs::path& relative) {
    const auto path = resolveExampleFile(roots.examples, relative);
    if (!path) {
        entry.blocked = "The scene manifest is missing or unreadable.";
        return;
    }
    try {
        const auto manifest = ofJson::parse(readFile(*path, scene::MaxManifestBytes));
        auto title = text(manifest.at("title"), 2048);
        if (title.find_first_not_of(" \t") == std::string::npos) throw std::runtime_error("Empty scene title");
        entry.title = std::move(title);
        if (manifest.at("version") != 1 && manifest.at("version") != 2) {
            entry.blocked = "This scene manifest version is unsupported by the current build.";
            return;
        }
        readInput(entry, manifest);
        if (entry.blocked.empty()) checkManifestAssets(entry, roots, relative.parent_path(), manifest.at("commands"));
    } catch (const std::exception&) {
        entry.blocked = "The scene manifest could not be read. Check its format and try again.";
    }
}

} // namespace

ExampleDirectories exampleDirectories() {
    const fs::path data(ofToDataPath("", true));
    const auto products = data.parent_path();
    const auto checkout = products.parent_path();
    std::error_code error;
    if (products.filename() == "bin" && fs::is_regular_file(checkout / "vosc.xcodeproj/project.pbxproj", error))
        return {checkout / "examples", data};
    return {data / "examples", data};
}

std::optional<fs::path> resolveExampleFile(const fs::path& root, const fs::path& relative) {
    if (!plainRelative(relative)) return std::nullopt;
    std::error_code error;
    if (!fs::is_directory(fs::symlink_status(root, error)) || error) return std::nullopt;
    const auto base = fs::canonical(root, error);
    if (error) return std::nullopt;
    auto path = base;
    for (const auto& part : relative) {
        path /= part;
        const auto status = fs::symlink_status(path, error);
        if (error || !fs::exists(status) || fs::is_symlink(status)) return std::nullopt;
    }
    if (!fs::is_regular_file(path, error) || error) return std::nullopt;
    const auto canonical = fs::canonical(path, error);
    if (error || canonical != path) return std::nullopt;
    std::ifstream file(canonical, std::ios::binary);
    if (file.peek() == std::ifstream::traits_type::eof()) return std::nullopt;
    return canonical;
}

std::vector<ExampleEntry> readExamples(const ExampleDirectories& roots, std::string& error) {
    error.clear();
    std::error_code failure;
    if (!fs::is_directory(fs::symlink_status(roots.examples, failure)) || failure) {
        error = "The Examples folder is unavailable. Check the checkout or staged renderer data.";
        return {};
    }
    const auto order = indexOrder(roots.examples);
    const auto display = ofJson::parse(readOptional(roots.examples, "browse.json"), nullptr, false);
    std::vector<ExampleEntry> entries;
    fs::recursive_directory_iterator item(roots.examples, fs::directory_options::skip_permission_denied, failure);
    const fs::recursive_directory_iterator end;
    for (; !failure && item != end; item.increment(failure)) {
        const auto relative = item->path().lexically_relative(roots.examples);
        const auto status = item->symlink_status(failure);
        if (failure || fs::is_symlink(status) || !plainRelative(relative)) {
            item.disable_recursion_pending();
            continue;
        }
        if (!fs::is_regular_file(status) || relative.filename() != "scene.vosc.json") continue;

        const auto folder = relative.parent_path();
        const auto folderName = folder.generic_string();
        ExampleEntry entry;
        entry.id = stableId(folderName);
        entry.title = folder.empty() ? "Scene" : folder.filename().string();
        entry.manifest = relative.generic_string();
        if (display.is_object()) {
            const auto record = display.find(folderName);
            if (record != display.end()) readDisplay(entry, *record);
        }
        if (resolveExampleFile(roots.examples, folder / "preview.png"))
            entry.preview = (folder / "preview.png").generic_string();
        if (resolveExampleFile(roots.examples, folder / "scene.scd"))
            entry.companion = (folder / "scene.scd").generic_string();
        else entry.attach.clear();
        readManifest(entry, roots, relative);
        useManifestDetails(entry);
        entries.push_back(std::move(entry));
    }
    if (failure) error = "Part of the Examples folder could not be read. Try opening it again.";
    const auto rank = [&](const ExampleEntry& entry) {
        const auto found = order.find(fs::path(entry.manifest).parent_path().generic_string());
        return found == order.end() ? order.size() : found->second;
    };
    std::sort(entries.begin(), entries.end(), [&](const auto& lhs, const auto& rhs) {
        const auto left = rank(lhs), right = rank(rhs);
        if (left != right) return left < right;
        if (lhs.title != rhs.title) return lhs.title < rhs.title;
        return lhs.id < rhs.id;
    });
    return entries;
}

} // namespace vosc::operator_ui
#endif
