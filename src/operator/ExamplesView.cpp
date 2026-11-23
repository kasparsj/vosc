#include "ExamplesView.hpp"

#if VOSC_OPERATOR_UI
#include "ExampleFiles.hpp"
#include "PreviewProcess.hpp"
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <string_view>
#include <utility>

#if defined(__linux__)
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#endif

#if defined(__linux__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 34)
#define VOSC_EXAMPLE_REVEAL_LINUX 1
extern char** environ;
#endif
#endif

namespace vosc::operator_ui {
namespace {
namespace fs = std::filesystem;
using State = PreviewProcess::State;
constexpr std::string_view rowPrefix = "example-row-";

std::string escaped(const std::string& text) {
    return Rml::StringUtilities::EncodeRml(text);
}

std::string note(const std::string& text, const char* kind = "browse-origin") {
    return text.empty() ? "" : "<p class='" + std::string(kind) + "'>" + escaped(text) + "</p>";
}

bool active(State state) {
    return state == State::Starting || state == State::Ready || state == State::Stopping;
}

std::optional<fs::path> readableClip(const fs::path& selection) {
    if (!selection.is_absolute()) return std::nullopt;
    std::error_code error;
    const auto path = fs::canonical(selection, error);
    if (error || !fs::is_regular_file(path, error) || error) return std::nullopt;
    const auto extension = Rml::StringUtilities::ToLower(path.extension().string());
    const auto name = path.string();
    if ((extension != ".mov" && extension != ".mp4") || name.size() > 2048 || name.find(':') != std::string::npos
        || !std::ifstream(path, std::ios::binary)) return std::nullopt;
    return path;
}

bool cameraIndex(const std::string& selection) {
    int value = 0;
    const auto* end = selection.data() + selection.size();
    const auto parsed = std::from_chars(selection.data(), end, value);
    return selection.size() <= 2 && parsed.ec == std::errc{} && parsed.ptr == end && value >= 0 && value <= 63
        && selection.front() >= '0' && selection.front() <= '9';
}

#if !defined(__APPLE__)
bool revealExampleFile(const fs::path& path) {
#if defined(VOSC_EXAMPLE_REVEAL_LINUX)
    // The file manager receives only the containing directory. Do not inherit the renderer's sockets.
    posix_spawn_file_actions_t actions;
    if (::posix_spawn_file_actions_init(&actions)) return false;
    const auto closeResult = ::posix_spawn_file_actions_addclosefrom_np(&actions, 3);
    std::string directory = path.parent_path().string();
    char executable[] = "xdg-open";
    char* args[] = {executable, directory.data(), nullptr};
    pid_t opener = -1;
    const auto result = closeResult ? closeResult : ::posix_spawnp(&opener, executable, &actions, nullptr, args, environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (result) return false;
    std::thread([opener] {
        while (::waitpid(opener, nullptr, 0) < 0 && errno == EINTR) {}
    }).detach();
    return true;
#else
    (void)path;
    return false;
#endif
}
#endif

}

struct ExamplesView::ClipRequest {
#if defined(__APPLE__)
    std::string item;
    std::optional<ClipPickResult> result;
    fs::path path;
#endif
};

const ExampleEntry* ExamplesView::find(const std::string& id) const {
    const auto match = std::find_if(catalog.rows.begin(), catalog.rows.end(), [&](const auto& entry) {
        return entry.id == id;
    });
    return match == catalog.rows.end() ? nullptr : &*match;
}

const ExamplesView::InputState& ExamplesView::media(const ExampleEntry& example) const {
    static const InputState empty;
    const auto found = inputs.find(example.id);
    return found == inputs.end() ? empty : found->second;
}

bool ExamplesView::matches(const ExampleEntry& example) const {
    return (category.empty() || category == example.category)
        && Rml::StringUtilities::ToLower(example.title).find(query) != std::string::npos;
}

bool ExamplesView::setFilters(std::string name, std::string kind) {
    name = Rml::StringUtilities::ToLower(std::move(name));
    if (name == query && kind == category) return false;
    query = std::move(name);
    category = std::move(kind);
    selected.clear();
    catalogScroll = 0;
    return true;
}

bool ExamplesView::openItem(const std::string& id, float scroll) {
    if (detail() || id.compare(0, rowPrefix.size(), rowPrefix) != 0) return false;
    const auto* example = find(id.substr(rowPrefix.size()));
    if (!example || !matches(*example)) return false;
    selected = item = example->id;
    catalogScroll = scroll;
    revealFeedback.clear();
    return true;
}

std::string ExamplesView::backItem() {
    return std::string(rowPrefix) + std::exchange(item, {});
}

void ExamplesView::requestCatalog() {
    if (pendingCatalog.valid()) return;
    nextCatalogCheck = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    directories = exampleDirectories();
    auto load = [roots = directories] {
        Catalog fresh;
        fresh.rows = readExamples(roots, fresh.error);
        return fresh;
    };
    try {
        pendingCatalog = std::async(std::launch::async, load);
    } catch (const std::system_error&) {
        catalog.error = "Could not start the catalog scan. Refresh examples to retry.";
        loaded = true;
    }
}

void ExamplesView::publishCatalog(Catalog snapshot) {
    const auto sameInput = [&](const std::string& id) {
        const auto* previous = find(id);
        const auto current = std::find_if(snapshot.rows.begin(), snapshot.rows.end(), [&](const auto& entry) {
            return entry.id == id;
        });
        return previous && current != snapshot.rows.end()
            && previous->inputKind == current->inputKind && previous->inputName == current->inputName;
    };
    for (auto input = inputs.begin(); input != inputs.end();) {
        if (sameInput(input->first)) ++input;
        else input = inputs.erase(input);
    }
#if defined(__APPLE__)
    if (clipRequest && !sameInput(clipRequest->item)) clipRequest.reset();
#endif
    catalog = std::move(snapshot);
}

void ExamplesView::refresh() {
    if (pendingCatalog.valid()) {
        if (pendingCatalog.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        try {
            publishCatalog(pendingCatalog.get());
        } catch (const std::exception&) {
            catalog.error = "Could not refresh examples. Showing the previous catalog.";
        }
        loaded = true;
    }
    if (std::chrono::steady_clock::now() >= nextCatalogCheck) requestCatalog();
}

void ExamplesView::selectClip(const std::string& example, const fs::path& path) {
    auto& input = inputs[example];
    if (const auto clip = readableClip(path)) {
        input.clip = *clip;
        input.draft = clip->string();
        input.feedback.clear();
        input.choosing = false;
        if (example == launched) launchIssue.clear();
    } else {
        input.feedback = "Choose a readable local MOV or MP4.";
    }
}

std::optional<std::string> ExamplesView::update() {
#if defined(__APPLE__)
    if (clipRequest && clipRequest->result) {
        auto finished = std::exchange(clipRequest, {});
        auto& input = inputs[finished->item];
        switch (*finished->result) {
            case ClipPickResult::Chosen:
                selectClip(finished->item, finished->path);
                break;
            case ClipPickResult::Cancelled:
                input.feedback = "Clip selection cancelled.";
                break;
            case ClipPickResult::Unavailable:
                input.feedback = "The clip picker could not open a local file. Try Choose clip again.";
                break;
        }
        if (item == finished->item) return "example-choose-clip";
    }
#endif
    return std::nullopt;
}

void ExamplesView::rememberInput(std::string value) {
    const auto* example = find(item);
    if (!example) return;
    if (example->inputKind == "camera") inputs[example->id].cameraOverride = std::move(value);
    else if (example->inputKind == "video" && media(*example).choosing) inputs[example->id].draft = std::move(value);
}

std::string ExamplesView::inputValue() const {
    const auto* example = find(item);
    if (!example) return {};
    const auto& input = media(*example);
    return example->inputKind == "camera" ? input.cameraOverride.value_or(example->inputDefault) : input.draft;
}

std::string ExamplesView::summary() const {
    if (detail() || catalog.rows.empty()) return {};
    const auto count = std::count_if(catalog.rows.begin(), catalog.rows.end(), [&](const auto& example) {
        return matches(example);
    });
    return std::to_string(count) + " of " + std::to_string(catalog.rows.size()) + " examples";
}

std::string ExamplesView::blockReason(const ExampleEntry& example) const {
    if (!example.blocked.empty()) return example.blocked;
    if (!PreviewProcess::supported()) return "Preview windows are unavailable in this build.";
    if (example.inputKind == "video" && media(example).clip.empty()) return "Choose a local MOV or MP4 to preview Video.";
    return {};
}

std::string ExamplesView::previewMarkup(const PreviewProcess& preview) const {
    std::string status, controls;
    const auto* example = find(item);
    if (example && !active(preview.state()) && (preview.state() != State::Failed || item != launched)
        && blockReason(*example).empty()) {
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
            if (const auto* target = find(launched); target && blockReason(*target).empty())
                controls += "<button id='preview-retry'>Retry</button>";
            break;
    }
    if (!launchIssue.empty()) status += (status.empty() ? "" : "<br />") + escaped(launchIssue);
    if (status.empty() && controls.empty()) return {};
    return "<div class='example-preview-actions'>" + controls
        + (status.empty() ? "" : "<p class='message'>" + status + "</p>") + "</div>";
}

std::string ExamplesView::detailMarkup(const ExampleEntry& example, const PreviewProcess& preview) const {
    std::string result = "<div class='inspect-label'>" + escaped(example.title) + "</div>";
    if (!example.preview.empty())
        result += "<div class='browse-preview'><img alt='Saved example preview' src=\"example:" + escaped(example.preview) + "\" /></div>";
    else
        result += "<div class='example-placeholder'>" + escaped(example.category) + " · no saved frame</div>";

    result += note(example.summary, "browse-requirements") + note(example.requirements) + note(example.cues);
    const auto& input = media(example);
    if (example.inputKind == "video") {
        const char* label = input.clip.empty() ? "Choose clip" : "Change clip";
        result += "<div class='example-input-actions'><button id='example-choose-clip'"
            + std::string(clipRequest || input.choosing ? " class='inactive'" : "") + ">" + label + "</button>";
        if (input.choosing) {
            result += "<div class='example-input-label'>Absolute path to a local MOV or MP4</div>"
                "<input type='text' id='example-input' class='example-input' maxlength='2048' aria-label='Local clip path' />"
                "<button id='example-use-clip'>Use clip</button><button id='example-cancel-clip'>Cancel</button>";
        }
        result += "</div>" + note(input.clip.empty() ? "" : input.clip.string()) + note(input.feedback);
    } else if (example.inputKind == "camera") {
        result += "<div class='example-input-actions'><label for='example-input'>Camera device index (0–63)</label>"
            "<input type='text' id='example-input' class='example-input camera-index' maxlength='2' "
            "aria-label='Camera device index' />"
            "</div>" + note(input.feedback);
    } else if (example.inputKind == "html") {
        result += note("Destination · " + example.inputDefault);
    }
    const auto blocked = blockReason(example);
    if (!blocked.empty()) {
        result += note(blocked, "message error");
        if (!example.blocked.empty() && example.inputKind != "html")
            result += "<button id='example-refresh'>Refresh examples</button>";
    }

    result += previewMarkup(preview);
    result += "<div class='example-files'>" + note("examples/" + example.manifest);
    if (!example.manifest.empty()) result += "<button id='example-reveal-scene'>Reveal scene file</button>";
    if (!example.companion.empty()) result += "<button id='example-reveal-companion'>Reveal companion file</button>";
    result += note(revealFeedback) + "</div>";

    if (example.attach == "attach" && example.id == launched && preview.state() == State::Ready) {
        result += note("SuperCollider · Open the saved companion. Set ~rendererHost = \"127.0.0.1\" and ~rendererPort = "
            + std::to_string(preview.port()) + ". Evaluate only the Attach block before the live cues. Setup resets its target.");
        const auto path = directories.examples / fs::path(example.manifest).parent_path();
        result += note("If evaluating from an unsaved buffer, set ~rendererRoot to this folder · " + path.string());
    } else if (example.attach == "replay") {
        result += note("The SuperCollider replay companion configures a separate blank renderer. It has no Attach block.");
    } else if (example.attach == "audio") {
        result += note("The audio analysis companion uses port 33333. See the Sound Data guide for separate SuperCollider setup. The script needs endpoint edits to attach to this preview.");
    }
    return result;
}

std::string ExamplesView::markup(const PreviewProcess& preview) const {
    if (const auto* example = find(item)) return detailMarkup(*example, preview);
    if (detail())
        return note("Example no longer available. The scene may have moved or been removed.", "message error")
            + previewMarkup(preview);

    auto result = previewMarkup(preview);
    if (!loaded) result += note("Loading examples…", "message");
    if (!catalog.error.empty())
        result += note(catalog.error, "message error") + "<button id='example-refresh'>Refresh examples</button>";
    unsigned count = 0;
    for (const auto& example : catalog.rows) {
        if (!matches(example)) continue;
        ++count;
        result += "<button id='" + std::string(rowPrefix) + escaped(example.id) + "' class='inspect-row asset-row example-row"
            + (selected == example.id ? " selected" : "") + "'>";
        if (!example.preview.empty())
            result += "<img class='example-thumb' src=\"example:" + escaped(example.preview) + "\" />";
        result += "<span class='inspect-row-label'>" + escaped(example.title)
            + "</span><span class='inspect-kind'>" + escaped(example.category)
            + "</span><img class='browse-chevron' src='browse:catalog/chevron.png' /></button>";
    }
    if (!count && !catalog.rows.empty())
        result += "<p class='message'>No examples match this search and category.</p>"
            "<button id='example-clear'>Clear search and category</button>";
    else if (loaded && catalog.rows.empty() && catalog.error.empty())
        result += note("No examples found.", "message");
    return result;
}

void ExamplesView::launch(const ExampleEntry& example, PreviewProcess& preview) {
    if (active(preview.state())) return;
    const auto blocked = blockReason(example);
    if (!blocked.empty()) {
        launchIssue = blocked;
        return;
    }
    const auto manifest = resolveExampleFile(directories.examples, example.manifest);
    if (!manifest) {
        launchIssue = "The packaged scene file is missing. Restage the renderer data, then retry.";
        return;
    }
    std::map<std::string, std::string> arguments;
    if (example.inputKind == "video") {
        const auto clip = readableClip(media(example).clip);
        if (!clip) {
            launchIssue = "The selected clip is unavailable. Choose a readable MOV or MP4.";
            return;
        }
        arguments.emplace(example.inputName, clip->string());
    } else if (example.inputKind == "camera") {
        auto& input = inputs[example.id];
        auto index = input.cameraOverride.value_or(example.inputDefault);
        if (!cameraIndex(index)) {
            input.feedback = "Enter a camera device index from 0 to 63.";
            return;
        }
        input.feedback.clear();
        arguments.emplace(example.inputName, std::move(index));
    }
    launchIssue.clear();
    launched = example.id;
    preview.start(*manifest, example.title, arguments);
}

std::optional<std::string> ExamplesView::activate(const std::string& id, PreviewProcess& preview) {
    if (id == "preview-focus" && preview.state() == State::Ready) {
        preview.focus();
        return previewFocus(preview);
    }
    if ((id == "preview-close" && preview.state() == State::Ready)
        || (id == "preview-cancel" && preview.state() == State::Starting)) {
        preview.stop();
        return previewFocus(preview);
    }
    if (id == "preview-retry" && preview.state() == State::Failed) {
        if (const auto* attempt = find(launched)) launch(*attempt, preview);
        return previewFocus(preview);
    }
    if (id == "example-refresh") {
        requestCatalog();
        return detail() ? "back" : "browse-example-search";
    }
    const auto* example = find(item);
    if (!example) return std::nullopt;
    if (id == "preview-launch") {
        launch(*example, preview);
        if (example->inputKind == "camera" && !media(*example).feedback.empty()) return "example-input";
        return previewFocus(preview);
    }
    if (id == "example-reveal-scene" || id == "example-reveal-companion") {
        const auto path = resolveExampleFile(directories.examples,
            id == "example-reveal-scene" ? example->manifest : example->companion);
        revealFeedback = !path ? "The packaged file is missing. Restage the renderer data."
            : revealExampleFile(*path) ? "" : "The file browser could not open. Use the staged path shown above.";
        return id;
    }
    if (example->inputKind != "video") return std::nullopt;
    if (id == "example-choose-clip") {
#if defined(__APPLE__)
        if (clipRequest) return id;
        clipRequest = std::make_shared<ClipRequest>();
        clipRequest->item = item;
        chooseExampleClip([request = std::weak_ptr(clipRequest)](ClipPickResult result, fs::path path) {
            if (auto current = request.lock()) {
                current->path = std::move(path);
                current->result = result;
            }
        });
        return id;
#else
        auto& input = inputs[item];
        input.choosing = true;
        input.draft = input.clip.string();
        input.feedback.clear();
        return "example-input";
#endif
    }
    if (id == "example-use-clip" && media(*example).choosing) {
        selectClip(item, media(*example).draft);
        return media(*example).choosing ? "example-input" : "example-choose-clip";
    }
    if (id == "example-cancel-clip" && media(*example).choosing) {
        auto& input = inputs[item];
        input.choosing = false;
        input.feedback = "Clip selection cancelled.";
        return "example-choose-clip";
    }
    return std::nullopt;
}

const char* ExamplesView::previewFocus(const PreviewProcess& preview) const {
    switch (preview.state()) {
        case State::Starting: return "preview-cancel";
        case State::Ready: return "preview-focus";
        case State::Failed:
            if (const auto* attempt = find(launched); attempt && blockReason(*attempt).empty()) return "preview-retry";
            break;
        case State::Idle:
            if (const auto* example = find(item); example && blockReason(*example).empty()) return "preview-launch";
            break;
        case State::Stopping: break;
    }
    return detail() ? "back" : "browse-example-search";
}

int ExamplesView::height(bool compact) const {
    if (!detail()) return compact ? 264 : 468;
    return compact ? 359 : 404;
}

} // namespace vosc::operator_ui
#endif
