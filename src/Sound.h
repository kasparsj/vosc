#pragma once

#include "ofxOsc.h"

#define MAX_LOUD 64

class Sound {
public:
    Sound() {
    }
    void parse(const ofxOscMessage &m);
    
    int instNum;
    float amplitude;
    float loudness;
    int onset;
    std::vector<float> mfcc;
    float maxAmp = 0.7;
    float maxLoud = MAX_LOUD;
    
};
