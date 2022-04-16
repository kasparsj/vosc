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
        unload();
    }
    void setup(int index, int numVisuals, string dataSource = "");
    void layout(Layout layout);
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    void draw(const glm::vec3 &pos, const glm::vec3 &size);
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
    LayerData *data = NULL;
    Gen *gen = NULL;
    glm::vec3 pos;
    glm::vec3 size;
    float rotAngle;
    glm::vec3 rotAxis = glm::vec3(0, 1, 0);
    ofFloatColor color = ofFloatColor(0, 0);
    float speed = 1.f;
    int behaviour = -1;
    float bri = 1.0;
    float alpha = 1.0;
    bool aspectRatio = true;
    float timeNorm = 0;
    bool noClear;
    bool useMFCCColor = false;
    bool isTidal = false;
    int randomSeed = 0;
    vector<string> dataSources;
    float thresh = 0.5;
    float onsetThresh = 0.5;
};
