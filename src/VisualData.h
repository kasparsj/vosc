#pragma once

#include "Config.h"
#include "Sound.h"
#include "ofxTidalCycles.h"

class VisualData {
public:
    VisualData(const int index, const glm::vec2 &pos, const glm::vec2 &size, const Config &config) : index(index), pos(pos), size(size), config(config) {
        
    }
    void update(const vector<string> dataSources, const vector<Sound> &sounds, const vector<TidalNote> &notes, const Config &globalConfig);
    void afterDraw();
    
    const int index;
    const glm::vec2 &pos;
    const glm::vec2 &size;
    const Config &config;
    Config mergedConfig;
    vector<float> values;
    bool visible;
    bool onset;
    bool tidal;
};
