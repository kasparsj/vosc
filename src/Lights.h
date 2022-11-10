#pragma once

#include "ofMain.h"
#include "ofxOsc.h"

class Lights {
public:
    void create(const ofxOscMessage& m);
    void enable();
    void disable();
    
private:
    map<string, ofLight> lights;
};
