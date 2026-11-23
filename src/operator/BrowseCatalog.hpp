#pragma once

#include "../Config.h"

#if VOSC_OPERATOR_UI
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vosc::operator_ui {

enum class AssetUse { Image, ShaderTexture, LayerProgram, Model, Box };

struct Asset {
    std::string id, path, parent, name, kind, source, origin, requirements;
    bool folder = false;
    std::string preview, vertex, geometry;
    std::vector<std::string> dependencies;
    std::vector<AssetUse> uses;

    bool operator==(const Asset& other) const;
    std::string sourceLabel() const;
    const char* useLabel(AssetUse use) const;
    bool available(AssetUse use) const;
};

std::optional<std::filesystem::path> stagedBrowseFile(const std::string& name);

struct ShaderUniform {
    std::string name, type;
    bool operator==(const ShaderUniform& other) const { return name == other.name && type == other.type; }
};

struct BrowseCatalog {
    using Entries = std::vector<Asset>;

    static BrowseCatalog scan(const std::filesystem::path& staged, std::shared_ptr<const Entries> previous);
    const Asset* registration(const Asset& entry) const;
    std::optional<std::vector<ShaderUniform>> uniforms(const Asset& entry) const;

    std::filesystem::path root;

    std::shared_ptr<const Entries> files;
    Entries builtins, registeredFiles;
    bool scanFailed = false;
    bool metadataFailed = false;
};

} // namespace vosc::operator_ui
#endif
