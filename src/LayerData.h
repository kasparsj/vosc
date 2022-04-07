#pragma once

#include "Config.h"
#include "Sound.h"
#include "ofxTidalCycles.h"

class Layer;

class LayerData {
public:
    LayerData(Layer *layer) : layer(layer) {}
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    void reset() {
        onset = false;
        values.clear();
        mfcc.clear();
        mfccColor = ofFloatColor(0);
    }
    
    ofFloatColor getMFCCColor() {
        if (mfccColor == ofFloatColor(0)) {
            int k = (mfcc.size() / 3);
            for (int i=0; i<mfcc.size(); i++) {
                int j = MIN(2, i / k);
                int k2 = j < 2 ? k : mfcc.size() - k*2;
                mfccColor[j] += (mfcc[i] * 1.f/k2);
            }
        }
        return mfccColor;
    }
    
    void afterDraw();
    
    const Layer *layer;
    float time = 0;
    float prevTime = 0;
    vector<float> values;
    bool visible;
    bool onset;
    bool tidal;
    vector<float> mfcc;
    ofFloatColor mfccColor = ofFloatColor(0);
};
