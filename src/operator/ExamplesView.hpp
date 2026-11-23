#pragma once

#include "../Config.h"

#if VOSC_OPERATOR_UI
#include <chrono>
#include <string>

namespace vosc::operator_ui {

class PreviewProcess;

class ExamplesView {
public:
    bool detail() const { return detailOpen; }
    bool openItem(const std::string& id, float scroll);
    std::string backItem();
    float originScroll() const { return catalogScroll; }
    bool setFilters(std::string name, std::string kind);
    void refresh();
    std::string summary() const;
    std::string markup(const PreviewProcess& preview) const;
    bool activate(const std::string& id, PreviewProcess& preview);
    const char* previewFocus(const PreviewProcess& preview) const;
    int height(bool compact, const PreviewProcess& preview) const;

private:
    bool matches() const;
    std::string previewMarkup(const PreviewProcess& preview) const;

    std::string title = "Editable Scene";
    std::string query, category, unavailable;
    std::chrono::steady_clock::time_point nextCheck{};
    float catalogScroll = 0;
    bool detailOpen = false, selected = false;
};

} // namespace vosc::operator_ui
#endif
