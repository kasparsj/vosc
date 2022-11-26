#pragma once

#include "ofMain.h"
#include "ofxOsc.h"

class Mic {
public:
    Mic() {
        maxAmp = 0.17;
    }
    void setupStream(const ofxOscMessage &m);
    void audioIn(ofSoundBuffer& input);
    virtual void update();
    
    int instNum;
    ofSoundStream soundStream;
    float amplitude;
    float ampSmooth;
    float ampScaled;
    vector<float> ampHist;
    float maxAmp;
};
