#pragma once

#include "ofMain.h"
#include "ofxImGui.h"

struct Line {
    float time;
    string message;
    ofFloatColor color;
};

class Console {
public:
    static Console& get() {
        return instance;
    }
    void log(string msg, ofColor color) {
        lines.push_back({ofGetElapsedTimef(), msg, color});
    }
    void info(string msg) {
        log(msg, ofColor::white);
    }
    void error(string msg) {
        log(msg, ofColor::red);
    }
    bool hasNewErrors(float maxAge = 60) {
        return hasItems(ofColor::red, &index, maxAge);
    }
    void draw(bool* isOpen);
    
private:
    static Console instance;
    
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
    
    vector<Line> lines;
    ImGuiTextFilter Filter;
    int index = 0;
};
