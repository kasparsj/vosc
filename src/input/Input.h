#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "VarsHolder.h"

class Input : public VarsHolder {
public:
    Input() {
        setVar<float>("maxAmp", 1.0);
        instNum = 0;
        amplitude = 0.f;
        ampSmooth = 0.f;
        ampScaled = 0.f;
    }
    virtual ~Input();
//    void initAsMic() {
//        setVar<float>("maxAmp", 0.17);
//    }
    void setupStream(const ofxOscMessage &m);
    void audioIn(ofSoundBuffer& input);
    virtual void update();
    float& get() { return ampScaled; }
    bool configuredResourceReady() const {
        return requestedChannels == 0 || soundStream.getNumInputChannels() == requestedChannels;
    }
    virtual void set(const ofxOscMessage& m, int idx = 1) = 0;
    virtual void oscCommand(const string& command, const ofxOscMessage& m) = 0;
    
    int instNum;
    bool requestedIdIsInteger = false;
    int requestedIntegerId = 0;
    string requestedDevice;
    int requestedChannels = 0;
    int requestedBufferSize = 256;
    ofSoundStream soundStream;
    float amplitude;
    float ampSmooth;
    float ampScaled;
    vector<float> ampHist;
};
