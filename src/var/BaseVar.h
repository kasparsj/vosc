#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "Sound.h"
#include "ofxTidalCycles.h"

class TexData;

class BaseVar {
public:
    virtual void update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes, TexData* data = NULL) = 0;
    virtual void afterDraw() = 0;
};
