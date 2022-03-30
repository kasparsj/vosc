#pragma once

#include "ofConstants.h"
#include "Shader.h"
#include "ofxTidalCycles.h"
#include "SoundData.h"
#include "Config.h"
#include "Video.h"

class Visual {
public:
    void setup(int index, int numVisuals);
    void layout(Layout layout);
    void update(vector<SoundData> &soundData, vector<TidalNote> &notes);
    void draw();
    
    int index;
    int total;
    vector<string> datas;
    Video video;
    Shader shader;
    glm::vec2 pos;
    glm::vec2 size;
    int brightness = 0;
    Config config;
    bool isOnset = false;
    bool isTidal = false;
};
