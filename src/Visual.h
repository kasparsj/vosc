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
        if (data != NULL) {
            delete data;
            data = NULL;
        }
    }
    void setup(int index, int numVisuals, string dataSource = "");
    void layout(Layout layout);
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes, const Config &config);
    void draw();
    void setDataSources(vector<string> ds);
    void addDataSources(vector<string> ds);
    void unload();
    
    int index;
    int total;
    VisualData *data;
    Video video;
    Shader shader;
    Sketch sketch;
    glm::vec2 pos;
    glm::vec2 size;
    ofFloatColor color = ofFloatColor(0);
    bool useMFCC = false;
    bool isTidal = false;
    Config config = Config(0);
private:
    vector<string> dataSources;
};
