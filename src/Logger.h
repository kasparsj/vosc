#pragma once

#include "ofMain.h"
#include "ofxImGui.h"

struct LoggerLine {
    float time;
    string message;
    ofFloatColor color;
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
    bool hasNewErrors(float maxAge = 60) {
        return hasItems(ofColor::red, &index, maxAge);
    }
    void draw(bool* isOpen);
    
private:
    bool hasItems(ofFloatColor color, int* startIndex, float maxAge = 60) {
        bool retVal = false;
        int lastIndex = (lines.size() - 1);
        for (int i=lastIndex; i>=(*startIndex); i--) {
            if (lines[i].time > (ofGetElapsedTimef() - maxAge) && lines[i].color == color) {
                retVal = true;
                break;
            }
        }
        (*startIndex) = (lastIndex+1);
        return retVal;
    }
    
    ofLogLevel consoleLogLevel = OF_LOG_VERBOSE;
    ofLogLevel fileLogLevel = OF_LOG_VERBOSE;
    ofConsoleLoggerChannel consoleLogger;
    ofFileLoggerChannel fileLogger;
    
    vector<LoggerLine> lines;
    ImGuiTextFilter Filter;
    int index = 0;
};
