#pragma once

#include "ofConstants.h"
#include "Shader.h"
#include "ofxTidalCycles.h"
#include "Sound.h"
#include "Config.h"
#include "Video.h"
#include "VisualData.h"
#include "Sketch.h"

class Visual {
public:
    ~Visual() {
        if (shaderData != NULL) {
            delete shaderData;
            shaderData = NULL;
        }
    }
    void setup(int index, int numVisuals, string data = "");
    void layout(Layout layout);
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes, const Config &config);
    void draw();
    
    int index;
    int total;
    vector<string> sources;
    Video video;
    Shader shader;
    Sketch sketch;
    glm::vec2 pos;
    glm::vec2 size;
    bool isTidal = false;
    Config config = Config(0);
    VisualData *shaderData;
};
