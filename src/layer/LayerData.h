#pragma once

#include "ofxOsc.h"
#include "TexData.h"

class Layer;

class LayerData : public TexData {
public:
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    
    bool visible;
    
private:
    bool prevOnset;
};
