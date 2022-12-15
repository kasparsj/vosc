#pragma once

#include "ofMain.h"
#include "ofxOsc.h"

class Lights {
public:
    ofLight& create(const ofxOscMessage& m);
    void remove(const ofxOscMessage& m);
    void enable();
    void disable();
    
private:
    map<string, ofLight> lights;
};
