#pragma once

#include "ofMain.h"
#include "ofxOscMessage.h"

class Args {
public:
    static bool isHexColor(const string& str);
    static bool isURL(const string& str);
    static bool isJSON(const string& str);
    static bool isJSONObj(const string& str);
    static bool isJSONArr(const string& str);
    static ofFloatColor parseHexColor(const string& str);
    static float parsePercent(const ofxOscMessage& m, int idx);
    template<typename T>
    static T parse(const ofxOscMessage& m, int idx = 0);
    template<typename T>
    static vector<T> parseConst(const ofxOscMessage& m, int idx = 0);
    template<typename T>
    static vector<string> parseExpr(const ofxOscMessage& m, int& idx);
};
