#pragma once

#include "BrowseCatalog.hpp"

#if VOSC_OPERATOR_UI
#include <chrono>
#include <future>

namespace vosc::operator_ui {

class BrowseView {
public:
    bool detail() const { return item.has_value(); }
    bool canGoBack() const { return detail() || !parents.empty(); }
    std::string heading() const;
    bool setFilters(std::string query, std::string category);
    const std::string& summary() const;
    std::string status() const;
    void refresh();
    const std::string& markup();
    int height(bool compact) const;

    bool openItem(const std::string& id, uint64_t row, float scroll);
    std::string backItem();
    float originScroll() const { return catalogScroll; }

private:
    const Asset& asset() const { return registration ? *registration : *item; }
    const BrowseCatalog::Entries& entries() const;
    bool matches(const Asset& entry) const;
    std::string detailMarkup() const;
    void updateRows();
    std::string rowId(size_t row) const;
    void checkAsset();

    BrowseCatalog catalog;
    std::future<BrowseCatalog> pendingScan;
    struct FolderOrigin {
        std::string folder, row;
        float scroll;
    };
    std::vector<FolderOrigin> parents;
    std::chrono::steady_clock::time_point nextCatalogCheck{};
    std::optional<Asset> item, registration;
    std::string folder, selectedRow, query, category;
    std::string rowsMarkup, countText, detailContent;
    float catalogScroll = 0;
    bool rowsDirty = true;
    bool scanned = false;

    std::optional<std::vector<ShaderUniform>> uniforms;
    std::vector<AssetUse> availableUses;
    std::string preview;
    std::chrono::steady_clock::time_point nextAssetCheck{};
};

} // namespace vosc::operator_ui
#endif
