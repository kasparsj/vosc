#pragma once

#include "ofConstants.h"
#include "ofxTidalCycles.h"
#include "Sound.h"
#include "Config.h"
#include "LayerData.h"
#include "Gen.h"

class Layer {
public:
    static Gen* factory(string type, string name);
    static Gen* factory(string source);
    
    ~Layer() {
        if (data != NULL) {
            delete data;
            data = NULL;
        }
    }
    void setup(int index, int numVisuals, string dataSource = "");
    void layout(Layout layout);
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    void draw(float left, float top, float width, float height);
    void draw();
    void setDataSources(vector<string> ds);
    void addDataSources(vector<string> ds);
    void load(string source);
    void choose(string type = "");
    void unload();
    void reload();
    void clear();
    void reset();
    
    ofFloatColor getColor() {
        if (useMFCCColor && data->mfcc.size() > 0) {
            return data->getMFCCColor();
        }
        return color;
    }
    
    bool randomShader() const {
        return behaviour > 0 && behaviour & B_RANDOM_SHADER;
    }
    
    int index;
    int total;
    LayerData *data;
    Gen *gen;
    glm::vec2 pos;
    glm::vec2 size;
    ofFloatColor color = ofFloatColor(0, 0);
    float speed = 1.f;
    int behaviour = -1;
    float bri = 1.0;
    float alpha = 1.0;
    float timeNorm = 0;
    bool noClear;
    bool useMFCCColor = false;
    bool isTidal = false;
    int randomSeed = 0;
    vector<string> dataSources;
};
