#pragma once

#include "Config.h"
#include "Sound.h"
#include "ofxTidalCycles.h"

class LayerData {
public:
    LayerData(const int index, const glm::vec2 &pos, const glm::vec2 &size, ofFloatColor &color, bool &useMFCC, const Config &config) :
        index(index), pos(pos), size(size), color(color), useMFCC(useMFCC), config(config) {
        
    }
    void update(const vector<string> dataSources, const vector<Sound> &sounds, const vector<TidalNote> &notes, const Config &globalConfig);
    void afterDraw();
    
    ofFloatColor getColor() {
        if (useMFCC) {
            return mfcc;
        }
        return color;
    }
    
    const int index;
    const glm::vec2 &pos;
    const glm::vec2 &size;
    ofFloatColor &color;
    bool &useMFCC;
    const Config &config;
    Config mergedConfig;
    vector<float> values;
    bool visible;
    bool onset;
    bool tidal;
    ofFloatColor mfcc;
    float time = 0;
};
