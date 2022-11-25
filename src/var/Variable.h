#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "Sound.h"
#include "ofxTidalCycles.h"
#include "Value.h"

class TexData;

class Variable {
public:
    Variable() {}
    Variable(float value) {
        set(value);
    }
    void set(float value);
    void set(vector<float> value);
    void set(glm::vec3 value);
    void set(ofFloatColor value);
    void set(const ofxOscMessage& value, int idx = 1);
    void setColor(const ofxOscMessage& m, int idx = 1);
    void setVec3(const ofxOscMessage& m, int idx = 1);
    void setFloat(const ofxOscMessage& m, int idx = 1);
    void setRange(float to) {
        for (int i=0; i<values.size(); i++) {
            values[i].rangeTo = to;
        }
    }
    void setRange(float from, float to) {
        for (int i=0; i<values.size(); i++) {
            values[i].rangeFrom = from;
            values[i].rangeTo = to;
        }
    }
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes, TexData* data = NULL);
    float get(int idx = 0) {
        return values[idx].get();
    }
    vector<float> getVec(int idx = 0) {
        vector<float> vec;
        for (int i=0; i<values.size(); i++) {
            vec.push_back(get(i));
        }
        return vec;
    }
    ofFloatColor getColor() {
        return ofFloatColor(get(0), get(1), get(2), get(3));
    }
    void afterDraw();
    
    vector<Value> values;
    bool isColor = false;
    bool isVec3 = false;
private:
    void tween(const vector<float>& target, float dur, ofxeasing::function ease);
    void tween(const vector<float> &target, float dur) {
        tween(target, dur, ofxeasing::linear::easeNone);
    }
};
