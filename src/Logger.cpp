#include "Logger.h"

Logger::Logger() {}

Logger::Logger(const string & path, bool append) :
    fileLogger(path, append) {}

void Logger::log(ofLogLevel level, const string &module, const string &message) {
    consoleLogger.log(level, module, message);
    if (level >= consoleLogLevel) lines.push_back({ofGetElapsedTimef(), message, toColor(level)});
    if (level >= fileLogLevel) fileLogger.log(level, module, message);
}

void Logger::log(ofLogLevel level, const string & module, const char* format, ...){
    va_list args;
    va_start(args, format);
    log(level, module, format, args);
    va_end(args);
}

void Logger::log(ofLogLevel level, const string & module, const char* format, va_list args) {
    consoleLogger.log(level, module, format, args);
    if (level >= consoleLogLevel) lines.push_back({ofGetElapsedTimef(), module + ": " + format, toColor(level)});
    if (level >= fileLogLevel) fileLogger.log(level, module, format, args);
}

void Logger::draw(bool* isOpen) {
    ImGui::SetNextWindowSize(ofVec2f(ofGetWidth(),200));
    ImGui::SetNextWindowPos(ofVec2f(0,ofGetHeight()-200));
    if (!ImGui::Begin("Console", isOpen, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::End();
    }
    
    if (ImGui::SmallButton("Clear")) { lines.clear(); }
    ImGui::SameLine();
    bool copy_to_clipboard = ImGui::SmallButton("Copy");

    ImGui::Separator();

    const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing(); // 1 separator, 1 input text
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar); // Leave room for 1 separator + 1 InputText
    if (ImGui::BeginPopupContextWindow())
    {
        if (ImGui::Selectable("Clear")) {
            lines.clear();
        }
        ImGui::EndPopup();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4,1)); // Tighten spacing
    if (copy_to_clipboard) {
        ImGui::LogToClipboard();
    }
    for (int i = 0; i < lines.size(); i++)
    {
        LoggerLine item = lines[i];
        string msg = item.message;
        if (!Filter.PassFilter(msg.c_str()))
            continue;

        // Normally you would store more information in your item (e.g. make Items[] an array of structure, store color/type etc.)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(item.color.r, item.color.g, item.color.b, item.color.a));
        ImGui::TextUnformatted(msg.c_str());
        ImGui::PopStyleColor();
    }
    if (copy_to_clipboard)
        ImGui::LogFinish();

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);

    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::Separator();
    
    ImGui::End();
}
