#pragma once

#include "ofxOsc.h"

#define MAX_LOUD 64

class Sound {
public:
    Sound() {
    }
    void parse(const ofxOscMessage &m);
    
    float getThreshAmp() const {
        if (threshAmp < 0) {
            return maxAmp / 2.f;
        }
        return threshAmp;
    }
    
    float getThreshLoud() const {
        if (threshLoud < 0) {
            return (maxLoud / MAX_LOUD) / 2;
        }
        return threshLoud;
    }
    
    int instNum;
    float amplitude;
    float loudness;
    int onset;
    std::vector<float> mfcc;
    float maxAmp = 0.7;
    float maxLoud = MAX_LOUD;
    float threshAmp = -1;
    float threshLoud = -1;
    
};
