#pragma once

#include "Mic.h"

#define MAX_AMP 0.7
#define MAX_LOUD 64

class Sound : public Mic {
public:
    Sound() {
        maxAmp = MAX_AMP;
        maxLoud = MAX_LOUD;
    }
    void parse(const ofxOscMessage &m);
    
    float loudness;
    int onset;
    vector<float> mfcc;
    float maxLoud;
    
};
