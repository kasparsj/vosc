#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "ofxTidalCycles.h"

class OSCInput;

struct BufData {
    GLsizeiptr bytes;
    const void * data;
};

class BaseVar {
public:
    virtual void update(const vector<OSCInput> &inputs, const vector<TidalNote> &notes) = 0;
    virtual void afterDraw() = 0;
    virtual BufData asBufferData() = 0;
};
