#pragma once

#include "ofxOsc.h"

class SoundData {
public:
    SoundData() {
        
    }
    void parse(ofxOscMessage &m);
    
    int instNum;
    float amplitude;
    float loudness;
    int onset;
};
