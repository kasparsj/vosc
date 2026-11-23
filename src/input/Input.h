#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "VarsHolder.h"

#ifdef TARGET_OSX
class AudioInputCapture;
#endif

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
#ifndef TARGET_OSX
    void audioIn(ofSoundBuffer& input);
#endif
    virtual void update();
    float& get() { return ampScaled; }
    bool configuredResourceReady() const;
    bool configuredResourcePending() const;
    std::string configuredResourceError() const;
    virtual void set(const ofxOscMessage& m, int idx = 1) = 0;
    virtual void oscCommand(const string& command, const ofxOscMessage& m) = 0;
    
    int instNum;
    bool requestedIdIsInteger = false;
    int requestedIntegerId = 0;
    string requestedDevice;
    int requestedChannels = 0;
    int requestedBufferSize = 256;
    float amplitude;
    float ampSmooth;
    float ampScaled;
    vector<float> ampHist;

private:
#ifdef TARGET_OSX
    std::shared_ptr<AudioInputCapture> audioCapture;
    uint64_t audioSequence = 0;
    bool reportedAudioError = false;
#else
    ofSoundStream soundStream;
#endif
};
