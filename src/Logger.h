#pragma once

#include "ofMain.h"
#include <cstdint>
#include <mutex>

struct LoggerLine {
    float time;
    string message;
    uint64_t sequence;
    ofLogLevel level;
    string module;
};

class Logger : public ofBaseLoggerChannel {
public:
    Logger();
    Logger(const string& path, bool append);

    void log(ofLogLevel level, const string& module, const string& message) override;
    void log(ofLogLevel level, const string& module, const char* format, ...) OF_PRINTF_ATTR(4, 5);
    void log(ofLogLevel level, const string& module, const char* format, va_list args);
    uint64_t logEntry(ofLogLevel level, const string& module, const string& message);
    uint64_t latestSequence() const;
    vector<LoggerLine> entriesSince(uint64_t after, size_t limit, bool& gap,
                                  uint64_t* latest = nullptr) const;

private:
    uint64_t append(ofLogLevel level, const string& module, const string& message);
    ofLogLevel consoleLogLevel = OF_LOG_VERBOSE;
    ofLogLevel fileLogLevel = OF_LOG_SILENT;
    ofConsoleLoggerChannel consoleLogger;
    ofFileLoggerChannel fileLogger;
    vector<LoggerLine> lines;
    mutable std::mutex linesMutex;
    uint64_t sequence = 0;
};
