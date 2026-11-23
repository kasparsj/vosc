#pragma once

#include "../Config.h"

#if VOSC_OPERATOR_UI
#include "ExampleCatalog.hpp"
#include "../scene/SceneFile.hpp"
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace vosc::operator_ui {

class ExamplesView {
public:
    using LoadRequest = std::function<bool(std::filesystem::path, scene::Inputs)>;

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
    std::string markup(unsigned int oscPort, bool sceneIsCurrent) const;
    std::optional<std::string> activate(const std::string& id, const LoadRequest& request);
    void finishLoad(bool success, std::string detail);
    int height(bool compact) const;

private:
    struct InputState {
        std::filesystem::path clip;
        std::optional<std::string> cameraOverride;
        std::string draft, feedback;
        bool choosing = false;
    };
    struct Catalog {
        std::vector<ExampleEntry> rows;
        std::string error;
    };
    struct ClipRequest;
    struct LoadAttempt {
        std::string item;
        std::string issue;
        bool pending = false;
    };

    bool matches(const ExampleEntry& example) const;
    const ExampleEntry* find(const std::string& id) const;
    const InputState& media(const ExampleEntry& example) const;
    std::string blockReason(const ExampleEntry& example) const;
    std::string detailMarkup(const ExampleEntry& example, unsigned int oscPort, bool sceneIsCurrent) const;
    void requestCatalog();
    void publishCatalog(Catalog snapshot);
    void selectClip(const std::string& example, const std::filesystem::path& path);
    void load(const ExampleEntry& example, const LoadRequest& request);

    Catalog catalog;
    ExampleDirectories directories;
    std::future<Catalog> pendingCatalog;
    std::chrono::steady_clock::time_point nextCatalogCheck{};
    std::map<std::string, InputState> inputs;
    std::shared_ptr<ClipRequest> clipRequest;
    LoadAttempt loadAttempt;
    std::string item, selected, activeExample, query, category;
    std::string revealFeedback;
    float catalogScroll = 0;
    bool loaded = false;
};

} // namespace vosc::operator_ui
#endif
