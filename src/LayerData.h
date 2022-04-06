#pragma once

#include "Config.h"
#include "Sound.h"
#include "ofxTidalCycles.h"

class LayerData {
public:
    LayerData(const int index, const glm::vec2 &pos, const glm::vec2 &size, ofFloatColor &color, float &speed, int behaviour, bool &useMFCC, const Config &config) :
        index(index), pos(pos), size(size), color(color), speed(speed), behaviour(behaviour), useMFCC(useMFCC), config(config) {
        
    }
    void update(const vector<string> dataSources, const vector<Sound> &sounds, const vector<TidalNote> &notes, const Config &globalConfig);
    void afterDraw();
    
    ofFloatColor getColor() {
        if (useMFCC) {
            return mfcc;
        }
        return color;
    }
    
    bool randomShader() const {
        return behaviour > 0 && behaviour & B_RANDOM_SHADER;
    }
    
    const int index;
    const glm::vec2 &pos;
    const glm::vec2 &size;
    ofFloatColor &color;
    float &speed;
    int &behaviour;
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
