#include "Logger.h"
#include "Config.h"
#include <cstdarg>
#include <cstdio>
#include <vector>

Logger::Logger() {}

Logger::Logger(const string & path, bool append) :
    fileLogger(path, append) {}

void Logger::append(ofLogLevel level, const string& module, const string& message) {
    string bounded = module.empty() ? message : module + ": " + message;
    if (bounded.size() > 2048) {
        size_t end = 2048;
        while (end > 0 && (static_cast<unsigned char>(bounded[end]) & 0xc0) == 0x80) --end;
        bounded.resize(end);
    }
    std::lock_guard<std::mutex> lock(linesMutex);
    lines.push_back({ofGetElapsedTimef(), bounded, toColor(level), ++sequence, level, module.substr(0, 128)});
    if (lines.size() > 512) lines.erase(lines.begin());
}

uint64_t Logger::latestSequence() const {
    std::lock_guard<std::mutex> lock(linesMutex);
    return sequence;
}

vector<LoggerLine> Logger::entriesSince(uint64_t after, size_t limit, bool& gap, uint64_t* latest) const {
    std::lock_guard<std::mutex> lock(linesMutex);
    if (latest) *latest = sequence;
    gap = after < sequence && (lines.empty() || after < lines.front().sequence - 1);
    vector<LoggerLine> result;
    for (const auto& line : lines) {
        if (line.sequence > after && result.size() < limit) result.push_back(line);
    }
    return result;
}

bool Logger::hasNewErrors(float maxAge) {
    std::lock_guard<std::mutex> lock(linesMutex);
    bool found = false;
    for (const auto& line : lines) {
        if (line.sequence > errorCursor && line.level == OF_LOG_ERROR &&
            line.time > ofGetElapsedTimef() - maxAge) found = true;
    }
    errorCursor = sequence;
    return found;
}

void Logger::log(ofLogLevel level, const string &module, const string &message) {
    consoleLogger.log(level, module, message);
    if (level >= consoleLogLevel) append(level, module, message);
    if (level >= fileLogLevel) fileLogger.log(level, module, message);
}

void Logger::log(ofLogLevel level, const string & module, const char* format, ...){
    va_list args;
    va_start(args, format);
    log(level, module, format, args);
    va_end(args);
}

void Logger::log(ofLogLevel level, const string & module, const char* format, va_list args) {
    // Format the message using vsnprintf
    va_list args_copy;
    va_copy(args_copy, args);
    int size = vsnprintf(nullptr, 0, format, args_copy) + 1;
    va_end(args_copy);
    
    if (size > 0) {
        std::vector<char> buffer(size);
        vsnprintf(buffer.data(), size, format, args);
        string message = string(buffer.data());
        
        log(level, module, message);
    }
}

void Logger::draw(bool* isOpen) {
#if USE_OFX_IMGUI
    ImGui::SetNextWindowSize(ofVec2f(ofGetWidth(),200));
    ImGui::SetNextWindowPos(ofVec2f(0,ofGetHeight()-200));
    if (!ImGui::Begin("Console", isOpen, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::End();
    }
    
    if (ImGui::SmallButton("Clear")) {
        std::lock_guard<std::mutex> lock(linesMutex);
        lines.clear();
    }
    ImGui::SameLine();
    bool copy_to_clipboard = ImGui::SmallButton("Copy");

    ImGui::Separator();

    const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing(); // 1 separator, 1 input text
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar); // Leave room for 1 separator + 1 InputText
    if (ImGui::BeginPopupContextWindow())
    {
        if (ImGui::Selectable("Clear")) {
            std::lock_guard<std::mutex> lock(linesMutex);
            lines.clear();
        }
        ImGui::EndPopup();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4,1)); // Tighten spacing
    if (copy_to_clipboard) {
        ImGui::LogToClipboard();
    }
    bool gap;
    const auto visibleLines = entriesSince(0, 512, gap);
    for (int i = 0; i < visibleLines.size(); i++)
    {
        LoggerLine item = visibleLines[i];
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
#endif
}
