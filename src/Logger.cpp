#include "Logger.h"
#include <cstdarg>
#include <cstdio>
#include <vector>

Logger::Logger() {}

Logger::Logger(const string & path, bool append) :
    fileLogger(path, append) {}

uint64_t Logger::append(ofLogLevel level, const string& module, const string& message) {
    string bounded = module.empty() ? message : module + ": " + message;
    if (bounded.size() > 2048) {
        size_t end = 2048;
        while (end > 0 && (static_cast<unsigned char>(bounded[end]) & 0xc0) == 0x80) --end;
        bounded.resize(end);
    }
    std::lock_guard<std::mutex> lock(linesMutex);
    lines.push_back({ofGetElapsedTimef(), bounded, ++sequence, level, module.substr(0, 128)});
    if (lines.size() > 512) lines.erase(lines.begin());
    return sequence;
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

void Logger::log(ofLogLevel level, const string &module, const string &message) {
    logEntry(level, module, message);
}

uint64_t Logger::logEntry(ofLogLevel level, const string& module, const string& message) {
    consoleLogger.log(level, module, message);
    const auto entry = level >= consoleLogLevel ? append(level, module, message) : 0;
    if (level >= fileLogLevel) fileLogger.log(level, module, message);
    return entry;
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
