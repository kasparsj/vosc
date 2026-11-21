#pragma once

#include "ofMain.h"
#include "Config.h"
#include <cstdint>
#include <mutex>
#if USE_OFX_IMGUI
#include "ofxImGui.h"
#endif

struct LoggerLine {
    float time;
    string message;
    ofFloatColor color;
    uint64_t sequence;
    ofLogLevel level;
    string module;
};

class Logger : public ofBaseLoggerChannel {
public:
    static ofFloatColor toColor(ofLogLevel level) {
        ofFloatColor color = ofFloatColor(1.f);
        switch (level) {
            case OF_LOG_ERROR:
                color = ofColor::red;
                break;
            case OF_LOG_WARNING:
                color = ofColor::orange;
                break;
        }
        return color;
    }
    
    Logger();
    Logger(const string & path, bool append);
    
    void log(ofLogLevel level, const string & module, const string & message);
    void log(ofLogLevel level, const string & module, const char* format, ...) OF_PRINTF_ATTR(4, 5);
    void log(ofLogLevel level, const string & module, const char* format, va_list args);
    bool hasNewErrors(float maxAge = 60);
    uint64_t latestSequence() const;
    vector<LoggerLine> entriesSince(uint64_t after, size_t limit, bool& gap,
                                  uint64_t* latest = nullptr) const;
    void draw(bool* isOpen);
    
private:
    void append(ofLogLevel level, const string& module, const string& message);
    
    ofLogLevel consoleLogLevel = OF_LOG_VERBOSE;
    ofLogLevel fileLogLevel = OF_LOG_SILENT;
    ofConsoleLoggerChannel consoleLogger;
    ofFileLoggerChannel fileLogger;
    
    vector<LoggerLine> lines;
#if USE_OFX_IMGUI
    ImGuiTextFilter Filter;
#endif
    mutable std::mutex linesMutex;
    uint64_t sequence = 0;
    uint64_t errorCursor = 0;
};
