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
    void tweenVec3(const ofxOscMessage& m, int idx = 1, function<void()> onComplete = function<void()>());
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
    void update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes, TexData* data = NULL);
    float get(int idx = 0) const {
        return values[idx].get();
    }
    int getInt(int idx = 0) const {
        return (int) get(idx);
    }
    bool getBool(int idx = 0) const {
        return getInt(idx) > 0;
    }
    vector<float> getVec() const {
        vector<float> vec;
        for (int i=0; i<values.size(); i++) {
            vec.push_back(get(i));
        }
        return vec;
    }
    glm::vec3 getVec3() const {
        return glm::vec3(get(0), get(1), get(2));
    }
    ofFloatColor getColor() const {
        return ofFloatColor(get(0), get(1), get(2), get(3));
    }
    void afterDraw();
    
    vector<Value> values;
    bool isColor = false;
    bool isVec3 = false;
private:
    void init(const ofxOscMessage& value, int idx = 1);
    void setRange(const ofxOscMessage& value, int idx = 1);
    void setSpeed(const ofxOscMessage& m, int idx = 1);
    void setValue(const ofxOscMessage& m, int idx = 1);
    void tween(const vector<float>& target, float dur, function<void()> onComplete, ofxeasing::function ease);
    void tween(const vector<float> &target, float dur, function<void()> onComplete) {
        tween(target, dur, onComplete, ofxeasing::linear::easeNone);
    }
    void tween(const vector<float> &target, float dur) {
        tween(target, dur, function<void()>(), ofxeasing::linear::easeNone);
    }
};
