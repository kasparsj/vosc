#pragma once

#include "ofMain.h"
#include "Sound.h"
#include "ofxTidalCycles.h"
#include "ofxOsc.h"
#include "Args.h"

class TexData;

class Value {
public:
    float get() const {
        return ofMap(value, 0.f, 1.f, rangeFrom, rangeTo);
    }
    void set(string type);
    void set(float value);
    void set(const ofxOscMessage& m, int i);
    void tween(float target, float dur, function<void()> onComplete, ofxeasing::function ease) {
        Args::get().createTween(&value, target, dur, onComplete, ease);
    }
    void update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes, int index, int total, TexData* data = NULL);
    void update(const vector<Mic> &mics, const vector<Sound> &sounds, int index, int total, TexData* data = NULL);
    void afterDraw();
    
    int index;
    string type = "";
    string subtype = "";
    string ref = "";
    int chan = -1;
    float value;
    float rangeFrom = 0.f;
    float rangeTo = 1.f;
    float speed = 1.f;
};
