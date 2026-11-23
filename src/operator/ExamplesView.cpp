#include "ExamplesView.hpp"

#if VOSC_OPERATOR_UI
#include "BrowseCatalog.hpp"
#include "PreviewProcess.hpp"
#include "ofJson.h"
#include <RmlUi/Core/StringUtilities.h>
#include <fstream>
#include <string_view>
#include <utility>

namespace vosc::operator_ui {
namespace {

constexpr auto manifestName = "examples/editable-scene/scene.vosc.json";
constexpr auto rowId = "example-row-editable-scene";
constexpr auto categoryName = "Recipe";
using State = PreviewProcess::State;

std::string escaped(const std::string& text) {
    return Rml::StringUtilities::EncodeRml(text);
}

bool active(State state) {
    return state == State::Starting || state == State::Ready || state == State::Stopping;
}

}

bool ExamplesView::matches() const {
    return (category.empty() || category == categoryName)
        && Rml::StringUtilities::ToLower(title).find(query) != std::string::npos;
}

bool ExamplesView::setFilters(std::string name, std::string kind) {
    name = Rml::StringUtilities::ToLower(std::move(name));
    if (name == query && kind == category) return false;
    query = std::move(name);
    category = std::move(kind);
    selected = false;
    catalogScroll = 0;
    return true;
}

bool ExamplesView::openItem(const std::string& id, float scroll) {
    if (detailOpen || id != rowId || !matches()) return false;
    detailOpen = selected = true;
    catalogScroll = scroll;
    return true;
}

std::string ExamplesView::backItem() {
    detailOpen = false;
    return rowId;
}

void ExamplesView::refresh() {
    const auto now = std::chrono::steady_clock::now();
    if (now < nextCheck) return;
    nextCheck = now + std::chrono::seconds(1);
    unavailable.clear();
    const auto manifest = stagedBrowseFile(manifestName);
    if (!manifest) {
        unavailable = "The packaged scene manifest is missing. Restage the renderer data.";
        return;
    }
    constexpr size_t limit = 64 * 1024;
    std::ifstream input(*manifest, std::ios::binary);
    std::string bytes(limit + 1, '\0');
    input.read(bytes.data(), bytes.size());
    bytes.resize(static_cast<size_t>(input.gcount()));
    const auto scene = ofJson::parse(bytes, nullptr, false);
    if (input.bad() || bytes.size() > limit || !scene.is_object() || !scene.contains("title")
        || !scene["title"].is_string() || scene["title"].get_ref<const std::string&>().empty()) {
        unavailable = "The packaged scene manifest could not be read. Restage the renderer data.";
        return;
    }
    title = scene["title"].get<std::string>();
    if (!stagedBrowseFile("examples/editable-scene/assets/solid.frag"))
        unavailable = "The packaged shader is missing. Restage the renderer data.";
}

std::string ExamplesView::summary() const {
    if (detailOpen) return {};
    return matches() ? "1 of 1 examples" : "0 of 1 examples";
}

std::string ExamplesView::previewMarkup(const PreviewProcess& preview) const {
    std::string status;
    std::string controls;
    if (detailOpen && !active(preview.state()) && preview.state() != State::Failed
        && unavailable.empty()) {
        controls = "<button id='preview-launch'>Open in Preview Window</button>";
    }
    switch (preview.state()) {
        case State::Idle:
            status = escaped(preview.diagnostic());
            break;
        case State::Starting:
            status = "Starting preview · " + escaped(preview.title());
            controls = "<button id='preview-cancel'>Cancel</button>";
            break;
        case State::Ready:
            status = "Preview ready · " + escaped(preview.title())
                + " · OSC 127.0.0.1:" + std::to_string(preview.port());
            controls = "<button id='preview-focus'>Focus Preview</button>"
                "<button id='preview-close'>Close Preview</button>";
            break;
        case State::Stopping:
            status = "Closing preview · " + escaped(preview.title());
            break;
        case State::Failed:
            status = "<span class='error-summary'>Preview failed · " + escaped(preview.title())
                + "</span><br />" + escaped(preview.diagnostic());
            if (unavailable.empty()) controls = "<button id='preview-retry'>Retry</button>";
            break;
    }
    if (status.empty() && controls.empty()) return {};
    return "<div class='example-preview-actions'>" + controls
        + (status.empty() ? "" : "<p class='message'>" + status + "</p>") + "</div>";
}

std::string ExamplesView::markup(const PreviewProcess& preview) const {
    if (!detailOpen) {
        auto result = previewMarkup(preview);
        if (!matches()) return result
            + "<p class='message'>No examples match this search and category.</p>"
              "<button id='example-clear'>Clear search and category</button>";
        return result + "<button id='" + rowId + "' class='inspect-row asset-row"
            + (selected ? " selected" : "")
            + "'><span class='inspect-row-label'>" + escaped(title)
            + "</span><span class='inspect-kind'>" + categoryName
            + "</span><img class='browse-chevron' src='browse:catalog/chevron.png' /></button>";
    }

    std::string result = "<div class='inspect-label'>" + escaped(title) + "</div>"
        "<div class='example-placeholder'>Recipe · no saved frame</div>"
        "<p class='browse-requirements'>Plays immediately with its bundled solid shader.</p>"
        "<p class='browse-origin'>" + manifestName + "</p>"
        "<p class='browse-origin'>Companion scene.scd uses VisualsQuark replay API 1 with a separate owned renderer.</p>";
    if (!unavailable.empty()) result += "<p class='message error'>" + escaped(unavailable) + "</p>";
    return result + previewMarkup(preview);
}

bool ExamplesView::activate(const std::string& id, PreviewProcess& preview) {
    switch (preview.state()) {
        case State::Starting:
            if (id != "preview-cancel") return false;
            preview.stop();
            return true;
        case State::Ready:
            if (id == "preview-close") preview.stop();
            else if (id == "preview-focus") preview.focus();
            else return false;
            return true;
        case State::Stopping:
            return false;
        case State::Idle:
            if (!detailOpen || id != "preview-launch") return false;
            break;
        case State::Failed:
            if (id != "preview-retry") return false;
            break;
    }
    nextCheck = {};
    refresh();
    if (unavailable.empty()) {
        if (const auto manifest = stagedBrowseFile(manifestName)) preview.start(*manifest, title);
        else unavailable = "The packaged scene manifest is missing. Restage the renderer data.";
    }
    return true;
}

const char* ExamplesView::previewFocus(const PreviewProcess& preview) const {
    switch (preview.state()) {
        case State::Starting: return "preview-cancel";
        case State::Ready: return "preview-focus";
        case State::Failed:
            if (unavailable.empty()) return "preview-retry";
            break;
        case State::Idle:
            if (detailOpen && unavailable.empty()) return "preview-launch";
            break;
        case State::Stopping: break;
    }
    return detailOpen ? "back" : "browse-example-search";
}

int ExamplesView::height(bool compact, const PreviewProcess& preview) const {
    if (!detailOpen) return compact ? 264 : 468;
    if (preview.state() == State::Failed || !unavailable.empty()) return compact ? 339 : 370;
    return compact ? 313 : 316;
}

} // namespace vosc::operator_ui
#endif
