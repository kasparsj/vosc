#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "VarsHolder.h"

class Buffer {
public:
    Buffer(string name, const ofxOscMessage& m, int arg, VarsHolder* parent);
    void update();
    const ofTexture& getTexture() const {
        return tex;
    }
    ofTexture& getTexture() {
        return tex;
    }
    
    BaseVar* var;
    ofBufferObject buf;
    ofTexture tex;
    BufData data;
};
