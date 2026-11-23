#include "BrowseView.hpp"

#if VOSC_OPERATOR_UI
#include "../VOSC.hpp"
#include "../layer/Layer.h"
#include "ofUtils.h"
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

std::string option(uint64_t value, const std::string& label, bool selected) {
    return "<option value='" + std::to_string(value) + "'" + (selected ? " selected" : "")
        + ">" + escaped(label) + "</option>";
}

std::string fact(const char* title, const std::string& value) {
    return "<div class='browse-fact'><span class='readout-label'>" + std::string(title)
        + "</span>" + escaped(value) + "</div>";
}

std::shared_ptr<Layer> currentLayer(const VOSC& renderer, int32_t index) {
    const auto& layers = renderer.operatorLayers();
    if (index < 0 || static_cast<size_t>(index) >= layers.size()) return {};
    const auto& layer = layers[index];
    return layer && layer->index == index ? layer : nullptr;
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
        message += "OSC command metadata unavailable.";
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
    availableUses.clear();
    use.reset();
    selectedTarget.reset();
    stale = false;
    checkAsset();
    invalidateCopy();
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

void BrowseView::invalidateCopy() {
    ++commandToken;
    feedback.clear();
}

void BrowseView::checkAsset() {
    nextAssetCheck = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    const auto* current = catalog.registration(*item);
    if (current) {
        if (!registration || !(*registration == *current)) {
            registration = *current;
            invalidateCopy();
        }
    } else if (registration) {
        registration.reset();
        invalidateCopy();
    }
    std::vector<AssetUse> valid;
    preview.clear();
    if (registration) {
        auto name = registration->preview;
        if (name.empty() && registration->kind == "Image") name = registration->sourceLabel();
        if (stagedBrowseFile(name)) preview = std::move(name);
        for (const auto candidate : registration->uses)
            if (registration->available(candidate)) valid.push_back(candidate);
    }
    if (availableUses == valid) return;
    availableUses = std::move(valid);
    if (!use || std::find(availableUses.begin(), availableUses.end(), *use) == availableUses.end())
        use = availableUses.empty() ? std::nullopt : std::optional{availableUses.front()};
    invalidateCopy();
}

void BrowseView::revalidateTarget(const VOSC& renderer) {
    if (!selectedTarget) return;
    const auto owner = selectedTarget->owner.lock();
    if (owner && currentLayer(renderer, selectedTarget->index) == owner) return;
    selectedTarget.reset();
    stale = true;
    invalidateCopy();
}

void BrowseView::syncTargets(const VOSC& renderer) {
    revalidateTarget(renderer);
    const auto& layers = renderer.operatorLayers();
    const auto isSame = [](const Target& target, const std::shared_ptr<Layer>& layer) {
        return layer && target.index == layer->index && target.owner.lock() == layer;
    };
    if (std::equal(visibleTargets.begin(), visibleTargets.end(), layers.begin(), layers.end(), isSame)) return;

    std::vector<Target> current;
    current.reserve(layers.size());
    for (const auto& layer : layers) {
        if (!layer) continue;
        const auto previous = std::find_if(visibleTargets.begin(), visibleTargets.end(),
            [&](const auto& target) { return isSame(target, layer); });
        current.push_back(previous == visibleTargets.end()
            ? Target{layer->index, ++nextTargetToken, layer} : *previous);
    }
    visibleTargets = std::move(current);
}

void BrowseView::selectUse(uint64_t option) {
    if (!detail() || !option) return;
    const auto found = std::find_if(availableUses.begin(), availableUses.end(), [&](auto candidate) {
        return static_cast<uint64_t>(candidate) + 1 == option;
    });
    if (found == availableUses.end() || use == *found) return;
    use = *found;
    invalidateCopy();
}

void BrowseView::selectTarget(const VOSC& renderer, uint64_t option) {
    if (!detail()) return;
    revalidateTarget(renderer);
    if (!option) {
        selectedTarget.reset();
    } else {
        const auto previous = std::find_if(visibleTargets.begin(), visibleTargets.end(), [&](const auto& target) {
            return target.token == option;
        });
        const auto owner = previous == visibleTargets.end() ? nullptr : previous->owner.lock();
        if (!owner || currentLayer(renderer, previous->index) != owner) {
            selectedTarget.reset();
            stale = true;
            invalidateCopy();
            return;
        }
        selectedTarget = *previous;
        stale = false;
    }
    invalidateCopy();
}

std::optional<AssetCommand> BrowseView::command() const {
    if (!registration || !use || !selectedTarget) return std::nullopt;
    return assetCommand(*registration, *use, selectedTarget->index);
}

std::optional<std::string> BrowseView::copyCommand(const VOSC& renderer, uint64_t token) {
    if (!detail()) return std::nullopt;
    checkAsset();
    revalidateTarget(renderer);
    if (token != commandToken) return std::nullopt;
    if (const auto ready = command()) return ready->snippet();
    return std::nullopt;
}

void BrowseView::finishCopy(bool success) {
    feedback = success ? "Command copied." : "Clipboard unavailable. Try copying again.";
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
    if (catalog.metadataFailed) return result + previewMarkup;
    if (!registration)
        return result + "<p class='browse-requirements'>No verified OSC use.</p>" + previewMarkup;
    if (availableUses.empty())
        return result + "<p class='message error'>Asset unavailable. Check the staged source and dependencies.</p>" + previewMarkup;
    if (visibleTargets.empty())
        result += "<p class='message error'>No layers available. Add a layer before choosing a target.</p>";
    else if (stale)
        result += "<p class='message error'>Target replaced. Select a current layer.</p>";

    result += "<div class='browse-choice'><div class='browse-field'><span>Source use</span>"
        "<select id='browse-use' aria-label='Source use'>";
    for (auto candidate : availableUses)
        result += option(static_cast<uint64_t>(candidate) + 1, entry.useLabel(candidate), use == candidate);
    result += "</select></div><div class='browse-field'><span>OSC target</span>"
        "<select id='browse-target' aria-label='OSC target'";
    if (visibleTargets.empty()) result += " disabled";
    result += ">" + option(0, "Choose a layer", !selectedTarget);
    for (const auto& target : visibleTargets)
        result += option(target.token, "Layer " + std::to_string(target.index),
                         selectedTarget && selectedTarget->token == target.token);
    result += "</select></div></div>";
    const auto ready = command();
    if (!ready) {
        if (!visibleTargets.empty() && !stale)
            result += "<p class='browse-origin'>Choose a layer to preview the OSC command.</p>";
        return result + previewMarkup;
    }

    std::string types;
    for (const auto& arg : ready->arguments) {
        if (!types.empty()) types += " · ";
        if (const auto* number = std::get_if<int32_t>(&arg)) types += "i: " + std::to_string(*number);
        else types += "s: " + std::get<std::string>(arg);
    }
    return result + "<div class='browse-command'><div class='browse-facts'>"
        + fact("OSC address", ready->address) + fact("Typed arguments", types)
        + "</div><span class='readout-label'>SuperCollider snippet · connected ~visuals client</span>"
          "<div class='browse-snippet'>" + escaped(ready->snippet()) + "</div>"
          "<button id='browse-copy' data-target='" + std::to_string(commandToken) + "'>Copy OSC command</button>"
          "<span class='browse-feedback'>" + escaped(feedback) + "</span></div>" + previewMarkup;
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

const std::string& BrowseView::markup(const VOSC& renderer) {
    if (!detail()) {
        if (rowsDirty) {
            updateRows();
        }
        return rowsMarkup;
    }
    syncTargets(renderer);
    if (std::chrono::steady_clock::now() >= nextAssetCheck) checkAsset();
    detailContent = detailMarkup();
    return detailContent;
}

int BrowseView::height(bool compact) const {
    if (!detail()) return showingBuiltins ? 156 : compact ? 264 : 468;
    const bool hasCommand = use && selectedTarget;
    const bool hasPreview = !preview.empty();
    if (compact) return hasCommand || hasPreview ? 339 : 264;
    if (hasCommand) return hasPreview ? 478 : 430;
    return hasPreview ? 372 : 300;
}

} // namespace vosc::operator_ui
#endif
