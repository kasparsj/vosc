#pragma once

#include "ofConstants.h"
#include "Shader.h"
#include "ofxTidalCycles.h"
#include "SoundData.h"
#include "Config.h"

enum Behaviour {
    B_RANDOM_SHADER = 1,
};

class Visual {
public:
    void update(vector<SoundData> &soundData, vector<TidalNote> &notes, Config &config);
    void draw();
    
    vector<string> sources;
    Shader shader;
    glm::vec2 pos;
    glm::vec2 size;
    int brightness = 0;
    int behaviour = 0;
};
