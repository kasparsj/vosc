#pragma once

#include "../Config.h"

#if VOSC_OPERATOR_UI
#include "ExampleCatalog.hpp"
#include <chrono>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace vosc::operator_ui {

class PreviewProcess;

class ExamplesView {
public:
    bool detail() const { return !item.empty(); }
    bool openItem(const std::string& id, float scroll);
    std::string backItem();
    float originScroll() const { return catalogScroll; }
    bool setFilters(std::string name, std::string kind);
    void refresh();
    std::optional<std::string> update();
    void rememberInput(std::string value);
    std::string inputValue() const;
    std::string summary() const;
    std::string markup(const PreviewProcess& preview) const;
    std::optional<std::string> activate(const std::string& id, PreviewProcess& preview);
    const char* previewFocus(const PreviewProcess& preview) const;
    int height(bool compact) const;

private:
    struct InputState {
        std::filesystem::path clip;
        std::string draft, feedback;
        bool choosing = false;
    };
    struct Catalog {
        std::vector<ExampleEntry> rows;
        std::string error;
    };
    struct ClipRequest;

    bool matches(const ExampleEntry& example) const;
    const ExampleEntry* find(const std::string& id) const;
    const InputState& media(const ExampleEntry& example) const;
    std::string blockReason(const ExampleEntry& example) const;
    std::string detailMarkup(const ExampleEntry& example, const PreviewProcess& preview) const;
    std::string previewMarkup(const PreviewProcess& preview) const;
    void requestCatalog();
    void selectClip(const std::string& example, const std::filesystem::path& path);
    void launch(const ExampleEntry& example, PreviewProcess& preview);

    Catalog catalog;
    ExampleDirectories directories;
    std::future<Catalog> pendingCatalog;
    std::chrono::steady_clock::time_point nextCatalogCheck{};
    std::map<std::string, InputState> inputs;
    std::shared_ptr<ClipRequest> clipRequest;
    std::string item, selected, launched, query, category;
    std::string launchIssue, revealFeedback;
    float catalogScroll = 0;
    bool loaded = false;
};

} // namespace vosc::operator_ui
#endif
