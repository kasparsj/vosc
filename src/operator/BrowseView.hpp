#pragma once

#include "BrowseCatalog.hpp"

#if VOSC_OPERATOR_UI
#include <chrono>
#include <memory>

class Layer;
class VOSC;

namespace vosc::operator_ui {

class BrowseView {
public:
    bool detail() const { return item.has_value(); }
    bool canGoBack() const { return detail() || !parents.empty(); }
    bool builtins() const { return showingBuiltins; }
    bool switchCatalog(bool builtins);
    std::string heading() const;
    bool setFilters(std::string query, std::string category);
    std::string summary() const;
    std::string markup(const VOSC& renderer);
    int height(bool compact) const;

    bool openItem(const std::string& id, uint64_t row, float scroll);
    std::string backItem();
    float originScroll() const { return catalogScroll; }
    void selectUse(uint64_t option);
    void selectTarget(const VOSC& renderer, uint64_t option);
    std::optional<std::string> copyCommand(const VOSC& renderer, uint64_t token);
    void finishCopy(bool success);

private:
    struct Target {
        int32_t index;
        uint64_t token;
        std::weak_ptr<Layer> owner;
    };

    const Asset& asset() const { return catalog.assets()[*item]; }
    bool matches(const Asset& entry) const;
    bool visible(const Asset& entry) const;
    std::string detailMarkup() const;
    std::string catalogMarkup() const;
    std::string rowId(size_t row) const;
    void syncTargets(const VOSC& renderer);
    void revalidateTarget(const VOSC& renderer);
    void checkAsset();
    void invalidateCopy();
    std::optional<AssetCommand> command() const;

    BrowseCatalog catalog;
    struct FolderOrigin {
        std::string folder, row;
        float scroll;
    };
    std::vector<FolderOrigin> parents;
    std::chrono::steady_clock::time_point nextCatalogCheck{};
    std::optional<size_t> item;
    std::string folder, selectedRow, query, category, feedback;
    float catalogScroll = 0;
    bool showingBuiltins = false;

    std::optional<AssetUse> use;
    std::vector<AssetUse> availableUses;
    std::optional<std::filesystem::path> preview;
    std::chrono::steady_clock::time_point nextAssetCheck{};
    std::vector<Target> visibleTargets;
    std::optional<Target> selectedTarget;
    bool stale = false;
    uint64_t nextTargetToken = 0, commandToken = 1;
};

} // namespace vosc::operator_ui
#endif
