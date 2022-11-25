#pragma once

#include "ofMain.h"
#include "Sound.h"
#include "ofxTidalCycles.h"
#include "ofxOsc.h"
#include "Args.h"

class Layer;

class Value {
public:
    float get() {
        return ofMap(value, 0.f, 1.f, rangeFrom, rangeTo);
    }
    void set(string type);
    void set(float value);
    void set(const ofxOscMessage& m, int i);
    void tween(float target, float dur, ofxeasing::function ease) {
        Args::getInstance().createTween(&value, target, dur, ease);
    }
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes, int index, int total, Layer* layer = NULL);
    void update(const vector<Sound> &sounds, int index, int total, Layer* layer = NULL);
    void afterDraw();
    
    int index;
    string type = "";
    string subtype = "";
    int chan;
    float value;
    float rangeFrom = 0.f;
    float rangeTo = 1.f;
    float speed = 1.f;
};
