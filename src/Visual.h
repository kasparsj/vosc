#pragma once

#include "ofConstants.h"
#include "Shader.h"
#include "ofxTidalCycles.h"
#include "SoundData.h"
#include "Config.h"
#include "Video.h"

enum Behaviour {
    B_RANDOM_SHADER = 1,
};

class Visual {
public:
    void setup(int index, int numVisuals);
    void update(vector<SoundData> &soundData, vector<TidalNote> &notes);
    void draw();
    
    int index;
    vector<string> datas;
    Video video;
    Shader shader;
    glm::vec2 pos;
    glm::vec2 size;
    int brightness = 0;
    Config config;
};
