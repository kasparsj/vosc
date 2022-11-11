#pragma once

#include "ofxOsc.h"
#include "TexData.h"

class Layer;

class LayerData : public TexData {
public:
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes, const map<string, VarConfig>& maps);
    void afterDraw();
    
    bool visible;
    
private:
    bool prevOnset;
};
