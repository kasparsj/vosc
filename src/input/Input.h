#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "VarsHolder.h"

class Input : public VarsHolder {
public:
    Input() {
        setVar<float>("maxAmp", 1.0);
    }
//    void initAsMic() {
//        setVar<float>("maxAmp", 0.17);
//    }
    void setupStream(const ofxOscMessage &m);
    void audioIn(ofSoundBuffer& input);
    virtual void update();
    float& get() { return ampScaled; }
    virtual void set(const ofxOscMessage& m, int idx = 1) = 0;
    virtual void oscCommand(const string& command, const ofxOscMessage& m) = 0;
    
    int instNum;
    ofSoundStream soundStream;
    float amplitude;
    float ampSmooth;
    float ampScaled;
    vector<float> ampHist;
};
