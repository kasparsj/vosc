#pragma once

#include "ofxOsc.h"

class Sound {
public:
    Sound() {
        
    }
    void parse(ofxOscMessage &m);
    
    int instNum;
    float amplitude;
    float loudness;
    int onset;
};