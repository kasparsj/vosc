#pragma once

#include "../Config.h"

#if VOSC_OPERATOR_UI
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vosc::operator_ui {

enum class AssetUse { Image, ShaderTexture, LayerProgram, Model, Box };

struct Asset {
    std::string id, name, kind, source, origin, requirements;
    std::string preview, vertex, geometry;
    std::vector<std::string> dependencies;
    std::vector<AssetUse> uses;

    std::string sourceLabel() const;
    const char* useLabel(AssetUse use) const;
    bool available(AssetUse use) const;
};

struct AssetCommand {
    const char* address;
    std::vector<std::variant<int32_t, std::string>> arguments;

    std::string snippet() const;
};

AssetCommand assetCommand(const Asset& asset, AssetUse use, int32_t layer);
std::optional<std::filesystem::path> stagedBrowseFile(const std::string& name);

class BrowseCatalog {
public:
    void load();
    const auto& assets() const { return entries; }
    bool available() const { return loaded; }

private:
    std::vector<Asset> entries;
    bool loaded = false;
};

} // namespace vosc::operator_ui
#endif
