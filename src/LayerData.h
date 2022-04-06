#pragma once

#include "Config.h"
#include "Sound.h"
#include "ofxTidalCycles.h"

class Layer;

class LayerData {
public:
    LayerData(Layer *layer) : layer(layer) {
        
    }
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes, const Config &globalConfig);
    void afterDraw();
    
    const Layer const *layer;
    float time = 0;
    float prevTime = 0;
    vector<float> values;
    bool visible;
    bool onset;
    bool tidal;
    ofFloatColor mfcc;
};
