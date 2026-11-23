#include "BrowseView.hpp"

#if VOSC_OPERATOR_UI
#include "../VOSC.hpp"
#include "../layer/Layer.h"
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <utility>

namespace vosc::operator_ui {
namespace {
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

bool BrowseView::setFilters(std::string name, std::string kind) {
    name = Rml::StringUtilities::ToLower(std::move(name));
    if (name == query && kind == category) return false;
    selectedRow.reset();
    catalogScroll = 0;
    query = std::move(name);
    category = std::move(kind);
    return true;
}

bool BrowseView::matches(const Asset& entry) const {
    return (category.empty() || category == entry.kind) && Rml::StringUtilities::ToLower(entry.name).find(query) != std::string::npos;
}

std::string BrowseView::summary() const {
    if (detail() || !catalog.available()) return {};
    const auto& assets = catalog.assets();
    return std::to_string(std::count_if(assets.begin(), assets.end(), [this](const auto& entry) {
        return matches(entry);
    })) + " of " + std::to_string(assets.size()) + " assets";
}

std::string BrowseView::rowId(size_t row) const {
    return "asset-row-" + catalog.assets()[row].id;
}

std::string BrowseView::catalogMarkup() const {
    if (!catalog.available()) return "<p class='message error'>The staged asset catalog is unavailable.</p>";
    const auto& assets = catalog.assets();
    std::string result;
    for (size_t index = 0; index < assets.size(); ++index) {
        const auto& entry = assets[index];
        if (!matches(entry)) continue;
        result += "<button id=\"" + escaped(rowId(index)) + "\" data-target='" + std::to_string(index + 1)
            + "' class='inspect-row asset-row" + (selectedRow == index ? " selected" : "")
            + "'><span class='inspect-row-label'>" + escaped(entry.name)
            + "<span class='inspect-subtitle'>" + escaped(entry.sourceLabel())
            + "</span></span><span class='inspect-kind'>" + escaped(entry.kind)
            + "</span><img class='browse-chevron' src='browse:catalog/chevron.png' /></button>";
    }
    if (result.empty()) {
        return "<p class='message'>No assets match this search and category.</p>"
            "<button id='browse-clear'>Clear search and category</button>";
    }
    return result;
}

bool BrowseView::openItem(uint64_t row, float scroll) {
    if (!row || row > catalog.assets().size() || !matches(catalog.assets()[row - 1])) return false;
    item = selectedRow = row - 1;
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
    const auto id = selectedRow ? rowId(*selectedRow) : "browse-search";
    item.reset();
    return id;
}

void BrowseView::invalidateCopy() {
    ++commandToken;
    feedback.clear();
}

void BrowseView::checkAsset() {
    nextAssetCheck = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    std::vector<AssetUse> valid;
    for (const auto candidate : asset().uses)
        if (asset().available(candidate)) valid.push_back(candidate);
    preview = stagedBrowseFile(asset().preview);
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
    if (!use || !selectedTarget) return std::nullopt;
    return assetCommand(asset(), *use, selectedTarget->index);
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
    std::string result = "<div class='inspect-label'>" + escaped(entry.name) + "</div>"
        "<div class='browse-facts'>" + fact("Kind", entry.kind) + fact("Source", entry.sourceLabel())
        + "</div><div class='browse-origin'>" + escaped(entry.origin)
        + "</div><p class='browse-requirements'>" + escaped(entry.requirements) + "</p>";
    const auto previewMarkup = preview
        ? "<div class='browse-preview'><img alt='Saved asset preview' src=\"browse:"
            + escaped(entry.preview) + "\" /></div>"
        : std::string{};
    if (availableUses.empty())
        result += "<p class='message error'>Asset unavailable. Check the staged source and dependencies.</p>";
    if (visibleTargets.empty())
        result += "<p class='message error'>No layers available. Add a layer before choosing a target.</p>";
    else if (stale)
        result += "<p class='message error'>Target replaced. Select a current layer.</p>";

    if (availableUses.empty()) return result + previewMarkup;
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

std::string BrowseView::markup(const VOSC& renderer) {
    if (!std::exchange(loadedCatalog, true)) catalog.load();
    if (!detail()) return catalogMarkup();
    syncTargets(renderer);
    if (std::chrono::steady_clock::now() >= nextAssetCheck) checkAsset();
    return detailMarkup();
}

int BrowseView::height(bool compact) const {
    if (!detail()) return compact ? 264 : 468;
    const bool hasCommand = use && selectedTarget;
    if (compact) return hasCommand || preview ? 339 : 264;
    if (hasCommand) return preview ? 478 : 430;
    return preview ? 372 : 300;
}

} // namespace vosc::operator_ui
#endif
