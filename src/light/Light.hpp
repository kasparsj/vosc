#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "VarsHolder.h"

class Light : public VarsHolder {
public:
    Light();
    void set(const ofxOscMessage& m, int idx);
    void update();
    
private:
    ofLight light;
};
