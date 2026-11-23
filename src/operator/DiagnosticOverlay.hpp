#pragma once

#include "../Config.h"

#if VOSC_OPERATOR_UI
#include "../Logger.h"
#include "../VOSC.hpp"
#include <map>
#include <memory>
#include <optional>
#include <set>

class DiagnosticOverlay {
public:
    DiagnosticOverlay();
    ~DiagnosticOverlay();
    void setup(VOSC& renderer);
    void rememberScene(const scene::File& scene, const VOSC& renderer);
    void draw(VOSC& renderer, const Logger& logger);
    bool isOpen() const { return open; }
    void shutdown();

private:
    enum class View { Overview, Inspector, Console, Browse, Count };
    struct Surface;
    struct CleanClick {
        glm::vec2 eventPosition;
        glm::dvec2 cursorPosition;
    };

    void show(View next, const std::string& focus);
    void close();
    void showDiagnostic(uint64_t sequence);
    bool keyPressed(ofKeyEventArgs& event);
    bool pointerEvent(ofMouseEventArgs& event, const VOSC& renderer);
    void activate(VOSC& renderer, const Logger& logger, const std::string& id, uint64_t target);
    void refreshEntries(const Logger& logger);
    void refreshMatches();
    void syncConsoleControls();
    const LoggerLine* diagnostic(uint64_t sequence) const;
    std::string overviewMarkup(const VOSC& renderer, const GLint* viewport);
    std::string shaderDiagnostic(const VOSC& renderer) const;

    std::unique_ptr<Surface> surface;
    ofEventListeners listeners;
    std::set<int> ownedKeys;
    std::map<int, int> ownedButtons;
    unsigned physicalButtons = 0;
    std::optional<CleanClick> cleanClick;
    bool open = false;
    View view = View::Overview;
    std::string sceneTitle;
    std::vector<std::weak_ptr<Layer>> sceneLayers;
    std::vector<LoggerLine> entries;
    uint64_t clearCursor = 0;
    uint64_t cachedSequence = 0;
    uint64_t selectedSequence = 0;
    bool logGap = false;
};
#endif
