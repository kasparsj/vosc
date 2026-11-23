#include "BrowseView.hpp"

#if VOSC_OPERATOR_UI
#include "ofFileUtils.h"
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace vosc::operator_ui {
namespace {
constexpr std::string_view rowPrefix = "asset-row-";

std::string escaped(const std::string& text) {
    return Rml::StringUtilities::EncodeRml(text);
}

std::string fact(const char* title, const std::string& value) {
    return "<div class='browse-fact'><span class='readout-label'>" + std::string(title)
        + "</span>" + escaped(value) + "</div>";
}

}

bool BrowseView::switchCatalog(bool builtins) {
    if (canGoBack() || showingBuiltins == builtins) return false;
    showingBuiltins = builtins;
    selectedRow.clear();
    rowsDirty = true;
    return true;
}

bool BrowseView::setFilters(std::string name, std::string kind) {
    name = Rml::StringUtilities::ToLower(std::move(name));
    if (name == query && kind == category) return false;
    selectedRow.clear();
    catalogScroll = 0;
    query = std::move(name);
    category = std::move(kind);
    rowsDirty = true;
    return true;
}

bool BrowseView::matches(const Asset& entry) const {
    return (category.empty() || category == entry.kind) && Rml::StringUtilities::ToLower(entry.name).find(query) != std::string::npos;
}

const BrowseCatalog::Entries& BrowseView::entries() const {
    static const BrowseCatalog::Entries empty;
    if (showingBuiltins) return catalog.builtins;
    return catalog.files ? *catalog.files : empty;
}

std::string BrowseView::heading() const {
    if (showingBuiltins) return "Built-ins";
    return folder.empty() ? "Assets" : "Assets / " + folder;
}

const std::string& BrowseView::summary() const {
    static const std::string empty;
    return detail() ? empty : countText;
}

std::string BrowseView::status() const {
    std::string message;
    if (catalog.scanFailed && !showingBuiltins) {
        message = catalog.files
            ? "Asset refresh failed. Showing the last listing."
            : "Asset refresh failed. The asset folder is unavailable.";
    }
    if (catalog.metadataFailed) {
        if (!message.empty()) message += '\n';
        message += "Asset metadata unavailable.";
    }
    return message;
}

std::string BrowseView::rowId(size_t row) const {
    return std::string(rowPrefix) + entries()[row].id;
}

void BrowseView::updateRows() {
    rowsDirty = false;
    rowsMarkup.clear();
    countText.clear();
    if (!scanned) {
        rowsMarkup = "<p class='message'>Scanning assets…</p>";
        return;
    }
    if (showingBuiltins ? catalog.metadataFailed : !catalog.files) return;

    const auto& assets = entries();
    const bool filtering = !showingBuiltins && (!query.empty() || !category.empty());
    const auto prefix = folder + '/';
    unsigned total = 0, filtered = 0;
    std::unordered_set<std::string> matchingFolders;
    std::vector<bool> matchingFiles(assets.size(), false);
    for (size_t index = 0; index < assets.size(); ++index) {
        const auto& entry = assets[index];
        if (entry.folder || (!folder.empty() && entry.path.rfind(prefix, 0) != 0)) continue;
        ++total;
        if (filtering && !matches(entry)) continue;
        ++filtered;
        matchingFiles[index] = true;
        if (filtering && entry.parent != folder) {
            const auto start = folder.empty() ? 0 : prefix.size();
            matchingFolders.insert(entry.path.substr(0, entry.path.find('/', start)));
        }
    }
    countText = total == filtered
        ? std::to_string(total) + (total == 1 ? " asset" : " assets")
        : std::to_string(filtered) + " of " + std::to_string(total) + " assets";

    for (size_t index = 0; index < assets.size(); ++index) {
        const auto& entry = assets[index];
        if (entry.parent != folder) continue;
        if (entry.folder ? filtering && matchingFolders.count(entry.path) == 0 : !matchingFiles[index])
            continue;
        const auto id = rowId(index);
        rowsMarkup += "<button id=\"" + escaped(id) + "\" data-target='" + std::to_string(index + 1)
            + "' class='inspect-row asset-row" + (selectedRow == id ? " selected" : "")
            + "'><span class='inspect-row-label'>" + escaped(entry.name)
            + "</span><span class='inspect-kind'>" + escaped(entry.kind)
            + "</span><img class='browse-chevron' src='browse:catalog/chevron.png' /></button>";
    }
    if (!rowsMarkup.empty()) return;
    if (showingBuiltins)
        rowsMarkup += "<p class='message'>No built-in sources are available.</p>";
    else if (!filtering)
        rowsMarkup += "<p class='message'>This folder is empty.</p>";
    else
        rowsMarkup += "<p class='message'>No assets match this search and category.</p>"
            "<button id='browse-clear'>Clear search and category</button>";
}

bool BrowseView::openItem(const std::string& id, uint64_t row, float scroll) {
    const std::string_view actionId = id;
    if (!row || actionId.substr(0, rowPrefix.size()) != rowPrefix) return false;
    const auto entryId = actionId.substr(rowPrefix.size());
    const auto& assets = entries();
    auto found = row <= assets.size() ? assets.begin() + (row - 1) : assets.end();
    if (found == assets.end() || found->id != entryId) {
        // A published refresh may have moved this asset since the click was queued.
        found = std::find_if(assets.begin(), assets.end(), [&](const auto& entry) {
            return entry.id == entryId;
        });
        if (found == assets.end()) return false;
    }
    const auto& entry = *found;
    if (entry.parent != folder || (!showingBuiltins && !entry.folder && !matches(entry))) return false;
    rowsDirty = true;
    if (entry.folder) {
        parents.push_back({folder, id, scroll});
        folder = entry.path;
        selectedRow.clear();
        return true;
    }
    item = entry;
    registration.reset();
    selectedRow = id;
    catalogScroll = scroll;
    uniforms = catalog.uniforms(entry);
    checkAsset();
    return true;
}

std::string BrowseView::backItem() {
    rowsDirty = true;
    if (detail()) {
        item.reset();
        return selectedRow;
    }
    const auto previous = std::move(parents.back());
    parents.pop_back();
    folder = previous.folder;
    selectedRow = previous.row;
    catalogScroll = previous.scroll;
    return selectedRow;
}

void BrowseView::checkAsset() {
    nextAssetCheck = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    const auto* current = catalog.registration(*item);
    if (current) registration = *current;
    else registration.reset();
    availableUses.clear();
    preview.clear();
    if (!registration) return;
    auto name = registration->preview;
    if (name.empty() && registration->kind == "Image") name = registration->sourceLabel();
    if (stagedBrowseFile(name)) preview = std::move(name);
    for (const auto use : registration->uses)
        if (registration->available(use)) availableUses.push_back(use);
}

std::string BrowseView::detailMarkup() const {
    const auto& entry = asset();
    std::string result = "<div class='inspect-label'>" + escaped(item->name) + "</div>"
        "<div class='browse-facts'>" + fact("Kind", entry.kind) + fact("Source", entry.sourceLabel())
        + "</div><div class='browse-origin'>" + escaped(entry.origin)
        + "</div>";
    if (!entry.requirements.empty())
        result += "<p class='browse-requirements'>" + escaped(entry.requirements) + "</p>";
    const auto previewMarkup = !preview.empty()
        ? "<div class='browse-preview'><img alt='Saved asset preview' src=\"browse:"
            + escaped(preview) + "\" /></div>"
        : std::string{};
    if (registration) {
        if (availableUses.empty()) {
            result += "<p class='message error'>Asset unavailable. Check the staged source and dependencies.</p>";
        } else if (availableUses.size() > 1 || entry.kind == "Fragment shader") {
            std::string uses;
            for (const auto use : availableUses) {
                if (!uses.empty()) uses += " · ";
                uses += entry.useLabel(use);
            }
            result += "<div class='browse-origin'>" + fact("Source uses", uses) + "</div>";
        }
    }
    if (item->kind == "Fragment shader" || item->kind == "Shader source") {
        result += "<div class='browse-requirements'><span class='readout-label'>Declared uniforms</span>";
        if (!uniforms) result += "<div>Could not read shader uniforms.</div>";
        else if (uniforms->empty()) result += "<div>No standalone uniforms found in the source.</div>";
        if (uniforms) {
            for (const auto& uniform : *uniforms) {
                result += "<div class='browse-uniform'><span>" + escaped(uniform.name)
                    + "</span><span>" + escaped(uniform.type) + "</span></div>";
            }
        }
        result += "</div>";
    }
    return result + previewMarkup;
}

void BrowseView::refresh() {
    const auto now = std::chrono::steady_clock::now();
    try {
        if (pendingScan.valid()) {
            if (pendingScan.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            auto snapshot = pendingScan.get();
            if (!scanned || (showingBuiltins
                    ? catalog.builtins != snapshot.builtins || catalog.metadataFailed != snapshot.metadataFailed
                    : catalog.files != snapshot.files))
                rowsDirty = true;
            if (detail() && (catalog.registeredFiles != snapshot.registeredFiles
                             || catalog.builtins != snapshot.builtins))
                nextAssetCheck = {};
            catalog = std::move(snapshot);
            scanned = true;
        }
        if (now >= nextCatalogCheck) {
            nextCatalogCheck = now + std::chrono::seconds(1);
            const std::filesystem::path staged = ofToDataPath("", true);
            pendingScan = std::async(std::launch::async, BrowseCatalog::scan, staged, catalog.files);
        }
    } catch (const std::system_error&) {
        catalog.scanFailed = true;
        catalog.metadataFailed = true;
        catalog.builtins.clear();
        catalog.registeredFiles.clear();
        nextCatalogCheck = now + std::chrono::seconds(1);
        nextAssetCheck = {};
        scanned = rowsDirty = true;
    }
}

const std::string& BrowseView::markup() {
    if (!detail()) {
        if (rowsDirty) {
            updateRows();
        }
        return rowsMarkup;
    }
    if (std::chrono::steady_clock::now() >= nextAssetCheck) checkAsset();
    detailContent = detailMarkup();
    return detailContent;
}

int BrowseView::height(bool compact) const {
    if (!detail()) return showingBuiltins ? 156 : compact ? 264 : 468;
    const bool hasPreview = !preview.empty();
    const bool hasUniforms = uniforms && !uniforms->empty();
    if (compact) return hasPreview || hasUniforms ? 339 : 264;
    if (hasUniforms) return 430;
    return hasPreview ? 372 : 300;
}

} // namespace vosc::operator_ui
#endif
