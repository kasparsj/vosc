#pragma once

#include "Config.h"
#include "Sound.h"
#include "ofxTidalCycles.h"
#include "ofxOsc.h"
#include "TexData.h"

class Layer;

class LayerData : public TexData {
public:
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    void afterDraw();
    
    Layer *layer;
    bool visible;
    bool prevOnset;
    bool tidal;
};
