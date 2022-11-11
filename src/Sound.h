#pragma once

#include "ofMain.h"
#include "ofxOsc.h"

#define MAX_LOUD 64

class Sound {
public:
    Sound() {}
    void update();
    void parse(const ofxOscMessage &m);
    void stream(const ofxOscMessage &m);
    void audioIn(ofSoundBuffer& input);
    
    ofSoundStream soundStream;
    int instNum;
    float volume;
    float volumeSmooth;
    float volumeScaled;
    vector<float> volumeHist;
    float amplitude;
    float loudness;
    int onset;
    std::vector<float> mfcc;
    float maxVol = 0.17;
    float maxAmp = 0.7;
    float maxLoud = MAX_LOUD;
    
};
