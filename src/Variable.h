#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "Sound.h"
#include "ofxTidalCycles.h"

class Variable {
public:
    Variable() {}
    Variable(string type, float value, float scale = 1.f, float rangeFrom = 0.f, float rangeTo = 1.f) : scale(scale), rangeFrom(rangeFrom), rangeTo(rangeTo) {
        init(type, value);
    }
    Variable(string type, vector<float> value, float scale = 1.f, float rangeFrom = 0.f, float rangeTo = 1.f) : type(type), scale(scale), rangeFrom(rangeFrom), rangeTo(rangeTo) {
        init(type, value);
    }
    Variable(float value) {
        init(value);
    }
    void init(string type, float value = 0.f);
    void init(string type, vector<float>& values);
    void init(float value);
    void init(vector<float> value);
    void init(glm::vec3 value);
    void init(ofFloatColor value);
    void init(const ofxOscMessage& value);
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    float get(int index = 0) {
        return (rangeFrom + values[index] * (rangeTo - rangeFrom)) * scale;
    }
    
    string type = "";
    vector<float> values;
    float scale = 1.f;
    float rangeFrom = 0.f;
    float rangeTo = 1.f;
    bool isColor;
    bool isVec3;
protected:
    float updateValue(const vector<Sound> &sounds, int idx = 0);
    
};
