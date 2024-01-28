#pragma once

#include "Input.h"

#define MAX_AMP 0.7
#define MAX_LOUD 64

class OSCInput : public Input {
public:
//    void initAsSC() {
//        setVar<float>("maxAmp", MAX_AMP);
//        setVar<float>("maxLoud", MAX_LOUD);
//    }
    void set(const ofxOscMessage& m, int idx = 1) override;
    void oscCommand(const string& command, const ofxOscMessage& m) override;
    
    float loudness;
    int onset;
    vector<float> mfcc;
    
};
