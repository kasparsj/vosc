#pragma once

#include "ofConstants.h"
#include "Shader.h"
#include "ofxTidalCycles.h"
#include "Sound.h"
#include "Config.h"
#include "Video.h"

class Visual {
public:
    void setup(int index, int numVisuals, string data = "");
    void layout(Layout layout);
    void update(vector<Sound> &soundData, vector<TidalNote> &notes, const Config &config);
    void draw();
    
    int index;
    int total;
    vector<string> datas;
    Video video;
    Shader shader;
    glm::vec2 pos;
    glm::vec2 size;
    int brightness = 0;
    Config config = Config(0);
    bool isOnset = false;
    bool isTidal = false;
};
